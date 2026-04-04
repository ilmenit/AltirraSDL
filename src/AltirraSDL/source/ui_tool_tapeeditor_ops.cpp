//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2021 Avery Lee
//	SDL3/ImGui tape editor — edit operations, undo/redo, analysis, file I/O
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#include <stdafx.h>
#include <algorithm>
#include <string>
#include <SDL3/SDL.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/error.h>
#include <vd2/system/text.h>
#include <at/atio/cassetteimage.h>
#include "cassette.h"
#include "simulator.h"
#include "ui_tool_tapeeditor.h"
#include "logging.h"

extern ATSimulator g_sim;

// ---- Undo/Redo ----

void ATTapeEditorState::PushUndo(uint32 start, uint32 len, uint32 newLen, TapeUndoSelMode selMode) {
	TapeUndoEntry ue;
	ue.mpData = mpImage->CopyRange(start, len);
	ue.mStart = start;
	ue.mLength = newLen;
	ue.mSelectionMode = selMode;

	mRedoQueue.clear();

	if (mUndoQueue.size() >= kUndoLimit)
		mUndoQueue.erase(mUndoQueue.begin());

	mUndoQueue.emplace_back(std::move(ue));
}

void ATTapeEditorState::ExecuteUndoRedo(TapeUndoEntry& ue) {
	uint32 deckPos = PreModify();

	vdrefptr saveData = mpImage->CopyRange(ue.mStart, ue.mLength);
	const uint32 clipLen = ue.mpData ? ue.mpData->GetLength() : 0;

	if (deckPos >= ue.mStart) {
		if (deckPos - ue.mStart >= ue.mLength)
			deckPos = (deckPos - ue.mLength) + clipLen;
		else
			deckPos = ue.mStart;
	}

	if (clipLen > ue.mLength) {
		ATCassetteWriteCursor cursor;
		cursor.mPosition = ue.mStart + ue.mLength;
		mpImage->WriteBlankData(cursor, clipLen - ue.mLength, true);
	} else if (clipLen < ue.mLength) {
		mpImage->DeleteRange(ue.mStart + clipLen, ue.mStart + ue.mLength);
	}

	if (ue.mpData)
		mpImage->ReplaceRange(ue.mStart, *ue.mpData);

	ue.mpData = std::move(saveData);
	ue.mLength = clipLen;

	switch (ue.mSelectionMode) {
		case TapeUndoSelMode::None:
			break;

		case TapeUndoSelMode::SelectionIsRange:
			SetSelection(ue.mStart, ue.mStart + ue.mLength);
			EnsureSelectionVisible();
			break;

		case TapeUndoSelMode::SelectionToEnd:
			ue.mSelectionMode = TapeUndoSelMode::EndToSelection;
			SetSelection(ue.mStart + ue.mLength, ue.mStart + ue.mLength);
			EnsureSelectionVisible();
			break;

		case TapeUndoSelMode::EndToSelection:
			ue.mSelectionMode = TapeUndoSelMode::SelectionToEnd;
			SetSelection(ue.mStart, ue.mStart + ue.mLength);
			EnsureSelectionVisible();
			break;
	}

	PostModify(deckPos);
}

bool ATTapeEditorState::CanUndo() const {
	return !mUndoQueue.empty();
}

bool ATTapeEditorState::CanRedo() const {
	return !mRedoQueue.empty();
}

void ATTapeEditorState::Undo() {
	if (mUndoQueue.empty())
		return;

	TapeUndoEntry ue = std::move(mUndoQueue.back());
	mUndoQueue.pop_back();
	ExecuteUndoRedo(ue);
	mRedoQueue.emplace_back(std::move(ue));
}

void ATTapeEditorState::Redo() {
	if (mRedoQueue.empty())
		return;

	TapeUndoEntry ue = std::move(mRedoQueue.back());
	mRedoQueue.pop_back();
	ExecuteUndoRedo(ue);
	mUndoQueue.emplace_back(std::move(ue));
}

void ATTapeEditorState::ClearUndoRedo() {
	mUndoQueue.clear();
	mRedoQueue.clear();
}

// ---- Clipboard operations ----

void ATTapeEditorState::SelectAll() {
	SetSelection(0, mSampleCount);
}

void ATTapeEditorState::DeselectAll() {
	if (mbSelectionValid)
		SetSelection(mSelSortedStartSample, mSelSortedStartSample);
}

void ATTapeEditorState::Cut() {
	if (!mbSelectionValid || mSelSortedEndSample <= mSelSortedStartSample || !mpImage)
		return;

	mpImageClip = mpImage->CopyRange(mSelSortedStartSample, mSelSortedEndSample);

	uint32 deckPos = PreModify();
	PushUndo(mSelSortedStartSample, mSelSortedEndSample - mSelSortedStartSample, 0, TapeUndoSelMode::SelectionIsRange);
	mpImage->DeleteRange(mSelSortedStartSample, mSelSortedEndSample);

	PostModify(deckPos >= mSelSortedEndSample ? mSelSortedStartSample + (deckPos - mSelSortedEndSample)
		: deckPos >= mSelSortedStartSample ? mSelSortedStartSample
		: deckPos);

	SetSelection(mSelSortedStartSample, mSelSortedStartSample);
}

void ATTapeEditorState::Copy() {
	if (mbSelectionValid && mSelSortedEndSample > mSelSortedStartSample && mpImage)
		mpImageClip = mpImage->CopyRange(mSelSortedStartSample, mSelSortedEndSample);
}

void ATTapeEditorState::Paste() {
	if (!mbSelectionValid || !mpImage || !mpImageClip)
		return;

	uint32 deckPos = PreModify();
	PushUndo(mSelSortedStartSample, mSelSortedEndSample - mSelSortedStartSample, mpImageClip->GetLength(), TapeUndoSelMode::SelectionIsRange);

	if (mSelSortedEndSample > mSelSortedStartSample) {
		mpImage->DeleteRange(mSelSortedStartSample, mSelSortedEndSample);

		if (deckPos >= mSelSortedEndSample)
			deckPos = mSelSortedStartSample + (deckPos - mSelSortedEndSample);
		else if (deckPos >= mSelSortedStartSample)
			deckPos = mSelSortedStartSample;

		SetSelection(mSelSortedStartSample, mSelSortedStartSample);
	}

	const uint32 newPos = mpImage->InsertRange(mSelSortedStartSample, *mpImageClip);

	if (deckPos > mSelSortedStartSample)
		deckPos = newPos + (deckPos - mSelSortedStartSample);

	PostModify(deckPos);
	SetSelection(newPos, newPos);
}

