//	AltirraSDL - Dear ImGui debugger source code pane
//	Replaces Win32 ATSourceWindow (uidbgsource.cpp).
//	Displays source code files with line numbers, breakpoint indicators,
//	PC highlighting, and active-line breakpoint commands.
//	Multiple source windows can be open simultaneously using dynamically
//	assigned pane IDs starting at kATUIPaneId_Source.

#include <stdafx.h>
#include <algorithm>
#include <mutex>
#include <string>
#include <vector>
#include <map>
#include <imgui.h>
#include <SDL3/SDL.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/filewatcher.h>
#include <vd2/system/error.h>
#include <at/atdebugger/symbols.h>
#include "../core/ui_main.h"
#include "ui_file_dialog_sdl3.h"
#include "ui_debugger.h"
#include "console.h"
#include "debugger.h"
#include "simulator.h"

extern ATSimulator g_sim;
extern SDL_Window *g_pWindow;

// Forward declaration — defined later in this file
IATSourceWindow *ATImGuiOpenSourceWindow(const wchar_t *path);
IATSourceWindow *ATImGuiOpenSourceWindow(const ATDebuggerSourceFileInfo& sourceFileInfo, bool searchPaths);
class ATImGuiSourcePaneImpl;
static bool SourceStepWithRanges(ATImGuiSourcePaneImpl *srcPane, bool stepOver);
static ATImGuiSourcePaneImpl *OpenResolvedSourceWindow(
	const ATDebuggerSourceFileInfo& sourceFileInfo,
	const VDStringW& resolvedPath,
	bool useAlias);
static bool OpenSourceWindowAndFocusLine(
	const ATDebuggerSourceFileInfo& sourceFileInfo,
	int line);

// =========================================================================
// Source pane — displays loaded source files for debugging
// =========================================================================

class ATImGuiSourcePaneImpl final : public ATImGuiDebuggerPane,
                                    public IVDFileWatcherCallback,
                                    public IATSourceWindow {
public:
	ATImGuiSourcePaneImpl(uint32 paneId, const wchar_t *path, const wchar_t *alias = nullptr);
	~ATImGuiSourcePaneImpl();

	bool Render() override;
	bool OnPaneCommand(ATUIPaneCommandId id) override;
	void OnFrame() override;
	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) override;
	void OnDebuggerEvent(ATDebugEvent eventId) override;
	bool OnFileUpdated(const wchar_t *path) override;

	// IATSourceWindow
	const wchar_t *GetFullPath() const override { return mFullPath.c_str(); }
	const wchar_t *GetPath() const override { return mPath.c_str(); }
	const wchar_t *GetPathAlias() const override { return mPathAlias.empty() ? nullptr : mPathAlias.c_str(); }
	void FocusOnLine(int line) override;
	void ActivateLine(int line) override;
	VDStringW ReadLine(int lineIndex) override;

	// Expose address-to-line map for source-level step range computation
	const std::map<uint32, int>& GetAddressToLineMap() const { return mAddressToLine; }
	int GetLineCountForTest() const { return (int)mLines.size(); }
	int GetSelectedLineForTest() const { return mSelectedLine; }
	int GetLineForAddress(uint32 addr) const;
	sint32 GetAddressForLine(int lineIndex) const;
	const char *GetLineForTest(int lineIndex) const;
	void ReloadFromWatcherForTest(const wchar_t *path);

private:
	bool LoadFile();
	void BindToSymbols();
	void MergeSourceMappings();
	void UpdatePCLine();
	void PollFileWatcher();
	void ReloadFileFromWatcher();
	bool ToggleSelectedBreakpoint();

	VDStringW mPath;
	VDStringW mFullPath;
	VDStringW mPathAlias;
	VDStringA mPathU8;
	VDStringA mModulePath;

	// Source text lines
	std::vector<VDStringA> mLines;
	bool mbLoaded = false;
	bool mbLoadError = false;
	bool mbReloadRequested = false;

	// Symbol binding
	uint32 mModuleId = 0;
	uint16 mFileId = 0;
	bool mbBound = false;

	// Address<->line mappings
	std::map<uint32, int> mAddressToLine;  // address -> 0-based line index
	std::map<int, uint32> mLineToAddress;  // 0-based line index -> address
	std::map<uint32, int> mSymbolAddressToLine;
	std::map<int, uint32> mSymbolLineToAddress;
	std::map<uint32, int> mFileAddressToLine;
	std::map<int, uint32> mFileLineToAddress;

	// PC highlighting
	int mPCLine = -1;
	int mFramePCLine = -1;

	// Scroll request
	int mScrollToLine = -1;
	bool mbNeedsSymbolRebind = true;
	int mSelectedLine = -1;

	// Context menu
	int mContextLine = -1;
	bool mbContextMenuRequested = false;
	VDFileWatcher mFileWatcher;
};

ATImGuiSourcePaneImpl::ATImGuiSourcePaneImpl(uint32 paneId, const wchar_t *path, const wchar_t *alias)
	: ATImGuiDebuggerPane(paneId, "Source")
	, mPath(path)
	, mFullPath(VDGetFullPath(path))
	, mPathAlias(alias ? alias : L"")
{
	mPathU8 = VDTextWToU8(mPath);

	// Set title to filename only, with ImGui unique ID suffix to handle
	// multiple files with the same filename from different directories
	VDStringW filename = VDFileSplitPathRight(mPath);
	VDStringA titleU8 = VDTextWToU8(filename);
	VDStringA uniqueTitle;
	uniqueTitle.sprintf("%s###src_%u", titleU8.c_str(), paneId);
	mTitle = uniqueTitle;

	LoadFile();
}

ATImGuiSourcePaneImpl::~ATImGuiSourcePaneImpl() {
	mFileWatcher.Shutdown();
}

