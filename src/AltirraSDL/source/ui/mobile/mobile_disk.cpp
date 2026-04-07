//	AltirraSDL - Mobile UI (split from ui_mobile.cpp Phase 3b)
//	Verbatim move; helpers/state shared via mobile_internal.h.

#include <stdafx.h>
#include <cwctype>
#include <vector>
#include <algorithm>
#include <functional>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/registry.h>
#include <vd2/system/error.h>
#include <at/atcore/media.h>
#include <at/atio/image.h>

#include "ui_mobile.h"
#include "ui_main.h"
#include "touch_controls.h"
#include "touch_widgets.h"
#include "simulator.h"
#include "gtia.h"
#include <at/ataudio/pokey.h>
#include "diskinterface.h"
#include "disk.h"
#include <at/atio/diskimage.h>
#include "mediamanager.h"
#include "firmwaremanager.h"
#include "uiaccessors.h"
#include "uitypes.h"
#include "constants.h"
#include "display_backend.h"
#include "android_platform.h"
#include <at/ataudio/audiooutput.h>

#include "mobile_internal.h"

extern ATSimulator g_sim;
extern VDStringA ATGetConfigDir();
extern void ATRegistryFlushToDisk();
extern IDisplayBackend *ATUIGetDisplayBackend();

static const char *BasenameU8(const char *path) {
	const char *p = strrchr(path, '/');
	return p ? p + 1 : path;
}

void RenderMobileDiskRow(ATSimulator &sim, int driveIdx,
	ATMobileUIState &mobileState)
{
	ATDiskInterface &di = sim.GetDiskInterface(driveIdx);
	bool loaded = di.IsDiskLoaded();
	bool dirty  = loaded && di.IsDirty();

	ImGui::PushID(driveIdx);

	// Row background for visual separation
	float rowH = dp(96.0f);
	ImVec2 cursor = ImGui::GetCursorScreenPos();
	float availW = ImGui::GetContentRegionAvail().x;
	ImDrawList *dl = ImGui::GetWindowDrawList();
	dl->AddRectFilled(
		cursor, ImVec2(cursor.x + availW, cursor.y + rowH),
		IM_COL32(30, 35, 50, 200), dp(10.0f));

	// --- Left column: drive label + filename ---
	float leftPad  = dp(16.0f);
	float rightPad = dp(16.0f);
	ImGui::SetCursorScreenPos(ImVec2(cursor.x + leftPad, cursor.y + dp(12.0f)));
	ImGui::SetWindowFontScale(1.25f);
	ImU32 labelCol = dirty
		? IM_COL32(255, 200, 80, 255)
		: IM_COL32(255, 255, 255, 255);
	ImGui::PushStyleColor(ImGuiCol_Text, labelCol);
	ImGui::Text("D%d:", driveIdx + 1);
	ImGui::PopStyleColor();
	ImGui::SetWindowFontScale(1.0f);

	// Filename / status, one line below the drive label.
	ImGui::SetCursorScreenPos(ImVec2(cursor.x + leftPad, cursor.y + dp(46.0f)));
	if (loaded) {
		const wchar_t *path = di.GetPath();
		if (path && *path) {
			VDStringA u8 = VDTextWToU8(VDStringW(path));
			ImGui::PushStyleColor(ImGuiCol_Text,
				ImVec4(0.80f, 0.85f, 0.92f, 1.0f));
			ImGui::Text("%s", BasenameU8(u8.c_str()));
			ImGui::PopStyleColor();
		} else {
			ImGui::PushStyleColor(ImGuiCol_Text,
				ImVec4(0.60f, 0.65f, 0.75f, 1.0f));
			ImGui::TextUnformatted("(loaded)");
			ImGui::PopStyleColor();
		}

		// Show "(modified)" tag if dirty
		if (dirty) {
			ImGui::SetCursorScreenPos(ImVec2(cursor.x + leftPad, cursor.y + dp(68.0f)));
			ImGui::PushStyleColor(ImGuiCol_Text,
				ImVec4(1.0f, 0.78f, 0.30f, 1.0f));
			ImGui::TextUnformatted("modified");
			ImGui::PopStyleColor();
		}
	} else {
		ImGui::PushStyleColor(ImGuiCol_Text,
			ImVec4(0.55f, 0.60f, 0.70f, 1.0f));
		ImGui::TextUnformatted("(empty)");
		ImGui::PopStyleColor();
	}

	// --- Right column: Mount + Eject buttons ---
	float btnW = dp(100.0f);
	float btnH = dp(56.0f);
	float btnGap = dp(8.0f);
	float btnY = cursor.y + (rowH - btnH) * 0.5f;
	float ejectX = cursor.x + availW - rightPad - btnW;
	float mountX = ejectX - btnGap - btnW;

	ImGui::SetCursorScreenPos(ImVec2(mountX, btnY));
	if (ImGui::Button("Mount", ImVec2(btnW, btnH))) {
		s_diskMountTargetDrive = driveIdx;
		s_romFolderMode = false;
		mobileState.currentScreen = ATMobileUIScreen::FileBrowser;
		s_fileBrowserNeedsRefresh = true;
	}

	ImGui::SetCursorScreenPos(ImVec2(ejectX, btnY));
	ImGui::BeginDisabled(!loaded);
	if (ImGui::Button("Eject", ImVec2(btnW, btnH))) {
		try {
			di.UnloadDisk();
		} catch (const MyError &e) {
			ShowInfoModal("Eject Failed", e.c_str());
		}
	}
	ImGui::EndDisabled();

	// Advance the cursor past the row for the next iteration
	ImGui::SetCursorScreenPos(ImVec2(cursor.x, cursor.y + rowH + dp(8.0f)));
	ImGui::PopID();
}

