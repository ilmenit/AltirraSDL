//	AltirraSDL - Dear ImGui debugger history pane
//	Replaces Win32 ATHistoryWindow + ATUIHistoryView.
//	Shows recently executed CPU instructions using a hierarchical tree
//	(ATHistoryTree + ATHistoryTreeBuilder) with full context menu.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/strutil.h>
#include <vd2/system/unknown.h>
#include <at/atcpu/history.h>
#include <at/atdebugger/historytree.h>
#include <at/atdebugger/historytreebuilder.h>
#include <at/atdebugger/target.h>
#include "ui_dbg_history.h"
#include "ui_debugger.h"
#include "console.h"
#include "debugger.h"
#include "disasm.h"
#include "simulator.h"
#include "cpu.h"

extern ATSimulator g_sim;
extern bool ATImGuiConsoleShowSource(uint32 addr);

// =========================================================================
// Construction
// =========================================================================

ATImGuiHistoryPaneImpl::ATImGuiHistoryPaneImpl()
	: ATImGuiDebuggerPane(kATUIPaneId_History, "History")
{
	mHistoryTreeBuilder.Init(&mHistoryTree);

	// Attach display settings with rebuild callback
	const auto redraw = [this] { /* redraw happens naturally each frame */ };

	mbShowPCAddress.Attach(g_ATDbgSettingHistoryShowPC, redraw);
	mbShowGlobalPCAddress.Attach(g_ATDbgSettingHistoryShowGlobalPC, redraw);
	mbShowRegisters.Attach(g_ATDbgSettingHistoryShowRegisters, redraw);
	mbShowSpecialRegisters.Attach(g_ATDbgSettingHistoryShowSpecialRegisters, redraw);
	mbShowFlags.Attach(g_ATDbgSettingHistoryShowFlags, redraw);
	mbShowCodeBytes.Attach(g_ATDbgSettingShowCodeBytes, redraw);
	mbShowLabels.Attach(g_ATDbgSettingShowLabels, redraw);
	mbShowLabelNamespaces.Attach(g_ATDbgSettingShowLabelNamespaces, redraw);

	// Collapse settings trigger full tree reload
	const auto reload = [this] { mbNeedsReload = true; };

	mbCollapseLoops.Attach(g_ATDbgSettingCollapseLoops, reload);
	mbCollapseCalls.Attach(g_ATDbgSettingCollapseCalls, reload);
	mbCollapseInterrupts.Attach(g_ATDbgSettingCollapseInterrupts, reload);
}

// =========================================================================
// Event handlers
// =========================================================================

void ATImGuiHistoryPaneImpl::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	ATImGuiDebuggerPane::OnDebuggerSystemStateUpdate(state);

	// Only update when stopped
	if (!state.mbRunning)
		mbNeedsUpdate = true;
}

void ATImGuiHistoryPaneImpl::OnDebuggerEvent(ATDebugEvent eventId) {
	mbNeedsUpdate = true;
}

// =========================================================================
// Tree management
// =========================================================================

void ATImGuiHistoryPaneImpl::ClearAllNodes() {
	mSelectedLine = {};
	mHistoryTree.Clear();
	mHistoryTreeBuilder.Reset();
	mpPreviewNode = nullptr;
	mInsnBuffer.clear();
	mbSearchActive = false;
}

void ATImGuiHistoryPaneImpl::Reset() {
	ClearAllNodes();
	mInsnPosStart = 0;
	mInsnPosEnd = 0;
}

void ATImGuiHistoryPaneImpl::ReloadOpcodes() {
	const uint32 hstart = mInsnPosStart;
	const uint32 hend = mInsnPosEnd;
	Reset();
	UpdateOpcodesRange(hstart, hend);
}

