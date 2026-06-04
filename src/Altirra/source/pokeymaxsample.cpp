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
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.
//
//=========================================================================
// PokeyMax SAMPLE engine -- Phase A (8-bit signed PCM block player).
//
// See pokeymaxsample.h for the register map and the Phase A scope. The
// engine renders directly into the stereo sync-audio buffers (no edge
// buffers), mirroring ATPokeyMaxSIDEmulator's Flush/Run accumulation
// model. Pitch is PHI2-independent: the per-output-sample phase increment
// is 56/period source bytes, derived from output rate PHI2/28 and playback
// rate 2*PHI2/period (PHI2 cancels).
//=========================================================================

#include <stdafx.h>
#include <string.h>
#include <at/atcore/audiomixer.h>
#include <at/atcore/consoleoutput.h>
#include <at/atcore/scheduler.h>
#include "pokeymaxsample.h"
#include "irqcontroller.h"

namespace {
	// Scheduler-event id for the end-of-sample IRQ flush (single event).
	constexpr uint32 kEndEventId = 1;

	// Standard 89-entry IMA/DVI ADPCM step-size table (sox-compatible). The
	// 4-bit IMA-ADPCM stream PokeyMax plays back is produced by sox
	// (-e ima-adpcm), which uses exactly this table, so reproducing it here
	// decodes those streams bit-accurately.
	const sint16 kADPCMStepTable[89] = {
		    7,     8,     9,    10,    11,    12,    13,    14,    16,    17,
		   19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
		   50,    55,    60,    66,    73,    80,    88,    97,   107,   118,
		  130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
		  337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
		  876,   963,  1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
		 2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
		 5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
		15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
	};

	// IMA step-index adjustment, indexed by the low 3 magnitude bits of the
	// nibble. The sign bit (bit 3) does not affect the index.
	const sint8 kADPCMIndexTable[8] = {
		-1, -1, -1, -1, 2, 4, 6, 8
	};

	void PutU8 (vdfastvector<uint8>& d, uint8  v) { d.push_back(v); }
	void PutU16(vdfastvector<uint8>& d, uint16 v) { d.push_back((uint8)v); d.push_back((uint8)(v >> 8)); }
	void PutU32(vdfastvector<uint8>& d, uint32 v) {
		d.push_back((uint8)v);         d.push_back((uint8)(v >> 8));
		d.push_back((uint8)(v >> 16)); d.push_back((uint8)(v >> 24));
	}
	void PutU64(vdfastvector<uint8>& d, uint64 v) { PutU32(d, (uint32)v); PutU32(d, (uint32)(v >> 32)); }

	uint8  GetU8 (const uint8 *&p) { return *p++; }
	uint16 GetU16(const uint8 *&p) { uint16 x = (uint16)(p[0] | (p[1] << 8)); p += 2; return x; }
	uint32 GetU32(const uint8 *&p) {
		uint32 x = (uint32)p[0] | ((uint32)p[1] << 8) | ((uint32)p[2] << 16) | ((uint32)p[3] << 24);
		p += 4;
		return x;
	}
	uint64 GetU64(const uint8 *&p) { uint64 lo = GetU32(p); uint64 hi = GetU32(p); return lo | (hi << 32); }
}

ATPokeyMaxSampleEmulator::ATPokeyMaxSampleEmulator() {
}

ATPokeyMaxSampleEmulator::~ATPokeyMaxSampleEmulator() {
	Shutdown();
}

void ATPokeyMaxSampleEmulator::Init(ATScheduler *sch, IATAudioMixer *mixer, ATIRQController *irq, uint32 irqBit) {
	mpScheduler = sch;
	mpAudioMixer = mixer;
	mpIRQController = irq;
	mIRQBit = irqBit;

	if (mixer && !mbRegistered) {
		mixer->AddSyncAudioSource(this);
		mbRegistered = true;
	}

	ColdReset();
}

