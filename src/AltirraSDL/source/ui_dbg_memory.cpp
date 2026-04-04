//	AltirraSDL - Dear ImGui debugger memory viewer pane
//	Core: construction, callbacks, data, editing, address bar, Render().
//	See also: ui_dbg_memory_hexdump.cpp, ui_dbg_memory_bitmap.cpp.

#include <stdafx.h>
#include <algorithm>
#include <cmath>
#include <vd2/system/registry.h>
#include <at/atcore/address.h>
#include <at/atdebugger/target.h>
#include "ui_dbg_memory.h"
#include "console.h"
#include "debugger.h"
#include "simulator.h"

extern ATSimulator g_sim;
extern SDL_Window *g_pWindow;

constexpr decltype(ATImGuiMemoryPaneImpl::kAddressSpaces) ATImGuiMemoryPaneImpl::kAddressSpaces;

// =========================================================================
// Construction / destruction
// =========================================================================

ATImGuiMemoryPaneImpl::ATImGuiMemoryPaneImpl(uint32 paneId, int instanceIndex)
	: ATImGuiDebuggerPane(paneId, "Memory")
	, mInstanceIndex(instanceIndex)
{
	VDStringA title;
	title.sprintf("Memory %d", instanceIndex + 1);
	mTitle = title;
	LoadSettings();
}

ATImGuiMemoryPaneImpl::~ATImGuiMemoryPaneImpl() {
	SaveSettings();
	if (mpBitmapTexture) {
		SDL_DestroyTexture(mpBitmapTexture);
		mpBitmapTexture = nullptr;
	}
}

// =========================================================================
// Settings persistence
// =========================================================================

void ATImGuiMemoryPaneImpl::SaveSettings() {
	char keyPath[64];
	snprintf(keyPath, sizeof(keyPath), "Debugger\\Memory %d", mInstanceIndex);
	VDRegistryAppKey key(keyPath, true);

	key.setInt("ViewStart", (int)mViewStart);
	key.setInt("Columns", (int)mColumns);
	key.setInt("ValueMode", (int)mValueMode);
	key.setInt("InterpretMode", (int)mInterpretMode);
	key.setInt("AddrSpace", mAddrSpaceIdx);
}

void ATImGuiMemoryPaneImpl::LoadSettings() {
	char keyPath[64];
	snprintf(keyPath, sizeof(keyPath), "Debugger\\Memory %d", mInstanceIndex);
	VDRegistryAppKey key(keyPath, false);

	mViewStart = (uint32)key.getInt("ViewStart", (int)mViewStart);
	mColumns = (uint32)key.getInt("Columns", (int)mColumns);

	int vm = key.getInt("ValueMode", (int)mValueMode);
	if (vm >= 0 && vm <= (int)ValueMode::DecWords)
		mValueMode = (ValueMode)vm;

	int im = key.getInt("InterpretMode", (int)mInterpretMode);
	if (im >= 0 && im <= (int)InterpretMode::Graphics8Bpp)
		mInterpretMode = (InterpretMode)im;

	int as = key.getInt("AddrSpace", mAddrSpaceIdx);
	if (as >= 0 && as < (int)(sizeof(kAddressSpaces) / sizeof(kAddressSpaces[0])))
		mAddrSpaceIdx = as;

	// Ensure dropdown matches the address space in mViewStart
	uint32 space = mViewStart & kATAddressSpaceMask;
	for (int i = 0; i < (int)(sizeof(kAddressSpaces) / sizeof(kAddressSpaces[0])); ++i) {
		if (kAddressSpaces[i].base == space) {
			mAddrSpaceIdx = i;
			break;
		}
	}

	// Apply constraints after loading
	SetColumns(mColumns);
}

// =========================================================================
// Debugger client callbacks
// =========================================================================

void ATImGuiMemoryPaneImpl::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	ATImGuiDebuggerPane::OnDebuggerSystemStateUpdate(state);
	mLastCycle = state.mCycle;
	if (!state.mbRunning)
		mbNeedsRebuild = true;
}

void ATImGuiMemoryPaneImpl::OnDebuggerEvent(ATDebugEvent eventId) {
	if (eventId == kATDebugEvent_MemoryChanged)
		mbNeedsRebuild = true;
}

// =========================================================================
// Data
// =========================================================================

