//	AltirraSDL - Dear ImGui debugger history pane (internal header)
//	Replaces Win32 ATHistoryWindow + ATUIHistoryView.

#ifndef f_AT_SDL3_UI_DBG_HISTORY_H
#define f_AT_SDL3_UI_DBG_HISTORY_H

#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/vdstl.h>
#include <at/atcpu/history.h>
#include <at/atdebugger/historytree.h>
#include <at/atdebugger/historytreebuilder.h>
#include "debuggersettings.h"
#include "ui_debugger.h"

// Timestamp display mode -- mirrors ATUIHistoryTimestampMode from
// uihistoryview.h but we avoid including that Win32-specific header.
enum class HistTimestampMode : uint8 {
	Beam,
	Microseconds,
	Cycles,
	UnhaltedCycles,
	TapePositionSamples,
	TapePositionSeconds
};

class ATImGuiHistoryPaneImpl final : public ATImGuiDebuggerPane {
public:
	ATImGuiHistoryPaneImpl();

	bool Render() override;
	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) override;
	void OnDebuggerEvent(ATDebugEvent eventId) override;

private:
	// === Tree management (ui_dbg_history.cpp) ===
	void UpdateOpcodes();
	void UpdateOpcodesRange(uint32 historyStart, uint32 historyEnd);
	void ReloadOpcodes();
	void Reset();
	void ClearAllNodes();
	void CheckDisasmMode();

	// === Rendering (ui_dbg_history.cpp) ===
	void RenderTreeContent();
	void RenderContextMenu();

	// === Selection & Navigation (ui_dbg_history.cpp) ===
	void SelectLine(const ATHTLineIterator& it);
	bool HandleKeyboardInput();

	// === Search/filter (ui_dbg_history.cpp) ===
	void Search(const char *substr);
	void RenderSearchBar();

	// === Line text formatting (ui_dbg_history_format.cpp) ===
	const char *GetLineText(const ATHTLineIterator& it);

	// === History entry access (ui_dbg_history_format.cpp) ===
	bool GetLineHistoryIndex(const ATHTLineIterator& it, uint32& index) const;
	const ATCPUHistoryEntry *GetLineHistoryEntry(const ATHTLineIterator& it) const;
	const ATCPUHistoryEntry *GetSelectedLineHistoryEntry() const;
	uint32 GetHistoryAddress(const ATCPUHistoryEntry& he) const;

	// === Timestamp model (ui_dbg_history_format.cpp) ===
	double DecodeTapeSample(uint32 cycle);
	double DecodeTapeSeconds(uint32 cycle);
	uint32 ConvertRawTimestamp(uint32 rawCycle);
	float ConvertRawTimestampDeltaF(sint32 rawCycleDelta);
	ATCPUBeamPosition DecodeBeamPosition(uint32 cycle);
	bool IsInterruptPositionVBI(uint32 cycle);

	// === Preview node (ui_dbg_history_format.cpp) ===
	bool UpdatePreviewNode(ATCPUHistoryEntry& he);

	// === Copy (ui_dbg_history_format.cpp) ===
	void CopyVisibleLines();

	// === State ===

	// History tree and builder
	ATHistoryTree mHistoryTree;
	ATHistoryTreeBuilder mHistoryTreeBuilder;
	vdfastdeque<ATCPUHistoryEntry, std::allocator<ATCPUHistoryEntry>, 10> mInsnBuffer;
	vdfastvector<uint32> mFilteredInsnLookup;

	// Preview
	ATHTNode *mpPreviewNode = nullptr;
	ATCPUHistoryEntry mPreviewNodeHEnt {};

	// Position tracking
	uint32 mInsnPosStart = 0;
	uint32 mInsnPosEnd = 0;

	// Selection
	ATHTLineIterator mSelectedLine {};

	// CPU mode
	ATDebugDisasmMode mDisasmMode = {};
	uint32 mSubCycles = 0;
	bool mbDecodeAnticNMI = false;

	// Timestamp state
	HistTimestampMode mTimestampMode = HistTimestampMode::Beam;
	uint32 mTimeBaseCycles = 0;
	uint32 mTimeBaseUnhaltedCycles = 0;
	uint32 mTimeBaseCyclesDefault = 0;
	uint32 mTimeBaseUnhaltedCyclesDefault = 0;

	// Display settings (persisted via ATDebuggerSettingView)
	ATDebuggerSettingView<bool> mbShowPCAddress;
	ATDebuggerSettingView<bool> mbShowGlobalPCAddress;
	ATDebuggerSettingView<bool> mbShowRegisters;
	ATDebuggerSettingView<bool> mbShowSpecialRegisters;
	ATDebuggerSettingView<bool> mbShowFlags;
	ATDebuggerSettingView<bool> mbShowCodeBytes;
	ATDebuggerSettingView<bool> mbShowLabels;
	ATDebuggerSettingView<bool> mbShowLabelNamespaces;
	ATDebuggerSettingView<bool> mbCollapseLoops;
	ATDebuggerSettingView<bool> mbCollapseCalls;
	ATDebuggerSettingView<bool> mbCollapseInterrupts;

	// UI state
	bool mbHistoryEnabled = false;
	bool mbNeedsUpdate = true;
	bool mbNeedsReload = false;
	bool mbScrollToBottom = false;
	bool mbScrollToSelection = false;    // scroll selected line into view
	bool mbSearchActive = false;
	char mSearchBuf[256] = {};

	// Context menu state -- captured at right-click time so we don't
	// hold a tree iterator across frames (tree can be rebuilt any frame).
	bool mbOpenContextMenu = false;
	bool mbContextHasInsn = false;         // right-clicked line is an instruction
	bool mbContextIsInsnNode = false;      // node type is kATHTNodeType_Insn (not preview/interrupt)
	ATCPUHistoryEntry mContextHent {};     // copy of the history entry (if any)
	uint32 mContextAddr = 0;              // resolved address for Go to Source

	// Temp buffer for line formatting
	VDStringA mTempLine;
};

#endif
