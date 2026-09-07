//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2022 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#include "stdafx.h"
#include <windows.h>
#include <windowsx.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/vdstl_hashset.h>
#include <vd2/system/w32assist.h>
#include <vd2/Dita/services.h>
#include <at/atnativeui/theme.h>
#include "oshelper.h"
#include "simulator.h"
#include "resource.h"
#include "uiaccessors.h"
#include "uidbgsource.h"

extern ATSimulator g_sim;
extern HWND g_hwnd;

typedef vdfastvector<IATSourceWindow *> SourceWindows;
SourceWindows g_sourceWindows;

ATSourceWindow::ATSourceWindow(uint32 id, const wchar_t *name)
	: ATUIDebuggerPaneWindow(id, name)
{

}

ATSourceWindow::~ATSourceWindow() {
}

bool ATSourceWindow::CreatePane(uint32 id, ATUIPane **pp) {
	uint32 index = id - kATUIPaneId_Source;

	if (index >= g_sourceWindows.size())
		return false;

	ATSourceWindow *w = static_cast<ATSourceWindow *>(g_sourceWindows[index]);

	if (!w)
		return false;

	w->AddRef();
	*pp = w;
	return true;
}

void ATSourceWindow::LoadFile(const wchar_t *s, const wchar_t *alias) {
	VDTextInputFile ifile(s);

	mpTextEditor->SetUpdateEnabled(false);
	mpTextEditor->Clear();

	mModuleId = 0;
	mFileId = 0;
	mPath = s;
	mFullPath = VDGetFullPath(s);

	BindToSymbols();

	mFileAddressToLineLookup.clear();
	mFileLineToAddressLookup.clear();

	uint32 lineno = 0;
	bool listingMode = false;
	while(const char *line = ifile.GetNextLine()) {
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
				mFileAddressToLineLookup.insert(AddressLookup::value_type(address, lineno));
				mFileLineToAddressLookup.insert(AddressLookup::value_type(lineno, address));
			}
		} else if (!mModuleId) {
			if (!lineno && !strncmp(line, "mads ", 5))
				listingMode = true;
		}

		mpTextEditor->Append(VDTextU8ToW(VDStringSpanA(line)).c_str());
		mpTextEditor->Append(L"\n");
		++lineno;
	}

	MergeSymbols();

	if (alias)
		mPathAlias = alias;
	else
		mPathAlias.clear();

	if (mModulePath.empty() && !listingMode)
		mModulePath = VDTextWToA(VDFileSplitPathRight(mPath));

	mpTextEditor->RecolorAll();
	mpTextEditor->SetUpdateEnabled(true);

	mFileWatcher.Init(s, this);

	VDSetWindowTextFW32(mhwnd, L"%ls [source] - Altirra", VDFileSplitPath(s));
	ATGetDebugger()->RequestClientUpdate(this);
}

VDStringW ATSourceWindow::ReadLine(int lineIndex) {
	vdfastvector<wchar_t> lineText;

	VDStringW s;
	if (mpTextEditor->GetLineText(lineIndex, lineText)) {
		s.append(lineText.data(), lineText.size());
		if (s.empty() || s.back() != '\n')
			s.push_back('\n');
	}

	return s;
}

