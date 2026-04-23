// Altirra SDL3 netplay - lobby client (impl)
//
// The JSON reader below is intentionally minimal: it handles the exact
// response shapes produced by altirra-sdl-lobby (one object or an array
// of objects, with string/number/bool/null values — no nested objects,
// no escaped surrogate pairs beyond the basics).  We do NOT try to be a
// general JSON library; when the schema needs more, pull in vdjson.
//
// Encoding is trivial: we write a flat object with known keys.

#include <stdafx.h>

#include "lobby_client.h"

#include "http_minimal.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ATNetplay {

namespace {

// ---------------------------------------------------------------------
// Tiny JSON scanner.
// ---------------------------------------------------------------------
struct JsonCursor {
	const char* p;
	const char* end;
	bool        ok = true;

	void skipWs() {
		while (p < end) {
			char c = *p;
			if (c == ' ' || c == '\t' || c == '\r' || c == '\n') ++p;
			else break;
		}
	}

	bool match(char c) {
		skipWs();
		if (p < end && *p == c) { ++p; return true; }
		return false;
	}

	bool peek(char c) {
		skipWs();
		return p < end && *p == c;
	}

	bool parseString(std::string& out) {
		out.clear();
		skipWs();
		if (p >= end || *p != '"') { ok = false; return false; }
		++p;
		while (p < end) {
			char c = *p++;
			if (c == '"') return true;
			if (c == '\\') {
				if (p >= end) { ok = false; return false; }
				char e = *p++;
				switch (e) {
					case '"':  out.push_back('"');  break;
					case '\\': out.push_back('\\'); break;
					case '/':  out.push_back('/');  break;
					case 'b':  out.push_back('\b'); break;
					case 'f':  out.push_back('\f'); break;
					case 'n':  out.push_back('\n'); break;
					case 'r':  out.push_back('\r'); break;
					case 't':  out.push_back('\t'); break;
					case 'u': {
						if (end - p < 4) { ok = false; return false; }
						unsigned cp = 0;
						for (int i = 0; i < 4; ++i) {
							char h = p[i];
							cp <<= 4;
							if      (h >= '0' && h <= '9') cp |= (h - '0');
							else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
							else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
							else { ok = false; return false; }
						}
						p += 4;
						// Emit UTF-8.  Surrogate pairs are not expected in
						// lobby payloads; treat a high surrogate as U+FFFD.
						if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
						if (cp < 0x80) {
							out.push_back((char)cp);
						} else if (cp < 0x800) {
							out.push_back((char)(0xC0 | (cp >> 6)));
							out.push_back((char)(0x80 | (cp & 0x3F)));
						} else {
							out.push_back((char)(0xE0 | (cp >> 12)));
							out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
							out.push_back((char)(0x80 | (cp & 0x3F)));
						}
						break;
					}
					default: ok = false; return false;
				}
			} else {
				out.push_back(c);
			}
		}
		ok = false;
		return false;
	}

