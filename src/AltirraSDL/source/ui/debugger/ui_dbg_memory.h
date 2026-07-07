//	AltirraSDL - Dear ImGui debugger memory viewer pane
//	Class definition for ATImGuiMemoryPaneImpl.
//	Split across: ui_dbg_memory.cpp (core), ui_dbg_memory_hexdump.cpp
//	(rendering), ui_dbg_memory_bitmap.cpp (bitmap interpretation).

#pragma once

#include <optional>
#include <vector>
#include <imgui.h>
#include <SDL3/SDL.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <at/atcore/address.h>
#include "ui_debugger.h"

// Standard column presets matching Windows (uidbgmemory.h)
inline constexpr uint32 kStdColumnCounts[] = {
	1, 2, 4, 8, 16, 24, 32, 40, 48, 64, 80, 128, 256
};

// Grayscale palettes for bitmap interpretation modes (ARGB8888)
inline constexpr uint32 kPal1Bpp[2] = { 0xFF101010, 0xFFF0F0F0 };
inline constexpr uint32 kPal2Bpp[4] = { 0xFF101010, 0xFF5B5B5B, 0xFFA5A5A5, 0xFFF0F0F0 };

class ATImGuiMemoryPaneImpl final : public ATImGuiDebuggerPane {
public:
	ATImGuiMemoryPaneImpl(uint32 paneId, int instanceIndex);
	~ATImGuiMemoryPaneImpl();