bool ATImGuiSourcePaneImpl::LoadFile() {
	mbLoaded = false;
	mbLoadError = false;
	mbReloadRequested = false;
	mLines.clear();
	mFileAddressToLine.clear();
	mFileLineToAddress.clear();
	BindToSymbols();
	mFileWatcher.Shutdown();

	try {
		VDTextInputFile file(mPath.c_str());

		uint32 lineNo = 0;
		bool listingMode = false;
		while (const char *line = file.GetNextLine()) {
			mLines.push_back(VDStringA(line));

			if (listingMode) {
				char space0;
				int origline;
				int address;
				char dummy;
				char space1;
				char space2;
				char space3;
				char space4;
				int op;

				bool valid = false;
				if (7 == sscanf(line, "%c%d %4x%c%c%2x%c", &space0, &origline, &address, &space1, &space2, &op, &space3)
					&& space0 == ' '
					&& space1 == ' '
					&& space2 == ' '
					&& (space3 == ' ' || space3 == '\t'))
				{
					valid = true;
				} else if (8 == sscanf(line, "%6x%c%c%c%c%c%2x%c", &address, &space0, &space1, &dummy, &space2, &space3, &op, &space4)
					&& space0 == ' '
					&& space1 == ' '
					&& space2 == ' '
					&& space3 == ' '
					&& (space4 == ' ' || space4 == '\t')
					&& isdigit((unsigned char)dummy))
				{
					valid = true;
				} else if (6 == sscanf(line, "%6d%c%4x%c%2x%c", &origline, &space0, &address, &space1, &op, &space2)
					&& space0 == ' '
					&& space1 == ' '
					&& (space2 == ' ' || space2 == '\t'))
				{
					valid = true;
				}

				if (valid) {
					mFileAddressToLine.insert(std::map<uint32, int>::value_type((uint32)address, (int)lineNo));
					mFileLineToAddress.insert(std::map<int, uint32>::value_type((int)lineNo, (uint32)address));
				}
			} else if (!mModuleId) {
				if (!lineNo && !strncmp(line, "mads ", 5))
					listingMode = true;
			}

			++lineNo;
		}

		MergeSourceMappings();
		if (mModulePath.empty() && !listingMode)
			mModulePath = VDTextWToA(VDFileSplitPathRight(mPath));

		mFileWatcher.Init(mPath.c_str(), this);
		mbLoaded = true;
	} catch (const MyError&) {
		mbLoadError = true;
		return false;
	}

	return true;
}

void ATImGuiSourcePaneImpl::BindToSymbols() {
	mbNeedsSymbolRebind = false;
	mSymbolAddressToLine.clear();
	mSymbolLineToAddress.clear();
	mModulePath.clear();
	mModuleId = 0;
	mFileId = 0;
	mbBound = false;

	IATDebuggerSymbolLookup *lookup = ATGetDebuggerSymbolLookup();
	if (!lookup) {
		MergeSourceMappings();
		return;
	}

	// Try to look up this file in the debugger's symbol tables. If the
	// user mapped a missing symbol path to another on-disk file, the alias
	// is the original symbol path and must be used for debugger binding.
	if (!lookup->LookupFile(mPath.c_str(), mModuleId, mFileId)
		&& (mPathAlias.empty()
			|| !lookup->LookupFile(mPathAlias.c_str(), mModuleId, mFileId))) {
		MergeSourceMappings();
		return;
	}

	mbBound = true;

	ATDebuggerSourceFileInfo sourceFileInfo;
	if (lookup->GetSourceFilePath(mModuleId, mFileId, sourceFileInfo))
		mModulePath = VDTextWToA(sourceFileInfo.mSourcePath);

	// Get all source line mappings for this file
	vdfastvector<ATSourceLineInfo> lines;
	lookup->GetLinesForFile(mModuleId, mFileId, lines);

	for (const auto& li : lines) {
		int lineIdx = (int)li.mLine - 1;  // convert 1-based to 0-based
		if (lineIdx >= 0) {
			mSymbolAddressToLine.insert(std::map<uint32, int>::value_type(li.mOffset, lineIdx));
			mSymbolLineToAddress.insert(std::map<int, uint32>::value_type(lineIdx, li.mOffset));
		}
	}

	MergeSourceMappings();
}

void ATImGuiSourcePaneImpl::MergeSourceMappings() {
	mAddressToLine.clear();
	mLineToAddress.clear();

	for (const auto& addressToLine : mSymbolAddressToLine)
		mAddressToLine.insert(addressToLine);

	for (const auto& addressToLine : mFileAddressToLine)
		mAddressToLine.insert(addressToLine);

	for (const auto& lineToAddress : mSymbolLineToAddress)
		mLineToAddress.insert(lineToAddress);

	for (const auto& lineToAddress : mFileLineToAddress)
		mLineToAddress.insert(lineToAddress);

	mbBound = !mAddressToLine.empty();
}

void ATImGuiSourcePaneImpl::UpdatePCLine() {
	mPCLine = -1;
	mFramePCLine = -1;

	if (!mbStateValid || !mbBound || mLastState.mbRunning)
		return;

	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	// Look up current PC in our address mappings
	mPCLine = GetLineForAddress(mLastState.mInsnPC);

	// Frame PC
	mFramePCLine = GetLineForAddress(mLastState.mFrameExtPC);
}

void ATImGuiSourcePaneImpl::FocusOnLine(int line) {
	mScrollToLine = line;
	mSelectedLine = line;
	RequestFocus();
}

void ATImGuiSourcePaneImpl::ActivateLine(int line) {
	// ActivateLine is like FocusOnLine but also sets the pane visible
	SetVisible(true);
	mScrollToLine = line;
	mSelectedLine = line;
	RequestFocus();
}

VDStringW ATImGuiSourcePaneImpl::ReadLine(int lineIndex) {
	if (lineIndex >= 0 && lineIndex < (int)mLines.size()) {
		VDStringW result = VDTextU8ToW(VDStringSpanA(mLines[lineIndex]));
		result += L'\n';
		return result;
	}
	return VDStringW();
}

const char *ATImGuiSourcePaneImpl::GetLineForTest(int lineIndex) const {
	if (lineIndex >= 0 && lineIndex < (int)mLines.size())
		return mLines[lineIndex].c_str();

	return "";
}

int ATImGuiSourcePaneImpl::GetLineForAddress(uint32 addr) const {
	auto it = mAddressToLine.upper_bound(addr);
	if (it != mAddressToLine.begin()) {
		--it;

		if (addr - it->first < 64)
			return it->second;
	}

	return -1;
}

sint32 ATImGuiSourcePaneImpl::GetAddressForLine(int lineIndex) const {
	auto it = mLineToAddress.find(lineIndex);
	if (it == mLineToAddress.end())
		return -1;

	return (sint32)it->second;
}

