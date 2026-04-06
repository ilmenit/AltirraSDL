//	AltirraSDL - Device configuration dialogs (part 2)
//	Second half of ui_devconfig_devices.cpp split in Phase 2h. Contains
//	KMK/JZ IDE, MyIDE2, HDVirtFAT, PCLink, HostFS, Custom Device,
//	ComputerEyes, VideoStill, NetSerial, PipeSerial, Dragon Cart,
//	Printer/HLE, Percom, AMDC, BlackBox Floppy, Happy 810, 815, ATR8000,
//	1020, Multiplexer, ParFileWriter, Karin Maxi Drive.

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
// KMK/JZ IDE — device ID + SDX
// =========================================================================

bool RenderKMKJZIDEConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kIDLabels[] = { "ID 0", "ID 1", "ID 2", "ID 3", "ID 4", "ID 5", "ID 6", "ID 7" };

	if (st.justOpened) {
		st.combo[0] = (int)props.GetUint32("id", 0);
		if (st.combo[0] > 7) st.combo[0] = 0;
		st.check[0] = props.GetBool("enablesdx", true);
	}

	ImGui::Combo("Device ID", &st.combo[0], kIDLabels, 8);
	ImGui::Checkbox("Enable SDX", &st.check[0]);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		props.SetUint32("id", (uint32)st.combo[0]);
		if (st.check[0]) props.SetBool("enablesdx", true);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// KMK/JZ IDE v2 — revision + ID + SDX + write protect + NVRAM guard
// =========================================================================

