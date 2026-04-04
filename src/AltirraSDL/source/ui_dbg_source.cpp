//	AltirraSDL - Dear ImGui debugger source code pane
//	Replaces Win32 ATSourceWindow (uidbgsource.cpp).
//	Displays source code files with line numbers, breakpoint indicators,
//	PC highlighting, and click-to-toggle breakpoints.
//	Multiple source windows can be open simultaneously using dynamically
//	assigned pane IDs starting at kATUIPaneId_Source.

#include <stdafx.h>
#include <algorithm>
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
#include <vd2/system/error.h>
#include <at/atdebugger/symbols.h>
#include "ui_debugger.h"
#include "console.h"
#include "debugger.h"
#include "simulator.h"

extern ATSimulator g_sim;
extern SDL_Window *g_pWindow;

// Forward declaration — defined later in this file
IATSourceWindow *ATImGuiOpenSourceWindow(const wchar_t *path);

// =========================================================================
// Source pane — displays loaded source files for debugging
// =========================================================================

class ATImGuiSourcePaneImpl final : public ATImGuiDebuggerPane,
                                    public IATSourceWindow {
public:
	ATImGuiSourcePaneImpl(uint32 paneId, const wchar_t *path);

	bool Render() override;
	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) override;
	void OnDebuggerEvent(ATDebugEvent eventId) override;

	// IATSourceWindow
	const wchar_t *GetFullPath() const override { return mPath.c_str(); }
	const wchar_t *GetPath() const override { return mPath.c_str(); }
	const wchar_t *GetPathAlias() const override { return mPath.c_str(); }
	void FocusOnLine(int line) override;
	void ActivateLine(int line) override;
	VDStringW ReadLine(int lineIndex) override;

	// Expose address-to-line map for source-level step range computation
	const std::map<uint32, int>& GetAddressToLineMap() const { return mAddressToLine; }

private:
	bool LoadFile();
	void BindToSymbols();
	void UpdatePCLine();

	VDStringW mPath;
	VDStringA mPathU8;

	// Source text lines
	std::vector<VDStringA> mLines;
	bool mbLoaded = false;
	bool mbLoadError = false;

	// Symbol binding
	uint32 mModuleId = 0;
	uint16 mFileId = 0;
	bool mbBound = false;

	// Address<->line mappings
	std::map<uint32, int> mAddressToLine;  // address -> 0-based line index
	std::map<int, uint32> mLineToAddress;  // 0-based line index -> address

	// PC highlighting
	int mPCLine = -1;
	int mFramePCLine = -1;

	// Scroll request
	int mScrollToLine = -1;
	bool mbNeedsSymbolRebind = true;

	// Context menu
	int mContextLine = -1;
	bool mbContextMenuRequested = false;
};

ATImGuiSourcePaneImpl::ATImGuiSourcePaneImpl(uint32 paneId, const wchar_t *path)
	: ATImGuiDebuggerPane(paneId, "Source")
	, mPath(path)
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

bool ATImGuiSourcePaneImpl::LoadFile() {
	mbLoaded = false;
	mbLoadError = false;
	mLines.clear();

	try {
		VDTextInputFile file(mPath.c_str());

		while (const char *line = file.GetNextLine()) {
			mLines.push_back(VDStringA(line));
		}

		mbLoaded = true;
	} catch (const MyError&) {
		mbLoadError = true;
		return false;
	}

	return true;
}

