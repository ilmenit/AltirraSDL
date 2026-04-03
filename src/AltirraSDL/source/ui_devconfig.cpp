//	AltirraSDL - Device Configuration Dialogs
//	Implements per-device settings dialogs matching Windows Altirra's
//	uiconfdev*.cpp dialogs using Dear ImGui.
//
//	Each device type identified by its configTag gets a specific dialog
//	with the same controls as the Windows version. Devices without a
//	specific dialog get a generic property editor via EnumProperties().

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
#include "ui_main.h"
#include "ui_devconfig.h"

// =========================================================================
// Helpers
// =========================================================================

VDStringA WToU8(const wchar_t *s) {
	return s ? VDTextWToU8(VDStringW(s)) : VDStringA();
}

VDStringW U8ToW(const char *s) {
	return VDTextU8ToW(VDStringA(s));
}
ATDeviceConfigState g_devCfg;
static void CleanupGenericEntries();

// =========================================================================
// Public API
// =========================================================================

void ATUIOpenDeviceConfig(IATDevice *dev, ATDeviceManager *devMgr) {
	if (!dev || !devMgr)
		return;

	ATDeviceInfo info;
	dev->GetDeviceInfo(info);

	if (!info.mpDef->mpConfigTag)
		return;

	g_devCfg.Reset();
	g_devCfg.open = true;
	g_devCfg.justOpened = true;
	g_devCfg.pDev = dev;
	g_devCfg.configTag = info.mpDef->mpConfigTag;
	g_devCfg.deviceName = WToU8(info.mpDef->mpName);
	dev->GetSettings(g_devCfg.props);
}

bool ATUIIsDeviceConfigOpen() {
	return g_devCfg.open;
}

void ATUICloseDeviceConfigFor(IATDevice *dev) {
	if (g_devCfg.open && g_devCfg.pDev == dev) {
		CleanupGenericEntries();
		g_devCfg.Reset();
	}
}

