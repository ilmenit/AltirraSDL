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

//=========================================================================
// PokeyMax SAMPLE engine (block-RAM sample player)  --  Phase A
//
// PokeyMax exposes a 4-channel DMA sample player backed by 42 KiB of
// on-board RAM. Software loads PCM/ADPCM data into the RAM through an
// auto-incrementing loader port, programs per-channel start/length/period/
// volume registers (banked through a channel-select register), and then
// enables per-channel DMA. The engine streams the data to four DACs.
//
// $D280-$D283 are the manual COVOX DAC (handled by ATCovoxEmulator); the
// SAMPLE engine owns $D284-$D293:
//
//   $D284  4   RAMADDRL    loader RAM pointer low
//   $D285  5   RAMADDRH    loader RAM pointer high
//   $D286  6   RAMDATA     read/write RAM at pointer (no auto-increment)
//   $D287  7   RAMDATAINC  read/write RAM at pointer, then auto-increment
//   $D288  8   CHANSEL     selected channel for the banked registers (1..4)
//   $D289  9   SAMADDRL    (banked) sample start byte address low
//   $D28A 10   SAMADDRH    (banked) sample start byte address high
//   $D28B 11   SAMLENL     (banked) raw length low  (effective = raw + 1)
//   $D28C 12   SAMLENH     (banked) raw length high
//   $D28D 13   SAMPERL     (banked) period low   (rate = 2*PHI2/period)
//   $D28E 14   SAMPERH     (banked) period high
//   $D28F 15   SAMVOL      (banked) volume 0..63
//   $D290 16   SAMDMA      per-channel DMA enable mask (bit0=ch1..bit3=ch4)
//   $D291 17   SAMIRQEN    per-channel IRQ enable mask
//   $D292 18   SAMIRQACT   per-channel IRQ-active; write-0 clears that bit
//   $D293 19   SAMCFG      bits0-3 per-ch ADPCM, bits4-7 per-ch 8-bit(1)/
//                          4-bit(0).  Default $F0 (all 8-bit, no ADPCM)
//
// Scope (this file):
//   * Three SAMCFG data formats, selected per channel:
//       - 8-bit signed PCM        (SAMCFG bit 4+ch = 1, ADPCM bit = 0)
//       - 4-bit signed linear PCM (SAMCFG bit 4+ch = 0, ADPCM bit = 0)
//       - 4-bit IMA-ADPCM         (SAMCFG bit 0+ch = 1; sox -e ima-adpcm)
//     For the 4-bit formats SAMLEN/SAMADDR index source *nibbles* (low nibble
//     first); for 8-bit they index bytes.
//   * SAMLEN effective length is raw + 1.
//   * End-of-sample is NOT one-shot: while the channel's SAMDMA bit stays
//     set, the engine sets the channel's SAMIRQACT status bit, applies a
//     freshly written SAMADDR/SAMLEN if one arrived mid-playback (echoplex-
//     style block chaining) or otherwise replays the same sample, and keeps
//     playing. A channel only goes silent when its SAMDMA bit is cleared.
//   * SAMIRQACT latches the per-channel status bit AND, when the channel's
//     SAMIRQEN bit is set, asserts the real 6502 IRQ line via the shared
//     ATIRQController. A scheduler event flushes the engine exactly at each
//     pending end-of-sample so the IRQ (and block chaining) are prompt.
//   * Save-state: SaveState/LoadState round-trip the whole engine.
//
// Integration: like ATPokeyMaxSIDEmulator, this engine does NOT install its
// own memory layer -- ATDevicePokeyMax owns the $D2 config overlay and
// forwards register accesses through WriteControl / ReadControl. The engine
// registers itself as an IATSyncAudioSource and mixes the four channels into
// the stereo bus directly (no edge buffers). Default panning mirrors the
// sibling 4-channel Covox DAC in the same physical area: ch1->L, ch2->R,
// ch3->R, ch4->L (LRRL). [TODO: confirm SAMPLE panning against hardware.]
//
// Note: the output sample rate is PHI2/28 and the playback rate is
// 2*PHI2/period, so the per-output-sample phase increment (56/period) is
// independent of PHI2 -- pitch automatically tracks PAL/NTSC with no clock
// caching required.
//=========================================================================