void ATImGuiSourcePaneImpl::BindToSymbols() {
	mbNeedsSymbolRebind = false;
	mAddressToLine.clear();
	mLineToAddress.clear();
	mbBound = false;

	IATDebuggerSymbolLookup *lookup = ATGetDebuggerSymbolLookup();
	if (!lookup)
		return;

	// Try to look up this file in the debugger's symbol tables
	if (!lookup->LookupFile(mPath.c_str(), mModuleId, mFileId))
		return;

	mbBound = true;

	// Get all source line mappings for this file
	vdfastvector<ATSourceLineInfo> lines;
	lookup->GetLinesForFile(mModuleId, mFileId, lines);

	for (const auto& li : lines) {
		int lineIdx = (int)li.mLine - 1;  // convert 1-based to 0-based
		if (lineIdx >= 0) {
			mAddressToLine[li.mOffset] = lineIdx;
			mLineToAddress[lineIdx] = li.mOffset;
		}
	}
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
	uint32 pc = mLastState.mInsnPC;
	auto it = mAddressToLine.find(pc);
	if (it != mAddressToLine.end())
		mPCLine = it->second;

	// Frame PC
	uint32 framePC = mLastState.mFrameExtPC & 0xFFFF;
	auto fit = mAddressToLine.find(framePC);
	if (fit != mAddressToLine.end())
		mFramePCLine = fit->second;
}

void ATImGuiSourcePaneImpl::FocusOnLine(int line) {
	mScrollToLine = line;
	RequestFocus();
}

void ATImGuiSourcePaneImpl::ActivateLine(int line) {
	// ActivateLine is like FocusOnLine but also sets the pane visible
	SetVisible(true);
	mScrollToLine = line;
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
					ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), ">");
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

				// Click to toggle breakpoint; right-click stores context line
				if (ImGui::Selectable(mLines[i].c_str(), false,
						ImGuiSelectableFlags_AllowOverlap)) {
					if (hasAddr && dbg) {
						VDStringA pathA = VDTextWToU8(mPath);
						dbg->ToggleSourceBreakpoint(pathA.c_str(), (uint32)(i + 1));
					}
				}
				if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
					mContextLine = i;
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
								IATSourceWindow *w = ATImGuiOpenSourceWindow(sourceFileInfo.mSourcePath.c_str());
								if (w) {
									w->FocusOnLine(lineInfo.mLine - 1);
									handled = true;
								}
							}
						}
					}
					// Fall through to disassembly if source not found
					if (!handled && hasAddr) {
						dbg->SetFramePC(addrIt->second);
						ATActivateUIPane(kATUIPaneId_Disassembly, true, true);
					}
				}

				// Go to Disassembly: matches Windows ID_CONTEXT_GOTODISASSEMBLY
				if (ImGui::MenuItem("Go to Disassembly", nullptr, false, hasAddr)) {
					dbg->SetFramePC(addrIt->second);
					ATActivateUIPane(kATUIPaneId_Disassembly, true, true);
				}

				ImGui::Separator();

				// Set Next Statement: matches Windows which uses SetPC
				if (ImGui::MenuItem("Set Next Statement", nullptr, false, hasAddr)) {
					dbg->SetPC((uint16)(addrIt->second & 0xFFFF));
				}

				ImGui::Separator();

				if (ImGui::MenuItem("Toggle Breakpoint", nullptr, false, hasAddr)) {
					VDStringA pathA = VDTextWToU8(mPath);
					dbg->ToggleSourceBreakpoint(pathA.c_str(), (uint32)(mContextLine + 1));
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
		if (wcscmp(w->GetPath(), path) == 0)
			return w;
	}
	return nullptr;
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
	if (!lookup->LookupLine(addr, true, moduleId, lineInfo))
		return false;

	ATDebuggerSourceFileInfo sourceFileInfo;
	if (!lookup->GetSourceFilePath(moduleId, lineInfo.mFileId, sourceFileInfo))
		return false;

	// Find or open the source window
	ATImGuiSourcePaneImpl *w = FindSourceWindow(sourceFileInfo.mSourcePath.c_str());
	if (!w) {
		uint32 paneId = g_nextSourcePaneId++;
		w = new ATImGuiSourcePaneImpl(paneId, sourceFileInfo.mSourcePath.c_str());
		ATUIDebuggerRegisterPane(w);
		g_sourceWindows.push_back(w);
	}

	w->FocusOnLine((int)lineInfo.mLine - 1);
	return true;
}

