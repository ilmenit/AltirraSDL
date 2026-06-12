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
// PokeyMax SID core (MOS 6581 / 8580 compatible).
//
// The oscillator / envelope / filter / accumulation-buffer synthesis loop,
// the ADSR prescaler model (kPrescalerRates / kLogTable), and the RBJ
// multimode filter are cloned from ATSlightSIDEmulator (slightsid.cpp,
// Copyright (C) 2008-2011 Avery Lee, GPL-2.0+), which is left untouched.
//
// The combined-waveform behaviour and the noise generator are reworked
// using MAME's SID device as an algorithmic reference (no MAME source is
// linked into this build):
//
//   * The measured combined-waveform ROM tables in pokeymaxsid_wave6581.h /
//     pokeymaxsid_wave8580.h are copied verbatim from MAME's sidw6581.h /
//     sidw8580.h. They were taken from reSID 0.5 (6581) and Deadman's 8580
//     dumps and carry the following attribution:
//
//         license:GPL-2.0+
//         copyright-holders:Dag Lem
//         Read-out combined waveforms taken from reSID 0.5.
//         Copyright Dag Lem <resid@nimrod.no>
//
//     The GPL-2.0+ license is compatible with Altirra's GPL-2+.
//
//   * The 24-bit noise LFSR (taps bit22 ^ bit17, seed 0x7ffff8) and the
//     8-bit noise output tap selection are reproduced from MAME's
//     sidvoice.cpp:
//
//         license:BSD-3-Clause
//         copyright-holders:Peter Trauner
//
//     The BSD-3-Clause license is compatible with Altirra's GPL-2+.
//=========================================================================

#include <stdafx.h>
#include <cstdint>
#include <vd2/system/binary.h>
#include <at/atcore/audiomixer.h>
#include <at/atcore/consoleoutput.h>
#include <at/atcore/scheduler.h>
#include "pokeymaxsid.h"

// Verbatim measured combined-waveform ROM tables (see license note above).
#include "pokeymaxsid_wave6581.h"
#include "pokeymaxsid_wave8580.h"

namespace {
	// 8-bit SID noise output: select taps {2,4,7,11,13,16,20,22} of the
	// 24-bit LFSR into output bits {0..7}. Matches MAME's noiseTable* bit
	// extraction (sidvoice.cpp, BSD-3-Clause).
	inline uint8 SIDNoise8(uint32 r) {
		return (uint8)(
			(((r >> 22) & 1) << 7) |
			(((r >> 20) & 1) << 6) |
			(((r >> 16) & 1) << 5) |
			(((r >> 13) & 1) << 4) |
			(((r >> 11) & 1) << 3) |
			(((r >>  7) & 1) << 2) |
			(((r >>  4) & 1) << 1) |
			(((r >>  2) & 1) << 0));
	}
}

ATPokeyMaxSIDEmulator::ATPokeyMaxSIDEmulator() {
	// Default to the 6581 table set (SIDMODE default $11 selects 6581).
	mpWaveform30 = waveform30_6581;
	mpWaveform50 = waveform50_6581;
	mpWaveform60 = waveform60_6581;
	mpWaveform70 = waveform70_6581;
}

ATPokeyMaxSIDEmulator::~ATPokeyMaxSIDEmulator() {
	Shutdown();
}

void ATPokeyMaxSIDEmulator::Init(ATScheduler *sch, IATAudioMixer *mixer, int chipIndex) {
	mpScheduler = sch;
	mpAudioMixer = mixer;
	mChipIndex = chipIndex;

	if (mixer && !mbRegistered) {
		mixer->AddSyncAudioSource(this);
		mbRegistered = true;
	}

	ColdReset();
}

void ATPokeyMaxSIDEmulator::Shutdown() {
	if (mpAudioMixer) {
		if (mbRegistered) {
			mpAudioMixer->RemoveSyncAudioSource(this);
			mbRegistered = false;
		}

		mpAudioMixer = nullptr;
	}

	mpScheduler = nullptr;
}

void ATPokeyMaxSIDEmulator::ColdReset() {
	mSidStepAccum = 0;
	mLastUpdate = mpScheduler ? ATSCHEDULER_GETTIME(mpScheduler) : 0;

	WarmReset();
}

