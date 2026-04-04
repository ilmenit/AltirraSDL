//	Altirra - Atari 800/800XL/5200 emulator
//	SDL3/ImGui tape editor — shared state header
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#ifndef ALTIRRASDL_UI_TOOL_TAPEEDITOR_H
#define ALTIRRASDL_UI_TOOL_TAPEEDITOR_H

#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/refcount.h>
#include <at/atio/cassetteimage.h>
#include <at/atcore/fft.h>

class IATCassetteImage;
class ATCassetteEmulator;

// ---- Enums ----

enum class TapeDrawMode : uint8 {
	Scroll,
	Select,
	Draw,
	Insert,
	Analyze
};

enum class TapeDecoder : uint8 {
	FSK_Sync,
	FSK_PLL,
	T2000,
};

enum class TapeFilterMode : uint8 {
	FSKDirectSample2000Baud,
	FSKDirectSample1000Baud
};

enum class TapeWaveformMode : uint8 {
	None,
	Waveform,
	Spectrogram
};

enum class TapeUndoSelMode : uint8 {
	None,
	SelectionIsRange,
	SelectionToEnd,
	EndToSelection,
};

enum class TapeDecodedByteFlags : uint8 {
	None = 0x00,
	FramingError = 0x01
};

inline TapeDecodedByteFlags operator&(TapeDecodedByteFlags x, TapeDecodedByteFlags y) { return (TapeDecodedByteFlags)((uint8)x & (uint8)y); }
inline TapeDecodedByteFlags operator|(TapeDecodedByteFlags x, TapeDecodedByteFlags y) { return (TapeDecodedByteFlags)((uint8)x | (uint8)y); }

// ---- Decoded data structures ----

struct TapeDecodedByte {
	uint32 mStartSample {};
	uint16 mBitSampleOffsets[10] {};
	uint8 mData {};
	TapeDecodedByteFlags mFlags {};
};

struct TapeDecodedBlock {
	uint32 mSampleStart {};
	uint32 mSampleEnd {};
	uint32 mSampleValidEnd {};
	float mBaudRate {};
	uint32 mStartByte {};
	uint32 mByteCount {};
	uint32 mChecksumPos {};
	bool mbValidFrame {};
	uint8 mSuspiciousBit {};
	bool mbSuspiciousBitPolarity {};
};

struct TapeDecodedBlocks {
	vdvector<TapeDecodedBlock> mBlocks;
	vdvector<TapeDecodedByte> mByteData;

	void Clear() {
		mBlocks.clear();
		mByteData.clear();
	}
};

struct TapeAnalysisChannel {
	TapeDecodedBlocks mDecodedBlocks;
	uint32 mSampleStart = 0;
	uint32 mSampleEnd = 0;
};

// ---- Undo entry ----

struct TapeUndoEntry {
	uint32 mStart = 0;
	uint32 mLength = 0;
	vdrefptr<IATTapeImageClip> mpData;
	TapeUndoSelMode mSelectionMode {};
};

// ---- Main editor state ----

struct ATTapeEditorState {
	static constexpr uint32 kUndoLimit = 50;
	static constexpr uint32 kInvalidChecksumPos = ~UINT32_C(0);

	// Image and emulator
	vdrefptr<IATCassetteImage> mpImage;
	vdrefptr<IATTapeImageClip> mpImageClip;
	uint32 mTapeChangedLock = 0;

	// View state
	sint64 mScrollX = 0;
	sint64 mScrollMax = 0;
	sint32 mZoom = -12;
	float mZoomAccum = 0;
	uint32 mSampleCount = 0;
	uint32 mViewResetLock = 0;

	// Draw mode
	TapeDrawMode mDrawMode = TapeDrawMode::Scroll;
	TapeDrawMode mActiveDragMode = TapeDrawMode::Scroll;
	TapeDecoder mAnalysisDecoder = TapeDecoder::FSK_Sync;

	// Selection
	bool mbSelectionValid = false;
	uint32 mSelStartSample = 0;
	uint32 mSelEndSample = 0;
	uint32 mSelSortedStartSample = 0;
	uint32 mSelSortedEndSample = 0;

	// Draw tool state
	bool mbDrawValid = false;
	bool mbDrawPolarity = false;
	uint32 mDrawStartSample = 0;
	uint32 mDrawEndSample = 0;