void ATImGuiHistoryPaneImpl::CheckDisasmMode() {
	const bool historyEnabled = g_sim.GetCPU().IsHistoryEnabled();
	mbHistoryEnabled = historyEnabled;
	if (!historyEnabled)
		return;

	IATDebugger *debugger = ATGetDebugger();
	if (!debugger)
		return;

	IATDebugTarget *target = debugger->GetTarget();
	if (!target)
		return;

	ATDebugDisasmMode disasmMode = target->GetDisasmMode();
	uint32 subCycles = 1;
	bool decodeAnticNMI = false;

	if (!debugger->GetTargetIndex()) {
		subCycles = g_sim.GetCPU().GetSubCycles();
		decodeAnticNMI = true;
	}

	if (mDisasmMode != disasmMode || mSubCycles != subCycles || mbDecodeAnticNMI != decodeAnticNMI) {
		mDisasmMode = disasmMode;
		mSubCycles = subCycles;
		mbDecodeAnticNMI = decodeAnticNMI;

		// Mode changed -- full rebuild
		Reset();
	}
}

void ATImGuiHistoryPaneImpl::UpdateOpcodes() {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	IATDebugTarget *target = dbg->GetTarget();
	if (!target)
		return;

	IATDebugTargetHistory *history = vdpoly_cast<IATDebugTargetHistory *>(target);
	if (!history)
		return;

	CheckDisasmMode();
	if (!mbHistoryEnabled)
		return;

	auto range = history->GetHistoryRange();
	UpdateOpcodesRange(range.first, range.second);
}

void ATImGuiHistoryPaneImpl::UpdateOpcodesRange(uint32 historyStart, uint32 historyEnd) {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	IATDebugTarget *target = dbg->GetTarget();
	if (!target)
		return;

	IATDebugTargetHistory *history = vdpoly_cast<IATDebugTargetHistory *>(target);
	if (!history)
		return;

	if (mInsnBuffer.empty()) {
		mInsnPosStart = historyStart;
		mInsnPosEnd = historyStart;
	}

	const ATHistoryTranslateInsnFn translateFn = ATHistoryGetTranslateInsnFn(mDisasmMode);

	// Refresh the last instruction's effective address after stepping.
	// The EA may not be available when the instruction is first recorded,
	// but becomes known after the instruction completes.
	if (!mInsnBuffer.empty()) {
		ATHTNode *lastNode = mpPreviewNode;

		if (lastNode) {
			while (lastNode && !lastNode->mpPrevSibling)
				lastNode = lastNode->mpParent;
			if (lastNode && lastNode->mpPrevSibling)
				lastNode = lastNode->mpPrevSibling;
			else
				lastNode = nullptr;
		} else {
			lastNode = mHistoryTree.GetBackNode();
		}

		while (lastNode && lastNode->mpLastChild)
			lastNode = lastNode->mpLastChild;

		if (lastNode && lastNode->mNodeType == kATHTNodeType_Insn) {
			if (lastNode->mInsn.mOffset + lastNode->mInsn.mCount == mInsnBuffer.size()) {
				const ATCPUHistoryEntry *helast = nullptr;
				if (history->ExtractHistory(&helast, mInsnPosEnd - 1, 1)) {
					ATCPUHistoryEntry& helast2 = mInsnBuffer.back();
					if (helast->mCycle == helast2.mCycle && helast->mEA != helast2.mEA) {
						helast2.mEA = helast->mEA;
					}
				}
			}
		}
	}

	uint32 dist = historyEnd - mInsnPosEnd;
	uint32 l = historyEnd - historyStart;

	ATHTNode *last = nullptr;
	bool heightChanged = false;

	if (dist > 0) {
		// Clear active search when new instructions arrive
		if (mbSearchActive) {
			Search(nullptr);
			mSearchBuf[0] = '\0';
		}

		// Remove preview node
		if (mpPreviewNode) {
			mHistoryTree.RemoveNode(mpPreviewNode);
			mpPreviewNode = nullptr;
		}

		// If too many new entries or buffer too large, full reset
		if (dist > l || mInsnBuffer.size() > 500000) {
			Reset();
			dist = l;
			mInsnPosEnd = historyEnd - l;
			mInsnPosStart = mInsnPosEnd;
		}

		mHistoryTreeBuilder.SetCollapseCalls(mbCollapseCalls);
		mHistoryTreeBuilder.SetCollapseInterrupts(mbCollapseInterrupts);
		mHistoryTreeBuilder.SetCollapseLoops(mbCollapseLoops);
		mHistoryTreeBuilder.BeginUpdate(dist <= 1000);

		const ATCPUHistoryEntry *htab[64];
		ATHistoryTraceInsn httab[64];
		uint32 hposnext = mInsnPosEnd;

		mInsnPosEnd += dist;
		while (dist) {
			uint32 batchSize = std::min<uint32>(dist, 64);
			batchSize = history->ExtractHistory(htab, hposnext, batchSize);

			if (!batchSize)
				break;

			hposnext += batchSize;
			dist -= batchSize;

			for (uint32 i = 0; i < batchSize; ++i)
				mInsnBuffer.push_back(*htab[i]);

			translateFn(httab, htab, batchSize);
			mHistoryTreeBuilder.Update(httab, batchSize);
		}

		mHistoryTreeBuilder.EndUpdate(last);
		heightChanged = true;
	}

	// Re-add preview node
	ATCPUHistoryEntry previewHe {};
	if (UpdatePreviewNode(previewHe)) {
		mPreviewNodeHEnt = previewHe;

		if (mpPreviewNode) {
			// Already exists -- just update the entry
		} else {
			if (!last)
				last = mHistoryTree.GetBackNode();

			// Don't insert inside a repeat node
			while (last && last->mNodeType == kATHTNodeType_Repeat)
				last = last->mpParent;

			mpPreviewNode = mHistoryTree.InsertNode(
				last ? last->mpParent : mHistoryTree.GetRootNode(),
				last, 0, kATHTNodeType_InsnPreview);
			heightChanged = true;
		}
		last = mpPreviewNode;
	}

	if (last)
		SelectLine(ATHTLineIterator { last, last->mVisibleLines - 1 });

	if (heightChanged)
		mbScrollToBottom = true;
}