#ifndef f_AT_POKEYMAXSAMPLE_H
#define f_AT_POKEYMAXSAMPLE_H

#include <vd2/system/memory.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/audiosource.h>
#include <at/atcore/scheduler.h>

class ATScheduler;
class IATAudioMixer;
class ATConsoleOutput;
class ATIRQController;

class ATPokeyMaxSampleEmulator final : public VDAlignedObject<16>, public IATSyncAudioSource, public IATSchedulerCallback {
	ATPokeyMaxSampleEmulator(const ATPokeyMaxSampleEmulator&) = delete;
	ATPokeyMaxSampleEmulator& operator=(const ATPokeyMaxSampleEmulator&) = delete;
public:
	// 42 KiB on-board sample RAM (168 * 256 = 43008 bytes).
	static constexpr uint32 kSampleRAMSize = 43008;

	ATPokeyMaxSampleEmulator();
	~ATPokeyMaxSampleEmulator();

	// Does NOT install a memory layer -- PokeyMax owns the $D2 overlay and
	// forwards register accesses. Registers this object as a sync audio
	// source. The IRQ controller + pre-allocated IRQ bit (owned by PokeyMax)
	// drive the real 6502 IRQ line when an enabled channel reaches end-of-
	// sample (SAMIRQACT & SAMIRQEN); pass null/0 to run without CPU IRQs
	// (e.g. the unit test).
	void Init(ATScheduler *sch, IATAudioMixer *mixer, ATIRQController *irq = nullptr, uint32 irqBit = 0);
	void Shutdown();

	void ColdReset();
	void WarmReset();

	void DumpStatus(ATConsoleOutput& output);

	// Gate audio output (RESTRICT bit4, shared with Covox). Leads with
	// Flush() so the change takes effect exactly at "now". When disabled the
	// DMA/IRQ bookkeeping still runs (positions advance, SAMIRQACT still
	// latches); only the mixed amplitude is muted.
	void SetEnabled(bool enabled);

	// Register access. reg is the offset from $D280 (i.e. 4..19 for
	// $D284-$D293). WriteControl / ReadControl have side effects (RAMDATAINC
	// auto-increment, SAMDMA edge handling, ...); DebugReadControl is pure.
	void WriteControl(uint8 reg, uint8 value);
	uint8 ReadControl(uint8 reg);
	uint8 DebugReadControl(uint8 reg) const;

	// Phase B save-state: serialize the full engine (registers, per-channel
	// playback + ADPCM decoder state, and the 42 KiB sample RAM) as a raw
	// versioned blob, mirroring the PSG/SID snapshot pattern in pokeymax.cpp.
	void SaveState(vdfastvector<uint8>& dst) const;
	void LoadState(const uint8 *data, size_t len);

public:	// IATSyncAudioSource
	bool RequiresStereoMixingNow() const override { return true; }
	void WriteAudio(const ATSyncAudioMixInfo& mixInfo) override;

public:	// IATSchedulerCallback
	// Fired at the next pending end-of-sample of an IRQ-enabled channel so the
	// CPU IRQ latches promptly (echoplex-style block chaining reprograms the
	// channel from the IRQ handler before the next block end).
	void OnScheduledEvent(uint32 id) override;

protected:
	void Flush();
	void Run(uint32 cycles);
	void HandleEndOfSample(int chIndex);
	void RecomputeInc(int chIndex);
	int SelectedChannel() const;

	// Phase C: assert/negate the 6502 IRQ line from (mIRQAct & mIRQEn) and
	// (re)arm the end-of-sample scheduler event for the nearest IRQ-enabled
	// active channel. Call after any Flush and after IRQ/DMA/period writes.
	void UpdateIRQ();
	void RescheduleEvent();

