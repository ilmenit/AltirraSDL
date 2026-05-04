// Altirra SDL3 netplay - lobby.ini parser (impl)

#include <stdafx.h>

#include "lobby_config.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ATNetplay {

const char* const kDefaultLobbyIni =
	"; Altirra SDL netplay — lobby directory.\n"
	"; Add more [sections] to federate across multiple lobby servers.\n"
	"; Set enabled=false to skip a lobby without deleting it.\n"
	"\n"
	"[official]\n"
	"name    = Altirra Official Lobby\n"
#if defined(__EMSCRIPTEN__)
	// WASM (browser) build: the page is served over HTTPS from
	// GitHub Pages, so mixed-content rules force every fetch to use
	// the TLS-fronted DuckDNS hostname.  Caddy on the lobby box
	// terminates TLS and reverse-proxies /v1/* (HTTP API) and
	// /netplay (WS bridge) to the appropriate localhost ports.
	"url     = http://altirra-lobby.duckdns.org\n"
#else
	"url     = http://158.180.27.70:8080\n"
#endif
	"region  = global\n"
	"enabled = true\n"
	"\n"
	"[lan]\n"
	"name      = LAN\n"
	"transport = udp-broadcast\n"
	"port      = 26101\n"
	"enabled   = true\n";

namespace {

void Trim(std::string& s) {
	size_t a = 0;
	while (a < s.size() &&
	       (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n'))
		++a;
	size_t b = s.size();
	while (b > a &&
	       (s[b-1] == ' ' || s[b-1] == '\t' || s[b-1] == '\r' || s[b-1] == '\n'))
		--b;
	s.assign(s, a, b - a);
}

void ToLower(std::string& s) {
	for (auto& c : s) c = (char)std::tolower((unsigned char)c);
}

bool ParseBool(const std::string& v, bool& out) {
	std::string lv = v;
	ToLower(lv);
	if (lv == "true" || lv == "yes" || lv == "on" || lv == "1") {
		out = true; return true;
	}
	if (lv == "false" || lv == "no" || lv == "off" || lv == "0") {
		out = false; return true;
	}
	return false;
}

} // anonymous

size_t ParseLobbyIni(const char* text, size_t len,
                     std::vector<LobbyEntry>& out,
                     std::vector<std::string>* warnings) {
	size_t added = 0;
	LobbyEntry cur;
	bool haveSection = false;

	auto flush = [&]() {
		if (!haveSection) return;
		// Validate.
		if (cur.kind == LobbyKind::Http) {
			if (cur.url.empty()) {
				if (warnings) warnings->push_back(
					"section [" + cur.section + "] missing `url`, dropped");
				return;
			}
		} else {
			if (cur.port == 0) {
				if (warnings) warnings->push_back(
					"section [" + cur.section + "] missing `port`, dropped");
				return;
			}
		}
		if (cur.name.empty()) cur.name = cur.section;
		out.push_back(std::move(cur));
		++added;
	};

	size_t i = 0;
	unsigned lineNo = 0;
	while (i < len) {
		++lineNo;
		// Read one line.
		size_t j = i;
		while (j < len && text[j] != '\n') ++j;
		std::string line(text + i, j - i);
		i = (j < len) ? j + 1 : j;

		// Strip comments (';' or '#') — only outside quoted values.
		// INI values are unquoted here, so plain scan is fine.
		for (size_t k = 0; k < line.size(); ++k) {
			if (line[k] == ';' || line[k] == '#') { line.erase(k); break; }
		}
		Trim(line);
		if (line.empty()) continue;

		if (line.front() == '[' && line.back() == ']') {
			flush();
			cur = LobbyEntry{};
			cur.section = line.substr(1, line.size() - 2);
			Trim(cur.section);
			ToLower(cur.section);
			haveSection = !cur.section.empty();
			if (!haveSection && warnings) {
				char buf[64];
				std::snprintf(buf, sizeof buf, "line %u: empty section", lineNo);
				warnings->emplace_back(buf);
			}
			continue;
		}

		size_t eq = line.find('=');
		if (eq == std::string::npos) {
			if (warnings) {
				char buf[96];
				std::snprintf(buf, sizeof buf, "line %u: no `=`, ignored",
					lineNo);
				warnings->emplace_back(buf);
			}
			continue;
		}
		std::string key = line.substr(0, eq);
		std::string val = line.substr(eq + 1);
		Trim(key); Trim(val);
		ToLower(key);

		if (!haveSection) {
			if (warnings) {
				char buf[96];
				std::snprintf(buf, sizeof buf,
					"line %u: key outside [section], ignored", lineNo);
				warnings->emplace_back(buf);
			}
			continue;
		}

		if      (key == "name")      cur.name   = val;
		else if (key == "url")       cur.url    = val;
		else if (key == "region")    cur.region = val;
		else if (key == "transport") {
			std::string lv = val; ToLower(lv);
			if (lv == "udp-broadcast" || lv == "lan") {
				cur.kind = LobbyKind::LanBroadcast;
			} else if (lv == "http" || lv == "https" || lv.empty()) {
				cur.kind = LobbyKind::Http;
			} else if (warnings) {
				warnings->push_back("unknown transport `" + val + "`");
			}
		}
		else if (key == "port") {
			long p = std::strtol(val.c_str(), nullptr, 10);
			if (p > 0 && p < 65536) cur.port = (uint16_t)p;
			else if (warnings) warnings->push_back(
				"port out of range in [" + cur.section + "]");
		}
		else if (key == "enabled") {
			bool b = true;
			if (ParseBool(val, b)) cur.enabled = b;
			else if (warnings) warnings->push_back(
				"enabled= must be true/false in [" + cur.section + "]");
		}
		// Unknown keys are silently ignored for forward-compat.
	}
	flush();
	return added;
}

void GetDefaultLobbies(std::vector<LobbyEntry>& out) {
	ParseLobbyIni(kDefaultLobbyIni,
		std::strlen(kDefaultLobbyIni), out, nullptr);
}

} // namespace ATNetplay