void ATImGuiHistoryPaneImpl::SelectLine(const ATHTLineIterator& it) {
	if (mSelectedLine == it)
		return;

	mSelectedLine = it;

	if (!it)
		return;

	// Auto-expand collapsed parents (matches Windows EnsureLineVisible)
	for (ATHTNode *p = it.mpNode->mpParent; p; p = p->mpParent) {
		if (p->mpFirstChild && !p->mbExpanded)
			mHistoryTree.ExpandNode(p);
	}

	mbScrollToSelection = true;
}

// =========================================================================
// Keyboard navigation
// =========================================================================

bool ATImGuiHistoryPaneImpl::HandleKeyboardInput() {
	if (ImGui::GetIO().WantTextInput)
		return false;

	if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
		return false;

	if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
		ATUIDebuggerFocusConsole();
		return true;
	}

	// Ctrl+C -> copy visible to clipboard
	if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) {
		CopyVisibleLines();
		return true;
	}

	if (!mSelectedLine)
		return false;

	if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
		ATHTLineIterator prev = mHistoryTree.GetPrevVisibleLine(mSelectedLine);
		if (prev)
			SelectLine(prev);
		return true;
	}

	if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
		ATHTLineIterator next = mHistoryTree.GetNextVisibleLine(mSelectedLine);
		if (next)
			SelectLine(next);
		return true;
	}

	if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
		ATHTNode *node = mSelectedLine.mpNode;
		if (node->mbExpanded && node->mpFirstChild) {
			mHistoryTree.CollapseNode(node);
		} else if (node->mpParent && node->mpParent != mHistoryTree.GetRootNode()) {
			SelectLine(ATHTLineIterator { node->mpParent, 0 });
		}
		return true;
	}

	if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
		ATHTNode *node = mSelectedLine.mpNode;
		if (node->mpFirstChild && !node->mbExpanded) {
			mHistoryTree.ExpandNode(node);
		} else if (node->mpFirstChild) {
			ATHTNode *firstChild = node->mpFirstChild;
			while (firstChild && !firstChild->mbVisible)
				firstChild = firstChild->mpNextSibling;
			if (firstChild)
				SelectLine(ATHTLineIterator { firstChild, 0 });
		}
		return true;
	}

	if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
		ATHTLineIterator target = mSelectedLine;
		for (int i = 0; i < 20; ++i) {
			ATHTLineIterator prev = mHistoryTree.GetPrevVisibleLine(target);
			if (!prev)
				break;
			target = prev;
		}
		SelectLine(target);
		return true;
	}

	if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
		ATHTLineIterator target = mSelectedLine;
		for (int i = 0; i < 20; ++i) {
			ATHTLineIterator next = mHistoryTree.GetNextVisibleLine(target);
			if (!next)
				break;
			target = next;
		}
		SelectLine(target);
		return true;
	}

	if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
		ATHTNode *front = mHistoryTree.GetFrontNode();
		if (front)
			SelectLine(ATHTLineIterator { front, 0 });
		return true;
	}

	if (ImGui::IsKeyPressed(ImGuiKey_End)) {
		ATHTNode *back = mHistoryTree.GetBackNode();
		if (back)
			SelectLine(ATHTLineIterator { back, back->mVisibleLines > 0 ? back->mVisibleLines - 1 : 0 });
		return true;
	}

	return false;
}