	// Drag state
	bool mbDragging = false;
	float mDragOriginX = 0;

	// View options
	bool mbShowTurboData = false;
	TapeWaveformMode mWaveformMode = TapeWaveformMode::Waveform;
	bool mbShowFrequencyGuidelines = false;
	bool mbStoreWaveformOnLoad = false;

	// Head position
	uint32 mHeadPosition = 0;
	bool mbHeadPlay = false;
	bool mbHeadRecord = false;

	// SIO monitor
	bool mbSIOMonitorEnabled = false;
	uint8 mSIOMonChecksum = 0;
	uint32 mSIOMonChecksumPos = kInvalidChecksumPos;
	uint32 mSIOMonFramingErrors = 0;

	// Analysis
	TapeAnalysisChannel mAnalysisChannels[2];

	// Undo/redo
	vdvector<TapeUndoEntry> mUndoQueue;
	vdvector<TapeUndoEntry> mRedoQueue;

	// Spectrogram
	vdautoptr<ATFFT<128>> mpFFT;
	float mFFTWindow[128] {};
	uint32 mSpectrogramPalette[256] {};

	// Palette for zoomed-out data view
	uint32 mPalette[257] {};
	int mPaletteShift = 0;

	// ---- Methods (ops) ----
	void SetImage(IATCassetteImage *image);
	void OnTapeModified();

	void SetSelection(uint32 startSample, uint32 endSample);
	void ClearSelection();
	void EnsureSelectionVisible();

	bool HasSelection() const { return mbSelectionValid; }
	bool HasNonEmptySelection() const { return mbSelectionValid && mSelSortedStartSample != mSelSortedEndSample; }
	bool HasClip() const { return mpImageClip != nullptr; }

	void SelectAll();
	void DeselectAll();
	void Cut();
	void Copy();
	void Paste();
	void Delete();
	void Insert();

	void ConvertToStdBlock();
	void ConvertToRawBlock();
	void ExtractSelectionAsCFile(vdfastvector<uint8>& data) const;

	bool HasDecodedData() const;
	void CopyDecodedData() const;

	bool CanUndo() const;
	bool CanRedo() const;
	void Undo();
	void Redo();
	void ClearUndoRedo();

	void Analyze(uint32 start, uint32 end);
	void ReAnalyze();
	void ReAnalyzeFlip();
	void Filter(TapeFilterMode filterMode);

	void OnByteDecoded(uint32 startPos, uint32 endPos, uint8 data, bool framingError, uint32 cyclesPerHalfBit);
	void SetSIOMonitorEnabled(bool enabled);
	vdfunction<void(uint32, uint32, uint8, bool, uint32)> mFnByteDecoded;

	// ---- Methods (view) ----
	void SetZoom(sint32 newZoom, sint32 centerClientX, sint32 viewWidth);
	void UpdateScrollLimit();
	uint32 ClientXToSampleEdge(float x, float viewWidth, bool clampToLength) const;
	uint32 ClientXToSample(float x, float viewWidth) const;
	float SampleToClientX(uint32 sample, float viewWidth) const;
	float SampleEdgeToClientX(uint32 sample, float viewWidth) const;

	void InitSpectrogram();
	void RenderTapeView(float viewWidth, float viewHeight, float viewX, float viewY);

private:
	void PushUndo(uint32 start, uint32 len, uint32 newLen, TapeUndoSelMode selMode);
	void ExecuteUndoRedo(TapeUndoEntry& ue);
	uint32 PreModify();
	void PostModify(uint32 newPos);

	void DecodeFSK(uint32 start, uint32 end, bool stopOnFramingError, TapeDecodedBlocks& output) const;
	void DecodeFSK2(uint32 start, uint32 end, bool stopOnFramingError, TapeDecodedBlocks& output) const;
	void DecodeT2000(uint32 start, uint32 end, TapeDecodedBlocks& output) const;
	static bool TryIdentifySuspiciousBit(const TapeDecodedBlocks& dblocks, TapeDecodedBlock& dblock, uint32 forcedSyncBytes, uint32 checksumPos, uint8 receivedSum);

	void UpdatePalettes();
};

#endif
