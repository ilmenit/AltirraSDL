// Altirra SDL netplay - tiny JSON cursor
//
// Header-only, allocation-light JSON reader.  Shared between
// lobby_client.cpp (reads server responses) and the lobby server
// (reads client request bodies).  Only what the lobby protocol
// actually needs: strings, ints, bools, null, and a skip helper for
// unknown fields.
//
// Not a full JSON parser — no number precision checks, no Unicode
// surrogate-pair handling (high surrogates decode to U+FFFD), no
// pretty error reporting.  The wire payloads are machine-generated
// by the matching server/client in this tree, so the parser is
// calibrated to exactly that shape.

#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

namespace ATLobby {

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

	// Skip any JSON value (for unknown keys).
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

// Small string-builder for JSON output (server side).
struct JsonBuilder {
	std::string s;

	void raw(const char *str)  { s.append(str); }
	void raw(char c)           { s.push_back(c); }
	void raw(const std::string& str) { s.append(str); }

	// Escape + quote a string value.  Control chars → \uXXXX.
	void str(const char *v, size_t n) {
		s.push_back('"');
		for (size_t i = 0; i < n; ++i) {
			unsigned char c = (unsigned char)v[i];
			switch (c) {
				case '"':  s.append("\\\"");  break;
				case '\\': s.append("\\\\"); break;
				case '\b': s.append("\\b"); break;
				case '\f': s.append("\\f"); break;
				case '\n': s.append("\\n"); break;
				case '\r': s.append("\\r"); break;
				case '\t': s.append("\\t"); break;
				default:
					if (c < 0x20) {
						char buf[8];
						std::snprintf(buf, sizeof buf, "\\u%04x", c);
						s.append(buf);
					} else {
						s.push_back((char)c);
					}
			}
		}
		s.push_back('"');
	}

	void str(const std::string& v)   { str(v.data(), v.size()); }
	void str(const char *v)          { str(v, v ? std::strlen(v) : 0); }

	void num(long long v) {
		char buf[32];
		std::snprintf(buf, sizeof buf, "%lld", v);
		s.append(buf);
	}

	void boolean(bool v) { s.append(v ? "true" : "false"); }

	// Key-value pair inside an object.  Caller inserts commas between
	// calls; this helper just writes `"key":` and returns for value.
	void key(const char *k) {
		str(k);
		s.push_back(':');
	}
};

} // namespace ATLobby
