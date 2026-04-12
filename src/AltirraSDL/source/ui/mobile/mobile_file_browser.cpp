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
#include <vd2/system/zip.h>
#include <at/atcore/media.h>
#include <at/atcore/vfs.h>
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
	if (!s_zipArchivePath.empty()) {
		if (s_zipInternalDir.empty()) {
			s_zipArchivePath.clear();
		} else {
			VDStringW d = s_zipInternalDir;
			while (!d.empty() && d.back() == L'/')
				d.pop_back();
			const wchar_t *base = d.c_str();
			const wchar_t *lastSlash = wcsrchr(base, L'/');
			if (lastSlash)
				s_zipInternalDir.assign(base, (size_t)(lastSlash - base));
			else
				s_zipInternalDir.clear();
		}
		s_fileBrowserNeedsRefresh = true;
		return;
	}

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
				parent.resize(1);
			s_fileBrowserDir = parent;
			s_fileBrowserNeedsRefresh = true;
			SaveFileBrowserDir(s_fileBrowserDir);
			return;
		}
	}
	s_fileBrowserDir = L"/";
	s_fileBrowserNeedsRefresh = true;
	SaveFileBrowserDir(s_fileBrowserDir);
}

// Jump to a well-known directory.  Used by the shortcut buttons so a
// user who has navigated into an empty/dead folder can always get back
// to somewhere useful.
void JumpToDirectory(const char *u8path) {
	s_zipArchivePath.clear();
	s_zipInternalDir.clear();
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
			s_zipArchivePath.clear();
			s_zipInternalDir.clear();
			if (s_diskMountTargetDrive >= 0) {
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

		{
			VDStringA dirU8;
			if (!s_zipArchivePath.empty()) {
				VDStringA zipName = VDTextWToU8(
					VDStringW(VDFileSplitPath(s_zipArchivePath.c_str())));
				VDStringA intDir = VDTextWToU8(s_zipInternalDir);
				if (intDir.empty())
					dirU8.sprintf("%s/", zipName.c_str());
				else
					dirU8.sprintf("%s/%s/", zipName.c_str(), intDir.c_str());
			} else {
				dirU8 = VDTextWToU8(s_fileBrowserDir);
			}
			const char *display = dirU8.c_str();

			const char *p = dirU8.c_str() + dirU8.length();
			int slashes = 0;
			while (p > dirU8.c_str()) {
				--p;
				if (*p == '/' && ++slashes == 2) break;
			}
			char shortPath[256];
			if (slashes >= 2 && p > dirU8.c_str()) {
				snprintf(shortPath, sizeof(shortPath), "...%s", p);
				display = shortPath;
			}

			ImGui::PushStyleColor(ImGuiCol_Text,
				ImVec4(0.65f, 0.72f, 0.82f, 1.0f));
			ImGui::TextUnformatted(display);
			ImGui::PopStyleColor();
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
				ImGui::SetTooltip("%s", dirU8.c_str());
		}

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
			ImGui::BeginChild("PermBanner",
				ImVec2(0, 0),
				ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY
					| ImGuiChildFlags_NavFlattened);
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

		// Shortcut + navigation bar — a single row (with wrapping)
		// that contains ".." (up), location shortcuts, and "/".
		// Merging Up into the shortcut row eliminates a whole row of
		// chrome, giving more space to the file list in landscape.
		//
		// The active shortcut (whose path is a prefix of the current
		// directory) is highlighted so the user can orient at a glance.
		// When multiple shortcuts match (e.g. Downloads is inside
		// Internal), the longest prefix wins.
		{
			float shortcutH = dp(44.0f);
			const ImGuiStyle &style = ImGui::GetStyle();
			float availRight = ImGui::GetCursorScreenPos().x
				+ ImGui::GetContentRegionAvail().x;

			auto sameLineIfFits = [&](float nextW) {
				float afterPrev = ImGui::GetItemRectMax().x
					+ style.ItemSpacing.x;
				if (afterPrev + nextW <= availRight)
					ImGui::SameLine();
			};

			// --- Determine which shortcut is "active" (longest
			// prefix match against the current directory) ---
			VDStringA dirU8 = VDTextWToU8(s_fileBrowserDir);
			const char *activePath = nullptr;
			size_t activeLen = 0;

			auto checkActive = [&](const char *path) {
				if (!path || !*path) return;
				size_t len = strlen(path);
				// Strip trailing slash for comparison.
				while (len > 1 && path[len - 1] == '/') --len;
				if (len <= activeLen) return;
				if (dirU8.length() < len) return;
				if (strncmp(dirU8.c_str(), path, len) != 0) return;
				// Must match at a path boundary.
				if (dirU8.length() > len && dirU8[len] != '/') return;
				activePath = path;
				activeLen = len;
			};

			// Check Downloads.
#ifdef __ANDROID__
			const char *dlPath = ATAndroid_GetPublicDownloadsDir();
			if (!dlPath || !*dlPath) dlPath = "/storage/emulated/0/Download";
			checkActive(dlPath);
#else
			// Copy SDL strings — SDL_GetUserFolder may use a shared
			// internal buffer that a second call could overwrite.
			VDStringA dlPathStr, homePathStr;
			{
				const char *p = SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS);
				if (p) dlPathStr = p;
			}
			{
				const char *p = SDL_GetUserFolder(SDL_FOLDER_HOME);
				if (p) homePathStr = p;
			}
			const char *dlPath = dlPathStr.empty()
				? nullptr : dlPathStr.c_str();
			const char *homePath = homePathStr.empty()
				? nullptr : homePathStr.c_str();
			if (dlPath) checkActive(dlPath);
			if (homePath) checkActive(homePath);
#endif

			// "/" is the fallback — only active if nothing else matched.
			if (!activePath)
				checkActive("/");

			// Highlight style for the active shortcut.
			auto pushActiveStyle = [](bool active) {
				if (active) {
					ImGui::PushStyleColor(ImGuiCol_Button,
						ImVec4(0.22f, 0.42f, 0.70f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
						ImVec4(0.26f, 0.50f, 0.80f, 1.0f));
				}
			};
			auto popActiveStyle = [](bool active) {
				if (active) ImGui::PopStyleColor(2);
			};

			// --- ".." (Up) — first in the row for easy reach ---
			{
				bool atRoot = s_zipArchivePath.empty()
					&& (s_fileBrowserDir.empty()
						|| s_fileBrowserDir == L"/");
				if (atRoot) {
					ImGui::BeginDisabled();
				}
				if (ImGui::Button("..", ImVec2(0, shortcutH)))
					NavigateUp();
				if (atRoot) {
					ImGui::EndDisabled();
				}
			}

			// --- Downloads ---
			{
				bool active = dlPath && activePath
					&& activeLen == strlen(dlPath)
					&& strncmp(activePath, dlPath, activeLen) == 0;
				float btnW = ImGui::CalcTextSize("Downloads").x
					+ style.FramePadding.x * 2.0f;
				sameLineIfFits(btnW);
				pushActiveStyle(active);
				if (ImGui::Button("Downloads", ImVec2(0, shortcutH))) {
#ifdef __ANDROID__
					JumpToDirectory(dlPath);
#else
					if (dlPath && *dlPath) JumpToDirectory(dlPath);
#endif
				}
				popActiveStyle(active);
			}

#ifdef __ANDROID__
			const auto& volumes = ATAndroid_GetStorageVolumes();
			for (size_t vi = 0; vi < volumes.size(); vi++) {
				const ATAndroidVolume &vol = volumes[vi];

				char btnLabel[64];
				if (vol.removable)
					snprintf(btnLabel, sizeof(btnLabel), "%.59s",
						vol.label.c_str());
				else
					snprintf(btnLabel, sizeof(btnLabel), "Internal");

				bool active = activePath
					&& activeLen == vol.path.size()
					&& strncmp(activePath, vol.path.c_str(),
						activeLen) == 0;

				float btnW = ImGui::CalcTextSize(btnLabel).x
					+ style.FramePadding.x * 2.0f;
				sameLineIfFits(btnW);

				ImGui::PushID((int)(0x1000 + vi));
				pushActiveStyle(active);
				if (ImGui::Button(btnLabel, ImVec2(0, shortcutH)))
					JumpToDirectory(vol.path.c_str());
				popActiveStyle(active);
				ImGui::PopID();
			}
#else
			{
				bool active = activePath && homePath
					&& activeLen == strlen(homePath)
					&& strncmp(activePath, homePath, activeLen) == 0;
				float btnW = ImGui::CalcTextSize("Home").x
					+ style.FramePadding.x * 2.0f;
				sameLineIfFits(btnW);
				pushActiveStyle(active);
				if (ImGui::Button("Home", ImVec2(0, shortcutH))) {
					if (homePath && *homePath) JumpToDirectory(homePath);
					else JumpToDirectory("/");
				}
				popActiveStyle(active);
			}
#endif

			// --- "/" (root) ---
			{
				bool active = activePath
					&& activeLen == 1 && activePath[0] == '/';
				sameLineIfFits(dp(40.0f));
				pushActiveStyle(active);
				if (ImGui::Button("/", ImVec2(dp(40.0f), shortcutH)))
					JumpToDirectory("/");
				popActiveStyle(active);
			}
		}

		// In ROM-folder mode, the user picks a directory instead of a
		// file — show a full-width "Use This Folder" action button.
		if (s_romFolderMode) {
			float rowBtnH = dp(44.0f);
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
		// NavFlattened lets gamepad nav cross into the list without an
		// explicit "enter child" press — see mobile_hamburger.cpp.
		ImGui::BeginChild("FileList", ImVec2(0, 0),
			ImGuiChildFlags_NavFlattened);

		// Install touch drag-scroll for this child window.  Shared
		// helper from touch_widgets.cpp — identical behaviour across
		// every scrollable surface in the mobile UI.
		ATTouchDragScroll();

		bool insideZip = !s_zipArchivePath.empty();

		for (size_t i = 0; i < s_fileBrowserEntries.size(); i++) {
			const FileBrowserEntry &entry = s_fileBrowserEntries[i];
			VDStringA nameU8 = VDTextWToU8(VDStringW(entry.name));

			char label[512];
			if (entry.isDirectory)
				snprintf(label, sizeof(label), "[DIR] %s", nameU8.c_str());
			else if (!insideZip && IsZipFile(entry.name.c_str()))
				snprintf(label, sizeof(label), "[ZIP] %s", nameU8.c_str());
			else
				snprintf(label, sizeof(label), "      %s", nameU8.c_str());

			ImGui::PushID((int)i);
			bool activated = ImGui::Selectable(label,
				(int)i == mobileState.selectedFileIdx,
				ImGuiSelectableFlags_AllowOverlap, ImVec2(0, itemH));

			if (activated && ATTouchIsDraggingBeyondSlop())
				activated = false;

			if (activated) {
				if (entry.isDirectory) {
					if (insideZip) {
						s_zipInternalDir = entry.fullPath;
					} else {
						s_fileBrowserDir = entry.fullPath;
						SaveFileBrowserDir(s_fileBrowserDir);
					}
					s_fileBrowserNeedsRefresh = true;
					mobileState.selectedFileIdx = -1;
				} else if (!insideZip && !s_romFolderMode
					&& IsZipFile(entry.name.c_str()))
				{
					s_zipArchivePath = entry.fullPath;
					s_zipInternalDir.clear();
					s_fileBrowserNeedsRefresh = true;
					mobileState.selectedFileIdx = -1;
				} else if (s_diskMountTargetDrive >= 0) {
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
