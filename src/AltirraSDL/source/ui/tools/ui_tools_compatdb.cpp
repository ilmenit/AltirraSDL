//	AltirraSDL - Tools dialog (split from ui_tools.cpp, Phase 2k)

#include <stdafx.h>
#include <algorithm>
#include <string>
#include <mutex>
#include <thread>
#include <vector>
#include <cstring>
#include <cstdio>
#include <imgui.h>
#include <SDL3/SDL.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/error.h>
#include <vd2/system/date.h>
#include <vd2/system/registry.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/configvar.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/media.h>
#include <at/atio/image.h>
#include <at/atio/diskimage.h>
#include <at/atio/cartridgeimage.h>
#include <at/atio/cassetteimage.h>
#include <vd2/Dita/accel.h>
#include "ui_main.h"
#include "accel_sdl3.h"
#include "simulator.h"
#include "gtia.h"
#include "constants.h"
#include "disk.h"
#include "diskinterface.h"
#include "firmwaremanager.h"
#include "firmwaredetect.h"
#include "compatengine.h"
#include "settings.h"
#include "uiaccessors.h"
#include "uikeyboard.h"
#include "uitypes.h"
#include "options.h"
#include "oshelper.h"

extern ATSimulator g_sim;

// =========================================================================
// Compatibility Database
// Reference: src/Altirra/source/uicompatdb.cpp (1,154 lines Win32)
// =========================================================================

#include <at/atio/image.h>
#include <at/atio/blobimage.h>
#include "compatedb.h"
#include "compatengine.h"
#include "compatdb.h"
#include "uicompat.h"
#include "cartridge.h"
#include "cassette.h"
#include "disk.h"
#include "diskinterface.h"
#include "hleprogramloader.h"
#include "logging.h"

// Local reimplementation of uicompatdb.cpp helper (excluded by ui* regex)
static void ATCompatAddSourcedRulesForImage(vdvector<ATCompatEDBSourcedAliasRule> &sourcedRules, IATImage *image, const wchar_t *sourceName) {
	if (!image) return;
	vdfastvector<ATCompatMarker> markers;
	ATCompatGetMarkersForImage(markers, image);
	for (const ATCompatMarker &marker : markers) {
		ATCompatEDBSourcedAliasRule &rule = sourcedRules.push_back();
		rule.mRule = marker;
		rule.mSource = sourceName;
	}
}

static struct CompatDBState {
	ATCompatEDB edb;
	VDStringW path;
	VDStringW compilePath;
	bool modified = false;
	bool needsCompile = false;
	bool isExternalDb = false;
	bool initialized = false;

	// Display state
	vdfastvector<ATCompatEDBTitle *> displayedTitles;
	int selectedTitle = -1;
	int selectedAlias = -1;
	int selectedTag = -1;
	char searchBuf[128] = {};
	char titleNameBuf[256] = {};

	// Tag picker popup
	bool showTagPicker = false;
	ATCompatKnownTag lastSelectedTag = kATCompatKnownTag_None;

	// Close confirmation state
	bool wantClose = false;
	bool showDiscardConfirm = false;
	bool showCompileConfirm = false;

	void Reset() {
		edb = ATCompatEDB();
		path.clear();
		compilePath.clear();
		modified = false;
		needsCompile = false;
		isExternalDb = false;
		initialized = false;
		displayedTitles.clear();
		selectedTitle = -1;
		selectedAlias = -1;
		selectedTag = -1;
		searchBuf[0] = 0;
		titleNameBuf[0] = 0;
		showTagPicker = false;
	}

	void RefreshTitles() {
		displayedTitles.clear();
		selectedAlias = -1;
		selectedTag = -1;

		VDStringW searchLower;
		if (searchBuf[0]) {
			searchLower = VDTextU8ToW(searchBuf, -1);
			for (wchar_t &c : searchLower) c = towlower(c);
		}

		VDStringW tmp;
		for (auto *title : edb.mTitleTable) {
			if (!searchLower.empty()) {
				tmp = title->mName;
				for (wchar_t &c : tmp) c = towlower(c);
				if (!wcsstr(tmp.c_str(), searchLower.c_str()))
					continue;
			}
			displayedTitles.push_back(title);
		}

		std::sort(displayedTitles.begin(), displayedTitles.end(),
			[](const ATCompatEDBTitle *a, const ATCompatEDBTitle *b) {
				return a->mName.comparei(b->mName) < 0;
			});

		if (selectedTitle >= (int)displayedTitles.size())
			selectedTitle = displayedTitles.empty() ? -1 : (int)displayedTitles.size() - 1;
	}

