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

void NavigateUp() {
	// Never leave the filesystem tree.  If we're at "/" there's nothing
	// to do and calling this would corrupt s_fileBrowserDir.
	if (s_fileBrowserDir.empty() || s_fileBrowserDir == L"/")
		return;

	VDStringW parent = s_fileBrowserDir;
	while (!parent.empty() && parent.back() == L'/')
		parent.pop_back();
	for (size_t i = parent.size(); i > 0; --i) {
		if (parent[i - 1] == L'/') {
			if (i > 1)
				parent.resize(i - 1);
			else
				parent.resize(1);  // keep root "/"
			s_fileBrowserDir = parent;
			s_fileBrowserNeedsRefresh = true;
			SaveFileBrowserDir(s_fileBrowserDir);
			return;
		}
	}
	// No '/' found — we were at something weird like "relative".
	// Fall back to the public Downloads dir so the user can recover.
	s_fileBrowserDir = L"/";
	s_fileBrowserNeedsRefresh = true;
	SaveFileBrowserDir(s_fileBrowserDir);
}

// Jump to a well-known directory.  Used by the shortcut buttons so a
// user who has navigated into an empty/dead folder can always get back
// to somewhere useful.
void JumpToDirectory(const char *u8path) {
	s_fileBrowserDir = VDTextU8ToW(VDStringA(u8path));
	s_fileBrowserNeedsRefresh = true;
	SaveFileBrowserDir(s_fileBrowserDir);
}