void ATImGuiSourcePaneImpl::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	ATImGuiDebuggerPane::OnDebuggerSystemStateUpdate(state);
	if (!state.mbRunning) {
		UpdatePCLine();
		// Auto-scroll to PC line
		if (mPCLine >= 0)
			mScrollToLine = mPCLine;
		else if (mFramePCLine >= 0)
			mScrollToLine = mFramePCLine;
	}
}

void ATImGuiSourcePaneImpl::OnDebuggerEvent(ATDebugEvent eventId) {
	if (eventId == kATDebugEvent_SymbolsChanged)
		mbNeedsSymbolRebind = true;
	if (eventId == kATDebugEvent_BreakpointsChanged) {
		// Trigger re-render to update breakpoint indicators
	}
}

bool ATImGuiSourcePaneImpl::OnFileUpdated(const wchar_t *) {
	mbReloadRequested = true;
	return true;
}

void ATImGuiSourcePaneImpl::ReloadFromWatcherForTest(const wchar_t *path) {
	OnFileUpdated(path);
	PollFileWatcher();
}

void ATImGuiSourcePaneImpl::PollFileWatcher() {
	if (mFileWatcher.IsActive())
		mFileWatcher.Wait(0);

	if (mbReloadRequested)
		ReloadFileFromWatcher();
}

void ATImGuiSourcePaneImpl::ReloadFileFromWatcher() {
	const int selectedLine = mSelectedLine;
	const int scrollToLine = mScrollToLine;
	const VDStringW alias = mPathAlias;

	LoadFile();

	if (!alias.empty())
		mPathAlias = alias;

	if (selectedLine >= 0 && selectedLine < (int)mLines.size())
		mSelectedLine = selectedLine;
	else
		mSelectedLine = -1;

	if (scrollToLine >= 0)
		mScrollToLine = scrollToLine;
	else if (mSelectedLine >= 0)
		mScrollToLine = mSelectedLine;
}

void ATImGuiSourcePaneImpl::OnFrame() {
	PollFileWatcher();
}

bool ATImGuiSourcePaneImpl::OnPaneCommand(ATUIPaneCommandId id) {
	switch(id) {
		case kATUIPaneCommandId_DebugRun:
			if (IATDebugger *dbg = ATGetDebugger())
				dbg->Run(kATDebugSrcMode_Source);
			return true;
		case kATUIPaneCommandId_DebugStepInto:
			return SourceStepWithRanges(this, false);
		case kATUIPaneCommandId_DebugStepOver:
			return SourceStepWithRanges(this, true);
		case kATUIPaneCommandId_DebugStepOut:
			try {
				if (IATDebugger *dbg = ATGetDebugger())
					dbg->StepOut(kATDebugSrcMode_Source);
			} catch(const MyError& e) {
				ATConsolePrintf("%s\n", e.c_str());
			}
			return true;
		case kATUIPaneCommandId_DebugToggleBreakpoint:
			return ToggleSelectedBreakpoint();
		default:
			break;
	}

	return false;
}

bool ATImGuiSourcePaneImpl::ToggleSelectedBreakpoint() {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return true;

	int line = mSelectedLine;
	if (line < 0)
		line = mFramePCLine >= 0 ? mFramePCLine : mPCLine;

	if (line < 0 || (size_t)line >= mLines.size())
		return true;

	if (!mModulePath.empty()) {
		dbg->ToggleSourceBreakpoint(mModulePath.c_str(), (uint32)(line + 1));
	} else {
		sint32 addr = GetAddressForLine(line);
		if (addr >= 0)
			dbg->ToggleBreakpoint((uint16)addr);
	}

	return true;
}

