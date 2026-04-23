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

// POSIX socket headers for the UDP reflector thread (Linux/Oracle Cloud
// deployment).  The rest of the server uses cpp-httplib which already
// pulls these in; we include explicitly so RunReflector compiles
// regardless of include order.
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>

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

// Forward declaration: v4 relay-table pruning helper.  The real
// class + accessor are defined below (after the Store / Sweeper
// plumbing) but Sweeper::Run() needs to call Prune() periodically.
int RelayPruneNow(int64_t nowMs);

// -----------------------------------------------------------------
// Session store
// -----------------------------------------------------------------

// v4 two-sided punch: one buffered joiner hint awaiting delivery to
// the host on its next heartbeat.  The lobby stores up to kPeerHintCap
// per session; entries older than kPeerHintTtlMs are pruned before
// being served or on every heartbeat/list call.
struct PendingHint {
	std::string nonceHex;    // 32-char hex, joiner's sessionNonce
	std::string handle;      // joiner handle (≤32 chars, informational)
	std::string candidates;  // "ip:port;ip:port;..." (≤512 chars)
	int64_t     arrivedMs = 0;
};

struct Session {
	std::string id;
	std::string cartName;
	std::string hostHandle;
	std::string hostEndpoint;
	// v3: semicolon-separated list of "ip:port" endpoints the joiner
	// should try (LAN, srflx/public, loopback).  Empty for old clients
	// — they only publish hostEndpoint.  See lobby_protocol.h comment.
	std::string candidates;
	// v4 two-sided punch: hints posted by joiners awaiting pickup.
	std::vector<PendingHint> pendingHints;
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

	// v4 two-sided punch: append a hint to a session.  Returns 404 if
	// no such session, 400 if the hint list is full AND replacement
	// would overflow (we drop the oldest instead, so this never returns
	// 400 in practice — kept as an escape hatch).  On success, the hint
	// is stored with arrivedMs = now.
	int AppendHint(const std::string& id, PendingHint hint) {
		std::lock_guard<std::mutex> lk(mMu);
		auto it = mItems.find(id);
		if (it == mItems.end()) return 404;
		hint.arrivedMs = NowMs();
		auto& v = it->second.pendingHints;
		// Dedupe by nonce: a joiner retransmitting its hint (e.g. its
		// first POST was rate-limited) replaces the existing entry in
		// place rather than stacking N copies of the same candidates.
		for (auto& existing : v) {
			if (!hint.nonceHex.empty() && existing.nonceHex == hint.nonceHex) {
				existing = std::move(hint);
				return 200;
			}
		}
		if (v.size() >= kPeerHintCap) {
			v.erase(v.begin());  // drop oldest
		}
		v.push_back(std::move(hint));
		return 200;
	}