bool ATSourceWindow::OnPaneCommand(ATUIPaneCommandId id) {
	switch(id) {
		case kATUIPaneCommandId_DebugToggleBreakpoint:
			ToggleBreakpoint();
			return true;

		case kATUIPaneCommandId_DebugRun:
			ATGetDebugger()->Run(kATDebugSrcMode_Source);
			return true;

		case kATUIPaneCommandId_DebugStepOver:
		case kATUIPaneCommandId_DebugStepInto:
			{
				IATDebugger *dbg = ATGetDebugger();
				IATDebuggerSymbolLookup *dsl = ATGetDebuggerSymbolLookup();
				const uint32 pc = dbg->GetPC();

				void (IATDebugger::*stepMethod)(ATDebugSrcMode, const ATDebuggerStepRange *, uint32)
					= (id == kATUIPaneCommandId_DebugStepOver) ? &IATDebugger::StepOver : &IATDebugger::StepInto;

				auto it = mAddressToLineLookup.upper_bound(pc);
				if (it != mAddressToLineLookup.end() && it != mAddressToLineLookup.begin()) {
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

				uint32 moduleId;
				ATSourceLineInfo sli1;
				ATSourceLineInfo sli2;
				if (dsl->LookupLine(pc, false, moduleId, sli1)
					&& dsl->LookupLine(pc, true, moduleId, sli2)
					&& sli2.mOffset > pc + 1
					&& sli2.mOffset - sli1.mOffset < 100)
				{
					ATDebuggerStepRange range = { pc + 1, sli2.mOffset - (pc + 1) };
					(dbg->*stepMethod)(kATDebugSrcMode_Source, &range, 1);
					return true;
				}

				(dbg->*stepMethod)(kATDebugSrcMode_Source, 0, 0);
			}

			return true;

		case kATUIPaneCommandId_DebugStepOut:
			ATGetDebugger()->StepOut(kATDebugSrcMode_Source);
			return true;
	}

	return false;
}

LRESULT ATSourceWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_USER + 101:
			if (!mPath.empty()) {
				try {
					LoadFile(mPath.c_str(), NULL);
				} catch(const MyError&) {
					// eat
				}
			}
			return 0;

		case WM_CONTEXTMENU:
			{
				int x = GET_X_LPARAM(lParam);
				int y = GET_Y_LPARAM(lParam);

				// if we don't have a point (Shift+F10), try to use the cursor
				// position, else just use the center
				if (x == -1 && y == -1) {
					const vdpoint32& pt = mpTextEditor->GetScreenPosForContextMenu();
					x = pt.x;
					y = pt.y;
				} else {
					POINT pt = {x, y};
					if (ScreenToClient(mhwndTextEditor, &pt))
						mpTextEditor->SetCursorPixelPosIfOutsideSelection(pt.x, pt.y);
				}

				TrackPopupMenu(ATUIGetSourceContextMenuW32(), TPM_LEFTALIGN|TPM_TOPALIGN, x, y, 0, mhwnd, NULL);
			}
			return 0;

		case WM_SETFOCUS:
			::SetFocus(mhwndTextEditor);
			return 0;

		case WM_SETCURSOR:
			if (LOWORD(lParam) == HTCLIENT && HIWORD(lParam)) {
				::SetCursor(::LoadCursor(NULL, IDC_IBEAM));
				return TRUE;
			}
			break;
	}

	return ATUIDebuggerPaneWindow::WndProc(msg, wParam, lParam);
}

bool ATSourceWindow::OnCreate() {
	if (!VDCreateTextEditor(~mpTextEditor))
		return false;

	mhwndTextEditor = (HWND)mpTextEditor->Create(WS_EX_NOPARENTNOTIFY, WS_CHILD|WS_VISIBLE, 0, 0, 0, 0, (VDGUIHandle)mhwnd, 100);

	OnFontsUpdated();

	mpTextEditor->SetReadOnly(true);
	mpTextEditor->SetCallback(this);
	mpTextEditor->SetColorizer(this);

	ATGetDebugger()->AddClient(this);

	mEventCallbackId = g_sim.GetEventManager()->AddEventCallback(kATSimEvent_CPUPCBreakpointsUpdated,
		[this] { mpTextEditor->RecolorAll(); });

	return ATUIDebuggerPaneWindow::OnCreate();
}

void ATSourceWindow::OnDestroy() {
	uint32 index = mPaneId - kATUIPaneId_Source;

	if (index < g_sourceWindows.size() && g_sourceWindows[index] == this) {
		g_sourceWindows[index] = NULL;
	} else {
		VDASSERT(!"Source window not found in global map.");
	}

	mFileWatcher.Shutdown();

	if (mEventCallbackId) {
		g_sim.GetEventManager()->RemoveEventCallback(mEventCallbackId);
		mEventCallbackId = 0;
	}

	ATGetDebugger()->RemoveClient(this);

	ATUIDebuggerPaneWindow::OnDestroy();
}