void ATImGuiMemoryPaneImpl::RebuildView() {
	mbNeedsRebuild = false;

	if (!mbStateValid || !mLastState.mpDebugTarget || mLastState.mbRunning)
		return;

	IATDebugTarget *target = mLastState.mpDebugTarget;
	uint32 spaceSize = ATAddressGetSpaceSize(mViewStart);
	uint32 spaceMask = spaceSize - 1;
	uint32 spaceBase = mViewStart & kATAddressSpaceMask;
	uint32 offset = mViewStart & kATAddressOffsetMask;

	uint32 totalBytes = mColumns * mVisibleRows;
	if (totalBytes > spaceSize)
		totalBytes = spaceSize;

	mPrevData = mViewData;
	mViewData.resize(totalBytes);
	mChanged.assign(totalBytes, false);

	// Bulk read with wrap-around handling
	uint32 normOff = offset & spaceMask;
	uint32 firstChunk = std::min(totalBytes, spaceSize - normOff);
	target->DebugReadMemory(spaceBase | normOff, mViewData.data(), firstChunk);
	if (firstChunk < totalBytes)
		target->DebugReadMemory(spaceBase, mViewData.data() + firstChunk,
								totalBytes - firstChunk);

	// Change detection
	if (mPrevData.size() == mViewData.size()) {
		for (uint32 i = 0; i < totalBytes; ++i)
			mChanged[i] = (mViewData[i] != mPrevData[i]);
	}
}

// =========================================================================
// Navigation
// =========================================================================

void ATImGuiMemoryPaneImpl::NavigateToAddress(const char *expr) {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg) return;

	try {
		sint32 addr = dbg->EvaluateThrow(expr);
		mViewStart = (uint32)addr;
		mbNeedsRebuild = true;
		CancelEdit();

		// Sync address space dropdown with evaluated address
		uint32 space = (uint32)addr & kATAddressSpaceMask;
		for (int i = 0; i < (int)(sizeof(kAddressSpaces) / sizeof(kAddressSpaces[0])); ++i) {
			if (kAddressSpaces[i].base == space) {
				mAddrSpaceIdx = i;
				break;
			}
		}

		// Add to history (deduplicate, cap at 20)
		VDStringA entry(expr);
		for (auto it = mAddressHistory.begin(); it != mAddressHistory.end(); ++it) {
			if (*it == entry) { mAddressHistory.erase(it); break; }
		}
		mAddressHistory.insert(mAddressHistory.begin(), entry);
		if (mAddressHistory.size() > 20)
			mAddressHistory.pop_back();
	} catch (const MyError&) {
	}
}

void ATImGuiMemoryPaneImpl::SetColumns(uint32 cols) {
	cols = std::clamp<uint32>(cols, 1, 256);
	if (IsWordMode())
		cols = (cols + 1) & ~1;
	if (IsFontMode())
		cols = (cols + 7) & ~7;
	if (cols < 1) cols = 1;
	mColumns = cols;
	mbNeedsRebuild = true;
}

void ATImGuiMemoryPaneImpl::EnsureHighlightVisible() {
	if (!mHighlightedAddress.has_value()) return;

	uint32 spBase = mViewStart & kATAddressSpaceMask;
	uint32 viewOff = mViewStart & kATAddressOffsetMask;
	uint32 addrOff = mHighlightedAddress.value() & kATAddressOffsetMask;
	uint32 viewEnd = viewOff + mColumns * mVisibleRows;

	if (addrOff < viewOff) {
		uint32 rowStart = addrOff - addrOff % mColumns;
		mViewStart = spBase | rowStart;
		mbNeedsRebuild = true;
	} else if (addrOff >= viewEnd) {
		uint32 rowStart = addrOff - addrOff % mColumns;
		uint32 topRow = (rowStart >= mColumns * (mVisibleRows - 1))
			? rowStart - mColumns * (mVisibleRows - 1) : 0;
		mViewStart = spBase | topRow;
		mbNeedsRebuild = true;
	}
}

// =========================================================================
// Editing
// =========================================================================

void ATImGuiMemoryPaneImpl::BeginEdit() {
	if (!mHighlightedAddress.has_value()) return;

	IATDebugTarget *target = mLastState.mpDebugTarget;
	if (!target) return;

	uint32 addr = mHighlightedAddress.value();
	if (IsWordMode() && !mbHighlightedData) {
		uint8 buf[2];
		target->DebugReadMemory(addr, buf, 2);
		mEditValue = buf[0] | ((sint32)buf[1] << 8);
	} else {
		mEditValue = target->DebugReadByte(addr);
	}
	mEditPhase = 0;
}

void ATImGuiMemoryPaneImpl::CommitEdit() {
	if (mEditValue < 0 || !mHighlightedAddress.has_value()) return;

	IATDebugTarget *target = mLastState.mpDebugTarget;
	if (!target) { mEditValue = -1; return; }

	uint32 addr = mHighlightedAddress.value();
	if (IsWordMode() && !mbHighlightedData) {
		uint8 buf[2] = {
			(uint8)(mEditValue & 0xFF),
			(uint8)((mEditValue >> 8) & 0xFF)
		};
		target->WriteMemory(addr, buf, 2);
	} else {
		target->WriteByte(addr, (uint8)(mEditValue & 0xFF));
	}

	mEditValue = -1;
	mEditPhase = 0;
	mbNeedsRebuild = true;
}

void ATImGuiMemoryPaneImpl::CancelEdit() {
	mEditValue = -1;
	mEditPhase = 0;
	mHighlightedAddress.reset();
	mbSelectionEnabled = false;
}