void RenderFileBrowser(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window)
{
#ifdef __ANDROID__
	// Lazy permission request — only fires the pre-API-30 runtime
	// dialog.  On API 30+ this is a no-op because we rely on
	// MANAGE_EXTERNAL_STORAGE, which requires a Settings page visit,
	// handled via a dedicated banner below.
	if (!IsPermissionAsked()) {
		ATAndroid_RequestStoragePermission();
		SetPermissionAsked();
	}
#endif
	if (s_fileBrowserNeedsRefresh)
		RefreshFileBrowser(s_fileBrowserDir);

	ImGuiIO &io = ImGui::GetIO();

	// Inset inside the safe area so the header and list don't clash
	// with the Android status bar or gesture nav.
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
		| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

	if (ImGui::Begin("##FileBrowser", nullptr, flags)) {
		// Header bar
		float headerH = dp(48.0f);
		ImVec2 backBtnSize(dp(48.0f), headerH);

		if (ImGui::Button("<", backBtnSize)) {
			if (s_diskMountTargetDrive >= 0) {
				// Cancelled disk mount — return to the disk manager.
				s_diskMountTargetDrive = -1;
				mobileState.currentScreen = ATMobileUIScreen::DiskManager;
			} else if (s_romFolderMode) {
				s_romFolderMode = false;
				mobileState.currentScreen = ATMobileUIScreen::Settings;
			} else {
				ATMobileUI_CloseMenu(sim, mobileState);
			}
		}
		ImGui::SameLine();

		const char *title;
		if (s_diskMountTargetDrive >= 0) {
			static char mountTitle[32];
			snprintf(mountTitle, sizeof(mountTitle),
				"Mount into D%d:", s_diskMountTargetDrive + 1);
			title = mountTitle;
		} else if (s_romFolderMode) {
			title = "Select Firmware Folder";
		} else {
			title = "Load Game";
		}
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (headerH - ImGui::GetTextLineHeight()) * 0.5f);
		ImGui::Text("%s", title);

		ImGui::Separator();

		// Current directory
		VDStringA dirU8 = VDTextWToU8(s_fileBrowserDir);
		ImGui::TextWrapped("%s", dirU8.c_str());

#ifdef __ANDROID__
		// Storage permission banner — if the user hasn't granted
		// "All files access" yet, show a prominent prompt explaining
		// the situation and offering a button that jumps straight to
		// the system Settings page.  This is the ONLY way to read
		// arbitrary .xex/.atr files outside the app-private directory
		// on Android 11+ (scoped storage) without going through SAF.
		if (!ATAndroid_HasStoragePermission()) {
			ImGui::PushStyleColor(ImGuiCol_ChildBg,
				ImVec4(0.30f, 0.12f, 0.12f, 0.85f));
			// Auto-size the child to its content so the "Open
			// Settings" button is never clipped — the wrapped
			// explanation text changes height with screen width and
			// font scale, and a fixed 160dp box was too short on
			// narrow phones, hiding the button under the next row.
			ImGui::BeginChild("PermBanner",
				ImVec2(0, 0),
				ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY);
			ImGui::Spacing();
			ImGui::TextColored(ImVec4(1, 1, 1, 1),
				"Storage access required");
			ImGui::TextWrapped(
				"To see ROM and disk image files in /sdcard/Download "
				"and other user folders, Altirra needs the system "
				"\"All files access\" permission.  This is a special "
				"permission you grant from Android Settings.");
			ImGui::Spacing();
			if (ImGui::Button("Open Settings to Grant Access",
				ImVec2(-1, dp(48.0f))))
			{
				ATAndroid_OpenManageStorageSettings();
				ShowInfoModal("Grant Access",
					"Find \"Altirra\" in the \"All files access\" "
					"list in Settings and enable it, then return "
					"to the app and the file list will refresh.");
			}
			ImGui::EndChild();
			ImGui::PopStyleColor();
			ImGui::Spacing();
		}
#endif

		// Quick-access shortcut bar — lets the user jump to common
		// locations from anywhere in the tree, so an accidental climb
		// into a filtered-empty folder is never a dead end.
		{
			float shortcutH = dp(40.0f);
			if (ImGui::Button("Downloads", ImVec2(dp(120.0f), shortcutH))) {
#ifdef __ANDROID__
				const char *dl = ATAndroid_GetPublicDownloadsDir();
				if (dl && *dl)
					JumpToDirectory(dl);
				else
					JumpToDirectory("/storage/emulated/0/Download");
#else
				const char *home = SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS);
				if (home && *home) JumpToDirectory(home);
#endif
			}
			ImGui::SameLine();
			if (ImGui::Button("Storage", ImVec2(dp(100.0f), shortcutH))) {
#ifdef __ANDROID__
				JumpToDirectory("/storage/emulated/0");
#else
				const char *home = SDL_GetUserFolder(SDL_FOLDER_HOME);
				if (home && *home) JumpToDirectory(home);
				else JumpToDirectory("/");
#endif
			}
			ImGui::SameLine();
			if (ImGui::Button("/", ImVec2(dp(50.0f), shortcutH))) {
				JumpToDirectory("/");
			}
		}

		// Navigation row: Up + (in ROM mode) "Select This Folder" button
		float rowBtnH = dp(48.0f);
		if (ImGui::Button(".. (Up)", ImVec2(dp(120.0f), rowBtnH)))
			NavigateUp();

		if (s_romFolderMode) {
			ImGui::SameLine();
			if (ImGui::Button("Use This Folder", ImVec2(-1, rowBtnH))) {
				// Trigger firmware scan on current directory
				s_romDir = s_fileBrowserDir;
				ATFirmwareManager *fwm = g_sim.GetFirmwareManager();
				ExecuteFirmwareScan(fwm, s_romDir);

				// Count detected firmware
				vdvector<ATFirmwareInfo> fwList;
				fwm->GetFirmwareList(fwList);
				s_romScanResult = (int)fwList.size();

				// Reload ROMs after scan so new firmware is active
				g_sim.LoadROMs();

				// Return to settings and show an info popup so the
				// user gets explicit feedback about the scan result.
				s_romFolderMode = false;
				mobileState.currentScreen = ATMobileUIScreen::Settings;

				VDStringA dirU8 = VDTextWToU8(s_romDir);
				char msg[1024];
				if (s_romScanResult > 0) {
					snprintf(msg, sizeof(msg),
						"Found %d ROM file%s in:\n\n%s\n\n"
						"These firmware images are now available.",
						s_romScanResult,
						s_romScanResult == 1 ? "" : "s",
						dirU8.c_str());
					ShowInfoModal("ROMs Imported", msg);
				} else {
					snprintf(msg, sizeof(msg),
						"No recognized Atari ROM files were found in:\n\n%s\n\n"
						"Altirra identifies firmware by content hash, so\n"
						"unmodified ROMs are detected automatically.\n"
						"The built-in replacement kernel is still available.",
						dirU8.c_str());
					ShowInfoModal("No ROMs Found", msg);
				}
			}
		}

		ImGui::Separator();

		// File/directory list
		// Touch scrolling: ImGui's default Selectable + child-scroll
		// interaction swallows the first press and highlights the row,
		// then a drag doesn't scroll because the child window only
		// scrolls when dragged in empty space.  We do two things:
		//   1) Manually scroll the list when the user drags anywhere
		//      inside the child, matching finger delta to scroll delta.
		//   2) Only treat a Selectable's "click" as an activation if
		//      the total drag distance stayed below a small threshold.
		// This gives natural touch-scroll behaviour while still letting
		// a tap select an item.
		float itemH = dp(56.0f);
		ImGui::BeginChild("FileList", ImVec2(0, 0), ImGuiChildFlags_None);

		// Install touch drag-scroll for this child window.  Shared
		// helper from touch_widgets.cpp — identical behaviour across
		// every scrollable surface in the mobile UI.
		ATTouchDragScroll();

		for (size_t i = 0; i < s_fileBrowserEntries.size(); i++) {
			const FileBrowserEntry &entry = s_fileBrowserEntries[i];
			VDStringA nameU8 = VDTextWToU8(VDStringW(entry.name));

			char label[512];
			if (entry.isDirectory)
				snprintf(label, sizeof(label), "[DIR] %s", nameU8.c_str());
			else
				snprintf(label, sizeof(label), "      %s", nameU8.c_str());

			ImGui::PushID((int)i);
			bool activated = ImGui::Selectable(label,
				(int)i == mobileState.selectedFileIdx,
				ImGuiSelectableFlags_AllowOverlap, ImVec2(0, itemH));

			// Suppress the activation if the finger moved — that was
			// a scroll drag, not a tap.
			if (activated && ATTouchIsDraggingBeyondSlop())
				activated = false;

			if (activated) {
				if (entry.isDirectory) {
					s_fileBrowserDir = entry.fullPath;
					s_fileBrowserNeedsRefresh = true;
					SaveFileBrowserDir(s_fileBrowserDir);
					mobileState.selectedFileIdx = -1;
				} else if (s_diskMountTargetDrive >= 0) {
					// Mount-into-drive path (from the mobile disk
					// manager).  Load the image into the target
					// drive and route back to the disk manager with
					// a status popup.
					int drive = s_diskMountTargetDrive;
					s_diskMountTargetDrive = -1;
					try {
						sim.GetDiskInterface(drive)
							.LoadDisk(entry.fullPath.c_str());
						VDStringA u8 = VDTextWToU8(entry.fullPath);
						char msg[1024];
						snprintf(msg, sizeof(msg),
							"Mounted into D%d:\n\n%s",
							drive + 1, u8.c_str());
						ShowInfoModal("Disk Mounted", msg);
					} catch (const MyError &e) {
						ShowInfoModal("Mount Failed", e.c_str());
					}
					mobileState.currentScreen = ATMobileUIScreen::DiskManager;
				} else if (!s_romFolderMode) {
					mobileState.selectedFileIdx = (int)i;
					VDStringA pathU8 = VDTextWToU8(VDStringW(entry.fullPath));
					ATUIPushDeferred(kATDeferred_BootImage, pathU8.c_str());
					mobileState.gameLoaded = true;
					ATMobileUI_CloseMenu(sim, mobileState);
					sim.Resume();
				}
			}
			ImGui::PopID();
		}

		ATTouchEndDragScroll();
		ImGui::EndChild();
	}
	ImGui::End();
}