void ATSourceWindow::OnSize() {
	RECT r;
	VDVERIFY(GetClientRect(mhwnd, &r));

	if (mhwndTextEditor) {
		VDVERIFY(SetWindowPos(mhwndTextEditor, NULL, 0, 0, r.right, r.bottom, SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE));
	}
}

void ATSourceWindow::OnFontsUpdated() {
	if (mhwndTextEditor)
		SendMessage(mhwndTextEditor, WM_SETFONT, (WPARAM)ATGetConsoleFontW32(), TRUE);
}

bool ATSourceWindow::OnCommand(uint32 cmd, uint32 extcode) {
	switch(cmd) {
		case ID_CONTEXT_TOGGLEBREAKPOINT:
			ToggleBreakpoint();
			return true;

		case ID_CONTEXT_SHOWNEXTSTATEMENT:
			{
				uint32 moduleId;
				ATSourceLineInfo lineInfo;
				IATDebuggerSymbolLookup *lookup = ATGetDebuggerSymbolLookup();
				if (lookup->LookupLine(ATGetDebugger()->GetFramePC(), false, moduleId, lineInfo)) {
					ATDebuggerSourceFileInfo sourceFileInfo;
					if (lookup->GetSourceFilePath(moduleId, lineInfo.mFileId, sourceFileInfo) && lineInfo.mLine) {
						IATSourceWindow *w = ATOpenSourceWindow(sourceFileInfo);
						if (w) {
							w->FocusOnLine(lineInfo.mLine - 1);

							return true;
						}
					}
				}
			}
			// fall through to go-to-disassembly

		case ID_CONTEXT_GOTODISASSEMBLY:
			{
				sint32 addr = GetCurrentLineAddress();

				if (addr != -1) {
					ATActivateUIPane(kATUIPaneId_Disassembly, true);
					IATUIDebuggerDisassemblyPane *disPane = vdpoly_cast<IATUIDebuggerDisassemblyPane *>(ATGetUIPane(kATUIPaneId_Disassembly));
					if (disPane) {
						disPane->SetPosition((uint16)addr);
					}
				} else {
					::MessageBoxW(mhwnd, L"There are no symbols associating code with that location.", L"Altirra Error", MB_ICONERROR | MB_OK);
				}
			}
			return true;

		case ID_CONTEXT_SETNEXTSTATEMENT:
			{
				sint32 addr = GetCurrentLineAddress();

				if (addr != -1) {
					ATGetDebugger()->SetPC((uint16)addr);
				}
			}
			return true;

		case ID_OPENIN_DEFAULTEDITOR:
			ATLaunchFileForEdit(mFullPath.c_str());
			return true;

		case ID_OPENIN_FILEEXPLORER:
			ATShowFileInSystemExplorer(mFullPath.c_str());
			return true;
	}
	return false;
}

void ATSourceWindow::OnTextEditorUpdated() {
}

void ATSourceWindow::OnTextEditorScrolled(int firstVisiblePara, int lastVisiblePara, int visibleParaCount, int totalParaCount) {
}

void ATSourceWindow::OnLinkSelected(uint32 selectionCode, int para, int offset) {
}

