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
// YM2149 / AY-3-8910 PSG emulator (used by the PokeyMax device)
//
// Self-contained, accurate emulation of one YM2149 / AY-3-8910-compatible
// programmable sound generator, registering itself as an
// IATSyncAudioSource. PokeyMax instantiates two of these (PSG1 / PSG2) and
// drives them from its memory-mapped register windows ($D2A0-$D2AF and
// $D2B0-$D2BF) plus the PSGMODE configuration register ($D215).
//
// The generator algorithm and the DAC volume tables are re-implemented from
// the AY-3-8910 / YM2149 description; see ympsg.cpp for the MAME reference
// and its BSD-3-Clause attribution covering the verbatim volume tables.
//=========================================================================

#ifndef f_AT_YMPSG_H
#define f_AT_YMPSG_H

#include <vd2/system/memory.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/audiosource.h>
#include <at/atcore/audiomixer.h>

class ATScheduler;
class IATAudioMixer;
class ATConsoleOutput;

class ATYMPSGEmulator final : public VDAlignedObject<16>, public IATSyncAudioSource {
	ATYMPSGEmulator(const ATYMPSGEmulator&) = delete;
	ATYMPSGEmulator& operator=(const ATYMPSGEmulator&) = delete;
public:
	ATYMPSGEmulator();
	~ATYMPSGEmulator();

	// chipIndex (0 = PSG1, 1 = PSG2) selects the default L/R side in the
	// dual-chip "Max" stereo mode. machineClockHz is the live PHI2 machine
	// clock (NTSC 1789772.5 / PAL 1773447) used to convert machine cycles to
	// PSG steps and to derive the PHI2 master-clock FREQ option.
	void Init(ATScheduler *sch, IATAudioMixer *mixer, int chipIndex, double machineClockHz);
	void Shutdown();

	void ColdReset();
	void WarmReset();

	void SetEnabled(bool enabled);

	// Decode the PokeyMax PSGMODE register ($D215): master clock (FREQ),
	// stereo routing (STEREO), envelope step count (ENV16) and volume curve
	// (VOLP).
	void SetMode(uint8 psgmode);

	// Direct, memory-mapped register access (no address/data latch). reg is
	// 0-15.
	void WriteReg(uint8 reg, uint8 value);
	uint8 ReadReg(uint8 reg) const;

	void DumpStatus(ATConsoleOutput& output);

	// Raw state serialization for the PokeyMax composite snapshot. The blob
	// is a self-describing little-endian packing of the register file plus
	// generator state.
	void SaveState(vdfastvector<uint8>& dst) const;
	void LoadState(const uint8 *data, size_t len);

public:	// IATSyncAudioSource
	bool RequiresStereoMixingNow() const override;
	void WriteAudio(const ATSyncAudioMixInfo& mixInfo) override;

private:
	void Flush();
	void Run(uint64 targetCycle);
	void StepOnce();
	void EmitOutputs(uint32 t);
	void NoiseRngTick();
	void RecomputeTiming();
	void ResyncStepClock();
	void RebuildVolumeTables();
	void PushEdge(ATSyncAudioEdgeBuffer& buf, uint32 t, float delta);

	ATScheduler *mpScheduler = nullptr;
	IATAudioMixer *mpAudioMixer = nullptr;
	int mChipIndex = 0;
	double mMachineClockHz = 1789772.5;
	bool mbEnabled = false;
	bool mbRegistered = false;

	// PSGMODE-derived configuration.
	uint8 mPSGMode = 0;
	double mPSGClockHz = 2000000.0;
	int mStereoMode = 0;		// 0=mono, 1=Polish, 2=Czech, 3=Max
	bool mbStereoActive = false;	// true when L and R can differ
	bool mbEnv16 = false;		// true=16-step (AY), false=32-step (YM)
	bool mbLinearVol = false;	// true=linear curve, false=log (default)

	// Register file (R0-R15).
	uint8 mReg[16] = {};

	// Tone generators.
	uint32 mTonePeriod[3] = {};
	sint32 mToneCount[3] = {};
	uint8 mToneOutput[3] = {};
	uint8 mToneDuty[3] = {};

	// Noise generator (5-bit period, 17-bit LFSR).
	sint32 mNoiseCount = 0;
	uint8 mNoisePrescale = 0;
	uint32 mNoiseRng = 1;

	// Envelope generator (shared across channels in standard mode).
	uint32 mEnvCount = 0;
	sint32 mEnvStep = 0;
	uint8 mEnvHold = 0;
	uint8 mEnvAlternate = 0;
	uint8 mEnvAttack = 0;
	uint8 mEnvHolding = 0;
	uint32 mEnvVolume = 0;
	uint8 mEnvStepMask = 0x1F;
	uint32 mEnvStepMul = 1;

	// Step-clock timing (machine cycles, fixed-point 16.16).
	uint64 mNextStepCycleFP = 0;
	uint64 mCyclesPerStepFP = 0;
	bool mbStepClockValid = false;

	// Output edge tracking.
	float mLastLeft = 0;
	float mLastRight = 0;

	// DAC tables, normalized to [0,1] with the off level mapped to 0.
	float mVolTableLog[16] = {};
	float mEnvTableLog[32] = {};
	float mVolTableLin[16] = {};
	float mEnvTableLin[32] = {};

	vdrefptr<ATSyncAudioEdgeBuffer> mpEdgeBufferL;
	vdrefptr<ATSyncAudioEdgeBuffer> mpEdgeBufferR;
};

#endif
