//	AltirraSDL - Device-specific configuration dialogs
//	Individual per-device settings renderers matching Windows uiconfdev*.cpp.
//	Called from ui_devconfig.cpp's DispatchDeviceDialog().

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/device.h>
#include "devicemanager.h"
#include "idevhdimage.h"
#include "ui_main.h"
#include "ui_devconfig.h"
#include "ui_devconfig_internal.h"

// =========================================================================
// Covox — base address + channels
// =========================================================================

bool RenderCovoxConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	// Must match Windows kRanges[] in uiconfdevcovox.cpp exactly
	static const char *kAddressLabels[] = {
		"$D100-D1FF", "$D280-D2FF", "$D500-D5FF",
		"$D600-D63F", "$D600-D6FF", "$D700-D7FF"
	};
	static const uint32 kAddressBase[] = { 0xD100, 0xD280, 0xD500, 0xD600, 0xD600, 0xD700 };
	static const uint32 kAddressSize[] = { 0x100, 0x80, 0x100, 0x40, 0x100, 0x100 };

	static const char *kChannelLabels[] = { "1 channel (mono)", "4 channels (stereo)" };

	if (st.justOpened) {
		uint32 base = props.GetUint32("base", 0xD600);
		uint32 size = props.GetUint32("size", 0);
		uint32 maxSize = size ? size : 0x100;
		// Find largest range with matching base address within specified size
		// (matches Windows "find largest range" algorithm)
		st.combo[0] = 4; // default: index 4 = $D600-D6FF
		uint32 bestSize = 0;
		for (int i = 0; i < 6; ++i) {
			if (kAddressBase[i] == base && kAddressSize[i] <= maxSize && kAddressSize[i] > bestSize) {
				st.combo[0] = i;
				bestSize = kAddressSize[i];
			}
		}
		st.combo[1] = props.GetUint32("channels", 4) > 1 ? 1 : 0;
	}

	ImGui::Combo("Address range", &st.combo[0], kAddressLabels, 6);
	ImGui::Combo("Channels", &st.combo[1], kChannelLabels, 2);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		int sel = st.combo[0];
		if (sel >= 0 && sel < 6) {
			props.SetUint32("base", kAddressBase[sel]);
			props.SetUint32("size", kAddressSize[sel]);
		}
		props.SetUint32("channels", st.combo[1] > 0 ? 4 : 1);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// 850 Interface — SIO emulation level + checkboxes
// =========================================================================

