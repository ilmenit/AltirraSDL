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
//
//=========================================================================
// The YM2149 / AY-3-8910 generator algorithm and the two verbatim DAC
// resistor-ladder tables below (ym2149 fixed 16-level and envelope 32-level)
// are derived from MAME's ay8910 device, which carries the following
// attribution. MAME is used here only as an algorithmic reference; no MAME
// source is linked into this build.
//
//   ay8910.cpp / ay8910.h
//   license:BSD-3-Clause
//   copyright-holders:Couriersud
//
// The BSD-3-Clause license permits redistribution of these tables in source
// form provided the copyright notice is retained, which it is here.
//=========================================================================

#include <stdafx.h>
#include <math.h>
#include <at/atcore/audiomixer.h>
#include <at/atcore/consoleoutput.h>
#include <at/atcore/scheduler.h>
#include "ympsg.h"

namespace {
	// YM2149 DAC resistor-ladder values (ohms) from MAME's ay8910.cpp,
	// license BSD-3-Clause, copyright-holders:Couriersud. The first two
	// constants are the pull-up / pull-down resistances; the arrays are the
	// per-level series resistances.
	constexpr double kYM2149_RUp = 630.0;
	constexpr double kYM2149_RDown = 801.0;

	// Fixed 16-level DAC (register volume bits 0-3).
	constexpr double kYM2149Res16[16] = {
		73770, 37586, 27458, 21451, 15864, 12371, 8922,  6796,
		 4763,  3521,  2403,  1737,  1123,   762,  438,   251
	};

	// Envelope 32-level DAC (YM2149 envelope generator).
	constexpr double kYM2149Res32[32] = {
		103350, 73770, 52657, 37586, 32125, 27458, 24269, 21451,
		 18447, 15864, 14009, 12371, 10506,  8922,  7787,  6796,
		  5689,  4763,  4095,  3521,  2909,  2403,  2043,  1737,
		  1397,  1123,   925,   762,   578,   438,   332,   251
	};

	// Default external load resistance assumed by the resistor-ladder model.
	constexpr double kLoadResistance = 1000.0;

	// Convert a series resistance to a normalized DAC output ratio using the
	// resistor-divider model (build_single_table in ay8910.cpp). The result
	// is in roughly [0.3, 0.7]; callers offset and rescale.
	double ResToRatio(double res) {
		const double rt = 1.0 / kYM2149_RDown + 1.0 / kLoadResistance
			+ 1.0 / res + 1.0 / kYM2149_RUp;
		const double rw = 1.0 / res + 1.0 / kYM2149_RUp;

		return rw / rt;
	}
}

ATYMPSGEmulator::ATYMPSGEmulator() {
	mpEdgeBufferL = new ATSyncAudioEdgeBuffer;
	mpEdgeBufferL->mpDebugLabel = "PSG L";

	mpEdgeBufferR = new ATSyncAudioEdgeBuffer;
	mpEdgeBufferR->mpDebugLabel = "PSG R";

	RebuildVolumeTables();
}

ATYMPSGEmulator::~ATYMPSGEmulator() {
	Shutdown();
}

void ATYMPSGEmulator::RebuildVolumeTables() {
	// Logarithmic (resistor-ladder) curve, offset so the off level maps to 0
	// and normalized so the loudest level maps to 1.
	const double base16 = ResToRatio(kYM2149Res16[0]);
	const double span16 = ResToRatio(kYM2149Res16[15]) - base16;

	for(int i = 0; i < 16; ++i) {
		const double v = (ResToRatio(kYM2149Res16[i]) - base16) / span16;
		mVolTableLog[i] = (float)v;
		mVolTableLin[i] = (float)(i / 15.0);
	}

	const double base32 = ResToRatio(kYM2149Res32[0]);
	const double span32 = ResToRatio(kYM2149Res32[31]) - base32;

	for(int i = 0; i < 32; ++i) {
		const double v = (ResToRatio(kYM2149Res32[i]) - base32) / span32;
		mEnvTableLog[i] = (float)v;
		mEnvTableLin[i] = (float)(i / 31.0);
	}
}

