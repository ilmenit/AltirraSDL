//	AltirraSDL - Background lobby HTTP worker (impl)

#include <stdafx.h>

#include "ui_netplay_lobby_worker.h"

#include "netplay/nat_discovery.h"
#include "netplay/port_mapping.h"
#include "netplay/lobby_protocol.h"

#include <at/atcore/logging.h>

#include <cstdio>
#include <cstring>

extern ATLogChannel g_ATLCNetplay;

namespace ATNetplayUI {

LobbyWorker::LobbyWorker() = default;

LobbyWorker::~LobbyWorker() { Stop(); }

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
		out.sourceLobby = q.source;

		switch (q.req.op) {
			case LobbyOp::List:
				out.ok = client.List(out.sessions);
				break;
			case LobbyOp::Create: {
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
						/*lifetimeSec=*/3600,    // 1 h; refresh on next Create
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
			case LobbyOp::PeerHint: {
				// req.sessionId  = target host's lobby session id
				// req.token      = joiner sessionNonce hex (32 chars)
				// req.state      = joiner handle
				// req.createReq.candidates[0] = candidates
				std::string cands;
				if (!q.req.createReq.candidates.empty())
					cands = q.req.createReq.candidates.front();
				out.ok = client.PostPeerHint(q.req.sessionId,
					q.req.state, q.req.token, cands);
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

} // namespace ATNetplayUI