void ATTapeEditorState::Delete() {
	if (!mbSelectionValid || mSelSortedEndSample <= mSelSortedStartSample || !mpImage)
		return;

	uint32 deckPos = PreModify();
	PushUndo(mSelSortedStartSample, mSelSortedEndSample - mSelSortedStartSample, 0, TapeUndoSelMode::SelectionIsRange);
	mpImage->DeleteRange(mSelSortedStartSample, mSelSortedEndSample);

	PostModify(deckPos >= mSelSortedEndSample ? mSelSortedStartSample + (deckPos - mSelSortedEndSample)
		: deckPos >= mSelSortedStartSample ? mSelSortedStartSample
		: deckPos);

	SetSelection(mSelSortedStartSample, mSelSortedStartSample);
}

void ATTapeEditorState::Insert() {
	if (!mbSelectionValid || mSelSortedEndSample <= mSelSortedStartSample || !mpImage)
		return;

	uint32 deckPos = PreModify();
	PushUndo(mSelSortedStartSample, 0, mSelSortedEndSample - mSelSortedStartSample, TapeUndoSelMode::EndToSelection);

	ATCassetteWriteCursor cursor {};
	cursor.mPosition = mSelSortedStartSample;
	mpImage->WriteBlankData(cursor, mSelSortedEndSample - mSelSortedStartSample, true);

	if (deckPos >= mSelSortedStartSample)
		deckPos += cursor.mPosition - mSelSortedStartSample;

	PostModify(deckPos);
	SetSelection(mSelSortedEndSample, mSelSortedEndSample);
}

// ---- Block conversion ----

void ATTapeEditorState::ConvertToStdBlock() {
	if (!mbSelectionValid || !mpImage)
		return;

	static constexpr uint32 kMinBitLen = (uint32)(kATCassetteDataSampleRate / 900.0f);
	static constexpr uint32 kMaxBitLen = (uint32)(kATCassetteDataSampleRate / 450.0f);

	const uint32 start = mSelSortedStartSample;
	const uint32 end = mSelSortedEndSample;
	if (start >= end)
		return;

	const uint32 deckPos = PreModify();
	PushUndo(start, end - start, end - start, TapeUndoSelMode::SelectionIsRange);

	TapeDecodedBlocks dblocks;
	DecodeFSK(start, end, true, dblocks);

	ATCassetteWriteCursor writeCursor;
	writeCursor.mPosition = start;

	for (const TapeDecodedBlock& dblock : dblocks.mBlocks) {
		if (writeCursor.mPosition <= dblock.mSampleStart)
			mpImage->WriteBlankData(writeCursor, dblock.mSampleStart - writeCursor.mPosition, false);

		for (uint32 bi = 0; bi < dblock.mByteCount; ++bi) {
			const TapeDecodedByte& byteInfo = dblocks.mByteData[dblock.mStartByte + bi];
			mpImage->WriteStdData(writeCursor, byteInfo.mData, dblock.mBaudRate, false);
		}

		if (writeCursor.mPosition < dblock.mSampleEnd)
			mpImage->WriteBlankData(writeCursor, dblock.mSampleEnd - writeCursor.mPosition, false);
	}

	PostModify(deckPos);
}

void ATTapeEditorState::ConvertToRawBlock() {
	if (!mbSelectionValid || !mpImage)
		return;

	const uint32 start = mSelSortedStartSample;
	const uint32 end = std::min(mSelSortedEndSample, mSampleCount);
	if (start >= end)
		return;

	vdfastvector<uint16> rleData;
	bool nextPolarity = false;
	for (uint32 pos = start; pos < end; ) {
		auto nextTransition = mpImage->FindNextBit(pos, end - 1, nextPolarity, mbShowTurboData);
		uint32 pulseEnd = std::min<uint32>(nextTransition.mPos, mSampleCount);
		uint32 pulseLen = pulseEnd - pos;

		if (pulseLen) {
			while (pulseLen > 0xFFFF) {
				rleData.push_back(0xFFFF);
				rleData.push_back(0);
				pulseLen -= 0xFFFF;
			}
			rleData.push_back(pulseLen);
		}

		nextPolarity = !nextPolarity;
		pos = pulseEnd;
	}

	const uint32 deckPos = PreModify();
	PushUndo(start, end - start, end - start, TapeUndoSelMode::SelectionIsRange);

	ATCassetteWriteCursor cursor;
	cursor.mPosition = start;

	nextPolarity = true;
	for (uint16 pulseLen : rleData) {
		mpImage->WritePulse(cursor, nextPolarity, pulseLen, false, true);
		nextPolarity = !nextPolarity;
	}

	PostModify(deckPos);
}

// ---- Copy Decoded Data ----

bool ATTapeEditorState::HasDecodedData() const {
	return !mAnalysisChannels[0].mDecodedBlocks.mBlocks.empty();
}

void ATTapeEditorState::CopyDecodedData() const {
	if (!HasDecodedData())
		return;

	const auto& ch = mAnalysisChannels[0];
	const auto& dblocks = ch.mDecodedBlocks;
	uint32 start = dblocks.mByteData.front().mStartSample;
	uint32 end = dblocks.mByteData.back().mStartSample;

	if (HasNonEmptySelection()) {
		start = mSelSortedStartSample;
		end = mSelSortedEndSample;
	}

	// Find overlapping byte range
	auto itByteStart = std::upper_bound(dblocks.mByteData.begin(), dblocks.mByteData.end(), start,
		[](uint32 pos, const TapeDecodedByte& dbyte) { return pos < dbyte.mStartSample; });
	if (itByteStart != dblocks.mByteData.begin())
		--itByteStart;

	auto itByteEnd = std::upper_bound(itByteStart, dblocks.mByteData.end(), end,
		[](uint32 pos, const TapeDecodedByte& dbyte) { return pos < dbyte.mStartSample; });

	uint32 byteStartIndex = (uint32)(itByteStart - dblocks.mByteData.begin());
	uint32 byteEndIndex = (uint32)(itByteEnd - dblocks.mByteData.begin());

	// Build hex string
	std::string s;
	for (const TapeDecodedBlock& dblock : dblocks.mBlocks) {
		uint32 bStart = std::max(byteStartIndex, dblock.mStartByte);
		uint32 bEnd = std::min(byteEndIndex, dblock.mStartByte + dblock.mByteCount);

		for (uint32 i = bStart; i < bEnd; ++i) {
			char buf[4];
			snprintf(buf, sizeof(buf), "%s%02x", s.empty() ? "" : " ", dblocks.mByteData[i].mData);
			s += buf;
		}
	}

	if (!s.empty())
		SDL_SetClipboardText(s.c_str());
}