void ATYMPSGEmulator::Init(ATScheduler *sch, IATAudioMixer *mixer, int chipIndex, double machineClockHz) {
	mpScheduler = sch;
	mpAudioMixer = mixer;
	mChipIndex = chipIndex;

	if (machineClockHz > 0)
		mMachineClockHz = machineClockHz;

	if (mixer && !mbRegistered) {
		mixer->AddSyncAudioSource(this);
		mbRegistered = true;
	}

	RecomputeTiming();
	ColdReset();
}

void ATYMPSGEmulator::Shutdown() {
	if (mpAudioMixer) {
		if (mbRegistered) {
			mpAudioMixer->RemoveSyncAudioSource(this);
			mbRegistered = false;
		}

		mpAudioMixer = nullptr;
	}

	mpScheduler = nullptr;
}

void ATYMPSGEmulator::ColdReset() {
	for(int i = 0; i < 16; ++i)
		mReg[i] = 0;

	for(int c = 0; c < 3; ++c) {
		mTonePeriod[c] = 0;
		mToneCount[c] = 0;
		mToneOutput[c] = 0;
		mToneDuty[c] = 0;
	}

	mNoiseCount = 0;
	mNoisePrescale = 0;
	mNoiseRng = 1;

	mEnvCount = 0;
	mEnvStep = mEnvStepMask;
	mEnvHold = 0;
	mEnvAlternate = 0;
	mEnvAttack = 0;
	mEnvHolding = 0;
	mEnvVolume = (uint32)(mEnvStep ^ mEnvAttack);

	WarmReset();
}

void ATYMPSGEmulator::WarmReset() {
	mpEdgeBufferL->mEdges.clear();
	mpEdgeBufferR->mEdges.clear();

	mLastLeft = 0;
	mLastRight = 0;

	mbStepClockValid = false;
}

void ATYMPSGEmulator::SetEnabled(bool enabled) {
	if (mbEnabled == enabled)
		return;

	// Run up to the current time with the old enable state so the change
	// takes effect exactly at "now".
	Flush();

	mbEnabled = enabled;

	if (!enabled) {
		// Bring both channels back to silence so a disabled chip doesn't hold
		// a DC level on the mixbus.
		const uint32 t = mpScheduler ? ATSCHEDULER_GETTIME(mpScheduler) : 0;

		if (mLastLeft != 0.0f) {
			PushEdge(*mpEdgeBufferL, t, -mLastLeft);
			mLastLeft = 0;
		}

		if (mLastRight != 0.0f) {
			PushEdge(*mpEdgeBufferR, t, -mLastRight);
			mLastRight = 0;
		}
	}
}

void ATYMPSGEmulator::SetMode(uint8 psgmode) {
	Flush();

	mPSGMode = psgmode;

	// FREQ (bits 1-0): master clock select.
	switch(psgmode & 0x03) {
		case 0x00:	// 2 MHz
			mPSGClockHz = 2000000.0;
			break;

		case 0x01:	// 1 MHz
			mPSGClockHz = 1000000.0;
			break;

		case 0x02:	// PHI2 (live Atari machine clock; NTSC vs PAL)
		case 0x03:	// reserved -> treat as PHI2
		default:
			mPSGClockHz = mMachineClockHz;
			break;
	}

	// STEREO (bits 3-2): output routing.
	const int newStereo = (psgmode >> 2) & 0x03;
	const bool newStereoActive = (newStereo != 0);

	// ENV16 (bit 4): 0 = 32-step (YM2149), 1 = 16-step (AY-3-8910).
	const bool newEnv16 = (psgmode & 0x10) != 0;

	// VOLP (bits 6-5): 0 = logarithmic curve (default), nonzero = linear.
	mbLinearVol = ((psgmode >> 5) & 0x03) != 0;

	// If the stereo routing topology changes, retract the current output to
	// silence so the new routing starts cleanly.
	if (newStereoActive != mbStereoActive) {
		const uint32 t = mpScheduler ? ATSCHEDULER_GETTIME(mpScheduler) : 0;

		if (mLastLeft != 0.0f) {
			PushEdge(*mpEdgeBufferL, t, -mLastLeft);
			mLastLeft = 0;
		}

		if (mLastRight != 0.0f) {
			PushEdge(*mpEdgeBufferR, t, -mLastRight);
			mLastRight = 0;
		}
	}

	mStereoMode = newStereo;
	mbStereoActive = newStereoActive;

	if (newEnv16 != mbEnv16) {
		mbEnv16 = newEnv16;
		mEnvStepMask = newEnv16 ? 0x0F : 0x1F;
		mEnvStepMul = newEnv16 ? 2 : 1;

		// Re-clamp the current envelope step into the new range.
		mEnvStep &= mEnvStepMask;
		mEnvAttack &= mEnvStepMask;
		mEnvVolume = (uint32)(mEnvStep ^ mEnvAttack);
	}

	RecomputeTiming();
}