void ATPokeyMaxSampleEmulator::Shutdown() {
	if (mpScheduler)
		mpScheduler->UnsetEvent(mpEndEvent);

	// Drop the IRQ line if we still hold it asserted (the bit is freed by the
	// owning PokeyMax device).
	if (mbIRQAsserted && mpIRQController && mIRQBit) {
		mpIRQController->Negate(mIRQBit, false);
		mbIRQAsserted = false;
	}

	mpIRQController = nullptr;
	mIRQBit = 0;

	if (mpAudioMixer) {
		if (mbRegistered) {
			mpAudioMixer->RemoveSyncAudioSource(this);
			mbRegistered = false;
		}

		mpAudioMixer = nullptr;
	}

	mpScheduler = nullptr;
}

void ATPokeyMaxSampleEmulator::ColdReset() {
	// Cold reset clears the on-board sample RAM; warm reset does not.
	memset(mRAM, 0, sizeof mRAM);

	mLastUpdate = mpScheduler ? ATSCHEDULER_GETTIME(mpScheduler) : 0;

	WarmReset();
}

void ATPokeyMaxSampleEmulator::WarmReset() {
	mRAMAddr = 0;
	mChanSel = 1;
	mDMA = 0;
	mIRQEn = 0;
	mIRQAct = 0;
	mCfg = 0xF0;	// default: all 8-bit, no ADPCM

	for(int i=0; i<4; ++i) {
		Channel& c = mChannels[i];
		c.mStartAddr = 0;
		c.mLength = 0;
		c.mPeriod = 0;
		c.mVolume = 0;
		c.mbHasFresh = false;
		c.mPlayStartAddr = 0;
		c.mPlayLength = 1;
		c.mPhase = 0;
		c.mInc = 0;
		c.mbActive = false;
		ResetDecoder(c);
	}

	mCycleAccum = 0;
	mOutputLevel = 0;
	mLastUpdate = mpScheduler ? ATSCHEDULER_GETTIME(mpScheduler) : 0;

	memset(mAccumLeft, 0, sizeof mAccumLeft);
	memset(mAccumRight, 0, sizeof mAccumRight);

	// Drop a held IRQ and cancel any pending end-of-sample event; mIRQAct is
	// cleared above, so the line must be low after a reset.
	if (mbIRQAsserted && mpIRQController && mIRQBit)
		mpIRQController->Negate(mIRQBit, false);
	mbIRQAsserted = false;

	if (mpScheduler)
		mpScheduler->UnsetEvent(mpEndEvent);
}

void ATPokeyMaxSampleEmulator::DumpStatus(ATConsoleOutput& output) {
	output("PokeyMax SAMPLE engine  enabled:%s  RAMptr:$%04X  CHANSEL:%u  SAMDMA:$%02X  SAMIRQEN:$%02X  SAMIRQACT:$%02X  SAMCFG:$%02X",
		mbEnabled ? "yes" : "no",
		mRAMAddr,
		mChanSel,
		mDMA, mIRQEn, mIRQAct, mCfg);

	output <<= "CH  Start  Len   Period  Vol  Active  PlayFrom  PlayLen  Phase";
	for(int i=0; i<4; ++i) {
		const Channel& c = mChannels[i];

		output("%2u  $%04X  %5u  %6u  %3u  %-6s  $%04X     %7u  %u"
			, i+1
			, c.mStartAddr
			, (unsigned)c.mLength + 1
			, c.mPeriod
			, c.mVolume
			, c.mbActive ? "yes" : "no"
			, c.mPlayStartAddr
			, c.mPlayLength
			, (unsigned)(c.mPhase >> kPhaseShift)
			);
	}
}

void ATPokeyMaxSampleEmulator::SetEnabled(bool enabled) {
	if (mbEnabled == enabled)
		return;

	// Lead with a flush so the change takes effect exactly at "now". When
	// disabled the DMA/IRQ bookkeeping still runs; only the mixed amplitude
	// is muted (handled in WriteAudio).
	Flush();

	mbEnabled = enabled;
}