// ---- SIO Monitor wiring ----

void ATTapeEditorState::SetSIOMonitorEnabled(bool enabled) {
	if (mbSIOMonitorEnabled == enabled)
		return;

	mbSIOMonitorEnabled = enabled;
	ATCassetteEmulator& cas = g_sim.GetCassette();

	if (!mFnByteDecoded) {
		mFnByteDecoded = [this](uint32 startPos, uint32 endPos, uint8 data, bool framingError, uint32 cyclesPerHalfBit) {
			OnByteDecoded(startPos, endPos, data, framingError, cyclesPerHalfBit);
		};
	}

	if (enabled) {
		if (cas.IsLoaded())
			cas.ByteDecoded.Add(&mFnByteDecoded);
	} else {
		if (cas.IsLoaded())
			cas.ByteDecoded.Remove(&mFnByteDecoded);
	}
}

// ---- Extract C: File ----

void ATTapeEditorState::ExtractSelectionAsCFile(vdfastvector<uint8>& data) const {
	data.clear();

	if (!mbSelectionValid || !mpImage)
		return;

	TapeDecodedBlocks dblocks;
	DecodeFSK(mSelSortedStartSample, mSelSortedEndSample, true, dblocks);

	if (dblocks.mBlocks.empty())
		return;

	int blockNo = 1;
	for (const TapeDecodedBlock& dblock : dblocks.mBlocks) {
		const TapeDecodedByte *byteInfo = dblocks.mByteData.data() + dblock.mStartByte;

		if (dblock.mByteCount >= 3) {
			if (byteInfo[0].mData != 0x55 || byteInfo[1].mData != 0x55)
				throw MyError("Sync error on block #%d", blockNo);
		}

		if (dblock.mByteCount < 132)
			throw MyError("Block #%d is too short", blockNo);

		if (std::find_if(byteInfo, byteInfo + dblock.mByteCount,
			[](const TapeDecodedByte& info) {
				return (info.mFlags & TapeDecodedByteFlags::FramingError) != TapeDecodedByteFlags::None;
			}) != byteInfo + dblock.mByteCount)
			throw MyError("Block #%d has a framing error.", blockNo);

		uint32 blockLen = 128;
		if (byteInfo[2].mData == 0xFE)
			break;
		else if (byteInfo[2].mData == 0xFA) {
			blockLen = byteInfo[130].mData;
			if (blockLen >= 128)
				throw MyError("Block #%d has invalid length for a partial block.", blockNo);
		} else if (byteInfo[2].mData != 0xFC)
			throw MyError("Block #%d has unrecognized control byte.", blockNo);

		uint32 chksum32 = 0;
		for (int i = 0; i < 131; ++i)
			chksum32 += byteInfo[i].mData;

		uint8 chksum = chksum32 ? (chksum32 - 1) % 255 + 1 : 0;
		if (chksum != byteInfo[131].mData)
			throw MyError("Block #%d has a checksum error.", blockNo);

		data.resize(data.size() + blockLen);
		for (uint32 i = 0; i < blockLen; ++i)
			(data.end() - blockLen)[i] = byteInfo[3 + i].mData;

		++blockNo;
	}
}

// ---- Analysis / decoding ----

void ATTapeEditorState::Analyze(uint32 start, uint32 end) {
	TapeAnalysisChannel& ch0 = mAnalysisChannels[0];
	ch0.mDecodedBlocks.Clear();

	switch (mAnalysisDecoder) {
		case TapeDecoder::FSK_Sync:
			DecodeFSK(start, end, false, ch0.mDecodedBlocks);
			break;
		case TapeDecoder::FSK_PLL:
			DecodeFSK2(start, end, false, ch0.mDecodedBlocks);
			break;
		case TapeDecoder::T2000:
			DecodeT2000(start, end, ch0.mDecodedBlocks);
			break;
	}

	ch0.mSampleStart = start;
	ch0.mSampleEnd = end;
}

void ATTapeEditorState::ReAnalyze() {
	TapeAnalysisChannel& ch0 = mAnalysisChannels[0];
	if (ch0.mSampleEnd > ch0.mSampleStart)
		Analyze(ch0.mSampleStart, ch0.mSampleEnd);
}

void ATTapeEditorState::ReAnalyzeFlip() {
	switch (mAnalysisDecoder) {
		case TapeDecoder::FSK_PLL:
			mAnalysisDecoder = TapeDecoder::FSK_Sync;
			ReAnalyze();
			break;
		case TapeDecoder::FSK_Sync:
			mAnalysisDecoder = TapeDecoder::FSK_PLL;
			ReAnalyze();
			break;
		default:
			break;
	}
}