bool ATImGuiSourcePaneImpl::Render() {
	bool open = true;

	if (mbFocusRequested) {
		ImGui::SetNextWindowFocus();
		mbFocusRequested = false;
	}

	ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(mTitle.c_str(), &open)) {
		mbHasFocus = false;
		ImGui::End();
		return open;
	}
	mbHasFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

	if (mbNeedsSymbolRebind)
		BindToSymbols();

	if (mbLoadError) {
		ImGui::TextDisabled("Error loading: %s", mPathU8.c_str());
		ImGui::End();
		return open;
	}

	if (!mbLoaded || mLines.empty()) {
		ImGui::TextDisabled("(empty file)");
		ImGui::End();
		return open;
	}

	IATDebugger *dbg = ATGetDebugger();
	const float lineH = ImGui::GetTextLineHeightWithSpacing();
	const float charW = ImGui::CalcTextSize("0").x;

	if (ImGui::BeginChild("SourceView", ImVec2(0, 0), ImGuiChildFlags_None,
			ImGuiWindowFlags_HorizontalScrollbar)) {

		ImGuiListClipper clipper;
		clipper.Begin((int)mLines.size());

		while (clipper.Step()) {
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
				ImVec2 pos = ImGui::GetCursorScreenPos();
				float width = ImGui::GetContentRegionAvail().x;

				// Background highlight for PC/frame PC lines
				if (i == mPCLine) {
					ImGui::GetWindowDrawList()->AddRectFilled(
						pos, ImVec2(pos.x + width, pos.y + lineH),
						IM_COL32(128, 128, 0, 80));
				} else if (i == mFramePCLine) {
					ImGui::GetWindowDrawList()->AddRectFilled(
						pos, ImVec2(pos.x + width, pos.y + lineH),
						IM_COL32(0, 128, 128, 80));
				}

				// Check if this line has a breakpoint
				bool hasBP = false;
				auto lineAddrIt = mLineToAddress.find(i);
				if (lineAddrIt != mLineToAddress.end() && dbg) {
					hasBP = dbg->IsBreakpointAtPC(lineAddrIt->second);
				}

				// Breakpoint indicator (red bar)
				if (hasBP) {
					ImGui::GetWindowDrawList()->AddRectFilled(
						pos, ImVec2(pos.x + 8, pos.y + lineH),
						IM_COL32(255, 80, 80, 200));
				}

				// PC arrow
				if (i == mPCLine) {
					ImGui::TextColored(ATUIColorWarningText(), ">");
					ImGui::SameLine(0, 0);
				} else if (i == mFramePCLine) {
					ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), ">");
					ImGui::SameLine(0, 0);
				} else {
					ImGui::TextUnformatted(" ");
					ImGui::SameLine(0, 0);
				}

				// Line number (1-based)
				ImGui::PushID(i);
				bool hasAddr = (mLineToAddress.find(i) != mLineToAddress.end());
				if (hasAddr)
					ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%5d", i + 1);
				else
					ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "%5d", i + 1);
				ImGui::SameLine(0, charW);

				// Click selects the active line; breakpoint commands operate on
				// the selected line, matching native text editor behavior.
				if (ImGui::Selectable(mLines[i].c_str(), i == mSelectedLine,
						ImGuiSelectableFlags_AllowOverlap)) {
					mSelectedLine = i;
				}
				if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
					mContextLine = i;
					mSelectedLine = i;
					mbContextMenuRequested = true;
				}
				ImGui::PopID();
			}
		}

		// Handle scroll request
		if (mScrollToLine >= 0) {
			float targetY = mScrollToLine * lineH;
			float visibleH = ImGui::GetWindowHeight();
			ImGui::SetScrollY(targetY - visibleH * 0.3f);
			mScrollToLine = -1;
		}

		// Context menu — rendered outside the clipper loop so it persists
		// even if the right-clicked line scrolls out of view
		if (mbContextMenuRequested) {
			ImGui::OpenPopup("SrcCtx");
			mbContextMenuRequested = false;
		}
		if (ImGui::BeginPopup("SrcCtx")) {
			if (dbg && mContextLine >= 0) {
				auto addrIt = mLineToAddress.find(mContextLine);
				bool hasAddr = (addrIt != mLineToAddress.end());

				// Show Next Statement: scroll to current PC in source
				// Matches Windows ID_CONTEXT_SHOWNEXTSTATEMENT — falls through
				// to Go to Disassembly if no source line found
				if (ImGui::MenuItem("Show Next Statement")) {
					IATDebuggerSymbolLookup *lookup = ATGetDebuggerSymbolLookup();
					bool handled = false;
					if (lookup) {
						uint32 moduleId;
						ATSourceLineInfo lineInfo;
						if (lookup->LookupLine(dbg->GetFramePC(), false, moduleId, lineInfo)) {
							ATDebuggerSourceFileInfo sourceFileInfo;
							if (lookup->GetSourceFilePath(moduleId, lineInfo.mFileId, sourceFileInfo) && lineInfo.mLine) {
								handled = OpenSourceWindowAndFocusLine(
									sourceFileInfo,
									(int)lineInfo.mLine - 1);
							}
						}
					}
					// Fall through to disassembly if source not found
					if (!handled && hasAddr) {
						ATUIDebuggerSetDisassemblyPosition(addrIt->second);
					}
				}

				// Go to Disassembly: matches Windows ID_CONTEXT_GOTODISASSEMBLY
				if (ImGui::MenuItem("Go to Disassembly", nullptr, false, hasAddr)) {
					ATUIDebuggerSetDisassemblyPosition(addrIt->second);
				}

				ImGui::Separator();

				// Set Next Statement: matches Windows which uses SetPC
				if (ImGui::MenuItem("Set Next Statement", nullptr, false, hasAddr)) {
					dbg->SetPC((uint16)(addrIt->second & 0xFFFF));
				}

				ImGui::Separator();

				if (ImGui::MenuItem("Toggle Breakpoint", nullptr, false, hasAddr)) {
					if (!mModulePath.empty()) {
						dbg->ToggleSourceBreakpoint(mModulePath.c_str(), (uint32)(mContextLine + 1));
					} else {
						sint32 addr = GetAddressForLine(mContextLine);
						if (addr >= 0)
							dbg->ToggleBreakpoint((uint16)addr);
					}
				}

				ImGui::Separator();

				// Open In submenu: matches Windows IDR_SOURCE_CONTEXT_MENU
				// Always available regardless of address mapping
				if (ImGui::BeginMenu("Open In")) {
					bool hasFile = !mPath.empty();
					if (ImGui::MenuItem("File Explorer", nullptr, false, hasFile)) {
						VDStringW dir = VDFileSplitPathLeft(mPath);
						VDStringA dirU8 = VDTextWToU8(dir);
						VDStringA url;
						url.sprintf("file://%s", dirU8.c_str());
						SDL_OpenURL(url.c_str());
					}
					if (ImGui::MenuItem("Default Editor", nullptr, false, hasFile)) {
						VDStringA url;
						url.sprintf("file://%s", mPathU8.c_str());
						SDL_OpenURL(url.c_str());
					}
					ImGui::EndMenu();
				}
			}
			ImGui::EndPopup();
		}

		// Escape → focus Console (matches Windows pattern)
		if (ImGui::IsWindowFocused()
				&& !ImGui::GetIO().WantTextInput
				&& ImGui::IsKeyPressed(ImGuiKey_Escape))
			ATUIDebuggerFocusConsole();
	}
	ImGui::EndChild();

	ImGui::End();
	return open;
}

// =========================================================================
// Source pane management — tracks all open source windows
// =========================================================================

namespace {
	std::vector<ATImGuiSourcePaneImpl *> g_sourceWindows;
	uint32 g_nextSourcePaneId = kATUIPaneId_Source;
}

// =========================================================================
// Implementations of source window functions from console.h
// (these replace stubs in console_stubs.cpp)
// =========================================================================

// Called from ATUIDebuggerShutdown (ui_debugger.cpp) to avoid dangling pointers
void ATUIDebuggerClearSourceWindows() {
	g_sourceWindows.clear();
	g_nextSourcePaneId = kATUIPaneId_Source;
}

static ATImGuiSourcePaneImpl *FindSourceWindow(const wchar_t *path) {
	for (auto *w : g_sourceWindows) {
		if (VDFileIsPathEqual(path, w->GetPath()))
			return w;

		const wchar_t *alias = w->GetPathAlias();
		if (alias && VDFileIsPathEqual(path, alias))
			return w;
	}

	const wchar_t *basePath = VDFileSplitPath(path);
	for (auto *w : g_sourceWindows) {
		if (VDFileIsPathEqual(basePath, VDFileSplitPath(w->GetPath())))
			return w;

		const wchar_t *alias = w->GetPathAlias();
		if (alias && VDFileIsPathEqual(basePath, VDFileSplitPath(alias)))
			return w;
	}

	return nullptr;
}

static bool AddUniqueSourceSearchDir(std::vector<VDStringW>& dirs, const VDStringW& dir) {
	if (dir.empty())
		return false;

	for (const VDStringW& existingDir : dirs) {
		if (VDFileIsPathEqual(existingDir.c_str(), dir.c_str()))
			return false;
	}

	dirs.push_back(dir);
	return true;
}