int ATPokeyMaxSampleEmulator::SelectedChannel() const {
	// CHANSEL is 1-based (1..4). Anything else defaults to channel 0.
	if (mChanSel >= 1 && mChanSel <= 4)
		return mChanSel - 1;

	return 0;
}

void ATPokeyMaxSampleEmulator::RecomputeInc(int chIndex) {
	Channel& c = mChannels[chIndex];

	// Per output sample the source advances 56/period bytes (see header).
	uint32 period = c.mPeriod;
	if (period < 1)
		period = 1;

	c.mInc = ((uint64)56 << kPhaseShift) / period;
}

void ATPokeyMaxSampleEmulator::ResetDecoder(Channel& c) {
	c.mAdpcmPredictor = 0;
	c.mAdpcmStepIndex = 0;
	c.mAdpcmDecodedIdx = -1;
	c.mDecodedSample = 0;
}

int ATPokeyMaxSampleEmulator::DecodeSampleADPCM(Channel& c, uint32 idx) {
	// Advance the decoder forward one nibble at a time until it has consumed
	// nibble index idx. Every intermediate nibble must be decoded so the
	// predictor/step state stays exact even when the phase increment skips
	// output samples (period < 56) -- ADPCM cannot be random-accessed.
	while ((sint32)idx > c.mAdpcmDecodedIdx) {
		++c.mAdpcmDecodedIdx;

		const uint32 ni = (uint32)c.mAdpcmDecodedIdx;
		const uint32 byteAddr = (uint32)(c.mPlayStartAddr + (ni >> 1)) % kSampleRAMSize;
		const uint8 b = mRAM[byteAddr];

		// Low nibble first (IMA convention used by sox).
		const uint8 nib = (ni & 1) ? (uint8)(b >> 4) : (uint8)(b & 0x0F);

		const int step = kADPCMStepTable[c.mAdpcmStepIndex];

		int diff = step >> 3;
		if (nib & 1) diff += step >> 2;
		if (nib & 2) diff += step >> 1;
		if (nib & 4) diff += step;

		if (nib & 8)
			c.mAdpcmPredictor -= diff;
		else
			c.mAdpcmPredictor += diff;

		if (c.mAdpcmPredictor > 32767)
			c.mAdpcmPredictor = 32767;
		else if (c.mAdpcmPredictor < -32768)
			c.mAdpcmPredictor = -32768;

		c.mAdpcmStepIndex += kADPCMIndexTable[nib & 7];
		if (c.mAdpcmStepIndex < 0)
			c.mAdpcmStepIndex = 0;
		else if (c.mAdpcmStepIndex > 88)
			c.mAdpcmStepIndex = 88;

		// Scale the 16-bit predictor to the same ~8-bit signed range the PCM
		// formats feed into the (sample * volume) mixing path.
		c.mDecodedSample = c.mAdpcmPredictor >> 8;
	}

	return c.mDecodedSample;
}

void ATPokeyMaxSampleEmulator::HandleEndOfSample(int chIndex) {
	Channel& c = mChannels[chIndex];

	// Latch the per-channel SAMIRQACT status bit (the CPU IRQ line is NOT
	// asserted in Phase A -- that is Phase C).
	mIRQAct |= (uint8)(1 << chIndex);

	// The channel does NOT stop here: while its SAMDMA bit stays set the
	// engine keeps playing. If a fresh SAMADDR/SAMLEN arrived mid-playback,
	// latch it for the next block (echoplex-style chaining); otherwise
	// replay the same block.
	if (c.mbHasFresh) {
		c.mPlayStartAddr = c.mStartAddr;
		c.mPlayLength = (uint32)c.mLength + 1;	// effective = raw + 1
		c.mbHasFresh = false;
	}

	c.mPhase = 0;

	// A new (or replayed) block restarts the IMA-ADPCM stream from a zero
	// predictor; harmless for the PCM formats.
	ResetDecoder(c);
	// mbActive is left untouched; only clearing the SAMDMA bit stops it.
}