// Dispatch to the appropriate per-device dialog renderer
static bool DispatchDeviceDialog(const char *tag, ATPropertySet& props, ATDeviceConfigState& st) {
	// Covox
	if (!strcmp(tag, "covox")) return RenderCovoxConfig(props, st);

	// 850 interface
	if (!strcmp(tag, "850")) return Render850Config(props, st);
	if (!strcmp(tag, "850full")) return Render850FullConfig(props, st);

	// Modems
	if (!strcmp(tag, "modem")) return RenderModemConfig(props, st, false, false);
	// 1030 has SIO level but no connect_rate/check_rate (unlike generic modem)
	if (!strcmp(tag, "1030")) return RenderModemConfig(props, st, false, false, false);
	if (!strcmp(tag, "1030full")) return RenderModemConfig(props, st, true, false);
	if (!strcmp(tag, "835")) return RenderModemConfig(props, st, false, true);
	if (!strcmp(tag, "835full")) return RenderModemConfig(props, st, true, true);
	// 1400XL has no connect_rate/check_rate/emulevel
	if (!strcmp(tag, "1400xl")) return RenderModemConfig(props, st, false, true);
	if (!strcmp(tag, "sx212")) return RenderSX212Config(props, st);
	if (!strcmp(tag, "pocketmodem")) return RenderPocketModemConfig(props, st);

	// Hard disk
	if (!strcmp(tag, "harddisk")) return RenderHardDiskConfig(props, st);

	// Black Box
	if (!strcmp(tag, "blackbox")) return RenderBlackBoxConfig(props, st);
	if (!strcmp(tag, "blackboxfloppy")) return RenderBlackBoxFloppyConfig(props, st);

	// Video/display
	if (!strcmp(tag, "vbxe")) return RenderVBXEConfig(props, st);
	if (!strcmp(tag, "xep80")) return RenderXEP80Config(props, st);
	if (!strcmp(tag, "computereyes")) return RenderComputerEyesConfig(props, st);
	if (!strcmp(tag, "videostillimage")) return RenderVideoStillImageConfig(props, st);
	if (!strcmp(tag, "videogenerator")) return RenderGenericConfig(props, st);

	// Sound
	if (!strcmp(tag, "soundboard")) return RenderSoundBoardConfig(props, st);

	// Disk drives
	if (!strcmp(tag, "diskdriveatr8000")) return RenderATR8000Config(props, st);
	if (!strcmp(tag, "diskdrivepercom")) return RenderPercomConfig(props, st, false, false);
	if (!strcmp(tag, "diskdrivepercomat")) return RenderPercomConfig(props, st, true, false);
	if (!strcmp(tag, "diskdrivepercomatspd")) return RenderPercomConfig(props, st, true, true);
	if (!strcmp(tag, "diskdriveamdc")) return RenderAMDCConfig(props, st);
	if (!strcmp(tag, "diskdrivehappy810")) return RenderHappy810Config(props, st);
	if (!strcmp(tag, "diskdrive815")) return Render815Config(props, st);

	// Full emulation disk drives (all share same dialog)
	if (!strncmp(tag, "diskdrive", 9)) return RenderDiskDriveFullConfig(props, st);

	// Cartridges / expansion
	if (!strcmp(tag, "side3")) return RenderSIDE3Config(props, st);
	if (!strcmp(tag, "veronica")) return RenderVeronicaConfig(props, st);
	if (!strcmp(tag, "corvus")) return RenderCorvusConfig(props, st);
	if (!strcmp(tag, "dongle")) return RenderDongleConfig(props, st);
	if (!strcmp(tag, "karinmaxidrive")) return RenderKarinMaxiDriveConfig(props, st);

	// IDE
	if (!strcmp(tag, "kmkjzide")) return RenderKMKJZIDEConfig(props, st);
	if (!strcmp(tag, "kmkjzide2")) return RenderKMKJZIDE2Config(props, st);
	if (!strcmp(tag, "myide2")) return RenderMyIDE2Config(props, st);

	// Host filesystem
	if (!strcmp(tag, "hdvirtfat16") || !strcmp(tag, "hdvirtfat32") || !strcmp(tag, "hdvirtsdfs"))
		return RenderHDVirtFATConfig(props, st);
	if (!strcmp(tag, "pclink")) return RenderPCLinkConfig(props, st);
	if (!strcmp(tag, "hostfs")) return RenderHostFSConfig(props, st);

	// Custom device
	if (!strcmp(tag, "custom")) return RenderCustomDeviceConfig(props, st);

	// Serial/network
	if (!strcmp(tag, "netserial")) return RenderNetSerialConfig(props, st);
	if (!strcmp(tag, "pipeserial")) return RenderPipeSerialConfig(props, st);
	if (!strcmp(tag, "dragoncart")) return RenderDragonCartConfig(props, st);

	// Printers
	if (!strcmp(tag, "820") || !strcmp(tag, "1025") || !strcmp(tag, "1029"))
		return RenderPrinterConfig(props, st);
	if (!strcmp(tag, "printer")) return RenderPrinterHLEConfig(props, st);
	if (!strcmp(tag, "1020")) return Render1020Config(props, st);

	// Multiplexer
	if (!strcmp(tag, "multiplexer")) return RenderMultiplexerConfig(props, st);

	// Parallel port
	if (!strcmp(tag, "parfilewriter")) return RenderParFileWriterConfig(props, st);

	// Fallback: generic property editor
	return RenderGenericConfig(props, st);
}