static bool g_missingSourcePromptOverrideActive = false;
static VDStringW g_missingSourcePromptOverridePath;
static bool g_missingSourceDialogStarted = false;
static int g_missingSourceRequestedFocusLine = -1;

struct PendingMissingSourceResult {
	ATDebuggerSourceFileInfo mSourceFileInfo;
	VDStringW mSelectedPath;
	int mFocusLine = -1;
};

static std::mutex g_missingSourceDialogMutex;
static ATDebuggerSourceFileInfo g_missingSourceDialogInfo;
static int g_missingSourceDialogFocusLine = -1;
static uintptr_t g_missingSourceDialogGeneration = 0;
static std::vector<PendingMissingSourceResult> g_pendingMissingSourceResults;

static void SDLCALL MissingSourceDialogCallback(void *userdata,
	const char * const *filelist, int)
{
	if (!filelist || !filelist[0] || !*filelist[0])
		return;

	const uintptr_t generation = (uintptr_t)userdata;
	const VDStringW selectedPath = VDTextU8ToW(filelist[0], -1);

	std::lock_guard<std::mutex> lock(g_missingSourceDialogMutex);
	if (generation != g_missingSourceDialogGeneration)
		return;

	g_pendingMissingSourceResults.push_back({
		g_missingSourceDialogInfo,
		selectedPath,
		g_missingSourceDialogFocusLine
	});
}

static void PromptForMissingSourceFileAsync(
	const ATDebuggerSourceFileInfo& sourceFileInfo,
	int focusLine)
{
	g_missingSourceDialogStarted = true;

	const VDStringW baseNameW(
		VDFileSplitPathRightSpan(sourceFileInfo.mSourcePath));
	const VDStringW fallbackDirW = !sourceFileInfo.mModulePath.empty()
		? VDFileSplitPathLeftSpan(sourceFileInfo.mModulePath)
		: VDStringW();
	const VDStringA fallbackDirU8 = VDTextWToU8(fallbackDirW);

	uintptr_t generation = 0;
	{
		std::lock_guard<std::mutex> lock(g_missingSourceDialogMutex);
		g_missingSourceDialogInfo = sourceFileInfo;
		g_missingSourceDialogFocusLine = focusLine;
		generation = ++g_missingSourceDialogGeneration;
	}

	const VDStringA baseNameU8 = VDTextWToU8(baseNameW);
	const SDL_DialogFileFilter filters[] = {
		{ baseNameU8.empty() ? "Source Files" : baseNameU8.c_str(),
			"s;asm;src;lst;inc;txt" },
		{ "All files", "*" },
	};

	ATUIShowOpenFileDialog('src ', MissingSourceDialogCallback,
		(void *)generation,
		g_pWindow,
		filters,
		2,
		false,
		fallbackDirU8.empty() ? nullptr : fallbackDirU8.c_str());
}

static VDStringW ResolveSourcePath(const ATDebuggerSourceFileInfo& sourceFileInfo, bool searchPaths, bool& useAlias, bool& resolved) {
	useAlias = false;
	resolved = false;

	if (searchPaths) {
		std::vector<VDStringW> seenDirs;
		const VDStringW sourceName(VDFileSplitPathRightSpan(sourceFileInfo.mSourcePath));

		if (!sourceFileInfo.mModulePath.empty()) {
			const VDStringW moduleDir(VDFileSplitPathLeftSpan(sourceFileInfo.mModulePath));
			if (AddUniqueSourceSearchDir(seenDirs, moduleDir)) {
				VDStringW fn = VDMakePath(moduleDir.c_str(), sourceName.c_str());
				if (VDDoesPathExist(fn.c_str())) {
					useAlias = true;
					resolved = true;
					return fn;
				}
			}
		}

		for (IATSourceWindow *sw : g_sourceWindows) {
			if (!sw)
				continue;

			const VDStringW baseDir(VDFileSplitPathLeftSpan(VDStringSpanW(sw->GetFullPath())));
			if (AddUniqueSourceSearchDir(seenDirs, baseDir)) {
				VDStringW fn = VDMakePath(baseDir.c_str(), sourceName.c_str());
				if (VDDoesPathExist(fn.c_str())) {
					useAlias = true;
					resolved = true;
					return fn;
				}
			}
		}
	}

	if (VDDoesPathExist(sourceFileInfo.mSourcePath.c_str())) {
		resolved = true;
		return sourceFileInfo.mSourcePath;
	}

	if (g_missingSourcePromptOverrideActive) {
		VDStringW selectedPath = g_missingSourcePromptOverridePath;
		if (!selectedPath.empty()) {
			useAlias = !VDFileIsPathEqual(selectedPath.c_str(), sourceFileInfo.mSourcePath.c_str());
			resolved = true;
			return selectedPath;
		}
	} else if (SDL_IsMainThread()) {
		PromptForMissingSourceFileAsync(sourceFileInfo,
			g_missingSourceRequestedFocusLine);
	}

	return VDStringW();
}

// Find an already-open source window by path (does NOT create)
IATSourceWindow *ATImGuiFindSourceWindow(const wchar_t *path) {
	ATImGuiSourcePaneImpl *w = FindSourceWindow(path);
	return w ? static_cast<IATSourceWindow *>(w) : nullptr;
}

// Called from ATConsoleShowSource (debugger.cpp) to show a source line
bool ATImGuiConsoleShowSource(uint32 addr) {
	IATDebuggerSymbolLookup *lookup = ATGetDebuggerSymbolLookup();
	if (!lookup)
		return false;

	uint32 moduleId;
	ATSourceLineInfo lineInfo;
	if (!lookup->LookupLine(addr, false, moduleId, lineInfo))
		return false;

	ATDebuggerSourceFileInfo sourceFileInfo;
	if (!lookup->GetSourceFilePath(moduleId, lineInfo.mFileId, sourceFileInfo))
		return false;

	return OpenSourceWindowAndFocusLine(sourceFileInfo, (int)lineInfo.mLine - 1);
}

// Open a source file by path
IATSourceWindow *ATImGuiOpenSourceWindow(const wchar_t *path) {
	ATDebuggerSourceFileInfo sourceFileInfo;
	sourceFileInfo.mSourcePath = path;

	return ATImGuiOpenSourceWindow(sourceFileInfo, true);
}