void ATPokeyMaxSampleEmulator::WriteControl(uint8 reg, uint8 value) {
	// Audio-affecting registers must take effect at "now": flush first.
	// (RAM loader writes and staged SAMADDR/SAMLEN do NOT change the audio
	// being rendered at the current instant, so they skip the flush -- this
	// also avoids 43008 flushes during a bulk RAM load.)
	switch(reg) {
		case 13:	// SAMPERL
		case 14:	// SAMPERH
		case 15:	// SAMVOL
		case 16:	// SAMDMA
		case 17:	// SAMIRQEN  (affects the IRQ line -> flush to "now" first)
		case 18:	// SAMIRQACT (write-0 clears -> may lower the IRQ line)
			Flush();
			break;
		default:
			break;
	}

	const int sel = SelectedChannel();
	Channel& c = mChannels[sel];

	switch(reg) {
		case 4:		// RAMADDRL
			mRAMAddr = (uint16)((mRAMAddr & 0xFF00) | value);
			break;

		case 5:		// RAMADDRH
			mRAMAddr = (uint16)((mRAMAddr & 0x00FF) | ((uint32)value << 8));
			break;

		case 6:		// RAMDATA (no auto-increment)
			mRAM[mRAMAddr % kSampleRAMSize] = value;
			break;

		case 7:		// RAMDATAINC (auto-increment on write)
			mRAM[mRAMAddr % kSampleRAMSize] = value;
			mRAMAddr = (uint16)(mRAMAddr + 1);
			break;

		case 8:		// CHANSEL (1-based)
			mChanSel = value;
			break;

		case 9:		// SAMADDRL (banked, buffered)
			c.mStartAddr = (uint16)((c.mStartAddr & 0xFF00) | value);
			c.mbHasFresh = true;
			break;

		case 10:	// SAMADDRH (banked, buffered)
			c.mStartAddr = (uint16)((c.mStartAddr & 0x00FF) | ((uint32)value << 8));
			c.mbHasFresh = true;
			break;

		case 11:	// SAMLENL (banked, buffered)
			c.mLength = (uint16)((c.mLength & 0xFF00) | value);
			c.mbHasFresh = true;
			break;

		case 12:	// SAMLENH (banked, buffered)
			c.mLength = (uint16)((c.mLength & 0x00FF) | ((uint32)value << 8));
			c.mbHasFresh = true;
			break;

		case 13:	// SAMPERL (banked, immediate)
			c.mPeriod = (uint16)((c.mPeriod & 0xFF00) | value);
			RecomputeInc(sel);
			break;

		case 14:	// SAMPERH (banked, immediate)
			c.mPeriod = (uint16)((c.mPeriod & 0x00FF) | ((uint32)value << 8));
			RecomputeInc(sel);
			break;

		case 15:	// SAMVOL (banked, immediate; 0..63)
			c.mVolume = (uint8)(value & 0x3F);
			break;

		case 16: {	// SAMDMA (per-channel enable mask)
			const uint8 oldMask = mDMA;
			const uint8 newMask = value;

			for(int ch=0; ch<4; ++ch) {
				const uint8 bit = (uint8)(1 << ch);
				const bool was = (oldMask & bit) != 0;
				const bool now = (newMask & bit) != 0;
				Channel& cc = mChannels[ch];

				if (now && !was) {
					// Rising edge -> (re)start playback from the current
					// register-visible SAMADDR/SAMLEN.
					cc.mPlayStartAddr = cc.mStartAddr;
					cc.mPlayLength = (uint32)cc.mLength + 1;	// effective = raw + 1
					cc.mPhase = 0;
					cc.mbHasFresh = false;
					RecomputeInc(ch);
					ResetDecoder(cc);
					cc.mbActive = true;
				} else if (!now && was) {
					// Falling edge -> stop/silence this channel.
					cc.mbActive = false;
				}
			}

			mDMA = newMask;
			break;
		}

		case 17:	// SAMIRQEN
			mIRQEn = value;
			break;

		case 18:	// SAMIRQACT (write-0 clears that status bit)
			mIRQAct &= value;
			break;

		case 19:	// SAMCFG
			mCfg = value;
			break;

		default:
			break;
	}

	// Registers that change the IRQ line or the next end-of-sample deadline
	// must re-assert/clear the CPU IRQ and re-arm the scheduler event. (The
	// Flush above already advanced the channels to "now" for these regs.)
	switch(reg) {
		case 13:	// SAMPERL  (period -> deadline)
		case 14:	// SAMPERH
		case 16:	// SAMDMA   (active set -> deadline + line)
		case 17:	// SAMIRQEN (line + which channels arm the event)
		case 18:	// SAMIRQACT(line)
			UpdateIRQ();
			break;
		default:
			break;
	}
}