// Open a source file by path
IATSourceWindow *ATImGuiOpenSourceWindow(const wchar_t *path) {
	ATImGuiSourcePaneImpl *w = FindSourceWindow(path);
	if (!w) {
		uint32 paneId = g_nextSourcePaneId++;
		w = new ATImGuiSourcePaneImpl(paneId, path);
		ATUIDebuggerRegisterPane(w);
		g_sourceWindows.push_back(w);
	}
	w->RequestFocus();
	w->SetVisible(true);
	return static_cast<IATSourceWindow *>(w);
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

// Compute step ranges for the current source line and execute a step.
// Returns true if handled (step ranges computed and step started).
static bool SourceStepWithRanges(bool stepOver) {
	ATImGuiSourcePaneImpl *srcPane = FindFocusedSourcePane();
	if (!srcPane)
		return false;

	IATDebugger *dbg = ATGetDebugger();
	IATDebuggerSymbolLookup *dsl = ATGetDebuggerSymbolLookup();
	if (!dbg || !dsl)
		return false;

	const uint32 pc = dbg->GetPC();

	auto stepMethod = stepOver ? &IATDebugger::StepOver : &IATDebugger::StepInto;

	try {
		// First attempt: use the pane's own address-to-line lookup
		// Find the address range [addr1, addr2) containing the current PC
		const auto& addrToLine = srcPane->GetAddressToLineMap();

		auto it = addrToLine.upper_bound(pc);
		if (it != addrToLine.end() && it != addrToLine.begin()) {
			auto itNext = it;
			--it;

			uint32 addr1 = it->first;
			uint32 addr2 = itNext->first;

			if (addr2 - addr1 < 100 && addr1 != addr2 && pc + 1 < addr2) {
				ATDebuggerStepRange range = { pc + 1, (addr2 - pc) - 1 };
				(dbg->*stepMethod)(kATDebugSrcMode_Source, &range, 1);
				return true;
			}
		}

		// Second attempt: use symbol lookup
		uint32 moduleId;
		ATSourceLineInfo sli1, sli2;
		if (dsl->LookupLine(pc, false, moduleId, sli1)
			&& dsl->LookupLine(pc, true, moduleId, sli2)
			&& sli2.mOffset > pc + 1
			&& sli2.mOffset - sli1.mOffset < 100)
		{
			ATDebuggerStepRange range = { pc + 1, sli2.mOffset - (pc + 1) };
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
	return SourceStepWithRanges(false);
}

bool ATUIDebuggerSourceStepOver() {
	return SourceStepWithRanges(true);
}

// =========================================================================
// Source file list dialog — rendered as a modal popup
// =========================================================================

static bool g_showSourceListDialog = false;

static bool g_sourceListNeedsRefresh = true;

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

	if (ImGui::Begin("Source Files", &g_showSourceListDialog,
			ImGuiWindowFlags_NoSavedSettings)) {

		IATDebugger *dbg = ATGetDebugger();
		if (dbg) {
			struct FileEntry {
				VDStringW mPath;
				uint32 mModuleId;
			};
			static std::vector<FileEntry> files;

			if (g_sourceListNeedsRefresh) {
				files.clear();
				dbg->EnumSourceFiles([](const wchar_t *path, uint32 moduleId) {
					files.push_back({VDStringW(path), moduleId});
				});
				g_sourceListNeedsRefresh = false;
			}

			if (files.empty()) {
				ImGui::TextDisabled("(no source files loaded)");
			} else {
				for (int i = 0; i < (int)files.size(); ++i) {
					VDStringA pathU8 = VDTextWToU8(files[i].mPath);
					ImGui::PushID(i);
					if (ImGui::Selectable(pathU8.c_str())) {
						ATImGuiOpenSourceWindow(files[i].mPath.c_str());
						g_showSourceListDialog = false;
					}
					ImGui::PopID();
				}
			}
		}
	}
	ImGui::End();
}