void ATTapeEditorState::Filter(TapeFilterMode filterMode) {
	if (!HasNonEmptySelection())
		return;

	uint32 window = 32;
	uint32 threshold = 12;

	switch (filterMode) {
		case TapeFilterMode::FSKDirectSample2000Baud:
			window = 16;
			threshold = 6;
			break;
		case TapeFilterMode::FSKDirectSample1000Baud:
			window = 32;
			threshold = 12;
			break;
	}

	ATTapeSlidingWindowCursor cursor {};
	cursor.mWindow = window;
	cursor.mOffset = window / 2;
	cursor.mThresholdLo = threshold;
	cursor.mThresholdHi = window - threshold;
	cursor.mbFSKBypass = false;
	cursor.Reset();

	uint32 deckPos = PreModify();

	const uint32 start = mSelSortedStartSample;
	const uint32 end = mSelSortedEndSample;
	const uint32 len = end - start;
	PushUndo(start, len, len, TapeUndoSelMode::SelectionIsRange);

	vdfastvector<uint32> pulses;
	bool polarity = true;
	uint32 pos = start + (window / 2 - threshold + 1);

	while (pos < end) {
		auto next = cursor.FindNext(*mpImage, pos, !polarity, end - 1);
		pulses.push_back(next.mPos - pos);
		polarity = !polarity;
		pos = next.mPos;
	}

	ATCassetteWriteCursor writeCursor;
	writeCursor.mPosition = mSelSortedStartSample;

	polarity = true;
	for (uint32 pulseWidth : pulses) {
		mpImage->WritePulse(writeCursor, polarity, pulseWidth, false, true);
		polarity = !polarity;
	}

	PostModify(deckPos);
	ClearSelection();
}

// ---- SIO monitor byte decoded callback ----

void ATTapeEditorState::OnByteDecoded(uint32 startPos, uint32 endPos, uint8 data, bool framingError, uint32 cyclesPerHalfBit) {
	if (!cyclesPerHalfBit)
		return;

	TapeAnalysisChannel& ch1 = mAnalysisChannels[1];

	if (ch1.mSampleStart < ch1.mSampleEnd && startPos < ch1.mSampleEnd) {
		ch1.mSampleStart = startPos;
		ch1.mSampleEnd = startPos;
		ch1.mDecodedBlocks.Clear();
	}

	float samplesPerBit = (float)cyclesPerHalfBit * 2.0f / kATCassetteCyclesPerDataSample;

	TapeDecodedBlocks& dblocks = ch1.mDecodedBlocks;
	if (dblocks.mBlocks.empty() || startPos - ch1.mSampleEnd > (uint32)(kATCassetteDataSampleRate / 20)) {
		auto& newdblock = dblocks.mBlocks.emplace_back(TapeDecodedBlock());
		newdblock.mSampleStart = startPos;
		newdblock.mSampleEnd = startPos;
		newdblock.mSampleValidEnd = startPos;
		newdblock.mByteCount = 0;
		newdblock.mChecksumPos = 0;
		newdblock.mBaudRate = kATCassetteDataSampleRate / samplesPerBit;
		newdblock.mStartByte = (uint32)dblocks.mByteData.size();
		newdblock.mbValidFrame = false;
		newdblock.mSuspiciousBit = 0;
		newdblock.mbSuspiciousBitPolarity = false;

		TapeDecodedByte& dbyte = dblocks.mByteData.emplace_back();
		dbyte.mStartSample = startPos;
		dbyte.mData = 0;
		dbyte.mFlags = TapeDecodedByteFlags::None;

		mSIOMonChecksum = 0;
		mSIOMonFramingErrors = 0;
		mSIOMonChecksumPos = kInvalidChecksumPos;
	}

	if (framingError)
		++mSIOMonFramingErrors;

	TapeDecodedBlock& dblock = dblocks.mBlocks.back();

	if (dblock.mByteCount == 2) {
		const TapeDecodedByte *dbytes = &dblocks.mByteData[dblock.mStartByte];

		if (dbytes[0].mData == 0xFA || dbytes[0].mData == 0xFC || dbytes[0].mData == 0xFE) {
			mSIOMonChecksumPos = 129;
			mSIOMonChecksum = (mSIOMonChecksum + 0xAA - 1) % 255 + 1;
		} else if (dbytes[1].mData == 0xFA || dbytes[1].mData == 0xFC || dbytes[1].mData == 0xFE) {
			mSIOMonChecksumPos = 130;
			mSIOMonChecksum = (mSIOMonChecksum + 0x55 - 1) % 255 + 1;
		} else
			mSIOMonChecksumPos = 131;
	}

	if (dblock.mByteCount >= mSIOMonChecksumPos && mSIOMonChecksum == data) {
		if (!dblock.mbValidFrame || !mSIOMonFramingErrors)
			dblock.mChecksumPos = dblock.mByteCount;

		if (!mSIOMonFramingErrors) {
			dblock.mbValidFrame = true;
			dblock.mSuspiciousBit = 0;
			if (dblock.mSampleValidEnd < dblock.mSampleEnd)
				dblock.mSampleValidEnd = dblock.mSampleEnd;
		}
	}

	if (dblock.mByteCount == mSIOMonChecksumPos && !dblock.mbValidFrame)
		TryIdentifySuspiciousBit(dblocks, dblock, 131 - mSIOMonChecksumPos, mSIOMonChecksumPos, data);

	const uint32 sum = (uint32)mSIOMonChecksum + data;
	mSIOMonChecksum = (uint8)(sum + (sum >> 8));

	++dblock.mByteCount;
	dblock.mSampleEnd = endPos;

	TapeDecodedByte& dbyte = dblocks.mByteData.back();
	dbyte.mData = data;

	if (framingError)
		dbyte.mFlags = dbyte.mFlags | TapeDecodedByteFlags::FramingError;

	if (dbyte.mStartSample < startPos)
		dbyte.mStartSample = startPos;

	uint32 bitPos = startPos;
	uint32 bitPosFrac = (kATCassetteCyclesPerDataSample >> 1) + cyclesPerHalfBit;
	for (int i = 0; i < 10; ++i) {
		bitPos += bitPosFrac / kATCassetteCyclesPerDataSample;
		bitPosFrac %= kATCassetteCyclesPerDataSample;
		dbyte.mBitSampleOffsets[i] = bitPos - startPos;
		bitPosFrac += 2 * cyclesPerHalfBit;
	}

	dblocks.mByteData.emplace_back();
	dblocks.mByteData.back().mStartSample = endPos;

	ch1.mSampleEnd = endPos;
}

// ---- FSK Decode (sync-based) ----