uint8 ATPokeyMaxSampleEmulator::ReadRegCommon(uint8 reg) const {
	const int sel = SelectedChannel();
	const Channel& c = mChannels[sel];

	switch(reg) {
		case 4:		return (uint8)(mRAMAddr & 0xFF);
		case 5:		return (uint8)(mRAMAddr >> 8);
		case 6:		return mRAM[mRAMAddr % kSampleRAMSize];
		// reg 7 (RAMDATAINC) is handled by the caller (increment differs).
		case 8:		return mChanSel;
		case 9:		return (uint8)(c.mStartAddr & 0xFF);
		case 10:	return (uint8)(c.mStartAddr >> 8);
		case 11:	return (uint8)(c.mLength & 0xFF);
		case 12:	return (uint8)(c.mLength >> 8);
		case 13:	return (uint8)(c.mPeriod & 0xFF);
		case 14:	return (uint8)(c.mPeriod >> 8);
		case 15:	return c.mVolume;
		case 16:	return mDMA;
		case 17:	return mIRQEn;
		case 18:	return mIRQAct;
		case 19:	return mCfg;
		default:	return 0xFF;
	}
}

uint8 ATPokeyMaxSampleEmulator::ReadControl(uint8 reg) {
	// $D287 RAMDATAINC is write-only on real hardware (per the PokeyMax dev
	// guide): the RAMADDR auto-increment happens ONLY on write, never on
	// read. Reading must NOT increment -- otherwise the 6502 STA (zp),Y
	// dummy read of the target address (cc65's bulk RAM loader) would bump
	// RAMADDR a second time per byte, dropping every other byte and
	// corrupting the loaded sample. Treat a read like RAMDATA (no inc).
	if (reg == 7)
		return mRAM[mRAMAddr % kSampleRAMSize];

	return ReadRegCommon(reg);
}

uint8 ATPokeyMaxSampleEmulator::DebugReadControl(uint8 reg) const {
	if (reg == 7)	// pure read -- no auto-increment
		return mRAM[mRAMAddr % kSampleRAMSize];

	return ReadRegCommon(reg);
}

void ATPokeyMaxSampleEmulator::UpdateIRQ() {
	// The 6502 IRQ line is the OR of the per-channel SAMIRQACT status bits
	// masked by SAMIRQEN. Only issue Assert/Negate on a real edge.
	const bool active = (mIRQAct & mIRQEn) != 0;

	if (active != mbIRQAsserted) {
		mbIRQAsserted = active;

		if (mpIRQController && mIRQBit) {
			if (active)
				mpIRQController->Assert(mIRQBit, false);
			else
				mpIRQController->Negate(mIRQBit, false);
		}
	}

	RescheduleEvent();
}

