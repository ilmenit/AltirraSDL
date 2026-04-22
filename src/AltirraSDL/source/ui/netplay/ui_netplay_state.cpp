//	AltirraSDL - Online Play UI state (impl)

#include <stdafx.h>

#include "ui_netplay_state.h"

#include "simulator.h"
#include "firmwaremanager.h"

#include <vd2/system/registry.h>
#include <vd2/system/VDString.h>
#include <vd2/system/zip.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/text.h>

#include "netplay/lobby_config.h"

extern ATSimulator g_sim;
extern VDStringA ATGetConfigDir();

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace ATNetplayUI {

namespace {

State g_state;
bool  g_initialized = false;

// Consonant/vowel tables for a pronounceable 8-char handle.  Avoids
// ambiguous letters (I/l/1, O/0) and vulgar syllables (by never
// placing two consonants in a row).
const char kConsonants[] = "bcdfghjkmnpqrstvwxyz";
const char kVowels[]     = "aeiou";

// Small xorshift for RNG so we don't pollute the global rand() sequence.
uint32_t NextRand() {
	static uint32_t s = 0;
	if (s == 0) {
		s = (uint32_t)(std::time(nullptr) ^ 0x9E3779B1u);
		if (s == 0) s = 1;
	}
	s ^= s << 13; s ^= s >> 17; s ^= s << 5;
	return s;
}

std::string AnonName() {
	char buf[9];
	for (int i = 0; i < 8; ++i) {
		const char* table = (i & 1) ? kVowels : kConsonants;
		size_t n = (i & 1) ? (sizeof kVowels - 1) : (sizeof kConsonants - 1);
		buf[i] = table[NextRand() % n];
	}
	buf[8] = 0;
	// Capitalise the first letter so it looks like a name.
	if (buf[0] >= 'a' && buf[0] <= 'z') buf[0] = (char)(buf[0] - 32);
	return std::string(buf, 8);
}

// Cache for ResolvedNickname().  Cleared on Shutdown().
std::string g_resolvedCache;

} // anonymous

State& GetState() { return g_state; }

std::string GenerateAnonymousNickname() { return AnonName(); }

const std::string& ResolvedNickname() {
	if (!g_state.prefs.nickname.empty()) return g_state.prefs.nickname;
	if (g_resolvedCache.empty()) g_resolvedCache = AnonName();
	return g_resolvedCache;
}

// -----------------------------------------------------------------------
// Registry I/O
// -----------------------------------------------------------------------

// Load all hosted hostedGames from the registry.  Keys:
//   GameCount       : int    — number of entries to read
//   Offer<N>_id      : str    — stable local id
//   Offer<N>_path    : str    — UTF-8 image path
//   Offer<N>_name    : str    — basename / library display name
//   Offer<N>_art     : str    — optional cartArtHash (hex)
//   Offer<N>_private : bool
//   Offer<N>_code    : str    — entry code (cleartext)
//   Offer<N>_enabled : bool
// Runtime fields (state/port/token) are never persisted; they rebuild
// on each Initialize from the reconcile loop.
static void LoadOffers(VDRegistryAppKey& key) {
	int count = key.getInt("GameCount", 0);
	if (count < 0) count = 0;
	if ((size_t)count > State::kMaxHostedGames)
		count = (int)State::kMaxHostedGames;

	g_state.hostedGames.clear();
	g_state.hostedGames.reserve(count);

	for (int i = 0; i < count; ++i) {
		char kn[24];
		HostedGame o;
		VDStringA buf;

		std::snprintf(kn, sizeof kn, "Game%d_id", i);
		if (key.getString(kn, buf)) o.id.assign(buf.c_str(), buf.size());
		std::snprintf(kn, sizeof kn, "Game%d_path", i);
		if (key.getString(kn, buf)) o.gamePath.assign(buf.c_str(), buf.size());
		std::snprintf(kn, sizeof kn, "Game%d_name", i);
		if (key.getString(kn, buf)) o.gameName.assign(buf.c_str(), buf.size());
		std::snprintf(kn, sizeof kn, "Game%d_art", i);
		if (key.getString(kn, buf)) o.cartArtHash.assign(buf.c_str(), buf.size());
		std::snprintf(kn, sizeof kn, "Game%d_private", i);
		o.isPrivate = key.getBool(kn, false);
		std::snprintf(kn, sizeof kn, "Game%d_code", i);
		if (key.getString(kn, buf)) o.entryCode.assign(buf.c_str(), buf.size());
		// Always start disabled on restart — users should explicitly
		// re-enable each entry so games don't silently re-list on the
		// lobby every time the app launches.
		std::snprintf(kn, sizeof kn, "Game%d_enabled", i);
		(void)key.getBool(kn, false);
		o.enabled = false;

		// MachineConfig — 8 keys.  Missing keys fall through to
		// struct defaults (800XL / 320K / NTSC / 6502 / BASIC off /
		// SIO on / no firmware CRC).
		std::snprintf(kn, sizeof kn, "Game%d_hwMode", i);
		o.config.hardwareMode = (ATHardwareMode)key.getInt(kn,
			(int)o.config.hardwareMode);
		std::snprintf(kn, sizeof kn, "Game%d_memMode", i);
		o.config.memoryMode = (ATMemoryMode)key.getInt(kn,
			(int)o.config.memoryMode);
		std::snprintf(kn, sizeof kn, "Game%d_videoStd", i);
		o.config.videoStandard = (ATVideoStandard)key.getInt(kn,
			(int)o.config.videoStandard);
		std::snprintf(kn, sizeof kn, "Game%d_cpuMode", i);
		o.config.cpuMode = (ATCPUMode)key.getInt(kn,
			(int)o.config.cpuMode);
		std::snprintf(kn, sizeof kn, "Game%d_basicEnabled", i);
		o.config.basicEnabled = key.getBool(kn, o.config.basicEnabled);
		std::snprintf(kn, sizeof kn, "Game%d_sioPatch", i);
		o.config.sioPatchEnabled = key.getBool(kn, o.config.sioPatchEnabled);
		std::snprintf(kn, sizeof kn, "Game%d_kernelCRC", i);
		o.config.kernelCRC32 = (uint32_t)key.getInt(kn, 0);
		std::snprintf(kn, sizeof kn, "Game%d_basicCRC", i);
		o.config.basicCRC32 = (uint32_t)key.getInt(kn, 0);

		// CRC cache — invalidated at use time if the outer file's
		// mtime stamp no longer matches.  Stamp is 64 bits stored as
		// two 32-bit halves since registry has no int64 accessor.
		std::snprintf(kn, sizeof kn, "Game%d_fileCRC", i);
		o.gameFileCRC32 = (uint32_t)key.getInt(kn, 0);
		std::snprintf(kn, sizeof kn, "Game%d_fileStampLo", i);
		uint32_t stampLo = (uint32_t)key.getInt(kn, 0);
		std::snprintf(kn, sizeof kn, "Game%d_fileStampHi", i);
		uint32_t stampHi = (uint32_t)key.getInt(kn, 0);
		o.gameFileStamp = ((uint64_t)stampHi << 32) | stampLo;

		// Drop obviously-invalid slots (no path or no name).
		if (o.gamePath.empty() || o.gameName.empty()) continue;
		if (o.id.empty()) o.id = GenerateHostedGameId();

		g_state.hostedGames.push_back(std::move(o));
	}
}

// Mirror of LoadOffers — writes the current hostedGames vector to registry.
static void SaveOffers(VDRegistryAppKey& key) {
	static const char *kSuffixes[] = {
		"_id", "_path", "_name", "_art", "_private", "_code", "_enabled",
		"_preset",  // legacy — scrub on save
		"_hwMode", "_memMode", "_videoStd", "_cpuMode",
		"_basicEnabled", "_sioPatch", "_kernelCRC", "_basicCRC",
		"_fileCRC", "_fileStampLo", "_fileStampHi",
	};

	// First, strip stale Game<N>_* keys left over from previous saves.
	const int kScan = (int)State::kMaxHostedGames + 4;
	for (int i = 0; i < kScan; ++i) {
		char kn[24];
		for (const char *sfx : kSuffixes) {
			std::snprintf(kn, sizeof kn, "Game%d%s", i, sfx);
			key.removeValue(kn);
		}
	}

	int n = (int)g_state.hostedGames.size();
	if ((size_t)n > State::kMaxHostedGames) n = (int)State::kMaxHostedGames;
	key.setInt("GameCount", n);
	for (int i = 0; i < n; ++i) {
		const HostedGame& o = g_state.hostedGames[i];
		char kn[24];
		std::snprintf(kn, sizeof kn, "Game%d_id", i);
		key.setString(kn, o.id.c_str());
		std::snprintf(kn, sizeof kn, "Game%d_path", i);
		key.setString(kn, o.gamePath.c_str());
		std::snprintf(kn, sizeof kn, "Game%d_name", i);
		key.setString(kn, o.gameName.c_str());
		std::snprintf(kn, sizeof kn, "Game%d_art", i);
		key.setString(kn, o.cartArtHash.c_str());
		std::snprintf(kn, sizeof kn, "Game%d_private", i);
		key.setBool(kn, o.isPrivate);
		std::snprintf(kn, sizeof kn, "Game%d_code", i);
		key.setString(kn, o.entryCode.c_str());
		std::snprintf(kn, sizeof kn, "Game%d_enabled", i);
		key.setBool(kn, o.enabled);
		std::snprintf(kn, sizeof kn, "Game%d_hwMode", i);
		key.setInt(kn, (int)o.config.hardwareMode);
		std::snprintf(kn, sizeof kn, "Game%d_memMode", i);
		key.setInt(kn, (int)o.config.memoryMode);
		std::snprintf(kn, sizeof kn, "Game%d_videoStd", i);
		key.setInt(kn, (int)o.config.videoStandard);
		std::snprintf(kn, sizeof kn, "Game%d_cpuMode", i);
		key.setInt(kn, (int)o.config.cpuMode);
		std::snprintf(kn, sizeof kn, "Game%d_basicEnabled", i);
		key.setBool(kn, o.config.basicEnabled);
		std::snprintf(kn, sizeof kn, "Game%d_sioPatch", i);
		key.setBool(kn, o.config.sioPatchEnabled);
		std::snprintf(kn, sizeof kn, "Game%d_kernelCRC", i);
		key.setInt(kn, (int)o.config.kernelCRC32);
		std::snprintf(kn, sizeof kn, "Game%d_basicCRC", i);
		key.setInt(kn, (int)o.config.basicCRC32);
		std::snprintf(kn, sizeof kn, "Game%d_fileCRC", i);
		key.setInt(kn, (int)o.gameFileCRC32);
		std::snprintf(kn, sizeof kn, "Game%d_fileStampLo", i);
		key.setInt(kn, (int)(uint32_t)(o.gameFileStamp & 0xFFFFFFFFu));
		std::snprintf(kn, sizeof kn, "Game%d_fileStampHi", i);
		key.setInt(kn, (int)(uint32_t)(o.gameFileStamp >> 32));
	}
}

namespace {
const char *HardwareModeShort(ATHardwareMode m) {
	switch (m) {
		case kATHardwareMode_800:    return "800";
		case kATHardwareMode_800XL:  return "800XL";
		case kATHardwareMode_1200XL: return "1200XL";
		case kATHardwareMode_130XE:  return "130XE";
		case kATHardwareMode_1400XL: return "1400XL";
		case kATHardwareMode_XEGS:   return "XEGS";
		case kATHardwareMode_5200:   return "5200";
		default:                     return "?";
	}
}
const char *MemoryModeShort(ATMemoryMode m) {
	switch (m) {
		case kATMemoryMode_8K:    return "8K";
		case kATMemoryMode_16K:   return "16K";
		case kATMemoryMode_24K:   return "24K";
		case kATMemoryMode_32K:   return "32K";
		case kATMemoryMode_40K:   return "40K";
		case kATMemoryMode_48K:   return "48K";
		case kATMemoryMode_52K:   return "52K";
		case kATMemoryMode_64K:   return "64K";
		case kATMemoryMode_128K:  return "128K";
		case kATMemoryMode_320K:  return "320K";
		case kATMemoryMode_576K:  return "576K";
		case kATMemoryMode_1088K: return "1088K";
		case kATMemoryMode_256K:  return "256K";
		case kATMemoryMode_576K_Compy: return "576K Compy";
		case kATMemoryMode_320K_Compy: return "320K Compy";
		default:                  return "?";
	}
}
const char *VideoStandardShort(ATVideoStandard v) {
	switch (v) {
		case kATVideoStandard_NTSC:   return "NTSC";
		case kATVideoStandard_PAL:    return "PAL";
		case kATVideoStandard_SECAM:  return "SECAM";
		case kATVideoStandard_NTSC50: return "NTSC50";
		case kATVideoStandard_PAL60:  return "PAL60";
		default:                      return "?";
	}
}
} // anonymous

const char *MachineConfigSummary(const MachineConfig& c) {
	static char buf[96];
	std::snprintf(buf, sizeof buf, "%s | %s | %s | BASIC %s",
		HardwareModeShort(c.hardwareMode),
		MemoryModeShort(c.memoryMode),
		VideoStandardShort(c.videoStandard),
		c.basicEnabled ? "on" : "off");
	return buf;
}

uint32_t ComputeFirmwareCRC32(uint64_t firmwareId) {
	if (firmwareId == 0) return 0;
	ATFirmwareManager *fwm = g_sim.GetFirmwareManager();
	if (!fwm) return 0;
	vdfastvector<uint8> buf;
	if (!fwm->LoadFirmware(firmwareId, nullptr, 0, 0, nullptr, nullptr, &buf))
		return 0;
	if (buf.empty()) return 0;
	return VDCRCTable::CRC32.CRC(buf.data(), buf.size());
}

const char *FirmwareNameForCRC(uint32_t crc) {
	if (crc == 0) return "";
	ATFirmwareManager *fwm = g_sim.GetFirmwareManager();
	if (!fwm) return "Unknown";

	vdvector<ATFirmwareInfo> fwList;
	fwm->GetFirmwareList(fwList);
	for (const auto& fw : fwList) {
		if (!fw.mbVisible) continue;
		if (ComputeFirmwareCRC32(fw.mId) != crc) continue;
		static thread_local std::string out;
		out = VDTextWToU8(fw.mName).c_str();
		return out.c_str();
	}
	return "Unknown";
}

std::string HostedGameSignature(const std::string& path,
                                const MachineConfig& c) {
	char buf[384];
	std::snprintf(buf, sizeof buf,
		"%s|hw=%d|mem=%d|vs=%d|cpu=%d|basic=%d|sio=%d"
		"|kc=%08X|bc=%08X",
		path.c_str(),
		(int)c.hardwareMode, (int)c.memoryMode,
		(int)c.videoStandard, (int)c.cpuMode,
		c.basicEnabled ? 1 : 0, c.sioPatchEnabled ? 1 : 0,
		c.kernelCRC32, c.basicCRC32);
	return buf;
}

MachineConfig CaptureCurrentMachineConfig() {
	MachineConfig c;
	c.hardwareMode    = g_sim.GetHardwareMode();
	c.memoryMode      = g_sim.GetMemoryMode();
	c.videoStandard   = g_sim.GetVideoStandard();
	c.cpuMode         = g_sim.GetCPUMode();
	c.basicEnabled    = g_sim.IsBASICEnabled();
	c.sioPatchEnabled = g_sim.IsSIOPatchEnabled();
	c.kernelCRC32     = ComputeFirmwareCRC32(g_sim.GetKernelId());
	c.basicCRC32      = ComputeFirmwareCRC32(g_sim.GetBasicId());
	return c;
}

std::string FriendlyLobbyError(const std::string& raw, int httpStatus) {
	// HTTP-level statuses take priority — they carry specific meaning.
	if (httpStatus == 429)
		return "Lobby is rate-limiting - please wait a moment";
	if (httpStatus == 401 || httpStatus == 403)
		return "Lobby refused the request (auth)";
	if (httpStatus == 404)
		return "Lobby endpoint not found (server may have changed)";
	if (httpStatus == 409)
		return "Lobby says this session is already registered";
	if (httpStatus >= 500 && httpStatus < 600) {
		char buf[64];
		std::snprintf(buf, sizeof buf,
			"Lobby server error (HTTP %d) - try again later",
			httpStatus);
		return buf;
	}
	if (httpStatus >= 400 && httpStatus < 500) {
		char buf[64];
		std::snprintf(buf, sizeof buf,
			"Lobby rejected the request (HTTP %d)",
			httpStatus);
		return buf;
	}

	// Network-level errors: match on known strings from http_minimal.cpp.
	if (raw.empty())
		return "Could not reach lobby server";
	if (raw == "recv() failed" ||
	    raw == "send() failed")
		return "Lobby server closed the connection unexpectedly";
	if (raw == "recv hdr timeout" ||
	    raw == "recv body timeout")
		return "Lobby server timed out";
	if (raw == "connection closed before headers")
		return "Lobby dropped the connection before replying";
	if (raw == "malformed status line" ||
	    raw == "chunked transfer-encoding not supported" ||
	    raw == "response too large")
		return "Lobby returned data in an unexpected format";
	if (raw.find("getaddrinfo") != std::string::npos ||
	    raw.find("DNS") != std::string::npos)
		return "Could not resolve lobby hostname (check DNS)";
	if (raw.find("connect") != std::string::npos ||
	    raw.find("refused") != std::string::npos)
		return "Could not connect to lobby (server down?)";

	// Unknown — prefix so users know it's a lobby issue.
	return std::string("Lobby error: ") + raw;
}

namespace {

std::vector<ATNetplay::LobbyEntry> s_lobbyCache;
bool s_lobbyCacheLoaded = false;

// Build the full path to $configDir/lobby.ini.
VDStringA LobbyIniPath() {
	VDStringA p = ATGetConfigDir();
	if (!p.empty() && p.back() != '/' && p.back() != '\\')
		p.push_back('/');
	p.append("lobby.ini");
	return p;
}

void LoadLobbyConfigIntoCache() {
	s_lobbyCache.clear();
	const VDStringA path = LobbyIniPath();
	const std::wstring wpath = VDTextU8ToW(path.c_str(), -1).c_str();

	// First-run: write the default lobby.ini so users can discover it
	// and edit without needing documentation.  Read failure on a
	// subsequent run is silently accepted — the user may be running
	// from read-only media.
	if (!VDDoesPathExist(wpath.c_str())) {
		try {
			VDFileStream fs(wpath.c_str(),
				nsVDFile::kWrite | nsVDFile::kCreateAlways
				| nsVDFile::kDenyAll);
			fs.Write(ATNetplay::kDefaultLobbyIni,
				(sint32)std::strlen(ATNetplay::kDefaultLobbyIni));
		} catch (...) {
			// Fall through — we'll just use in-memory defaults.
		}
	}

	// Read the file.  If anything goes wrong, fall back to defaults.
	std::string contents;
	try {
		VDFileStream fs(wpath.c_str(),
			nsVDFile::kRead | nsVDFile::kOpenExisting | nsVDFile::kDenyNone);
		sint64 sz = fs.Length();
		if (sz > 0 && sz < 128 * 1024) {
			contents.resize((size_t)sz);
			fs.Read(contents.data(), (long)sz);
		}
	} catch (...) {
		contents.clear();
	}

	if (!contents.empty()) {
		ATNetplay::ParseLobbyIni(contents.data(), contents.size(),
			s_lobbyCache, nullptr);
	}
	if (s_lobbyCache.empty()) {
		ATNetplay::GetDefaultLobbies(s_lobbyCache);
	}
	s_lobbyCacheLoaded = true;
}

} // anonymous

const std::vector<ATNetplay::LobbyEntry>& GetConfiguredLobbies() {
	if (!s_lobbyCacheLoaded) LoadLobbyConfigIntoCache();
	return s_lobbyCache;
}

void ReloadLobbyConfig() {
	s_lobbyCacheLoaded = false;
	LoadLobbyConfigIntoCache();
}

HostedGame* FindHostedGame(const std::string& id) {
	if (id.empty()) return nullptr;
	for (auto& o : g_state.hostedGames)
		if (o.id == id) return &o;
	return nullptr;
}

std::string GenerateHostedGameId() {
	char buf[17];
	for (int i = 0; i < 16; ++i) {
		uint32_t r = NextRand();
		static const char hex[] = "0123456789abcdef";
		buf[i] = hex[r & 0xF];
	}
	buf[16] = 0;
	return std::string(buf, 16);
}

void Initialize() {
	if (g_initialized) return;
	g_initialized = true;

	// Warm the lobby-config cache so $configDir/lobby.ini is created
	// on first run even before the user opens a netplay window.
	(void)GetConfiguredLobbies();

	VDRegistryAppKey key("Netplay", true);

	// Nickname.
	VDStringA handle;
	if (key.getString("Handle", handle)) {
		g_state.prefs.nickname.assign(handle.c_str(), handle.size());
	}
	g_state.prefs.isAnonymous     = key.getBool("Anonymous", false);
	// Legacy: the old AcceptMode pref (auto-accept vs prompt) was
	// removed because silent auto-accept is too intrusive.  Every
	// incoming join now prompts.  Scrub the stale key so a future
	// reader can't mistake it for a live setting.
	key.removeValue("AcceptMode");
	g_state.prefs.notif.flashWindow  = key.getBool("NotifyFlash", true);
	g_state.prefs.notif.systemNotify = key.getBool("NotifySystem", true);
	g_state.prefs.notif.playChime    = key.getBool("NotifyChime", true);
	g_state.prefs.defaultInputDelayLan      = key.getInt("InputDelayLan", 3);
	g_state.prefs.defaultInputDelayInternet = key.getInt("InputDelayNet", 4);
	g_state.prefs.focusOnAttention = key.getBool("FocusOnAttention", false);
	// Legacy: scrub the dead "Show manual-IP join" checkbox.
	key.removeValue("AdvancedManualIp");

	VDStringA code;
	if (key.getString("LastEntryCode", code)) {
		g_state.prefs.lastEntryCode.assign(code.c_str(), code.size());
	}

	// Scrub the legacy LastAddPreset key; the new Add-Game dialog
	// uses MachineConfig fields captured from the live sim instead.
	key.removeValue("LastAddPreset");

	g_state.prefs.showSessionHUD  = key.getBool("ShowSessionHUD", true);
	g_state.prefs.showBrowserArt  = key.getBool("ShowBrowserArt", true);

	// Clamp values that may have come from a corrupted registry write.
	if (g_state.prefs.defaultInputDelayLan < 1)   g_state.prefs.defaultInputDelayLan = 3;
	if (g_state.prefs.defaultInputDelayLan > 10)  g_state.prefs.defaultInputDelayLan = 10;
	if (g_state.prefs.defaultInputDelayInternet < 1)  g_state.prefs.defaultInputDelayInternet = 4;
	if (g_state.prefs.defaultInputDelayInternet > 10) g_state.prefs.defaultInputDelayInternet = 10;

	LoadOffers(key);
}

void SaveToRegistry() {
	VDRegistryAppKey key("Netplay", true);
	key.setString("Handle",           g_state.prefs.nickname.c_str());
	key.setBool  ("Anonymous",        g_state.prefs.isAnonymous);
	key.setBool  ("NotifyFlash",      g_state.prefs.notif.flashWindow);
	key.setBool  ("NotifySystem",     g_state.prefs.notif.systemNotify);
	key.setBool  ("NotifyChime",      g_state.prefs.notif.playChime);
	key.setInt   ("InputDelayLan",    g_state.prefs.defaultInputDelayLan);
	key.setInt   ("InputDelayNet",    g_state.prefs.defaultInputDelayInternet);
	key.setBool  ("FocusOnAttention", g_state.prefs.focusOnAttention);
	key.setString("LastEntryCode",    g_state.prefs.lastEntryCode.c_str());
	key.setBool  ("ShowSessionHUD",   g_state.prefs.showSessionHUD);
	key.setBool  ("ShowBrowserArt",   g_state.prefs.showBrowserArt);

	SaveOffers(key);
}

void Shutdown() {
	if (g_initialized) SaveToRegistry();
	g_state = State{};
	g_resolvedCache.clear();
	g_initialized = false;
}

void Navigate(Screen next) {
	if (next == Screen::Closed) {
		g_state.backStack.clear();
		g_state.screen = Screen::Closed;
		return;
	}
	if (g_state.screen != Screen::Closed)
		g_state.backStack.push_back(g_state.screen);
	g_state.screen = next;
}

bool Back() {
	if (g_state.backStack.empty()) {
		g_state.screen = Screen::Closed;
		return false;
	}
	g_state.screen = g_state.backStack.back();
	g_state.backStack.pop_back();
	return true;
}

} // namespace ATNetplayUI
