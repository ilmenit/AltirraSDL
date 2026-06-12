//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2025 Avery Lee
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

//=========================================================================
// PokeyMax SID core (MOS 6581 / 8580 compatible)
//
// This class is a clone of ATSlightSIDEmulator (slightsid.cpp/.h, left
// UNTOUCHED) extended with the accuracy elements that SlightSID lacks:
//
//   * measured combined-waveform ROM tables (reSID/Dag Lem, GPL-2.0+) for
//     both the MOS 6581 and the MOS 8580, replacing SlightSID's logical-AND
//     approximation of the $30/$50/$60/$70 combined waveforms;
//   * the correct 24-bit noise LFSR taps (bit22 ^ bit17, seed 0x7ffff8)
//     instead of SlightSID's approximate taps;
//   * 6581-vs-8580 model selection (SetModel) driven by PokeyMax SIDMODE;
//   * optional OSC3 ($1B) / ENV3 ($1C) read-back.
//
// PokeyMax-specific integration deltas vs SlightSID:
//
//   * No self-installed memory layer. PokeyMax owns the whole-page $D2
//     config overlay and forwards register accesses via WriteControl /
//     ReadControl. (Drop memMan / mpMemLayerControl.)
//   * SetEnabled() gates the audio output (RESTRICT bit2).
//   * SetClock() recomputes the PHI2-derived SID step ratio. The PokeyMax
//     SID clock tracks the Atari PHI2 machine clock, NOT the fixed C64
//     0.985 MHz used by SlightSID:  PAL = PHI2 x 5/9, NTSC = PHI2 x 4/7.
//   * chipIndex (0 = SID1, 1 = SID2) routes SID1 -> left, SID2 -> right.
//   * SaveState / LoadState serialize the full audio phase for snapshots.
//=========================================================================

#ifndef f_AT_POKEYMAXSID_H
#define f_AT_POKEYMAXSID_H

#include <vd2/system/memory.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/audiosource.h>

class ATScheduler;
class IATAudioMixer;
class ATConsoleOutput;

class ATPokeyMaxSIDEmulator final : public VDAlignedObject<16>, public IATSyncAudioSource {
	ATPokeyMaxSIDEmulator(const ATPokeyMaxSIDEmulator&) = delete;
	ATPokeyMaxSIDEmulator& operator=(const ATPokeyMaxSIDEmulator&) = delete;
public:
	ATPokeyMaxSIDEmulator();
	~ATPokeyMaxSIDEmulator();

	// chipIndex (0 = SID1, 1 = SID2) selects the default L/R side: SID1 ->
	// left, SID2 -> right. Does NOT install a memory layer -- PokeyMax owns
	// the $D2 overlay and forwards register accesses.
	void Init(ATScheduler *sch, IATAudioMixer *mixer, int chipIndex);
	void Shutdown();

	void ColdReset();
	void WarmReset();

	void DumpStatus(ATConsoleOutput& output);

	// Direct, memory-mapped register access (no address/data latch). addr is
	// 0-24 ($00-$18). Higher reserved addresses are handled by the caller.
	void WriteControl(uint8 addr, uint8 value);
	uint8 ReadControl(uint8 addr) const;

	// Gate audio output (RESTRICT bit2). Leads with Flush() so the change
	// takes effect exactly at "now".
	void SetEnabled(bool enabled);

	// Select 6581 vs 8580 combined-waveform table set; digifix flag is stored
	// for the follow-up digifix DC behaviour. Leads with Flush().
	void SetModel(bool is6581, bool digifix);

	// Altirra extension (not real PokeyMax hardware): choose the waveform/noise
	// generation engine. accurate==true  -> the "MAME" engine (measured
	// combined-waveform ROM tables + correct 24-bit noise LFSR). accurate==false
	// -> the original "SlightSID" engine (logical-AND combined waveforms +
	// approximate noise taps). Only the combined-waveform and noise paths
	// differ; clock, ADSR, filter, SYNC/RING/TEST are identical. Leads with
	// Flush().
	void SetEngineMode(bool accurate);