void ATUIRenderDeviceConfig(ATDeviceManager *devMgr) {
	if (!g_devCfg.open)
		return;

	VDStringA title;
	title.sprintf("%s Settings###DeviceConfig", g_devCfg.deviceName.c_str());

	ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (g_devCfg.justOpened) {
		ImGui::SetNextWindowFocus();
	}

	bool windowOpen = true;
	if (ImGui::Begin(title.c_str(), &windowOpen, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		if (ATUICheckEscClose()) {
			windowOpen = false;
		} else {
			bool applied = DispatchDeviceDialog(g_devCfg.configTag.c_str(), g_devCfg.props, g_devCfg);

			if (applied && g_devCfg.pDev && devMgr) {
				try {
					devMgr->ReconfigureDevice(*g_devCfg.pDev, g_devCfg.props);
				} catch (...) {
					fprintf(stderr, "[AltirraSDL] Failed to reconfigure device\n");
				}
				g_devCfg.Reset();
			}
		}
	}
	// Clear justOpened AFTER dispatch so per-device renderers can see it
	g_devCfg.justOpened = false;
	ImGui::End();

	if (!windowOpen) {
		CleanupGenericEntries(); // clean up generic editor if it was active
		g_devCfg.Reset();
	}

}

// =========================================================================
// Generic Property Editor — fallback for unknown devices
// Uses EnumProperties() to discover and edit all properties
// =========================================================================

// Generic property editor state — separate from ATDeviceConfigState to
// hold dynamically-sized per-property string buffers
struct GenericPropEntry {
	VDStringA name;
	ATPropertyType type;
	bool boolVal;
	sint32 i32Val;
	uint32 u32Val;
	float fVal;
	double dVal;
	char strBuf[512]; // per-entry string buffer (avoids shared-static-buffer bug)
};
static vdvector<GenericPropEntry> g_genericEntries;
static bool g_genericNeedsInit = true;

static void CleanupGenericEntries() {
	g_genericEntries.clear();
	g_genericNeedsInit = true;
}

bool RenderGenericConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	if (st.justOpened || g_genericNeedsInit) {
		g_genericEntries.clear();
		props.EnumProperties([](const char *name, const ATPropertyValue& val) {
			GenericPropEntry e;
			e.name = name;
			e.type = val.mType;
			e.boolVal = false;
			e.i32Val = 0;
			e.u32Val = 0;
			e.fVal = 0;
			e.dVal = 0;
			memset(e.strBuf, 0, sizeof(e.strBuf));
			switch (val.mType) {
				case kATPropertyType_Bool: e.boolVal = val.mValBool; break;
				case kATPropertyType_Int32: e.i32Val = val.mValI32; break;
				case kATPropertyType_Uint32: e.u32Val = val.mValU32; break;
				case kATPropertyType_Float: e.fVal = val.mValF; break;
				case kATPropertyType_Double: e.dVal = val.mValD; break;
				case kATPropertyType_String16:
					if (val.mValStr16) {
						VDStringA u8 = VDTextWToU8(VDStringW(val.mValStr16));
						strncpy(e.strBuf, u8.c_str(), sizeof(e.strBuf) - 1);
					}
					break;
				default: break;
			}
			g_genericEntries.push_back(std::move(e));
		});
		g_genericNeedsInit = false;
	}

	if (g_genericEntries.empty()) {
		ImGui::TextDisabled("This device has no configurable properties.");
	} else {
		for (int i = 0; i < (int)g_genericEntries.size(); ++i) {
			auto &e = g_genericEntries[i];
			ImGui::PushID(i);
			switch (e.type) {
				case kATPropertyType_Bool:
					ImGui::Checkbox(e.name.c_str(), &e.boolVal);
					break;
				case kATPropertyType_Int32:
					ImGui::InputInt(e.name.c_str(), &e.i32Val);
					break;
				case kATPropertyType_Uint32: {
					int v = (int)e.u32Val;
					if (ImGui::InputInt(e.name.c_str(), &v))
						e.u32Val = (uint32)v;
					break;
				}
				case kATPropertyType_Float:
					ImGui::InputFloat(e.name.c_str(), &e.fVal);
					break;
				case kATPropertyType_Double: {
					float f = (float)e.dVal;
					if (ImGui::InputFloat(e.name.c_str(), &f))
						e.dVal = (double)f;
					break;
				}
				case kATPropertyType_String16:
					ImGui::InputText(e.name.c_str(), e.strBuf, sizeof(e.strBuf));
					break;
				default:
					ImGui::TextDisabled("%s: (unknown type)", e.name.c_str());
					break;
			}
			ImGui::PopID();
		}
	}

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		for (auto &e : g_genericEntries) {
			switch (e.type) {
				case kATPropertyType_Bool:
					props.SetBool(e.name.c_str(), e.boolVal);
					break;
				case kATPropertyType_Int32:
					props.SetInt32(e.name.c_str(), e.i32Val);
					break;
				case kATPropertyType_Uint32:
					props.SetUint32(e.name.c_str(), e.u32Val);
					break;
				case kATPropertyType_Float:
					props.SetFloat(e.name.c_str(), e.fVal);
					break;
				case kATPropertyType_Double:
					props.SetDouble(e.name.c_str(), e.dVal);
					break;
				case kATPropertyType_String16:
					if (e.strBuf[0])
						props.SetString(e.name.c_str(), U8ToW(e.strBuf).c_str());
					break;
				default:
					break;
			}
		}
		CleanupGenericEntries();
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0))) {
		CleanupGenericEntries();
		g_devCfg.Reset();
	}

	return false;
}