	// Parse any JSON value, returning its raw bytes.  Used to skip
	// unknown fields cheaply.
	bool skipValue() {
		skipWs();
		if (p >= end) { ok = false; return false; }
		char c = *p;
		if (c == '"') { std::string tmp; return parseString(tmp); }
		if (c == '{' || c == '[') {
			char open = c, close = (c == '{' ? '}' : ']');
			++p;
			int depth = 1;
			bool inStr = false;
			while (p < end && depth > 0) {
				char ch = *p++;
				if (inStr) {
					if (ch == '\\' && p < end) { ++p; continue; }
					if (ch == '"') inStr = false;
				} else {
					if (ch == '"') inStr = true;
					else if (ch == open) ++depth;
					else if (ch == close) --depth;
				}
			}
			if (depth != 0) { ok = false; return false; }
			return true;
		}
		// number / true / false / null — read until delimiter.
		while (p < end) {
			char ch = *p;
			if (ch == ',' || ch == '}' || ch == ']' ||
			    ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
				break;
			++p;
		}
		return true;
	}

	bool parseBool(bool& out) {
		skipWs();
		if (end - p >= 4 && std::memcmp(p, "true", 4) == 0) {
			p += 4; out = true; return true;
		}
		if (end - p >= 5 && std::memcmp(p, "false", 5) == 0) {
			p += 5; out = false; return true;
		}
		ok = false;
		return false;
	}

	bool parseInt(int& out) {
		skipWs();
		char* endp = nullptr;
		long v = std::strtol(p, &endp, 10);
		if (endp == p) { ok = false; return false; }
		out = (int)v;
		p = endp;
		return true;
	}

	bool parseNull() {
		skipWs();
		if (end - p >= 4 && std::memcmp(p, "null", 4) == 0) {
			p += 4; return true;
		}
		return false;
	}
};

// ---------------------------------------------------------------------
// Reader helpers for LobbySession.
// ---------------------------------------------------------------------
bool ReadSession(JsonCursor& c, LobbySession& s) {
	if (!c.match('{')) return false;
	// Handle empty object.
	if (c.match('}')) return true;
	for (;;) {
		std::string key;
		if (!c.parseString(key)) return false;
		if (!c.match(':')) return false;

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
		else { if (!c.parseNull() && !c.skipValue()) return false; }

		if (!c.ok) return false;
		if (c.match(',')) continue;
		if (c.match('}')) return true;
		return false;
	}
}

// ---------------------------------------------------------------------
// JSON string writer — escapes the characters required by RFC 8259.
// ---------------------------------------------------------------------
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
					out.push_back(ch);
				}
		}
	}
	out.push_back('"');
}

void AppendKV(std::string& out, const char* key, const std::string& v,
              bool& first) {
	if (!first) out.push_back(',');
	first = false;
	out.push_back('"'); out += key; out += "\":";
	AppendJsonString(out, v);
}

void AppendKV(std::string& out, const char* key, int v, bool& first) {
	if (!first) out.push_back(',');
	first = false;
	char tmp[24]; std::snprintf(tmp, sizeof tmp, "%d", v);
	out.push_back('"'); out += key; out += "\":"; out += tmp;
}

void AppendKV(std::string& out, const char* key, bool v, bool& first) {
	if (!first) out.push_back(',');
	first = false;
	out.push_back('"'); out += key; out += "\":";
	out += v ? "true" : "false";
}

// ---------------------------------------------------------------------
// Read the createResp shape.
// ---------------------------------------------------------------------
bool ReadCreateResp(JsonCursor& c, LobbyCreateResponse& r) {
	if (!c.match('{')) return false;
	if (c.match('}')) return true;
	for (;;) {
		std::string key;
		if (!c.parseString(key)) return false;
		if (!c.match(':')) return false;

		if      (key == "sessionId")  c.parseString(r.sessionId);
		else if (key == "token")      c.parseString(r.token);
		else if (key == "ttlSeconds") c.parseInt(r.ttlSeconds);
		else { if (!c.parseNull() && !c.skipValue()) return false; }

		if (!c.ok) return false;
		if (c.match(',')) continue;
		if (c.match('}')) return true;
		return false;
	}
}

// Read an error object: { "error": "..." }.  Returns empty on parse
// failure; the caller falls back to the HTTP status text.
std::string ReadError(const std::vector<uint8_t>& body) {
	JsonCursor c{(const char*)body.data(),
	             (const char*)body.data() + body.size()};
	if (!c.match('{')) return {};
	while (c.ok) {
		std::string key;
		if (!c.parseString(key)) return {};
		if (!c.match(':')) return {};
		if (key == "error") {
			std::string v;
			c.parseString(v);
			return v;
		}
		c.skipValue();
		if (!c.match(',')) break;
	}
	return {};
}

void FormatHttpError(std::string& out, const HttpResponse& r) {
	if (!r.error.empty()) { out = r.error; return; }
	std::string msg = ReadError(r.body);
	char buf[64];
	std::snprintf(buf, sizeof buf, "HTTP %d", r.status);
	out = buf;
	if (!r.statusText.empty()) { out.push_back(' '); out += r.statusText; }
	if (!msg.empty())          { out += ": "; out += msg; }
}

} // anonymous

// ---------------------------------------------------------------------
// LobbyClient
// ---------------------------------------------------------------------

