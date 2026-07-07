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
#include "gl_helpers.h"
#include "console.h"
#include "debugger.h"
#include "simulator.h"
#include "logging.h"

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
	if (mBitmapGLTexture) {
		glDeleteTextures(1, &mBitmapGLTexture);
		mBitmapGLTexture = 0;
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

	// When the debugger cycle changes (step, break), snapshot the current
	// mViewData as the reference for change detection.  This matches
	// Windows' RemakeView cycle-change snapshot (uidbgmemory.cpp:1325-1330).
	if (state.mCycle != mLastCycle) {
		mRefData = mViewData;
		mRefViewStart = mViewStart;
		mRefCycle = mLastCycle;
		mLastCycle = state.mCycle;
	}

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

	LOG_INFO("Debugger", "visRows=%u cols=%u dataSize=%zu", mVisibleRows, mColumns, mViewData.size());

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

	mViewData.resize(totalBytes);
	mChanged.assign(totalBytes, false);

	// Bulk read with wrap-around handling
	uint32 normOff = offset & spaceMask;
	uint32 firstChunk = std::min(totalBytes, spaceSize - normOff);
	target->DebugReadMemory(spaceBase | normOff, mViewData.data(), firstChunk);
	if (firstChunk < totalBytes)
		target->DebugReadMemory(spaceBase, mViewData.data() + firstChunk,
								totalBytes - firstChunk);

	// Change detection — compare current data against the reference
	// snapshot from the previous debugger cycle.  The reference is only
	// updated when the cycle changes (OnDebuggerSystemStateUpdate), so
	// change highlighting persists across repaints, scrolling, and resizes
	// within the same cycle.  This matches Windows' mRefViewData mechanism.
	if (!mRefData.empty()
		&& (mRefViewStart & kATAddressSpaceMask) == spaceBase) {
		uint32 refOff = mRefViewStart & kATAddressOffsetMask;
		uint32 refSize = (uint32)mRefData.size();

		for (uint32 i = 0; i < totalBytes; ++i) {
			// Map view index i to an offset within the reference data
			uint32 addr = (offset + i) & spaceMask;
			uint32 refIdx = (addr - refOff) & spaceMask;

			if (refIdx < refSize)
				mChanged[i] = (mViewData[i] != mRefData[refIdx]);
		}
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
	uint32 spaceSize = ATAddressGetSpaceSize(mViewStart);
	uint32 visibleBytes = mColumns * mVisibleRows;
	if (visibleBytes > spaceSize)
		visibleBytes = spaceSize;

	if (visibleBytes) {
		uint32 spaceMask = spaceSize - 1;
		uint32 relOff = (addrOff - viewOff) & spaceMask;
		if (relOff < visibleBytes)
			return;
	}

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

void ATImGuiMemoryPaneImpl::SetHighlightedAddress(
	std::optional<uint32> highlightAddr,
	bool highlightData,
	bool select)
{
	mHighlightedAddress = highlightAddr;
	mbHighlightedData = highlightData;
	mbSelectionEnabled = select;
	mEditValue = -1;
	mEditPhase = 0;

	if (mHighlightedAddress.has_value())
		EnsureHighlightVisible();
}

void ATImGuiMemoryPaneImpl::AdvanceSelectionHorizontal(int dir) {
	if (!mHighlightedAddress.has_value())
		return;

	const uint32 hiAddr = mHighlightedAddress.value();
	const uint32 step = (IsWordMode() && !mbHighlightedData) ? 2 : 1;
	const uint32 spaceBase = hiAddr & kATAddressSpaceMask;
	const uint32 off = hiAddr & kATAddressOffsetMask;
	const uint32 spaceSize = ATAddressGetSpaceSize(hiAddr);
	if (!spaceSize)
		return;

	uint32 newOff = off;
	if (dir < 0) {
		if (off < step)
			return;
		newOff = off - step;
	} else if (dir > 0) {
		const uint32 maxOff = spaceSize - 1;
		if (off + step > maxOff)
			return;
		newOff = off + step;
	} else {
		return;
	}

	SetHighlightedAddress(spaceBase | newOff, mbHighlightedData, true);
}

void ATImGuiMemoryPaneImpl::MoveSelectionHorizontal(int dir) {
	if (!mHighlightedAddress.has_value())
		return;

	CommitEdit();
	AdvanceSelectionHorizontal(dir);
}

void ATImGuiMemoryPaneImpl::MoveSelectionVertical(int dir) {
	if (!mHighlightedAddress.has_value())
		return;

	CommitEdit();

	const uint32 hiAddr = mHighlightedAddress.value();
	const uint32 spaceBase = hiAddr & kATAddressSpaceMask;
	const uint32 off = hiAddr & kATAddressOffsetMask;

	if (dir < 0) {
		if (off >= mColumns) {
			SetHighlightedAddress(spaceBase | (off - mColumns),
				mbHighlightedData,
				true);
		}
	} else if (dir > 0) {
		const uint32 maxOff = ATAddressGetSpaceSize(hiAddr) - 1;
		if (off + mColumns <= maxOff) {
			SetHighlightedAddress(spaceBase | (off + mColumns),
				mbHighlightedData,
				true);
		}
	}
}

void ATImGuiMemoryPaneImpl::ScrollViewRows(int rows) {
	const uint32 spaceBase = mViewStart & kATAddressSpaceMask;
	const uint32 off = mViewStart & kATAddressOffsetMask;
	const uint32 maxOff = ATAddressGetSpaceSize(mViewStart) - 1;

	sint64 newOff = (sint64)off + (sint64)rows * (sint64)mColumns;
	if (newOff < 0)
		newOff = 0;
	else if ((uint64)newOff > maxOff)
		newOff = maxOff;

	mViewStart = spaceBase | (uint32)newOff;
	mbNeedsRebuild = true;
}

void ATImGuiMemoryPaneImpl::ScrollViewPage(int dir) {
	const uint32 spaceBase = mViewStart & kATAddressSpaceMask;
	const uint32 off = mViewStart & kATAddressOffsetMask;
	const uint32 columnOffset = mColumns ? off % mColumns : 0;
	const uint32 scrollBytes = mColumns * mVisibleRows;

	uint32 newOff = off;
	if (dir < 0) {
		newOff = (off >= scrollBytes) ? off - scrollBytes : columnOffset;
	} else if (dir > 0) {
		const uint32 maxOff = ATAddressGetSpaceSize(mViewStart) - 1;
		newOff = std::min(off + scrollBytes, maxOff);
	}

	mViewStart = spaceBase | newOff;
	mbNeedsRebuild = true;
}

bool ATImGuiMemoryPaneImpl::TestEnsureHighlightVisible(uint32 viewStart,
	uint32 columns,
	uint32 visibleRows,
	uint32 highlightAddr,
	uint32& outViewStart)
{
	mViewStart = viewStart;
	mColumns = std::max<uint32>(columns, 1);
	mVisibleRows = std::max<uint32>(visibleRows, 1);
	SetHighlightedAddress(highlightAddr, false, true);
	EnsureHighlightVisible();
	outViewStart = mViewStart;
	return true;
}

bool ATImGuiMemoryPaneImpl::TestNavigation(uint32 viewStart,
	uint32 columns,
	uint32 visibleRows,
	uint32 highlightAddr,
	bool dataColumn,
	const char *op,
	uint32& outViewStart,
	uint32& outHighlightAddr,
	bool& outDataColumn)
{
	mViewStart = viewStart;
	mColumns = std::max<uint32>(columns, 1);
	mVisibleRows = std::max<uint32>(visibleRows, 1);
	SetHighlightedAddress(highlightAddr, dataColumn, true);
	mEditValue = -1;
	mEditPhase = 0;
	mbNeedsRebuild = false;

	if (!strcmp(op, "left"))
		MoveSelectionHorizontal(-1);
	else if (!strcmp(op, "right"))
		MoveSelectionHorizontal(1);
	else if (!strcmp(op, "up"))
		MoveSelectionVertical(-1);
	else if (!strcmp(op, "down"))
		MoveSelectionVertical(1);
	else if (!strcmp(op, "ctrl_up"))
		ScrollViewRows(-1);
	else if (!strcmp(op, "ctrl_down"))
		ScrollViewRows(1);
	else if (!strcmp(op, "page_up"))
		ScrollViewPage(-1);
	else if (!strcmp(op, "page_down"))
		ScrollViewPage(1);
	else if (!strcmp(op, "enter")) {
		CommitEdit();
		MoveSelectionHorizontal(1);
	} else if (!strcmp(op, "tab")) {
		CommitEdit();
		mbHighlightedData = !mbHighlightedData;
	} else {
		return false;
	}

	outViewStart = mViewStart;
	outHighlightAddr = mHighlightedAddress.value_or(0);
	outDataColumn = mbHighlightedData;
	return true;
}

bool ATImGuiMemoryPaneImpl::TestHexByteEdit(uint32 addr,
	uint8 value,
	uint8& outBefore,
	uint8& outAfter)
{
	IATDebugger *dbg = ATGetDebugger();
	IATDebugTarget *target = dbg ? dbg->GetTarget() : nullptr;
	if (!target)
		return false;

	mLastState.mpDebugTarget = target;
	mbStateValid = true;
	mValueMode = ValueMode::HexBytes;
	SetHighlightedAddress(addr, false, true);
	mEditValue = -1;
	mEditPhase = 0;

	outBefore = target->DebugReadByte(addr);

	BeginEdit();
	if (mEditValue < 0)
		return false;

	mEditValue = value;
	mEditPhase = 2;
	CommitEdit();

	outAfter = target->DebugReadByte(addr);
	return true;
}

bool ATImGuiMemoryPaneImpl::TestHexDigitAutoAdvance(int modeValue,
	uint32 viewStart,
	uint32 columns,
	uint32 visibleRows,
	uint32 addr,
	const char *digits,
	uint16& outValue,
	uint32& outSelectedAddr,
	uint32& outViewStart)
{
	IATDebugger *dbg = ATGetDebugger();
	IATDebugTarget *target = dbg ? dbg->GetTarget() : nullptr;
	if (!target || !digits)
		return false;

	if (modeValue != (int)ValueMode::HexBytes
		&& modeValue != (int)ValueMode::HexWords)
		return false;

	const ValueMode mode = (ValueMode)modeValue;

	mLastState.mpDebugTarget = target;
	mbStateValid = true;
	mViewStart = viewStart;
	mColumns = std::max<uint32>(columns, 1);
	mVisibleRows = std::max<uint32>(visibleRows, 1);
	mValueMode = mode;
	SetHighlightedAddress(addr, false, true);
	mEditValue = -1;
	mEditPhase = 0;
	mbNeedsRebuild = false;

	const int maxPhase = IsWordMode() ? 4 : 2;
	int phaseCount = 0;
	for (const char *s = digits; *s; ++s) {
		int digit = -1;
		if (*s >= '0' && *s <= '9')
			digit = *s - '0';
		else if (*s >= 'a' && *s <= 'f')
			digit = *s - 'a' + 10;
		else if (*s >= 'A' && *s <= 'F')
			digit = *s - 'A' + 10;
		else
			return false;

		if (mEditValue < 0)
			BeginEdit();
		if (mEditValue < 0)
			return false;

		const int shift = 4 * ((maxPhase - 1) - mEditPhase);
		mEditValue = (mEditValue & ~(0xF << shift)) | (digit << shift);
		++mEditPhase;
		++phaseCount;

		if (mEditPhase >= maxPhase) {
			CommitEdit();
			AdvanceSelectionHorizontal(1);
		}
	}

	if (phaseCount != maxPhase)
		return false;

	uint8 buf[2] {};
	target->DebugReadMemory(addr, buf, IsWordMode() ? 2 : 1);
	outValue = buf[0] | ((uint16)buf[1] << 8);
	outSelectedAddr = mHighlightedAddress.value_or(0);
	outViewStart = mViewStart;
	return true;
}

bool ATImGuiMemoryPaneImpl::TestHexByteCancel(uint32 addr,
	uint8 value,
	uint8& outBefore,
	uint8& outAfter,
	uint32& outSelectedAddr,
	bool& outSelectionEnabled)
{
	IATDebugger *dbg = ATGetDebugger();
	IATDebugTarget *target = dbg ? dbg->GetTarget() : nullptr;
	if (!target)
		return false;

	mLastState.mpDebugTarget = target;
	mbStateValid = true;
	mValueMode = ValueMode::HexBytes;
	SetHighlightedAddress(addr, false, true);
	mEditValue = -1;
	mEditPhase = 0;

	outBefore = target->DebugReadByte(addr);

	BeginEdit();
	if (mEditValue < 0)
		return false;

	mEditValue = value;
	mEditPhase = 1;
	CancelEdit();

	outAfter = target->DebugReadByte(addr);
	outSelectedAddr = mHighlightedAddress.value_or(0);
	outSelectionEnabled = mbSelectionEnabled;
	return true;
}

bool ATImGuiMemoryPaneImpl::TestValueEdit(ValueMode mode,
	uint32 addr,
	uint16 value,
	uint16& outBefore,
	uint16& outAfter)
{
	IATDebugger *dbg = ATGetDebugger();
	IATDebugTarget *target = dbg ? dbg->GetTarget() : nullptr;
	if (!target)
		return false;

	mLastState.mpDebugTarget = target;
	mbStateValid = true;
	mValueMode = mode;
	SetHighlightedAddress(addr, false, true);
	mEditValue = -1;
	mEditPhase = 0;

	const bool wordMode = IsWordMode();
	const uint16 maxValue = wordMode ? 0xFFFF : 0x00FF;
	if (value > maxValue)
		return false;

	uint8 beforeBuf[2] {};
	target->DebugReadMemory(addr, beforeBuf, wordMode ? 2 : 1);
	outBefore = beforeBuf[0] | ((uint16)beforeBuf[1] << 8);

	BeginEdit();
	if (mEditValue < 0)
		return false;

	mEditValue = value;
	CommitEdit();

	uint8 afterBuf[2] {};
	target->DebugReadMemory(addr, afterBuf, wordMode ? 2 : 1);
	outAfter = afterBuf[0] | ((uint16)afterBuf[1] << 8);
	return true;
}

bool ATImGuiMemoryPaneImpl::TestTextEdit(InterpretMode mode,
	uint32 addr,
	uint8 ch,
	uint8& outBefore,
	uint8& outAfter,
	uint8& outWritten)
{
	IATDebugger *dbg = ATGetDebugger();
	IATDebugTarget *target = dbg ? dbg->GetTarget() : nullptr;
	if (!target)
		return false;

	if (mode != InterpretMode::Atascii && mode != InterpretMode::Internal)
		return false;

	mLastState.mpDebugTarget = target;
	mbStateValid = true;
	mInterpretMode = mode;
	SetHighlightedAddress(addr, true, true);
	mEditValue = -1;
	mEditPhase = 0;

	outBefore = target->DebugReadByte(addr);

	uint8 byte = ch;
	if (mode == InterpretMode::Internal) {
		switch (byte & 0x60) {
			case 0x20:
				byte ^= 0x20;
				break;
			case 0x40:
				byte ^= 0x60;
				break;
			case 0x60:
				break;
			default:
				return false;
		}
	}

	mEditValue = byte;
	CommitEdit();
	AdvanceSelectionHorizontal(1);

	outWritten = byte;
	outAfter = target->DebugReadByte(addr);
	return true;
}

bool ATImGuiMemoryPaneImpl::TestTextAutoAdvance(InterpretMode mode,
	uint32 viewStart,
	uint32 columns,
	uint32 visibleRows,
	uint32 addr,
	uint8 ch,
	uint8& outWritten,
	uint32& outSelectedAddr,
	uint32& outViewStart)
{
	IATDebugger *dbg = ATGetDebugger();
	IATDebugTarget *target = dbg ? dbg->GetTarget() : nullptr;
	if (!target)
		return false;

	if (mode != InterpretMode::Atascii && mode != InterpretMode::Internal)
		return false;

	mLastState.mpDebugTarget = target;
	mbStateValid = true;
	mViewStart = viewStart;
	mColumns = std::max<uint32>(columns, 1);
	mVisibleRows = std::max<uint32>(visibleRows, 1);
	mInterpretMode = mode;
	SetHighlightedAddress(addr, true, true);
	mEditValue = -1;
	mEditPhase = 0;
	mbNeedsRebuild = false;

	uint8 byte = ch;
	if (mode == InterpretMode::Internal) {
		switch (byte & 0x60) {
			case 0x20:
				byte ^= 0x20;
				break;
			case 0x40:
				byte ^= 0x60;
				break;
			case 0x60:
				break;
			default:
				return false;
		}
	}

	mEditValue = byte;
	CommitEdit();
	AdvanceSelectionHorizontal(1);

	outWritten = byte;
	outSelectedAddr = mHighlightedAddress.value_or(0);
	outViewStart = mViewStart;
	return true;
}

// =========================================================================
// Editing
// =========================================================================

void ATImGuiMemoryPaneImpl::BeginEdit() {
	if (!mHighlightedAddress.has_value() || !mbSelectionEnabled
		|| mEditValue >= 0)
		return;

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
	if (mEditValue < 0 || !mHighlightedAddress.has_value()
		|| !mbSelectionEnabled)
		return;

	IATDebugTarget *target = mLastState.mpDebugTarget;
	if (!target) { mEditValue = -1; return; }

	uint32 addr = mHighlightedAddress.value();

	// Update mViewData directly so the display reflects the edit
	// immediately, without waiting for a full rebuild.  This matches
	// Windows CommitEdit (uidbgmemory.cpp:1670-1678).
	sint32 viewIdx = AddrToIndex(addr);
	if (viewIdx >= 0 && (uint32)viewIdx < (uint32)mViewData.size()) {
		mViewData[viewIdx] = (uint8)(mEditValue & 0xFF);

		if (IsWordMode() && !mbHighlightedData
			&& (uint32)(viewIdx + 1) < (uint32)mViewData.size()) {
			mViewData[viewIdx + 1] = (uint8)((mEditValue >> 8) & 0xFF);
		}

		// Mark as changed for blue-highlight feedback
		if ((uint32)viewIdx < mChanged.size())
			mChanged[viewIdx] = true;
	}

	// Write to the emulated target memory
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
	mbEditCommittedThisFrame = true;
	// Note: intentionally NOT setting mbNeedsRebuild here.  mViewData
	// already contains the written value.  A full rebuild would re-read
	// memory and could revert the display if the write went to a
	// read-only region.  Windows also skips the rebuild on commit.
}

void ATImGuiMemoryPaneImpl::CancelEdit() {
	if (!mHighlightedAddress.has_value() || !mbSelectionEnabled)
		return;

	mEditValue = -1;
	mEditPhase = 0;
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
	mbEditCommittedThisFrame = false;
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

	// Calculate visible rows from window height.
	// Apply zoom factor to match the scaled font inside the hex dump child
	// (which uses SetWindowFontScale).  For font bitmap modes, rows are
	// enlarged to at least 24 px × zoom, rounded to a multiple of 8.
	float contentH = ImGui::GetContentRegionAvail().y
		- ImGui::GetFrameHeightWithSpacing() * 2;
	float lineH = ImGui::GetTextLineHeightWithSpacing() * mZoomFactor;
	if (IsFontMode()) {
		float fontH = std::max(ImGui::GetTextLineHeight() * mZoomFactor,
							   24.0f * mZoomFactor);
		fontH = std::ceil(fontH / 8.0f) * 8.0f;
		lineH = fontH;
	}
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

bool ATUIDebuggerEnsureMemoryHighlightVisibleForTest(uint32 viewStart,
	uint32 columns,
	uint32 visibleRows,
	uint32 highlightAddr,
	uint32& outViewStart)
{
	ATUIDebuggerOpen();
	ATActivateUIPane(kATUIPaneId_MemoryN, true, true);

	auto *pane = static_cast<ATImGuiMemoryPaneImpl *>(
		ATUIDebuggerGetPane(kATUIPaneId_MemoryN));
	if (!pane)
		return false;

	return pane->TestEnsureHighlightVisible(viewStart,
		columns,
		visibleRows,
			highlightAddr,
			outViewStart);
}

bool ATUIDebuggerMemoryNavigationForTest(uint32 viewStart,
	uint32 columns,
	uint32 visibleRows,
	uint32 highlightAddr,
	bool dataColumn,
	const char *op,
	uint32& outViewStart,
	uint32& outHighlightAddr,
	bool& outDataColumn)
{
	ATUIDebuggerOpen();
	ATActivateUIPane(kATUIPaneId_MemoryN, true, true);

	auto *pane = static_cast<ATImGuiMemoryPaneImpl *>(
		ATUIDebuggerGetPane(kATUIPaneId_MemoryN));
	if (!pane)
		return false;

	return pane->TestNavigation(viewStart,
		columns,
		visibleRows,
		highlightAddr,
		dataColumn,
		op,
		outViewStart,
		outHighlightAddr,
		outDataColumn);
}

bool ATUIDebuggerEditMemoryHexByteForTest(uint32 addr,
	uint8 value,
	uint8& outBefore,
	uint8& outAfter)
{
	ATUIDebuggerOpen();
	ATActivateUIPane(kATUIPaneId_MemoryN, true, true);

	auto *pane = static_cast<ATImGuiMemoryPaneImpl *>(
		ATUIDebuggerGetPane(kATUIPaneId_MemoryN));
	return pane ? pane->TestHexByteEdit(addr, value, outBefore, outAfter) : false;
}

bool ATUIDebuggerMemoryHexAutoAdvanceForTest(int mode,
	uint32 viewStart,
	uint32 columns,
	uint32 visibleRows,
	uint32 addr,
	const char *digits,
	uint16& outValue,
	uint32& outSelectedAddr,
	uint32& outViewStart)
{
	ATImGuiMemoryPaneImpl::ValueMode valueMode;
	switch(mode) {
		case 0:
			valueMode = ATImGuiMemoryPaneImpl::ValueMode::HexBytes;
			break;
		case 1:
			valueMode = ATImGuiMemoryPaneImpl::ValueMode::HexWords;
			break;
		default:
			return false;
	}

	ATUIDebuggerOpen();
	ATActivateUIPane(kATUIPaneId_MemoryN, true, true);

	auto *pane = static_cast<ATImGuiMemoryPaneImpl *>(
		ATUIDebuggerGetPane(kATUIPaneId_MemoryN));
	return pane ? pane->TestHexDigitAutoAdvance((int)valueMode,
		viewStart,
		columns,
		visibleRows,
		addr,
		digits,
		outValue,
		outSelectedAddr,
		outViewStart) : false;
}

bool ATUIDebuggerCancelMemoryHexByteEditForTest(uint32 addr,
	uint8 value,
	uint8& outBefore,
	uint8& outAfter,
	uint32& outSelectedAddr,
	bool& outSelectionEnabled)
{
	ATUIDebuggerOpen();
	ATActivateUIPane(kATUIPaneId_MemoryN, true, true);

	auto *pane = static_cast<ATImGuiMemoryPaneImpl *>(
		ATUIDebuggerGetPane(kATUIPaneId_MemoryN));
	return pane ? pane->TestHexByteCancel(addr,
		value,
		outBefore,
		outAfter,
		outSelectedAddr,
		outSelectionEnabled) : false;
}

bool ATUIDebuggerEditMemoryValueForTest(int mode,
	uint32 addr,
	uint16 value,
	uint16& outBefore,
	uint16& outAfter)
{
	if (mode < 0 || mode > (int)ATImGuiMemoryPaneImpl::ValueMode::DecWords)
		return false;

	ATUIDebuggerOpen();
	ATActivateUIPane(kATUIPaneId_MemoryN, true, true);

	auto *pane = static_cast<ATImGuiMemoryPaneImpl *>(
		ATUIDebuggerGetPane(kATUIPaneId_MemoryN));
	return pane ? pane->TestValueEdit(
		(ATImGuiMemoryPaneImpl::ValueMode)mode,
		addr,
		value,
		outBefore,
		outAfter) : false;
}

bool ATUIDebuggerEditMemoryTextForTest(int mode,
	uint32 addr,
	uint8 ch,
	uint8& outBefore,
	uint8& outAfter,
	uint8& outWritten)
{
	ATImGuiMemoryPaneImpl::InterpretMode interpretMode;
	switch(mode) {
		case 0:
			interpretMode = ATImGuiMemoryPaneImpl::InterpretMode::Atascii;
			break;
		case 1:
			interpretMode = ATImGuiMemoryPaneImpl::InterpretMode::Internal;
			break;
		default:
			return false;
	}

	ATUIDebuggerOpen();
	ATActivateUIPane(kATUIPaneId_MemoryN, true, true);

	auto *pane = static_cast<ATImGuiMemoryPaneImpl *>(
		ATUIDebuggerGetPane(kATUIPaneId_MemoryN));
	return pane ? pane->TestTextEdit(interpretMode,
		addr,
		ch,
		outBefore,
		outAfter,
		outWritten) : false;
}

bool ATUIDebuggerMemoryTextAutoAdvanceForTest(int mode,
	uint32 viewStart,
	uint32 columns,
	uint32 visibleRows,
	uint32 addr,
	uint8 ch,
	uint8& outWritten,
	uint32& outSelectedAddr,
	uint32& outViewStart)
{
	ATImGuiMemoryPaneImpl::InterpretMode interpretMode;
	switch(mode) {
		case 0:
			interpretMode = ATImGuiMemoryPaneImpl::InterpretMode::Atascii;
			break;
		case 1:
			interpretMode = ATImGuiMemoryPaneImpl::InterpretMode::Internal;
			break;
		default:
			return false;
	}

	ATUIDebuggerOpen();
	ATActivateUIPane(kATUIPaneId_MemoryN, true, true);

	auto *pane = static_cast<ATImGuiMemoryPaneImpl *>(
		ATUIDebuggerGetPane(kATUIPaneId_MemoryN));
	return pane ? pane->TestTextAutoAdvance(interpretMode,
		viewStart,
		columns,
		visibleRows,
		addr,
		ch,
		outWritten,
		outSelectedAddr,
		outViewStart) : false;
}