void ATPokeyMaxSIDEmulator::WarmReset() {
	memset(mChannels, 0, sizeof mChannels);

	// Seed the noise LFSR to MAME's noiseSeed (0x7ffff8) for every voice.
	for(int i=0; i<3; ++i)
		mChannels[i].mNoiseLFSR = 0x7ffff8;

	mOutputAccumF = 0;
	mOutputAccumNF = 0;
	mOutputCount = 0;
	mOutputLevel = 0;
	mVolumeScale = 0;
	mFilterDelayX1 = 0;
	mFilterDelayX2 = 0;
	mFilterDelayY1 = 0;
	mFilterDelayY2 = 0;
	mFilterCoeffB0 = 0;
	mFilterCoeffB1 = 0;
	mFilterCoeffB2 = 0;
	mFilterCoeffA0 = 0;
	mFilterCoeffA1 = 0;
	mFilterCoeffA2 = 0;
	memset(mRegisters, 0xFF, sizeof mRegisters);
	for(int i=0; i<32; ++i)
		WriteControl(i, 0);

	memset(mAccumBuffer, 0, sizeof mAccumBuffer);
}

void ATPokeyMaxSIDEmulator::DumpStatus(ATConsoleOutput& output) {
	output("Model: %s  digifix:%s  enabled:%s  clock: PHI2*%u/%u  side:%s",
		mb6581 ? "6581" : "8580",
		mbDigifix ? "on" : "off",
		mbEnabled ? "yes" : "no",
		mStepNum, mStepDen,
		mChipIndex == 0 ? "left" : "right");

	output <<= "CH  Freq  Phase   Wfrm  ADSR  Env-M";
	for(int i=0; i<3; ++i) {
		const Channel& ch = mChannels[i];

		output("%2u  %04X  %06X  %c%c%c%c  %X%X%X%X  %02X-%c"
			, i+1
			, ch.mFreq >> 8
			, ch.mPhase >> 8
			, ch.mWaveform & 8 ? 'N' : ' '
			, ch.mWaveform & 4 ? 'P' : ' '
			, ch.mWaveform & 2 ? 'S' : ' '
			, ch.mWaveform & 1 ? 'T' : ' '
			, ch.mAttack
			, ch.mDecay
			, ch.mSustain >> 4
			, ch.mRelease
			, ch.mEnvelope
			, ch.mEnvelopeMode == 1 && ch.mEnvelope <= ch.mSustain ? 'S' : "ADR"[ch.mEnvelopeMode]
			);
	}
}

void ATPokeyMaxSIDEmulator::SetEnabled(bool enabled) {
	if (mbEnabled == enabled)
		return;

	// Lead with a flush so the change takes effect exactly at "now".
	Flush();

	mbEnabled = enabled;
}

void ATPokeyMaxSIDEmulator::SetModel(bool is6581, bool digifix) {
	if (mb6581 == is6581 && mbDigifix == digifix)
		return;

	// Render up to now with the old model, then switch table sets.
	Flush();

	mb6581 = is6581;
	mbDigifix = digifix;

	if (is6581) {
		mpWaveform30 = waveform30_6581;
		mpWaveform50 = waveform50_6581;
		mpWaveform60 = waveform60_6581;
		mpWaveform70 = waveform70_6581;
	} else {
		mpWaveform30 = waveform30_8580;
		mpWaveform50 = waveform50_8580;
		mpWaveform60 = waveform60_8580;
		mpWaveform70 = waveform70_8580;
	}
}

void ATPokeyMaxSIDEmulator::SetEngineMode(bool accurate) {
	if (mbAccurateEngine == accurate)
		return;

	// Render up to now with the old engine, then switch generators.
	Flush();

	mbAccurateEngine = accurate;
}

void ATPokeyMaxSIDEmulator::SetClock(double machineClockHz, bool palMode) {
	(void)machineClockHz;	// the step ratio depends only on the TV standard

	const uint32 newNum = palMode ? 5 : 4;
	const uint32 newDen = palMode ? 9 : 7;

	if (newNum == mStepNum && newDen == mStepDen)
		return;

	// Render up to now with the OLD step ratio, then switch.
	Flush();

	const uint32 oldDen = mStepDen;

	mStepNum = newNum;
	mStepDen = newDen;

	// Rescale the fractional step residue into the new denominator so we never
	// carry a raw residue that is invalid under the new denominator (e.g. an
	// old residue of 8 would be invalid under denominator 7 and would inject
	// an unintended extra SID step).
	if (oldDen != 0) {
		mSidStepAccum = (mSidStepAccum * newDen) / oldDen;
		if (mSidStepAccum >= newDen)
			mSidStepAccum %= newDen;
	} else {
		mSidStepAccum = 0;
	}

	// The output box-filter normalization depends on mStepNum.
	RecomputeVolumeScale();
}

void ATPokeyMaxSIDEmulator::RecomputeVolumeScale() {
	mVolumeScale = (float)(mRegisters[0x18] & 15);

	mVolumeScale *= 1.0f
					/ (128.0f*255.0f)			// waveform * envelope scaling
					/ (28.0f*(float)mStepNum)	// 28*mStepNum counts accumulated per sample
					/ 15.0f						// for 0-15 global volume scale
					;
}