void ATYMPSGEmulator::RecomputeTiming() {
	// Internal generators advance at master clock / 8. Convert that step rate
	// into machine cycles per step (fixed-point 16.16).
	if (mPSGClockHz <= 0)
		mPSGClockHz = mMachineClockHz;

	const double cyclesPerStep = mMachineClockHz * 8.0 / mPSGClockHz;

	mCyclesPerStepFP = (uint64)(cyclesPerStep * 65536.0 + 0.5);
	if (mCyclesPerStepFP == 0)
		mCyclesPerStepFP = 1;

	// Force the step clock to re-anchor at the next Run().
	mbStepClockValid = false;
}

void ATYMPSGEmulator::ResyncStepClock() {
	const uint64 nowCycle = mpScheduler ? mpScheduler->GetTick64() : 0;

	mNextStepCycleFP = nowCycle << 16;
	mbStepClockValid = true;
}

void ATYMPSGEmulator::NoiseRngTick() {
	// 17-bit shift register, input = bit0 XOR bit3 (bit0 is the output).
	mNoiseRng = (mNoiseRng >> 1) | (((mNoiseRng ^ (mNoiseRng >> 3)) & 1) << 16);
}

void ATYMPSGEmulator::WriteReg(uint8 reg, uint8 value) {
	reg &= 0x0F;

	// Run the generators up to the current time using the existing register
	// state before applying the change.
	Flush();

	mReg[reg] = value;

	switch(reg) {
		case 0:		// Tone A period fine
		case 1:		// Tone A period coarse
			mTonePeriod[0] = (uint32)mReg[0] | ((uint32)(mReg[1] & 0x0F) << 8);
			break;

		case 2:		// Tone B period
		case 3:
			mTonePeriod[1] = (uint32)mReg[2] | ((uint32)(mReg[3] & 0x0F) << 8);
			break;

		case 4:		// Tone C period
		case 5:
			mTonePeriod[2] = (uint32)mReg[4] | ((uint32)(mReg[5] & 0x0F) << 8);
			break;

		case 13:	// Envelope shape -- always re-triggers, even if unchanged.
			mEnvAttack = (value & 0x04) ? mEnvStepMask : 0x00;

			if ((value & 0x08) == 0) {
				// Continue = 0: map to the equivalent Continue = 1 shape.
				mEnvHold = 1;
				mEnvAlternate = mEnvAttack;
			} else {
				mEnvHold = value & 0x01;
				mEnvAlternate = (value & 0x02) ? 1 : 0;
			}

			mEnvStep = mEnvStepMask;
			mEnvHolding = 0;
			mEnvVolume = (uint32)(mEnvStep ^ mEnvAttack);
			break;

		default:
			break;
	}
}

uint8 ATYMPSGEmulator::ReadReg(uint8 reg) const {
	return mReg[reg & 0x0F];
}