void RenderMobileDiskManager(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window)
{
	ImGuiIO &io = ImGui::GetIO();

	// Full-screen dark background
	ImGui::GetBackgroundDrawList()->AddRectFilled(
		ImVec2(0, 0), io.DisplaySize, IM_COL32(18, 20, 28, 255));

	float insetT = (float)mobileState.layout.insets.top;
	float insetB = (float)mobileState.layout.insets.bottom;
	float insetL = (float)mobileState.layout.insets.left;
	float insetR = (float)mobileState.layout.insets.right;

	ImGui::SetNextWindowPos(ImVec2(insetL, insetT));
	ImGui::SetNextWindowSize(ImVec2(
		io.DisplaySize.x - insetL - insetR,
		io.DisplaySize.y - insetT - insetB));

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoBackground;

	if (ImGui::Begin("##MobileDiskMgr", nullptr, flags)) {
		// Header
		float headerH = dp(48.0f);
		if (ImGui::Button("<", ImVec2(dp(48.0f), headerH)))
			mobileState.currentScreen = ATMobileUIScreen::HamburgerMenu;
		ImGui::SameLine();
		ImGui::SetCursorPosY(
			ImGui::GetCursorPosY() + (headerH - ImGui::GetTextLineHeight()) * 0.5f);
		ImGui::SetWindowFontScale(1.15f);
		ImGui::TextColored(ImVec4(1, 1, 1, 1), "Disk Drives");
		ImGui::SetWindowFontScale(1.0f);

		ImGui::Separator();
		ImGui::Spacing();

		// Scrollable list of drives
		float reserveFooter = dp(140.0f);
		ImGui::BeginChild("DriveList",
			ImVec2(0, ImGui::GetContentRegionAvail().y - reserveFooter),
			ImGuiChildFlags_None);
		ATTouchDragScroll();

		// Default: D1:-D4: (the 99% case)
		int visibleDrives = s_mobileShowAllDrives ? 15 : 4;
		for (int i = 0; i < visibleDrives; ++i)
			RenderMobileDiskRow(sim, i, mobileState);

		// Show/hide additional drives
		ImGui::Spacing();
		if (ImGui::Button(
			s_mobileShowAllDrives ? "Hide drives D5:-D15:" : "Show drives D5:-D15:",
			ImVec2(-1, dp(48.0f))))
		{
			s_mobileShowAllDrives = !s_mobileShowAllDrives;
		}

		ATTouchEndDragScroll();
		ImGui::EndChild();

		// Footer: global emulation-level segmented control
		ImGui::Spacing();
		ATTouchSection("Emulation Level");

		// Match the desktop ui_disk.cpp ordering but collapse to the
		// handful of options a mobile user actually cares about.
		static const ATDiskEmulationMode kMobileEmuValues[] = {
			kATDiskEmulationMode_Generic,
			kATDiskEmulationMode_FastestPossible,
			kATDiskEmulationMode_810,
			kATDiskEmulationMode_1050,
			kATDiskEmulationMode_Happy1050,
		};
		static const char *kMobileEmuLabels[] = {
			"Generic", "Fast", "810", "1050", "Happy",
		};
		constexpr int kNumMobileEmu =
			sizeof(kMobileEmuValues) / sizeof(kMobileEmuValues[0]);

		ATDiskEmulationMode curEmu = sim.GetDiskDrive(0).GetEmulationMode();
		int emuIdx = 0;
		for (int i = 0; i < kNumMobileEmu; ++i)
			if (kMobileEmuValues[i] == curEmu) { emuIdx = i; break; }

		if (ATTouchSegmented("Drive type", &emuIdx,
			kMobileEmuLabels, kNumMobileEmu))
		{
			for (int i = 0; i < 15; ++i)
				sim.GetDiskDrive(i).SetEmulationMode(kMobileEmuValues[emuIdx]);
		}
	}
	ImGui::End();
}