	bool Render() override;
	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) override;
	void OnDebuggerEvent(ATDebugEvent eventId) override;

	// -- Core (ui_dbg_memory.cpp) --
	void RebuildView();
	void RenderAddressBar();
	void NavigateToAddress(const char *expr);
	void SetColumns(uint32 cols);
	void SaveSettings();
	void LoadSettings();

	// -- Editing (ui_dbg_memory.cpp) --
		void BeginEdit();
		void CommitEdit();
		void CancelEdit();
		void EnsureHighlightVisible();
		void SetHighlightedAddress(std::optional<uint32> highlightAddr,
			bool highlightData,
			bool select);
		void AdvanceSelectionHorizontal(int dir);
		void MoveSelectionHorizontal(int dir);
		void MoveSelectionVertical(int dir);
		void ScrollViewRows(int rows);
		void ScrollViewPage(int dir);
		bool TestEnsureHighlightVisible(uint32 viewStart,
			uint32 columns,
			uint32 visibleRows,
			uint32 highlightAddr,
			uint32& outViewStart);
		bool TestNavigation(uint32 viewStart,
			uint32 columns,
			uint32 visibleRows,
			uint32 highlightAddr,
			bool dataColumn,
			const char *op,
			uint32& outViewStart,
			uint32& outHighlightAddr,
			bool& outDataColumn);
	bool TestHexByteEdit(uint32 addr,
		uint8 value,
		uint8& outBefore,
		uint8& outAfter);
	bool TestHexDigitAutoAdvance(int mode,
		uint32 viewStart,
		uint32 columns,
		uint32 visibleRows,
		uint32 addr,
		const char *digits,
		uint16& outValue,
		uint32& outSelectedAddr,
		uint32& outViewStart);
		bool TestHexByteCancel(uint32 addr,
			uint8 value,
			uint8& outBefore,
			uint8& outAfter,
			uint32& outSelectedAddr,
			bool& outSelectionEnabled);

	// -- Hex dump rendering (ui_dbg_memory_hexdump.cpp) --
	void RenderHexDump();

	// -- Bitmap interpretation (ui_dbg_memory_bitmap.cpp) --
	void UpdateBitmapTexture(int rowCount);
	void GenerateBitmapRow(uint32 *dst, int dstPitch,
						   const uint8 *src, uint32 cols);

	// ---- Enums ----

	enum class ValueMode : uint8 { HexBytes, HexWords, DecBytes, DecWords };
		enum class InterpretMode : uint8 {
			None, Atascii, Internal,
			Font1Bpp, Font2Bpp,
			Graphics1Bpp, Graphics2Bpp, Graphics4Bpp, Graphics8Bpp
		};
		bool TestValueEdit(ValueMode mode,
			uint32 addr,
			uint16 value,
			uint16& outBefore,
			uint16& outAfter);
		bool TestTextEdit(InterpretMode mode,
			uint32 addr,
			uint8 ch,
			uint8& outBefore,
			uint8& outAfter,
			uint8& outWritten);
		bool TestTextAutoAdvance(InterpretMode mode,
			uint32 viewStart,
			uint32 columns,
			uint32 visibleRows,
			uint32 addr,
			uint8 ch,
			uint8& outWritten,
			uint32& outSelectedAddr,
			uint32& outViewStart);

	// ---- Inline helpers ----

	bool IsBitmapMode() const {
		return mInterpretMode >= InterpretMode::Font1Bpp;
	}
	bool IsFontMode() const {
		return mInterpretMode == InterpretMode::Font1Bpp
			|| mInterpretMode == InterpretMode::Font2Bpp;
	}
	bool IsWordMode() const {
		return mValueMode == ValueMode::HexWords
			|| mValueMode == ValueMode::DecWords;
	}
	bool IsHexMode() const {
		return mValueMode == ValueMode::HexBytes
			|| mValueMode == ValueMode::HexWords;
	}
	int StepSize() const { return IsWordMode() ? 2 : 1; }

	// Convert a view-data index to an absolute address
	uint32 IndexToAddr(uint32 idx) const {
		uint32 spaceBase = mViewStart & kATAddressSpaceMask;
		uint32 offset = mViewStart & kATAddressOffsetMask;
		uint32 spaceMask = ATAddressGetSpaceSize(mViewStart) - 1;
		return spaceBase | ((offset + idx) & spaceMask);
	}

	// Convert an absolute address to a view-data index, or -1 if not visible
	sint32 AddrToIndex(uint32 addr) const {
		uint32 spaceBase = mViewStart & kATAddressSpaceMask;
		uint32 offset = mViewStart & kATAddressOffsetMask;
		uint32 addrOff = addr & kATAddressOffsetMask;
		if ((addr & kATAddressSpaceMask) != spaceBase)
			return -1;
		uint32 idx = (addrOff >= offset) ? addrOff - offset
			: addrOff + ATAddressGetSpaceSize(mViewStart) - offset;
		return (idx < (uint32)mViewData.size()) ? (sint32)idx : -1;
	}

	// ---- State ----

	int mInstanceIndex;
	uint32 mViewStart = 0;
	uint32 mColumns = 16;
	uint32 mVisibleRows = 16;
	bool mbNeedsRebuild = true;

	ValueMode mValueMode = ValueMode::HexBytes;
	InterpretMode mInterpretMode = InterpretMode::Atascii;

	// Address bar
	char mAddrInput[64] = {};
	std::vector<VDStringA> mAddressHistory;		// most-recent-first, max 20

	// View data and change detection
	// mViewData      — current memory contents (re-read on every rebuild)
	// mRefData       — reference snapshot from the previous debugger cycle;
	//                   frozen until the cycle changes (matching Windows'
	//                   mRefViewData / mOldViewData mechanism)
	// mRefViewStart  — the mViewStart that was active when mRefData was
	//                   captured, so we can realign when the view scrolls
	// mChanged       — per-byte flag: true if byte differs from reference
	std::vector<uint8> mViewData;
	std::vector<uint8> mRefData;
	uint32 mRefViewStart = 0;
	std::vector<bool> mChanged;
	uint32 mLastCycle = 0;
	uint32 mRefCycle = 0;		// cycle at which mRefData was last updated

	// Selection / highlight (mirrors Windows mHighlightedAddress system)
	std::optional<uint32> mHighlightedAddress;	// absolute address
	bool mbHighlightedData = false;		// true = interp column active
	bool mbSelectionEnabled = false;	// true = clicked (editable)
	sint32 mEditValue = -1;				// -1 = no edit, >=0 = accumulated value
	int mEditPhase = 0;					// nibble/digit counter
	bool mbEditCancelledThisFrame = false;
	bool mbEditCommittedThisFrame = false;	// set by CommitEdit, cleared each frame

	// Context menu
	uint32 mContextMenuAddr = 0;

	// Zoom
	float mZoomFactor = 1.0f;

	// Bitmap interpretation texture — dual backend support
	// For SDL_Renderer backend: mpBitmapTexture is used.
	// For OpenGL backend: mBitmapGLTexture is used.
	SDL_Texture *mpBitmapTexture = nullptr;
	uint32 mBitmapGLTexture = 0;	// GLuint — 0 means not allocated
	int mBitmapTexW = 0;
	int mBitmapTexH = 0;

	// Returns the bitmap texture as an ImTextureID (backend-aware).
	void *GetBitmapImTextureID() const;

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