void ATPokeyMaxSIDEmulator::WriteControl(uint8 addr, uint8 value) {
	if (addr >= 25)
		return;

	const uint8 prevValue = mRegisters[addr];
	if (prevValue == value)
		return;

	Flush();
	mRegisters[addr] = value;

	if (addr < 21) {
		static const ptrdiff_t kChOffsets[21]={
			0, 0, 0, 0, 0, 0, 0,
			sizeof(Channel), sizeof(Channel), sizeof(Channel), sizeof(Channel), sizeof(Channel), sizeof(Channel), sizeof(Channel),
			sizeof(Channel)*2, sizeof(Channel)*2, sizeof(Channel)*2, sizeof(Channel)*2, sizeof(Channel)*2, sizeof(Channel)*2, sizeof(Channel)*2,
		};

		static const int kRegOffsets[21]={
			0, 0, 0, 0, 0, 0, 0,
			7, 7, 7, 7, 7, 7, 7,
			14, 14, 14, 14, 14, 14, 14
		};

		Channel& ch = *(Channel *)((char *)mChannels + kChOffsets[addr]);
		const uint8 *rbase = mRegisters + kRegOffsets[addr];

		switch(addr - kRegOffsets[addr]) {
			case 0x00:
			case 0x01:
				ch.mFreq = ((uint32)rbase[0] << 8) + ((uint32)rbase[1] << 16);
				break;

			case 0x02:
			case 0x03:
				ch.mPulseWidth = ((uint32)rbase[2] << 20) + ((uint32)(rbase[3] & 15) << 28);
				break;

			case 0x04:
				ch.mWaveform = value >> 4;

				if ((prevValue ^ value) & 1) {
					if (value & 1) {
						if (ch.mEnvelope < 255)
							ch.mEnvelopeMode = 0;
						else
							ch.mEnvelopeMode = 1;
					} else {
						ch.mEnvelopeMode = 2;
					}
				}

				ch.mbSync = (value & 2) != 0;
				ch.mbRingMod = (value & 4) != 0;

				if ((prevValue ^ value) & 8) {
					ch.mbTestOn = (value & 0x08) & 1;

					if (ch.mbTestOn) {
						ch.mPhase = 0;
						ch.mNoiseLFSR = 0x7ffff8;
					}
				}
				break;

			case 0x05:
				ch.mAttack = value >> 4;
				ch.mDecay = value & 15;
				break;

			case 0x06:
				ch.mSustain = (value >> 4) * 17;
				ch.mRelease = value & 15;
				break;
		}
	} else {
		switch(addr) {
			case 0x15:
			case 0x16:
			case 0x17:
			case 0x18:
				{
					// The filter cutoff varies linearly from 30Hz to 12KHz at 1MHz according to the
					// SID datasheet. This is very hand-wavy, but fortunately there is a lot of
					// variance on real C64s.
					float fc = (30.0f + 11970.0f*((mRegisters[0x15] & 7) + ((uint32)mRegisters[0x16] << 3)) / 2047.0f);

					// "Based on my experience, the resonance Q value goes from 0.71 to 1.71, which as a control
					//  goes from about 0 dB resonance to about 10 dB."
					float q = 0.71f + (float)(mRegisters[0x17] >> 4) / 15.0f;

					float w0 = 2.0f * 3.1415926535f * fc / 63920.0f;
					float alpha = sinf(w0) / (2.0f * q);

					float b0 = 0;
					float b1 = 0;
					float b2 = 0;

					// low-pass filter
					if (mRegisters[0x18] & 0x10) {
						b0 += (1.0f - cosf(w0)) * 0.5f;
						b1 += 1.0f - cosf(w0);
						b2 += (1.0f - cosf(w0)) * 0.5f;
					}

					// band-pass filter
					if (mRegisters[0x18] & 0x20) {
						b0 += sinf(w0) * 0.5f;
						b2 += -sinf(w0) * 0.5f;
					}

					// high-pass filter
					if (mRegisters[0x18] & 0x40) {
						b0 += (1.0f + cosf(w0)) * 0.5f;
						b1 += -(1.0f + cosf(w0));
						b2 += (1.0f + cosf(w0)) * 0.5f;
					}

					float inv_a0 = 1.0f / (1.0f + alpha);
					float a1 = -2.0f * cosf(w0);
					float a2 = 1.0f - alpha;
					mFilterCoeffB0 = b0 * inv_a0;
					mFilterCoeffB1 = b1 * inv_a0;
					mFilterCoeffB2 = b2 * inv_a0;
					mFilterCoeffA0 = inv_a0;
					mFilterCoeffA1 = -a1 * inv_a0;
					mFilterCoeffA2 = -a2 * inv_a0;

					// Apply a tiny bit of decay to the recursive parameters. This prevents the filter
					// from blowing up at low cutoff frequencies due to numerical accuracy issues.
					mFilterCoeffA1 *= 0.99999f;
					mFilterCoeffA2 *= 0.99999f;
				}

				mChannels[0].mFilteredEnable = (mRegisters[0x17] & 0x01) ? ~0 : 0;
				mChannels[0].mNonFilteredEnable = ~mChannels[0].mFilteredEnable;
				mChannels[1].mFilteredEnable = (mRegisters[0x17] & 0x02) ? ~0 : 0;
				mChannels[1].mNonFilteredEnable = ~mChannels[1].mFilteredEnable;
				mChannels[2].mFilteredEnable = (mRegisters[0x17] & 0x04) ? ~0 : 0;

				if (mRegisters[0x18] & 0x80)
					mChannels[2].mNonFilteredEnable = 0;
				else
					mChannels[2].mNonFilteredEnable = ~mChannels[2].mFilteredEnable;

				RecomputeVolumeScale();
				break;
		}
	}
}