	void SetModified() {
		modified = true;
		needsCompile = true;
	}

	VDStringA GetCaption() const {
		VDStringA s;
		if (modified) s += "*";
		if (path.empty())
			s += "New file";
		else {
			VDStringW name = VDFileSplitPathRightSpan(path);
			s += VDTextWToU8(name);
		}
		s += " - Compatibility Database";
		return s;
	}
} g_compatDB;

// Thread-safe pending paths for compat DB file operations
static std::mutex g_compatDBFileMutex;
static std::string g_compatDBPendingLoad;
static std::string g_compatDBPendingSave;
static std::string g_compatDBPendingCompile;

static void CompatDBLoadCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	std::lock_guard<std::mutex> lock(g_compatDBFileMutex);
	g_compatDBPendingLoad = filelist[0];
}

static void CompatDBSaveCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	std::lock_guard<std::mutex> lock(g_compatDBFileMutex);
	g_compatDBPendingSave = filelist[0];
}

static void CompatDBCompileCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	std::lock_guard<std::mutex> lock(g_compatDBFileMutex);
	g_compatDBPendingCompile = filelist[0];
}

static void CompatDBProcessPending() {
	std::string loadPath, savePath, compilePath;
	{
		std::lock_guard<std::mutex> lock(g_compatDBFileMutex);
		loadPath.swap(g_compatDBPendingLoad);
		savePath.swap(g_compatDBPendingSave);
		compilePath.swap(g_compatDBPendingCompile);
	}

	if (!loadPath.empty()) {
		try {
			VDStringW wpath = VDTextU8ToW(loadPath.c_str(), -1);
			ATCompatEDB tempEDB;
			ATLoadCompatEDB(wpath.c_str(), tempEDB);
			for (auto &entry : tempEDB.mTagTable) {
				auto knownTag = ATCompatGetKnownTagByKey(entry.second.mKey.c_str());
				if (knownTag)
					entry.second.mDisplayName = ATUICompatGetKnownTagDisplayName(knownTag);
			}
			g_compatDB.edb = std::move(tempEDB);
			g_compatDB.path = wpath;
			g_compatDB.compilePath.clear();
			g_compatDB.modified = false;
			g_compatDB.isExternalDb = false;
			g_compatDB.needsCompile = false;
			g_compatDB.RefreshTitles();
		} catch (const MyError &e) {
			LOG_ERROR("UI", "CompatDB load failed: %s", e.c_str());
		}
	}

	if (!savePath.empty()) {
		try {
			VDStringW wpath = VDTextU8ToW(savePath.c_str(), -1);
			ATSaveCompatEDB(wpath.c_str(), g_compatDB.edb);
			g_compatDB.path = wpath;
			g_compatDB.modified = false;
		} catch (const MyError &e) {
			LOG_ERROR("UI", "CompatDB save failed: %s", e.c_str());
		}
	}

	if (!compilePath.empty()) {
		try {
			VDStringW wpath = VDTextU8ToW(compilePath.c_str(), -1);
			vdblock<char> buf;
			ATCompileCompatEDB(buf, g_compatDB.edb);
			VDFile f(wpath.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
			f.write(buf.data(), (long)buf.size());
			f.close();
			g_compatDB.compilePath = wpath;
			g_compatDB.needsCompile = false;
			ATCompatReloadExtDatabase();
		} catch (const MyError &e) {
			LOG_ERROR("UI", "CompatDB compile failed: %s", e.c_str());
		}
	}
}

void ATUIRenderCompatDB(ATSimulator &sim, ATUIState &state) {
	// Process pending file dialog results on main thread
	CompatDBProcessPending();

	if (!g_compatDB.initialized) {
		g_compatDB.initialized = true;

		// Auto-load external DB if present
		if (ATCompatIsExtDatabaseLoaded()) {
			const VDStringW &dbPath = ATCompatGetExtDatabasePath();
			if (!dbPath.empty()) {
				try {
					VDStringW srcPath = VDFileSplitExtLeft(dbPath) + L".atcompatdb";
					ATLoadCompatEDB(srcPath.c_str(), g_compatDB.edb);
					g_compatDB.path = srcPath;
					g_compatDB.compilePath = dbPath;
					g_compatDB.isExternalDb = true;

					for (auto &entry : g_compatDB.edb.mTagTable) {
						auto knownTag = ATCompatGetKnownTagByKey(entry.second.mKey.c_str());
						if (knownTag)
							entry.second.mDisplayName = ATUICompatGetKnownTagDisplayName(knownTag);
					}
				} catch (const MyError &) {
				}
			}
		}
		g_compatDB.RefreshTitles();
	}

	VDStringA caption = g_compatDB.GetCaption();

	ImGui::SetNextWindowSize(ImVec2(750, 550), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	bool open = state.showCompatDB;
	if (!ImGui::Begin(caption.c_str(), &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar)) {
		// Window collapsed — if user clicked X, just flag for close (popup will show next frame when uncollapsed)
		if (!open) {
			if (!g_compatDB.modified && !(g_compatDB.isExternalDb && g_compatDB.needsCompile)) {
				g_compatDB.Reset();
				state.showCompatDB = false;
			} else {
				g_compatDB.wantClose = true;
			}
		}
		ImGui::End();
		return;
	}

	// Handle deferred close request (from collapsed state or ESC/X)
	if (g_compatDB.wantClose || !open || ATUICheckEscClose()) {
		g_compatDB.wantClose = false;
		if (g_compatDB.modified) {
			g_compatDB.showDiscardConfirm = true;
		} else if (g_compatDB.isExternalDb && g_compatDB.needsCompile) {
			g_compatDB.showCompileConfirm = true;
		} else {
			g_compatDB.Reset();
			state.showCompatDB = false;
			ImGui::End();
			return;
		}
	}

	// Menu bar
	if (ImGui::BeginMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("New")) {
				if (!g_compatDB.modified) {
					g_compatDB.edb = ATCompatEDB();
					g_compatDB.path.clear();
					g_compatDB.compilePath.clear();
					g_compatDB.modified = false;
					g_compatDB.needsCompile = false;
					g_compatDB.isExternalDb = false;
					g_compatDB.RefreshTitles();
				} else {
					g_compatDB.showDiscardConfirm = true;
				}
			}
			if (ImGui::MenuItem("Load...")) {
				static const SDL_DialogFileFilter kFilters[] = {
					{ "Altirra CompatDB", "atcompatdb" },
					{ "All Files", "*" },
				};
				SDL_ShowOpenFileDialog(CompatDBLoadCallback, nullptr, nullptr, kFilters, 2, nullptr, false);
			}
			if (ImGui::MenuItem("Save")) {
				if (g_compatDB.path.empty()) {
					static const SDL_DialogFileFilter kFilters[] = {
						{ "Altirra CompatDB", "atcompatdb" },
						{ "All Files", "*" },
					};
					SDL_ShowSaveFileDialog(CompatDBSaveCallback, nullptr, nullptr, kFilters, 2, nullptr);
				} else {
					try {
						ATSaveCompatEDB(g_compatDB.path.c_str(), g_compatDB.edb);
						g_compatDB.modified = false;
					} catch (const MyError &e) {
						LOG_ERROR("UI", "Save failed: %s", e.c_str());
					}
				}
			}
			if (ImGui::MenuItem("Save As...")) {
				static const SDL_DialogFileFilter kFilters[] = {
					{ "Altirra CompatDB", "atcompatdb" },
					{ "All Files", "*" },
				};
				SDL_ShowSaveFileDialog(CompatDBSaveCallback, nullptr, nullptr, kFilters, 2, nullptr);
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Compile...")) {
				if (!g_compatDB.compilePath.empty()) {
					try {
						vdblock<char> buf;
						ATCompileCompatEDB(buf, g_compatDB.edb);
						VDFile f(g_compatDB.compilePath.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
						f.write(buf.data(), (long)buf.size());
						f.close();
						g_compatDB.needsCompile = false;
						ATCompatReloadExtDatabase();
					} catch (const MyError &e) {
						LOG_ERROR("UI", "Compile failed: %s", e.c_str());
					}
				} else {
					static const SDL_DialogFileFilter kFilters[] = {
						{ "Compat Engine", "atcpengine" },
						{ "All Files", "*" },
					};
					SDL_ShowSaveFileDialog(CompatDBCompileCallback, nullptr, nullptr, kFilters, 2, nullptr);
				}
			}
			if (ImGui::MenuItem("Compile To...")) {
				static const SDL_DialogFileFilter kFilters[] = {
					{ "Compat Engine", "atcpengine" },
					{ "All Files", "*" },
				};
				SDL_ShowSaveFileDialog(CompatDBCompileCallback, nullptr, nullptr, kFilters, 2, nullptr);
			}
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}

	// Left pane: title list
	float leftW = ImGui::GetContentRegionAvail().x * 0.4f;

	ImGui::BeginChild("TitlePane", ImVec2(leftW, 0));

	ImGui::Text("Titles");
	ImGui::SetNextItemWidth(-1);
	bool searchChanged = ImGui::InputTextWithHint("##search", "Quick search...", g_compatDB.searchBuf, sizeof(g_compatDB.searchBuf));
	if (searchChanged)
		g_compatDB.RefreshTitles();

	// Title list
	if (ImGui::BeginListBox("##titles", ImVec2(-1, ImGui::GetContentRegionAvail().y - 30))) {
		for (int i = 0; i < (int)g_compatDB.displayedTitles.size(); ++i) {
			VDStringA name = VDTextWToU8(g_compatDB.displayedTitles[i]->mName);
			bool selected = (g_compatDB.selectedTitle == i);
			if (ImGui::Selectable(name.c_str(), selected)) {
				g_compatDB.selectedTitle = i;
				g_compatDB.selectedAlias = -1;
				g_compatDB.selectedTag = -1;
			}
		}
		ImGui::EndListBox();
	}

	// Add/Delete title buttons
	if (ImGui::Button("Add Title")) {
		auto *newTitle = g_compatDB.edb.mTitleTable.Create();
		newTitle->mName = L"(New Title)";
		g_compatDB.SetModified();
		g_compatDB.RefreshTitles();
		// Select the new title
		for (int i = 0; i < (int)g_compatDB.displayedTitles.size(); ++i) {
			if (g_compatDB.displayedTitles[i] == newTitle) {
				g_compatDB.selectedTitle = i;
				break;
			}
		}
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(g_compatDB.selectedTitle < 0);
	if (ImGui::Button("Delete Title") && g_compatDB.selectedTitle >= 0) {
		auto *title = g_compatDB.displayedTitles[g_compatDB.selectedTitle];
		g_compatDB.edb.mTitleTable.Destroy(title->mId);
		g_compatDB.SetModified();
		g_compatDB.RefreshTitles();
	}
	ImGui::EndDisabled();

	ImGui::EndChild();

	ImGui::SameLine();

	// Right pane: aliases and tags for selected title
	ImGui::BeginChild("DetailPane", ImVec2(0, 0));

	ATCompatEDBTitle *selTitle = nullptr;
	if (g_compatDB.selectedTitle >= 0 && g_compatDB.selectedTitle < (int)g_compatDB.displayedTitles.size())
		selTitle = g_compatDB.displayedTitles[g_compatDB.selectedTitle];

	static ATCompatEDBTitle *s_lastEditTitle = nullptr;

	if (selTitle) {
		// Title name editing — only sync buffer when selection changes
		if (selTitle != s_lastEditTitle) {
			s_lastEditTitle = selTitle;
			VDStringA nameU8 = VDTextWToU8(selTitle->mName);
			strncpy(g_compatDB.titleNameBuf, nameU8.c_str(), sizeof(g_compatDB.titleNameBuf) - 1);
			g_compatDB.titleNameBuf[sizeof(g_compatDB.titleNameBuf) - 1] = 0;
		}
		ImGui::Text("Title:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1);
		if (ImGui::InputText("##titlename", g_compatDB.titleNameBuf, sizeof(g_compatDB.titleNameBuf),
			ImGuiInputTextFlags_EnterReturnsTrue)) {
			uint32 titleId = selTitle->mId;
			selTitle->mName = VDTextU8ToW(g_compatDB.titleNameBuf, -1);
			g_compatDB.SetModified();
			g_compatDB.RefreshTitles();
			// Re-select the renamed title by ID (it may have moved due to re-sort)
			for (int i = 0; i < (int)g_compatDB.displayedTitles.size(); ++i) {
				if (g_compatDB.displayedTitles[i]->mId == titleId) {
					g_compatDB.selectedTitle = i;
					break;
				}
			}
			s_lastEditTitle = nullptr;  // force re-sync after refresh
		}

		ImGui::Spacing();

		// Aliases section
		ImGui::Text("Aliases (%d):", (int)selTitle->mAliases.size());

		float halfH = (ImGui::GetContentRegionAvail().y - 30) * 0.5f;
		if (ImGui::BeginListBox("##aliases", ImVec2(-1, halfH))) {
			for (int i = 0; i < (int)selTitle->mAliases.size(); ++i) {
				VDStringW displayStr;
				for (const auto &rule : selTitle->mAliases[i].mRules) {
					if (!displayStr.empty()) displayStr += L", ";
					displayStr += rule.ToDisplayString();
				}
				VDStringA u8 = VDTextWToU8(displayStr);
				bool selected = (g_compatDB.selectedAlias == i);
				ImGui::PushID(i);
				if (ImGui::Selectable(u8.c_str(), selected))
					g_compatDB.selectedAlias = i;
				ImGui::PopID();
			}
			ImGui::EndListBox();
		}

		ImGui::BeginDisabled(g_compatDB.selectedAlias < 0);
		if (ImGui::Button("Edit Alias...") && g_compatDB.selectedAlias >= 0 &&
			g_compatDB.selectedAlias < (int)selTitle->mAliases.size()) {
			ImGui::OpenPopup("Edit Alias");
		}
		ImGui::SameLine();
		if (ImGui::Button("Delete Alias") && g_compatDB.selectedAlias >= 0 &&
			g_compatDB.selectedAlias < (int)selTitle->mAliases.size()) {
			selTitle->mAliases.erase(selTitle->mAliases.begin() + g_compatDB.selectedAlias);
			g_compatDB.SetModified();
			g_compatDB.selectedAlias = -1;
		}
		ImGui::EndDisabled();

		ImGui::SameLine();

		// Add alias from currently loaded media
		if (ImGui::Button("Add From Loaded Media")) {
			vdvector<ATCompatEDBSourcedAliasRule> srcRules;
			VDStringW sourceName;

			for (int i = 0; i < 15; ++i) {
				auto *pImage = sim.GetDiskInterface(i).GetDiskImage();
				if (pImage) {
					sourceName.sprintf(L"D%u:", i + 1);
					ATCompatAddSourcedRulesForImage(srcRules, pImage, sourceName.c_str());
				}
			}
			for (int i = 0; i < 2; ++i) {
				auto *cart = sim.GetCartridge(i);
				if (cart)
					ATCompatAddSourcedRulesForImage(srcRules, cart->GetImage(), L"Cart");
			}
			ATCompatAddSourcedRulesForImage(srcRules, sim.GetCassette().GetImage(), L"Tape");
			auto *pl = sim.GetProgramLoader();
			if (pl)
				ATCompatAddSourcedRulesForImage(srcRules, pl->GetCurrentImage(), L"Exe");

			if (!srcRules.empty()) {
				ATCompatEDBAlias alias;
				for (auto &sr : srcRules)
					alias.mRules.push_back(sr.mRule);
				selTitle->mAliases.push_back_as(std::move(alias));
				g_compatDB.SetModified();
			}
		}

		// Edit Alias popup
		// Edit Alias popup statics (must be outside inner scope)
		static vdvector<ATCompatEDBSourcedAliasRule> s_availRules;
		static bool s_availLoaded = false;

		if (ImGui::BeginPopupModal("Edit Alias", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
			if (g_compatDB.selectedAlias >= 0 &&
				g_compatDB.selectedAlias < (int)selTitle->mAliases.size()) {
				auto &alias = selTitle->mAliases[g_compatDB.selectedAlias];

				ImGui::Text("Rules in this alias:");

				// Available rules from loaded media
				if (!s_availLoaded) {
					s_availRules.clear();
					VDStringW sourceName;
					for (int d = 0; d < 15; ++d) {
						auto *pImage = sim.GetDiskInterface(d).GetDiskImage();
						if (pImage) {
							sourceName.sprintf(L"D%u:", d + 1);
							ATCompatAddSourcedRulesForImage(s_availRules, pImage, sourceName.c_str());
						}
					}
					for (int c = 0; c < 2; ++c) {
						auto *cart = sim.GetCartridge(c);
						if (cart)
							ATCompatAddSourcedRulesForImage(s_availRules, cart->GetImage(), L"Cart");
					}
					ATCompatAddSourcedRulesForImage(s_availRules, sim.GetCassette().GetImage(), L"Tape");
					auto *pl = sim.GetProgramLoader();
					if (pl)
						ATCompatAddSourcedRulesForImage(s_availRules, pl->GetCurrentImage(), L"Exe");
					s_availLoaded = true;
				}

				// Current rules
				static int s_activeRuleSel = -1;
				ImGui::Text("Active rules:");
				if (ImGui::BeginListBox("##activerules", ImVec2(400, 120))) {
					for (int r = 0; r < (int)alias.mRules.size(); ++r) {
						VDStringA u8 = VDTextWToU8(alias.mRules[r].ToDisplayString());
						ImGui::PushID(r);
						if (ImGui::Selectable(u8.c_str(), s_activeRuleSel == r))
							s_activeRuleSel = r;
						ImGui::PopID();
					}
					ImGui::EndListBox();
				}

				if (ImGui::Button("Remove Selected Rule") && s_activeRuleSel >= 0 &&
					s_activeRuleSel < (int)alias.mRules.size()) {
					alias.mRules.erase(alias.mRules.begin() + s_activeRuleSel);
					g_compatDB.SetModified();
					s_activeRuleSel = -1;
				}

				ImGui::Spacing();
				ImGui::Text("Available rules from loaded media:");
				if (ImGui::BeginListBox("##availrules", ImVec2(400, 120))) {
					for (int r = 0; r < (int)s_availRules.size(); ++r) {
						VDStringA u8 = VDTextWToU8(s_availRules[r].ToDisplayString());
						ImGui::PushID(r + 1000);
						if (ImGui::Selectable(u8.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
							if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
								// Add to alias if not already present
								bool found = false;
								for (auto &existing : alias.mRules) {
									if (existing == s_availRules[r].mRule) { found = true; break; }
								}
								if (!found) {
									alias.mRules.push_back(s_availRules[r].mRule);
									g_compatDB.SetModified();
								}
							}
						}
						ImGui::PopID();
					}
					ImGui::EndListBox();
				}

				ImGui::TextDisabled("Double-click to add a rule to the alias.");
			}

			ImGui::Spacing();
			if (ImGui::Button("Close", ImVec2(80, 0))) {
				s_availLoaded = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		ImGui::Spacing();

		// Tags section
		ImGui::Text("Tags (%d):", (int)selTitle->mTags.size());

		if (ImGui::BeginListBox("##tags", ImVec2(-1, ImGui::GetContentRegionAvail().y - 30))) {
			for (int i = 0; i < (int)selTitle->mTags.size(); ++i) {
				VDStringW displayName;
				auto knownTag = ATCompatGetKnownTagByKey(selTitle->mTags[i].c_str());
				if (knownTag)
					displayName = ATUICompatGetKnownTagDisplayName(knownTag);
				else
					displayName.sprintf(L"[%hs]", selTitle->mTags[i].c_str());

				VDStringA u8 = VDTextWToU8(displayName);
				bool selected = (g_compatDB.selectedTag == i);
				ImGui::PushID(i + 10000);
				if (ImGui::Selectable(u8.c_str(), selected))
					g_compatDB.selectedTag = i;
				ImGui::PopID();
			}
			ImGui::EndListBox();
		}

		if (ImGui::Button("Add Tag"))
			g_compatDB.showTagPicker = true;

		ImGui::SameLine();
		ImGui::BeginDisabled(g_compatDB.selectedTag < 0);
		if (ImGui::Button("Delete Tag") && g_compatDB.selectedTag >= 0 &&
			g_compatDB.selectedTag < (int)selTitle->mTags.size()) {
			selTitle->mTags.erase(selTitle->mTags.begin() + g_compatDB.selectedTag);
			g_compatDB.SetModified();
			g_compatDB.selectedTag = -1;
		}
		ImGui::EndDisabled();

	} else {
		ImGui::TextDisabled("Select a title to view details.");
	}

	ImGui::EndChild();

	// Tag picker popup
	if (g_compatDB.showTagPicker) {
		ImGui::OpenPopup("Select Tag");
		g_compatDB.showTagPicker = false;
	}

	if (ImGui::BeginPopupModal("Select Tag", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		static char tagFilter[64] = {};
		ImGui::SetNextItemWidth(300);
		ImGui::InputTextWithHint("##tagfilter", "Filter tags...", tagFilter, sizeof(tagFilter));

		if (ImGui::BeginListBox("##taglist", ImVec2(300, 300))) {
			for (int i = 1; i < kATCompatKnownTagCount; ++i) {
				ATCompatKnownTag tag = (ATCompatKnownTag)i;
				const wchar_t *wname = ATUICompatGetKnownTagDisplayName(tag);
				VDStringA name = VDTextWToU8(VDStringW(wname));

				if (tagFilter[0] && !strcasestr(name.c_str(), tagFilter))
					continue;

				if (ImGui::Selectable(name.c_str(), tag == g_compatDB.lastSelectedTag, ImGuiSelectableFlags_AllowDoubleClick)) {
					g_compatDB.lastSelectedTag = tag;
					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && selTitle) {
						const char *key = ATCompatGetKeyForKnownTag(tag);

						// Ensure tag exists in EDB
						auto r = g_compatDB.edb.mTagTable.insert_as(key);
						if (r.second) {
							r.first->second.mKey = key;
							r.first->second.mDisplayName = wname;
						}

						// Add to title if not already present
						VDStringA keyStr(key);
						if (std::find(selTitle->mTags.begin(), selTitle->mTags.end(), keyStr) == selTitle->mTags.end()) {
							selTitle->mTags.push_back_as(keyStr);
							g_compatDB.SetModified();
						}

						tagFilter[0] = 0;
						ImGui::CloseCurrentPopup();
					}
				}
			}
			ImGui::EndListBox();
		}

		if (ImGui::Button("Cancel")) {
			tagFilter[0] = 0;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	// Unsaved changes confirmation popup
	if (g_compatDB.showDiscardConfirm) {
		ImGui::OpenPopup("Unsaved Changes");
		g_compatDB.showDiscardConfirm = false;
	}
	if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::TextUnformatted("Unsaved changes will be lost. Are you sure?");
		ImGui::Spacing();
		if (ImGui::Button("Discard", ImVec2(120, 0))) {
			ImGui::CloseCurrentPopup();
			g_compatDB.Reset();
			state.showCompatDB = false;
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120, 0)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	// Compile-before-exit confirmation for external databases
	if (g_compatDB.showCompileConfirm) {
		ImGui::OpenPopup("Compile Before Exit?");
		g_compatDB.showCompileConfirm = false;
	}
	if (ImGui::BeginPopupModal("Compile Before Exit?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::TextWrapped("The external compatibility database still needs to be compiled before changes will take effect.\nCompile it before exiting?");
		ImGui::Spacing();
		if (ImGui::Button("Compile & Close", ImVec2(140, 0))) {
			ImGui::CloseCurrentPopup();
			if (!g_compatDB.compilePath.empty()) {
				try {
					vdblock<char> buf;
					ATCompileCompatEDB(buf, g_compatDB.edb);
					VDFile f(g_compatDB.compilePath.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
					f.write(buf.data(), (long)buf.size());
					f.close();
					ATCompatReloadExtDatabase();
				} catch (const MyError &e) {
					LOG_ERROR("UI", "Compile failed: %s", e.c_str());
				}
			}
			g_compatDB.Reset();
			state.showCompatDB = false;
		}
		ImGui::SameLine();
		if (ImGui::Button("Close Without Compile", ImVec2(180, 0))) {
			ImGui::CloseCurrentPopup();
			g_compatDB.Reset();
			state.showCompatDB = false;
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(80, 0)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	ImGui::End();
}