void ATPokeyMaxSampleEmulator::RescheduleEvent() {
	// Must be called with the channels advanced to "now" (i.e. right after a
	// Flush) so mPhase/mCycleAccum are current.
	if (!mpScheduler)
		return;

	uint64 best = ~UINT64_C(0);

	for(int ch=0; ch<4; ++ch) {
		const Channel& c = mChannels[ch];

		// Only IRQ-enabled active channels need a prompt flush -- the CPU
		// reprograms them from the IRQ handler before the next block end.
		if (!c.mbActive)
			continue;
		if (!(mIRQEn & (1 << ch)))
			continue;
		if (c.mInc == 0)
			continue;

		const uint64 target = (uint64)c.mPlayLength << kPhaseShift;

		// k = number of output samples until the source index reaches the
		// effective length (end-of-sample is detected when idx >= length).
		uint64 k;
		if (c.mPhase >= target)
			k = 0;
		else
			k = (uint64)(target - c.mPhase + c.mInc - 1) / c.mInc;

		// Run() generates floor((mCycleAccum + cycles)/28) samples and reads
		// index k on the (k+1)-th sample, so it needs (k+1)*28 - mCycleAccum
		// machine cycles to detect this end. (Always >= 1 since accum < 28.)
		uint64 cyc = (k + 1) * 28;
		cyc = (cyc > (uint64)mCycleAccum) ? (cyc - mCycleAccum) : 1;

		if (cyc < best)
			best = cyc;
	}

	if (best == ~UINT64_C(0)) {
		mpScheduler->UnsetEvent(mpEndEvent);
		return;
	}

	// Cap very long deadlines so the event re-evaluates periodically rather
	// than scheduling a multi-second tick.
	if (best > 1000000)
		best = 1000000;

	mpScheduler->SetEvent((uint32)best, this, kEndEventId, mpEndEvent);
}

void ATPokeyMaxSampleEmulator::OnScheduledEvent(uint32 id) {
	// The scheduler auto-removes the fired event; clear our handle first.
	mpEndEvent = nullptr;

	// Advance to the end-of-sample instant (latches SAMIRQACT / applies block
	// chaining), then assert the CPU IRQ and re-arm for the next end.
	Flush();
	UpdateIRQ();
}

void ATPokeyMaxSampleEmulator::Flush() {
	if (!mpScheduler)
		return;

	uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
	uint32 dt = t - mLastUpdate;
	mLastUpdate = t;

	Run(dt);
}

void ATPokeyMaxSampleEmulator::Run(uint32 cycles) {
	// One output sample spans 28 machine (PHI2) cycles. Keep a persistent
	// fractional residue so timing never drifts across Flush boundaries.
	mCycleAccum += cycles;
	uint32 samples = mCycleAccum / 28u;
	mCycleAccum %= 28u;

	while(samples--) {
		// Buffer-overflow guard (mirrors ATPokeyMaxSIDEmulator): if the
		// drain side (WriteAudio) has fallen behind, stop generating so we
		// never write past the accumulation buffer. The DMA/IRQ bookkeeping
		// for the dropped samples is conservatively skipped too.
		if (mOutputLevel >= kAccumBufferSize)
			break;

		float left = 0.0f;
		float right = 0.0f;

		for(int ch=0; ch<4; ++ch) {
			Channel& c = mChannels[ch];
			if (!c.mbActive)
				continue;

			uint32 idx = (uint32)(c.mPhase >> kPhaseShift);

			// End-of-sample handling (replay-while-DMA). The loop covers the
			// degenerate case where a freshly latched block is itself already
			// exhausted; mPlayLength is always >= 1 so it terminates.
			while(idx >= c.mPlayLength) {
				HandleEndOfSample(ch);
				idx = (uint32)(c.mPhase >> kPhaseShift);
			}

			// SAMCFG selects the per-channel format:
			//   ADPCM bit (0+ch) set -> 4-bit IMA-ADPCM (takes priority)
			//   else 8-bit bit (4+ch) set -> 8-bit signed PCM
			//   else                       -> 4-bit signed linear PCM
			// idx counts source *bytes* for 8-bit and source *nibbles* (low
			// nibble first) for the two 4-bit formats. Each path yields a
			// signed ~8-bit value fed into the shared (sample * volume) mix.
			const bool isADPCM = (mCfg & (uint8)(0x01 << ch)) != 0;
			const bool is8bit  = (mCfg & (uint8)(0x10 << ch)) != 0;

			int s;
			if (isADPCM) {
				s = DecodeSampleADPCM(c, idx);
			} else if (is8bit) {
				s = (int)(sint8)mRAM[(uint32)(c.mPlayStartAddr + idx) % kSampleRAMSize];
			} else {
				const uint32 byteAddr = (uint32)(c.mPlayStartAddr + (idx >> 1)) % kSampleRAMSize;
				const uint8 b = mRAM[byteAddr];
				const uint8 nib = (idx & 1) ? (uint8)(b >> 4) : (uint8)(b & 0x0F);

				// Sign-extend the 4-bit sample (-8..7) and scale to ~8-bit.
				const int signed4 = (int)(nib ^ 0x08) - 0x08;
				s = signed4 << 4;
			}

			const float v = (float)s * (float)c.mVolume;

			// Default panning mirrors the sibling 4-channel Covox DAC:
			// ch1->L, ch2->R, ch3->R, ch4->L (LRRL).
			if (ch == 0 || ch == 3)
				left += v;
			else
				right += v;

			c.mPhase += c.mInc;
		}

		mAccumLeft[mOutputLevel] = left;
		mAccumRight[mOutputLevel] = right;
		++mOutputLevel;
	}
}

