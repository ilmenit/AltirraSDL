// Altirra SDL netplay lobby — C++ reference server.
//
// In-memory HTTP/JSON session directory.  No database, no rendezvous,
// no TLS termination.  Drop-in replacement for the previous Go
// implementation (server.go); wire contract and validation rules are
// byte-identical so existing clients don't notice the swap.
//
// Transport: cpp-httplib (vendored, single header).
// JSON:      hand-rolled reader + writer in json_cursor.h.
// Protocol:  field names and bounds live in lobby_protocol.h, which
//            is also included by the client — client/server drift is
//            caught at compile time.

#include "lobby_protocol.h"
#include "json_cursor.h"

// cpp-httplib opts into OpenSSL iff CPPHTTPLIB_OPENSSL_SUPPORT is
// *defined* (any value).  We don't want TLS in-tree — HTTP-only
// deployment behind the reverse proxy keeps the binary dep-free.
#include "httplib.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

using namespace ATLobby;
using Clock = std::chrono::steady_clock;

// -----------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------

int64_t NowMs() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		Clock::now().time_since_epoch()).count();
}

std::string IsoNow() {
	std::time_t t = std::time(nullptr);
	std::tm tm;
#ifdef _WIN32
	gmtime_s(&tm, &t);
#else
	gmtime_r(&t, &tm);
#endif
	char buf[32];
	std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
	return buf;
}