bool Render850Config(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kSIOLabels[] = { "None", "Minimal", "Full" };

	if (st.justOpened) {
		uint32 level = props.GetUint32("emulevel", 0);
		st.combo[0] = (level < 3) ? (int)level : 0;
		st.check[0] = props.GetBool("unthrottled", false);
		st.check[1] = props.GetBool("baudex", false);
	}

	ImGui::Combo("SIO emulation level", &st.combo[0], kSIOLabels, 3);
	ImGui::Checkbox("Disable throttling", &st.check[0]);
	ImGui::Checkbox("Extended baud rates", &st.check[1]);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		if (st.combo[0] > 0)
			props.SetUint32("emulevel", (uint32)st.combo[0]);
		if (st.check[0]) props.SetBool("unthrottled", true);
		if (st.check[1]) props.SetBool("baudex", true);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// 850 Full Emulation — 4 per-port baud rates
// (matches Windows ATUIConfDev850Full using generic config with serbaud1-4)
// =========================================================================

bool Render850FullConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	// Labels and values must match Windows AddChoice order exactly
	static const char *kBaudLabels[] = {
		"Auto", "45.5 baud", "50 baud", "56.875 baud",
		"75 baud", "110 baud", "134.5 baud", "150 baud",
		"300 baud", "600 baud", "1200 baud", "1800 baud",
		"2400 baud", "4800 baud", "9600 baud"
	};
	static const int kBaudValues[] = {
		0, 2, 3, 4, 5, 6, 7, 8, 1, 10, 11, 12, 13, 14, 15
	};
	static const int kNumBaud = 15;

	if (st.justOpened) {
		for (int p = 0; p < 4; ++p) {
			char key[16];
			snprintf(key, sizeof(key), "serbaud%d", p + 1);
			uint32 val = props.GetUint32(key, 0);
			st.combo[p] = 0; // default Auto
			for (int i = 0; i < kNumBaud; ++i) {
				if ((uint32)kBaudValues[i] == val) { st.combo[p] = i; break; }
			}
		}
	}

	for (int p = 0; p < 4; ++p) {
		char label[32];
		snprintf(label, sizeof(label), "Port %d baud rate", p + 1);
		ImGui::Combo(label, &st.combo[p], kBaudLabels, kNumBaud);
	}

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		for (int p = 0; p < 4; ++p) {
			char key[16];
			snprintf(key, sizeof(key), "serbaud%d", p + 1);
			int idx = st.combo[p];
			if (idx >= 0 && idx < kNumBaud)
				props.SetUint32(key, (uint32)kBaudValues[idx]);
		}
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Modem — full modem config (1030, 835, 1400xl, generic modem)
// Note: SX212 and PocketModem have their own dialogs below
// =========================================================================

bool RenderModemConfig(ATPropertySet& props, ATDeviceConfigState& st, bool fullEmu, bool is835, bool hasConnectRate) {
	// fullEmu: hide throttling, SIO level, connect rate, check rate (1030full, 835full)
	// is835: hide SIO level, connect rate, check rate (835, 1400xl)
	// hasConnectRate: show connection speed + check rate (only generic "modem")
	// 1030 uses fullEmu=false, is835=false, hasConnectRate=false
	static const char *kTermTypes[] = {
		"(none)", "ansi", "dec-vt52", "dec-vt100", "vt52", "vt100", "vt102", "vt320", "(custom)"
	};
	static const int kNumTermPresets = 8; // entries before "(custom)"
	static const char *kNetModes[] = {
		"Disabled - no audio or delays",
		"Minimal - simulate dialing but skip handshaking phase",
		"Full - simulate dialing and handshaking"
	};
	static const char *kNetModeValues[] = { "none", "minimal", "full" };
	static const char *kSIOLabels[] = { "None", "Minimal", "Full" };

	static const uint32 kConnSpeeds[] = {
		300, 600, 1200, 2400, 4800, 7200, 9600, 12000, 14400, 19200, 38400, 57600, 115200, 230400
	};
	static const char *kConnSpeedLabels[] = {
		"300", "600", "1200", "2400", "4800", "7200", "9600", "12000",
		"14400", "19200", "38400", "57600", "115200", "230400"
	};

	if (st.justOpened) {
		uint32 port = props.GetUint32("port", 0);
		st.check[0] = (port > 0); // accept connections
		snprintf(st.portBuf, sizeof(st.portBuf), "%u", port > 0 ? port : 9000);
		st.check[1] = props.GetBool("outbound", true);
		st.check[2] = props.GetBool("telnet", true);
		st.check[3] = props.GetBool("telnetlf", true);
		st.check[4] = props.GetBool("ipv6", true);
		st.check[5] = props.GetBool("unthrottled", false);
		st.check[6] = props.GetBool("check_rate", false);

		// Terminal type — support custom values not in preset list
		const wchar_t *tt = props.GetString("termtype");
		st.combo[0] = 0;
		st.mappingBuf[0] = 0;
		if (tt && *tt) {
			VDStringA ttU8 = WToU8(tt);
			bool found = false;
			for (int i = 1; i < kNumTermPresets; ++i) {
				if (!strcmp(kTermTypes[i], ttU8.c_str())) {
					st.combo[0] = i;
					found = true;
					break;
				}
			}
			if (!found) {
				st.combo[0] = kNumTermPresets; // "(custom)"
				snprintf(st.mappingBuf, sizeof(st.mappingBuf), "%s", ttU8.c_str());
			}
		}

		// Network mode
		const wchar_t *nm = props.GetString("netmode", L"full");
		st.combo[1] = 2; // default full
		if (nm) {
			VDStringA nmU8 = WToU8(nm);
			for (int i = 0; i < 3; ++i) {
				if (!strcmp(kNetModeValues[i], nmU8.c_str())) {
					st.combo[1] = i;
					break;
				}
			}
		}

		// SIO level
		st.combo[2] = (int)props.GetUint32("emulevel", 0);
		if (st.combo[2] > 2) st.combo[2] = 0;

		// Connect speed
		uint32 speed = props.GetUint32("connect_rate", 9600);
		st.combo[3] = 6; // 9600 default
		for (int i = 0; i < 14; ++i) {
			if (kConnSpeeds[i] >= speed) { st.combo[3] = i; break; }
		}

		// Dial address/service
		const wchar_t *da = props.GetString("dialaddr", L"");
		snprintf(st.addrBuf, sizeof(st.addrBuf), "%s", WToU8(da).c_str());
		const wchar_t *ds = props.GetString("dialsvc", L"");
		snprintf(st.svcBuf, sizeof(st.svcBuf), "%s", WToU8(ds).c_str());
	}

	// Incoming connections
	ImGui::SeparatorText("Incoming");
	ImGui::Checkbox("Accept connections", &st.check[0]);
	ImGui::BeginDisabled(!st.check[0]);
	ImGui::InputText("Listen port", st.portBuf, sizeof(st.portBuf));
	ImGui::Checkbox("Accept IPv6", &st.check[4]);
	ImGui::EndDisabled();

	// Outgoing connections
	ImGui::SeparatorText("Outgoing");
	ImGui::Checkbox("Allow outbound", &st.check[1]);
	ImGui::BeginDisabled(!st.check[1]);
	ImGui::InputText("Dial address", st.addrBuf, sizeof(st.addrBuf));
	ImGui::InputText("Dial service", st.svcBuf, sizeof(st.svcBuf));
	ImGui::Combo("Terminal type", &st.combo[0], kTermTypes, kNumTermPresets + 1);
	if (st.combo[0] == kNumTermPresets)
		ImGui::InputText("Custom type", st.mappingBuf, sizeof(st.mappingBuf));
	ImGui::EndDisabled();

	// Protocol
	ImGui::SeparatorText("Protocol");
	ImGui::Checkbox("Telnet protocol", &st.check[2]);
	ImGui::BeginDisabled(!st.check[2]);
	ImGui::Checkbox("Telnet LF conversion", &st.check[3]);
	ImGui::EndDisabled();

	ImGui::Combo("Network mode", &st.combo[1], kNetModes, 3);

	// Connection speed only for generic modem (not 1030/835/1400xl)
	if (!fullEmu && !is835 && hasConnectRate) {
		ImGui::Combo("Connection speed", &st.combo[3], kConnSpeedLabels, 14);
	}

	if (!fullEmu) {
		ImGui::Checkbox("Disable throttling", &st.check[5]);
	}

	// SIO emulation level for generic modem and 1030 (not 835/1400xl/full emu)
	if (!fullEmu && !is835) {
		ImGui::Combo("SIO emulation level", &st.combo[2], kSIOLabels, 3);
		// check_rate only for generic modem (not 1030)
		if (hasConnectRate)
			ImGui::Checkbox("Require matched DTE rate", &st.check[6]);
	}

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		if (st.check[0]) {
			unsigned p = 0;
			sscanf(st.portBuf, "%u", &p);
			if (p >= 1 && p <= 65535)
				props.SetUint32("port", p);
		}
		props.SetBool("outbound", st.check[1]);
		props.SetBool("telnet", st.check[2]);
		props.SetBool("telnetlf", st.check[3]);
		props.SetBool("ipv6", st.check[4]);
		if (!fullEmu)
			props.SetBool("unthrottled", st.check[5]);
		// check_rate only exists on generic modem (not 1030/835)
		if (!fullEmu && !is835 && hasConnectRate)
			props.SetBool("check_rate", st.check[6]);

		// connect_rate only for generic modem (not 1030/835/1400xl)
		if (!fullEmu && !is835 && hasConnectRate && st.combo[3] >= 0 && st.combo[3] < 14)
			props.SetUint32("connect_rate", kConnSpeeds[st.combo[3]]);

		if (st.combo[0] > 0 && st.combo[0] < kNumTermPresets)
			props.SetString("termtype", U8ToW(kTermTypes[st.combo[0]]).c_str());
		else if (st.combo[0] == kNumTermPresets && st.mappingBuf[0])
			props.SetString("termtype", U8ToW(st.mappingBuf).c_str());

		props.SetString("netmode", U8ToW(kNetModeValues[st.combo[1]]).c_str());

		if (!fullEmu && !is835 && st.combo[2] > 0)
			props.SetUint32("emulevel", (uint32)st.combo[2]);

		if (st.addrBuf[0])
			props.SetString("dialaddr", U8ToW(st.addrBuf).c_str());
		if (st.svcBuf[0])
			props.SetString("dialsvc", U8ToW(st.svcBuf).c_str());

		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// SX212 Modem — 300/1200 baud radio + SIO level (None/Full only)
// =========================================================================

bool RenderSX212Config(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kTermTypes[] = {
		"(none)", "ansi", "dec-vt52", "dec-vt100", "vt52", "vt100", "vt102", "vt320", "(custom)"
	};
	static const int kNumTermPresets = 8;
	static const char *kNetModes[] = {
		"Disabled - no audio or delays",
		"Minimal - simulate dialing but skip handshaking phase",
		"Full - simulate dialing and handshaking"
	};
	static const char *kNetModeValues[] = { "none", "minimal", "full" };

	if (st.justOpened) {
		uint32 port = props.GetUint32("port", 0);
		st.check[0] = (port > 0);
		snprintf(st.portBuf, sizeof(st.portBuf), "%u", port > 0 ? port : 9000);
		st.check[1] = props.GetBool("outbound", true);
		st.check[2] = props.GetBool("telnet", true);
		st.check[3] = props.GetBool("telnetlf", true);
		st.check[4] = props.GetBool("ipv6", true);
		st.check[5] = props.GetBool("unthrottled", false);

		// Connect rate: 300 or 1200 (radio buttons)
		st.combo[0] = (props.GetUint32("connect_rate", 1200) <= 300) ? 0 : 1;

		// SIO level: None or Full only
		st.combo[1] = (props.GetUint32("emulevel", 0) > 0) ? 1 : 0;

		// Terminal type — support custom values
		const wchar_t *tt = props.GetString("termtype");
		st.combo[2] = 0;
		st.mappingBuf[0] = 0;
		if (tt && *tt) {
			VDStringA ttU8 = WToU8(tt);
			bool found = false;
			for (int i = 1; i < kNumTermPresets; ++i)
				if (!strcmp(kTermTypes[i], ttU8.c_str())) { st.combo[2] = i; found = true; break; }
			if (!found) {
				st.combo[2] = kNumTermPresets;
				snprintf(st.mappingBuf, sizeof(st.mappingBuf), "%s", ttU8.c_str());
			}
		}

		// Network mode
		const wchar_t *nm = props.GetString("netmode", L"full");
		st.combo[3] = 2;
		if (nm) {
			VDStringA nmU8 = WToU8(nm);
			for (int i = 0; i < 3; ++i)
				if (!strcmp(kNetModeValues[i], nmU8.c_str())) { st.combo[3] = i; break; }
		}

		const wchar_t *da = props.GetString("dialaddr", L"");
		snprintf(st.addrBuf, sizeof(st.addrBuf), "%s", WToU8(da).c_str());
		const wchar_t *ds = props.GetString("dialsvc", L"");
		snprintf(st.svcBuf, sizeof(st.svcBuf), "%s", WToU8(ds).c_str());
	}

	// Incoming
	ImGui::SeparatorText("Incoming");
	ImGui::Checkbox("Accept connections", &st.check[0]);
	ImGui::BeginDisabled(!st.check[0]);
	ImGui::InputText("Listen port", st.portBuf, sizeof(st.portBuf));
	ImGui::Checkbox("Accept IPv6", &st.check[4]);
	ImGui::EndDisabled();

	// Outgoing
	ImGui::SeparatorText("Outgoing");
	ImGui::Checkbox("Allow outbound", &st.check[1]);
	ImGui::BeginDisabled(!st.check[1]);
	ImGui::InputText("Dial address", st.addrBuf, sizeof(st.addrBuf));
	ImGui::InputText("Dial service", st.svcBuf, sizeof(st.svcBuf));
	ImGui::Combo("Terminal type", &st.combo[2], kTermTypes, kNumTermPresets + 1);
	if (st.combo[2] == kNumTermPresets)
		ImGui::InputText("Custom type", st.mappingBuf, sizeof(st.mappingBuf));
	ImGui::EndDisabled();

	// Protocol
	ImGui::SeparatorText("Protocol");
	ImGui::Checkbox("Telnet protocol", &st.check[2]);
	ImGui::BeginDisabled(!st.check[2]);
	ImGui::Checkbox("Telnet LF conversion", &st.check[3]);
	ImGui::EndDisabled();

	ImGui::Combo("Network mode", &st.combo[3], kNetModes, 3);

	// Connection speed: 300/1200 radio buttons (not full combo)
	ImGui::SeparatorText("Connection speed");
	ImGui::RadioButton("300 baud", &st.combo[0], 0);
	ImGui::SameLine();
	ImGui::RadioButton("1200 baud", &st.combo[0], 1);

	// SIO emulation: None/Full only (not 3-level)
	static const char *kSIOLabels[] = { "None", "Full" };
	ImGui::Combo("SIO emulation level", &st.combo[1], kSIOLabels, 2);

	ImGui::Checkbox("Disable throttling", &st.check[5]);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		if (st.check[0]) {
			unsigned p = 0;
			sscanf(st.portBuf, "%u", &p);
			if (p >= 1 && p <= 65535)
				props.SetUint32("port", p);
		}
		props.SetBool("outbound", st.check[1]);
		props.SetBool("telnet", st.check[2]);
		props.SetBool("telnetlf", st.check[3]);
		props.SetBool("ipv6", st.check[4]);
		props.SetBool("unthrottled", st.check[5]);
		props.SetUint32("connect_rate", st.combo[0] == 0 ? 300 : 1200);
		// SIO level: 0=None, kAT850SIOEmulationLevel_Full for Full
		// Windows uses kAT850SIOEmulationLevel_Full which is typically 2
		props.SetUint32("emulevel", st.combo[1] > 0 ? 2 : 0);

		if (st.combo[2] > 0 && st.combo[2] < kNumTermPresets)
			props.SetString("termtype", U8ToW(kTermTypes[st.combo[2]]).c_str());
		else if (st.combo[2] == kNumTermPresets && st.mappingBuf[0])
			props.SetString("termtype", U8ToW(st.mappingBuf).c_str());
		props.SetString("netmode", U8ToW(kNetModeValues[st.combo[3]]).c_str());

		if (st.addrBuf[0])
			props.SetString("dialaddr", U8ToW(st.addrBuf).c_str());
		if (st.svcBuf[0])
			props.SetString("dialsvc", U8ToW(st.svcBuf).c_str());
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Pocket Modem — minimal modem config (no speed/SIO/throttle/check_rate)
// =========================================================================

bool RenderPocketModemConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kTermTypes[] = {
		"(none)", "ansi", "dec-vt52", "dec-vt100", "vt52", "vt100", "vt102", "vt320", "(custom)"
	};
	static const int kNumTermPresets = 8;

	if (st.justOpened) {
		uint32 port = props.GetUint32("port", 0);
		st.check[0] = (port > 0);
		snprintf(st.portBuf, sizeof(st.portBuf), "%u", port > 0 ? port : 9000);
		st.check[1] = props.GetBool("outbound", true);
		st.check[2] = props.GetBool("telnet", true);
		st.check[3] = props.GetBool("telnetlf", true);
		st.check[4] = props.GetBool("ipv6", true);

		// Terminal type — support custom values
		const wchar_t *tt = props.GetString("termtype");
		st.combo[0] = 0;
		st.mappingBuf[0] = 0;
		if (tt && *tt) {
			VDStringA ttU8 = WToU8(tt);
			bool found = false;
			for (int i = 1; i < kNumTermPresets; ++i)
				if (!strcmp(kTermTypes[i], ttU8.c_str())) { st.combo[0] = i; found = true; break; }
			if (!found) {
				st.combo[0] = kNumTermPresets;
				snprintf(st.mappingBuf, sizeof(st.mappingBuf), "%s", ttU8.c_str());
			}
		}

		const wchar_t *da = props.GetString("dialaddr", L"");
		snprintf(st.addrBuf, sizeof(st.addrBuf), "%s", WToU8(da).c_str());
		const wchar_t *ds = props.GetString("dialsvc", L"");
		snprintf(st.svcBuf, sizeof(st.svcBuf), "%s", WToU8(ds).c_str());
	}

	// Incoming
	ImGui::SeparatorText("Incoming");
	ImGui::Checkbox("Accept connections", &st.check[0]);
	ImGui::BeginDisabled(!st.check[0]);
	ImGui::InputText("Listen port", st.portBuf, sizeof(st.portBuf));
	ImGui::Checkbox("Accept IPv6", &st.check[4]);
	ImGui::EndDisabled();

	// Outgoing
	ImGui::SeparatorText("Outgoing");
	ImGui::Checkbox("Allow outbound", &st.check[1]);
	ImGui::BeginDisabled(!st.check[1]);
	ImGui::InputText("Dial address", st.addrBuf, sizeof(st.addrBuf));
	ImGui::InputText("Dial service", st.svcBuf, sizeof(st.svcBuf));
	ImGui::Combo("Terminal type", &st.combo[0], kTermTypes, kNumTermPresets + 1);
	if (st.combo[0] == kNumTermPresets)
		ImGui::InputText("Custom type", st.mappingBuf, sizeof(st.mappingBuf));
	ImGui::EndDisabled();

	// Protocol
	ImGui::SeparatorText("Protocol");
	ImGui::Checkbox("Telnet protocol", &st.check[2]);
	ImGui::BeginDisabled(!st.check[2]);
	ImGui::Checkbox("Telnet LF conversion", &st.check[3]);
	ImGui::EndDisabled();

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		if (st.check[0]) {
			unsigned p = 0;
			sscanf(st.portBuf, "%u", &p);
			if (p >= 1 && p <= 65535)
				props.SetUint32("port", p);
		}
		props.SetBool("outbound", st.check[1]);
		if (st.combo[0] > 0 && st.combo[0] < kNumTermPresets)
			props.SetString("termtype", U8ToW(kTermTypes[st.combo[0]]).c_str());
		else if (st.combo[0] == kNumTermPresets && st.mappingBuf[0])
			props.SetString("termtype", U8ToW(st.mappingBuf).c_str());
		props.SetBool("telnet", st.check[2]);
		props.SetBool("telnetlf", st.check[3]);
		props.SetBool("ipv6", st.check[4]);

		if (st.addrBuf[0])
			props.SetString("dialaddr", U8ToW(st.addrBuf).c_str());
		if (st.svcBuf[0])
			props.SetString("dialsvc", U8ToW(st.svcBuf).c_str());
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Create VHD Image dialog
// =========================================================================

struct CreateVHDState {
	bool open = false;
	bool justOpened = false;
	char pathBuf[1024] = {};
	char parentPathBuf[1024] = {};
	int diskType = 1;		// 0=Fixed, 1=Dynamic, 2=Differencing
	int sizeInMB = 8;
	int sizeInSectors = 8 * 2048;
	int geometryMode = 0;	// 0=Auto, 1=Manual
	int heads = 15;
	int spt = 63;
	bool inhibitSizeSync = false;
	bool createdOK = false;
	uint32 resultSectorCount = 0;
	char resultPath[1024] = {};
	char errorMsg[512] = {};
	bool showError = false;
	bool showSuccess = false;
};

static CreateVHDState s_createVHD;

static void CreateVHDUpdateGeometry(CreateVHDState& st) {
	// VHD spec geometry calculation (matches Windows ATUIDialogCreateVHDImage2::UpdateGeometry)
	uint32 secCount = std::min<uint32>((uint32)st.sizeInSectors, 65535U * 16 * 255);

	if (secCount >= 65535U * 16 * 63) {
		st.spt = 255;
		st.heads = 16;
	} else {
		st.spt = 17;

		uint32 tracks = secCount / 17;
		uint32 heads = (tracks + 1023) >> 10;

		if (heads < 4)
			heads = 4;

		if (tracks >= (heads * 1024) || heads > 16) {
			st.spt = 31;
			heads = 16;
			tracks = secCount / 31;
		}

		if (tracks >= (heads * 1024)) {
			st.spt = 63;
			heads = 16;
		}

		st.heads = (int)heads;
	}
}

// Ensure path has .vhd extension (matches Windows save dialog default extension behavior)
static void EnsureVHDExtension(char *buf, size_t bufSize) {
	size_t len = strlen(buf);
	if (len == 0)
		return;

	// Check if it already ends with .vhd (case-insensitive)
	if (len >= 4) {
		const char *ext = buf + len - 4;
		if (ext[0] == '.' &&
			(ext[1] == 'v' || ext[1] == 'V') &&
			(ext[2] == 'h' || ext[2] == 'H') &&
			(ext[3] == 'd' || ext[3] == 'D'))
			return;
	}

	// Append .vhd if there's no extension at all (no dot after last separator)
	const char *lastSep = strrchr(buf, '/');
	const char *lastDot = strrchr(buf, '.');
	if (!lastDot || (lastSep && lastDot < lastSep)) {
		if (len + 4 < bufSize) {
			strcat(buf, ".vhd");
		}
	}
}

// Renders the Create VHD dialog. Returns true when a VHD was successfully created.
static bool RenderCreateVHDDialog(CreateVHDState& st) {
	if (!st.open)
		return false;

	bool result = false;

	ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (ImGui::Begin("Create VHD Image", &st.open,
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking))
	{
		// Path
		if (InputTextWithBrowse("Path", st.pathBuf, sizeof(st.pathBuf), "browseVHDPath")) {
			static const SDL_DialogFileFilter kVHDFilters[] = {
				{ "Virtual hard disk image", "vhd" },
				{ "All files", "*" },
			};
			DevBrowseForSaveFile(st.pathBuf, sizeof(st.pathBuf), kVHDFilters, 1);
		}

		// Type
		ImGui::SeparatorText("Type");
		ImGui::RadioButton("Fixed size disk image", &st.diskType, 0);
		ImGui::RadioButton("Dynamic disk image", &st.diskType, 1);
		ImGui::RadioButton("Differencing disk image", &st.diskType, 2);

		bool isDifferencing = (st.diskType == 2);

		// Parent path (only for differencing — matches Windows UpdateEnables)
		if (isDifferencing) {
			ImGui::SeparatorText("Parent");
			if (InputTextWithBrowse("Parent path", st.parentPathBuf, sizeof(st.parentPathBuf), "browseVHDParent")) {
				static const SDL_DialogFileFilter kVHDFilters[] = {
					{ "Virtual hard disk image", "vhd" },
					{ "All files", "*" },
				};
				DevBrowseForFile(st.parentPathBuf, sizeof(st.parentPathBuf), kVHDFilters, 1);
			}
		}

		// Size (disabled for differencing disks — inherits from parent)
		if (!isDifferencing) {
			ImGui::SeparatorText("Size");

			if (ImGui::InputInt("Size (MB)", &st.sizeInMB)) {
				st.sizeInMB = std::clamp(st.sizeInMB, 1, 4095);
				if (!st.inhibitSizeSync) {
					st.inhibitSizeSync = true;
					st.sizeInSectors = st.sizeInMB * 2048;
					st.inhibitSizeSync = false;
				}
				if (st.geometryMode == 0)
					CreateVHDUpdateGeometry(st);
			}

			if (ImGui::InputInt("Size (sectors)", &st.sizeInSectors)) {
				st.sizeInSectors = std::clamp(st.sizeInSectors, 2048, (int)0x7FFFFFFEU);
				if (!st.inhibitSizeSync) {
					st.inhibitSizeSync = true;
					st.sizeInMB = st.sizeInSectors >> 11;
					st.inhibitSizeSync = false;
				}
				if (st.geometryMode == 0)
					CreateVHDUpdateGeometry(st);
			}

			// Geometry
			ImGui::SeparatorText("Geometry");
			ImGui::RadioButton("Auto", &st.geometryMode, 0);
			ImGui::SameLine();
			ImGui::RadioButton("Manual", &st.geometryMode, 1);

			if (st.geometryMode == 0) {
				// Show computed geometry as read-only
				ImGui::BeginDisabled();
				ImGui::InputInt("Heads", &st.heads);
				ImGui::InputInt("Sectors per track", &st.spt);
				ImGui::EndDisabled();
			} else {
				ImGui::InputInt("Heads", &st.heads);
				st.heads = std::clamp(st.heads, 1, 16);
				ImGui::InputInt("Sectors per track", &st.spt);
				st.spt = std::clamp(st.spt, 1, 255);
			}
		}

		// OK / Cancel
		ImGui::Separator();
		if (ImGui::Button("OK", ImVec2(120, 0))) {
			if (st.pathBuf[0] == 0) {
				snprintf(st.errorMsg, sizeof(st.errorMsg), "Please specify a path for the VHD image.");
				st.showError = true;
			} else if (isDifferencing && st.parentPathBuf[0] == 0) {
				snprintf(st.errorMsg, sizeof(st.errorMsg), "Parent path is needed for a differencing disk image.");
				st.showError = true;
			} else {
				// Ensure .vhd extension
				EnsureVHDExtension(st.pathBuf, sizeof(st.pathBuf));

				try {
					ATIDEVHDImage parentVhd;
					ATIDEVHDImage vhd;

					VDStringW vhdPath(U8ToW(st.pathBuf));

					if (isDifferencing) {
						VDStringW parentPath(U8ToW(st.parentPathBuf));
						parentVhd.Init(parentPath.c_str(), false, true);
					}

					bool dynamic = (st.diskType != 0);

					if (st.geometryMode == 0)
						CreateVHDUpdateGeometry(st);

					vhd.InitNew(vhdPath.c_str(),
						(uint8)st.heads, (uint8)st.spt,
						(uint32)st.sizeInSectors,
						dynamic,
						isDifferencing ? &parentVhd : nullptr);
					vhd.Flush();

					st.resultSectorCount = (uint32)st.sizeInSectors;
					snprintf(st.resultPath, sizeof(st.resultPath), "%s", st.pathBuf);
					st.createdOK = true;
					st.showSuccess = true;
				} catch(const MyUserAbortError&) {
					// User cancelled progress dialog (e.g. during fixed disk creation) — silent
				} catch(const MyError& e) {
					snprintf(st.errorMsg, sizeof(st.errorMsg), "VHD creation failed: %s", e.c_str());
					st.showError = true;
				}
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120, 0))) {
			st.open = false;
		}

		// Error popup
		if (st.showError) {
			ImGui::OpenPopup("VHD Error");
			st.showError = false;
		}
		if (ImGui::BeginPopupModal("VHD Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::TextWrapped("%s", st.errorMsg);
			if (ImGui::Button("OK", ImVec2(120, 0)))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		// Success popup
		if (st.showSuccess) {
			ImGui::OpenPopup("VHD Created");
			st.showSuccess = false;
		}
		if (ImGui::BeginPopupModal("VHD Created", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::Text("VHD creation was successful.");
			if (ImGui::Button("OK", ImVec2(120, 0))) {
				ImGui::CloseCurrentPopup();
				st.open = false;
				result = true;
			}
			ImGui::EndPopup();
		}
	}
	ImGui::End();

	return result;
}

// =========================================================================
// Hard Disk config — helper to compute CHS from sector count
// (matches Windows ATUIDialogDeviceHardDisk::UpdateCapacityBySectorCount)
// =========================================================================

static void HDComputeCHSFromSectors(uint64 sectors, int& cylinders, int& heads, int& spt) {
	// Use fixed heads=15, spt=63 and derive cylinders (matches Windows)
	spt = 63;
	heads = 15;
	cylinders = 1;

	if (sectors > UINT32_MAX)
		sectors = UINT32_MAX;

	if (sectors)
		cylinders = (int)(sectors / ((uint32)heads * (uint32)spt));
}

// =========================================================================
// Hard Disk — path + CHS geometry + options
// Matches Windows IDD_DEVICE_HARDDISK / ATUIDialogDeviceHardDisk
// =========================================================================

bool RenderHardDiskConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	// intVal[0..2] = cylinders, heads, sectors_per_track
	// intVal[3..4] = resulting size (MB), LBA sector count (display only)
	// check[0]     = read only
	// combo[0]     = device speed: 0=fast(solid-state), 1=slow(spinning)
	// addrBuf      = last probed VHD path (for auto-geometry detection)
	if (st.justOpened) {
		const wchar_t *path = props.GetString("path", L"");
		snprintf(st.pathBuf, sizeof(st.pathBuf), "%s", WToU8(path).c_str());
		st.intVal[0] = (int)props.GetUint32("cylinders", 0);
		st.intVal[1] = (int)props.GetUint32("heads", 0);
		st.intVal[2] = (int)props.GetUint32("sectors_per_track", 0);
		st.check[0] = !props.GetBool("write_enabled", false); // inverted: readonly
		st.combo[0] = props.GetBool("solid_state", false) ? 0 : 1;

		// Initialize addrBuf to current path so VHD auto-probe doesn't
		// overwrite stored geometry on dialog open (matches Windows: auto-
		// detect only runs on explicit browse, not on dialog open).
		snprintf(st.addrBuf, sizeof(st.addrBuf), "%s", st.pathBuf);

		// Compute initial size display from CHS or stored sector count
		if (st.intVal[0] > 0 && st.intVal[1] > 0 && st.intVal[2] > 0) {
			uint64 sectors = (uint64)st.intVal[0] * (uint64)st.intVal[1] * (uint64)st.intVal[2];
			st.intVal[3] = (int)(sectors >> 11);
			st.intVal[4] = (int)std::min<uint64>(sectors, INT_MAX);
		} else {
			uint32 totalSectors = props.GetUint32("sectors", 0);
			if (totalSectors) {
				int c, h, s;
				HDComputeCHSFromSectors(totalSectors, c, h, s);
				st.intVal[0] = c;
				st.intVal[1] = h;
				st.intVal[2] = s;
				st.intVal[3] = (int)(totalSectors >> 11);
				st.intVal[4] = (int)totalSectors;
			} else {
				st.intVal[3] = 0;
				st.intVal[4] = 0;
			}
		}
	}

	if (InputTextWithBrowse("Image path", st.pathBuf, sizeof(st.pathBuf), "browseHD")) {
		static const SDL_DialogFileFilter kHDFilters[] = {
			{ "Hard disk images", "vhd;iso;img;bin" },
			{ "All files", "*" },
		};
		DevBrowseForFile(st.pathBuf, sizeof(st.pathBuf), kHDFilters, 2);
	}

	// When the path changes to a .vhd file, try to read geometry from it
	// (matches Windows OnBrowseImage behavior).
	// Use addrBuf to store the last probed path — re-probe only when path changes.
	if (st.pathBuf[0]) {
		size_t plen = strlen(st.pathBuf);
		if (plen >= 4 && strcasecmp(st.pathBuf + plen - 4, ".vhd") == 0) {
			if (strcmp(st.pathBuf, st.addrBuf) != 0) {
				snprintf(st.addrBuf, sizeof(st.addrBuf), "%s", st.pathBuf);
				try {
					vdrefptr<ATIDEVHDImage> vhdImage(new ATIDEVHDImage);
					VDStringW wpath(U8ToW(st.pathBuf));
					vhdImage->Init(wpath.c_str(), false, false);
					uint32 secCount = vhdImage->GetSectorCount();
					int c, h, s;
					HDComputeCHSFromSectors(secCount, c, h, s);
					st.intVal[0] = c;
					st.intVal[1] = h;
					st.intVal[2] = s;
					st.intVal[3] = (int)(secCount >> 11);
					st.intVal[4] = (int)secCount;
				} catch(const MyError&) {
					// Not a valid VHD or not found — don't auto-fill
				}
			}
		}
	}

	if (ImGui::Button("Create VHD Image...")) {
		s_createVHD = CreateVHDState{};
		s_createVHD.open = true;
		s_createVHD.justOpened = true;
		CreateVHDUpdateGeometry(s_createVHD);
	}

	// Render the Create VHD dialog (if open) and apply results
	if (RenderCreateVHDDialog(s_createVHD)) {
		// VHD was created — fill in path and geometry from sector count
		// (matches Windows OnCreateVHD -> UpdateCapacityBySectorCount)
		snprintf(st.pathBuf, sizeof(st.pathBuf), "%s", s_createVHD.resultPath);
		uint32 secCount = s_createVHD.resultSectorCount;
		int c, h, s;
		HDComputeCHSFromSectors(secCount, c, h, s);
		st.intVal[0] = c;
		st.intVal[1] = h;
		st.intVal[2] = s;
		st.intVal[3] = (int)(secCount >> 11);
		st.intVal[4] = (int)secCount;
		snprintf(st.addrBuf, sizeof(st.addrBuf), "%s", st.pathBuf); // mark as already probed
	}

	ImGui::SeparatorText("Geometry");

	// Live CHS <-> size sync (matches Windows UpdateCapacityByCHS)
	bool chsChanged = false;
	chsChanged |= ImGui::InputInt("Cylinders", &st.intVal[0]);
	chsChanged |= ImGui::InputInt("Heads", &st.intVal[1]);
	chsChanged |= ImGui::InputInt("Sectors/track", &st.intVal[2]);

	if (chsChanged) {
		st.intVal[0] = std::max(st.intVal[0], 0);
		st.intVal[1] = std::clamp(st.intVal[1], 0, 16);
		st.intVal[2] = std::clamp(st.intVal[2], 0, 255);

		if (st.intVal[0] > 0 && st.intVal[1] > 0 && st.intVal[2] > 0) {
			uint64 sectors = (uint64)st.intVal[0] * (uint64)st.intVal[1] * (uint64)st.intVal[2];
			if (sectors <= UINT32_MAX) {
				st.intVal[3] = (int)(sectors >> 11);
				st.intVal[4] = (int)sectors;
			} else {
				st.intVal[3] = 0;
				st.intVal[4] = 0;
			}
		} else {
			st.intVal[3] = 0;
			st.intVal[4] = 0;
		}
	}

	// Resulting size display (matches Windows IDC_IDE_SIZE / IDC_SECTOR_COUNT)
	ImGui::BeginDisabled();
	ImGui::InputInt("Resulting size (MB)", &st.intVal[3]);
	ImGui::InputInt("LBA sector count", &st.intVal[4]);
	ImGui::EndDisabled();

	ImGui::SeparatorText("Device speed");
	ImGui::RadioButton("Solid-state (fast)", &st.combo[0], 0);
	ImGui::SameLine();
	ImGui::RadioButton("Spinning platter (slow)", &st.combo[0], 1);

	ImGui::SeparatorText("More options");
	ImGui::Checkbox("Read only", &st.check[0]);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		if (st.pathBuf[0] == 0) {
			// Path required
			return false;
		}
		props.Clear();
		props.SetString("path", U8ToW(st.pathBuf).c_str());
		if (st.intVal[0] > 0 && st.intVal[1] > 0 && st.intVal[2] > 0) {
			props.SetUint32("cylinders", (uint32)std::clamp(st.intVal[0], 0, 16777216));
			props.SetUint32("heads", (uint32)std::clamp(st.intVal[1], 0, 16));
			props.SetUint32("sectors_per_track", (uint32)std::clamp(st.intVal[2], 0, 255));
			uint32 sectors = (uint32)st.intVal[0] * (uint32)st.intVal[1] * (uint32)st.intVal[2];
			props.SetUint32("sectors", sectors);
		}
		props.SetBool("write_enabled", !st.check[0]);
		props.SetBool("solid_state", st.combo[0] == 0);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Black Box — DIP switches + sector size + RAM
// =========================================================================

bool RenderBlackBoxConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kSwitchLabels[] = {
		"1: Ignore printer fault line",
		"2: Enable hard disk and high speed floppy SIO (16K ROM only)",
		"3: Enable printer port",
		"4: Enable RS232 port",
		"5: Enable printer linefeeds",
		"6: ProWriter printer mode",
		"7: MIO compatibility mode",
		"8: Unused"
	};
	static const char *kRAMLabels[] = { "8K", "32K", "64K" };
	// Windows stores raw K values (8, 32, 64), not byte counts
	static const uint32 kRAMValues[] = { 8, 32, 64 };

	if (st.justOpened) {
		uint32 dipsw = props.GetUint32("dipsw", 0x0F);
		for (int i = 0; i < 8; ++i)
			st.check[i] = (dipsw & (1 << i)) != 0;

		uint32 blksize = props.GetUint32("blksize", 512);
		st.combo[0] = (blksize == 256) ? 0 : 1;

		uint32 ramsize = props.GetUint32("ramsize", 8);
		st.combo[1] = 0;
		for (int i = 0; i < 3; ++i)
			if (kRAMValues[i] == ramsize) st.combo[1] = i;
	}

	ImGui::SeparatorText("DIP Switches");
	for (int i = 0; i < 8; ++i)
		ImGui::Checkbox(kSwitchLabels[i], &st.check[i]);

	ImGui::SeparatorText("Hard Disk");
	static const char *kBlkLabels[] = { "256 bytes/sector", "512 bytes/sector" };
	ImGui::Combo("Sector size", &st.combo[0], kBlkLabels, 2);

	ImGui::Combo("SRAM size", &st.combo[1], kRAMLabels, 3);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		uint32 dipsw = 0;
		for (int i = 0; i < 8; ++i)
			if (st.check[i]) dipsw |= (1 << i);
		props.SetUint32("dipsw", dipsw);
		props.SetUint32("blksize", st.combo[0] == 0 ? 256 : 512);
		props.SetUint32("ramsize", kRAMValues[st.combo[1]]);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// VBXE — version + base address + shared memory
// =========================================================================

bool RenderVBXEConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kVersionLabels[] = { "FX 1.20", "FX 1.24", "FX 1.26" };
	static const uint32 kVersionValues[] = { 120, 124, 126 };

	if (st.justOpened) {
		uint32 ver = props.GetUint32("version", 126);
		st.combo[0] = 2;
		for (int i = 0; i < 3; ++i)
			if (kVersionValues[i] == ver) st.combo[0] = i;

		st.combo[1] = props.GetBool("alt_page", false) ? 1 : 0;
		st.check[0] = props.GetBool("shared_mem", false);
	}

	ImGui::Combo("Core version", &st.combo[0], kVersionLabels, 3);

	static const char *kBaseLabels[] = { "$D600 (standard)", "$D700 (alternate)" };
	ImGui::Combo("Base address", &st.combo[1], kBaseLabels, 2);
	ImGui::Checkbox("Enable shared memory", &st.check[0]);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		props.SetUint32("version", kVersionValues[st.combo[0]]);
		if (st.combo[1] > 0) props.SetBool("alt_page", true);
		if (st.check[0]) props.SetBool("shared_mem", true);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Sound Board — version + base address
// =========================================================================

bool RenderSoundBoardConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kVersionLabels[] = { "1.1 (VBXE-based)", "1.2 (with multiplier)", "2.0 Preview" };
	static const uint32 kVersionValues[] = { 110, 120, 200 };
	static const char *kBaseLabels[] = { "$D280", "$D2C0", "$D600", "$D700" };
	static const uint32 kBaseValues[] = { 0xD280, 0xD2C0, 0xD600, 0xD700 };

	if (st.justOpened) {
		uint32 ver = props.GetUint32("version", 120);
		st.combo[0] = 1;
		for (int i = 0; i < 3; ++i)
			if (kVersionValues[i] == ver) st.combo[0] = i;

		uint32 base = props.GetUint32("base", 0xD2C0);
		st.combo[1] = 1;
		for (int i = 0; i < 4; ++i)
			if (kBaseValues[i] == base) st.combo[1] = i;
	}

	ImGui::Combo("Version", &st.combo[0], kVersionLabels, 3);
	ImGui::Combo("Base address", &st.combo[1], kBaseLabels, 4);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		props.SetUint32("version", kVersionValues[st.combo[0]]);
		props.SetUint32("base", kBaseValues[st.combo[1]]);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Disk Drive Full Emulation — drive select
// =========================================================================

bool RenderDiskDriveFullConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kDriveLabels[] = {
		"Drive 1 (D1:)", "Drive 2 (D2:)", "Drive 3 (D3:)", "Drive 4 (D4:)"
	};

	if (st.justOpened) {
		st.combo[0] = (int)props.GetUint32("id", 0);
		if (st.combo[0] > 3) st.combo[0] = 0;
	}

	ImGui::Combo("Drive select", &st.combo[0], kDriveLabels, 4);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		props.SetUint32("id", (uint32)st.combo[0]);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// SIDE 3 — version + LED + recovery
// =========================================================================

bool RenderSIDE3Config(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kVersionLabels[] = {
		"SIDE 3 (JED 1.1: 2MB RAM)",
		"SIDE 3.1 (JED 1.4: 8MB RAM, enhanced DMA)"
	};

	if (st.justOpened) {
		st.check[0] = props.GetBool("led_enable", true);
		st.check[1] = props.GetBool("recovery", false);
		st.combo[0] = props.GetUint32("version", 10) > 10 ? 1 : 0;
	}

	ImGui::Combo("Version", &st.combo[0], kVersionLabels, 2);
	ImGui::Checkbox("Enable activity LED", &st.check[0]);
	ImGui::Checkbox("Recovery mode", &st.check[1]);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		props.SetBool("led_enable", st.check[0]);
		props.SetBool("recovery", st.check[1]);
		props.SetUint32("version", st.combo[0] > 0 ? 14 : 10);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// XEP-80 — joystick port
// =========================================================================

bool RenderXEP80Config(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kPortLabels[] = { "Port 1", "Port 2 (default)", "Port 3 (400/800 only)", "Port 4 (400/800 only)" };

	if (st.justOpened) {
		// Windows stores as 1-based (1-4), default 2
		uint32 port = props.GetUint32("port", 2);
		st.combo[0] = (int)std::clamp<uint32>(port - 1, 0, 3);
	}

	ImGui::Combo("Joystick port", &st.combo[0], kPortLabels, 4);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		// Store as 1-based (matching Windows)
		props.SetUint32("port", (uint32)(st.combo[0] & 3) + 1);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Corvus — port selection
// =========================================================================

bool RenderCorvusConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kPortLabels[] = { "Ports 3+4 (standard, but 400/800 only)", "Ports 1+2 (XL/XE compatible)" };

	if (st.justOpened) {
		st.combo[0] = props.GetBool("altports", false) ? 1 : 0;
	}

	ImGui::Combo("Controller ports", &st.combo[0], kPortLabels, 2);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		if (st.combo[0] > 0) props.SetBool("altports", true);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Veronica — version
// =========================================================================

bool RenderVeronicaConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kVersionLabels[] = { "V1 (three RAM chips)", "V2 (single RAM chip)" };

	if (st.justOpened) {
		st.combo[0] = props.GetBool("version1", false) ? 0 : 1;
	}

	ImGui::Combo("Version", &st.combo[0], kVersionLabels, 2);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		if (st.combo[0] == 0) props.SetBool("version1", true);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Dongle — port + mapping
// =========================================================================

bool RenderDongleConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kPortLabels[] = { "Port 1", "Port 2", "Port 3 (400/800 only)", "Port 4 (400/800 only)" };

	if (st.justOpened) {
		st.combo[0] = (int)props.GetUint32("port", 0);
		if (st.combo[0] > 3) st.combo[0] = 0;
		const wchar_t *m = props.GetString("mapping", L"FFFFFFFFFFFFFFFF");
		snprintf(st.mappingBuf, sizeof(st.mappingBuf), "%s", WToU8(m).c_str());
	}

	ImGui::Combo("Joystick port", &st.combo[0], kPortLabels, 4);
	ImGui::InputText("Mapping (hex)", st.mappingBuf, sizeof(st.mappingBuf));
	ImGui::SetItemTooltip("16-character hex string mapping input bits to output bits");

	// Validate mapping: must be exactly 16 hex digits (matching Windows validation)
	int mappingLen = (int)strlen(st.mappingBuf);
	bool mappingValid = (mappingLen == 16);
	if (mappingValid) {
		for (int i = 0; i < 16; ++i) {
			char c = st.mappingBuf[i];
			if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) {
				mappingValid = false;
				break;
			}
		}
	}
	if (!mappingValid)
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "The mapping string must be a set of 16 hexadecimal digits.");

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		if (!mappingValid)
			return false;
		props.Clear();
		props.SetUint32("port", (uint32)st.combo[0]);
		if (st.mappingBuf[0])
			props.SetString("mapping", U8ToW(st.mappingBuf).c_str());
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