	// v4: drain all non-expired hints from a session (called under the
	// Store lock on every heartbeat before replying).  `out` is cleared
	// before appending.  Returns true iff the session exists.
	bool DrainHints(const std::string& id, int64_t nowMs,
	                std::vector<PendingHint>& out) {
		std::lock_guard<std::mutex> lk(mMu);
		auto it = mItems.find(id);
		if (it == mItems.end()) return false;
		auto& v = it->second.pendingHints;
		out.clear();
		int64_t cutoff = nowMs - (int64_t)kPeerHintTtlMs;
		for (auto& h : v) {
			if (h.arrivedMs >= cutoff) out.push_back(h);
		}
		// Hints are single-delivery: clear after handing them to the
		// host so the next heartbeat doesn't re-deliver stale rows.
		v.clear();
		return true;
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
	// v3 candidates: always emit even when empty — clients distinguish
	// "server silent" from "host didn't publish" the same way they do
	// for firmware CRCs.  Old clients ignore the unknown key.
	b.key(Field::kCandidates);      b.str(s.candidates);   b.raw(',');
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
	std::string candidates;   // v3: semicolon-separated "ip:port;..."
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
		else if (key == Field::kCandidates)      c.parseString(r.candidates);
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

// v4 peer-hint request body.
struct PeerHintReq {
	std::string joinerHandle;
	std::string sessionNonce;   // 32 hex chars
	std::string candidates;     // "ip:port;ip:port;..."
};

bool ParsePeerHint(const std::string& body, PeerHintReq& r,
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
		if      (key == Field::kJoinerHandle) c.parseString(r.joinerHandle);
		else if (key == Field::kSessionNonce) c.parseString(r.sessionNonce);
		else if (key == Field::kCandidates)   c.parseString(r.candidates);
		else { if (!c.parseNull() && !c.skipValue()) {
			errOut = "invalid json"; return false;
		} }
		if (!c.ok) { errOut = "invalid json"; return false; }
		if (c.match(',')) continue;
		if (c.match('}')) return true;
		errOut = "invalid json"; return false;
	}
}

std::string ValidatePeerHint(const PeerHintReq& r) {
	if (r.candidates.empty())
		return "candidates: required";
	if ((int)r.candidates.size() > kPeerHintCandidatesMax)
		return "candidates: <=" +
			std::to_string(kPeerHintCandidatesMax) + " chars";
	if ((int)r.joinerHandle.size() > kHostHandleMax)
		return "joinerHandle: <=" +
			std::to_string(kHostHandleMax) + " chars";
	if (!r.sessionNonce.empty()) {
		if ((int)r.sessionNonce.size() != kPeerHintNonceHexLen)
			return "sessionNonce: exactly 32 hex chars required";
		for (char c : r.sessionNonce)
			if (!IsHexChar(c))
				return "sessionNonce: hex digits only";
	}
	return {};
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
	// v3 candidates: cap the length to deter abuse (8 candidates at
	// ~40 bytes each = 320 bytes).  Content is not parsed here —
	// joiner-side splitter validates each entry.
	if (r.candidates.size() > 512)
		return "candidates: <=512 chars";
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
			in.candidates   = r.candidates;
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
				// v4: drain buffered peer-hints into the response so
				// the host can pick them up without a separate
				// polling endpoint.  Single-delivery — hints are
				// cleared from the session by DrainHints().
				std::vector<PendingHint> hints;
				store.DrainHints(id, NowMs(), hints);
				JsonBuilder b;
				b.raw('{');
				b.key(Field::kTTLSeconds);
				b.num(store.Cfg().ttlSeconds);
				b.raw(',');
				b.key(Field::kHints);
				b.raw('[');
				int64_t nowMs = NowMs();
				for (size_t i = 0; i < hints.size(); ++i) {
					if (i) b.raw(',');
					const auto& h = hints[i];
					b.raw('{');
					b.key(Field::kSessionNonce);
					b.str(h.nonceHex);         b.raw(',');
					b.key(Field::kJoinerHandle);
					b.str(h.handle);           b.raw(',');
					b.key(Field::kCandidates);
					b.str(h.candidates);       b.raw(',');
					b.key(Field::kAgeMs);
					long long age = (long long)(nowMs - h.arrivedMs);
					if (age < 0) age = 0;
					b.num(age);
					b.raw('}');
				}
				b.raw(']');
				b.raw('}');
				res.status = 200;
				res.set_content(std::move(b.s), "application/json");
			} else if (code == 404) WriteErr(res, 404, "no such session");
			else                    WriteErr(res, 401, "bad token");
			return true;
		}
		if (req.method == "POST" && suffix == kPathPeerHintSuffix) {
			PeerHintReq ph;
			std::string err;
			if (!ParsePeerHint(req.body, ph, err)) {
				WriteErr(res, 400, err); return true;
			}
			err = ValidatePeerHint(ph);
			if (!err.empty()) { WriteErr(res, 400, err); return true; }
			PendingHint h;
			h.nonceHex   = ph.sessionNonce;
			h.handle     = ph.joinerHandle;
			h.candidates = ph.candidates;
			int code = store.AppendHint(id, std::move(h));
			if (code == 404) {
				WriteErr(res, 404, "no such session");
				return true;
			}
			JsonBuilder b;
			b.raw('{');
			b.key(Field::kTTLSeconds);
			b.num(kPeerHintTtlMs / 1000);
			b.raw('}');
			res.status = 200;
			res.set_content(std::move(b.s), "application/json");
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
			RelayPruneNow(now);
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

// -----------------------------------------------------------------
// v4 Relay table (for UDP relay fallback after direct punch fails).
// -----------------------------------------------------------------
//
// Stateless-ish forwarder: keyed on the 16-byte session id (first
// 16 bytes of the UUIDv4 returned by Create(), hex-decoded without
// dashes).  Each entry holds the observed UDP endpoint of the host
// and the joiner as they arrive via 'ASGR' register packets.  On
// 'ASDF' receipt the server looks up the other side and forwards
// the inner bytes to its last-observed endpoint.  Entries expire
// after kRelayPeerIdleMs of silence from that side so a crashed peer
// doesn't keep the slot alive forever.
//
// Kept in its own thread with its own mutex so the HTTP handler
// mutex never contends with the hot relay path.  Access volume is
// ~60 packets/s per active session.

// Must match the client-side magics/roles in packets.h.  Duplicated
// here so server.cpp stays independent of the SDL3 client tree:
// 'A' 'S' 'D' 'R' — reflector (existing).
// 'A' 'S' 'D' 'F' — relay data  (v4).
// 'A' 'S' 'G' 'R' — relay register (v4).
constexpr size_t  kWireRelayHeaderSize   = 4 + 16 + 1 + 3;  // 24
constexpr size_t  kWireRelayRegisterSize = kWireRelayHeaderSize;
constexpr uint8_t kRelayRoleHost   = 0;
constexpr uint8_t kRelayRoleJoiner = 1;

struct RelayEndpoint {
	bool        known = false;
	sockaddr_in addr{};
	int64_t     lastSeenMs = 0;
};

struct RelayPair {
	RelayEndpoint host;
	RelayEndpoint joiner;
};

class RelayTable {
public:
	using Key = std::string;  // raw 16 bytes

	static Key MakeKey(const uint8_t sid[16]) {
		return std::string(reinterpret_cast<const char*>(sid), 16);
	}

	void Register(const uint8_t sid[16], uint8_t role,
	              const sockaddr_in& from, int64_t nowMs) {
		std::lock_guard<std::mutex> lk(mMu);
		auto& pair = mByKey[MakeKey(sid)];
		RelayEndpoint& slot = (role == kRelayRoleHost)
			? pair.host : pair.joiner;
		slot.known      = true;
		slot.addr       = from;
		slot.lastSeenMs = nowMs;
	}

	bool LookupOther(const uint8_t sid[16], uint8_t senderRole,
	                 const sockaddr_in& from, int64_t nowMs,
	                 sockaddr_in& outOther) {
		std::lock_guard<std::mutex> lk(mMu);
		auto it = mByKey.find(MakeKey(sid));
		if (it == mByKey.end()) return false;
		auto& pair = it->second;
		RelayEndpoint& me    = (senderRole == kRelayRoleHost)
			? pair.host : pair.joiner;
		RelayEndpoint& other = (senderRole == kRelayRoleHost)
			? pair.joiner : pair.host;
		me.known      = true;
		me.addr       = from;
		me.lastSeenMs = nowMs;
		if (!other.known) return false;
		if (nowMs - other.lastSeenMs > kRelayPeerIdleMs) return false;
		outOther = other.addr;
		return true;
	}

	bool IsRegisteredFromSource(const uint8_t sid[16], uint8_t role,
	                            const sockaddr_in& from, int64_t nowMs) {
		std::lock_guard<std::mutex> lk(mMu);
		auto it = mByKey.find(MakeKey(sid));
		if (it == mByKey.end()) return false;
		auto& pair = it->second;
		const RelayEndpoint& slot = (role == kRelayRoleHost)
			? pair.host : pair.joiner;
		if (!slot.known) return false;
		if (nowMs - slot.lastSeenMs > kRelayPeerIdleMs) return false;
		return slot.addr.sin_addr.s_addr == from.sin_addr.s_addr
		    && slot.addr.sin_port        == from.sin_port;
	}

	int Prune(int64_t nowMs) {
		std::lock_guard<std::mutex> lk(mMu);
		int n = 0;
		for (auto it = mByKey.begin(); it != mByKey.end(); ) {
			auto& p = it->second;
			int64_t maxSeen = 0;
			if (p.host.known   && p.host.lastSeenMs   > maxSeen)
				maxSeen = p.host.lastSeenMs;
			if (p.joiner.known && p.joiner.lastSeenMs > maxSeen)
				maxSeen = p.joiner.lastSeenMs;
			if (maxSeen == 0 ||
			    nowMs - maxSeen > kRelayPeerIdleMs) {
				it = mByKey.erase(it);
				++n;
			} else ++it;
		}
		return n;
	}

	size_t Size() const {
		std::lock_guard<std::mutex> lk(mMu);
		return mByKey.size();
	}

private:
	mutable std::mutex                 mMu;
	std::unordered_map<Key, RelayPair> mByKey;
};

RelayTable& Relay() {
	static RelayTable t;
	return t;
}

int RelayPruneNow(int64_t nowMs) {
	return Relay().Prune(nowMs);
}

}  // anonymous