void ATPokeyMaxSampleEmulator::WriteAudio(const ATSyncAudioMixInfo& mixInfo) {
	float *const dstLeft = mixInfo.mpLeft;
	float *const dstRight = mixInfo.mpRight;
	const uint32 n = mixInfo.mCount;

	Flush();

	// Draining the block may have crossed end-of-sample boundaries; sync the
	// IRQ line and re-arm the end event.
	UpdateIRQ();

	VDASSERT(n <= kAccumBufferSize);

	// If we don't have enough samples yet, pad out; we'll catch up.
	if (mOutputLevel < n) {
		memset(mAccumLeft + mOutputLevel, 0, sizeof(mAccumLeft[0]) * (n - mOutputLevel));
		memset(mAccumRight + mOutputLevel, 0, sizeof(mAccumRight[0]) * (n - mOutputLevel));

		mOutputLevel = n;
	}

	// Normalize int8*vol (max 128*63) to ~unit scale, /2 for the two
	// channels summed per side -- matching ATCovoxEmulator's 1/128/2 scale
	// for the sibling 4-channel DAC.
	static constexpr float kSampleScale = 1.0f / 128.0f / 63.0f / 2.0f;

	// Gate output via the runtime enable. When disabled the buffer is still
	// drained below so it never overflows.
	const float volume = mbEnabled ? mixInfo.mpMixLevels[kATAudioMix_Other] * kSampleScale : 0.0f;

	if (volume != 0.0f) {
		for(uint32 i=0; i<n; ++i)
			dstLeft[i] += mAccumLeft[i] * volume;

		if (dstRight) {
			for(uint32 i=0; i<n; ++i)
				dstRight[i] += mAccumRight[i] * volume;
		} else {
			// Defensive: mono mixing buffer -> fold the right channel into
			// the left so it is not silently dropped.
			for(uint32 i=0; i<n; ++i)
				dstLeft[i] += mAccumRight[i] * volume;
		}
	}

	// Shift down the accumulation buffers.
	const uint32 samplesLeft = mOutputLevel - n;

	if (samplesLeft) {
		memmove(mAccumLeft, mAccumLeft + n, samplesLeft * sizeof(mAccumLeft[0]));
		memmove(mAccumRight, mAccumRight + n, samplesLeft * sizeof(mAccumRight[0]));
	}

	mOutputLevel = samplesLeft;
}