void ATSourceWindow::RecolorLine(int line, const wchar_t *text, int length, IVDTextEditorColorization *cl) {
	int next = 0;

	auto it = mLineToAddressLookup.find(line);
	bool addressMapped = false;

	const auto& tc = ATUIGetThemeColors();

	if (it != mLineToAddressLookup.end()) {
		uint32 addr = it->second;

		if (ATGetDebugger()->IsBreakpointAtPC(addr)) {
			cl->AddTextColorPoint(next, tc.mHiMarkedFg, tc.mHiMarkedBg);
			next += 4;
		}

		addressMapped = true;
	}

	if (!addressMapped && ATGetDebugger()->IsDeferredBreakpointSet(mModulePath.c_str(), line + 1)) {
		cl->AddTextColorPoint(next, tc.mPendingHiMarkedFg, tc.mPendingHiMarkedBg);
		next += 4;
		addressMapped = true;
	}

	if (line == mPCLine) {
		cl->AddTextColorPoint(next, tc.mDirectLocationFg, tc.mDirectLocationBg);
		return;
	} else if (line == mFramePCLine) {
		cl->AddTextColorPoint(next, tc.mIndirectLocationFg, tc.mIndirectLocationBg);
		return;
	}

	if (next > 0) {
		cl->AddTextColorPoint(next, -1, -1);
		return;
	}

	sint32 backColor = -1;
	if (!addressMapped) {
		backColor = tc.mInactiveTextBg;
		cl->AddTextColorPoint(next, -1, backColor);
	}

	for(int i=0; i<length; ++i) {
		wchar_t c = text[i];

		if (c == ';') {
			cl->AddTextColorPoint(0, tc.mCommentText, backColor);
			return;
		} else if (c == '.') {
			int j = i + 1;

			while(j < length) {
				c = text[j];

				if (!(c >= 'a' && c <= 'z') &&
					!(c >= 'A' && c <= 'Z') &&
					!(c >= '0' && c <= '9'))
				{
					break;
				}

				++j;
			}

			if (j > i+1) {
				cl->AddTextColorPoint(i, tc.mKeywordText, backColor);
				cl->AddTextColorPoint(j, -1, backColor);
			}

			return;
		}

		if (c != ' ' && c != '\t')
			break;
	}
}

void ATSourceWindow::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	if (mLastFrameExtPC == state.mFrameExtPC && mLastPC == state.mPC)
		return;

	mLastFrameExtPC = state.mFrameExtPC;
	mLastPC = state.mPC;

	int pcline = -1;
	int frameline = -1;

	if (!state.mbRunning) {
		pcline = GetLineForAddress(state.mInsnPC);
		frameline = GetLineForAddress(state.mFrameExtPC);
	}

	if (pcline >= 0)
		mpTextEditor->SetCursorPos(pcline, 0);

	SetPCLine(pcline);

	if (frameline >= 0)
		mpTextEditor->SetCursorPos(frameline, 0);

	SetFramePCLine(frameline);
}

void ATSourceWindow::OnDebuggerEvent(ATDebugEvent eventId) {
	switch(eventId) {
		case kATDebugEvent_BreakpointsChanged:
			mpTextEditor->RecolorAll();
			break;

		case kATDebugEvent_SymbolsChanged:
			BindToSymbols();
			MergeSymbols();

			mpTextEditor->RecolorAll();
			break;
	}
}

int ATSourceWindow::GetLineForAddress(uint32 addr) {
	auto it = mAddressToLineLookup.upper_bound(addr);
	if (it != mAddressToLineLookup.begin()) {
		--it;

		if (addr - (uint32)it->first < 64)
			return it->second;
	}

	return -1;
}

bool ATSourceWindow::OnFileUpdated(const wchar_t *path) {
	if (mhwnd)
		PostMessage(mhwnd, WM_USER + 101, 0, 0);

	return true;
}

void ATSourceWindow::ToggleBreakpoint() {
	IATDebugger *dbg = ATGetDebugger();
	const int line = mpTextEditor->GetCursorLine();

	if (!mModulePath.empty()) {
		dbg->ToggleSourceBreakpoint(mModulePath.c_str(), line + 1);
	} else {
		auto it = mLineToAddressLookup.find(line);
		if (it != mLineToAddressLookup.end())
			ATGetDebugger()->ToggleBreakpoint((uint16)it->second);
		else
			MessageBeep(MB_ICONEXCLAMATION);
	}
}

void ATSourceWindow::ActivateLine(int line) {
	mpTextEditor->SetCursorPos(line, 0);

	ATActivateUIPane(mPaneId, true);
}

void ATSourceWindow::FocusOnLine(int line) {
	mpTextEditor->CenterViewOnLine(line);
	mpTextEditor->SetCursorPos(line, 0);

	ATActivateUIPane(mPaneId, true);
}

void ATSourceWindow::SetPCLine(int line) {
	if (line == mPCLine)
		return;

	int oldLine = mPCLine;
	mPCLine = line;
	mpTextEditor->RecolorLine(oldLine);
	mpTextEditor->RecolorLine(line);
}