void ATYMPSGEmulator::StepOnce() {
	// Tone generators.
	for(int c = 0; c < 3; ++c) {
		const sint32 period = (sint32)(mTonePeriod[c] ? mTonePeriod[c] : 1);

		if (++mToneCount[c] >= period) {
			do {
				mToneDuty[c] = (uint8)((mToneDuty[c] - 1) & 0x1F);
				mToneOutput[c] = (uint8)(mToneDuty[c] & 1);
				mToneCount[c] -= period;
			} while(mToneCount[c] >= period);
		}
	}

	// Noise generator. Period is the raw 5-bit value (0 behaves like the
	// hardware: the comparison triggers every prescaler tick).
	const sint32 noisePeriod = (sint32)(mReg[6] & 0x1F);
	if (++mNoiseCount >= noisePeriod) {
		mNoiseCount = 0;
		mNoisePrescale ^= 1;

		if (!mNoisePrescale)
			NoiseRngTick();
	}

	// Envelope generator.
	if (!mEnvHolding) {
		const uint32 envPeriod16 = (uint32)mReg[11] | ((uint32)mReg[12] << 8);
		const uint32 period = envPeriod16 * mEnvStepMul;

		if (++mEnvCount >= period) {
			mEnvCount = 0;
			--mEnvStep;

			if (mEnvStep < 0) {
				if (mEnvHold) {
					if (mEnvAlternate)
						mEnvAttack ^= mEnvStepMask;

					mEnvHolding = 1;
					mEnvStep = 0;
				} else {
					if (mEnvAlternate && (mEnvStep & (sint32)(mEnvStepMask + 1)))
						mEnvAttack ^= mEnvStepMask;

					mEnvStep &= mEnvStepMask;
				}
			}
		}
	}

	mEnvVolume = (uint32)(mEnvStep ^ mEnvAttack);
}

void ATYMPSGEmulator::EmitOutputs(uint32 t) {
	const uint8 noiseOut = (uint8)(mNoiseRng & 1);

	float chAmp[3];
	for(int c = 0; c < 3; ++c) {
		const uint8 toneDisable = (uint8)((mReg[7] >> c) & 1);
		const uint8 noiseDisable = (uint8)((mReg[7] >> (3 + c)) & 1);
		const uint8 enabled = (uint8)((mToneOutput[c] | toneDisable) & (noiseOut | noiseDisable));

		const uint8 volReg = mReg[8 + c];

		if (volReg & 0x10) {
			// Envelope-controlled level.
			const uint32 ev = enabled ? mEnvVolume : 0;

			if (mbEnv16)
				chAmp[c] = mbLinearVol ? mVolTableLin[ev & 0x0F] : mVolTableLog[ev & 0x0F];
			else
				chAmp[c] = mbLinearVol ? mEnvTableLin[ev & 0x1F] : mEnvTableLog[ev & 0x1F];
		} else {
			// Fixed 4-bit level.
			const uint32 lv = enabled ? (uint32)(volReg & 0x0F) : 0;

			chAmp[c] = mbLinearVol ? mVolTableLin[lv] : mVolTableLog[lv];
		}
	}

	const float a = chAmp[0];
	const float b = chAmp[1];
	const float c = chAmp[2];

	if (!mbStereoActive) {
		// Mono: everything goes to the left buffer (mixed to both outputs in
		// WriteAudio).
		const float out = a + b + c;
		const float dL = out - mLastLeft;

		if (dL != 0.0f) {
			PushEdge(*mpEdgeBufferL, t, dL);
			mLastLeft = out;
		}

		return;
	}

	float L;
	float R;

	switch(mStereoMode) {
		case 1:		// Polish: L = A+B, R = B+C
			L = a + b;
			R = b + c;
			break;

		case 2:		// Czech: L = A+C, R = B+C
			L = a + c;
			R = b + c;
			break;

		case 3:		// Max (dual chip): PSG1 -> left, PSG2 -> right
		default:
			if (mChipIndex == 0) {
				L = a + b + c;
				R = 0;
			} else {
				L = 0;
				R = a + b + c;
			}
			break;
	}

	const float dL = L - mLastLeft;
	if (dL != 0.0f) {
		PushEdge(*mpEdgeBufferL, t, dL);
		mLastLeft = L;
	}

	const float dR = R - mLastRight;
	if (dR != 0.0f) {
		PushEdge(*mpEdgeBufferR, t, dR);
		mLastRight = R;
	}
}