// =========================================================================
// Search / filter
// =========================================================================

void ATImGuiHistoryPaneImpl::Search(const char *substr) {
	if (substr && !*substr)
		substr = nullptr;

	if (!substr && !mbSearchActive)
		return;

	if (substr) {
		mFilteredInsnLookup.clear();
		mFilteredInsnLookup.resize(mInsnBuffer.size(), 0);

		const char ch0 = substr[0];
		const char mask0 = (unsigned)(((unsigned char)ch0 & 0xdf) - 'A') < 26 ? (char)0xdf : (char)0xff;
		const size_t substrlen = strlen(substr);

		const auto filter = [=, this](const ATHTNode& node) -> uint32 {
			const bool isInsnNode = (node.mNodeType == kATHTNodeType_Insn);
			const uint32 lineCount = isInsnNode ? node.mInsn.mCount : 1;
			const uint32 startOffset = isInsnNode ? node.mInsn.mOffset : 0;
			uint32 visibleLines = 0;

			for (uint32 lineIndex = 0; lineIndex < lineCount; ++lineIndex) {
				const char *s = GetLineText(ATHTLineIterator { const_cast<ATHTNode *>(&node), lineIndex });
				if (!s)
					continue;

				const size_t len = strlen(s);

				if (len < substrlen)
					continue;

				size_t maxoffset = len - substrlen;
				for (size_t i = 0; i <= maxoffset; ++i) {
					if (!((s[i] ^ ch0) & mask0) && !vdstricmp(substr, s + i, substrlen)) {
						if (isInsnNode)
							mFilteredInsnLookup[startOffset + visibleLines] = startOffset + lineIndex;

						++visibleLines;
						break;
					}
				}
			}

			return visibleLines;
		};

		mHistoryTree.Search(filter);

		// Select first visible line after filtering
		mSelectedLine = mHistoryTree.GetNearestVisibleLine(
			ATHTLineIterator { mHistoryTree.GetFrontNode(), 0 });
	} else {
		// Clearing search -- translate selected line from filtered to unfiltered index
		if (mSelectedLine.mpNode) {
			if (mSelectedLine.mpNode->mbFiltered) {
				const uint32 insnBase = mSelectedLine.mpNode->mInsn.mOffset;
				uint32 insnIndex = mFilteredInsnLookup[insnBase + mSelectedLine.mLineIndex] - insnBase;
				mSelectedLine.mLineIndex = insnIndex;
			}
		}

		mHistoryTree.Unsearch();

		if (!mSelectedLine.mpNode) {
			if (mpPreviewNode) {
				mSelectedLine = { mpPreviewNode, 0 };
			} else {
				ATHTNode *back = mHistoryTree.GetBackNode();
				if (back)
					mSelectedLine = { back, back->mVisibleLines > 0 ? back->mVisibleLines - 1 : 0 };
			}
		}
	}

	mbSearchActive = (substr != nullptr);
	mbScrollToSelection = true;
}