// UUIDv4 from OS RNG.  RFC 4122 bits 6/8 masked.
std::string NewUUIDv4() {
	std::random_device rd;
	uint8_t b[16];
	for (int i = 0; i < 16; i += 4) {
		uint32_t x = rd();
		b[i]   = (uint8_t)(x);
		b[i+1] = (uint8_t)(x >> 8);
		b[i+2] = (uint8_t)(x >> 16);
		b[i+3] = (uint8_t)(x >> 24);
	}
	b[6] = (b[6] & 0x0f) | 0x40;
	b[8] = (b[8] & 0x3f) | 0x80;
	char out[40];
	std::snprintf(out, sizeof out,
		"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
		"%02x%02x%02x%02x%02x%02x",
		b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
		b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
	return out;
}

// 128-bit opaque token, hex.
std::string NewToken() {
	std::random_device rd;
	uint8_t b[16];
	for (int i = 0; i < 16; i += 4) {
		uint32_t x = rd();
		b[i]   = (uint8_t)(x);
		b[i+1] = (uint8_t)(x >> 8);
		b[i+2] = (uint8_t)(x >> 16);
		b[i+3] = (uint8_t)(x >> 24);
	}
	char out[33];
	for (int i = 0; i < 16; ++i)
		std::snprintf(out + i*2, 3, "%02x", b[i]);
	return out;
}

bool IsHexChar(char c) {
	return (c >= '0' && c <= '9') ||
	       (c >= 'a' && c <= 'f') ||
	       (c >= 'A' && c <= 'F');
}

// "host:port" where port is 1..65535.  Same rule as the Go server.
bool IsEndpoint(const std::string& s) {
	auto i = s.rfind(':');
	if (i == std::string::npos || i == 0 || i == s.size() - 1)
		return false;
	int port = std::atoi(s.c_str() + i + 1);
	return port > 0 && port < 65536;
}

// -----------------------------------------------------------------
// Session store
// -----------------------------------------------------------------

struct Session {
	std::string id;
	std::string cartName;
	std::string hostHandle;
	std::string hostEndpoint;
	std::string region;
	int         playerCount    = 1;
	int         maxPlayers     = 2;
	int         protocolVer    = 0;
	std::string visibility;        // "public" | "private"
	bool        requiresCode   = false;
	std::string cartArtHash;       // optional
	std::string createdAt;         // ISO-8601
	int64_t     lastSeenMs     = 0;  // monotonic ms
	std::string token;             // never serialised in List/Get

	// v2 firmware pre-flight: joiners check their ATFirmwareManager
	// against these CRCs before even attempting to connect.  Stored
	// as 8-char uppercase hex strings (empty means "no constraint").
	std::string kernelCRC32;
	std::string basicCRC32;
	std::string hardwareMode;      // "800XL" / "5200" / etc.
	std::string videoStandard;     // "NTSC" / "PAL" / "SECAM" / etc.
	std::string memoryMode;        // "320K" / "1088K" / etc.

	// v2 session state: "waiting" = joinable, "playing" = in session
	// (kept in the listing so the lobby looks alive, but Browser greys
	// it out and suppresses Join).  Hosts update via heartbeat.
	std::string state;             // "waiting" | "playing"
};

struct Config {
	std::string bind          = "0.0.0.0";
	int         port          = 8080;
	int         ttlSeconds    = kSessionTTLSeconds;
	int         maxSessions   = kMaxSessions;
	int         rateBurst     = kRateBurst;
	int         rateRefillMs  = kRateRefillMillis;
};

// Token-bucket rate limit per source IP.
struct Bucket {
	double  tokens    = 0.0;
	int64_t lastFillMs = 0;
};

class RateLimiter {
public:
	explicit RateLimiter(const Config& cfg)
		: mBurst((double)cfg.rateBurst)
		, mRefillMs((double)cfg.rateRefillMs) {}

	bool Allow(const std::string& ip, int64_t nowMs) {
		std::lock_guard<std::mutex> lk(mMu);
		auto& b = mByIp[ip];
		if (b.lastFillMs == 0) {
			b.tokens     = mBurst;
			b.lastFillMs = nowMs;
		} else {
			int64_t elapsed = nowMs - b.lastFillMs;
			if (elapsed > 0) {
				b.tokens += (double)elapsed / mRefillMs;
				if (b.tokens > mBurst) b.tokens = mBurst;
				b.lastFillMs = nowMs;
			}
		}
		if (b.tokens < 1.0) return false;
		b.tokens -= 1.0;
		return true;
	}

	int Prune(int64_t nowMs, int64_t keepMs) {
		std::lock_guard<std::mutex> lk(mMu);
		int n = 0;
		for (auto it = mByIp.begin(); it != mByIp.end();) {
			if (nowMs - it->second.lastFillMs > keepMs) {
				it = mByIp.erase(it);
				++n;
			} else {
				++it;
			}
		}
		return n;
	}

private:
	std::mutex                              mMu;
	std::unordered_map<std::string, Bucket> mByIp;
	double                                  mBurst;
	double                                  mRefillMs;
};

class Store {
public:
	explicit Store(const Config& cfg) : mCfg(cfg), mRate(cfg) {}

	// Create returns empty id on capacity failure.
	Session Create(const Session& in) {
		std::lock_guard<std::mutex> lk(mMu);
		if ((int)mItems.size() >= mCfg.maxSessions) return Session{};
		Session s    = in;
		s.id         = NewUUIDv4();
		s.token      = NewToken();
		s.createdAt  = IsoNow();
		s.lastSeenMs = NowMs();
		if (s.state.empty()) s.state = kStateWaiting;
		mItems[s.id] = s;
		return s;
	}

	// Heartbeat: verify token, update lastSeen + playerCount + state.
	// Empty `newState` is treated as "no change".
	// Returns: 200 (ok), 401 (bad token), 404 (no such session).
	int Heartbeat(const std::string& id, const std::string& token,
	              int newPlayerCount, const std::string& newState) {
		std::lock_guard<std::mutex> lk(mMu);
		auto it = mItems.find(id);
		if (it == mItems.end()) return 404;
		if (it->second.token != token) return 401;
		it->second.lastSeenMs = NowMs();
		if (newPlayerCount > 0 &&
		    newPlayerCount <= it->second.maxPlayers) {
			it->second.playerCount = newPlayerCount;
		}
		if (newState == kStateWaiting || newState == kStatePlaying) {
			it->second.state = newState;
		}
		return 200;
	}

	// Aggregate stats for /v1/stats.  Cheap O(N) walk under the same
	// lock List uses; with kListCap=500 this is microseconds.
	struct Stats {
		int sessions = 0;
		int waiting  = 0;
		int playing  = 0;
		int hosts    = 0;   // unique hostHandle count
	};
	Stats ComputeStats() const {
		std::lock_guard<std::mutex> lk(mMu);
		Stats s;
		s.sessions = (int)mItems.size();
		std::unordered_map<std::string, int> hostSeen;
		for (const auto& kv : mItems) {
			if (kv.second.state == kStatePlaying) ++s.playing;
			else                                  ++s.waiting;
			if (!kv.second.hostHandle.empty())
				hostSeen[kv.second.hostHandle] = 1;
		}
		s.hosts = (int)hostSeen.size();
		return s;
	}

	int Delete(const std::string& id, const std::string& token) {
		std::lock_guard<std::mutex> lk(mMu);
		auto it = mItems.find(id);
		if (it == mItems.end()) return 404;
		if (it->second.token != token) return 401;
		mItems.erase(it);
		return 204;
	}

	bool Get(const std::string& id, Session& out) const {
		std::lock_guard<std::mutex> lk(mMu);
		auto it = mItems.find(id);
		if (it == mItems.end()) return false;
		out = it->second;
		return true;
	}

	// Returns up to kListCap sessions, newest-createdAt first.
	std::vector<Session> List() const {
		std::vector<Session> out;
		{
			std::lock_guard<std::mutex> lk(mMu);
			out.reserve(mItems.size());
			for (const auto& kv : mItems) out.push_back(kv.second);
		}
		std::sort(out.begin(), out.end(),
			[](const Session& a, const Session& b) {
				// createdAt is ISO-8601 and strictly increasing with
				// wall-clock, so lexicographic desc works for "newest
				// first" within the same second; within the same ms
				// the ordering is undefined but stable across calls.
				return a.createdAt > b.createdAt;
			});
		if ((int)out.size() > kListCap) out.resize(kListCap);
		return out;
	}

	size_t Count() const {
		std::lock_guard<std::mutex> lk(mMu);
		return mItems.size();
	}

	int ExpireOnce(int64_t nowMs) {
		std::lock_guard<std::mutex> lk(mMu);
		int n = 0;
		int64_t cutoff = nowMs - (int64_t)mCfg.ttlSeconds * 1000;
		for (auto it = mItems.begin(); it != mItems.end();) {
			if (it->second.lastSeenMs < cutoff) {
				it = mItems.erase(it);
				++n;
			} else {
				++it;
			}
		}
		return n;
	}

	RateLimiter& Rate()          { return mRate; }
	const Config& Cfg() const    { return mCfg; }

private:
	mutable std::mutex                       mMu;
	Config                                   mCfg;
	std::unordered_map<std::string, Session> mItems;
	RateLimiter                              mRate;
};

// -----------------------------------------------------------------
// JSON serialization
// -----------------------------------------------------------------

void WriteSessionJson(JsonBuilder& b, const Session& s) {
	b.raw('{');
	b.key(Field::kSessionId);       b.str(s.id);           b.raw(',');
	b.key(Field::kCartName);        b.str(s.cartName);     b.raw(',');
	b.key(Field::kHostHandle);      b.str(s.hostHandle);   b.raw(',');
	b.key(Field::kHostEndpoint);    b.str(s.hostEndpoint); b.raw(',');
	b.key(Field::kRegion);          b.str(s.region);       b.raw(',');
	b.key(Field::kPlayerCount);     b.num(s.playerCount);  b.raw(',');
	b.key(Field::kMaxPlayers);      b.num(s.maxPlayers);   b.raw(',');
	b.key(Field::kProtocolVersion); b.num(s.protocolVer);  b.raw(',');
	b.key(Field::kVisibility);      b.str(s.visibility);   b.raw(',');
	b.key(Field::kRequiresCode);    b.boolean(s.requiresCode); b.raw(',');
	// Always emit the firmware / hardware / art-hash fields, even when
	// empty.  Clients treat the keys as part of the schema contract;
	// conditional omission forces them to guess "missing vs. empty",
	// which produced the "?" hardwareMode bug in the desktop browser
	// when the server silently dropped the keys on older deployments.
	// Empty string = "no constraint / unknown"; that's unambiguous.
	b.key(Field::kCartArtHash);   b.str(s.cartArtHash);   b.raw(',');
	b.key(Field::kKernelCRC32);   b.str(s.kernelCRC32);   b.raw(',');
	b.key(Field::kBasicCRC32);    b.str(s.basicCRC32);    b.raw(',');
	b.key(Field::kHardwareMode);  b.str(s.hardwareMode);  b.raw(',');
	b.key(Field::kVideoStandard); b.str(s.videoStandard); b.raw(',');
	b.key(Field::kMemoryMode);    b.str(s.memoryMode);    b.raw(',');
	b.key(Field::kState);           b.str(s.state.empty() ? kStateWaiting
	                                                       : s.state);
	b.raw(',');
	b.key(Field::kCreatedAt);       b.str(s.createdAt);    b.raw(',');
	// lastSeen is the server's monotonic clock; we emit an ISO time
	// for debug inspection — clients don't parse it.
	b.key(Field::kLastSeen);        b.str(s.createdAt);
	b.raw('}');
}

std::string ErrorJson(const std::string& msg) {
	JsonBuilder b;
	b.raw('{');
	b.key(Field::kError); b.str(msg);
	b.raw('}');
	return std::move(b.s);
}

// -----------------------------------------------------------------
// Request parsing / validation
// -----------------------------------------------------------------

struct CreateReq {
	std::string cartName;
	std::string hostHandle;
	std::string hostEndpoint;
	std::string region;
	std::string visibility;
	std::string cartArtHash;
	int         playerCount     = 0;
	int         maxPlayers      = 0;
	int         protocolVersion = 0;
	bool        requiresCode    = false;
	// v2 firmware pre-flight + hardware tag.
	std::string kernelCRC32;
	std::string basicCRC32;
	std::string hardwareMode;
	std::string videoStandard;
	std::string memoryMode;
};

struct HeartbeatReq {
	std::string token;
	int         playerCount = 0;
	std::string state;          // optional; "" = no change
};

bool ParseCreate(const std::string& body, CreateReq& r,
                 std::string& errOut) {
	if (body.size() > (size_t)kMaxRequestBodyBytes) {
		errOut = "invalid json: body too large";
		return false;
	}
	JsonCursor c{body.data(), body.data() + body.size()};
	if (!c.match('{')) { errOut = "invalid json"; return false; }
	if (c.match('}')) return true;  // empty obj → caller validates
	for (;;) {
		std::string key;
		if (!c.parseString(key)) { errOut = "invalid json"; return false; }
		if (!c.match(':'))       { errOut = "invalid json"; return false; }

		if      (key == Field::kCartName)        c.parseString(r.cartName);
		else if (key == Field::kHostHandle)      c.parseString(r.hostHandle);
		else if (key == Field::kHostEndpoint)    c.parseString(r.hostEndpoint);
		else if (key == Field::kRegion)          c.parseString(r.region);
		else if (key == Field::kVisibility)      c.parseString(r.visibility);
		else if (key == Field::kCartArtHash)     c.parseString(r.cartArtHash);
		else if (key == Field::kKernelCRC32)     c.parseString(r.kernelCRC32);
		else if (key == Field::kBasicCRC32)      c.parseString(r.basicCRC32);
		else if (key == Field::kHardwareMode)    c.parseString(r.hardwareMode);
		else if (key == Field::kVideoStandard)   c.parseString(r.videoStandard);
		else if (key == Field::kMemoryMode)      c.parseString(r.memoryMode);
		else if (key == Field::kPlayerCount)     c.parseInt(r.playerCount);
		else if (key == Field::kMaxPlayers)      c.parseInt(r.maxPlayers);
		else if (key == Field::kProtocolVersion) c.parseInt(r.protocolVersion);
		else if (key == Field::kRequiresCode)    c.parseBool(r.requiresCode);
		else { if (!c.parseNull() && !c.skipValue()) {
			errOut = "invalid json"; return false;
		} }

		if (!c.ok) { errOut = "invalid json"; return false; }
		if (c.match(',')) continue;
		if (c.match('}')) return true;
		errOut = "invalid json";
		return false;
	}
}

bool ParseHeartbeat(const std::string& body, HeartbeatReq& r,
                    std::string& errOut) {
	if (body.size() > (size_t)kMaxRequestBodyBytes) {
		errOut = "invalid json: body too large";
		return false;
	}
	JsonCursor c{body.data(), body.data() + body.size()};
	if (!c.match('{')) { errOut = "invalid json"; return false; }
	if (c.match('}')) return true;
	for (;;) {
		std::string key;
		if (!c.parseString(key)) { errOut = "invalid json"; return false; }
		if (!c.match(':'))       { errOut = "invalid json"; return false; }
		if      (key == Field::kToken)       c.parseString(r.token);
		else if (key == Field::kPlayerCount) c.parseInt(r.playerCount);
		else if (key == Field::kState)       c.parseString(r.state);
		else { if (!c.parseNull() && !c.skipValue()) {
			errOut = "invalid json"; return false;
		} }
		if (!c.ok) { errOut = "invalid json"; return false; }
		if (c.match(',')) continue;
		if (c.match('}')) return true;
		errOut = "invalid json"; return false;
	}
}

// Returns empty string on OK, or a human-readable reason.
std::string ValidateCreate(CreateReq& r) {
	if (r.cartName.empty() || (int)r.cartName.size() > kCartNameMax)
		return "cartName: 1.." + std::to_string(kCartNameMax) + " chars required";
	if (r.hostHandle.empty() || (int)r.hostHandle.size() > kHostHandleMax)
		return "hostHandle: 1.." + std::to_string(kHostHandleMax) + " chars required";
	if (!IsEndpoint(r.hostEndpoint))
		return "hostEndpoint: host:port required";
	if (r.maxPlayers < kMinPlayers || r.maxPlayers > kMaxPlayersLimit)
		return "maxPlayers: 2..8 required";
	if (r.playerCount < 1 || r.playerCount > r.maxPlayers)
		return "playerCount: 1..maxPlayers required";
	if (r.protocolVersion <= 0)
		return "protocolVersion required";
	if ((int)r.region.size() > kRegionMax)
		return "region: <=32 chars";
	if (r.visibility.empty()) r.visibility = kVisibilityPublic;
	if (r.visibility != kVisibilityPublic &&
	    r.visibility != kVisibilityPrivate)
		return "visibility: \"\" | \"public\" | \"private\" required";
	if (r.requiresCode && r.visibility != kVisibilityPrivate)
		return "requiresCode requires visibility \"private\"";
	if (r.visibility == kVisibilityPrivate && !r.requiresCode)
		return "visibility \"private\" requires requiresCode=true";
	if ((int)r.cartArtHash.size() > kCartArtHashMax)
		return "cartArtHash: <=64 hex chars";
	for (char c : r.cartArtHash)
		if (!IsHexChar(c)) return "cartArtHash: hex digits only";

	// v2 fields — all optional, but if present must be well-formed.
	auto checkCRCHex = [](const std::string& s, const char *name)
	    -> std::string {
		if (s.empty()) return {};
		if (s.size() != 8) return std::string(name) + ": 8 hex chars required";
		for (char c : s) if (!IsHexChar(c))
			return std::string(name) + ": hex digits only";
		return {};
	};
	if (auto e = checkCRCHex(r.kernelCRC32, "kernelCRC32"); !e.empty())
		return e;
	if (auto e = checkCRCHex(r.basicCRC32, "basicCRC32"); !e.empty())
		return e;
	if ((int)r.hardwareMode.size() > kHardwareModeMax)
		return "hardwareMode: <=16 chars";
	if ((int)r.videoStandard.size() > kVideoStandardMax)
		return "videoStandard: <=" + std::to_string(kVideoStandardMax) + " chars";
	if ((int)r.memoryMode.size() > kMemoryModeMax)
		return "memoryMode: <=" + std::to_string(kMemoryModeMax) + " chars";
	return {};
}

// -----------------------------------------------------------------
// HTTP plumbing — route wiring + middleware chain
// -----------------------------------------------------------------

// Derive the client's IP.  cpp-httplib gives us req.remote_addr; we
// also honour X-Forwarded-For (first entry) so reverse-proxy
// deployments see real source IPs.
std::string ClientIp(const httplib::Request& req) {
	if (req.has_header("X-Forwarded-For")) {
		std::string xff = req.get_header_value("X-Forwarded-For");
		auto comma = xff.find(',');
		if (comma != std::string::npos) xff.resize(comma);
		while (!xff.empty() && xff.front() == ' ') xff.erase(xff.begin());
		while (!xff.empty() && xff.back()  == ' ') xff.pop_back();
		return xff;
	}
	return req.remote_addr;
}

void SetCorsForGet(httplib::Response& res) {
	res.set_header("Access-Control-Allow-Origin", "*");
	res.set_header("Vary",                        "Origin");
}

void WriteErr(httplib::Response& res, int code, const std::string& msg) {
	res.status = code;
	res.set_content(ErrorJson(msg), "application/json");
}

// Structured access log — one JSON line per request to stdout.
void LogReq(const std::string& ip, const std::string& method,
            const std::string& path, int code, size_t nbytes,
            int64_t durMs) {
	JsonBuilder b;
	b.raw('{');
	b.key("ts");     b.str(IsoNow()); b.raw(',');
	b.key("ip");     b.str(ip);       b.raw(',');
	b.key("method"); b.str(method);   b.raw(',');
	b.key("path");   b.str(path);     b.raw(',');
	b.key("status"); b.num(code);     b.raw(',');
	b.key("bytes");  b.num((long long)nbytes); b.raw(',');
	b.key("dur_ms"); b.num(durMs);
	b.raw('}');
	std::fputs(b.s.c_str(), stdout);
	std::fputc('\n', stdout);
	std::fflush(stdout);
}

// Helper: does `path` start with `kPathSession + "/"`?  Returns the
// id portion after the slash (and the suffix portion after the id).
bool ParseSessionPath(const std::string& path,
                      std::string& idOut, std::string& suffixOut) {
	const std::string prefix = std::string(kPathSession) + "/";
	if (path.size() <= prefix.size()) return false;
	if (path.compare(0, prefix.size(), prefix) != 0) return false;
	std::string rest = path.substr(prefix.size());
	auto slash = rest.find('/');
	if (slash == std::string::npos) {
		idOut      = rest;
		suffixOut  = "";
	} else {
		idOut      = rest.substr(0, slash);
		suffixOut  = rest.substr(slash);
	}
	return !idOut.empty();
}

// -----------------------------------------------------------------
// Server setup
// -----------------------------------------------------------------

void Install(httplib::Server& srv, Store& store) {
	// Pre-route middleware: rate limit + origin guard on writes.
	srv.set_pre_routing_handler(
		[&store](const httplib::Request& req, httplib::Response& res) {
			std::string ip = ClientIp(req);

			if (req.method == "POST" || req.method == "DELETE") {
				// Native clients never send Origin; a browser that
				// reaches this point is a cross-site CSRF attempt.
				if (req.has_header("Origin"))
					return (WriteErr(res, 403,
						"browser origin not permitted on write routes"),
						httplib::Server::HandlerResponse::Handled);
			}
			if (req.method == "GET") SetCorsForGet(res);

			if (!store.Rate().Allow(ip, NowMs())) {
				int retryS = store.Cfg().rateRefillMs / 1000 + 1;
				if (retryS < 1) retryS = 1;
				res.set_header("Retry-After", std::to_string(retryS));
				WriteErr(res, 429, "rate limit exceeded");
				return httplib::Server::HandlerResponse::Handled;
			}
			return httplib::Server::HandlerResponse::Unhandled;
		});

	// Post-routing logger.
	srv.set_logger([](const httplib::Request& req,
	                  const httplib::Response& res) {
		LogReq(ClientIp(req), req.method, req.path, res.status,
			res.body.size(), 0);  // duration is tricky without a hook
	});

	// -- Routes -------------------------------------------------------

	srv.Get(kPathHealthz,
		[&store](const httplib::Request&, httplib::Response& res) {
			char buf[64];
			std::snprintf(buf, sizeof buf, "ok sessions=%zu",
				store.Count());
			res.set_content(buf, "text/plain; charset=utf-8");
		});

	// v2: aggregate stats — single small JSON object the Browser
	// fetches once per refresh tick to render the "X sessions • Y in
	// play • Z hosts" footer.  Cheap O(N) walk under the same lock
	// List takes; no per-user presence tracking required.
	srv.Get(kPathStats,
		[&store](const httplib::Request&, httplib::Response& res) {
			Store::Stats st = store.ComputeStats();
			JsonBuilder b;
			b.raw('{');
			b.key(Field::kSessionCount); b.num(st.sessions); b.raw(',');
			b.key(Field::kWaitingCount); b.num(st.waiting);  b.raw(',');
			b.key(Field::kPlayingCount); b.num(st.playing);  b.raw(',');
			b.key(Field::kHostCount);    b.num(st.hosts);
			b.raw('}');
			res.status = 200;
			res.set_content(std::move(b.s), "application/json");
		});

	srv.Get(kPathSessions,
		[&store](const httplib::Request&, httplib::Response& res) {
			auto list = store.List();
			JsonBuilder b;
			b.raw('[');
			for (size_t i = 0; i < list.size(); ++i) {
				if (i) b.raw(',');
				WriteSessionJson(b, list[i]);
			}
			b.raw(']');
			res.status = 200;
			res.set_content(std::move(b.s), "application/json");
		});

	srv.Post(kPathSession,
		[&store](const httplib::Request& req, httplib::Response& res) {
			CreateReq r;
			std::string err;
			if (!ParseCreate(req.body, r, err)) {
				WriteErr(res, 400, err); return;
			}
			err = ValidateCreate(r);
			if (!err.empty()) { WriteErr(res, 400, err); return; }

			Session in;
			in.cartName     = r.cartName;
			in.hostHandle   = r.hostHandle;
			in.hostEndpoint = r.hostEndpoint;
			in.region       = r.region;
			in.playerCount  = r.playerCount;
			in.maxPlayers   = r.maxPlayers;
			in.protocolVer  = r.protocolVersion;
			in.visibility   = r.visibility;
			in.requiresCode = r.requiresCode;
			in.cartArtHash  = r.cartArtHash;
			in.kernelCRC32   = r.kernelCRC32;
			in.basicCRC32    = r.basicCRC32;
			in.hardwareMode  = r.hardwareMode;
			in.videoStandard = r.videoStandard;
			in.memoryMode    = r.memoryMode;

			Session s = store.Create(in);
			if (s.id.empty()) {
				WriteErr(res, 429, "lobby full"); return;
			}

			JsonBuilder b;
			b.raw('{');
			b.key(Field::kSessionId);  b.str(s.id);    b.raw(',');
			b.key(Field::kToken);      b.str(s.token); b.raw(',');
			b.key(Field::kTTLSeconds); b.num(store.Cfg().ttlSeconds);
			b.raw('}');
			res.status = 201;
			res.set_content(std::move(b.s), "application/json");
		});

	// All /v1/session/{id}[...] routes — dispatched through a single
	// pattern match because cpp-httplib's built-in param syntax varies
	// across versions, and we need suffix matching for /heartbeat.
	auto dispatchId = [&store](const httplib::Request& req,
	                           httplib::Response& res) -> bool {
		std::string id, suffix;
		if (!ParseSessionPath(req.path, id, suffix)) return false;

		if (req.method == "GET" && suffix.empty()) {
			Session s;
			if (!store.Get(id, s)) {
				WriteErr(res, 404, "no such session");
				return true;
			}
			JsonBuilder b;
			WriteSessionJson(b, s);
			res.status = 200;
			res.set_content(std::move(b.s), "application/json");
			return true;
		}
		if (req.method == "POST" && suffix == kPathHeartbeatSuffix) {
			HeartbeatReq hr;
			std::string err;
			if (!ParseHeartbeat(req.body, hr, err)) {
				WriteErr(res, 400, err); return true;
			}
			int code = store.Heartbeat(id, hr.token, hr.playerCount,
			                           hr.state);
			if (code == 200) {
				JsonBuilder b;
				b.raw('{');
				b.key(Field::kTTLSeconds);
				b.num(store.Cfg().ttlSeconds);
				b.raw('}');
				res.status = 200;
				res.set_content(std::move(b.s), "application/json");
			} else if (code == 404) WriteErr(res, 404, "no such session");
			else                    WriteErr(res, 401, "bad token");
			return true;
		}
		if (req.method == "DELETE" && suffix.empty()) {
			std::string tok = req.get_header_value(kHeaderSessionToken);
			int code = store.Delete(id, tok);
			if (code == 204)       res.status = 204;
			else if (code == 404)  WriteErr(res, 404, "no such session");
			else                   WriteErr(res, 401, "bad token");
			return true;
		}
		return false;
	};

	// cpp-httplib lets us register per-method handlers with a regex.
	// Wire all three verbs to the dispatcher.
	const std::string idRegex =
		std::string(kPathSession) + "/[^/]+(?:/[^/]+)?";
	srv.Get(idRegex,
		[dispatchId](const httplib::Request& req, httplib::Response& res) {
			if (!dispatchId(req, res)) WriteErr(res, 404, "not found");
		});
	srv.Post(idRegex,
		[dispatchId](const httplib::Request& req, httplib::Response& res) {
			if (!dispatchId(req, res)) WriteErr(res, 404, "not found");
		});
	srv.Delete(idRegex,
		[dispatchId](const httplib::Request& req, httplib::Response& res) {
			if (!dispatchId(req, res)) WriteErr(res, 404, "not found");
		});

	// CORS preflight.
	srv.Options(".*",
		[](const httplib::Request&, httplib::Response& res) {
			res.set_header("Access-Control-Allow-Origin",  "*");
			res.set_header("Access-Control-Allow-Methods", "GET");
			res.set_header("Access-Control-Allow-Headers", "Content-Type");
			res.set_header("Access-Control-Max-Age",       "600");
			res.status = 204;
		});
}

// -----------------------------------------------------------------
// Background sweeper
// -----------------------------------------------------------------

class Sweeper {
public:
	explicit Sweeper(Store& s) : mStore(s) {}
	void Start() {
		mThread = std::thread([this] { Run(); });
	}
	void Stop() {
		{ std::lock_guard<std::mutex> lk(mMu); mStop = true; mCv.notify_all(); }
		if (mThread.joinable()) mThread.join();
	}
private:
	void Run() {
		while (true) {
			std::unique_lock<std::mutex> lk(mMu);
			mCv.wait_for(lk,
				std::chrono::milliseconds(kSweepIntervalMillis),
				[this] { return mStop; });
			if (mStop) return;
			lk.unlock();
			int64_t now = NowMs();
			mStore.ExpireOnce(now);
			mStore.Rate().Prune(now, kRateBucketKeepMillis);
		}
	}
	Store&                   mStore;
	std::thread              mThread;
	std::mutex               mMu;
	std::condition_variable  mCv;
	bool                     mStop = false;
};

// -----------------------------------------------------------------
// Config from environment
// -----------------------------------------------------------------

Config LoadConfig() {
	Config cfg;
	if (const char *v = std::getenv("BIND")) {
		// Accept ":PORT" or "HOST:PORT"
		std::string s = v;
		auto colon = s.rfind(':');
		if (colon != std::string::npos) {
			cfg.port = std::atoi(s.c_str() + colon + 1);
			if (colon > 0) cfg.bind = s.substr(0, colon);
		}
	}
	if (const char *v = std::getenv("PORT")) cfg.port = std::atoi(v);
	if (const char *v = std::getenv("TTL_SECONDS"))
		cfg.ttlSeconds = std::atoi(v);
	if (const char *v = std::getenv("MAX_SESSIONS"))
		cfg.maxSessions = std::atoi(v);
	if (const char *v = std::getenv("RATE_BURST"))
		cfg.rateBurst = std::atoi(v);
	return cfg;
}

}  // anonymous