void ATYMPSGEmulator::PushEdge(ATSyncAudioEdgeBuffer& buf, uint32 t, float delta) {
	if (!buf.mEdges.empty() && buf.mEdges.back().mTime == t)
		buf.mEdges.back().mDeltaValue += delta;
	else
		buf.mEdges.push_back(ATSyncAudioEdge { .mTime = t, .mDeltaValue = delta });
}

void ATYMPSGEmulator::Run(uint64 targetCycle) {
	if (!mbStepClockValid)
		ResyncStepClock();

	const uint64 targetFP = targetCycle << 16;

	// Guard against a large backlog (e.g. after a long pause) by snapping
	// forward without generating an unbounded number of steps.
	if (mNextStepCycleFP + (mCyclesPerStepFP << 18) < targetFP)
		mNextStepCycleFP = targetFP - (mCyclesPerStepFP << 18);

	while(mNextStepCycleFP <= targetFP) {
		const uint32 t = (uint32)(mNextStepCycleFP >> 16);

		StepOnce();

		if (mbEnabled)
			EmitOutputs(t);

		mNextStepCycleFP += mCyclesPerStepFP;
	}
}

void ATYMPSGEmulator::Flush() {
	if (!mpScheduler)
		return;

	Run(mpScheduler->GetTick64());
}

bool ATYMPSGEmulator::RequiresStereoMixingNow() const {
	return mbStereoActive;
}

void ATYMPSGEmulator::WriteAudio(const ATSyncAudioMixInfo& mixInfo) {
	Flush();

	// 1/3 keeps the three summed channels within unity headroom.
	const float scale = 1.0f / 3.0f;
	const float volume = mixInfo.mpMixLevels[kATAudioMix_Other] * scale;

	auto& edgePlayer = mpAudioMixer->GetEdgePlayer();

	if (mbStereoActive) {
		mpEdgeBufferL->mLeftVolume = volume;
		mpEdgeBufferL->mRightVolume = 0;
		mpEdgeBufferR->mLeftVolume = 0;
		mpEdgeBufferR->mRightVolume = volume;

		if (!mpEdgeBufferL->mEdges.empty())
			edgePlayer.AddEdgeBuffer(mpEdgeBufferL);

		if (!mpEdgeBufferR->mEdges.empty())
			edgePlayer.AddEdgeBuffer(mpEdgeBufferR);
	} else {
		mpEdgeBufferL->mLeftVolume = volume;
		mpEdgeBufferL->mRightVolume = volume;

		if (!mpEdgeBufferL->mEdges.empty())
			edgePlayer.AddEdgeBuffer(mpEdgeBufferL);
	}
}

void ATYMPSGEmulator::DumpStatus(ATConsoleOutput& output) {
	output("PSG registers:");
	output("  Tone A: period=%-4u vol=$%02X", mTonePeriod[0], mReg[8]);
	output("  Tone B: period=%-4u vol=$%02X", mTonePeriod[1], mReg[9]);
	output("  Tone C: period=%-4u vol=$%02X", mTonePeriod[2], mReg[10]);
	output("  Noise period=%-2u  Mixer=$%02X", mReg[6] & 0x1F, mReg[7]);
	output("  Envelope period=%-5u shape=$%02X step=%d",
		(uint32)mReg[11] | ((uint32)mReg[12] << 8), mReg[13], mEnvStep);
	output("  Clock=%.0f Hz  Stereo=%d  Env%s  %s",
		mPSGClockHz, mStereoMode, mbEnv16 ? "16" : "32",
		mbLinearVol ? "linear" : "log");
}