static ATImGuiSourcePaneImpl *OpenResolvedSourceWindow(
	const ATDebuggerSourceFileInfo& sourceFileInfo,
	const VDStringW& resolvedPath,
	bool useAlias)
{
	const wchar_t *alias = useAlias ? sourceFileInfo.mSourcePath.c_str() : nullptr;

	ATImGuiSourcePaneImpl *w = FindSourceWindow(resolvedPath.c_str());
	if (w) {
		w->RequestFocus();
		w->SetVisible(true);
		return w;
	}

	uint32 paneId = g_nextSourcePaneId++;
	w = new ATImGuiSourcePaneImpl(paneId, resolvedPath.c_str(), alias);
	ATUIDebuggerRegisterPane(w);
	g_sourceWindows.push_back(w);
	w->RequestFocus();
	w->SetVisible(true);
	return w;
}

IATSourceWindow *ATImGuiOpenSourceWindow(const ATDebuggerSourceFileInfo& sourceFileInfo, bool searchPaths) {
	ATImGuiSourcePaneImpl *w = FindSourceWindow(sourceFileInfo.mSourcePath.c_str());
	if (!w) {
		bool useAlias = false;
		bool resolved = false;
		VDStringW resolvedPath = ResolveSourcePath(sourceFileInfo, searchPaths, useAlias, resolved);
		if (!resolved || resolvedPath.empty())
			return nullptr;

		w = OpenResolvedSourceWindow(sourceFileInfo, resolvedPath, useAlias);
	}
	w->RequestFocus();
	w->SetVisible(true);
	return static_cast<IATSourceWindow *>(w);
}

static bool OpenSourceWindowAndFocusLine(
	const ATDebuggerSourceFileInfo& sourceFileInfo,
	int line)
{
	g_missingSourceDialogStarted = false;
	g_missingSourceRequestedFocusLine = line;
	IATSourceWindow *sourceWindow = ATImGuiOpenSourceWindow(sourceFileInfo, true);
	g_missingSourceRequestedFocusLine = -1;

	auto *w = static_cast<ATImGuiSourcePaneImpl *>(sourceWindow);
	if (!w)
		return g_missingSourceDialogStarted;

	w->FocusOnLine(line);
	return true;
}

void ATUIDebuggerPollPendingSourceDialogs() {
	std::vector<PendingMissingSourceResult> pendingResults;
	{
		std::lock_guard<std::mutex> lock(g_missingSourceDialogMutex);
		pendingResults.swap(g_pendingMissingSourceResults);
	}

	for (const PendingMissingSourceResult& result : pendingResults) {
		if (result.mSelectedPath.empty())
			continue;

		const bool useAlias = !VDFileIsPathEqual(
			result.mSelectedPath.c_str(),
			result.mSourceFileInfo.mSourcePath.c_str());

		ATUIDebuggerOpen();
		ATImGuiSourcePaneImpl *w = OpenResolvedSourceWindow(result.mSourceFileInfo,
			result.mSelectedPath,
			useAlias);
		if (w && result.mFocusLine >= 0)
			w->FocusOnLine(result.mFocusLine);
	}
}

bool ATUIDebuggerOpenMissingSourceForTest(const char *symbolPathUtf8,
	const char *resolvedPathUtf8,
	VDStringW& outPath,
	VDStringW& outAlias)
{
	outPath.clear();
	outAlias.clear();

	if (!symbolPathUtf8 || !*symbolPathUtf8)
		return false;

	ATDebuggerSourceFileInfo sourceFileInfo {};
	sourceFileInfo.mSourcePath = VDTextU8ToW(symbolPathUtf8, -1);

	g_missingSourcePromptOverridePath = (resolvedPathUtf8 && *resolvedPathUtf8)
		? VDTextU8ToW(resolvedPathUtf8, -1)
		: VDStringW();
	g_missingSourcePromptOverrideActive = true;

	IATSourceWindow *sourceWindow = ATImGuiOpenSourceWindow(sourceFileInfo, true);

	g_missingSourcePromptOverrideActive = false;
	g_missingSourcePromptOverridePath.clear();

	if (!sourceWindow)
		return true;

	outPath = sourceWindow->GetPath();

	if (const wchar_t *alias = sourceWindow->GetPathAlias())
		outAlias = alias;

	return true;
}

bool ATUIDebuggerOpenSourceForTest(const char *utf8Path,
	uint32& outPaneId,
	int& outLineCount,
	VDStringW& outPath)
{
	outPaneId = 0;
	outLineCount = 0;
	outPath.clear();

	if (!utf8Path || !*utf8Path)
		return false;

	VDStringW path = VDTextU8ToW(utf8Path, -1);
	if (!VDDoesPathExist(path.c_str()))
		return true;

	ATUIDebuggerOpen();

	IATSourceWindow *sourceWindow = ATImGuiOpenSourceWindow(path.c_str());
	auto *w = static_cast<ATImGuiSourcePaneImpl *>(sourceWindow);
	if (!w)
		return true;

	outPaneId = w->GetPaneId();
	outLineCount = w->GetLineCountForTest();
	outPath = w->GetPath();
	return true;
}

bool ATUIDebuggerQuerySourceForTest(const char *utf8Path,
	uint32& outPaneId,
	int& outLineCount,
	int& outSelectedLine,
	VDStringW& outPath,
	VDStringA& outFirstLine,
	VDStringA& outLastLine)
{
	outPaneId = 0;
	outLineCount = 0;
	outSelectedLine = -1;
	outPath.clear();
	outFirstLine.clear();
	outLastLine.clear();

	if (!utf8Path || !*utf8Path)
		return false;

	VDStringW path = VDTextU8ToW(utf8Path, -1);
	ATImGuiSourcePaneImpl *w = FindSourceWindow(path.c_str());
	if (!w)
		return true;

	outPaneId = w->GetPaneId();
	outLineCount = w->GetLineCountForTest();
	outSelectedLine = w->GetSelectedLineForTest();
	outPath = w->GetPath();

	if (outLineCount > 0) {
		outFirstLine = w->GetLineForTest(0);
		outLastLine = w->GetLineForTest(outLineCount - 1);
	}

	return true;
}

