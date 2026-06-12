//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2025 Avery Lee
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

#include <stdafx.h>
#include <vd2/system/vdalloc.h>
#include <at/atcore/scheduler.h>
#include <at/atcore/audiomixer.h>
#include <at/atcore/audiosource.h>
#include "test.h"
#include <pokeymaxsample.h>

// Register offsets from $D280 used by ATPokeyMaxSampleEmulator::*Control.
namespace {
	enum : uint8 {
		kRAMADDRL	= 4,
		kRAMADDRH	= 5,
		kRAMDATA	= 6,
		kRAMDATAINC	= 7,
		kCHANSEL	= 8,
		kSAMADDRL	= 9,
		kSAMADDRH	= 10,
		kSAMLENL	= 11,
		kSAMLENH	= 12,
		kSAMPERL	= 13,
		kSAMPERH	= 14,
		kSAMVOL		= 15,
		kSAMDMA		= 16,
		kSAMIRQEN	= 17,
		kSAMIRQACT	= 18,
		kSAMCFG		= 19,
	};
}

AT_DEFINE_TEST(Emu_PokeyMaxSample) {
	ATScheduler sch;

	vdautoptr<ATPokeyMaxSampleEmulator> sample(new ATPokeyMaxSampleEmulator);

	// Init without a mixer so the engine does not try to register itself as
	// a sync audio source; we drive WriteAudio() directly below.
	sample->Init(&sch, nullptr);
	sample->ColdReset();

	ATPokeyMaxSampleEmulator& s = *sample;

	// --- Defaults after cold reset ------------------------------------
	AT_TEST_ASSERT(s.DebugReadControl(kCHANSEL) == 1);		// CHANSEL 1-based
	AT_TEST_ASSERT(s.DebugReadControl(kSAMCFG) == 0xF0);	// all 8-bit, no ADPCM
	AT_TEST_ASSERT(s.DebugReadControl(kSAMDMA) == 0x00);
	AT_TEST_ASSERT(s.DebugReadControl(kSAMIRQACT) == 0x00);

	// --- RAMDATAINC writes + auto-increment; RAMDATA does not ---------
	s.WriteControl(kRAMADDRL, 0x00);
	s.WriteControl(kRAMADDRH, 0x00);
	s.WriteControl(kRAMDATAINC, 0x11);	// RAM[0]=0x11, ptr->1
	s.WriteControl(kRAMDATAINC, 0x22);	// RAM[1]=0x22, ptr->2
	s.WriteControl(kRAMDATAINC, 0x33);	// RAM[2]=0x33, ptr->3
	AT_TEST_ASSERT(s.DebugReadControl(kRAMADDRL) == 0x03);
	AT_TEST_ASSERT(s.DebugReadControl(kRAMADDRH) == 0x00);

	// Point back to 0 and read with non-incrementing RAMDATA.
	s.WriteControl(kRAMADDRL, 0x00);
	AT_TEST_ASSERT(s.DebugReadControl(kRAMDATA) == 0x11);
	AT_TEST_ASSERT(s.DebugReadControl(kRAMADDRL) == 0x00);	// pointer unchanged

	// RAMDATAINC read auto-increments the pointer.
	AT_TEST_ASSERT(s.ReadControl(kRAMDATAINC) == 0x11);	// RAM[0], ptr->1
	AT_TEST_ASSERT(s.ReadControl(kRAMDATAINC) == 0x22);	// RAM[1], ptr->2
	AT_TEST_ASSERT(s.DebugReadControl(kRAMADDRL) == 0x02);

	// DebugReadControl(RAMDATAINC) must be side-effect free.
	s.WriteControl(kRAMADDRL, 0x00);
	AT_TEST_ASSERT(s.DebugReadControl(kRAMDATAINC) == 0x11);
	AT_TEST_ASSERT(s.DebugReadControl(kRAMADDRL) == 0x00);

	// --- CHANSEL 1-based banking isolation ---------------------------
	// SAMVOL is a banked register. Write a distinct value to each channel
	// bank, then verify every bank reads back independently.
	for(uint8 ch=1; ch<=4; ++ch) {
		s.WriteControl(kCHANSEL, ch);
		s.WriteControl(kSAMVOL, (uint8)(ch * 5));
	}
	for(uint8 ch=1; ch<=4; ++ch) {
		s.WriteControl(kCHANSEL, ch);
		AT_TEST_ASSERTF(s.DebugReadControl(kSAMVOL) == ch * 5,
			"SAMVOL bank %u not isolated", ch);
	}

	// SAMADDR / SAMLEN readback on channel 1.
	s.WriteControl(kCHANSEL, 1);
	s.WriteControl(kSAMADDRL, 0x34);
	s.WriteControl(kSAMADDRH, 0x12);
	s.WriteControl(kSAMLENL, 0x05);
	s.WriteControl(kSAMLENH, 0x00);
	AT_TEST_ASSERT(s.DebugReadControl(kSAMADDRL) == 0x34);
	AT_TEST_ASSERT(s.DebugReadControl(kSAMADDRH) == 0x12);
	AT_TEST_ASSERT(s.DebugReadControl(kSAMLENL) == 0x05);
	AT_TEST_ASSERT(s.DebugReadControl(kSAMLENH) == 0x00);

	// --- SAMDMA mask readback ----------------------------------------
	s.WriteControl(kSAMDMA, 0x05);	// enable ch1 + ch3
	AT_TEST_ASSERT(s.DebugReadControl(kSAMDMA) == 0x05);
	s.WriteControl(kSAMDMA, 0x00);	// stop all
	AT_TEST_ASSERT(s.DebugReadControl(kSAMDMA) == 0x00);

	// =================================================================
	// Behavioral: SAMLEN effective length = raw + 1, replay-while-DMA,
	// and the SAMIRQACT status latch / write-0-clear.
	// =================================================================
	s.ColdReset();

	// Load four PCM bytes into RAM at address 0.
	s.WriteControl(kRAMADDRL, 0x00);
	s.WriteControl(kRAMADDRH, 0x00);
	s.WriteControl(kRAMDATAINC, 0x10);
	s.WriteControl(kRAMDATAINC, 0x20);
	s.WriteControl(kRAMDATAINC, 0x30);
	s.WriteControl(kRAMDATAINC, 0x40);

	// Channel 1: start=0, raw length=1 (effective=2), period=56. With the
	// output rate PHI2/28 and play rate 2*PHI2/period, period=56 yields a
	// phase increment of exactly one source byte per output sample.
	s.WriteControl(kCHANSEL, 1);
	s.WriteControl(kSAMADDRL, 0x00);
	s.WriteControl(kSAMADDRH, 0x00);
	s.WriteControl(kSAMLENL, 0x01);	// raw length 1 -> effective 2
	s.WriteControl(kSAMLENH, 0x00);
	s.WriteControl(kSAMPERL, 56);
	s.WriteControl(kSAMPERH, 0x00);
	s.WriteControl(kSAMVOL, 63);
	s.WriteControl(kSAMCFG, 0xF0);	// 8-bit, no ADPCM
	s.WriteControl(kSAMDMA, 0x01);	// start channel 1

	AT_TEST_ASSERT(s.DebugReadControl(kSAMIRQACT) == 0x00);

	// Drive exactly N output samples: advance the scheduler N*28 cycles,
	// then drain via WriteAudio. mbEnabled is left false (no mixer), but
	// the DMA/IRQ bookkeeping in Run() advances regardless of the mute.
	float left[64] = {};
	float right[64] = {};
	float mixLevels[kATAudioMixCount] = {};
	mixLevels[kATAudioMix_Other] = 1.0f;

	const auto Generate = [&](uint32 nsamples) {
		for(uint32 i=0; i<nsamples * kATCyclesPerSyncSample; ++i)
			ATSCHEDULER_ADVANCE(&sch);

		ATSyncAudioMixInfo info = {};
		info.mStartTime = ATSCHEDULER_GETTIME(&sch);
		info.mCount = nsamples;
		info.mNumCycles = nsamples * kATCyclesPerSyncSample;
		info.mMixingRate = 63920.0f;
		info.mpLeft = left;
		info.mpRight = right;
		info.mpMixLevels = mixLevels;
		info.mpDCLeft = nullptr;
		info.mpDCRight = nullptr;

		s.WriteAudio(info);
	};

	// Two output samples consume indices 0 and 1. If the effective length
	// were the raw value (1) the sample would already have ended; because
	// it is raw+1 (2), SAMIRQACT must still be clear here.
	Generate(2);
	AT_TEST_ASSERTF(s.DebugReadControl(kSAMIRQACT) == 0x00,
		"SAMIRQACT set too early (effective length should be raw+1=2)");

	// The third sample hits index 2 == effective length -> end-of-sample
	// latches SAMIRQACT bit 0.
	Generate(1);
	AT_TEST_ASSERTF(s.DebugReadControl(kSAMIRQACT) == 0x01,
		"SAMIRQACT did not latch at end of sample");

	// --- write-0-clears: writing 0xFE clears bit 0 only ---------------
	s.WriteControl(kSAMIRQACT, 0xFE);
	AT_TEST_ASSERT(s.DebugReadControl(kSAMIRQACT) == 0x00);

	// --- replay while DMA stays enabled -------------------------------
	// Two more samples replay the same 2-sample block and re-latch the
	// status bit, proving playback did not one-shot stop.
	Generate(2);
	AT_TEST_ASSERTF(s.DebugReadControl(kSAMIRQACT) == 0x01,
		"channel did not replay while SAMDMA stayed set");

	// --- clearing SAMDMA stops the channel ----------------------------
	s.WriteControl(kSAMIRQACT, 0xFE);	// clear bit 0
	s.WriteControl(kSAMDMA, 0x00);		// stop channel 1
	AT_TEST_ASSERT(s.DebugReadControl(kSAMIRQACT) == 0x00);

	Generate(8);	// channel is inactive: no further end events
	AT_TEST_ASSERTF(s.DebugReadControl(kSAMIRQACT) == 0x00,
		"channel kept running after SAMDMA cleared");

	return 0;
}