void ATTapeEditorState::DecodeFSK(uint32 start, uint32 end, bool stopOnFramingError, TapeDecodedBlocks& output) const {
	static constexpr uint32 kMinBitLen = (uint32)(kATCassetteDataSampleRate / 900.0f);
	static constexpr uint32 kMaxBitLen = (uint32)(kATCassetteDataSampleRate / 450.0f);

	if (start >= end)
		return;

	ATCassetteEmulator& cas = g_sim.GetCassette();
	uint32 pos = start;

	while (pos < end) {
		ATTapeSlidingWindowCursor cursor = cas.GetFSKSampleCursor();
		cursor.mbFSKBypass = mbShowTurboData;

		uint32 syncState = 0;
		uint32 syncStart = pos;

		while (syncState < 21) {
			const auto nextSyncInfo = cursor.FindNext(*mpImage, pos, (syncState & 1) == 0, end - 1);
			if (nextSyncInfo.mPos >= end) {
				pos = end;
				break;
			}

			uint32 syncBitLen = nextSyncInfo.mPos - pos;

			if (syncState >= 2 && (syncBitLen < kMinBitLen || syncBitLen > kMaxBitLen)) {
				if (syncBitLen < kMinBitLen)
					syncState = (syncState + 1) & 1;
				else
					syncState = (syncState & 1) + 1;
				syncStart = pos;
			} else {
				if (syncState == 2)
					syncStart = pos;
				++syncState;
			}

			pos = nextSyncInfo.mPos;
		}

		if (pos >= end)
			break;

		float bitPeriodF = (float)(pos - syncStart) / 19.0f;
		uint32 bitPeriod = (uint32)VDRoundToInt(bitPeriodF);

		ATTapeSlidingWindowCursor bitCursor = cas.GetFSKBitCursor(bitPeriod >> 1);
		bitCursor.mbFSKBypass = mbShowTurboData;

		TapeDecodedBlock& dblock = output.mBlocks.emplace_back();
		dblock.mSampleStart = syncStart;
		dblock.mSampleEnd = syncStart;
		dblock.mSampleValidEnd = syncStart;
		dblock.mStartByte = (uint32)output.mByteData.size();
		dblock.mByteCount = 0;
		dblock.mChecksumPos = 0;
		dblock.mBaudRate = 0;
		dblock.mSuspiciousBit = 0;
		dblock.mbSuspiciousBitPolarity = false;

		uint32 pos2 = syncStart;
		uint32 blockTimeout = bitPeriod * 20;
		bool blockEndedEarly = false;
		uint32 posLastByteEnd = pos2;

		while (pos2 < end) {
			const auto startBitInfo = bitCursor.FindNext(*mpImage, pos2, false, end - 1);

			if (startBitInfo.mPos - pos2 > blockTimeout)
				break;

			if (startBitInfo.mPos >= end || end - pos2 < bitPeriod * 10) {
				blockEndedEarly = true;
				break;
			}

			uint32 startBitPos = startBitInfo.mPos + (bitPeriod >> 1);

			if (bitCursor.GetBit(*mpImage, startBitPos)) {
				pos2 = startBitPos;
				posLastByteEnd = startBitPos;
				continue;
			}

			uint32 dataBitPos[8];
			uint8 v = 0;
			for (int i = 0; i < 8; ++i) {
				dataBitPos[i] = startBitPos + (i + 1) * bitPeriod;
				v = (v >> 1) + (bitCursor.GetBit(*mpImage, dataBitPos[i]) ? 0x80 : 0);
			}

			uint32 stopBitPos = startBitPos + 9 * bitPeriod;
			pos2 = stopBitPos;
			posLastByteEnd = startBitInfo.mPos + 10 * bitPeriod;

			TapeDecodedByteFlags flags = TapeDecodedByteFlags::None;
			if (!bitCursor.GetBit(*mpImage, pos2)) {
				if (stopOnFramingError) {
					blockEndedEarly = true;
					break;
				}
				flags = TapeDecodedByteFlags::FramingError;
				pos2 = stopBitPos;
			}

			TapeDecodedByte& dbyte = output.mByteData.emplace_back();
			dbyte.mFlags = flags;
			dbyte.mStartSample = startBitInfo.mPos;
			dbyte.mData = v;
			dbyte.mBitSampleOffsets[0] = (uint16)(startBitPos - startBitInfo.mPos);
			for (int i = 0; i < 8; ++i)
				dbyte.mBitSampleOffsets[i + 1] = (uint16)(dataBitPos[i] - startBitInfo.mPos);
			dbyte.mBitSampleOffsets[9] = (uint16)(stopBitPos - startBitInfo.mPos);
		}

		if (output.mByteData.size() == dblock.mStartByte) {
			output.mBlocks.pop_back();
			break;
		}

		dblock.mByteCount = (uint32)output.mByteData.size() - dblock.mStartByte;

		if (dblock.mByteCount >= 132) {
			const TapeDecodedByte *bdata = output.mByteData.begin() + dblock.mStartByte;
			uint32 sum = 0;
			for (uint32 i = 0; i < 131; ++i)
				sum += bdata[i].mData;

			uint8 chk = sum ? (sum - 1) % 255 + 1 : 0;
			bool framingOK = std::find_if(bdata, bdata + 131,
				[](const TapeDecodedByte& byteInfo) {
					return (byteInfo.mFlags & TapeDecodedByteFlags::FramingError) != TapeDecodedByteFlags::None;
				}) == bdata + 131;

			for (uint32 i = 131; i < dblock.mByteCount; ++i) {
				if ((bdata[i].mFlags & TapeDecodedByteFlags::FramingError) != TapeDecodedByteFlags::None)
					framingOK = false;

				const uint8 c = bdata[i].mData;
				if (chk == c) {
					if (framingOK || !dblock.mbValidFrame)
						dblock.mChecksumPos = i;
					if (framingOK)
						dblock.mbValidFrame = true;
				}

				sum = (uint32)chk + bdata[i].mData;
				chk = (uint8)(sum + (sum >> 8));
			}

			if (!dblock.mbValidFrame)
				TryIdentifySuspiciousBit(output, dblock, 0, 131, bdata[131].mData);
		}

		pos = std::min(posLastByteEnd, end);
		dblock.mSampleEnd = pos;

		if (dblock.mbValidFrame)
			dblock.mSampleValidEnd = pos;

		TapeDecodedByte& dbyte = output.mByteData.emplace_back();
		dbyte.mStartSample = pos;

		const uint32 baudRateBitPeriod = (uint32)VDRoundToInt(kATCassetteDataSampleRate / bitPeriodF);
		const uint32 numBytes = dblock.mByteCount;
		const uint32 numBits = 10 * numBytes;
		const uint32 baudRateSampleRange = VDRoundToInt(kATCassetteDataSampleRate * (float)numBits / (float)(dblock.mSampleEnd - dblock.mSampleStart));

		uint32 baudRate = std::max(baudRateBitPeriod, baudRateSampleRange);
		dblock.mBaudRate = baudRate;

		ATCassetteWriteCursor writeCursor;
		writeCursor.mPosition = syncStart;
		for (int i = 0; i < 5; ++i) {
			uint32 neededSamples = mpImage->EstimateWriteStdData(writeCursor, numBytes, baudRate);
			if (neededSamples <= pos - syncStart)
				break;
			++baudRate;
		}

		if (blockEndedEarly)
			break;
	}
}

