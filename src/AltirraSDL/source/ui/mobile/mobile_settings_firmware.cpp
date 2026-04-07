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

extern ATSimulator g_sim;
extern void ATRegistryFlushToDisk();

void RenderSettingsPage_Firmware(ATMobileUIState &mobileState) {
	ATFirmwareManager *fwm = g_sim.GetFirmwareManager();

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
			ImGui::TextWrapped("ROM Directory: %s", dirU8.c_str());
		} else {
			ImGui::Text("ROM Directory: (not set)");
		}
		if (s_romScanResult >= 0)
			ImGui::Text("Status: %d ROMs found", s_romScanResult);

		ImGui::Spacing();
		if (ImGui::Button("Select Firmware Folder", ImVec2(-1, dp(56.0f)))) {
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

		float rowH = dp(72.0f);
		for (size_t i = 0; i < sizeof(kSlots)/sizeof(kSlots[0]); ++i) {
			ImGui::PushID((int)i);
			uint64 curId = fwm->GetDefaultFirmware(kSlots[i].type);
			VDStringA curName = nameForId(curId);

			ImVec2 cursor = ImGui::GetCursorScreenPos();
			float availW = ImGui::GetContentRegionAvail().x;
			ImDrawList *dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(cursor,
				ImVec2(cursor.x + availW, cursor.y + rowH),
				IM_COL32(30, 35, 50, 200), dp(10.0f));

			if (ImGui::InvisibleButton("##fwslot", ImVec2(availW, rowH)))
				s_fwPicker = kSlots[i].type;

			dl->AddText(ImVec2(cursor.x + dp(16.0f), cursor.y + dp(10.0f)),
				IM_COL32(240, 242, 248, 255), kSlots[i].title);
			dl->AddText(ImVec2(cursor.x + dp(16.0f), cursor.y + dp(40.0f)),
				IM_COL32(160, 175, 200, 255), curName.c_str());
			dl->AddText(ImVec2(cursor.x + availW - dp(28.0f),
					cursor.y + rowH * 0.5f - dp(8.0f)),
				IM_COL32(160, 175, 200, 255), ">");

			ImGui::Dummy(ImVec2(0, dp(8.0f)));
			ImGui::PopID();
		}

		ImGui::Dummy(ImVec2(0, dp(12.0f)));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.70f, 0.78f, 1));
		ImGui::TextWrapped(
			"Tap a slot to choose which ROM to use.  Selections "
			"apply on the next cold reset.  The built-in HLE "
			"kernel is used as a fallback if no ROM is picked.");
		ImGui::PopStyleColor();
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

		if (ImGui::Button("< Back", ImVec2(dp(120.0f), dp(48.0f))))
			s_fwPicker = kATFirmwareType_Unknown;

		ImGui::Dummy(ImVec2(0, dp(8.0f)));

		vdvector<ATFirmwareInfo> fwList;
		fwm->GetFirmwareList(fwList);

		uint64 curId = fwm->GetDefaultFirmware(picking);

		// "Use built-in HLE" row — selecting this clears the
		// default so the simulator falls back to the bundled
		// HLE kernel at next cold reset.
		{
			float rowH = dp(64.0f);
			ImVec2 cursor = ImGui::GetCursorScreenPos();
			float availW = ImGui::GetContentRegionAvail().x;
			ImDrawList *dl = ImGui::GetWindowDrawList();
			bool selected = (curId == 0);
			dl->AddRectFilled(cursor,
				ImVec2(cursor.x + availW, cursor.y + rowH),
				selected ? IM_COL32(40, 90, 160, 220)
				         : IM_COL32(30, 35, 50, 200),
				dp(10.0f));
			if (ImGui::InvisibleButton("##fwhle", ImVec2(availW, rowH))) {
				fwm->SetDefaultFirmware(picking, 0);
				ATRegistryFlushToDisk();
				g_sim.LoadROMs();
				g_sim.ColdReset();
				s_fwPicker = kATFirmwareType_Unknown;
			}
			dl->AddText(ImVec2(cursor.x + dp(16.0f),
					cursor.y + rowH * 0.5f - dp(8.0f)),
				IM_COL32(240, 242, 248, 255),
				"Built-in HLE (fallback)");
			ImGui::Dummy(ImVec2(0, dp(8.0f)));
		}

		int shown = 0;
		for (const ATFirmwareInfo &info : fwList) {
			if (info.mType != picking)
				continue;
			if (!info.mbVisible)
				continue;
			++shown;

			ImGui::PushID((int)info.mId ^ (int)(info.mId >> 32));
			float rowH = dp(64.0f);
			ImVec2 cursor = ImGui::GetCursorScreenPos();
			float availW = ImGui::GetContentRegionAvail().x;
			ImDrawList *dl = ImGui::GetWindowDrawList();
			bool selected = (curId == info.mId);
			dl->AddRectFilled(cursor,
				ImVec2(cursor.x + availW, cursor.y + rowH),
				selected ? IM_COL32(40, 90, 160, 220)
				         : IM_COL32(30, 35, 50, 200),
				dp(10.0f));
			if (ImGui::InvisibleButton("##fw", ImVec2(availW, rowH))) {
				fwm->SetDefaultFirmware(picking, info.mId);
				ATRegistryFlushToDisk();
				g_sim.LoadROMs();
				g_sim.ColdReset();
				s_fwPicker = kATFirmwareType_Unknown;
			}
			VDStringA nm = VDTextWToU8(info.mName);
			VDStringA ph = VDTextWToU8(info.mPath);
			dl->AddText(ImVec2(cursor.x + dp(16.0f), cursor.y + dp(8.0f)),
				IM_COL32(240, 242, 248, 255), nm.c_str());
			dl->AddText(ImVec2(cursor.x + dp(16.0f), cursor.y + dp(36.0f)),
				IM_COL32(160, 175, 200, 255), ph.c_str());
			ImGui::Dummy(ImVec2(0, dp(8.0f)));
			ImGui::PopID();
		}

		if (shown == 0) {
			ImGui::PushStyleColor(ImGuiCol_Text,
				ImVec4(0.65f, 0.70f, 0.78f, 1));
			ImGui::TextWrapped(
				"No ROMs of this type were found in your "
				"firmware folder.  Tap 'Select Firmware "
				"Folder' on the previous screen to scan "
				"a directory containing Atari ROM images.");
			ImGui::PopStyleColor();
		}
	}
}