bool LobbyClient::Create(const LobbyCreateRequest& req,
                         LobbyCreateResponse& out) {
	std::string body;
	body.reserve(256);
	body.push_back('{');
	bool first = true;
	AppendKV(body, "cartName",        req.cartName,        first);
	AppendKV(body, "hostHandle",      req.hostHandle,      first);
	AppendKV(body, "hostEndpoint",    req.hostEndpoint,    first);
	{
		// v3: serialize candidates vector as a semicolon-delimited
		// string so the JSON reader on either side stays minimal.
		std::string joined;
		for (size_t i = 0; i < req.candidates.size(); ++i) {
			if (i) joined.push_back(';');
			joined += req.candidates[i];
		}
		AppendKV(body, "candidates",  joined,              first);
	}
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
	body.push_back('}');

	HttpRequest hr;
	hr.method      = "POST";
	hr.host        = mEp.host.c_str();
	hr.port        = mEp.port;
	hr.path        = "/v1/session";
	hr.contentType = "application/json";
	hr.body        = (const uint8_t*)body.data();
	hr.bodyLen     = body.size();
	hr.timeoutMs   = mEp.timeoutMs;

	HttpResponse resp;
	HttpRequestSync(hr, resp); mLastStatus = resp.status;
	if (resp.status != 201) {
		FormatHttpError(mLastError, resp);
		return false;
	}
	JsonCursor c{(const char*)resp.body.data(),
	             (const char*)resp.body.data() + resp.body.size()};
	if (!ReadCreateResp(c, out)) {
		mLastError = "malformed createResp";
		return false;
	}
	mLastError.clear();
	return true;
}

bool LobbyClient::List(std::vector<LobbySession>& out) {
	out.clear();
	HttpRequest hr;
	hr.method    = "GET";
	hr.host      = mEp.host.c_str();
	hr.port      = mEp.port;
	hr.path      = "/v1/sessions";
	hr.timeoutMs = mEp.timeoutMs;

	HttpResponse resp;
	HttpRequestSync(hr, resp); mLastStatus = resp.status;
	if (resp.status != 200) {
		FormatHttpError(mLastError, resp);
		return false;
	}
	JsonCursor c{(const char*)resp.body.data(),
	             (const char*)resp.body.data() + resp.body.size()};
	c.skipWs();
	// Accept empty array, null, or array of sessions.
	if (c.parseNull()) { mLastError.clear(); return true; }
	if (!c.match('[')) {
		mLastError = "expected JSON array";
		return false;
	}
	if (c.match(']')) { mLastError.clear(); return true; }
	for (;;) {
		LobbySession s;
		if (!ReadSession(c, s)) {
			mLastError = "malformed session entry";
			return false;
		}
		out.push_back(std::move(s));
		if (c.match(',')) continue;
		if (c.match(']')) break;
		mLastError = "unterminated array";
		return false;
	}
	mLastError.clear();
	return true;
}

bool LobbyClient::Heartbeat(const std::string& sessionId,
                            const std::string& token,
                            int playerCount,
                            const std::string& state) {
	std::vector<LobbySessionHint> discard;
	return HeartbeatWithHints(sessionId, token, playerCount, state, discard);
}