// ---- FSK Decode (PLL-based) ----

void ATTapeEditorState::DecodeFSK2(uint32 start, uint32 end, bool stopOnFramingError, TapeDecodedBlocks& output) const {
	static constexpr uint32 kMinBitLen = (uint32)(kATCassetteDataSampleRate / 900.0f + 0.5f);
	static constexpr uint32 kMaxBitLen = (uint32)(kATCassetteDataSampleRate / 450.0f + 0.5f);

	if (start >= end)
		return;

	ATCassetteEmulator& cas = g_sim.GetCassette();
	uint32 pos = start;

	static constexpr uint32 kStdBitLenX256 = (uint32)(kATCassetteDataSampleRate * 256.0f / 600.0f + 0.5f);
	static constexpr uint32 kStdByteLen = (uint32)(kATCassetteDataSampleRate * 10.0f / 600.0f + 0.5f);
	uint32 bitWidthX256 = kStdBitLenX256;

	sint32 bitError = 0;
	ATTapeSlidingWindowCursor cursor = cas.GetFSKSampleCursor();
	cursor.mbFSKBypass = mbShowTurboData;

	while (pos < end) {
		bool blockEndedEarly = false;
		TapeDecodedBlock *dblock = nullptr;
		uint32 lastByteEnd = pos;

		while (pos < end) {
			const auto nextSyncInfo = cursor.FindNext(*mpImage, pos, false, end - 1);
			if (nextSyncInfo.mPos >= end) {
				pos = end;
				break;
			}

			if (nextSyncInfo.mPos - lastByteEnd > kStdByteLen) {
				bitWidthX256 = kStdBitLenX256;
				pos = nextSyncInfo.mPos;
				break;
			}

			uint32 bitSamplePos[10];
			const uint32 startBitPos = nextSyncInfo.mPos;
			pos = startBitPos + (bitWidthX256 >> 9);

			if (pos >= end)
				break;

			bitSamplePos[0] = pos;
			bool startBit = cursor.GetBit(*mpImage, pos);
			if (startBit)
				continue;

			uint32 accumX256 = 128;
			uint32 shifter = 0;
			bool lastBit = false;

			for (int i = 0; i < 9; ++i) {
				accumX256 += bitWidthX256;
				uint32 next = pos + (accumX256 >> 8);
				if (next >= end) {
					pos = next;
					break;
				}

				bitSamplePos[i + 1] = next;
				accumX256 &= 0xFF;
				const auto sumAndNextInfo = cursor.GetBitSumAndNext(*mpImage, pos, next);

				if (lastBit != sumAndNextInfo.mNextBit) {
					lastBit = sumAndNextInfo.mNextBit;

					uint32 bitLen = next - pos;
					uint32 bitLenLo = (bitLen * 3 + 4) / 8;
					uint32 bitLenHi = (bitLen * 5 + 4) / 8;
					uint32 edgeSum = lastBit ? bitLen - sumAndNextInfo.mSum : sumAndNextInfo.mSum;

					if (edgeSum < bitLenLo) {
						if (bitError > 0) bitError = 0;
						else if (bitError > -10) --bitError;
						if (bitError <= -3) {
							bitWidthX256 += 10 * (bitError + 2);
							--pos;
						}
					} else if (edgeSum > bitLenHi) {
						if (bitError < 0) bitError = 0;
						else if (bitError < 10) ++bitError;
						if (bitError >= 3) {
							bitWidthX256 += 10 * (bitError - 2);
							++pos;
						}
					} else
						bitError = 0;
				}

				shifter >>= 1;
				if (sumAndNextInfo.mNextBit)
					shifter += 0x100;

				pos = next;
				lastByteEnd = pos;
			}

			if (stopOnFramingError && !(shifter & 0x100)) {
				blockEndedEarly = true;
				break;
			}

			if (!dblock) {
				dblock = &output.mBlocks.emplace_back();
				dblock->mSampleStart = startBitPos;
				dblock->mSampleEnd = startBitPos;
				dblock->mSampleValidEnd = startBitPos;
				dblock->mStartByte = (uint32)output.mByteData.size();
				dblock->mByteCount = 0;
				dblock->mChecksumPos = 0;
				dblock->mBaudRate = 0;
				dblock->mSuspiciousBit = 0;
				dblock->mbSuspiciousBitPolarity = false;
			}

			TapeDecodedByte& dbyte = output.mByteData.emplace_back();
			dbyte.mData = (uint8)shifter;
			dbyte.mFlags = shifter & 0x100 ? TapeDecodedByteFlags::None : TapeDecodedByteFlags::FramingError;
			dbyte.mStartSample = nextSyncInfo.mPos;

			for (int i = 0; i < 10; ++i)
				dbyte.mBitSampleOffsets[i] = (uint16)(bitSamplePos[i] - dbyte.mStartSample);

			++dblock->mByteCount;
		}

		if (!dblock)
			continue;

		dblock->mSampleEnd = std::min(lastByteEnd + ((bitWidthX256 + 256) >> 9), pos);

		output.mByteData.emplace_back();
		output.mByteData.back().mStartSample = dblock->mSampleEnd;

		if (dblock->mByteCount >= 132) {
			const TapeDecodedByte *bdata = output.mByteData.begin() + dblock->mStartByte;
			uint32 sum = 0;
			for (uint32 i = 0; i < 131; ++i)
				sum += bdata[i].mData;

			uint8 chk = sum ? (sum - 1) % 255 + 1 : 0;
			bool framingOK = std::find_if(bdata, bdata + 131,
				[](const TapeDecodedByte& dbyte) {
					return (dbyte.mFlags & TapeDecodedByteFlags::FramingError) != TapeDecodedByteFlags::None;
				}) == bdata + 131;

			for (uint32 i = 131; i < dblock->mByteCount; ++i) {
				if ((bdata[i].mFlags & TapeDecodedByteFlags::FramingError) != TapeDecodedByteFlags::None)
					framingOK = false;

				const uint8 c = bdata[i].mData;
				if (chk == c) {
					if (framingOK || !dblock->mbValidFrame)
						dblock->mChecksumPos = i;
					if (framingOK) {
						dblock->mbValidFrame = true;
						dblock->mSampleValidEnd = dblock->mSampleEnd;
					}
				}

				sum = (uint32)chk + bdata[i].mData;
				chk = (uint8)(sum + (sum >> 8));
			}

			if (!dblock->mbValidFrame)
				TryIdentifySuspiciousBit(output, *dblock, 0, 131, bdata[131].mData);
		}

		const uint32 numBytes = dblock->mByteCount;
		const uint32 numBits = 10 * numBytes;
		uint32 baudRate = VDRoundToInt(kATCassetteDataSampleRate * (float)numBits / (float)(dblock->mSampleEnd - dblock->mSampleStart));

		ATCassetteWriteCursor writeCursor;
		writeCursor.mPosition = dblock->mSampleStart;
		for (int i = 0; i < 5; ++i) {
			uint32 neededSamples = mpImage->EstimateWriteStdData(writeCursor, numBytes, baudRate);
			if (neededSamples <= pos - dblock->mSampleStart)
				break;
			++baudRate;
		}

		dblock->mBaudRate = baudRate;

		if (blockEndedEarly)
			break;
	}
}