void ATSourceWindow::SetFramePCLine(int line) {
	if (line == mFramePCLine)
		return;

	int oldLine = mFramePCLine;
	mFramePCLine = line;
	mpTextEditor->RecolorLine(oldLine);
	mpTextEditor->RecolorLine(line);
}

sint32 ATSourceWindow::GetCurrentLineAddress() const {
	int line = mpTextEditor->GetCursorLine();

	auto it = mLineToAddressLookup.find(line);
	if (it == mLineToAddressLookup.end())
		return -1;

	return it->second;
}

bool ATSourceWindow::BindToSymbols() {
	mSymbolAddressToLineLookup.clear();
	mSymbolLineToAddressLookup.clear();

	IATDebuggerSymbolLookup *symlookup = ATGetDebuggerSymbolLookup();
	if (!symlookup->LookupFile(mPath.c_str(), mModuleId, mFileId))
		return false;

	ATDebuggerSourceFileInfo sourceFileInfo;
	symlookup->GetSourceFilePath(mModuleId, mFileId, sourceFileInfo);

	mModulePath = VDTextWToA(sourceFileInfo.mSourcePath);

	vdfastvector<ATSourceLineInfo> lines;
	ATGetDebuggerSymbolLookup()->GetLinesForFile(mModuleId, mFileId, lines);

	for (const ATSourceLineInfo& linfo : lines) {
		mSymbolAddressToLineLookup.insert(AddressLookup::value_type(linfo.mOffset, linfo.mLine - 1));
		mSymbolLineToAddressLookup.insert(AddressLookup::value_type(linfo.mLine - 1, linfo.mOffset));
	}

	return true;
}

void ATSourceWindow::MergeSymbols() {
	mAddressToLineLookup.clear();

	for(const auto& addressToLine : mSymbolAddressToLineLookup)
		mAddressToLineLookup.insert(addressToLine);

	for(const auto& addressToLine : mFileAddressToLineLookup)
		mAddressToLineLookup.insert(addressToLine);

	mLineToAddressLookup.clear();

	for(const auto& lineToAddress : mSymbolLineToAddressLookup)
		mLineToAddressLookup.insert(lineToAddress);

	for(const auto& lineToAddress : mFileLineToAddressLookup)
		mLineToAddressLookup.insert(lineToAddress);
}

IATSourceWindow *ATGetSourceWindow(const wchar_t *s) {
	for(IATSourceWindow *w : g_sourceWindows) {
		if (!w)
			continue;

		const wchar_t *t = w->GetPath();

		if (VDFileIsPathEqual(s, t))
			return w;

		t = w->GetPathAlias();

		if (t && VDFileIsPathEqual(s, t))
			return w;
	}

	// try a looser check
	s = VDFileSplitPath(s);

	for(IATSourceWindow *w : g_sourceWindows) {
		if (!w)
			continue;

		const wchar_t *t = VDFileSplitPath(w->GetPath());

		if (VDFileIsPathEqual(s, t))
			return w;

		t = w->GetPathAlias();
		if (t) {
			t = VDFileSplitPath(t);

			if (VDFileIsPathEqual(s, t))
				return w;
		}
	}

	return NULL;
}

IATSourceWindow *ATOpenSourceWindow(const wchar_t *s) {
	ATDebuggerSourceFileInfo sourceFileInfo;
	sourceFileInfo.mSourcePath = s;

	return ATOpenSourceWindow(sourceFileInfo, true);
}