bool LobbyClient::HeartbeatWithHints(const std::string& sessionId,
                                     const std::string& token,
                                     int playerCount,
                                     const std::string& state,
                                     std::vector<LobbySessionHint>& outHints) {
	outHints.clear();
	std::string body;
	body.reserve(128);
	body.push_back('{');
	bool first = true;
	AppendKV(body, "token",       token,       first);
	AppendKV(body, "playerCount", playerCount, first);
	if (!state.empty()) AppendKV(body, "state", state, first);
	body.push_back('}');

	std::string path = "/v1/session/";
	path += sessionId;
	path += "/heartbeat";

	HttpRequest hr;
	hr.method      = "POST";
	hr.host        = mEp.host.c_str();
	hr.port        = mEp.port;
	hr.path        = path.c_str();
	hr.contentType = "application/json";
	hr.body        = (const uint8_t*)body.data();
	hr.bodyLen     = body.size();
	hr.timeoutMs   = mEp.timeoutMs;

	HttpResponse resp;
	HttpRequestSync(hr, resp); mLastStatus = resp.status;
	if (resp.status != 200) {
		FormatHttpError(mLastError, resp);
		return false;
	}

	// Parse the response body for the optional v4 "hints" array.
	// Old servers return {"ttlSeconds":N} with no hints key — we
	// tolerate that and return an empty vector.
	JsonCursor c{(const char*)resp.body.data(),
	             (const char*)resp.body.data() + resp.body.size()};
	if (c.match('{') && !c.match('}')) {
		for (;;) {
			std::string key;
			if (!c.parseString(key)) break;
			if (!c.match(':'))       break;
			if (key == "hints") {
				if (!c.match('[')) { c.skipValue(); }
				else if (c.match(']')) { /* empty array */ }
				else {
					for (;;) {
						LobbySessionHint h;
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
	}
	mLastError.clear();
	return true;
}

bool LobbyClient::PostPeerHint(const std::string& sessionId,
                               const std::string& joinerHandle,
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

	std::string path = "/v1/session/";
	path += sessionId;
	path += "/peer-hint";

	HttpRequest hr;
	hr.method      = "POST";
	hr.host        = mEp.host.c_str();
	hr.port        = mEp.port;
	hr.path        = path.c_str();
	hr.contentType = "application/json";
	hr.body        = (const uint8_t*)body.data();
	hr.bodyLen     = body.size();
	hr.timeoutMs   = mEp.timeoutMs;

	HttpResponse resp;
	HttpRequestSync(hr, resp); mLastStatus = resp.status;
	if (resp.status != 200) {
		FormatHttpError(mLastError, resp);
		return false;
	}
	mLastError.clear();
	return true;
}

bool LobbyClient::Delete(const std::string& sessionId,
                         const std::string& token) {
	std::string path = "/v1/session/";
	path += sessionId;

	std::string header = "X-Session-Token: ";
	header += token;

	HttpRequest hr;
	hr.method      = "DELETE";
	hr.host        = mEp.host.c_str();
	hr.port        = mEp.port;
	hr.path        = path.c_str();
	hr.extraHeader = header.c_str();
	hr.timeoutMs   = mEp.timeoutMs;

	HttpResponse resp;
	HttpRequestSync(hr, resp); mLastStatus = resp.status;
	if (resp.status == 204 || resp.status == 404) {
		mLastError.clear();
		return true;
	}
	FormatHttpError(mLastError, resp);
	return false;
}

bool LobbyClient::Stats(LobbyStats& out) {
	out = LobbyStats{};
	HttpRequest hr;
	hr.method    = "GET";
	hr.host      = mEp.host.c_str();
	hr.port      = mEp.port;
	hr.path      = "/v1/stats";
	hr.timeoutMs = mEp.timeoutMs;

	HttpResponse resp;
	HttpRequestSync(hr, resp); mLastStatus = resp.status;
	if (resp.status != 200) {
		FormatHttpError(mLastError, resp);
		return false;
	}
	JsonCursor c{(const char*)resp.body.data(),
	             (const char*)resp.body.data() + resp.body.size()};
	if (!c.match('{')) { mLastError = "expected JSON object"; return false; }
	if (c.match('}')) { mLastError.clear(); return true; }
	for (;;) {
		std::string key;
		if (!c.parseString(key)) {
			mLastError = "malformed stats response"; return false;
		}
		if (!c.match(':')) {
			mLastError = "malformed stats response"; return false;
		}
		if      (key == "sessions") c.parseInt(out.sessions);
		else if (key == "waiting")  c.parseInt(out.waiting);
		else if (key == "playing")  c.parseInt(out.playing);
		else if (key == "hosts")    c.parseInt(out.hosts);
		else { if (!c.parseNull() && !c.skipValue()) {
			mLastError = "malformed stats response"; return false;
		} }
		if (!c.ok) { mLastError = "malformed stats response"; return false; }
		if (c.match(',')) continue;
		if (c.match('}')) break;
		mLastError = "malformed stats response";
		return false;
	}
	mLastError.clear();
	return true;
}

} // namespace ATNetplay