void ATImGuiHistoryPaneImpl::RenderSearchBar() {
	float buttonWidth = 60.0f;
	float spacing = ImGui::GetStyle().ItemSpacing.x;
	float availWidth = ImGui::GetContentRegionAvail().x;

	// Button: "Search" or "Clear"
	if (ImGui::Button(mbSearchActive ? "Clear" : "Search", ImVec2(buttonWidth, 0))) {
		if (mbSearchActive) {
			Search(nullptr);
			mSearchBuf[0] = '\0';
		} else {
			Search(mSearchBuf);
		}
	}

	ImGui::SameLine();

	// Search input field
	ImGui::SetNextItemWidth(availWidth - buttonWidth - spacing);
	if (ImGui::InputText("##histsearch", mSearchBuf, sizeof(mSearchBuf),
		ImGuiInputTextFlags_EnterReturnsTrue))
	{
		if (mSearchBuf[0])
			Search(mSearchBuf);
		else
			Search(nullptr);
	}
}

// =========================================================================
// Context Menu
// =========================================================================

void ATImGuiHistoryPaneImpl::RenderContextMenu() {
	if (mbOpenContextMenu) {
		ImGui::OpenPopup("HistCtx");
		mbOpenContextMenu = false;
	}

	if (!ImGui::BeginPopup("HistCtx"))
		return;

	// Go to Source
	if (ImGui::MenuItem("Go to Source", nullptr, false, mbContextHasInsn)) {
		ATImGuiConsoleShowSource(mContextAddr);
	}

	ImGui::Separator();

	// Show toggles
	{
		bool v;
		v = (bool)mbShowPCAddress;
		if (ImGui::MenuItem("Show PC Address", nullptr, v))
			mbShowPCAddress = !v;

		v = (bool)mbShowGlobalPCAddress;
		if (ImGui::MenuItem("Show Global PC Address", nullptr, v, (bool)mbShowPCAddress))
			mbShowGlobalPCAddress = !v;

		v = (bool)mbShowRegisters;
		if (ImGui::MenuItem("Show Registers", nullptr, v))
			mbShowRegisters = !v;

		v = (bool)mbShowSpecialRegisters;
		if (ImGui::MenuItem("Show Special Registers", nullptr, v, (bool)mbShowRegisters))
			mbShowSpecialRegisters = !v;

		v = (bool)mbShowFlags;
		if (ImGui::MenuItem("Show Flags", nullptr, v))
			mbShowFlags = !v;

		v = (bool)mbShowCodeBytes;
		if (ImGui::MenuItem("Show Code Bytes", nullptr, v))
			mbShowCodeBytes = !v;

		v = (bool)mbShowLabels;
		if (ImGui::MenuItem("Show Labels", nullptr, v))
			mbShowLabels = !v;

		v = (bool)mbShowLabelNamespaces;
		if (ImGui::MenuItem("Show Label Namespaces", nullptr, v, (bool)mbShowLabels))
			mbShowLabelNamespaces = !v;
	}

	ImGui::Separator();

	// Collapse toggles
	{
		bool v;
		v = (bool)mbCollapseLoops;
		if (ImGui::MenuItem("Collapse Loops", nullptr, v))
			mbCollapseLoops = !v;

		v = (bool)mbCollapseCalls;
		if (ImGui::MenuItem("Collapse Calls", nullptr, v))
			mbCollapseCalls = !v;

		v = (bool)mbCollapseInterrupts;
		if (ImGui::MenuItem("Collapse Interrupts", nullptr, v))
			mbCollapseInterrupts = !v;
	}

	ImGui::Separator();

	// Timestamp origin
	if (ImGui::MenuItem("Reset Timestamp Origin")) {
		mTimeBaseCycles = mTimeBaseCyclesDefault;
		mTimeBaseUnhaltedCycles = mTimeBaseUnhaltedCyclesDefault;
	}

	if (ImGui::MenuItem("Set Timestamp Origin", nullptr, false, mbContextIsInsnNode && mbContextHasInsn)) {
		mTimeBaseCycles = mContextHent.mCycle;
		mTimeBaseUnhaltedCycles = mContextHent.mUnhaltedCycle;
	}

	ImGui::Separator();

	// Timestamp mode radio group
	auto tsRadio = [&](const char *label, HistTimestampMode mode) {
		if (ImGui::MenuItem(label, nullptr, mTimestampMode == mode))
			mTimestampMode = mode;
	};
	tsRadio("Show Beam Position", HistTimestampMode::Beam);
	tsRadio("Show Microseconds", HistTimestampMode::Microseconds);
	tsRadio("Show Cycles", HistTimestampMode::Cycles);
	tsRadio("Show Unhalted Cycles", HistTimestampMode::UnhaltedCycles);
	tsRadio("Show Tape Position (Samples)", HistTimestampMode::TapePositionSamples);
	tsRadio("Show Tape Position (Seconds)", HistTimestampMode::TapePositionSeconds);

	ImGui::Separator();

	if (ImGui::MenuItem("Copy Visible to Clipboard"))
		CopyVisibleLines();

	ImGui::EndPopup();
}