bool ATUIDebuggerReloadSourceForTest(const char *utf8Path,
	int selectLine,
	uint32& outPaneId,
	int& outBeforeLineCount,
	int& outAfterLineCount,
	int& outBeforeSelectedLine,
	int& outAfterSelectedLine,
	VDStringA& outBeforeLastLine,
	VDStringA& outAfterLastLine)
{
	outPaneId = 0;
	outBeforeLineCount = 0;
	outAfterLineCount = 0;
	outBeforeSelectedLine = -1;
	outAfterSelectedLine = -1;
	outBeforeLastLine.clear();
	outAfterLastLine.clear();

	if (!utf8Path || !*utf8Path)
		return false;

	VDStringW path = VDTextU8ToW(utf8Path, -1);
	if (!VDDoesPathExist(path.c_str()))
		return true;

	ATUIDebuggerOpen();

	IATSourceWindow *sourceWindow = ATImGuiOpenSourceWindow(path.c_str());
	auto *w = static_cast<ATImGuiSourcePaneImpl *>(sourceWindow);
	if (!w)
		return true;

	outPaneId = w->GetPaneId();

	if (selectLine >= 0)
		w->FocusOnLine(selectLine);

	outBeforeLineCount = w->GetLineCountForTest();
	outBeforeSelectedLine = w->GetSelectedLineForTest();
	if (outBeforeLineCount > 0)
		outBeforeLastLine = w->GetLineForTest(outBeforeLineCount - 1);

	w->ReloadFromWatcherForTest(path.c_str());

	outAfterLineCount = w->GetLineCountForTest();
	outAfterSelectedLine = w->GetSelectedLineForTest();
	if (outAfterLineCount > 0)
		outAfterLastLine = w->GetLineForTest(outAfterLineCount - 1);

	return true;
}

bool ATUIDebuggerQuerySourceMappingForTest(const char *utf8Path,
	uint32 address,
	int lineIndex,
	int& outLineForAddress,
	sint32& outAddressForLine)
{
	outLineForAddress = -1;
	outAddressForLine = -1;

	if (!utf8Path || !*utf8Path)
		return false;

	VDStringW path = VDTextU8ToW(utf8Path, -1);
	ATImGuiSourcePaneImpl *w = FindSourceWindow(path.c_str());
	if (!w)
		return true;

	outLineForAddress = w->GetLineForAddress(address);
	outAddressForLine = w->GetAddressForLine(lineIndex);
	return true;
}

// =========================================================================
// Source-level stepping — compute step ranges from address mappings
// (matches Windows uidbgsource.cpp OnPaneCommand)
// =========================================================================

static ATImGuiSourcePaneImpl *FindFocusedSourcePane() {
	uint32 focusId = ATUIDebuggerGetFocusedPaneId();
	if (focusId < kATUIPaneId_Source)
		return nullptr;
	for (auto *w : g_sourceWindows) {
		if (w->GetPaneId() == focusId)
			return w;
	}
	return nullptr;
}

static bool ComputeSourceStepRange(ATImGuiSourcePaneImpl *srcPane, uint32 pc, ATDebuggerStepRange& range) {
	if (!srcPane)
		return false;

	const auto& addrToLine = srcPane->GetAddressToLineMap();

	auto it = addrToLine.upper_bound(pc);
	if (it != addrToLine.end() && it != addrToLine.begin()) {
		auto itNext = it;
		--it;

		uint32 addr1 = it->first;
		uint32 addr2 = itNext->first;

		if (addr2 - addr1 < 100 && addr1 != addr2 && pc + 1 < addr2) {
			range.mAddr = pc + 1;
			range.mSize = (addr2 - pc) - 1;
			return true;
		}
	}

	IATDebuggerSymbolLookup *dsl = ATGetDebuggerSymbolLookup();
	if (!dsl)
		return false;

	uint32 moduleId;
	ATSourceLineInfo sli1, sli2;
	if (dsl->LookupLine(pc, false, moduleId, sli1)
		&& dsl->LookupLine(pc, true, moduleId, sli2)
		&& sli2.mOffset > pc + 1
		&& sli2.mOffset - sli1.mOffset < 100)
	{
		range.mAddr = pc + 1;
		range.mSize = sli2.mOffset - (pc + 1);
		return true;
	}

	return false;
}

bool ATUIDebuggerQuerySourceStepRangeForTest(const char *utf8Path,
	uint32 pc,
	bool& outHasRange,
	uint32& outStart,
	uint32& outLength)
{
	outHasRange = false;
	outStart = 0;
	outLength = 0;

	if (!utf8Path || !*utf8Path)
		return false;

	VDStringW path = VDTextU8ToW(utf8Path, -1);
	ATImGuiSourcePaneImpl *w = FindSourceWindow(path.c_str());
	if (!w)
		return true;

	ATDebuggerStepRange range {};
	if (ComputeSourceStepRange(w, pc, range)) {
		outHasRange = true;
		outStart = range.mAddr;
		outLength = range.mSize;
	}

	return true;
}

bool ATUIDebuggerToggleSourceBreakpointForTest(const char *utf8Path,
	int lineIndex,
	int& outBefore,
	int& outAfter,
	sint32& outAddress)
{
	outBefore = 0;
	outAfter = 0;
	outAddress = -1;

	if (!utf8Path || !*utf8Path || lineIndex < 0)
		return false;

	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return false;

	ATUIDebuggerOpen();

	VDStringW path = VDTextU8ToW(utf8Path, -1);
	IATSourceWindow *sourceWindow = ATImGuiOpenSourceWindow(path.c_str());
	auto *w = static_cast<ATImGuiSourcePaneImpl *>(sourceWindow);
	if (!w)
		return true;

	outAddress = w->GetAddressForLine(lineIndex);

	vdfastvector<uint32> bps;
	dbg->GetBreakpointList(bps);
	outBefore = (int)bps.size();

	w->ActivateLine(lineIndex);
	w->OnPaneCommand(kATUIPaneCommandId_DebugToggleBreakpoint);

	bps.clear();
	dbg->GetBreakpointList(bps);
	outAfter = (int)bps.size();
	return true;
}