// ---- T2000 Decode ----

void ATTapeEditorState::DecodeT2000(uint32 start, uint32 end, TapeDecodedBlocks& output) const {
	if (start >= end)
		return;

	bool polarity = false;
	uint32 lastPulseWidth = 0;
	uint32 pilotWindow[16] {};
	uint32 pilotWindowIndex = 0;
	uint32 pilotWindowSum = 8;
	uint32 pilotCount = 0;

	enum class State : uint8 {
		Pilot, Sync1, Sync2,
		Data0a, Data0b, Data1a, Data1b, Data2a, Data2b, Data3a, Data3b,
		Data4a, Data4b, Data5a, Data5b, Data6a, Data6b, Data7a, Data7b,
	} state = State::Pilot;

	uint32 pos = start;
	uint8 c = 0;
	uint32 byteStart = 0;

	while (pos < end) {
		const auto bitInfo = mpImage->FindNextBit(pos, end, polarity, true);
		polarity = !polarity;

		if (bitInfo.mPos >= end)
			break;

		const uint32 pulseWidth = bitInfo.mPos - pos;
		pos = bitInfo.mPos;

		const uint32 cycleWidth = pulseWidth + lastPulseWidth;
		lastPulseWidth = pulseWidth;

		switch (state) {
			case State::Pilot: {
				uint32 avgPilotWidth = pilotWindowSum >> 4;
				pilotWindowSum -= pilotWindow[pilotWindowIndex];
				pilotWindowSum += cycleWidth;
				pilotWindow[pilotWindowIndex] = cycleWidth;
				pilotWindowIndex = (pilotWindowIndex + 1) & 15;

				if (avgPilotWidth > 5) {
					uint32 minPilotWidth = avgPilotWidth - 2;
					uint32 maxPilotWidth = avgPilotWidth + 2;

					if (cycleWidth < minPilotWidth) {
						if (pilotCount >= 16)
							state = State::Sync1;
						pilotCount = 0;
					} else if (cycleWidth > maxPilotWidth)
						pilotCount = 0;
					else
						++pilotCount;
				}
				break;
			}

			case State::Sync1:
				state = State::Sync2;
				break;

			case State::Sync2:
				state = State::Data0a;
				byteStart = pos;
				break;

			case State::Data0a: case State::Data1a: case State::Data2a: case State::Data3a:
			case State::Data4a: case State::Data5a: case State::Data6a: case State::Data7a:
				state = (State)((uint8)state + 1);
				break;

			case State::Data0b: case State::Data1b: case State::Data2b: case State::Data3b:
			case State::Data4b: case State::Data5b: case State::Data6b:
				c += c;
				if (cycleWidth >= 16)
					++c;
				state = (State)((uint8)state + 1);
				break;

			case State::Data7b: {
				c += c;
				if (cycleWidth >= 16)
					++c;

				if (output.mBlocks.empty()) {
					auto& dblock = output.mBlocks.emplace_back();
					dblock.mSampleStart = byteStart;
					dblock.mSampleEnd = pos;
					dblock.mSampleValidEnd = byteStart;
					dblock.mBaudRate = 0;
					dblock.mStartByte = 0;
					dblock.mByteCount = 0;
					dblock.mChecksumPos = 0;
					dblock.mbValidFrame = false;
					dblock.mSuspiciousBit = 0;
					dblock.mbSuspiciousBitPolarity = false;

					output.mByteData.emplace_back();
					output.mByteData.back().mStartSample = byteStart;
				}

				auto& dblock2 = output.mBlocks.back();
				byteStart = pos;
				dblock2.mSampleEnd = pos;
				++dblock2.mByteCount;

				output.mByteData.back().mData = c;

				output.mByteData.emplace_back();
				output.mByteData.back().mStartSample = pos;

				state = State::Data0a;
				break;
			}
		}
	}

	if (!output.mBlocks.empty()) {
		auto& dblock = output.mBlocks.back();

		uint8 chk = output.mByteData[0].mData;
		uint32 n = dblock.mByteCount;
		for (uint32 i = 1; i < n; ++i) {
			chk ^= output.mByteData[i].mData;
			if (chk == 0) {
				dblock.mChecksumPos = i;
				dblock.mbValidFrame = true;
				dblock.mSampleValidEnd = dblock.mSampleEnd;
			}
		}
	}
}

// ---- Suspicious bit identification ----