// =========================================================================
// Tree rendering
// =========================================================================

void ATImGuiHistoryPaneImpl::RenderTreeContent() {
	const ATHTNode *root = mHistoryTree.GetRootNode();
	const uint32 totalLines = root->mHeight > 0 ? root->mHeight - 1 : 0;

	if (totalLines == 0) {
		if (mbStateValid && mLastState.mbRunning)
			ImGui::TextDisabled("(running)");
		else
			ImGui::TextDisabled("(no history)");
		return;
	}

	const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
	const float indentWidth = lineHeight;  // match Windows: indent = item height

	ImGui::BeginChild("HistoryScroll", ImVec2(0, 0), ImGuiChildFlags_None,
		ImGuiWindowFlags_HorizontalScrollbar);

	IATDebugger *dbg = ATGetDebugger();

	ImGuiListClipper clipper;
	clipper.Begin((int)totalLines);

	while (clipper.Step()) {
		// Find start line via tree position lookup
		ATHTLineIterator it = mHistoryTree.GetLineFromPos(clipper.DisplayStart);

		for (int i = clipper.DisplayStart; i < clipper.DisplayEnd && it; ++i) {
			// Calculate nesting depth (root not displayed)
			int level = 0;
			for (ATHTNode *p = it.mpNode->mpParent; p; p = p->mpParent)
				++level;
			level -= 1;  // root is not displayed

			// In the Windows version, text starts at (level+1)*itemHeight
			// and the expand box occupies level*itemHeight...(level+1)*itemHeight.
			// We follow the same model: the expand box column is at
			// level*indentWidth and text starts at (level+1)*indentWidth.
			float boxColumnX = level * indentWidth;
			float textX = (level + 1) * indentWidth;
			bool isSelected = (it == mSelectedLine);

			ImGui::PushID(i);

			// Draw expand/collapse box for nodes with children
			bool hasChildren = (it.mpNode->mpFirstChild != nullptr) && (it.mLineIndex == 0);
			float boxSize = lineHeight * 0.6f;

			// Get the base X for this line (left edge of content area)
			float baseX = ImGui::GetCursorPosX();

			if (hasChildren) {
				ImVec2 screenPos = ImGui::GetCursorScreenPos();

				float boxX = screenPos.x + boxColumnX + (indentWidth - boxSize) * 0.5f;
				float boxY = screenPos.y + (lineHeight - boxSize) * 0.5f;

				ImDrawList *dl = ImGui::GetWindowDrawList();
				ImU32 boxColor = IM_COL32(180, 180, 180, 255);

				dl->AddRect(ImVec2(boxX, boxY), ImVec2(boxX + boxSize, boxY + boxSize), boxColor);

				// + or - sign
				float cx = boxX + boxSize * 0.5f;
				float cy = boxY + boxSize * 0.5f;
				float arm = boxSize * 0.3f;
				dl->AddLine(ImVec2(cx - arm, cy), ImVec2(cx + arm, cy), boxColor);
				if (!it.mpNode->mbExpanded) {
					dl->AddLine(ImVec2(cx, cy - arm), ImVec2(cx, cy + arm), boxColor);
				}

				// Invisible button for clicking the expand box
				ImGui::SetCursorPosX(baseX + boxColumnX);
				if (ImGui::InvisibleButton("##exp", ImVec2(indentWidth, lineHeight))) {
					if (it.mpNode->mbExpanded)
						mHistoryTree.CollapseNode(it.mpNode);
					else
						mHistoryTree.ExpandNode(it.mpNode);
				}
				ImGui::SameLine();
			}

			// Position text after the expand box column
			ImGui::SetCursorPosX(baseX + textX);

			// Determine text color
			ImU32 textColor = 0;
			bool pushColor = false;
			if (it.mpNode->mNodeType == kATHTNodeType_Insn || it.mpNode->mNodeType == kATHTNodeType_Interrupt) {
				const ATCPUHistoryEntry *lineHe = GetLineHistoryEntry(it);
				if (lineHe) {
					if (lineHe->mbNMI && lineHe->mbIRQ)
						textColor = IM_COL32(200, 200, 0, 255);   // HLE (yellow)
					else if (lineHe->mbNMI)
						textColor = IM_COL32(255, 128, 128, 255);  // NMI (red)
					else if (lineHe->mbIRQ)
						textColor = IM_COL32(128, 128, 255, 255);  // IRQ (blue)
				}
			} else if (it.mpNode->mNodeType == kATHTNodeType_Repeat || it.mpNode->mNodeType == kATHTNodeType_Label) {
				textColor = IM_COL32(140, 140, 140, 255);  // dimmed
			} else if (it.mpNode->mNodeType == kATHTNodeType_InsnPreview) {
				textColor = IM_COL32(140, 140, 140, 255);  // dimmed for NEXT
			}

			if (textColor) {
				ImGui::PushStyleColor(ImGuiCol_Text, textColor);
				pushColor = true;
			}

			// Get line text
			const char *text = GetLineText(it);

			// Render as selectable
			if (ImGui::Selectable(text ? text : "", isSelected,
				ImGuiSelectableFlags_AllowOverlap))
			{
				SelectLine(it);
			}

			// Right-click -- capture data immediately so we don't hold
			// a tree iterator across frames (tree can be rebuilt).
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
				SelectLine(it);
				mbOpenContextMenu = true;

				const ATCPUHistoryEntry *ctxHe = GetLineHistoryEntry(it);
				mbContextHasInsn = (ctxHe != nullptr) && it.mpNode
					&& (it.mpNode->mNodeType == kATHTNodeType_Insn
					 || it.mpNode->mNodeType == kATHTNodeType_InsnPreview
					 || it.mpNode->mNodeType == kATHTNodeType_Interrupt);
				mbContextIsInsnNode = it.mpNode
					&& it.mpNode->mNodeType == kATHTNodeType_Insn;
				if (ctxHe) {
					mContextHent = *ctxHe;
					mContextAddr = GetHistoryAddress(*ctxHe);
				} else {
					mContextHent = {};
					mContextAddr = 0;
				}
			}

			// Double-click -> jump to disassembly
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
				const ATCPUHistoryEntry *lineHe = GetLineHistoryEntry(it);
				if (lineHe && dbg) {
					dbg->SetFrameExtPC(GetHistoryAddress(*lineHe));
					ATActivateUIPane(kATUIPaneId_Disassembly, true, true);
				}
			}

			if (pushColor)
				ImGui::PopStyleColor();

			ImGui::PopID();

			it = mHistoryTree.GetNextVisibleLine(it);
		}
	}

	// Right-click on empty area of the child window (below last line)
	if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)
		&& ImGui::IsMouseClicked(ImGuiMouseButton_Right)
		&& !mbOpenContextMenu)
	{
		mbOpenContextMenu = true;
		mbContextHasInsn = false;
		mbContextIsInsnNode = false;
		mContextHent = {};
		mContextAddr = 0;
	}

	// Context menu (must be in same ID scope as OpenPopup)
	RenderContextMenu();

	// Scroll to bottom after update
	if (mbScrollToBottom) {
		ImGui::SetScrollHereY(1.0f);
		mbScrollToBottom = false;
		mbScrollToSelection = false;  // bottom takes priority
	}

	// Scroll to keep selected line visible (keyboard navigation)
	if (mbScrollToSelection && mSelectedLine) {
		mbScrollToSelection = false;

		uint32 ypos = mHistoryTree.GetLineYPos(mSelectedLine);
		float targetY = (float)ypos * lineHeight;
		float scrollY = ImGui::GetScrollY();
		float windowH = ImGui::GetWindowHeight();

		if (targetY < scrollY) {
			ImGui::SetScrollY(targetY);
		} else if (targetY + lineHeight > scrollY + windowH) {
			ImGui::SetScrollY(targetY + lineHeight - windowH);
		}
	}

	ImGui::EndChild();
}