uint8 ATPokeyMaxSIDEmulator::ReadControl(uint8 addr) const {
	if (addr >= 0x20)
		return 0xFF;

	// OSC3 ($1B): top 8 bits of voice-3 oscillator output.
	if (addr == 0x1B)
		return mChannels[2].mLastWaveSample;

	// ENV3 ($1C): voice-3 envelope value.
	if (addr == 0x1C)
		return mChannels[2].mEnvelope;

	// SlightSID-compatible default for all other (write-only / reserved)
	// registers.
	return 0x33;
}

#if _MSC_VER >= 1400
// We need to enable precise FP code generation here since we have anti-denormal
// code that we need not to be optimized out.
#pragma float_control(push)
#pragma float_control(precise, on)
#endif

void ATPokeyMaxSIDEmulator::Run(uint32 cycles) {
	// Convert machine (PHI2) cycles into SID steps using the PHI2-derived
	// ratio (PAL 5/9, NTSC 4/7), keeping a persistent fractional residue so
	// pitch never drifts across Run/Flush chunk boundaries.
	mSidStepAccum += (uint64)cycles * mStepNum;
	uint64 sidSteps = mSidStepAccum / mStepDen;
	mSidStepAccum %= mStepDen;

	const uint32 stepWeight = mStepDen;			// each SID step spans mStepDen units
	const uint32 sampleUnits = 28u * mStepNum;	// one output sample spans 28 machine cycles

	while(sidSteps--) {
		int output = 0;
		int filtoutput = 0;

		Channel *__restrict chprev = &mChannels[2];
		for(int chidx = 0; chidx < 3; ++chidx) {
			Channel *__restrict ch = &mChannels[chidx];
			uint32 prevPhase = ch->mPhase;
			uint32 nextPhase = prevPhase + ch->mFreq;

			if (ch->mbSync && chprev->mPhase < chprev->mPrevPhase)
				nextPhase = 0;

			ch->mPrevPhase = prevPhase;
			ch->mPhase = nextPhase;

			uint8 v;

			if (mbAccurateEngine) {
				// === "MAME" engine =====================================
				// Measured combined-waveform ROM tables + correct 24-bit
				// noise LFSR (taps bit22 ^ bit17, seed 0x7ffff8).
				if (ch->mbTestOn) {
					v = 0x80;
				} else {
					const uint8 wf = ch->mWaveform;			// bit0 tri, bit1 saw, bit2 pulse, bit3 noise
					const uint8 wlow = (uint8)(wf & 0x07);
					const uint32 ph12 = ch->mPhase >> 20;		// 12-bit oscillator phase
					const uint32 pw12 = ch->mPulseWidth >> 20;	// 12-bit pulse width

					if (wf & 0x08) {
						// Noise selected. Advance the 24-bit LFSR once per phase
						// wrap (taps bit22 ^ bit17, MAME parity).
						if (nextPhase < prevPhase)
							ch->mNoiseLFSR = ((ch->mNoiseLFSR << 1) | (((ch->mNoiseLFSR >> 22) ^ (ch->mNoiseLFSR >> 17)) & 1)) & 0x7FFFFF;

						if (wlow) {
							// Noise combined with another waveform: the real chip
							// drives the LFSR towards all-zero and the voice goes
							// silent.
							v = 0x80;
						} else {
							v = SIDNoise8(ch->mNoiseLFSR);
						}
					} else {
						switch(wlow) {
							case 0x0:
							default:
								// No waveform selected -> oscillator midpoint.
								v = 0x80;
								break;

							case 0x1:	// triangle
								{
									const sint32 ringSign = ch->mbRingMod
										? ((sint32)chprev->mPhase >> 31)
										: ((sint32)ch->mPhase >> 31);

									v = (uint8)((ch->mPhase >> 23) ^ (uint32)ringSign);
								}
								break;

							case 0x2:	// sawtooth
								v = (uint8)(ch->mPhase >> 24);
								break;

							case 0x4:	// pulse
								v = (ch->mPhase >= ch->mPulseWidth) ? 0xFF : 0x00;
								break;

							case 0x3:	// triangle + sawtooth
								v = mpWaveform30[ph12];
								if (ch->mbRingMod && (chprev->mPhase & 0x80000000))
									v ^= 0xFF;
								break;

							case 0x5:	// triangle + pulse
								v = mpWaveform50[ph12 + pw12];
								if (ch->mbRingMod && (chprev->mPhase & 0x80000000))
									v ^= 0xFF;
								break;

							case 0x6:	// sawtooth + pulse
								v = mpWaveform60[ph12 + pw12];
								break;

							case 0x7:	// triangle + sawtooth + pulse
								v = mpWaveform70[ph12 + pw12];
								if (ch->mbRingMod && (chprev->mPhase & 0x80000000))
									v ^= 0xFF;
								break;
						}
					}
				}
			} else {
				// === "SlightSID" engine ================================
				// Original ATSlightSIDEmulator combined-waveform / noise
				// approximation: logical-AND combination of the individual
				// waveforms and approximate noise taps. Reproduced verbatim
				// from slightsid.cpp so this engine matches the standalone
				// SlightSID device bit-for-bit.
				if (ch->mbTestOn) {
					v = 0;
				} else {
					v = 0xFF;
					if (ch->mWaveform & 8) {
						if (nextPhase < prevPhase)
							ch->mNoiseLFSR = (ch->mNoiseLFSR << 1) + (((ch->mNoiseLFSR >> 22) + (ch->mNoiseLFSR >> 17)) & 1);
						v = (uint8)ch->mNoiseLFSR;	// not the right taps
					}
					if (ch->mWaveform & 4)
						v &= (ch->mPhase >= ch->mPulseWidth) ? 0xFF : 0x00;
					if (ch->mWaveform & 2)
						v = ch->mPhase >> 24;
					if (ch->mWaveform & 1) {
						if (ch->mbRingMod)
							v &= (ch->mPhase >> 23) ^ ((sint32)chprev->mPhase >> 31);
						else
							v &= (ch->mPhase >> 23) ^ ((sint32)ch->mPhase >> 31);
					}
				}
			}

			// Latch the raw 8-bit oscillator sample for OSC3 read-back.
			ch->mLastWaveSample = v;

			// These prescaler rates are in cycles and come from the on-chip LFSR comparator
			// ROM: http://blog.kevtris.org/?p=13
			static const uint32 kPrescalerRates[6][16]={
#define PRESCALER_RATE(x) ((uint32)((0xFFFFFFFFULL + (x))/(x)))
#define PRESCALER_RATES(y) {	\
				PRESCALER_RATE(    9*(y)),	\
				PRESCALER_RATE(   32*(y)),	\
				PRESCALER_RATE(   63*(y)),	\
				PRESCALER_RATE(   95*(y)),	\
				PRESCALER_RATE(  149*(y)),	\
				PRESCALER_RATE(  220*(y)),	\
				PRESCALER_RATE(  267*(y)),	\
				PRESCALER_RATE(  313*(y)),	\
				PRESCALER_RATE(  392*(y)),	\
				PRESCALER_RATE(  977*(y)),	\
				PRESCALER_RATE( 1954*(y)),	\
				PRESCALER_RATE( 3126*(y)),	\
				PRESCALER_RATE( 3907*(y)),	\
				PRESCALER_RATE(11720*(y)),	\
				PRESCALER_RATE(19532*(y)),	\
				PRESCALER_RATE(31251*(y))	\
				}

				PRESCALER_RATES(1),
				PRESCALER_RATES(2),
				PRESCALER_RATES(4),
				PRESCALER_RATES(8),
				PRESCALER_RATES(16),
				PRESCALER_RATES(30),
#undef PRESCALER_RATES
#undef PRESCALER_RATE
			};

			// This table maps sections of the envelope ramp to different piecewise curve sections.
			static const uint8 kLogTable[256]={
#define LOG_ENTRY(x) ((x) > 0x5D ? 0 :	\
				  (x) > 0x36 ? 1 :	\
				  (x) > 0x1A ? 2 :	\
				  (x) > 0x0E ? 3 :	\
				  (x) > 0x06 ? 4 : 5)
#define LOG_ENTRY_LINE(x)	LOG_ENTRY((x)+0),LOG_ENTRY((x)+1),LOG_ENTRY((x)+2),LOG_ENTRY((x)+3),	\
						LOG_ENTRY((x)+4),LOG_ENTRY((x)+5),LOG_ENTRY((x)+6),LOG_ENTRY((x)+7),	\
						LOG_ENTRY((x)+8),LOG_ENTRY((x)+9),LOG_ENTRY((x)+10),LOG_ENTRY((x)+11),	\
						LOG_ENTRY((x)+12),LOG_ENTRY((x)+13),LOG_ENTRY((x)+14),LOG_ENTRY((x)+15)
				LOG_ENTRY_LINE(0x00),
				LOG_ENTRY_LINE(0x10),
				LOG_ENTRY_LINE(0x20),
				LOG_ENTRY_LINE(0x30),
				LOG_ENTRY_LINE(0x40),
				LOG_ENTRY_LINE(0x50),
				LOG_ENTRY_LINE(0x60),
				LOG_ENTRY_LINE(0x70),
				LOG_ENTRY_LINE(0x80),
				LOG_ENTRY_LINE(0x90),
				LOG_ENTRY_LINE(0xA0),
				LOG_ENTRY_LINE(0xB0),
				LOG_ENTRY_LINE(0xC0),
				LOG_ENTRY_LINE(0xD0),
				LOG_ENTRY_LINE(0xE0),
				LOG_ENTRY_LINE(0xF0),
#undef LOG_ENTRY_LINE
#undef LOG_ENTRY
			};

			int env = ch->mEnvelope;

			switch(ch->mEnvelopeMode) {
				case 0:
					if (env >= 255) {
						ch->mEnvelopeMode = 1;
					} else {
						uint32 preAccumPrev = ch->mPrescalerAccum;
						uint32 preAccumNext = preAccumPrev + kPrescalerRates[0][ch->mAttack];
						ch->mPrescalerAccum = preAccumNext;

						if (preAccumNext < preAccumPrev)
							++env;
					}
					break;

				case 1:
				default:
					if (env > ch->mSustain) {
						uint32 preAccumPrev = ch->mPrescalerAccum;
						uint32 preAccumNext = preAccumPrev + kPrescalerRates[kLogTable[env]][ch->mDecay];
						ch->mPrescalerAccum = preAccumNext;

						if (preAccumNext < preAccumPrev)
							--env;
					}
					break;

				case 2:
					if (env > 0) {
						uint32 preAccumPrev = ch->mPrescalerAccum;
						uint32 preAccumNext = preAccumPrev + kPrescalerRates[kLogTable[env]][ch->mRelease];
						ch->mPrescalerAccum = preAccumNext;

						if (preAccumNext < preAccumPrev)
							--env;
					}
					break;
			}

			ch->mEnvelope = env;

			sint32 choutput = ((int)v - 128) * env;
			filtoutput += choutput & ch->mFilteredEnable;
			output += choutput & ch->mNonFilteredEnable;

			chprev = ch;
		}

		float outputF = (float)filtoutput * mVolumeScale;
		float outputNF = (float)output * mVolumeScale;

		uint32 nativeCycles = sampleUnits - mOutputCount;

		if (nativeCycles > stepWeight) {
			mOutputAccumF += outputF;
			mOutputAccumNF += outputNF;
			mOutputCount += stepWeight;
		} else {
			float nativeCyclesDivStep = (float)nativeCycles * (1.0f / (float)stepWeight);

			if (mOutputLevel < kAccumBufferSize) {
				mOutputAccumF += outputF * nativeCyclesDivStep;
				mOutputAccumNF += outputNF * nativeCyclesDivStep;

				// Direct Form I biquad (stable at very low cutoff / high
				// resonance).
				float filtresult = mOutputAccumF * mFilterCoeffB0
						+ mFilterDelayX1 * mFilterCoeffB1
						+ mFilterDelayX2 * mFilterCoeffB2
						+ mFilterDelayY1 * mFilterCoeffA1
						+ mFilterDelayY2 * mFilterCoeffA2
						;

				mFilterDelayX2 = mFilterDelayX1;
				mFilterDelayX1 = mOutputAccumF;
				mFilterDelayY2 = mFilterDelayY1;

				// Perturb the delayed result to avoid denormals.
				mFilterDelayY1 = (filtresult + 1e-10f) - 1e-10f;

				mAccumBuffer[mOutputLevel++] = mOutputAccumNF + filtresult;
			}

			mOutputCount = stepWeight - nativeCycles;
			mOutputAccumF = outputF - outputF * nativeCyclesDivStep;
			mOutputAccumNF = outputNF - outputNF * nativeCyclesDivStep;
		}
	}
}