bool ATUIDebuggerExecuteSourceCommandForTest(const char *utf8Path,
	const char *command,
	bool pcOverride,
	uint32 pcOverrideValue,
	bool& outHandled,
	uint32& outPC,
	bool& outHadRange,
	uint32& outRangeStart,
	uint32& outRangeLength,
	bool& outWasRunning,
	bool& outRunningAfterCommand,
	bool& outRunningAfterCleanup)
{
	outHandled = false;
	outPC = 0;
	outHadRange = false;
	outRangeStart = 0;
	outRangeLength = 0;
	outWasRunning = false;
	outRunningAfterCommand = false;
	outRunningAfterCleanup = false;

	if (!utf8Path || !*utf8Path || !command || !*command)
		return false;

	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return false;

	VDStringW path = VDTextU8ToW(utf8Path, -1);
	IATSourceWindow *sourceWindow = ATImGuiOpenSourceWindow(path.c_str());
	auto *w = static_cast<ATImGuiSourcePaneImpl *>(sourceWindow);
	if (!w)
		return true;

	ATUIPaneCommandId commandId {};
	if (!strcmp(command, "run"))
		commandId = kATUIPaneCommandId_DebugRun;
	else if (!strcmp(command, "step_into"))
		commandId = kATUIPaneCommandId_DebugStepInto;
	else if (!strcmp(command, "step_over"))
		commandId = kATUIPaneCommandId_DebugStepOver;
	else if (!strcmp(command, "step_out"))
		commandId = kATUIPaneCommandId_DebugStepOut;
	else
		return false;

	ATActivateUIPane(w->GetPaneId(), true, true);
	outWasRunning = dbg->IsRunning();
	if (outWasRunning)
		dbg->Break();

	if (pcOverride)
		dbg->SetPC((uint16)pcOverrideValue);

	outPC = dbg->GetPC();

	ATDebuggerStepRange range {};
	if (commandId == kATUIPaneCommandId_DebugStepInto
		|| commandId == kATUIPaneCommandId_DebugStepOver) {
		if (ComputeSourceStepRange(w, outPC, range)) {
			outHadRange = true;
			outRangeStart = range.mAddr;
			outRangeLength = range.mSize;
		}
	}

	outHandled = w->OnPaneCommand(commandId);
	outRunningAfterCommand = dbg->IsRunning();

	if (outRunningAfterCommand)
		dbg->Break();

	outRunningAfterCleanup = dbg->IsRunning();
	return true;
}

// Compute step ranges for the current source line and execute a step.
// Returns true if handled (step ranges computed and step started).
static bool SourceStepWithRanges(ATImGuiSourcePaneImpl *srcPane, bool stepOver) {
	if (!srcPane)
		return false;

	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return false;

	const uint32 pc = dbg->GetPC();

	auto stepMethod = stepOver ? &IATDebugger::StepOver : &IATDebugger::StepInto;

	try {
		ATDebuggerStepRange range {};
		if (ComputeSourceStepRange(srcPane, pc, range)) {
			(dbg->*stepMethod)(kATDebugSrcMode_Source, &range, 1);
			return true;
		}

		// Fallback: step without ranges (single instruction in source mode)
		(dbg->*stepMethod)(kATDebugSrcMode_Source, nullptr, 0);
		return true;

	} catch(const MyError& e) {
		ATConsolePrintf("%s\n", e.c_str());
		return true;
	}
}

bool ATUIDebuggerSourceStepInto() {
	return SourceStepWithRanges(FindFocusedSourcePane(), false);
}

bool ATUIDebuggerSourceStepOver() {
	return SourceStepWithRanges(FindFocusedSourcePane(), true);
}

// =========================================================================
// Source file list dialog — rendered as a modal popup
// =========================================================================

struct SourceListEntry {
	VDStringW mPath;
	uint32 mLineCount = 0;
};

static bool g_showSourceListDialog = false;
static bool g_sourceListNeedsRefresh = true;
static std::vector<SourceListEntry> g_sourceListFiles;
static int g_sourceListSelected = -1;

static void RefreshSourceListDialogFiles() {
	g_sourceListFiles.clear();
	g_sourceListSelected = -1;

	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	dbg->EnumSourceFiles([](const wchar_t *path, uint32 lineCount) {
		// Match native ATUIDebuggerSourceListDialog: CC65 symbols can contain
		// source-file entries without line mappings, and Windows hides them.
		if (lineCount)
			g_sourceListFiles.push_back({VDStringW(path), lineCount});
	});

	std::sort(g_sourceListFiles.begin(), g_sourceListFiles.end(),
		[](const SourceListEntry& a, const SourceListEntry& b) {
			return a.mPath < b.mPath;
		});

	if (!g_sourceListFiles.empty())
		g_sourceListSelected = 0;
}

static void OpenSelectedSourceListFile() {
	if (g_sourceListSelected < 0 || g_sourceListSelected >= (int)g_sourceListFiles.size())
		return;

	ATImGuiOpenSourceWindow(g_sourceListFiles[g_sourceListSelected].mPath.c_str());
	g_showSourceListDialog = false;
	ImGui::CloseCurrentPopup();
}

void ATUIDebuggerShowSourceListDialog() {
	g_showSourceListDialog = true;
	g_sourceListNeedsRefresh = true;
}

void ATUIDebuggerRenderSourceListDialog() {
	if (!g_showSourceListDialog)
		return;

	ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	ImGui::OpenPopup("Source Files");

	bool open = true;
	if (ImGui::BeginPopupModal("Source Files", &open, ImGuiWindowFlags_NoSavedSettings)) {
		if (g_sourceListNeedsRefresh) {
			RefreshSourceListDialogFiles();
			g_sourceListNeedsRefresh = false;
		}

		const float buttonRowH = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
		if (ImGui::BeginChild("SourceFileList", ImVec2(0, -buttonRowH), ImGuiChildFlags_None,
				ImGuiWindowFlags_HorizontalScrollbar)) {
			if (g_sourceListFiles.empty()) {
				ImGui::TextDisabled("No symbols loaded with source file information.");
			} else {
				for (int i = 0; i < (int)g_sourceListFiles.size(); ++i) {
					VDStringA pathU8 = VDTextWToU8(g_sourceListFiles[i].mPath);
					if (ImGui::Selectable(pathU8.c_str(), i == g_sourceListSelected,
							ImGuiSelectableFlags_AllowDoubleClick)) {
						g_sourceListSelected = i;

						if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
							OpenSelectedSourceListFile();
					}
				}
			}
		}
		ImGui::EndChild();

		const bool canOpen = g_sourceListSelected >= 0;
		if (!canOpen)
			ImGui::BeginDisabled();
		if (ImGui::Button("OK"))
			OpenSelectedSourceListFile();
		if (!canOpen)
			ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			g_showSourceListDialog = false;
			ImGui::CloseCurrentPopup();
		}

		if (!open)
			g_showSourceListDialog = false;

		ImGui::EndPopup();
	} else if (!open) {
		g_showSourceListDialog = false;
	}
}