	// Recompute the PHI2 -> SID step ratio: PAL = 5/9, NTSC = 4/7 of PHI2.
	// Leads with Flush() then rescales the fractional step residue. machine
	// clock is kept for reference/future use; the ratio depends only on
	// palMode.
	void SetClock(double machineClockHz, bool palMode);

	const uint8 *GetRegisters() const { return mRegisters; }
	uint8 GetEnvelopeValue(int ch) const { return mChannels[ch].mEnvelope; }
	int GetEnvelopeMode(int ch) const { return mChannels[ch].mEnvelopeMode; }

	void Run(uint32 cycles);

	// Raw state serialization for the PokeyMax composite snapshot.
	void SaveState(vdfastvector<uint8>& dst) const;
	void LoadState(const uint8 *data, size_t len);

public:	// IATSyncAudioSource
	bool RequiresStereoMixingNow() const override { return true; }
	void WriteAudio(const ATSyncAudioMixInfo& mixInfo) override;

protected:
	void Flush();
	void RecomputeVolumeScale();

	struct Channel {
		uint32		mPrevPhase;
		uint32		mPhase;			// phase accumulator (freq in bits 8-23)
		uint32		mFreq;			// 16-bit frequency shifted into bits 8-23
		uint32		mPulseWidth;	// 12-bit pulse width shifted into bits 20-31
		sint32		mPrescalerAccum;
		bool		mbSync;
		bool		mbRingMod;
		bool		mbTestOn;
		uint8		mEnvelopeMode;
		uint8		mEnvelope;
		uint8		mWaveform;		// bit0 tri, bit1 saw, bit2 pulse, bit3 noise
		uint8		mAttack;
		uint8		mDecay;
		uint8		mSustain;
		uint8		mRelease;
		uint8		mControl;
		sint32		mFilteredEnable;
		sint32		mNonFilteredEnable;
		uint32		mNoiseLFSR;
		uint8		mLastWaveSample;	// last 8-bit waveform output (OSC3)
	};

	ATScheduler *mpScheduler = nullptr;
	IATAudioMixer *mpAudioMixer = nullptr;
	int mChipIndex = 0;
	bool mbRegistered = false;
	bool mbEnabled = false;

	// Chip model selection. mb6581 picks the 6581 combined-waveform tables;
	// false picks the 8580 tables. mbDigifix is stored for the digifix DC
	// behaviour (follow-up).
	bool mb6581 = true;
	bool mbDigifix = false;

	// Altirra extension: waveform/noise engine selector. true = "MAME" engine
	// (ROM tables + correct noise), false = "SlightSID" engine (logical-AND +
	// approximate noise). Default true to preserve current behaviour.
	bool mbAccurateEngine = true;

	// PHI2-derived SID step ratio. mStepNum SID steps per mStepDen PHI2
	// cycles: PAL = 5/9, NTSC = 4/7.
	uint32 mStepNum = 5;
	uint32 mStepDen = 9;
	uint64 mSidStepAccum = 0;	// persistent fractional step residue

	// Active combined-waveform table pointers (point at the 6581 or 8580
	// table set selected by SetModel).
	const uint8 *mpWaveform30 = nullptr;
	const uint8 *mpWaveform50 = nullptr;
	const uint8 *mpWaveform60 = nullptr;
	const uint8 *mpWaveform70 = nullptr;

	float	mVolumeScale = 0;
	float	mFilterDelayX1 = 0;
	float	mFilterDelayX2 = 0;
	float	mFilterDelayY1 = 0;
	float	mFilterDelayY2 = 0;
	float	mFilterCoeffB0 = 0;
	float	mFilterCoeffB1 = 0;
	float	mFilterCoeffB2 = 0;
	float	mFilterCoeffA0 = 0;
	float	mFilterCoeffA1 = 0;
	float	mFilterCoeffA2 = 0;

	float	mOutputAccumNF = 0;
	float	mOutputAccumF = 0;
	uint32	mOutputCount = 0;
	uint32	mOutputLevel = 0;

	uint32	mLastUpdate = 0;

	uint8	mRegisters[32] = {};

	Channel mChannels[3] = {};

	enum {
		kAccumBufferSize = 1536
	};

	VDALIGN(16) float mAccumBuffer[kAccumBufferSize] = {};
};

#endif