	// Shared side-effect-free register read (used by both ReadControl and
	// DebugReadControl). RAMDATAINC ($D287) auto-increment is NOT handled
	// here -- the caller applies it.
	uint8 ReadRegCommon(uint8 reg) const;

	// Fixed-point phase: integer sample index in the high 32 bits.
	static constexpr int kPhaseShift = 32;

	struct Channel {
		// Register-visible (CHANSEL-banked) latches:
		uint16	mStartAddr = 0;		// SAMADDR raw byte address (R/W)
		uint16	mLength = 0;		// SAMLEN raw value (effective = raw + 1)
		uint16	mPeriod = 0;		// SAMPER (immediate)
		uint8	mVolume = 0;		// SAMVOL 0..63 (immediate)

		// "Fresh SAMADDR/SAMLEN written since the last (re)start" flag. When
		// set, the next replay latches the register values; otherwise it
		// replays the same block.
		bool	mbHasFresh = false;

		// Live playback state (the block actually being streamed):
		uint16	mPlayStartAddr = 0;	// byte address currently playing from
		uint32	mPlayLength = 1;	// effective length (raw + 1) being played
		uint64	mPhase = 0;			// fixed-point read position
		uint64	mInc = 0;			// phase increment per output sample
		bool	mbActive = false;	// DMA running for this channel

		// Phase C: IMA-ADPCM decoder state. ADPCM is stateful/sequential, so
		// each channel carries its running predictor (16-bit), step-table
		// index (0..88), the last nibble index decoded into the predictor
		// (-1 = nothing decoded yet) and the cached 8-bit-scaled output for
		// when the phase holds on the same source index (period > 56).
		sint32	mAdpcmPredictor = 0;
		sint32	mAdpcmStepIndex = 0;
		sint32	mAdpcmDecodedIdx = -1;
		sint32	mDecodedSample = 0;
	};

	// Phase C: reset a channel's IMA-ADPCM decoder on (re)start or replay.
	void ResetDecoder(Channel& c);
	// Phase C: sequential IMA-ADPCM decode (decodes every intermediate nibble
	// up to the requested source index, advancing the per-channel predictor).
	int DecodeSampleADPCM(Channel& c, uint32 idx);

	ATScheduler *mpScheduler = nullptr;
	IATAudioMixer *mpAudioMixer = nullptr;
	bool mbRegistered = false;
	bool mbEnabled = false;

	// Phase C: 6502 IRQ line. The bit is allocated/freed by the owning
	// ATDevicePokeyMax; mbIRQAsserted tracks the last edge so Assert/Negate
	// are only issued on a real transition.
	ATIRQController *mpIRQController = nullptr;
	uint32 mIRQBit = 0;
	bool mbIRQAsserted = false;
	ATEvent *mpEndEvent = nullptr;

	uint16	mRAMAddr = 0;	// loader pointer (RAMADDRL/H)
	uint8	mChanSel = 1;	// CHANSEL (1-based)
	uint8	mDMA = 0;		// SAMDMA mask
	uint8	mIRQEn = 0;		// SAMIRQEN mask
	uint8	mIRQAct = 0;	// SAMIRQACT status
	uint8	mCfg = 0xF0;	// SAMCFG (default: all 8-bit, no ADPCM)

	uint32	mLastUpdate = 0;	// machine-cycle time of the last Flush
	uint32	mCycleAccum = 0;	// leftover machine cycles (< 28) between Flushes

	Channel	mChannels[4];

	uint8	mRAM[kSampleRAMSize] = {};

	enum {
		kAccumBufferSize = 1536
	};

	uint32	mOutputLevel = 0;	// generated-but-undrained samples
	VDALIGN(16) float mAccumLeft[kAccumBufferSize] = {};
	VDALIGN(16) float mAccumRight[kAccumBufferSize] = {};
};

#endif	// f_AT_POKEYMAXSAMPLE_H
