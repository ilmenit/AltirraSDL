//	AltirraSDL - Dear ImGui debugger memory viewer pane
//	Replaces Win32 ATMemoryWindow (uidbgmemory.cpp).
//	Supports 4 instances, hex/dec byte/word display, ATASCII interpretation,
//	address navigation with expression evaluation, inline editing, change
//	detection, address space selection, and context menu with breakpoints.

#include <stdafx.h>
#include <algorithm>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <at/atcore/address.h>
#include <at/atdebugger/target.h>
#include "ui_debugger.h"
#include "console.h"
#include "debugger.h"
#include "simulator.h"

extern ATSimulator g_sim;

// =========================================================================
// Memory pane — up to 4 instances (Memory 1 through Memory 4)
// =========================================================================

class ATImGuiMemoryPaneImpl final : public ATImGuiDebuggerPane {
public:
	ATImGuiMemoryPaneImpl(uint32 paneId, int instanceIndex);

	bool Render() override;
	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) override;
	void OnDebuggerEvent(ATDebugEvent eventId) override;

private:
	void RebuildView();
	void RenderAddressBar();
	void RenderHexDump();
	void CommitEdit();
	void NavigateToAddress(const char *expr);

	enum class ValueMode : uint8 { HexBytes, HexWords, DecBytes, DecWords };
	enum class InterpretMode : uint8 { None, Atascii, Internal };

	int mInstanceIndex;
	uint32 mViewStart = 0;
	uint32 mColumns = 16;
	uint32 mVisibleRows = 16;
	bool mbNeedsRebuild = true;

	ValueMode mValueMode = ValueMode::HexBytes;
	InterpretMode mInterpretMode = InterpretMode::Atascii;

	// Address bar input
	char mAddrInput[64] = {};
	bool mbAddrInputActive = false;

	// Current view data and change detection
	std::vector<uint8> mViewData;
	std::vector<uint8> mPrevData;
	std::vector<bool> mChanged;
	uint32 mLastCycle = 0;

	// Context menu
	uint32 mContextMenuAddr = 0;

	// Editing state
	sint32 mEditOffset = -1;	// -1 = not editing
	int mEditNibble = 0;		// 0 = high nibble, 1 = low nibble
	uint8 mEditValue = 0;

	// Address space selection
	static constexpr struct { const char *name; uint32 base; } kAddressSpaces[] = {
		{ "CPU",     kATAddressSpace_CPU },
		{ "ANTIC",   kATAddressSpace_ANTIC },
		{ "VBXE",    kATAddressSpace_VBXE },
		{ "Ext RAM", kATAddressSpace_EXTRAM },
		{ "RAM",     kATAddressSpace_RAM },
		{ "ROM",     kATAddressSpace_ROM },
		{ "Cart",    kATAddressSpace_CART },
		{ "PORTB",   kATAddressSpace_PORTB },
	};
	int mAddrSpaceIdx = 0;
};

constexpr decltype(ATImGuiMemoryPaneImpl::kAddressSpaces) ATImGuiMemoryPaneImpl::kAddressSpaces;

ATImGuiMemoryPaneImpl::ATImGuiMemoryPaneImpl(uint32 paneId, int instanceIndex)
	: ATImGuiDebuggerPane(paneId, "Memory")
	, mInstanceIndex(instanceIndex)
{
	// Set title with instance number
	VDStringA title;
	title.sprintf("Memory %d", instanceIndex + 1);
	mTitle = title;
}

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

	// Save previous data for change detection
	mPrevData = mViewData;

	mViewData.resize(totalBytes);
	mChanged.assign(totalBytes, false);

	// Read memory from target
	for (uint32 i = 0; i < totalBytes; ++i) {
		uint32 addr = spaceBase | ((offset + i) & spaceMask);
		mViewData[i] = target->DebugReadByte(addr);
	}

	// Detect changes
	if (mPrevData.size() == mViewData.size()) {
		for (uint32 i = 0; i < totalBytes; ++i)
			mChanged[i] = (mViewData[i] != mPrevData[i]);
	} else {
		std::fill(mChanged.begin(), mChanged.end(), false);
	}
}