#if _MSC_VER >= 1400
#pragma float_control(pop)
#endif

void ATPokeyMaxSIDEmulator::WriteAudio(const ATSyncAudioMixInfo& mixInfo) {
	float *const dstLeft = mixInfo.mpLeft;
	float *const dstRight = mixInfo.mpRight;
	const uint32 n = mixInfo.mCount;

	Flush();

	VDASSERT(n <= kAccumBufferSize);

	// If we don't have enough samples, pad out; eventually we'll catch up.
	if (mOutputLevel < n) {
		memset(mAccumBuffer + mOutputLevel, 0, sizeof(mAccumBuffer[0]) * (n - mOutputLevel));

		mOutputLevel = n;
	}

	// Gate output via the runtime enable. When disabled the buffer is still
	// drained below so it never overflows.
	const float volume = mbEnabled ? mixInfo.mpMixLevels[kATAudioMix_Other] : 0.0f;

	// Stereo routing: SID1 (chipIndex 0) -> left, SID2 (chipIndex 1) -> right.
	if (volume != 0.0f) {
		if (mChipIndex == 0) {
			for(uint32 i=0; i<n; ++i)
				dstLeft[i] += mAccumBuffer[i] * volume;
		} else if (dstRight) {
			for(uint32 i=0; i<n; ++i)
				dstRight[i] += mAccumBuffer[i] * volume;
		} else {
			// Defensive: if the mixer handed us a mono buffer, fall back to
			// the left channel so SID2 is not silently dropped.
			for(uint32 i=0; i<n; ++i)
				dstLeft[i] += mAccumBuffer[i] * volume;
		}
	}

	// Shift down the accumulation buffer.
	uint32 samplesLeft = mOutputLevel - n;

	if (samplesLeft)
		memmove(mAccumBuffer, mAccumBuffer + n, samplesLeft * sizeof(mAccumBuffer[0]));

	mOutputLevel = samplesLeft;
}

