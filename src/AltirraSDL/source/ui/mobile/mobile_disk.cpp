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

	// Row background: gradient card, same recipe as the Settings home
	// category rows.  Matches the rest of the Gaming-Mode UI.
	float rowH = dp(80.0f);
	ImVec2 cursor = ImGui::GetCursorScreenPos();
	float availW = ImGui::GetContentRegionAvail().x;
	ImDrawList *dl = ImGui::GetWindowDrawList();
	const ATMobilePalette &pal = ATMobileGetPalette();
	{
		ImVec2 cardBR(cursor.x + availW, cursor.y + rowH);
		ATMobileDrawGradientRect(cursor, cardBR,
			pal.cardBgTop, pal.cardBg, dp(10.0f));
		dl->AddRect(cursor, cardBR, pal.cardBorder, dp(10.0f), 0, 1.0f);
	}

	// --- Left column: drive label + filename ---
	float leftPad  = dp(16.0f);
	float rightPad = dp(16.0f);
	ImGui::SetCursorScreenPos(ImVec2(cursor.x + leftPad, cursor.y + dp(10.0f)));
	ImGui::SetWindowFontScale(1.25f);
	// Semantic warning colour for "modified", palette text otherwise.
	ImU32 labelCol = dirty ? pal.warning : pal.text;
	ImGui::PushStyleColor(ImGuiCol_Text, ATMobileCol(labelCol));
	ImGui::Text("D%d:", driveIdx + 1);
	ImGui::PopStyleColor();
	ImGui::SetWindowFontScale(1.0f);

	// Filename / status, one line below the drive label.
	ImGui::SetCursorScreenPos(ImVec2(cursor.x + leftPad, cursor.y + dp(36.0f)));
	if (loaded) {
		const wchar_t *path = di.GetPath();
		if (path && *path) {
			VDStringA u8 = VDTextWToU8(VDStringW(path));
			ImGui::PushStyleColor(ImGuiCol_Text, ATMobileCol(pal.text));
			ImGui::Text("%s", BasenameU8(u8.c_str()));
			ImGui::PopStyleColor();
		} else {
			ImGui::PushStyleColor(ImGuiCol_Text, ATMobileCol(pal.textMuted));
			ImGui::TextUnformatted("(loaded)");
			ImGui::PopStyleColor();
		}

		// Show "(modified)" tag if dirty.
		if (dirty) {
			ImGui::SetCursorScreenPos(ImVec2(cursor.x + leftPad, cursor.y + dp(58.0f)));
			ImGui::PushStyleColor(ImGuiCol_Text, ATMobileCol(pal.warning));
			ImGui::TextUnformatted("modified");
			ImGui::PopStyleColor();
		}
	} else {
		ImGui::PushStyleColor(ImGuiCol_Text, ATMobileCol(pal.textMuted));
		ImGui::TextUnformatted("(empty)");
		ImGui::PopStyleColor();
	}

	// --- Right column: Mount [+ Select] + Eject buttons ---
	// Select is only shown when the mounted disk belongs to a
	// multi-variant Game Library entry — see GameBrowser_FindEntryForPath.
	int gameEntry = -1;
	if (loaded) {
		const wchar_t *path = di.GetPath();
		if (path && *path)
			gameEntry = GameBrowser_FindEntryForPath(path);
	}
	const bool hasAlts = gameEntry >= 0
		&& GameBrowser_GetVariantCount(gameEntry) > 1;

	float btnW = dp(88.0f);
	float btnH = dp(48.0f);
	float btnGap = dp(8.0f);
	float btnY = cursor.y + (rowH - btnH) * 0.5f;
	float ejectX = cursor.x + availW - rightPad - btnW;
	float selectX = hasAlts ? (ejectX - btnGap - btnW) : ejectX;
	float mountX = hasAlts
		? (selectX - btnGap - btnW)
		: (ejectX - btnGap - btnW);

	ImGui::SetCursorScreenPos(ImVec2(mountX, btnY));
	if (ATTouchButton("Mount", ImVec2(btnW, btnH),
		ATTouchButtonStyle::Accent))
	{
		s_diskMountTargetDrive = driveIdx;
		s_romFolderMode = false;
		mobileState.currentScreen = ATMobileUIScreen::FileBrowser;
		s_fileBrowserNeedsRefresh = true;
	}

	if (hasAlts) {
		ImGui::SetCursorScreenPos(ImVec2(selectX, btnY));
		if (ATTouchButton("Side", ImVec2(btnW, btnH))) {
			int drive = driveIdx;
			GameBrowser_ShowVariantPickerForSwap(gameEntry,
				[drive](const VDStringW &variantPath) {
					ATDiskInterface &tgt =
						g_sim.GetDiskInterface(drive);
					try {
						// Route through ATSimulator::Load (matches
						// Windows uidisk.cpp:1060-1065).  The 1-arg
						// ATDiskInterface::LoadDisk path flags images
						// as non-updatable; see ui_main.cpp
						// kATDeferred_AttachDisk for the full note.
						// The Side button is only shown when the drive
						// already has a disk mounted, so inheriting the
						// current write mode keeps the same R/O vs.
						// R/W vs. VRW choice the user had.
						ATImageLoadContext ctx;
						ctx.mLoadType  = kATImageType_Disk;
						ctx.mLoadIndex = drive;
						g_sim.Load(variantPath.c_str(),
							tgt.GetWriteMode(), &ctx);
					} catch (const MyError &e) {
						ShowInfoModal("Mount Failed", e.c_str());
					}
				});
		}
	}

	ImGui::SetCursorScreenPos(ImVec2(ejectX, btnY));
	ImGui::BeginDisabled(!loaded);
	if (ATTouchButton("Eject", ImVec2(btnW, btnH))) {
		try {
			di.UnloadDisk();
			sim.GetDiskDrive(driveIdx).SetEnabled(false);
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

	// Full-screen background — palette-aware so light theme doesn't
	// punch a black hole behind the translucent card rows.
	{
		const ATMobilePalette &bgPal = ATMobileGetPalette();
		ImGui::GetBackgroundDrawList()->AddRectFilled(
			ImVec2(0, 0), io.DisplaySize, bgPal.windowBg);
	}

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
		// ESC / B-button / Backspace returns to hamburger.
		if (!s_confirmActive && !s_infoModalOpen) {
			bool back = ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
			if (!ImGui::IsAnyItemActive()) {
				back = back
					|| ImGui::IsKeyPressed(ImGuiKey_Escape, false)
					|| ImGui::IsKeyPressed(ImGuiKey_Backspace, false);
			}
			if (back)
				mobileState.currentScreen = ATMobileUIScreen::HamburgerMenu;
		}

		// 8 dp top padding so the header never sits flush with the
		// status bar on devices with a small top inset.
		ImGui::Dummy(ImVec2(0, dp(8.0f)));

		// Header
		float headerH = dp(48.0f);
		if (ATTouchButton("<", ImVec2(dp(48.0f), headerH),
			ATTouchButtonStyle::Subtle))
		{
			mobileState.currentScreen = ATMobileUIScreen::HamburgerMenu;
		}
		ImGui::SameLine();
		ImGui::SetCursorPosY(
			ImGui::GetCursorPosY() + (headerH - ImGui::GetTextLineHeight()) * 0.5f);
		ImGui::SetWindowFontScale(1.15f);
		{
			const ATMobilePalette &hdrPal = ATMobileGetPalette();
			ImGui::TextColored(ATMobileCol(hdrPal.text), "Disk Drives");
		}
		ImGui::SetWindowFontScale(1.0f);

		ImGui::Separator();
		ImGui::Spacing();

		// Single-scroll layout: the whole window scrolls — drive list
		// AND emulation-level footer share one scroll region.  This
		// avoids the previous "reserve 140 dp for footer, clip
		// everything else" split, which clipped content on short
		// viewports.
		ATTouchDragScroll();

		// Default: D1:-D4: (the 99% case)
		int visibleDrives = s_mobileShowAllDrives ? 15 : 4;
		for (int i = 0; i < visibleDrives; ++i)
			RenderMobileDiskRow(sim, i, mobileState);

		// Show/hide additional drives
		ImGui::Spacing();
		if (ATTouchButton(
			s_mobileShowAllDrives ? "Hide drives D5:-D15:" : "Show drives D5:-D15:",
			ImVec2(-1, dp(48.0f))))
		{
			s_mobileShowAllDrives = !s_mobileShowAllDrives;
		}

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

		// 8 dp bottom padding so the segmented control never sits
		// flush with the gesture-bar inset on tight viewports.
		ImGui::Dummy(ImVec2(0, dp(8.0f)));

		ATTouchEndDragScroll();
	}
	ImGui::End();
}