IATSourceWindow *ATOpenSourceWindow(const ATDebuggerSourceFileInfo& sourceFileInfo, bool searchPaths) {
	IATSourceWindow *w = ATGetSourceWindow(sourceFileInfo.mSourcePath.c_str());
	if (w)
		return w;

	VDStringW fn;
	bool fnValid = false;

	if (searchPaths) {
		// try module relative path
		vdhashset<VDStringW> seenPaths;

		if (!fnValid && !sourceFileInfo.mModulePath.empty()) {
			const auto moduleDir = VDFileSplitPathLeftSpan(sourceFileInfo.mModulePath);

			if (!moduleDir.empty()) {
				seenPaths.insert(moduleDir);

				fn = VDMakePath(
					moduleDir,
					VDFileSplitPathRightSpan(sourceFileInfo.mSourcePath)
				);

				fnValid = VDDoesPathExist(fn.c_str());
			}
		}

		// try full paths of any other open source windows
		if (!fnValid) {
			for (IATSourceWindow *sw : g_sourceWindows) {
				if (!sw)
					continue;

				VDStringSpanW baseDir = VDFileSplitPathLeftSpan(VDStringSpanW(sw->GetFullPath()));

				if (!baseDir.empty() && seenPaths.insert(baseDir).second) {
					fn = VDMakePath(
						baseDir,
						VDFileSplitPathRightSpan(sourceFileInfo.mSourcePath)
					);

					fnValid = VDDoesPathExist(fn.c_str());
					if (fnValid)
						break;
				}
			}
		}
	}

	if (!fnValid) {
		fn = sourceFileInfo.mSourcePath;
		fnValid = VDDoesPathExist(fn.c_str());
	}

	if (!fnValid) {
		VDStringW title(L"Find source file ");
		title += fn;

		static const wchar_t kCatchAllFilter[] = L"\0All files (*.*)\0*.*";
		const wchar_t *baseName = VDFileSplitPath(fn.c_str());
		VDStringW filter(baseName);
		filter += L'\0';
		filter += baseName;
		filter.append(std::begin(kCatchAllFilter), std::end(kCatchAllFilter));		// !! intentionally capturing end null

		const auto parent = ATUIGetNewPopupOwner();
		fn = VDGetLoadFileName("src "_vdfcctypeid, parent, title.c_str(), filter.c_str(), NULL);

		if (fn.empty())
			return nullptr;
	}

	// find a free pane ID
	SourceWindows::iterator it = std::find(g_sourceWindows.begin(), g_sourceWindows.end(), nullptr);
	const uint32 paneIndex = (uint32)(it - g_sourceWindows.begin());

	const uint32 paneId = kATUIPaneId_Source + paneIndex;
	vdrefptr<ATSourceWindow> srcwin(new ATSourceWindow(paneId, VDFileSplitPath(fn.c_str())));

	if (it == g_sourceWindows.end())
		g_sourceWindows.push_back(nullptr);

	g_sourceWindows[paneIndex] = srcwin;

	// If the active window is a source or disassembly window, dock in the same place.
	// Otherwise, if we have a source window, dock over that, otherwise dock over the
	// disassembly window.
	uint32 activePaneId = ATUIGetActivePaneId();

	if (activePaneId >= kATUIPaneId_Source || activePaneId == kATUIPaneId_Disassembly)
		ATActivateUIPane(paneId, true, true, activePaneId, kATContainerDockCenter);
	else if (paneIndex > 0) {
		// We always add the new pane at the first non-null position or at the end,
		// whichever comes first. Therefore, unless this is pane 0, the previous pane
		// is always present.
		ATActivateUIPane(paneId, true, true, paneId - 1, kATContainerDockCenter);
	} else if (ATGetUIPane(kATUIPaneId_Disassembly))
		ATActivateUIPane(paneId, true, true, kATUIPaneId_Disassembly, kATContainerDockCenter);
	else
		ATActivateUIPane(paneId, true);

	if (srcwin->GetHandleW32()) {
		try {
			if (fn == sourceFileInfo.mSourcePath)
				srcwin->LoadFile(fn.c_str(), nullptr);
			else
				srcwin->LoadFile(fn.c_str(), sourceFileInfo.mSourcePath.c_str());
		} catch(const MyError& e) {
			ATCloseUIPane(paneId);
			e.post(g_hwnd, "Altirra error");
			return NULL;
		}

		return srcwin;
	}

	g_sourceWindows[paneIndex] = NULL;

	return NULL;
}

void ATUIDebuggerRegisterSourcePane() {
	ATRegisterUIPaneClass(kATUIPaneId_Source, ATSourceWindow::CreatePane);
}
