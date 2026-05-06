//	AltirraSDL - Background lobby HTTP worker (impl)
//
//	Two implementations under one header:
//
//	  Native (Win/Mac/Linux/Android):
//	    A dedicated std::thread runs LobbyClient::*() (which uses
//	    http_minimal.cpp on raw POSIX/Winsock sockets).  Synchronous
//	    inside the thread, async at the API surface (Post / Poll).
//
//	  WASM (Emscripten):
//	    No thread.  Each Post() fires `emscripten_fetch_t` async and
//	    the success/failure callback enqueues the result.  Same API
//	    surface, same LobbyResult shape — the UI doesn't notice.
//	    The browser main thread can't do synchronous fetch, and
//	    Asyncify would cost a ~1.5x WASM size penalty for every C++
//	    function on the call stack at await time, so we skip it.

#include <stdafx.h>

#include "ui_netplay_lobby_worker.h"

#include "netplay/nat_discovery.h"
#include "netplay/port_mapping.h"
#include "netplay/lobby_protocol.h"
#include "netplay/json_cursor.h"

#include <at/atcore/logging.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(__EMSCRIPTEN__)
#include <emscripten/fetch.h>
#endif

extern ATLogChannel g_ATLCNetplay;

namespace ATNetplayUI {

LobbyWorker::LobbyWorker() = default;

LobbyWorker::~LobbyWorker() { Stop(); }

#if !defined(__EMSCRIPTEN__)
// -----------------------------------------------------------------
// Native: thread-backed implementation.
// -----------------------------------------------------------------

void LobbyWorker::Start() {
	if (mRunning.exchange(true)) return;
	mStop.store(false);
	mThread = std::thread(&LobbyWorker::ThreadMain, this);
}

void LobbyWorker::Stop() {
	if (!mRunning.exchange(false)) {
		if (mThread.joinable()) mThread.join();
		return;
	}
	mStop.store(true);
	mCv.notify_all();
	if (mThread.joinable()) mThread.join();

	// Drain remaining queues so subsequent Poll() sees no stale data.
	std::lock_guard<std::mutex> lk(mMu);
	mInQueue.clear();
	mOutQueue.clear();
	mInFlight.store(0);
}

bool LobbyWorker::Post(LobbyRequest req, const std::string& source) {
	if (!mRunning.load()) return false;
	{
		std::lock_guard<std::mutex> lk(mMu);
		mInQueue.push_back({std::move(req), source});
		++mInFlight;
	}
	mCv.notify_one();
	return true;
}

void LobbyWorker::Poll(const std::function<void(LobbyResult&)>& onResult) {
	std::deque<LobbyResult> local;
	{
		std::lock_guard<std::mutex> lk(mMu);
		local.swap(mOutQueue);
	}
	for (auto& r : local) onResult(r);
}

size_t LobbyWorker::InFlightCount() const { return mInFlight.load(); }

void LobbyWorker::ThreadMain() {
	ATNetplay::LobbyClient client;
	for (;;) {
		Queued q;
		{
			std::unique_lock<std::mutex> lk(mMu);
			mCv.wait(lk, [&]{ return mStop.load() || !mInQueue.empty(); });
			if (mStop.load() && mInQueue.empty()) return;
			q = std::move(mInQueue.front());
			mInQueue.pop_front();
		}

		client.SetEndpoint(q.req.endpoint);
		LobbyResult out{};
		out.op          = q.req.op;
		out.tag         = q.req.tag;
		out.coordGen    = q.req.coordGen;
		out.sourceLobby = q.source;

		switch (q.req.op) {
			case LobbyOp::List:
				out.ok = client.List(out.sessions);
				// LobbyClient stamps the GET RTT regardless of HTTP
				// status; copy it through so the UI can render
				// "Lobby ping: 45 ms" even on a partial parse.
				out.listLatencyMs = client.LastListLatencyMs();
				break;
			case LobbyOp::Create: {
#if !defined(__EMSCRIPTEN__)
				// v3 NAT traversal: right before posting Create, probe
				// the lobby's UDP reflector to learn the srflx (public)
				// endpoint the host's NAT assigns to the game socket.
				// This is the ONLY candidate that works for cross-
				// internet joiners; failing to probe is survivable —
				// we fall back to LAN + loopback candidates which still
				// cover same-LAN + same-box cases.
				//
				// The probe uses the bound game port with SO_REUSEADDR
				// so it observes the SAME NAT mapping the game socket
				// will use (on cone-NAT routers — symmetric NATs can't
				// be helped at this layer).
				//
				// Infer the game port from the legacy hostEndpoint
				// (which ui_netplay_actions built as "ip:PORT").
				//
				// WASM build skips this entire block: browsers can't
				// open UDP sockets, NAT-PMP, or run a reflector probe.
				// WASM hosts publish wssRelayOnly=true and route their
				// traffic through the lobby's WS bridge instead.
				uint16_t gamePort = 0;
				{
					const std::string& ep = q.req.createReq.hostEndpoint;
					auto colon = ep.rfind(':');
					if (colon != std::string::npos) {
						gamePort = (uint16_t)std::atoi(ep.c_str() + colon + 1);
					}
				}
				if (gamePort != 0) {
					// Step A: try NAT-PMP / PCP router-assisted port
					// mapping first — this is the BitTorrent-style
					// auto-forward that makes NAT traversal Just Work
					// on the majority of consumer routers.  When it
					// succeeds, the router installs a permanent
					// external→internal port-forward; we publish the
					// resulting external endpoint and no hole-punching
					// is ever needed.  Fall through silently on any
					// failure (router doesn't speak NAT-PMP, timeout,
					// etc.) — reflector probe still gives us a working
					// srflx for full-cone NATs.
					ATNetplay::PortMapping mapping;
					std::string mapErr;
					bool mapped = ATNetplay::RequestUdpPortMapping(
						gamePort,
						/*lifetimeSec=*/7200,    // 2 h; ReconcileHostedGames
						                         // refreshes at half-lifetime,
						                         // so the mapping stays live
						                         // for arbitrarily long
						                         // sessions.  Many routers
						                         // clamp to their own max
						                         // (commonly 7200 or 86400).
						mapping, mapErr);
					if (mapped && !mapping.externalIp.empty() &&
					    mapping.externalPort != 0) {
						// Propagate the mapping back so the main
						// thread stores it on HostedGame — the
						// session-delete path releases it via
						// ATNetplay::ReleaseUdpPortMapping.
						out.natPmpProtocol     = mapping.protocol;
						out.natPmpExternalIp   = mapping.externalIp;
						out.natPmpExternalPort = mapping.externalPort;
						out.natPmpInternalPort = mapping.internalPort;
						out.natPmpLifetimeSec  = mapping.lifetimeSec;
						char mapped_ep[64];
						std::snprintf(mapped_ep, sizeof mapped_ep,
							"%s:%u",
							mapping.externalIp.c_str(),
							(unsigned)mapping.externalPort);
						auto& cs = q.req.createReq.candidates;
						bool already = false;
						for (const auto& c : cs)
							if (c == mapped_ep) { already = true; break; }
						if (!already) {
							// Insert the mapped endpoint FIRST —
							// router-mapped addresses are the most
							// reliable path (no hole-punching needed)
							// so joiners should try them first.
							cs.insert(cs.begin(), mapped_ep);
						}
						// Use the router-mapped endpoint as the
						// legacy hostEndpoint too — it's the most
						// reachable single address.
						q.req.createReq.hostEndpoint = mapped_ep;
						g_ATLCNetplay(
							"lobby Create: %s mapped %u → %s "
							"(lease %us) — router-assisted forward "
							"active",
							mapping.protocol.c_str(),
							(unsigned)mapping.internalPort,
							mapped_ep,
							(unsigned)mapping.lifetimeSec);
					} else {
						g_ATLCNetplay(
							"lobby Create: NAT-PMP/PCP unavailable "
							"(%s) — falling back to reflector probe",
							mapErr.empty() ? "no response" : mapErr.c_str());
					}

					// Step B: reflector probe (STUN-lite) gives us
					// the srflx endpoint even when the router doesn't
					// speak NAT-PMP.  Works for full-cone NATs; a
					// no-op for symmetric NATs.  Always run it even
					// after NAT-PMP succeeded — the srflx is a second
					// opinion that helps diagnose mis-mapping.
					ATNetplay::ReflectorProbe probe;
					std::string srflx, err;
					bool ok = probe.Run(
						q.req.endpoint.host.c_str(),
						ATLobby::kReflectorPortDefault,
						gamePort,
						/*timeoutMs=*/1500,
						srflx, err);
					if (ok && !srflx.empty()) {
						// Avoid dup if the reflector observes our LAN
						// IP (no NAT, e.g. the host sits directly on
						// the same public network as the lobby).
						bool already = false;
						for (const auto& c : q.req.createReq.candidates) {
							if (c == srflx) { already = true; break; }
						}
						if (!already) {
							// Insert srflx AFTER LAN but BEFORE loopback —
							// LAN is best for same-network joiners, srflx
							// is needed for cross-internet, loopback is
							// last-resort for same-box testing.
							auto& cs = q.req.createReq.candidates;
							auto ins = cs.end();
							if (!cs.empty() &&
							    cs.back().rfind("127.", 0) == 0) {
								ins = cs.end() - 1;
							}
							cs.insert(ins, srflx);
						}
						// Upgrade the legacy hostEndpoint field from
						// LAN→srflx so v2 clients (which don't read
						// candidates) still get a reachable address
						// when joining across the internet.  v3
						// clients consult the candidates list and
						// will still try LAN first.
						q.req.createReq.hostEndpoint = srflx;
						g_ATLCNetplay(
							"lobby Create: reflector yielded srflx=%s "
							"(also used as legacy hostEndpoint)",
							srflx.c_str());
					} else {
						g_ATLCNetplay(
							"lobby Create: reflector probe failed "
							"(%s) — no srflx candidate",
							err.empty() ? "timeout" : err.c_str());
					}
				}
#endif  // !__EMSCRIPTEN__
				out.ok = client.Create(q.req.createReq, out.create);
				break;
			}
			case LobbyOp::Heartbeat:
				// v4: collect peer hints alongside the regular
				// heartbeat result so the host can drive
				// Coordinator::IngestPeerHint on the next UI tick.
				out.ok = client.HeartbeatWithHints(
					q.req.sessionId, q.req.token,
					q.req.playerCount, q.req.state, out.hints);
				break;
			case LobbyOp::Delete:
				out.ok = client.Delete(q.req.sessionId, q.req.token);
				break;
			case LobbyOp::Stats:
				out.ok = client.Stats(out.stats);
				break;
			case LobbyOp::GetById: {
				// Deep-link single-session fetch.  The session id was
				// supplied via req.sessionId; result goes into the
				// usual sessions vector so the response handler can
				// reuse the List-style code path.
				ATNetplay::LobbySession one;
				out.ok = client.GetById(q.req.sessionId, one);
				if (out.ok) {
					out.sessions.clear();
					out.sessions.push_back(std::move(one));
				}
				out.httpStatus = client.LastStatus();
				if (!out.ok) {
					out.error = client.LastError();
				}
				break;
			}
			case LobbyOp::PeerHint: {
				// req.sessionId  = target host's lobby session id
				// req.token      = joiner sessionNonce hex (32 chars)
				// req.state      = joiner handle
				// req.createReq.candidates[0] = candidates (already
				//                               srflx-enriched by the
				//                               main thread before
				//                               JoinerSetRelayContext)
				std::string cands;
				if (!q.req.createReq.candidates.empty())
					cands = q.req.createReq.candidates.front();
				out.ok = client.PostPeerHint(q.req.sessionId,
					q.req.state, q.req.token, cands);
				break;
			}
			case LobbyOp::PortMapRefresh: {
#if !defined(__EMSCRIPTEN__)
				// Re-request the NAT-PMP / PCP mapping for the
				// already-bound internal port.  The router may:
				//   - return the same (internal, external) pair with a
				//     fresh lease → happy path, no joiner-visible
				//     change
				//   - hand us a different external port (if someone
				//     else squatted our old one during the lease) → we
				//     report the new mapping and the main thread
				//     updates the HostedGame / hostEndpoint; joiners
				//     re-fetching the lobby listing pick up the new
				//     endpoint
				//   - refuse → we surface the error; the OLD mapping
				//     may still be live on the router until its own
				//     lease expires, so current joiners aren't affected
				ATNetplay::PortMapping mapping;
				std::string err;
				bool mapped = ATNetplay::RequestUdpPortMapping(
					q.req.portRefreshInternalPort,
					q.req.portRefreshLifetimeSec
						? q.req.portRefreshLifetimeSec
						: 7200,
					mapping, err);
				out.ok = mapped;
				if (mapped) {
					out.natPmpProtocol     = mapping.protocol;
					out.natPmpExternalIp   = mapping.externalIp;
					out.natPmpExternalPort = mapping.externalPort;
					out.natPmpInternalPort = mapping.internalPort;
					out.natPmpLifetimeSec  = mapping.lifetimeSec;
				} else {
					out.error = err.empty() ? "no response" : err;
				}
#else
				// WASM: there's no NAT to refresh and no router to
				// talk to.  Surface a static "not applicable" error so
				// the UI can render something sensible if it ever
				// schedules this op in a WASM build.
				out.ok = false;
				out.error = "NAT-PMP unavailable in WASM build";
#endif  // !__EMSCRIPTEN__
				break;
			}
		}
		if (!out.ok) out.error = client.LastError();
		out.httpStatus = client.LastStatus();

		{
			std::lock_guard<std::mutex> lk(mMu);
			mOutQueue.push_back(std::move(out));
			--mInFlight;
		}
	}
}

#else  // __EMSCRIPTEN__

// -----------------------------------------------------------------
// WASM (Emscripten): emscripten_fetch_t-backed implementation.
// -----------------------------------------------------------------
//
// No thread, no LobbyClient (its http_minimal backend uses raw POSIX
// sockets which Emscripten doesn't simulate).  Each Post() builds the
// JSON body inline and fires emscripten_fetch_t asynchronously.  The
// success/error callback parses the response (using json_cursor.h —
// same parser the native path uses, kept transport-agnostic) and
// pushes a LobbyResult into mOutQueue.  Poll() drains as on native.

namespace {

// ---- JSON body builders (mirrors lobby_client.cpp's anonymous-ns
// helpers byte-for-byte; reimplemented locally so the WASM TU does
// not need to link http_minimal.cpp).

void AppendJsonString(std::string& out, const std::string& s) {
	out.push_back('"');
	for (char ch : s) {
		unsigned char c = (unsigned char)ch;
		switch (c) {
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\b': out += "\\b";  break;
			case '\f': out += "\\f";  break;
			case '\n': out += "\\n";  break;
			case '\r': out += "\\r";  break;
			case '\t': out += "\\t";  break;
			default:
				if (c < 0x20) {
					char tmp[8];
					std::snprintf(tmp, sizeof tmp, "\\u%04X", (unsigned)c);
					out += tmp;
				} else {
					out.push_back((char)c);
				}
				break;
		}
	}
	out.push_back('"');
}

void AppendKV(std::string& out, const char* key,
              const std::string& v, bool& first) {
	if (!first) out.push_back(',');
	first = false;
	out.push_back('"'); out.append(key); out += "\":";
	AppendJsonString(out, v);
}

void AppendKV(std::string& out, const char* key, int v, bool& first) {
	if (!first) out.push_back(',');
	first = false;
	char tmp[32];
	std::snprintf(tmp, sizeof tmp, "%d", v);
	out.push_back('"'); out.append(key); out += "\":"; out.append(tmp);
}

void AppendKV(std::string& out, const char* key, bool v, bool& first) {
	if (!first) out.push_back(',');
	first = false;
	out.push_back('"'); out.append(key); out += "\":";
	out += v ? "true" : "false";
}

std::string BuildCreateBody(const ATNetplay::LobbyCreateRequest& req) {
	std::string body;
	body.reserve(256);
	body.push_back('{');
	bool first = true;
	AppendKV(body, "cartName",        req.cartName,        first);
	AppendKV(body, "hostHandle",      req.hostHandle,      first);
	AppendKV(body, "hostEndpoint",    req.hostEndpoint,    first);
	std::string joined;
	for (size_t i = 0; i < req.candidates.size(); ++i) {
		if (i) joined.push_back(';');
		joined += req.candidates[i];
	}
	AppendKV(body, "candidates",      joined,              first);
	AppendKV(body, "region",          req.region,          first);
	AppendKV(body, "playerCount",     req.playerCount,     first);
	AppendKV(body, "maxPlayers",      req.maxPlayers,      first);
	AppendKV(body, "protocolVersion", req.protocolVersion, first);
	AppendKV(body, "visibility",      req.visibility,      first);
	AppendKV(body, "requiresCode",    req.requiresCode,    first);
	AppendKV(body, "cartArtHash",     req.cartArtHash,     first);
	AppendKV(body, "kernelCRC32",     req.kernelCRC32,     first);
	AppendKV(body, "basicCRC32",      req.basicCRC32,      first);
	AppendKV(body, "hardwareMode",    req.hardwareMode,    first);
	AppendKV(body, "videoStandard",   req.videoStandard,   first);
	AppendKV(body, "memoryMode",      req.memoryMode,      first);
	if (req.wssRelayOnly)
		AppendKV(body, "wssRelayOnly",  true,                first);
	body.push_back('}');
	return body;
}

std::string BuildHeartbeatBody(const std::string& token,
                               int playerCount,
                               const std::string& state) {
	std::string body;
	body.reserve(128);
	body.push_back('{');
	bool first = true;
	AppendKV(body, "token",       token,       first);
	AppendKV(body, "playerCount", playerCount, first);
	if (!state.empty()) AppendKV(body, "state", state, first);
	body.push_back('}');
	return body;
}

std::string BuildPeerHintBody(const std::string& joinerHandle,
                              const std::string& sessionNonceHex,
                              const std::string& candidates) {
	std::string body;
	body.reserve(256);
	body.push_back('{');
	bool first = true;
	AppendKV(body, "joinerHandle", joinerHandle,    first);
	AppendKV(body, "sessionNonce", sessionNonceHex, first);
	AppendKV(body, "candidates",   candidates,      first);
	body.push_back('}');
	return body;
}

// ---- JSON response parsers ------------------------------------------

bool ParseSession(ATLobby::JsonCursor& c, ATNetplay::LobbySession& s) {
	if (!c.match('{')) return false;
	if (c.match('}')) return true;
	for (;;) {
		std::string key;
		if (!c.parseString(key)) return false;
		if (!c.match(':'))       return false;
		if      (key == "sessionId")      c.parseString(s.sessionId);
		else if (key == "cartName")       c.parseString(s.cartName);
		else if (key == "hostHandle")     c.parseString(s.hostHandle);
		else if (key == "hostEndpoint")   c.parseString(s.hostEndpoint);
		else if (key == "candidates")     c.parseString(s.candidates);
		else if (key == "region")         c.parseString(s.region);
		else if (key == "visibility")     c.parseString(s.visibility);
		else if (key == "cartArtHash")    c.parseString(s.cartArtHash);
		else if (key == "createdAt")      c.parseString(s.createdAt);
		else if (key == "lastSeen")       c.parseString(s.lastSeen);
		else if (key == "kernelCRC32")    c.parseString(s.kernelCRC32);
		else if (key == "basicCRC32")     c.parseString(s.basicCRC32);
		else if (key == "hardwareMode")   c.parseString(s.hardwareMode);
		else if (key == "videoStandard")  c.parseString(s.videoStandard);
		else if (key == "memoryMode")     c.parseString(s.memoryMode);
		else if (key == "state")          c.parseString(s.state);
		else if (key == "playerCount")    c.parseInt(s.playerCount);
		else if (key == "maxPlayers")     c.parseInt(s.maxPlayers);
		else if (key == "protocolVersion") c.parseInt(s.protocolVersion);
		else if (key == "requiresCode")   c.parseBool(s.requiresCode);
		else if (key == "wssRelayOnly")   c.parseBool(s.wssRelayOnly);
		else { if (!c.parseNull() && !c.skipValue()) return false; }
		if (!c.ok) return false;
		if (c.match(',')) continue;
		if (c.match('}')) return true;
		return false;
	}
}

bool ParseCreateResp(const char* data, size_t len,
                     ATNetplay::LobbyCreateResponse& out) {
	ATLobby::JsonCursor c{data, data + len};
	if (!c.match('{')) return false;
	for (;;) {
		std::string k;
		if (!c.parseString(k)) return false;
		if (!c.match(':'))     return false;
		if      (k == "sessionId")  c.parseString(out.sessionId);
		else if (k == "token")      c.parseString(out.token);
		else if (k == "ttlSeconds") c.parseInt(out.ttlSeconds);
		else c.skipValue();
		if (!c.ok) return false;
		if (c.match(',')) continue;
		if (c.match('}')) return true;
		return false;
	}
}

bool ParseListResp(const char* data, size_t len,
                   std::vector<ATNetplay::LobbySession>& out) {
	out.clear();
	ATLobby::JsonCursor c{data, data + len};
	c.skipWs();
	if (c.parseNull()) return true;
	if (!c.match('[')) return false;
	if (c.match(']')) return true;
	for (;;) {
		ATNetplay::LobbySession s;
		if (!ParseSession(c, s)) return false;
		out.push_back(std::move(s));
		if (c.match(',')) continue;
		if (c.match(']')) return true;
		return false;
	}
}

bool ParseHeartbeatResp(const char* data, size_t len,
                        std::vector<ATNetplay::LobbySessionHint>& outHints) {
	outHints.clear();
	ATLobby::JsonCursor c{data, data + len};
	if (!c.match('{') || c.match('}')) return true;  // empty/missing
	for (;;) {
		std::string key;
		if (!c.parseString(key)) break;
		if (!c.match(':'))       break;
		if (key == "hints") {
			if (!c.match('[')) { c.skipValue(); }
			else if (c.match(']')) { /* empty */ }
			else {
				for (;;) {
					ATNetplay::LobbySessionHint h;
					if (!c.match('{')) { c.skipValue(); break; }
					for (;;) {
						std::string hk;
						if (!c.parseString(hk)) break;
						if (!c.match(':'))      break;
						if      (hk == "sessionNonce") c.parseString(h.nonceHex);
						else if (hk == "joinerHandle") c.parseString(h.joinerHandle);
						else if (hk == "candidates")   c.parseString(h.candidates);
						else if (hk == "ageMs")        c.parseInt(h.ageMs);
						else { if (!c.parseNull() && !c.skipValue()) break; }
						if (c.match(',')) continue;
						c.match('}'); break;
					}
					outHints.push_back(std::move(h));
					if (c.match(',')) continue;
					c.match(']'); break;
				}
			}
		} else {
			c.skipValue();
		}
		if (c.match(',')) continue;
		c.match('}'); break;
	}
	return true;
}

bool ParseStatsResp(const char* data, size_t len,
                    ATNetplay::LobbyStats& out) {
	ATLobby::JsonCursor c{data, data + len};
	if (!c.match('{')) return false;
	for (;;) {
		std::string k;
		if (!c.parseString(k)) return false;
		if (!c.match(':'))     return false;
		if      (k == "sessions") c.parseInt(out.sessions);
		else if (k == "waiting")  c.parseInt(out.waiting);
		else if (k == "playing")  c.parseInt(out.playing);
		else if (k == "hosts")    c.parseInt(out.hosts);
		else c.skipValue();
		if (!c.ok) return false;
		if (c.match(',')) continue;
		if (c.match('}')) return true;
		return false;
	}
}

// ---- per-fetch context -----------------------------------------------

struct FetchCtx {
	LobbyWorker* worker = nullptr;
	LobbyOp      op     = LobbyOp::List;
	uint32_t     tag    = 0;
	uint32_t     coordGen = 0;
	std::string  source;     // sourceLobby
	std::string  body;       // owns the POST body bytes for fetch
	std::string  sessionToken;  // for X-Session-Token on Delete
};

}  // namespace

void LobbyWorker::Start() {
	// Mark running so Post() accepts work.  No thread to spawn — fetches
	// run on the browser's network thread under the hood.
	mRunning.store(true);
	mStop.store(false);
}

void LobbyWorker::Stop() {
	if (!mRunning.exchange(false)) return;
	mStop.store(true);
	// emscripten_fetch_t in flight will still call back; FetchCtx
	// becomes orphaned and we drop its result rather than touching
	// torn-down queues.  The callback checks worker->mStop.
	std::lock_guard<std::mutex> lk(mMu);
	mInQueue.clear();
	mOutQueue.clear();
}

// Defined at namespace scope (declared as friend in the header) so we
// can poke the LobbyWorker's private members from the C-style fetch
// callbacks without making the queues themselves public.
void Internal_LobbyWorker_PushResult(LobbyWorker* w, LobbyResult&& out) {
	if (!w) return;
	std::lock_guard<std::mutex> lk(w->mMu);
	if (w->mStop.load()) return;
	w->mOutQueue.push_back(std::move(out));
	if (w->mInFlight.load() > 0) --w->mInFlight;
}

namespace {

void OnSuccess(emscripten_fetch_t* f) {
	auto* ctx = static_cast<FetchCtx*>(f->userData);
	LobbyResult out{};
	out.op          = ctx->op;
	out.tag         = ctx->tag;
	out.coordGen    = ctx->coordGen;
	out.sourceLobby = ctx->source;
	out.httpStatus  = (int)f->status;
	out.ok          = (f->status >= 200 && f->status < 300);

	const char* body = f->data ? f->data : "";
	size_t      len  = (size_t)f->numBytes;

	if (out.ok) {
		switch (ctx->op) {
			case LobbyOp::List:
				out.ok = ParseListResp(body, len, out.sessions);
				if (!out.ok) out.error = "malformed list response";
				break;
			case LobbyOp::GetById: {
				// Single-session response: parse one entry directly.
				ATLobby::JsonCursor c{body, body + len};
				c.skipWs();
				ATNetplay::LobbySession one;
				if (!ParseSession(c, one)) {
					out.ok = false;
					out.error = "malformed session response";
				} else {
					out.sessions.clear();
					out.sessions.push_back(std::move(one));
				}
				break;
			}
			case LobbyOp::Create:
				out.ok = ParseCreateResp(body, len, out.create);
				if (!out.ok) out.error = "malformed create response";
				break;
			case LobbyOp::Heartbeat:
				out.ok = ParseHeartbeatResp(body, len, out.hints);
				// Tolerant: empty body / no hints field is success.
				if (!out.ok) out.error = "malformed heartbeat response";
				break;
			case LobbyOp::Stats:
				out.ok = ParseStatsResp(body, len, out.stats);
				if (!out.ok) out.error = "malformed stats response";
				break;
			case LobbyOp::Delete:
				// 204 No Content — no body to parse.
				break;
			case LobbyOp::PeerHint:
				// 200 OK with arbitrary body; ignore it.
				break;
			case LobbyOp::PortMapRefresh:
				// Should not happen — we never schedule this op for WASM.
				out.ok = false;
				out.error = "PortMapRefresh not supported in WASM";
				break;
		}
	} else {
		out.error.assign(f->statusText ? f->statusText : "fetch error");
	}

	Internal_LobbyWorker_PushResult(ctx->worker, std::move(out));
	emscripten_fetch_close(f);
	delete ctx;
}

void OnError(emscripten_fetch_t* f) {
	auto* ctx = static_cast<FetchCtx*>(f->userData);
	LobbyResult out{};
	out.op          = ctx->op;
	out.tag         = ctx->tag;
	out.coordGen    = ctx->coordGen;
	out.sourceLobby = ctx->source;
	out.httpStatus  = (int)f->status;
	out.ok          = false;
	// Prefer a response body of the form {"error":"..."} when the
	// upstream returned one (the lobby's 4xx responses always do —
	// e.g. {"error":"hostHandle: 1..32 chars required"}).  Falls back
	// to statusText (often "Not Found" from Caddy) when no JSON is
	// present so the user still gets *something* actionable.  Without
	// this the surface error was always literally "Not Found"
	// regardless of what actually went wrong.
	const char* body = f->data ? f->data : "";
	const size_t blen = (size_t)f->numBytes;
	std::string detail;
	if (blen) {
		ATLobby::JsonCursor c{body, body + blen};
		if (c.match('{')) {
			while (c.ok && !c.match('}')) {
				std::string key;
				if (!c.parseString(key)) break;
				if (!c.match(':'))       break;
				if (key == "error") {
					c.parseString(detail);
					break;
				}
				c.skipValue();
				c.match(',');  // tolerant of trailing commas
			}
		}
	}
	if (!detail.empty()) {
		out.error = std::move(detail);
	} else {
		out.error.assign(f->statusText ? f->statusText : "network error");
	}
	Internal_LobbyWorker_PushResult(ctx->worker, std::move(out));
	emscripten_fetch_close(f);
	delete ctx;
}

}  // namespace

bool LobbyWorker::Post(LobbyRequest req, const std::string& source) {
	if (!mRunning.load()) return false;

	auto* ctx       = new FetchCtx();
	ctx->worker     = this;
	ctx->op         = req.op;
	ctx->tag        = req.tag;
	ctx->coordGen   = req.coordGen;
	ctx->source     = source;

	emscripten_fetch_attr_t attr;
	emscripten_fetch_attr_init(&attr);
	attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
	attr.userData   = ctx;
	attr.onsuccess  = OnSuccess;
	attr.onerror    = OnError;

	// Construct URL.  All requests go to the configured lobby host;
	// the scheme is forced to https because the WASM build is loaded
	// over https (mixed-content rule).  Path varies per op.
	std::string url = "https://";
	url += req.endpoint.host;
	// Ignore req.endpoint.port — the public TLS edge listens on 443
	// (Caddy) and routes all /v1/* and /netplay paths internally.
	std::string path;
	const char* methodStr = "GET";

	switch (req.op) {
		case LobbyOp::List:
			path = "/v1/sessions";
			methodStr = "GET";
			break;
		case LobbyOp::Create:
			path = "/v1/session";
			methodStr = "POST";
			ctx->body = BuildCreateBody(req.createReq);
			break;
		case LobbyOp::Heartbeat:
			path  = "/v1/session/";
			path += req.sessionId;
			path += "/heartbeat";
			methodStr = "POST";
			ctx->body = BuildHeartbeatBody(
				req.token, req.playerCount, req.state);
			break;
		case LobbyOp::Delete:
			path  = "/v1/session/";
			path += req.sessionId;
			methodStr = "DELETE";
			ctx->sessionToken = req.token;
			break;
		case LobbyOp::Stats:
			path = "/v1/stats";
			methodStr = "GET";
			break;
		case LobbyOp::GetById:
			path  = "/v1/session/";
			path += req.sessionId;
			methodStr = "GET";
			break;
		case LobbyOp::PeerHint: {
			path  = "/v1/session/";
			path += req.sessionId;
			path += "/peer-hint";
			methodStr = "POST";
			std::string cands;
			if (!req.createReq.candidates.empty())
				cands = req.createReq.candidates.front();
			ctx->body = BuildPeerHintBody(req.state, req.token, cands);
			break;
		}
		case LobbyOp::PortMapRefresh: {
			// Silently fail with a clear error — PMP/PCP requires
			// LAN UDP which the browser doesn't have.
			delete ctx;
			LobbyResult out{};
			out.op       = req.op;
			out.tag      = req.tag;
			out.coordGen = req.coordGen;
			out.ok       = false;
			out.error    = "PortMapRefresh not supported in WASM";
			std::lock_guard<std::mutex> lk(mMu);
			mOutQueue.push_back(std::move(out));
			return true;
		}
	}
	url += path;

	std::strncpy(attr.requestMethod, methodStr,
		sizeof attr.requestMethod - 1);
	attr.requestMethod[sizeof attr.requestMethod - 1] = '\0';

	// Headers vary by op.  Static const-char* arrays terminated by NULL.
	const char* delHeaders[] = {
		"X-Session-Token", ctx->sessionToken.c_str(),
		nullptr,
	};
	const char* postHeaders[] = {
		"Content-Type", "application/json",
		nullptr,
	};
	if (req.op == LobbyOp::Delete) {
		attr.requestHeaders = delHeaders;
	} else if (req.op == LobbyOp::Create ||
	           req.op == LobbyOp::Heartbeat ||
	           req.op == LobbyOp::PeerHint) {
		attr.requestHeaders = postHeaders;
		attr.requestData     = ctx->body.data();
		attr.requestDataSize = ctx->body.size();
	}

	// Timeout: same default LobbyClient uses on native (a few seconds).
	attr.timeoutMSecs = req.endpoint.timeoutMs ? req.endpoint.timeoutMs
	                                           : 5000;

	++mInFlight;
	emscripten_fetch_t* f = emscripten_fetch(&attr, url.c_str());
	if (!f) {
		// Synchronous failure to even start the fetch.  Surface as a
		// network error so the UI doesn't wedge on this in-flight tag.
		--mInFlight;
		LobbyResult out{};
		out.op       = req.op;
		out.tag      = req.tag;
		out.coordGen = req.coordGen;
		out.ok       = false;
		out.error    = "emscripten_fetch failed to start";
		std::lock_guard<std::mutex> lk(mMu);
		mOutQueue.push_back(std::move(out));
		delete ctx;
	}
	return true;
}

void LobbyWorker::Poll(const std::function<void(LobbyResult&)>& onResult) {
	std::deque<LobbyResult> local;
	{
		std::lock_guard<std::mutex> lk(mMu);
		local.swap(mOutQueue);
	}
	for (auto& r : local) onResult(r);
}

size_t LobbyWorker::InFlightCount() const { return mInFlight.load(); }

void LobbyWorker::ThreadMain() {
	// Unused on WASM; the symbol exists for header parity.
}

#endif  // !__EMSCRIPTEN__

} // namespace ATNetplayUI