// =========================================================================
// Address bar
// =========================================================================

void ATImGuiMemoryPaneImpl::RenderAddressBar() {
	// Address space selector
	ImGui::SetNextItemWidth(80);
	if (ImGui::BeginCombo("##space", kAddressSpaces[mAddrSpaceIdx].name)) {
		for (int i = 0; i < (int)(sizeof(kAddressSpaces) / sizeof(kAddressSpaces[0])); ++i) {
			if (ImGui::Selectable(kAddressSpaces[i].name, i == mAddrSpaceIdx)) {
				mAddrSpaceIdx = i;
				mViewStart = kAddressSpaces[i].base;
				mbNeedsRebuild = true;
				CancelEdit();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();

	// Address input with history dropdown
	ImGui::SetNextItemWidth(120);
	if (ImGui::InputText("##addr", mAddrInput, sizeof(mAddrInput),
			ImGuiInputTextFlags_EnterReturnsTrue)) {
		NavigateToAddress(mAddrInput);
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(20);
	if (ImGui::BeginCombo("##addrhist", "", ImGuiComboFlags_NoPreview)) {
		for (auto& h : mAddressHistory) {
			if (ImGui::Selectable(h.c_str())) {
				snprintf(mAddrInput, sizeof(mAddrInput), "%s", h.c_str());
				NavigateToAddress(mAddrInput);
			}
		}
		ImGui::EndCombo();
	}

	ImGui::SameLine();
	ImGui::Spacing();
	ImGui::SameLine();

	// Column preset dropdown
	{
		char colBuf[16];
		snprintf(colBuf, sizeof(colBuf), "%u", mColumns);

		ImGui::SetNextItemWidth(70);
		if (ImGui::BeginCombo("##cols", colBuf)) {
			for (auto c : kStdColumnCounts) {
				char label[16];
				snprintf(label, sizeof(label), "%u", c);
				if (ImGui::Selectable(label, c == mColumns))
					SetColumns(c);
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		ImGui::TextUnformatted("cols");
	}

	ImGui::SameLine();
	ImGui::Spacing();
	ImGui::SameLine();

	// Value mode selector
	const char *vmLabels[] = { "Hex Bytes", "Hex Words", "Dec Bytes", "Dec Words" };
	ImGui::SetNextItemWidth(100);
	int vm = (int)mValueMode;
	if (ImGui::Combo("##vmode", &vm, vmLabels, 4)) {
		mValueMode = (ValueMode)vm;
		SetColumns(mColumns);
		CancelEdit();
	}

	ImGui::SameLine();

	// Interpret mode selector
	static const char *imLabels[] = {
		"None", "ATASCII", "Internal",
		"1-bpp Font", "2-bpp Font",
		"1-bpp Gfx", "2-bpp Gfx", "4-bpp Gfx", "8-bpp Gfx"
	};
	ImGui::SetNextItemWidth(100);
	int im = (int)mInterpretMode;
	if (ImGui::Combo("##imode", &im, imLabels, 9)) {
		mInterpretMode = (InterpretMode)im;
		SetColumns(mColumns);
	}
}

// =========================================================================
// Top-level Render
// =========================================================================

bool ATImGuiMemoryPaneImpl::Render() {
	mbEditCancelledThisFrame = false;
	bool open = true;

	if (mbFocusRequested) {
		ImGui::SetNextWindowFocus();
		mbFocusRequested = false;
	}

	ImGui::SetNextWindowSize(ImVec2(500, 350), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(mTitle.c_str(), &open)) {
		mbHasFocus = false;
		ImGui::End();
		return open;
	}
	mbHasFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

	if (mbNeedsRebuild)
		RebuildView();

	if (!mbStateValid || mLastState.mbRunning) {
		ImGui::TextDisabled(mLastState.mbRunning ? "(running)" : "(no state)");
		ImGui::End();
		return open;
	}

	// Calculate visible rows from window height
	float contentH = ImGui::GetContentRegionAvail().y
		- ImGui::GetFrameHeightWithSpacing() * 2;
	float lineH = ImGui::GetTextLineHeightWithSpacing();
	if (lineH > 0)
		mVisibleRows = std::max<uint32>((uint32)(contentH / lineH), 4);

	// Ensure we have enough data
	if (mViewData.size() != mColumns * mVisibleRows)
		mbNeedsRebuild = true;
	if (mbNeedsRebuild)
		RebuildView();

	RenderAddressBar();
	ImGui::Separator();
	RenderHexDump();

	ImGui::End();
	return open;
}

// =========================================================================
// Registration — 4 instances
// =========================================================================

void ATUIDebuggerEnsureMemoryPane(int index) {
	uint32 paneId = kATUIPaneId_MemoryN + (uint32)index;
	if (!ATUIDebuggerGetPane(paneId)) {
		auto *pane = new ATImGuiMemoryPaneImpl(paneId, index);
		ATUIDebuggerRegisterPane(pane);
	}
}