void ATPokeyMaxSampleEmulator::SaveState(vdfastvector<uint8>& dst) const {
	dst.clear();

	// version byte (bump if the layout changes)
	PutU8(dst, 1);

	PutU16(dst, mRAMAddr);
	PutU8 (dst, mChanSel);
	PutU8 (dst, mDMA);
	PutU8 (dst, mIRQEn);
	PutU8 (dst, mIRQAct);
	PutU8 (dst, mCfg);
	PutU32(dst, mCycleAccum);

	for(int i=0; i<4; ++i) {
		const Channel& c = mChannels[i];

		PutU16(dst, c.mStartAddr);
		PutU16(dst, c.mLength);
		PutU16(dst, c.mPeriod);
		PutU8 (dst, c.mVolume);
		PutU8 (dst, c.mbHasFresh ? 1 : 0);
		PutU16(dst, c.mPlayStartAddr);
		PutU32(dst, c.mPlayLength);
		PutU64(dst, c.mPhase);
		PutU64(dst, c.mInc);
		PutU8 (dst, c.mbActive ? 1 : 0);
		PutU32(dst, (uint32)c.mAdpcmPredictor);
		PutU32(dst, (uint32)c.mAdpcmStepIndex);
		PutU32(dst, (uint32)c.mAdpcmDecodedIdx);
		PutU32(dst, (uint32)c.mDecodedSample);
	}

	// 42 KiB on-board sample RAM.
	dst.insert(dst.end(), mRAM, mRAM + kSampleRAMSize);
}

void ATPokeyMaxSampleEmulator::LoadState(const uint8 *data, size_t len) {
	// header (12) + 4 channels * 47 + RAM.
	constexpr size_t kPerChannel = 2+2+2+1+1+2+4+8+8+1 + 4+4+4+4;	// = 47
	constexpr size_t kExpected = 12 + kPerChannel * 4 + kSampleRAMSize;

	if (!data || len < kExpected)
		return;

	const uint8 *p = data;

	const uint8 version = GetU8(p);
	if (version != 1)
		return;

	mRAMAddr = GetU16(p);
	mChanSel = GetU8(p);
	mDMA     = GetU8(p);
	mIRQEn   = GetU8(p);
	mIRQAct  = GetU8(p);
	mCfg     = GetU8(p);
	mCycleAccum = GetU32(p) % 28u;

	for(int i=0; i<4; ++i) {
		Channel& c = mChannels[i];

		c.mStartAddr     = GetU16(p);
		c.mLength        = GetU16(p);
		c.mPeriod        = GetU16(p);
		c.mVolume        = (uint8)(GetU8(p) & 0x3F);
		c.mbHasFresh     = GetU8(p) != 0;
		c.mPlayStartAddr = GetU16(p);
		c.mPlayLength    = GetU32(p);
		c.mPhase         = GetU64(p);
		c.mInc           = GetU64(p);
		c.mbActive       = GetU8(p) != 0;
		c.mAdpcmPredictor  = (sint32)GetU32(p);
		c.mAdpcmStepIndex  = (sint32)GetU32(p);
		c.mAdpcmDecodedIdx = (sint32)GetU32(p);
		c.mDecodedSample   = (sint32)GetU32(p);

		// Defensive clamps so a corrupt blob can't index out of range.
		if (c.mPlayLength < 1)
			c.mPlayLength = 1;
		if (c.mAdpcmStepIndex < 0)
			c.mAdpcmStepIndex = 0;
		else if (c.mAdpcmStepIndex > 88)
			c.mAdpcmStepIndex = 88;
	}

	memcpy(mRAM, p, kSampleRAMSize);
	p += kSampleRAMSize;

	// Audio accumulation is transient; restart it and re-anchor the clock.
	mOutputLevel = 0;
	memset(mAccumLeft, 0, sizeof mAccumLeft);
	memset(mAccumRight, 0, sizeof mAccumRight);
	mLastUpdate = mpScheduler ? ATSCHEDULER_GETTIME(mpScheduler) : 0;

	// Restore the IRQ line + end-of-sample event from the loaded state.
	mbIRQAsserted = false;
	UpdateIRQ();
}