void ATPokeyMaxSIDEmulator::Flush() {
	if (!mpScheduler)
		return;

	uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
	uint32 dt = t - mLastUpdate;
	mLastUpdate = t;

	Run(dt);
}

//=========================================================================
// Raw state serialization
//
// The blob is a fixed little-endian packing covering enough to fully
// restore the audio phase: step ratio + residue, model flags, the 25-byte
// register file (stored as the full 32-byte image), and per-voice / filter
// state.
//=========================================================================

namespace {
	void PutU8(vdfastvector<uint8>& v, uint8 x) { v.push_back(x); }
	void PutU32(vdfastvector<uint8>& v, uint32 x) {
		v.push_back((uint8)x);
		v.push_back((uint8)(x >> 8));
		v.push_back((uint8)(x >> 16));
		v.push_back((uint8)(x >> 24));
	}
	void PutFloat(vdfastvector<uint8>& v, float f) {
		uint32 x;
		memcpy(&x, &f, sizeof x);
		PutU32(v, x);
	}

	uint8 GetU8(const uint8 *&p) { return *p++; }
	uint32 GetU32(const uint8 *&p) {
		uint32 x = (uint32)p[0] | ((uint32)p[1] << 8) | ((uint32)p[2] << 16) | ((uint32)p[3] << 24);
		p += 4;
		return x;
	}
	float GetFloat(const uint8 *&p) {
		uint32 x = GetU32(p);
		float f;
		memcpy(&f, &x, sizeof f);
		return f;
	}
}