bool ATTapeEditorState::TryIdentifySuspiciousBit(const TapeDecodedBlocks& dblocks, TapeDecodedBlock& dblock, uint32 forcedSyncBytes, uint32 checksumPos, uint8 receivedSum) {
	if (dblock.mByteCount < checksumPos)
		return false;

	uint32 computedSum32 = forcedSyncBytes * 0x55;
	for (uint32 i = 0; i < checksumPos; ++i)
		computedSum32 += dblocks.mByteData[dblock.mStartByte + i].mData;

	const uint8 computedSum = computedSum32 ? (uint8)((computedSum32 - 1) % 255 + 1) : 0xFF;

	for (int bitPos = 0; bitPos < 8; ++bitPos) {
		uint8 bit = 1 << bitPos;

		if (computedSum == (255 - receivedSum >= bit ? receivedSum + bit : receivedSum + bit - 255)) {
			dblock.mSuspiciousBit = (uint8)(bitPos + 1);
			dblock.mbSuspiciousBitPolarity = true;
			dblock.mChecksumPos = 131;
			return true;
		} else if (computedSum == (receivedSum > bit ? receivedSum - bit : receivedSum + 255 - bit)) {
			dblock.mSuspiciousBit = (uint8)(bitPos + 1);
			dblock.mbSuspiciousBitPolarity = false;
			dblock.mChecksumPos = 131;
			return true;
		}
	}

	return false;
}

// ---- Helper methods ----

uint32 ATTapeEditorState::PreModify() {
	++mTapeChangedLock;
	ATCassetteEmulator& cas = g_sim.GetCassette();
	return cas.IsLoaded() ? cas.OnPreModifyTape() : 0;
}

void ATTapeEditorState::PostModify(uint32 newPos) {
	ATCassetteEmulator& cas = g_sim.GetCassette();
	if (cas.IsLoaded())
		cas.OnPostModifyTape(newPos);

	mSampleCount = mpImage->GetDataLength();
	--mTapeChangedLock;
}

void ATTapeEditorState::OnTapeModified() {
	if (!mTapeChangedLock) {
		ClearUndoRedo();

		if (mpImage)
			mSampleCount = mpImage->GetDataLength();
		else
			mSampleCount = 0;

		UpdateScrollLimit();
	}
}

void ATTapeEditorState::SetImage(IATCassetteImage *image) {
	if (mpImage != image) {
		mpImage = image;
		OnTapeModified();

		if (!mViewResetLock) {
			mZoom = -12;
			mScrollX = mSampleCount >> (12 + 1);
		}
	}
}

void ATTapeEditorState::SetSelection(uint32 startSample, uint32 endSample) {
	mbSelectionValid = true;
	mSelStartSample = startSample;
	mSelEndSample = endSample;
	mSelSortedStartSample = std::min(startSample, endSample);
	mSelSortedEndSample = std::max(startSample, endSample);
}

void ATTapeEditorState::ClearSelection() {
	mbSelectionValid = false;
}

void ATTapeEditorState::EnsureSelectionVisible() {
	if (!mbSelectionValid)
		return;

	uint32 startSample = mSelSortedStartSample;
	uint32 endSample = mSelSortedEndSample;

	sint64 centerSample = startSample + ((endSample - startSample + 1) >> 1);
	mScrollX = mZoom < 0 ? centerSample >> -mZoom : centerSample << mZoom;

	sint64 len = mSampleCount;
	if (len) {
		if (mZoom < 0)
			len = ((len - 1) >> -mZoom) + 1;
		else
			len <<= mZoom;
	}
	mScrollX = std::clamp<sint64>(mScrollX, 0, len);
}

void ATTapeEditorState::UpdateScrollLimit() {
	sint64 len = mSampleCount;
	if (len) {
		if (mZoom < 0)
			len = ((len - 1) >> -mZoom) + 1;
		else
			len <<= mZoom;
	}
	mScrollMax = len;
}

void ATTapeEditorState::SetZoom(sint32 newZoom, sint32 centerClientX, sint32 viewWidth) {
	newZoom = std::clamp(newZoom, -20, 16);
	if (mZoom == newZoom)
		return;

	const sint32 hw = viewWidth >> 1;
	sint32 xoff = centerClientX - hw;
	const int zoomChange = newZoom - mZoom;
	mZoom = newZoom;

	if (zoomChange < 0)
		mScrollX = ((mScrollX + xoff) >> -zoomChange) - xoff;
	else
		mScrollX = ((mScrollX + xoff) << zoomChange) - xoff;

	UpdateScrollLimit();
	mScrollX = std::clamp<sint64>(mScrollX, 0, mScrollMax);
}

// ---- Coordinate conversion ----

uint32 ATTapeEditorState::ClientXToSampleEdge(float x, float viewWidth, bool clampToLength) const {
	float hw = viewWidth * 0.5f;
	sint64 gpx = (sint64)(mScrollX + (sint64)(x - hw));

	if (mZoom <= 0)
		gpx <<= -mZoom;
	else
		gpx = ((gpx >> (mZoom - 1)) + 1) >> 1;

	return (uint32)std::min<uint64>((uint64)std::max<sint64>(gpx, 0), clampToLength ? mSampleCount : kATCassetteDataLimit);
}

uint32 ATTapeEditorState::ClientXToSample(float x, float viewWidth) const {
	float hw = viewWidth * 0.5f;
	sint64 gpx = (sint64)(mScrollX + (sint64)(x - hw));

	if (mZoom <= 0)
		gpx <<= -mZoom;
	else
		gpx >>= mZoom;

	return (uint32)std::min<uint64>((uint64)std::max<sint64>(gpx, 0), mSampleCount);
}

float ATTapeEditorState::SampleToClientX(uint32 sample, float viewWidth) const {
	sint64 gpx = (sint64)sample;
	if (mZoom < 0)
		gpx = ((gpx >> (-mZoom - 1)) + 1) >> 1;
	else
		gpx <<= mZoom;

	return (float)(gpx + (sint64)(viewWidth * 0.5f) - mScrollX);
}

float ATTapeEditorState::SampleEdgeToClientX(uint32 sample, float viewWidth) const {
	sint64 gpx = (sint64)sample;
	if (mZoom <= 0)
		gpx >>= -mZoom;
	else
		gpx <<= mZoom;

	return (float)(gpx + (sint64)(viewWidth * 0.5f) - mScrollX);
}