// =========================================================================
// Main render
// =========================================================================

bool ATImGuiHistoryPaneImpl::Render() {
	bool open = true;

	if (mbFocusRequested) {
		ImGui::SetNextWindowFocus();
		mbFocusRequested = false;
	}

	if (!ImGui::Begin(mTitle.c_str(), &open)) {
		mbHasFocus = false;
		ImGui::End();
		return open;
	}
	mbHasFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

	// Handle collapse toggle reload
	if (mbNeedsReload) {
		mbNeedsReload = false;
		ReloadOpcodes();
		mbScrollToBottom = true;
	}

	// Handle normal update
	if (mbNeedsUpdate) {
		mbNeedsUpdate = false;
		UpdateOpcodes();
	}

	if (!mbHistoryEnabled) {
		ImGui::TextWrapped("CPU history is not enabled.");
		ImGui::Spacing();
		if (ImGui::Button("Enable CPU History")) {
			g_sim.GetCPU().SetHistoryEnabled(true);
			mbNeedsUpdate = true;
		}
		ImGui::End();
		return open;
	}

	// Search bar
	RenderSearchBar();

	// Main tree content (includes context menu)
	RenderTreeContent();

	// Keyboard
	HandleKeyboardInput();

	ImGui::End();
	return open;
}

// =========================================================================
// Registration
// =========================================================================

void ATUIDebuggerEnsureHistoryPane() {
	if (!ATUIDebuggerGetPane(kATUIPaneId_History)) {
		auto *pane = new ATImGuiHistoryPaneImpl();
		ATUIDebuggerRegisterPane(pane);
	}
}