void ATPokeyMaxSIDEmulator::SaveState(vdfastvector<uint8>& dst) const {
	dst.clear();

	// Clock / model.
	PutU32(dst, mStepNum);
	PutU32(dst, mStepDen);
	PutU32(dst, (uint32)mSidStepAccum);
	PutU8(dst, mb6581 ? 1 : 0);
	PutU8(dst, mbDigifix ? 1 : 0);
	PutU8(dst, mbEnabled ? 1 : 0);

	// Register file (full 32-byte image).
	for(int i=0; i<32; ++i)
		PutU8(dst, mRegisters[i]);

	// Per-voice state.
	for(int i=0; i<3; ++i) {
		const Channel& ch = mChannels[i];
		PutU32(dst, ch.mPrevPhase);
		PutU32(dst, ch.mPhase);
		PutU32(dst, ch.mFreq);
		PutU32(dst, ch.mPulseWidth);
		PutU32(dst, (uint32)ch.mPrescalerAccum);
		PutU8(dst, ch.mbSync ? 1 : 0);
		PutU8(dst, ch.mbRingMod ? 1 : 0);
		PutU8(dst, ch.mbTestOn ? 1 : 0);
		PutU8(dst, ch.mEnvelopeMode);
		PutU8(dst, ch.mEnvelope);
		PutU8(dst, ch.mWaveform);
		PutU8(dst, ch.mAttack);
		PutU8(dst, ch.mDecay);
		PutU8(dst, ch.mSustain);
		PutU8(dst, ch.mRelease);
		PutU8(dst, ch.mControl);
		PutU32(dst, (uint32)ch.mFilteredEnable);
		PutU32(dst, (uint32)ch.mNonFilteredEnable);
		PutU32(dst, ch.mNoiseLFSR);
		PutU8(dst, ch.mLastWaveSample);
	}

	// Filter biquad delay/coeff state.
	PutFloat(dst, mFilterDelayX1);
	PutFloat(dst, mFilterDelayX2);
	PutFloat(dst, mFilterDelayY1);
	PutFloat(dst, mFilterDelayY2);
	PutFloat(dst, mFilterCoeffB0);
	PutFloat(dst, mFilterCoeffB1);
	PutFloat(dst, mFilterCoeffB2);
	PutFloat(dst, mFilterCoeffA0);
	PutFloat(dst, mFilterCoeffA1);
	PutFloat(dst, mFilterCoeffA2);

	// Engine selector (Altirra extension). Appended after the original blob so
	// older save-states still load (engine then keeps its current value).
	PutU8(dst, mbAccurateEngine ? 1 : 0);
}