// The test binary #include's this .cpp file directly with
// ALTIRRA_LOBBY_TEST_MAIN defined.  That TU gets all anonymous-ns
// symbols (Store, ParseCreate, etc.) and defines its own main();
// only the production main() below is skipped.
#ifndef ALTIRRA_LOBBY_TEST_MAIN

// -----------------------------------------------------------------
// UDP reflector (STUN-lite)
// -----------------------------------------------------------------
//
// Listens on UDP <reflectorPort> on every interface.  For each
// request matching the 8-byte magic 'A' 'S' 'D' 'R' + txid, replies
// with 24 bytes: magic + txid + family(=4) + pad + port(BE) + ipv4(BE)
// + 8 reserved bytes.
//
// Stateless — one thread, one blocking socket, zero per-session
// state.  Bandwidth cost is negligible: ~30 bytes per probe, each
// client probes once per session.

void RunReflector(uint16_t port, std::atomic<bool>& stop) {
	int s = ::socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0) {
		std::fprintf(stderr, "reflector: socket() failed: %s\n",
			std::strerror(errno));
		return;
	}

	int yes = 1;
	::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

	sockaddr_in local {};
	local.sin_family = AF_INET;
	local.sin_port = htons(port);
	local.sin_addr.s_addr = htonl(INADDR_ANY);
	if (::bind(s, (const sockaddr*)&local, sizeof local) != 0) {
		std::fprintf(stderr, "reflector: bind(:%u) failed: %s\n",
			(unsigned)port, std::strerror(errno));
		::close(s);
		return;
	}

	// 1 s poll timeout so we can notice `stop` promptly on shutdown.
	timeval tv;
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

	std::fprintf(stdout, "reflector listening on UDP :%u\n",
		(unsigned)port);
	std::fflush(stdout);

	// Per-source-IP rate limit: simple fixed-window counter.  UDP is
	// spoofable so this only stops honest heavy use (a reconnect
	// storm, a buggy client); a malicious peer can still forge
	// source IPs.  Amplification factor is 3x (8-byte request, 24-
	// byte response) which is low, but rate-limiting keeps it
	// negligible.  Window: 1 second; cap: 20 probes/sec per IP.
	constexpr int      kRateWindowSec      = 1;
	constexpr int      kRateMaxPerWindow   = 20;
	constexpr size_t   kRateTableMaxEntries = 4096;
	struct RateEntry { int64_t windowSec = 0; int count = 0; };
	std::unordered_map<uint32_t, RateEntry> rateTable;

	// Must fit an ASDF frame carrying an Altirra snapshot chunk
	// (~1232 B inner) plus our 24-byte relay header.  Reflector
	// (8-byte req) and punch (20 B) are well under this.
	constexpr size_t kRxBufSize = 1500;
	std::vector<uint8_t> rxBuf(kRxBufSize);
	uint8_t* buf = rxBuf.data();
	while (!stop.load(std::memory_order_relaxed)) {
		sockaddr_in from {};
		socklen_t fromLen = sizeof from;
		int n = ::recvfrom(s, (char*)buf, (int)kRxBufSize, 0,
			(sockaddr*)&from, &fromLen);
		if (n < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
			if (errno == EINTR) continue;
			break;
		}
		if (n < 8) continue;
		int64_t nowMs = NowMs();

		// v4: decide packet family before rate-limiting so we can
		// bypass the 20 pps cap for relay traffic from registered
		// peers (lockstep runs at 60 Hz in each direction).
		if (buf[0] == 'A' && buf[1] == 'S' &&
		    buf[2] == 'G' && buf[3] == 'R') {
			if (n < (int)kWireRelayRegisterSize) continue;
			uint8_t sid[16];
			std::memcpy(sid, buf + 4, 16);
			uint8_t role = buf[20];
			if (role != kRelayRoleHost && role != kRelayRoleJoiner)
				continue;
			Relay().Register(sid, role, from, nowMs);
			continue;  // one-shot register, no reply
		}
		if (buf[0] == 'A' && buf[1] == 'S' &&
		    buf[2] == 'D' && buf[3] == 'F') {
			if (n < (int)kWireRelayHeaderSize) continue;
			uint8_t sid[16];
			std::memcpy(sid, buf + 4, 16);
			uint8_t role = buf[20];
			if (role != kRelayRoleHost && role != kRelayRoleJoiner)
				continue;
			sockaddr_in other{};
			if (!Relay().LookupOther(sid, role, from, nowMs, other))
				continue;
			// Forward the inner bytes (after our 24-B header).
			// Per-IP rate cap bypass: registered peer, live slot.
			::sendto(s, (const char*)(buf + kWireRelayHeaderSize),
				(int)(n - (int)kWireRelayHeaderSize), 0,
				(const sockaddr*)&other, sizeof other);
			continue;
		}
		if (buf[0] != 'A' || buf[1] != 'S' ||
		    buf[2] != 'D' || buf[3] != 'R') continue;

		// Per-IP rate check.
		int64_t nowSec = nowMs / 1000;
		uint32_t key   = from.sin_addr.s_addr;
		auto& re = rateTable[key];
		if (re.windowSec != nowSec) {
			re.windowSec = nowSec;
			re.count = 0;
		}
		if (++re.count > kRateMaxPerWindow) {
			// Silently drop — no response means the sender can't
			// distinguish "rate-limited" from "no route" and won't
			// amplify.  Cheapest possible mitigation.
			continue;
		}

		// Periodically evict stale entries so the table doesn't
		// grow without bound under a wide IP sweep.  O(N) but
		// infrequent (only when the table exceeds the cap).
		if (rateTable.size() > kRateTableMaxEntries) {
			for (auto it = rateTable.begin(); it != rateTable.end(); ) {
				if (it->second.windowSec < nowSec - 60)
					it = rateTable.erase(it);
				else ++it;
			}
		}

		uint8_t resp[24] = {};
		resp[0] = 'A'; resp[1] = 'S'; resp[2] = 'D'; resp[3] = 'R';
		// Echo txid verbatim.
		resp[4] = buf[4]; resp[5] = buf[5];
		resp[6] = buf[6]; resp[7] = buf[7];
		resp[8]  = 4;          // family = IPv4
		resp[9]  = 0;          // pad
		// port big-endian
		uint16_t portBE = ntohs(from.sin_port);
		resp[10] = (uint8_t)((portBE >> 8) & 0xFF);
		resp[11] = (uint8_t)(portBE & 0xFF);
		// ipv4 big-endian
		uint32_t ipBE = ntohl(from.sin_addr.s_addr);
		resp[12] = (uint8_t)((ipBE >> 24) & 0xFF);
		resp[13] = (uint8_t)((ipBE >> 16) & 0xFF);
		resp[14] = (uint8_t)((ipBE >> 8) & 0xFF);
		resp[15] = (uint8_t)(ipBE & 0xFF);
		// resp[16..23] reserved, zero

		::sendto(s, (const char*)resp, (int)sizeof resp, 0,
			(const sockaddr*)&from, sizeof from);
	}

	::close(s);
}

int main(int argc, char **argv) {
	(void)argc; (void)argv;

	Config cfg = LoadConfig();
	Store  store(cfg);
	Sweeper sweeper(store);
	sweeper.Start();

	// UDP reflector port: default kReflectorPortDefault, env override.
	uint16_t reflectorPort = kReflectorPortDefault;
	if (const char* e = std::getenv("UDP_REFLECTOR_PORT")) {
		int v = std::atoi(e);
		if (v > 0 && v < 65536) reflectorPort = (uint16_t)v;
	}
	std::atomic<bool> reflectorStop{false};
	std::thread reflectorThread;
	if (reflectorPort > 0) {
		reflectorThread = std::thread(
			[&] { RunReflector(reflectorPort, reflectorStop); });
	}

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
	reflectorStop.store(true);
	if (reflectorThread.joinable()) reflectorThread.join();
	std::fputs("bye\n", stdout);
	return ok ? 0 : 1;
}

#endif  // ALTIRRA_LOBBY_TEST_MAIN