//=========================================================================
// Raw state serialization
//
// The blob layout is fixed and little-endian:
//   [0..15]   register file R0-R15
//   tone:     period[3] (u16), count[3] (s16), output[3] (u8), duty[3] (u8)
//   noise:    count (s16), prescale (u8), rng (u32)
//   envelope: count (u32), step (s16), hold/alt/attack/holding (u8 each),
//             volume (u8)
//=========================================================================

namespace {
	void PutU8(vdfastvector<uint8>& v, uint8 x) { v.push_back(x); }
	void PutU16(vdfastvector<uint8>& v, uint16 x) { v.push_back((uint8)x); v.push_back((uint8)(x >> 8)); }
	void PutU32(vdfastvector<uint8>& v, uint32 x) {
		v.push_back((uint8)x);
		v.push_back((uint8)(x >> 8));
		v.push_back((uint8)(x >> 16));
		v.push_back((uint8)(x >> 24));
	}

	uint8 GetU8(const uint8 *&p) { return *p++; }
	uint16 GetU16(const uint8 *&p) { uint16 x = (uint16)(p[0] | (p[1] << 8)); p += 2; return x; }
	uint32 GetU32(const uint8 *&p) {
		uint32 x = (uint32)p[0] | ((uint32)p[1] << 8) | ((uint32)p[2] << 16) | ((uint32)p[3] << 24);
		p += 4;
		return x;
	}
}

void ATYMPSGEmulator::SaveState(vdfastvector<uint8>& dst) const {
	dst.clear();

	for(int i = 0; i < 16; ++i)
		PutU8(dst, mReg[i]);

	for(int c = 0; c < 3; ++c)
		PutU16(dst, (uint16)mTonePeriod[c]);
	for(int c = 0; c < 3; ++c)
		PutU16(dst, (uint16)(sint16)mToneCount[c]);
	for(int c = 0; c < 3; ++c)
		PutU8(dst, mToneOutput[c]);
	for(int c = 0; c < 3; ++c)
		PutU8(dst, mToneDuty[c]);

	PutU16(dst, (uint16)(sint16)mNoiseCount);
	PutU8(dst, mNoisePrescale);
	PutU32(dst, mNoiseRng);

	PutU32(dst, mEnvCount);
	PutU16(dst, (uint16)(sint16)mEnvStep);
	PutU8(dst, mEnvHold);
	PutU8(dst, mEnvAlternate);
	PutU8(dst, mEnvAttack);
	PutU8(dst, mEnvHolding);
	PutU8(dst, (uint8)mEnvVolume);
}

void ATYMPSGEmulator::LoadState(const uint8 *data, size_t len) {
	// Required blob size: 16 + (6+6+3+3) + (2+1+4) + (4+2+1+1+1+1+1) = 53.
	constexpr size_t kExpected = 16 + 18 + 7 + 11;
	if (!data || len < kExpected)
		return;

	const uint8 *p = data;

	for(int i = 0; i < 16; ++i)
		mReg[i] = GetU8(p);

	for(int c = 0; c < 3; ++c)
		mTonePeriod[c] = GetU16(p);
	for(int c = 0; c < 3; ++c)
		mToneCount[c] = (sint16)GetU16(p);
	for(int c = 0; c < 3; ++c)
		mToneOutput[c] = GetU8(p);
	for(int c = 0; c < 3; ++c)
		mToneDuty[c] = GetU8(p);

	mNoiseCount = (sint16)GetU16(p);
	mNoisePrescale = GetU8(p);
	mNoiseRng = GetU32(p);

	mEnvCount = GetU32(p);
	mEnvStep = (sint16)GetU16(p);
	mEnvHold = GetU8(p);
	mEnvAlternate = GetU8(p);
	mEnvAttack = GetU8(p);
	mEnvHolding = GetU8(p);
	mEnvVolume = GetU8(p);

	if (mNoiseRng == 0)
		mNoiseRng = 1;
}
