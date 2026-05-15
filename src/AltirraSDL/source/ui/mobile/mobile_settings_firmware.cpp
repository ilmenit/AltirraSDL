//	AltirraSDL - Mobile UI: Settings → Firmware sub-page
//	Split out of mobile_settings.cpp to keep it under 20KB.  Verbatim
//	move; behaviour identical.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/vdstl.h>

#include "ui_mobile.h"
#include "touch_widgets.h"
#include "simulator.h"
#include "firmwaremanager.h"

#include "mobile_internal.h"
#include "altirra_icons.h"

extern ATSimulator g_sim;
extern void ATRegistryFlushToDisk();

void RenderSettingsPage_Firmware(ATMobileUIState &mobileState) {
	ATFirmwareManager *fwm = g_sim.GetFirmwareManager();
	const ATMobilePalette &pal = ATMobileGetPalette();

	auto nameForId = [&](uint64 id) -> VDStringA {
		if (!id) return VDStringA("(internal)");
		ATFirmwareInfo info;
		if (fwm->GetFirmwareInfo(id, info))
			return VDTextWToU8(info.mName);
		return VDStringA("(unknown)");
	};

	if (s_fwPicker == kATFirmwareType_Unknown) {
		ATTouchSection("Firmware");

		if (!s_romDir.empty()) {
			VDStringA dirU8 = VDTextWToU8(s_romDir);
			ImGui::PushStyleColor(ImGuiCol_Text, ATMobileCol(pal.text));
			ImGui::TextWrapped("ROM Directory: %s", dirU8.c_str());
			ImGui::PopStyleColor();
		} else {
			ImGui::PushStyleColor(ImGuiCol_Text, ATMobileCol(pal.textMuted));
			ImGui::Text("ROM Directory: (not set)");
			ImGui::PopStyleColor();
		}
		if (s_romScanResult >= 0) {
			ImGui::PushStyleColor(ImGuiCol_Text, ATMobileCol(pal.textMuted));
			ImGui::Text("Status: %d ROMs found", s_romScanResult);
			ImGui::PopStyleColor();
		}

		ImGui::Spacing();
		if (ATTouchButton("Select Firmware Folder",
			ImVec2(-1, dp(56.0f)), ATTouchButtonStyle::Accent,
			ICON_MD_FOLDER_OPEN))
		{
			s_romFolderMode = true;
			s_fileBrowserNeedsRefresh = true;
			mobileState.currentScreen = ATMobileUIScreen::FileBrowser;
		}

		ImGui::Dummy(ImVec2(0, dp(16.0f)));
		ATTouchSection("Kernel & BASIC");

		// Tappable card rows for each user-selectable slot.
		// Kept to the kernels + BASIC that mobile users actually
		// care about — the desktop Firmware Manager covers the
		// long tail of device ROMs.
		struct Slot { const char *title; ATFirmwareType type; };
		static const Slot kSlots[] = {
			{ "OS-B (400/800)",      kATFirmwareType_Kernel800_OSB  },
			{ "OS-A (400/800)",      kATFirmwareType_Kernel800_OSA  },
			{ "XL/XE Kernel",        kATFirmwareType_KernelXL       },
			{ "XEGS Kernel",         kATFirmwareType_KernelXEGS     },
			{ "5200 Kernel",         kATFirmwareType_Kernel5200     },
			{ "Atari BASIC",         kATFirmwareType_Basic          },
		};

		for (size_t i = 0; i < sizeof(kSlots)/sizeof(kSlots[0]); ++i) {
			ImGui::PushID((int)i);
			uint64 curId = fwm->GetDefaultFirmware(kSlots[i].type);
			VDStringA curName = nameForId(curId);

			// Two-line card with chevron — matches the Settings home
			// category rows so the firmware slots feel like part of the
			// same navigation surface.
			if (ATTouchListItem(kSlots[i].title, curName.c_str(),
				/*selected*/ false, /*chevron*/ true))
			{
				s_fwPicker = kSlots[i].type;
			}

			ImGui::Dummy(ImVec2(0, dp(6.0f)));
			ImGui::PopID();
		}

		ImGui::Dummy(ImVec2(0, dp(12.0f)));
		ATTouchMutedText(
			"Tap a slot to choose which ROM to use.  Selections "
			"apply on the next cold reset.  The built-in HLE "
			"kernel is used as a fallback if no ROM is picked.");
	} else {
		// --- Firmware picker ---
		ATFirmwareType picking = s_fwPicker;
		const char *slotTitle = "Firmware";
		switch (picking) {
		case kATFirmwareType_Kernel800_OSA: slotTitle = "OS-A (400/800)"; break;
		case kATFirmwareType_Kernel800_OSB: slotTitle = "OS-B (400/800)"; break;
		case kATFirmwareType_KernelXL:      slotTitle = "XL/XE Kernel"; break;
		case kATFirmwareType_KernelXEGS:    slotTitle = "XEGS Kernel"; break;
		case kATFirmwareType_Kernel5200:    slotTitle = "5200 Kernel"; break;
		case kATFirmwareType_Basic:         slotTitle = "Atari BASIC"; break;
		default: break;
		}
		ATTouchSection(slotTitle);

		if (ATTouchButton("< Back", ImVec2(dp(120.0f), dp(48.0f)),
			ATTouchButtonStyle::Subtle))
		{
			s_fwPicker = kATFirmwareType_Unknown;
		}

		ImGui::Dummy(ImVec2(0, dp(8.0f)));

		vdvector<ATFirmwareInfo> fwList;
		fwm->GetFirmwareList(fwList);

		uint64 curId = fwm->GetDefaultFirmware(picking);

		// "Use built-in HLE" row — selecting this clears the
		// default so the simulator falls back to the bundled
		// HLE kernel at next cold reset.
		{
			bool selected = (curId == 0);
			if (ATTouchListItem("Built-in HLE (fallback)",
				/*subtitle*/ nullptr, selected, /*chevron*/ false))
			{
				fwm->SetDefaultFirmware(picking, 0);
				ATRegistryFlushToDisk();
				g_sim.LoadROMs();
				g_sim.ColdReset();
				s_fwPicker = kATFirmwareType_Unknown;
			}
			ImGui::Dummy(ImVec2(0, dp(6.0f)));
		}

		int shown = 0;
		for (const ATFirmwareInfo &info : fwList) {
			if (info.mType != picking)
				continue;
			if (!info.mbVisible)
				continue;
			++shown;

			ImGui::PushID((int)info.mId ^ (int)(info.mId >> 32));

			VDStringA nm = VDTextWToU8(info.mName);
			VDStringA ph = VDTextWToU8(info.mPath);
			bool selected = (curId == info.mId);

			if (ATTouchListItem(nm.c_str(), ph.c_str(), selected,
				/*chevron*/ false))
			{
				fwm->SetDefaultFirmware(picking, info.mId);
				ATRegistryFlushToDisk();
				g_sim.LoadROMs();
				g_sim.ColdReset();
				s_fwPicker = kATFirmwareType_Unknown;
			}

			ImGui::Dummy(ImVec2(0, dp(6.0f)));
			ImGui::PopID();
		}

		if (shown == 0) {
			ATTouchMutedText(
				"No ROMs of this type were found in your "
				"firmware folder.  Tap 'Select Firmware "
				"Folder' on the previous screen to scan "
				"a directory containing Atari ROM images.");
		}
	}
}