void ATPokeyMaxSIDEmulator::LoadState(const uint8 *data, size_t len) {
	// Header(3xu32 + 3xu8) + 32 regs + 3 voices(5xu32+11xu8+3xu32) + 10 floats.
	constexpr size_t kVoiceBytes = 5*4 + 11 + 3*4 + 1;	// = 44
	constexpr size_t kExpected = (3*4 + 3) + 32 + 3*kVoiceBytes + 10*4;
	if (!data || len < kExpected)
		return;

	const uint8 *p = data;

	mStepNum = GetU32(p);
	mStepDen = GetU32(p);
	mSidStepAccum = GetU32(p);
	mb6581 = GetU8(p) != 0;
	mbDigifix = GetU8(p) != 0;
	mbEnabled = GetU8(p) != 0;

	if (mStepDen == 0)
		mStepDen = 9;
	if (mStepNum == 0)
		mStepNum = 5;
	if (mSidStepAccum >= mStepDen)
		mSidStepAccum %= mStepDen;

	// Re-point the combined-waveform tables for the restored model.
	if (mb6581) {
		mpWaveform30 = waveform30_6581;
		mpWaveform50 = waveform50_6581;
		mpWaveform60 = waveform60_6581;
		mpWaveform70 = waveform70_6581;
	} else {
		mpWaveform30 = waveform30_8580;
		mpWaveform50 = waveform50_8580;
		mpWaveform60 = waveform60_8580;
		mpWaveform70 = waveform70_8580;
	}

	for(int i=0; i<32; ++i)
		mRegisters[i] = GetU8(p);

	for(int i=0; i<3; ++i) {
		Channel& ch = mChannels[i];
		ch.mPrevPhase = GetU32(p);
		ch.mPhase = GetU32(p);
		ch.mFreq = GetU32(p);
		ch.mPulseWidth = GetU32(p);
		ch.mPrescalerAccum = (sint32)GetU32(p);
		ch.mbSync = GetU8(p) != 0;
		ch.mbRingMod = GetU8(p) != 0;
		ch.mbTestOn = GetU8(p) != 0;
		ch.mEnvelopeMode = GetU8(p);
		ch.mEnvelope = GetU8(p);
		ch.mWaveform = GetU8(p);
		ch.mAttack = GetU8(p);
		ch.mDecay = GetU8(p);
		ch.mSustain = GetU8(p);
		ch.mRelease = GetU8(p);
		ch.mControl = GetU8(p);
		ch.mFilteredEnable = (sint32)GetU32(p);
		ch.mNonFilteredEnable = (sint32)GetU32(p);
		ch.mNoiseLFSR = GetU32(p);
		ch.mLastWaveSample = GetU8(p);

		if (ch.mNoiseLFSR == 0)
			ch.mNoiseLFSR = 0x7ffff8;
	}

	mFilterDelayX1 = GetFloat(p);
	mFilterDelayX2 = GetFloat(p);
	mFilterDelayY1 = GetFloat(p);
	mFilterDelayY2 = GetFloat(p);
	mFilterCoeffB0 = GetFloat(p);
	mFilterCoeffB1 = GetFloat(p);
	mFilterCoeffB2 = GetFloat(p);
	mFilterCoeffA0 = GetFloat(p);
	mFilterCoeffA1 = GetFloat(p);
	mFilterCoeffA2 = GetFloat(p);

	// Optional engine selector (appended; absent in older save-states).
	if ((size_t)(p - data) < len)
		mbAccurateEngine = GetU8(p) != 0;

	RecomputeVolumeScale();
}