void ATImGuiMemoryPaneImpl::NavigateToAddress(const char *expr) {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg) return;

	try {
		sint32 addr = dbg->EvaluateThrow(expr);
		mViewStart = (uint32)addr;
		mbNeedsRebuild = true;
		mEditOffset = -1;
	} catch (const MyError&) {
		// Invalid expression — ignore
	}
}

void ATImGuiMemoryPaneImpl::CommitEdit() {
	if (mEditOffset < 0) return;

	IATDebugTarget *target = mLastState.mpDebugTarget;
	if (!target) { mEditOffset = -1; return; }

	uint32 spaceSize = ATAddressGetSpaceSize(mViewStart);
	uint32 spaceMask = spaceSize - 1;
	uint32 spaceBase = mViewStart & kATAddressSpaceMask;
	uint32 offset = mViewStart & kATAddressOffsetMask;

	uint32 addr = spaceBase | ((offset + mEditOffset) & spaceMask);
	target->WriteByte(addr, mEditValue);

	mbNeedsRebuild = true;
}

void ATImGuiMemoryPaneImpl::RenderAddressBar() {
	// Address space selector
	ImGui::SetNextItemWidth(80);
	if (ImGui::BeginCombo("##space", kAddressSpaces[mAddrSpaceIdx].name)) {
		for (int i = 0; i < (int)(sizeof(kAddressSpaces) / sizeof(kAddressSpaces[0])); ++i) {
			if (ImGui::Selectable(kAddressSpaces[i].name, i == mAddrSpaceIdx)) {
				mAddrSpaceIdx = i;
				mViewStart = kAddressSpaces[i].base;
				mbNeedsRebuild = true;
				mEditOffset = -1;
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();

	// Address input
	ImGui::SetNextItemWidth(120);
	if (ImGui::InputText("##addr", mAddrInput, sizeof(mAddrInput),
			ImGuiInputTextFlags_EnterReturnsTrue)) {
		NavigateToAddress(mAddrInput);
	}
	ImGui::SameLine();
	if (ImGui::Button("Go"))
		NavigateToAddress(mAddrInput);

	ImGui::SameLine();
	ImGui::Spacing();
	ImGui::SameLine();

	// Columns selector
	ImGui::SetNextItemWidth(60);
	int cols = (int)mColumns;
	if (ImGui::InputInt("##cols", &cols, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
		cols = std::clamp(cols, 1, 256);
		if (mValueMode == ValueMode::HexWords || mValueMode == ValueMode::DecWords)
			cols = (cols + 1) & ~1;
		mColumns = (uint32)cols;
		mbNeedsRebuild = true;
	}
	ImGui::SameLine();
	ImGui::TextUnformatted("cols");

	ImGui::SameLine();
	ImGui::Spacing();
	ImGui::SameLine();

	// Value mode selector
	const char *vmLabels[] = { "Hex Bytes", "Hex Words", "Dec Bytes", "Dec Words" };
	ImGui::SetNextItemWidth(100);
	int vm = (int)mValueMode;
	if (ImGui::Combo("##vmode", &vm, vmLabels, 4)) {
		mValueMode = (ValueMode)vm;
		if (mValueMode == ValueMode::HexWords || mValueMode == ValueMode::DecWords)
			mColumns = (mColumns + 1) & ~1;
		mbNeedsRebuild = true;
		mEditOffset = -1;
	}

	ImGui::SameLine();

	// Interpret mode selector
	const char *imLabels[] = { "None", "ATASCII", "Internal" };
	ImGui::SetNextItemWidth(90);
	int im = (int)mInterpretMode;
	if (ImGui::Combo("##imode", &im, imLabels, 3)) {
		mInterpretMode = (InterpretMode)im;
	}
}

void ATImGuiMemoryPaneImpl::RenderHexDump() {
	if (mViewData.empty())
		return;

	IATDebugger *dbg = ATGetDebugger();
	if (!dbg) return;

	uint32 spaceSize = ATAddressGetSpaceSize(mViewStart);
	uint32 spaceMask = spaceSize - 1;
	uint32 spaceBase = mViewStart & kATAddressSpaceMask;
	uint32 offset = mViewStart & kATAddressOffsetMask;
	uint32 totalBytes = (uint32)mViewData.size();

	const float charW = ImGui::CalcTextSize("0").x;
	const float lineH = ImGui::GetTextLineHeightWithSpacing();

	// Right-click context menu
	uint32 contextAddr = 0;
	bool contextAddrValid = false;

	if (ImGui::BeginChild("MemoryHex", ImVec2(0, 0), ImGuiChildFlags_None,
			ImGuiWindowFlags_HorizontalScrollbar)) {

		ImGuiListClipper clipper;
		uint32 rows = totalBytes / mColumns;
		if (rows == 0) rows = 1;
		clipper.Begin((int)rows);

		while (clipper.Step()) {
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
				uint32 rowOffset = (uint32)row * mColumns;
				if (rowOffset >= totalBytes) break;

				uint32 rowAddr = spaceBase | ((offset + rowOffset) & spaceMask);

				// Address label
				VDStringA addrText = dbg->GetAddressText(rowAddr, true);
				ImGui::TextUnformatted(addrText.c_str());
				ImGui::SameLine(0, charW);

				// Hex/Dec values
				uint32 bytesInRow = std::min(mColumns, totalBytes - rowOffset);

				if (mValueMode == ValueMode::HexBytes || mValueMode == ValueMode::DecBytes) {
					for (uint32 col = 0; col < bytesInRow; ++col) {
						uint32 idx = rowOffset + col;
						uint8 val = mViewData[idx];
						bool changed = (idx < mChanged.size()) && mChanged[idx];
						bool editing = ((sint32)idx == mEditOffset);

						if (changed)
							ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.5f, 1.0f, 1.0f));

						char buf[8];
						if (mValueMode == ValueMode::HexBytes)
							snprintf(buf, sizeof(buf), "%02X", val);
						else
							snprintf(buf, sizeof(buf), "%3u", val);

						ImGui::SameLine(0, charW * 0.5f);

						if (editing) {
							// Show editing value with underscore cursor
							// nibble 0: awaiting high nibble → "_X" showing original low nibble
							// nibble 1: high nibble typed    → "X_" showing typed high nibble
							char editBuf[8];
							if (mEditNibble == 0)
								snprintf(editBuf, sizeof(editBuf), "_%X", mEditValue & 0x0F);
							else
								snprintf(editBuf, sizeof(editBuf), "%X_", (mEditValue >> 4) & 0x0F);
							ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
							ImGui::TextUnformatted(editBuf);
							ImGui::PopStyleColor();
						} else {
							// Clickable byte
							ImGui::PushID((int)idx);
							if (ImGui::Selectable(buf, false, ImGuiSelectableFlags_None,
									ImGui::CalcTextSize(buf))) {
								mEditOffset = (sint32)idx;
								mEditNibble = 0;
								mEditValue = val;
							}
							// Right-click for context menu
							if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
								contextAddr = spaceBase | ((offset + idx) & spaceMask);
								contextAddrValid = true;
							}
							ImGui::PopID();
						}

						if (changed)
							ImGui::PopStyleColor();
					}
				} else {
					// Word mode
					for (uint32 col = 0; col + 1 < bytesInRow; col += 2) {
						uint32 idx = rowOffset + col;
						uint16 val = mViewData[idx] | ((uint16)mViewData[idx + 1] << 8);
						bool changed = ((idx < mChanged.size()) && mChanged[idx]) ||
									   ((idx + 1 < mChanged.size()) && mChanged[idx + 1]);

						if (changed)
							ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.5f, 1.0f, 1.0f));

						char buf[8];
						if (mValueMode == ValueMode::HexWords)
							snprintf(buf, sizeof(buf), "%04X", val);
						else
							snprintf(buf, sizeof(buf), "%5u", val);

						ImGui::SameLine(0, charW * 0.5f);
						ImGui::TextUnformatted(buf);

						if (changed)
							ImGui::PopStyleColor();
					}
				}

				// Interpretation column
				if (mInterpretMode != InterpretMode::None) {
					ImGui::SameLine(0, charW * 2);
					ImGui::TextUnformatted("|");
					ImGui::SameLine(0, 0);

					char interpBuf[260];
					uint32 len = std::min(bytesInRow, (uint32)256);
					for (uint32 i = 0; i < len; ++i) {
						uint8 ch = mViewData[rowOffset + i];

						if (mInterpretMode == InterpretMode::Internal) {
							// Internal → ATASCII transform (matches uidbgmemory.cpp:1102)
							static constexpr uint8 kXorTab[] = { 0x20, 0x60, 0x40, 0x00 };
							ch ^= kXorTab[(ch >> 5) & 3];
						}

						// ATASCII printable range
						if (ch >= 0x20 && ch < 0x7F)
							interpBuf[i] = (char)ch;
						else
							interpBuf[i] = '.';
					}
					interpBuf[len] = '\0';

					ImGui::TextUnformatted(interpBuf, interpBuf + len);
					ImGui::SameLine(0, 0);
					ImGui::TextUnformatted("|");
				}

				ImGui::NewLine();
			}
		}

		// Handle keyboard input for hex editing.
		// Only accept hex keys when the child window is focused and no
		// ImGui text input widget (like the address bar) is active.
		if (mEditOffset >= 0 && ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput) {
			for (ImGuiKey key = ImGuiKey_0; key <= ImGuiKey_9; key = (ImGuiKey)(key + 1)) {
				if (ImGui::IsKeyPressed(key)) {
					int digit = key - ImGuiKey_0;
					if (mEditNibble == 0) {
						mEditValue = (uint8)(digit << 4) | (mEditValue & 0x0F);
						mEditNibble = 1;
					} else {
						mEditValue = (mEditValue & 0xF0) | (uint8)digit;
						CommitEdit();
						// Advance to next byte
						if ((uint32)(mEditOffset + 1) < totalBytes) {
							mEditOffset++;
							mEditNibble = 0;
							mEditValue = mViewData[mEditOffset];
						} else {
							mEditOffset = -1;
						}
					}
				}
			}
			for (ImGuiKey key = ImGuiKey_A; key <= ImGuiKey_F; key = (ImGuiKey)(key + 1)) {
				if (ImGui::IsKeyPressed(key)) {
					int digit = 10 + (key - ImGuiKey_A);
					if (mEditNibble == 0) {
						mEditValue = (uint8)(digit << 4) | (mEditValue & 0x0F);
						mEditNibble = 1;
					} else {
						mEditValue = (mEditValue & 0xF0) | (uint8)digit;
						CommitEdit();
						if ((uint32)(mEditOffset + 1) < totalBytes) {
							mEditOffset++;
							mEditNibble = 0;
							mEditValue = mViewData[mEditOffset];
						} else {
							mEditOffset = -1;
						}
					}
				}
			}
			if (ImGui::IsKeyPressed(ImGuiKey_Escape))
				mEditOffset = -1;
			if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && mEditOffset > 0) {
				mEditOffset--;
				mEditNibble = 0;
				mEditValue = mViewData[mEditOffset];
			}
			if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && (uint32)(mEditOffset + 1) < totalBytes) {
				mEditOffset++;
				mEditNibble = 0;
				mEditValue = mViewData[mEditOffset];
			}
			if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && mEditOffset >= (sint32)mColumns) {
				mEditOffset -= (sint32)mColumns;
				mEditNibble = 0;
				mEditValue = mViewData[mEditOffset];
			}
			if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && (uint32)(mEditOffset + mColumns) < totalBytes) {
				mEditOffset += (sint32)mColumns;
				mEditNibble = 0;
				mEditValue = mViewData[mEditOffset];
			}
		}

		// Scrolling: Page Up/Down — operate on offset only to preserve
		// address space bits in the upper nibble.
		if (ImGui::IsWindowFocused()) {
			if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
				uint32 spBase = mViewStart & kATAddressSpaceMask;
				uint32 off = mViewStart & kATAddressOffsetMask;
				uint32 scrollBytes = mColumns * mVisibleRows;
				off = (off >= scrollBytes) ? off - scrollBytes : 0;
				mViewStart = spBase | off;
				mbNeedsRebuild = true;
				mEditOffset = -1;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
				uint32 spBase = mViewStart & kATAddressSpaceMask;
				uint32 off = mViewStart & kATAddressOffsetMask;
				uint32 scrollBytes = mColumns * mVisibleRows;
				uint32 maxOff = ATAddressGetSpaceSize(mViewStart) - 1;
				off = std::min(off + scrollBytes, maxOff);
				mViewStart = spBase | off;
				mbNeedsRebuild = true;
				mEditOffset = -1;
			}
		}

		// Context menu — must be in same window scope as OpenPopup
		if (contextAddrValid) {
			mContextMenuAddr = contextAddr;
			ImGui::OpenPopup("MemCtx");
		}
		if (ImGui::BeginPopup("MemCtx")) {
			IATDebugger *d = ATGetDebugger();
			if (d) {
				VDStringA addrStr = d->GetAddressText(mContextMenuAddr, true, true);
				ImGui::TextDisabled("%s", addrStr.c_str());
				ImGui::Separator();

				if (ImGui::MenuItem("Toggle Read Breakpoint"))
					d->ToggleAccessBreakpoint(mContextMenuAddr, false);
				if (ImGui::MenuItem("Toggle Write Breakpoint"))
					d->ToggleAccessBreakpoint(mContextMenuAddr, true);
			}
			ImGui::EndPopup();
		}

		// Mouse wheel scroll
		float wheel = ImGui::GetIO().MouseWheel;
		if (wheel != 0 && ImGui::IsWindowHovered()) {
			sint32 scrollRows = -(sint32)(wheel * 3);
			sint32 newOffset = (sint32)(mViewStart & kATAddressOffsetMask) + scrollRows * (sint32)mColumns;
			if (newOffset < 0) newOffset = 0;
			uint32 maxAddr = ATAddressGetSpaceSize(mViewStart) - 1;
			if ((uint32)newOffset > maxAddr)
				newOffset = (sint32)maxAddr;
			mViewStart = (mViewStart & kATAddressSpaceMask) | (uint32)newOffset;
			mbNeedsRebuild = true;
		}
	}
	ImGui::EndChild();
}

bool ATImGuiMemoryPaneImpl::Render() {
	bool open = true;

	if (mbFocusRequested) {
		ImGui::SetNextWindowFocus();
		mbFocusRequested = false;
	}

	ImGui::SetNextWindowSize(ImVec2(500, 350), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(mTitle.c_str(), &open)) {
		ImGui::End();
		return open;
	}

	if (mbNeedsRebuild)
		RebuildView();

	if (!mbStateValid || mLastState.mbRunning) {
		ImGui::TextDisabled(mLastState.mbRunning ? "(running)" : "(no state)");
		ImGui::End();
		return open;
	}

	// Calculate visible rows from window height
	float contentH = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 2;
	float lineH = ImGui::GetTextLineHeightWithSpacing();
	if (lineH > 0)
		mVisibleRows = std::max<uint32>((uint32)(contentH / lineH), 4);

	// Ensure we have enough data
	uint32 needed = mColumns * mVisibleRows;
	if (mViewData.size() != needed)
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