// The test binary #include's this .cpp file directly with
// ALTIRRA_LOBBY_TEST_MAIN defined.  That TU gets all anonymous-ns
// symbols (Store, ParseCreate, etc.) and defines its own main();
// only the production main() below is skipped.
#ifndef ALTIRRA_LOBBY_TEST_MAIN

int main(int argc, char **argv) {
	(void)argc; (void)argv;

	Config cfg = LoadConfig();
	Store  store(cfg);
	Sweeper sweeper(store);
	sweeper.Start();

	httplib::Server srv;
	Install(srv, store);

	// Timeouts — without these a stuck client keeps a worker busy
	// forever.
	srv.set_read_timeout  (10, 0);
	srv.set_write_timeout (10, 0);
	srv.set_idle_interval (60, 0);
	srv.set_payload_max_length(kMaxRequestBodyBytes);

	std::atomic<httplib::Server*> srvPtr{&srv};

	// Signal handlers → stop() the HTTP server thread.
	static std::atomic<httplib::Server*> gShutdownPtr{nullptr};
	gShutdownPtr.store(&srv);
	auto onSignal = +[](int) {
		auto *s = gShutdownPtr.load();
		if (s) s->stop();
	};
	std::signal(SIGINT,  onSignal);
	std::signal(SIGTERM, onSignal);

	std::fprintf(stdout,
		"altirra-lobby listening on %s:%d (max=%d, ttl=%ds, burst=%d)\n",
		cfg.bind.c_str(), cfg.port, cfg.maxSessions, cfg.ttlSeconds,
		cfg.rateBurst);
	std::fflush(stdout);

	bool ok = srv.listen(cfg.bind, cfg.port);
	sweeper.Stop();
	std::fputs("bye\n", stdout);
	return ok ? 0 : 1;
}

#endif  // ALTIRRA_LOBBY_TEST_MAIN