bool RenderKMKJZIDE2Config(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kRevLabels[] = { "Rev. C", "Rev. D", "Rev. Ds/S (rev.D with Covox)", "Rev. E" };
	static const char *kRevValues[] = { "c", "d", "s", "e" };
	static const char *kIDLabels[] = { "ID 0", "ID 1", "ID 2", "ID 3", "ID 4", "ID 5", "ID 6", "ID 7" };

	if (st.justOpened) {
		st.combo[0] = 1; // default Rev D
		const wchar_t *rev = props.GetString("revision", L"d");
		if (rev) {
			VDStringA revU8 = WToU8(rev);
			for (int i = 0; i < 4; ++i)
				if (!strcmp(kRevValues[i], revU8.c_str())) { st.combo[0] = i; break; }
		}
		st.combo[1] = (int)props.GetUint32("id", 0);
		if (st.combo[1] > 7) st.combo[1] = 0;
		st.check[0] = props.GetBool("enablesdx", false);
		st.check[1] = props.GetBool("writeprotect", false);
		st.check[2] = props.GetBool("nvramguard", true);
	}

	ImGui::Combo("Revision", &st.combo[0], kRevLabels, 4);
	ImGui::Combo("Device ID", &st.combo[1], kIDLabels, 8);
	ImGui::Checkbox("Enable SDX", &st.check[0]);
	ImGui::Checkbox("Write protect", &st.check[1]);
	ImGui::Checkbox("NVRAM guard", &st.check[2]);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		props.SetString("revision", U8ToW(kRevValues[st.combo[0]]).c_str());
		props.SetUint32("id", (uint32)st.combo[1]);
		if (st.check[0]) props.SetBool("enablesdx", true);
		if (st.check[1]) props.SetBool("writeprotect", true);
		if (st.check[2]) props.SetBool("nvramguard", true);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// MyIDE-II — CPLD version
// =========================================================================

bool RenderMyIDE2Config(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kVersionLabels[] = { "Original", "Updated (video playback)" };

	if (st.justOpened) {
		// Windows: cpldver >= 2 means v2; combo index 0 = original, 1 = updated
		st.combo[0] = props.GetUint32("cpldver", 0) >= 2 ? 1 : 0;
	}

	ImGui::Combo("CPLD version", &st.combo[0], kVersionLabels, 2);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		// Windows writes cpldver=2 for v2, nothing for v1 (defaults to 0)
		if (st.combo[0] == 1)
			props.SetUint32("cpldver", 2);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Virtual FAT HD — directory path
// =========================================================================

bool RenderHDVirtFATConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	if (st.justOpened) {
		const wchar_t *path = props.GetString("path", L"");
		snprintf(st.pathBuf, sizeof(st.pathBuf), "%s", WToU8(path).c_str());
	}

	if (InputTextWithBrowse("Directory path", st.pathBuf, sizeof(st.pathBuf), "browseVFAT"))
		DevBrowseForFolder(st.pathBuf, sizeof(st.pathBuf));
	ImGui::SetItemTooltip("Path to host directory to map as virtual disk");

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		if (st.pathBuf[0] == 0)
			return false;
		props.Clear();
		props.SetString("path", U8ToW(st.pathBuf).c_str());
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// PCLink — path + write + timestamps
// =========================================================================

bool RenderPCLinkConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	if (st.justOpened) {
		const wchar_t *path = props.GetString("path", L"");
		snprintf(st.pathBuf, sizeof(st.pathBuf), "%s", WToU8(path).c_str());
		st.check[0] = props.GetBool("write", false);
		st.check[1] = props.GetBool("set_timestamps", false);
	}

	if (InputTextWithBrowse("Base directory", st.pathBuf, sizeof(st.pathBuf), "browsePCLink"))
		DevBrowseForFolder(st.pathBuf, sizeof(st.pathBuf));
	ImGui::Checkbox("Allow writes", &st.check[0]);
	ImGui::Checkbox("Set file timestamps", &st.check[1]);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		if (st.pathBuf[0])
			props.SetString("path", U8ToW(st.pathBuf).c_str());
		if (st.check[0]) props.SetBool("write", true);
		if (st.check[1]) props.SetBool("set_timestamps", true);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Host FS — 4 paths + LFN mode + readonly + lowercase + fakedisk
// =========================================================================

bool RenderHostFSConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kLFNLabels[] = {
		"8.3 only, truncate long names",
		"8.3 only, encode long names",
		"Use long file names"
	};

	// Use pathBuf for path1, extraPaths[0..2] for path2-4
	if (st.justOpened) {
		for (int i = 0; i < 4; ++i) {
			char key[16];
			snprintf(key, sizeof(key), "path%d", i + 1);
			const wchar_t *p = props.GetString(key, L"");
			char *dest = (i == 0) ? st.pathBuf : st.extraPaths[i - 1];
			int maxLen = (i == 0) ? (int)sizeof(st.pathBuf) : (int)sizeof(st.extraPaths[0]);
			snprintf(dest, maxLen, "%s", WToU8(p).c_str());
		}
		st.check[0] = props.GetBool("readonly", true);
		st.check[1] = props.GetBool("lowercase", true);
		st.check[2] = props.GetBool("fakedisk", false);

		bool encodeLFN = props.GetBool("encodelfn", true);
		bool enableLFN = props.GetBool("longfilenames", false);
		st.combo[0] = enableLFN ? 2 : encodeLFN ? 1 : 0;
	}

	if (InputTextWithBrowse("Path 1 (H1:)", st.pathBuf, sizeof(st.pathBuf), "browseH1"))
		DevBrowseForFolder(st.pathBuf, sizeof(st.pathBuf));
	if (InputTextWithBrowse("Path 2 (H2:)", st.extraPaths[0], sizeof(st.extraPaths[0]), "browseH2"))
		DevBrowseForFolder(st.extraPaths[0], sizeof(st.extraPaths[0]));
	if (InputTextWithBrowse("Path 3 (H3:)", st.extraPaths[1], sizeof(st.extraPaths[1]), "browseH3"))
		DevBrowseForFolder(st.extraPaths[1], sizeof(st.extraPaths[1]));
	if (InputTextWithBrowse("Path 4 (H4:)", st.extraPaths[2], sizeof(st.extraPaths[2]), "browseH4"))
		DevBrowseForFolder(st.extraPaths[2], sizeof(st.extraPaths[2]));

	ImGui::SeparatorText("Options");
	ImGui::Checkbox("Read only", &st.check[0]);
	ImGui::Checkbox("Lowercase filenames", &st.check[1]);
	ImGui::Checkbox("Install as disk", &st.check[2]);
	ImGui::Combo("Long filename mode", &st.combo[0], kLFNLabels, 3);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		if (!st.check[0]) props.SetBool("readonly", false);
		if (!st.check[1]) props.SetBool("lowercase", false);
		if (st.check[2]) props.SetBool("fakedisk", true);
		props.SetBool("encodelfn", st.combo[0] >= 1);
		props.SetBool("longfilenames", st.combo[0] >= 2);

		const char *bufs[] = { st.pathBuf, st.extraPaths[0], st.extraPaths[1], st.extraPaths[2] };
		for (int i = 0; i < 4; ++i) {
			if (bufs[i][0]) {
				char key[16];
				snprintf(key, sizeof(key), "path%d", i + 1);
				props.SetString(key, U8ToW(bufs[i]).c_str());
			}
		}
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Custom Device — script path + options
// =========================================================================

bool RenderCustomDeviceConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	if (st.justOpened) {
		const wchar_t *path = props.GetString("path", L"");
		snprintf(st.pathBuf, sizeof(st.pathBuf), "%s", WToU8(path).c_str());
		st.check[0] = props.GetBool("hotreload", false);
		st.check[1] = props.GetBool("allowunsafe", false);
	}

	if (InputTextWithBrowse("Device descriptor path", st.pathBuf, sizeof(st.pathBuf), "browseCustom")) {
		static const SDL_DialogFileFilter kCustomFilters[] = {
			{ "Custom device files", "atdevice" },
			{ "All files", "*" },
		};
		DevBrowseForFile(st.pathBuf, sizeof(st.pathBuf), kCustomFilters, 2);
	}
	ImGui::Checkbox("Hot reload on change", &st.check[0]);
	ImGui::Checkbox("Allow unsafe operations", &st.check[1]);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		if (st.pathBuf[0])
			props.SetString("path", U8ToW(st.pathBuf).c_str());
		if (st.check[0]) props.SetBool("hotreload", true);
		if (st.check[1]) props.SetBool("allowunsafe", true);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// ComputerEyes — brightness
// =========================================================================

bool RenderComputerEyesConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	if (st.justOpened) {
		st.intVal[0] = (int)props.GetUint32("brightness", 50);
	}

	ImGui::SliderInt("Brightness", &st.intVal[0], 0, 100, "%d");
	ImGui::SetItemTooltip("Displayed as offset: %d", st.intVal[0] - 50);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		props.SetUint32("brightness", (uint32)std::clamp(st.intVal[0], 0, 100));
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Video Still Image — image path
// =========================================================================

bool RenderVideoStillImageConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	if (st.justOpened) {
		const wchar_t *path = props.GetString("path", L"");
		snprintf(st.pathBuf, sizeof(st.pathBuf), "%s", WToU8(path).c_str());
	}

	if (InputTextWithBrowse("Image file path", st.pathBuf, sizeof(st.pathBuf), "browseVSI")) {
		static const SDL_DialogFileFilter kImageFilters[] = {
			{ "Image files", "png;jpg;jpeg;bmp;tga" },
			{ "All files", "*" },
		};
		DevBrowseForFile(st.pathBuf, sizeof(st.pathBuf), kImageFilters, 2);
	}

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		if (st.pathBuf[0])
			props.SetString("path", U8ToW(st.pathBuf).c_str());
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Network Serial — address + port + baud + listen mode
// =========================================================================

bool RenderNetSerialConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	if (st.justOpened) {
		const wchar_t *addr = props.GetString("connect_addr", L"");
		snprintf(st.addrBuf, sizeof(st.addrBuf), "%s", WToU8(addr).c_str());
		snprintf(st.portBuf, sizeof(st.portBuf), "%u", props.GetUint32("port", 9000));
		snprintf(st.baudBuf, sizeof(st.baudBuf), "%u", props.GetUint32("baud_rate", 31250));
		st.combo[0] = props.GetBool("listen", false) ? 1 : 0;
	}

	static const char *kModeLabels[] = { "Connect (outbound)", "Listen (inbound)" };
	ImGui::Combo("Mode", &st.combo[0], kModeLabels, 2);

	ImGui::InputText("Address", st.addrBuf, sizeof(st.addrBuf));
	ImGui::InputText("Port", st.portBuf, sizeof(st.portBuf));
	ImGui::InputText("Baud rate", st.baudBuf, sizeof(st.baudBuf));

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		unsigned port = 0, baud = 0;
		sscanf(st.portBuf, "%u", &port);
		sscanf(st.baudBuf, "%u", &baud);
		if (port < 1 || port > 65535) return false;
		if (baud < 1 || baud > 1000000) return false;
		props.SetUint32("port", port);
		props.SetUint32("baud_rate", baud);
		if (st.addrBuf[0])
			props.SetString("connect_addr", U8ToW(st.addrBuf).c_str());
		if (st.combo[0] > 0) props.SetBool("listen", true);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Pipe Serial — pipe name + baud rate
// =========================================================================

bool RenderPipeSerialConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	if (st.justOpened) {
		const wchar_t *name = props.GetString("pipe_name", L"AltirraSerial");
		snprintf(st.addrBuf, sizeof(st.addrBuf), "%s", WToU8(name).c_str());
		snprintf(st.baudBuf, sizeof(st.baudBuf), "%u", props.GetUint32("baud_rate", 9600));
	}

	ImGui::InputText("Pipe name", st.addrBuf, sizeof(st.addrBuf));
	ImGui::InputText("Baud rate", st.baudBuf, sizeof(st.baudBuf));

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		if (st.addrBuf[0])
			props.SetString("pipe_name", U8ToW(st.addrBuf).c_str());
		unsigned baud = 0;
		sscanf(st.baudBuf, "%u", &baud);
		if (baud >= 1 && baud <= 1000000)
			props.SetUint32("baud_rate", baud);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// DragonCart — access mode, network address, VXLAN tunnel, forwarding
// (matches Windows uidragoncart.cpp exactly)
// =========================================================================

// Helper: parse "A.B.C.D" into packed uint32 (a<<24 | b<<16 | c<<8 | d)
static bool ParseIPv4(const char *s, uint32 &out) {
	unsigned a0, a1, a2, a3;
	char c;
	if (4 != sscanf(s, "%u.%u.%u.%u%c", &a0, &a1, &a2, &a3, &c))
		return false;
	if ((a0 | a1 | a2 | a3) >= 256)
		return false;
	out = (a0 << 24) | (a1 << 16) | (a2 << 8) | a3;
	return true;
}

// Helper: format packed uint32 to "A.B.C.D"
static void FormatIPv4(char *buf, int bufSize, uint32 addr) {
	snprintf(buf, bufSize, "%u.%u.%u.%u",
		(addr >> 24) & 0xff, (addr >> 16) & 0xff,
		(addr >> 8) & 0xff, addr & 0xff);
}

bool RenderDragonCartConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kAccessModes[] = { "None", "Host only", "NAT" };
	static const char *kAccessValues[] = { "none", "hostonly", "nat" };

	// addrBuf = net address, svcBuf = net mask, extraPaths[0] = tunnel addr,
	// portBuf = tunnel src port, baudBuf = tunnel tgt port,
	// extraPaths[1] = fwd address, extraPaths[2] = fwd port
	// combo[0] = access mode, check[0] = VXLAN enabled

	if (st.justOpened) {
		uint32 netaddr = props.GetUint32("netaddr", 0xC0A80000); // 192.168.0.0
		uint32 netmask = props.GetUint32("netmask", 0xFFFFFF00); // 255.255.255.0
		FormatIPv4(st.addrBuf, sizeof(st.addrBuf), netaddr);
		FormatIPv4(st.svcBuf, sizeof(st.svcBuf), netmask);

		// Access mode
		const wchar_t *access = props.GetString("access");
		st.combo[0] = 2; // default NAT
		if (access) {
			VDStringA au8 = WToU8(access);
			for (int i = 0; i < 3; ++i)
				if (!strcmp(kAccessValues[i], au8.c_str())) { st.combo[0] = i; break; }
		}

		// VXLAN tunnel
		uint32 tunaddr = props.GetUint32("tunaddr", 0);
		st.check[0] = (tunaddr != 0);
		if (tunaddr)
			FormatIPv4(st.extraPaths[0], sizeof(st.extraPaths[0]), tunaddr);
		else
			st.extraPaths[0][0] = 0;

		uint32 tunsrcport = props.GetUint32("tunsrcport", 0);
		uint32 tuntgtport = props.GetUint32("tuntgtport", 0);
		if (tunsrcport)
			snprintf(st.portBuf, sizeof(st.portBuf), "%u", tunsrcport);
		else
			st.portBuf[0] = 0;
		if (tuntgtport)
			snprintf(st.baudBuf, sizeof(st.baudBuf), "%u", tuntgtport);
		else
			st.baudBuf[0] = 0;

		// Forwarding
		uint32 fwaddr = props.GetUint32("fwaddr", 0);
		uint32 fwport = props.GetUint32("fwport", 0);
		if (fwaddr)
			FormatIPv4(st.extraPaths[1], sizeof(st.extraPaths[1]), fwaddr);
		else
			st.extraPaths[1][0] = 0;
		if (fwport)
			snprintf(st.extraPaths[2], sizeof(st.extraPaths[2]), "%u", fwport);
		else
			st.extraPaths[2][0] = 0;
	}

	// Network
	ImGui::SeparatorText("Network");
	ImGui::InputText("Network address", st.addrBuf, sizeof(st.addrBuf));
	ImGui::SetItemTooltip("IPv4 address, e.g. 192.168.0.0");
	ImGui::InputText("Network mask", st.svcBuf, sizeof(st.svcBuf));
	ImGui::SetItemTooltip("e.g. 255.255.255.0");

	// Access mode
	ImGui::SeparatorText("Access mode");
	ImGui::Combo("Access", &st.combo[0], kAccessModes, 3);

	// Forwarding (only when NAT)
	bool isNAT = (st.combo[0] == 2);
	ImGui::BeginDisabled(!isNAT);
	ImGui::SeparatorText("Port forwarding");
	ImGui::InputText("Forward address", st.extraPaths[1], sizeof(st.extraPaths[1]));
	ImGui::SetItemTooltip("IPv4 address on the emulation network to forward to");
	ImGui::InputText("Forward port", st.extraPaths[2], sizeof(st.extraPaths[2]));
	ImGui::EndDisabled();

	// VXLAN tunnel
	ImGui::SeparatorText("VXLAN tunnel");
	ImGui::Checkbox("Enable VXLAN tunnel", &st.check[0]);
	ImGui::BeginDisabled(!st.check[0]);
	ImGui::InputText("Tunnel address", st.extraPaths[0], sizeof(st.extraPaths[0]));
	ImGui::SetItemTooltip("IPv4 address on host network for VXLAN endpoint");
	ImGui::InputText("Source port", st.portBuf, sizeof(st.portBuf));
	ImGui::SetItemTooltip("UDP source port (blank for dynamic)");
	ImGui::InputText("Target port", st.baudBuf, sizeof(st.baudBuf));
	ImGui::SetItemTooltip("UDP target port (blank for default 4789)");
	ImGui::EndDisabled();

	// Validation
	bool valid = true;
	uint32 netaddr = 0, netmask = 0;

	if (!ParseIPv4(st.addrBuf, netaddr)) {
		ImGui::TextColored(ImVec4(1,0.4f,0.4f,1), "Invalid network address. Must be A.B.C.D format.");
		valid = false;
	}
	if (!ParseIPv4(st.svcBuf, netmask)) {
		ImGui::TextColored(ImVec4(1,0.4f,0.4f,1), "Invalid network mask. Must be A.B.C.D format.");
		valid = false;
	} else {
		// Contiguous 1-bits check
		uint32 test = 0 - netmask;
		if (test & (test - 1)) {
			ImGui::TextColored(ImVec4(1,0.4f,0.4f,1), "Network mask must have contiguous 1 bits.");
			valid = false;
		} else if (valid && (netaddr & ~netmask)) {
			ImGui::TextColored(ImVec4(1,0.4f,0.4f,1), "Network address has bits set outside mask.");
			valid = false;
		}
	}

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		if (!valid)
			return false;

		props.Clear();
		props.SetUint32("netaddr", netaddr);
		props.SetUint32("netmask", netmask);
		props.SetString("access", U8ToW(kAccessValues[st.combo[0]]).c_str());

		// Forwarding (only when NAT)
		if (st.combo[0] == 2 && st.extraPaths[1][0]) {
			uint32 fwaddr = 0;
			unsigned fwport = 0;
			if (ParseIPv4(st.extraPaths[1], fwaddr)) {
				sscanf(st.extraPaths[2], "%u", &fwport);
				if (fwaddr && fwport >= 1 && fwport <= 65535) {
					props.SetUint32("fwaddr", fwaddr);
					props.SetUint32("fwport", fwport);
				}
			}
		}

		// VXLAN tunnel
		if (st.check[0] && st.extraPaths[0][0]) {
			uint32 tunaddr = 0;
			if (ParseIPv4(st.extraPaths[0], tunaddr) && tunaddr) {
				props.SetUint32("tunaddr", tunaddr);

				unsigned srcport = 0, tgtport = 0;
				if (st.portBuf[0])
					sscanf(st.portBuf, "%u", &srcport);
				props.SetUint32("tunsrcport", srcport);

				if (st.baudBuf[0])
					sscanf(st.baudBuf, "%u", &tgtport);
				else
					tgtport = 4789; // default
				props.SetUint32("tuntgtport", tgtport);
			}
		}

		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Printer (820/1025/1029) — graphics + timing + sound
// =========================================================================

bool RenderPrinterConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	if (st.justOpened) {
		st.check[0] = props.GetBool("graphics", false);
		st.check[1] = props.GetBool("accurate_timing", false);
		st.check[2] = props.GetBool("sound", false);
	}

	ImGui::Checkbox("Enable graphical output", &st.check[0]);

	ImGui::BeginDisabled(!st.check[0]);
	ImGui::Checkbox("Enable accurate timing", &st.check[1]);
	ImGui::EndDisabled();

	ImGui::BeginDisabled(!st.check[0] || !st.check[1]);
	ImGui::Checkbox("Enable sound", &st.check[2]);
	ImGui::EndDisabled();

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		if (st.check[0]) props.SetBool("graphics", true);
		if (st.check[1]) props.SetBool("accurate_timing", true);
		if (st.check[2]) props.SetBool("sound", true);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Printer HLE (P:) — translation mode
// =========================================================================

bool RenderPrinterHLEConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kModeLabels[] = {
		"Default: Translate EOL -> CR",
		"Raw: No translation",
		"ATASCII to UTF-8"
	};
	// Enum string values matching ATPrinterPortTranslationMode
	static const char *kModeValues[] = { "default", "raw", "atasciitoutf8" };

	if (st.justOpened) {
		// Read as enum string
		const wchar_t *modeStr = props.GetString("translation_mode");
		st.combo[0] = 0; // default
		if (modeStr) {
			VDStringA modeU8 = WToU8(modeStr);
			for (int i = 0; i < 3; ++i)
				if (!strcmp(kModeValues[i], modeU8.c_str())) { st.combo[0] = i; break; }
		}
	}

	ImGui::Combo("Port translation", &st.combo[0], kModeLabels, 3);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		// Store as enum string (matching AT_DEFINE_ENUM_TABLE)
		props.SetString("translation_mode", U8ToW(kModeValues[st.combo[0]]).c_str());
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Percom disk controller — FDC type + drive types + drive ID
// =========================================================================

bool RenderPercomConfig(ATPropertySet& props, ATDeviceConfigState& st, bool atMode, bool atSPDMode) {
	static const char *kDriveTypeLabels[] = {
		"None", "5.25\" (40 track)", "5.25\" (80 track)"
	};

	if (st.justOpened) {
		for (int i = 0; i < 4; ++i) {
			char key[16];
			snprintf(key, sizeof(key), "drivetype%d", i);
			st.combo[4 + i] = (int)props.GetUint32(key, i == 0 ? 1 : 0);
			if (st.combo[4 + i] > 2) st.combo[4 + i] = 0;
		}

		if (atMode) {
			bool use1795 = props.GetBool("use1795", false);
			if (atSPDMode) {
				st.combo[0] = use1795 ? 1 : 0;
			} else {
				bool ddcapable = props.GetBool("ddcapable", true);
				if (use1795) st.combo[0] = 1;
				else if (!ddcapable) st.combo[0] = 2;
				else st.combo[0] = 0;
			}
		} else {
			st.combo[0] = (int)props.GetUint32("id", 0);
			if (st.combo[0] > 7) st.combo[0] = 0;
		}
	}

	if (atMode) {
		if (atSPDMode) {
			static const char *kFDCLabels[] = {
				"1791 (side compare optional)", "1795 (side compare always on)"
			};
			ImGui::Combo("FDC type", &st.combo[0], kFDCLabels, 2);
		} else {
			static const char *kFDCLabels[] = {
				"1771+1791 (double density, side compare optional)",
				"1771+1795 (double density, side compare always on)",
				"1771 (single density only)"
			};
			ImGui::Combo("FDC type", &st.combo[0], kFDCLabels, 3);
		}
	} else {
		static const char *kIDLabels[] = {
			"Drive 1", "Drive 2", "Drive 3", "Drive 4",
				"Drive 5", "Drive 6", "Drive 7", "Drive 8"
		};
		ImGui::Combo("Drive select", &st.combo[0], kIDLabels, 8);
	}

	ImGui::SeparatorText("Drive types");
	for (int i = 0; i < 4; ++i) {
		char label[32];
		snprintf(label, sizeof(label), "Drive %d", i + 1);
		ImGui::Combo(label, &st.combo[4 + i], kDriveTypeLabels, 3);
	}

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		if (atMode) {
			if (atSPDMode) {
				props.SetBool("use1795", st.combo[0] != 0);
			} else {
				props.SetBool("use1795", st.combo[0] == 1);
				props.SetBool("ddcapable", st.combo[0] != 2);
			}
		} else {
			props.SetUint32("id", (uint32)st.combo[0]);
		}
		for (int i = 0; i < 4; ++i) {
			char key[16];
			snprintf(key, sizeof(key), "drivetype%d", i);
			props.SetUint32(key, (uint32)st.combo[4 + i]);
		}
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// AMDC — drive select + switches + drive types
// =========================================================================

bool RenderAMDCConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kDriveSelectLabels[] = {
		"Drive 1 (D1:)", "Drive 2 (D2:)", "Drive 3 (D3:)", "Drive 4 (D4:)",
		"Drive 5 (D5:)", "Drive 6 (D6:)", "Drive 7 (D7:)", "Drive 8 (D8:)"
	};
	static const char *kDriveTypeLabels[] = {
		"None", "3\"/5.25\" (40 track)", "3\"/5.25\" (80 track)"
	};
	static const char *kSwitchLabels[] = {
		"SW1", "SW2", "SW3", "SW4", "SW7", "SW8", "Jumper"
	};
	static const uint32 kSwitchBits[] = { 0x01, 0x02, 0x04, 0x08, 0x40, 0x80, 0x100 };

	if (st.justOpened) {
		uint32 sw = props.GetUint32("switches", 0x40);
		st.combo[0] = (int)((sw >> 4) & 3);
		for (int i = 0; i < 7; ++i)
			st.check[i] = (sw & kSwitchBits[i]) != 0;
		st.check[7] = props.GetBool("drive2", false);

		for (int i = 0; i < 2; ++i) {
			char key[16];
			snprintf(key, sizeof(key), "extdrive%d", i);
			st.combo[1 + i] = (int)props.GetUint32(key, 0);
			if (st.combo[1 + i] > 2) st.combo[1 + i] = 0;
		}
	}

	ImGui::Combo("Drive select", &st.combo[0], kDriveSelectLabels, 8);

	ImGui::SeparatorText("DIP Switches");
	for (int i = 0; i < 7; ++i)
		ImGui::Checkbox(kSwitchLabels[i], &st.check[i]);

	ImGui::SeparatorText("External drives");
	ImGui::Checkbox("Drive 2 present", &st.check[7]);
	for (int i = 0; i < 2; ++i) {
		char label[32];
		snprintf(label, sizeof(label), "External drive %d", i + 1);
		ImGui::Combo(label, &st.combo[1 + i], kDriveTypeLabels, 3);
	}

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		uint32 sw = ((uint32)st.combo[0] & 3) << 4;
		for (int i = 0; i < 7; ++i)
			if (st.check[i]) sw |= kSwitchBits[i];
		props.SetUint32("switches", sw);
		props.SetBool("drive2", st.check[7]);
		for (int i = 0; i < 2; ++i) {
			char key[16];
			snprintf(key, sizeof(key), "extdrive%d", i);
			props.SetUint32(key, (uint32)st.combo[1 + i]);
		}
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Black Box Floppy — 4 slots with drive ID + type + mapping
// =========================================================================

bool RenderBlackBoxFloppyConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kDriveSlotLabels[] = {
		"Not connected", "D1:", "D2:", "D3:", "D4:", "D5:", "D6:", "D7:",
		"D8:", "D9:", "D10:", "D11:", "D12:", "D13:", "D14:"
	};
	static const char *kDriveTypeLabels[] = {
		"180K 5.25\" 40 track, single-sided",
		"360K 5.25\" 40 track, double-sided",
		"1.2M 5.25\" 80 track, double-sided HD",
		"360K 3.5\" 80 track, single-sided",
		"720K 3.5\" 80 track, double-sided",
		"1.4M 3.5\" 80 track, double-sided HD",
		"1M 8\" 77 track, double-sided HD"
	};
	// Enum string values matching ATBlackBoxFloppyType
	static const char *kDriveTypeValues[] = {
		"fiveinch180K", "fiveinch360K", "fiveinch12M",
		"threeinch360K", "threeinch720K", "threeinch144M", "eightinch1M"
	};
	static const char *kMappingLabels[] = {
		"Map double-sided as XF551",
		"Map double-sided as ATR8000",
		"Map double-sided as PERCOM"
	};
	// Enum string values matching ATBlackBoxFloppyMappingType
	static const char *kMappingValues[] = { "xf551", "atr8000", "percom" };

	if (st.justOpened) {
		for (int i = 0; i < 4; ++i) {
			char key[32];
			// Drive slot is stored as int (not enum)
			snprintf(key, sizeof(key), "driveslot%d", i);
			st.combo[i * 3] = (int)props.GetUint32(key, 0);
			if (st.combo[i * 3] > 14) st.combo[i * 3] = 0;

			// Drive type is stored as enum string
			snprintf(key, sizeof(key), "drivetype%d", i);
			const wchar_t *dtStr = props.GetString(key);
			st.combo[i * 3 + 1] = 0; // default FiveInch180K
			if (dtStr) {
				VDStringA dtU8 = WToU8(dtStr);
				for (int j = 0; j < 7; ++j)
					if (!strcmp(kDriveTypeValues[j], dtU8.c_str())) { st.combo[i * 3 + 1] = j; break; }
			}

			// Mapping type is stored as enum string
			snprintf(key, sizeof(key), "drivemapping%d", i);
			const wchar_t *dmStr = props.GetString(key);
			st.combo[i * 3 + 2] = 0; // default XF551
			if (dmStr) {
				VDStringA dmU8 = WToU8(dmStr);
				for (int j = 0; j < 3; ++j)
					if (!strcmp(kMappingValues[j], dmU8.c_str())) { st.combo[i * 3 + 2] = j; break; }
			}
		}
	}

	for (int i = 0; i < 4; ++i) {
		ImGui::PushID(i);
		char header[32];
		snprintf(header, sizeof(header), "PBI Floppy %d", i);
		ImGui::SeparatorText(header);

		ImGui::Combo("Drive slot", &st.combo[i * 3], kDriveSlotLabels, 15);
		ImGui::Combo("Drive type", &st.combo[i * 3 + 1], kDriveTypeLabels, 7);
		ImGui::Combo("Mapping", &st.combo[i * 3 + 2], kMappingLabels, 3);
		ImGui::PopID();
	}

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		for (int i = 0; i < 4; ++i) {
			char key[32];
			snprintf(key, sizeof(key), "driveslot%d", i);
			props.SetUint32(key, (uint32)st.combo[i * 3]);
			// Store enum types as string values (matching AT_DEFINE_ENUM_TABLE)
			snprintf(key, sizeof(key), "drivetype%d", i);
			props.SetString(key, U8ToW(kDriveTypeValues[st.combo[i * 3 + 1]]).c_str());
			snprintf(key, sizeof(key), "drivemapping%d", i);
			props.SetString(key, U8ToW(kMappingValues[st.combo[i * 3 + 2]]).c_str());
		}
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Happy 810 — drive ID + auto-speed + speed rate
// =========================================================================

bool RenderHappy810Config(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kDriveLabels[] = {
		"Drive 1 (D1:)", "Drive 2 (D2:)", "Drive 3 (D3:)", "Drive 4 (D4:)"
	};

	if (st.justOpened) {
		st.combo[0] = (int)props.GetUint32("id", 0);
		if (st.combo[0] > 3) st.combo[0] = 0;
		st.check[0] = props.GetBool("autospeed", false);
		// Store speed as int * 10 for slider (200-400 range = 2000-4000)
		float speed = props.GetFloat("autospeedrate", 266.0f);
		st.intVal[0] = (int)(speed + 0.5f);
		if (st.intVal[0] < 200) st.intVal[0] = 200;
		if (st.intVal[0] > 400) st.intVal[0] = 400;
	}

	ImGui::Combo("Drive select", &st.combo[0], kDriveLabels, 4);
	ImGui::Checkbox("Auto-speed", &st.check[0]);
	ImGui::BeginDisabled(!st.check[0]);
	ImGui::SliderInt("RPM", &st.intVal[0], 200, 400);
	ImGui::EndDisabled();

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		props.SetUint32("id", (uint32)st.combo[0]);
		if (st.check[0]) props.SetBool("autospeed", true);
		props.SetFloat("autospeedrate", (float)st.intVal[0]);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// 815 Dual Drive — ID + invert mode
// =========================================================================

bool Render815Config(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kDriveLabels[] = {
		"Drives 1-2 (D1-D2:)", "Drives 3-4 (D3-D4:)",
		"Drives 5-6 (D5-D6:)", "Drives 7-8 (D7-D8:)"
	};

	if (st.justOpened) {
		// Windows stores id as selection << 1 (bit-shifted)
		st.combo[0] = (int)(props.GetUint32("id", 0) >> 1);
		if (st.combo[0] > 3) st.combo[0] = 0;
		st.check[0] = props.GetBool("accurate_invert", false);
	}

	ImGui::Combo("Drive pair", &st.combo[0], kDriveLabels, 4);
	ImGui::Checkbox("Accurate data inversion", &st.check[0]);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		// Windows stores id as selection << 1 (bit-shifted)
		props.SetUint32("id", (uint32)st.combo[0] << 1);
		if (st.check[0]) props.SetBool("accurate_invert", true);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// ATR8000 — drive types (None/5.25"/8") + serial port signals
// (matches Windows uiconfdevatr8000.cpp exactly)
// =========================================================================

bool RenderATR8000Config(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kDriveTypeLabels[] = { "None", "5.25\"", "8\"" };

	static const char *kSignal1Labels[] = {
		"Request To Send (RTS)", "Data Terminal Ready (DTR)"
	};
	static const char *kSignal1Values[] = { "rts", "dtr" };

	static const char *kSignal2Labels[] = {
		"Clear To Send (CTS)", "Data Set Ready (DSR)",
		"Carrier Detect (CD)", "Secondary Request To Send (SRTS)"
	};
	static const char *kSignal2Values[] = { "cts", "dsr", "cd", "srts" };

	if (st.justOpened) {
		for (int i = 0; i < 4; ++i) {
			char key[16];
			snprintf(key, sizeof(key), "drivetype%d", i);
			st.combo[i] = (int)props.GetUint32(key, i == 0 ? 1 : 0);
			if (st.combo[i] > 2) st.combo[i] = 0;
		}

		// Serial signal 1
		st.combo[4] = 0;
		const wchar_t *s1 = props.GetString("signal1", L"");
		if (s1) {
			VDStringA s1u8 = WToU8(s1);
			for (int i = 0; i < 2; ++i)
				if (!strcmp(kSignal1Values[i], s1u8.c_str())) { st.combo[4] = i; break; }
		}

		// Serial signal 2
		st.combo[5] = 0;
		const wchar_t *s2 = props.GetString("signal2", L"");
		if (s2) {
			VDStringA s2u8 = WToU8(s2);
			for (int i = 0; i < 4; ++i)
				if (!strcmp(kSignal2Values[i], s2u8.c_str())) { st.combo[5] = i; break; }
		}
	}

	ImGui::SeparatorText("Drive types");
	for (int i = 0; i < 4; ++i) {
		char label[32];
		snprintf(label, sizeof(label), "Drive %d", i + 1);
		ImGui::Combo(label, &st.combo[i], kDriveTypeLabels, 3);
	}

	ImGui::SeparatorText("Serial port signals");
	ImGui::Combo("Signal 1", &st.combo[4], kSignal1Labels, 2);
	ImGui::Combo("Signal 2", &st.combo[5], kSignal2Labels, 4);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		for (int i = 0; i < 4; ++i) {
			char key[16];
			snprintf(key, sizeof(key), "drivetype%d", i);
			props.SetUint32(key, (uint32)st.combo[i]);
		}
		props.SetString("signal1", U8ToW(kSignal1Values[st.combo[4]]).c_str());
		props.SetString("signal2", U8ToW(kSignal2Values[st.combo[5]]).c_str());
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// 1020 Plotter — pen colors
// =========================================================================

bool Render1020Config(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kColorLabels[] = { "Black", "Blue", "Green", "Red" };
	// RGB values matching Windows 1020 palette: { 0x000000, 0x181FF0, 0x0B9C2F, 0xC91B12 }
	static const ImVec4 kPenColors[] = {
		ImVec4(0.0f, 0.0f, 0.0f, 1.0f),        // Black
		ImVec4(0.094f, 0.122f, 0.941f, 1.0f),   // Blue (0x18/FF, 0x1F/FF, 0xF0/FF)
		ImVec4(0.043f, 0.612f, 0.184f, 1.0f),   // Green (0x0B/FF, 0x9C/FF, 0x2F/FF)
		ImVec4(0.788f, 0.106f, 0.071f, 1.0f),   // Red (0xC9/FF, 0x1B/FF, 0x12/FF)
	};

	if (st.justOpened) {
		for (int i = 0; i < 4; ++i) {
			char key[16];
			snprintf(key, sizeof(key), "pencolor%d", i);
			st.combo[i] = (int)props.GetUint32(key, (uint32)i);
			if (st.combo[i] > 3) st.combo[i] = i;
		}
	}

	ImGui::SeparatorText("Pen Colors");
	for (int i = 0; i < 4; ++i) {
		ImGui::PushID(i);
		char label[32];
		snprintf(label, sizeof(label), "Pen %d", i + 1);
		// Show color swatch next to combo
		int sel = st.combo[i];
		if (sel >= 0 && sel < 4) {
			ImGui::ColorButton("##col", kPenColors[sel],
				ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
				ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
			ImGui::SameLine();
		}
		ImGui::Combo(label, &st.combo[i], kColorLabels, 4);
		ImGui::PopID();
	}

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		for (int i = 0; i < 4; ++i) {
			char key[16];
			snprintf(key, sizeof(key), "pencolor%d", i);
			props.SetUint32(key, (uint32)st.combo[i]);
		}
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Multiplexer — device ID + host address + port + external
// =========================================================================

bool RenderMultiplexerConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kDeviceLabels[] = {
		"Host", "Client (ID 1)", "Client (ID 2)", "Client (ID 3)",
		"Client (ID 4)", "Client (ID 5)", "Client (ID 6)",
		"Client (ID 7)", "Client (ID 8)"
	};
	// Maps: combo index 0 = -1 (host), 1 = 0 (client 1), etc.

	if (st.justOpened) {
		sint32 id = props.GetInt32("device_id", -1);
		st.combo[0] = id + 1;
		if (st.combo[0] < 0 || st.combo[0] > 8) st.combo[0] = 0;

		const wchar_t *addr = props.GetString("host_address", L"");
		snprintf(st.addrBuf, sizeof(st.addrBuf), "%s", WToU8(addr).c_str());
		snprintf(st.portBuf, sizeof(st.portBuf), "%u", props.GetUint32("port", 6522));
		st.check[0] = props.GetBool("allow_external", false);
	}

	ImGui::Combo("Device ID", &st.combo[0], kDeviceLabels, 9);

	bool isHost = (st.combo[0] == 0);
	bool isClient = !isHost;

	ImGui::BeginDisabled(!isClient);
	ImGui::InputText("Host address", st.addrBuf, sizeof(st.addrBuf));
	ImGui::EndDisabled();

	ImGui::InputText("TCP port", st.portBuf, sizeof(st.portBuf));

	ImGui::BeginDisabled(!isHost);
	ImGui::Checkbox("Allow external connections", &st.check[0]);
	ImGui::EndDisabled();

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		props.SetInt32("device_id", st.combo[0] - 1);
		if (st.addrBuf[0])
			props.SetString("host_address", U8ToW(st.addrBuf).c_str());
		unsigned port = 0;
		sscanf(st.portBuf, "%u", &port);
		if (port >= 1 && port <= 65535)
			props.SetUint32("port", port);
		if (st.check[0]) props.SetBool("allow_external", true);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Parallel File Writer — output path + text mode
// =========================================================================

bool RenderParFileWriterConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	if (st.justOpened) {
		const wchar_t *path = props.GetString("path", L"");
		snprintf(st.pathBuf, sizeof(st.pathBuf), "%s", WToU8(path).c_str());
		st.check[0] = props.GetBool("text_mode", false);
	}

	if (InputTextWithBrowse("Output file path", st.pathBuf, sizeof(st.pathBuf), "browsePFW")) {
		static const SDL_DialogFileFilter kOutputFilters[] = {
			{ "All files", "*" },
		};
		DevBrowseForSaveFile(st.pathBuf, sizeof(st.pathBuf), kOutputFilters, 1);
	}
	ImGui::Checkbox("Text mode", &st.check[0]);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		if (st.pathBuf[0])
			props.SetString("path", U8ToW(st.pathBuf).c_str());
		if (st.check[0]) props.SetBool("text_mode", true);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Karin Maxi Drive — hardware version + drives + switches
// =========================================================================

bool RenderKarinMaxiDriveConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kHWLabels[] = { "Original", "PBI selection fix" };
	static const char *kDriveTypeLabels[] = {
		"None", "5.25\" Drive (40 tracks)", "5.25\" Drive (80 tracks)", "3.5\" Drive"
	};
	static const char *kSW1Labels[] = { "D1: and D2:", "D2: and D3:" };
	static const char *kSW2Labels[] = { "Automatic", "Manual (SW3-SW6)" };
	static const char *kStepLabels[] = { "3ms", "6ms" };

	if (st.justOpened) {
		st.combo[0] = (int)props.GetUint32("hwversion", 0);
		if (st.combo[0] > 1) st.combo[0] = 0;
		st.combo[1] = (int)props.GetUint32("drivetype1", 1);
		if (st.combo[1] > 3) st.combo[1] = 1;
		st.combo[2] = (int)props.GetUint32("drivetype2", 0);
		if (st.combo[2] > 3) st.combo[2] = 0;
		st.combo[3] = (int)props.GetUint32("sw1", 0);
		st.combo[4] = (int)props.GetUint32("sw2", 0);
		st.combo[5] = (int)props.GetUint32("sw3", 0);
		st.combo[6] = (int)props.GetUint32("sw4", 0);
	}

	ImGui::Combo("Hardware version", &st.combo[0], kHWLabels, 2);

	ImGui::SeparatorText("Drives");
	ImGui::Combo("Drive 1", &st.combo[1], kDriveTypeLabels, 4);
	ImGui::Combo("Drive 2", &st.combo[2], kDriveTypeLabels, 4);

	ImGui::SeparatorText("Switches");
	ImGui::Combo("SW1 Drive IDs", &st.combo[3], kSW1Labels, 2);
	ImGui::Combo("SW2 Config", &st.combo[4], kSW2Labels, 2);
	ImGui::Combo("SW3 Drive 2 step", &st.combo[5], kStepLabels, 2);
	ImGui::Combo("SW4 Drive 1 step", &st.combo[6], kStepLabels, 2);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		props.SetUint32("hwversion", (uint32)st.combo[0]);
		props.SetUint32("drivetype1", (uint32)st.combo[1]);
		props.SetUint32("drivetype2", (uint32)st.combo[2]);
		props.SetUint32("sw1", (uint32)st.combo[3]);
		props.SetUint32("sw2", (uint32)st.combo[4]);
		props.SetUint32("sw3", (uint32)st.combo[5]);
		props.SetUint32("sw4", (uint32)st.combo[6]);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}
