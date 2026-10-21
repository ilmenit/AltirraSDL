//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2021 Avery Lee
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
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#include "stdafx.h"
#include <at/atcore/savestate.h>
#include <at/ataudio/pokeysavestate.h>

template<ATExchanger T>
void ATSaveStatePokeyInternal::Exchange(T& rw) {
	rw.Transfer("clock15_offset", &mClock15Offset);
	rw.Transfer("clock64_offset", &mClock64Offset);
	rw.Transfer("poly9_offset", &mPoly9Offset);
	rw.Transfer("poly17_offset", &mPoly17Offset);
	rw.Transfer("polyoff_offset", &mPolyShutOffTime);
	rw.TransferArray("timer_counters", mTimerCounters);
	rw.TransferArray("timer_borrow_counters", mTimerBorrowCounters);
	rw.TransferArray("two_tone_reset_counters", mTwoToneResetCounters);
	rw.Transfer("serin_counter", &mSerInCounter);
	rw.Transfer("serin_shift_register", &mSerInShiftRegister);
	rw.Transfer("serin_deferred_load", &mbSerInDeferredLoad);
	rw.Transfer("serin_waiting_for_start_bit", &mbSerInWaitingForStartBit);

	rw.Transfer("serout_event_time", &mSerOutEventTime);
	rw.Transfer("serout_counter", &mSerOutCounter);
	rw.Transfer("serout_shift_register", &mSerOutShiftRegister);
	rw.Transfer("serout_valid", &mbSerOutValid);
	rw.Transfer("serout_shift_valid", &mbSerOutShiftValid);

	rw.Transfer("trace_byte_index", &mTraceByteIndex);

	rw.Transfer("renderer_poly4_offset", &mRendererState.mPoly4Offset);
	rw.Transfer("renderer_poly5_offset", &mRendererState.mPoly5Offset);
	rw.Transfer("renderer_poly9_offset", &mRendererState.mPoly9Offset);
	rw.Transfer("renderer_poly17_offset", &mRendererState.mPoly17Offset);
	rw.Transfer("output_flip_flops", &mRendererState.mOutputFlipFlops);

	rw.TransferArray("pot_charge_levels", mPotChargeLevels);
	rw.Transfer("pot_master_counter", &mPotMasterCounter);
	rw.Transfer("pot_scan_active", &mbPotScanActive);

	// Preserve the electrical input state and scan phase when rewinding.
	rw.Transfer("pot_runtime_exact", &mbPotRuntimePresent);
	rw.TransferArray("pot_hi_positions", mPotHiPositions);
	rw.TransferArray("pot_charge_rates", mPotChargeRates);
	rw.Transfer("pot_grounded_mask", &mPotGroundedMask);
	rw.Transfer("pot_last_scan_age", &mPotLastScanAge);
	rw.Transfer("pot_last_fast_offset", &mPotLastFastOffset);
	rw.Transfer("pot_last_slow_offset", &mPotLastSlowOffset);

	// Fork extension: exact serial engine replay. Missing fields in old
	// snapshots retain the upstream compatibility behavior.
	rw.Transfer("serial_serial_state_present", &mbSerialStatePresent);
	rw.Transfer("serial_serin", &mSERIN);
	rw.Transfer("serial_serout", &mSEROUT);
	rw.Transfer("serial_serial_input_pending_status", &mSerialInputPendingStatus);
	rw.Transfer("serial_ser_in_event_delay", &mSerInEventDelay);
	rw.Transfer("serial_serial_output_start_age", &mSerialOutputStartAge);
	rw.Transfer("serial_serial_output_state", &mbSerialOutputState);
	rw.Transfer("serial_serial_rate_changed", &mbSerialRateChanged);
	rw.Transfer("serial_ser_in_burst_pending_irq1", &mbSerInBurstPendingIRQ1);
	rw.Transfer("serial_ser_in_burst_pending_irq2", &mbSerInBurstPendingIRQ2);
	rw.Transfer("serial_ser_in_burst_pending_data", &mbSerInBurstPendingData);
	rw.Transfer("serial_ser_out_burst_deadline_present", &mbSerOutBurstDeadlinePresent);
	rw.Transfer("serial_ser_out_burst_deadline_offset", &mSerOutBurstDeadlineOffset);
	rw.Transfer("serial_serial_input_reset_counter", &mSerialInputResetCounter);
	rw.Transfer("serial_serial_simulate_input_base_offset", &mSerialSimulateInputBaseOffset);
	rw.Transfer("serial_serial_simulate_input_cycles_per_bit", &mSerialSimulateInputCyclesPerBit);
	rw.Transfer("serial_serial_simulate_input_data", &mSerialSimulateInputData);
	rw.Transfer("serial_serial_simulate_input_port", &mbSerialSimulateInputPort);
	rw.Transfer("serial_serial_ext_base_offset", &mSerialExtBaseOffset);
	rw.Transfer("serial_serial_ext_period", &mSerialExtPeriod);
	rw.Transfer("serial_serial_data_in_flip_present", &mbSerialDataInFlipPresent);
	rw.Transfer("serial_serial_data_in_flip_offset", &mSerialDataInFlipOffset);
	rw.Transfer("serial_ser_clock_phase", &mbSerClockPhase);

	// Fork extension: exact keyboard replay, optional for old snapshots.
	rw.Transfer("keyboard_exact", &mbKeyboardStatePresent);
	rw.Transfer("keyboard_key_code_timer", &mKeyCodeTimer);
	rw.Transfer("keyboard_key_cooldown_timer", &mKeyCooldownTimer);
	rw.Transfer("keyboard_irqdelay", &mKeyboardIRQDelay);
	rw.Transfer("keyboard_scan_delay", &mKeyboardScanDelay);
	rw.Transfer("keyboard_irqpending", &mbKeyboardIRQPending);
	rw.Transfer("keyboard_use_key_cooldown_timer", &mbUseKeyCooldownTimer);
	rw.Transfer("keyboard_cooked_key_mode", &mbCookedKeyMode);
	rw.Transfer("keyboard_scan_enabled", &mbKeyboardScanEnabled);
	rw.Transfer("keyboard_shift_key_state", &mbShiftKeyState);
	rw.Transfer("keyboard_shift_key_latched_state", &mbShiftKeyLatchedState);
	rw.Transfer("keyboard_control_key_state", &mbControlKeyState);
	rw.Transfer("keyboard_control_key_latched_state", &mbControlKeyLatchedState);
	rw.Transfer("keyboard_break_key_state", &mbBreakKeyState);
	rw.Transfer("keyboard_break_key_latched_state", &mbBreakKeyLatchedState);
	rw.Transfer("keyboard_key_scan_state", &mKeyScanState);
	rw.Transfer("keyboard_key_scan_code", &mKeyScanCode);
	rw.Transfer("keyboard_key_scan_latch", &mKeyScanLatch);
	rw.Transfer("keyboard_key_queue", &mKeyQueue);
	rw.TransferArray("keyboard_matrix", mKeyMatrix);
	rw.TransferArray("keyboard_effective_matrix", mEffectiveKeyMatrix);

	if constexpr (T::IsReader) {
		if (mbPotRuntimePresent) {
			for (uint32 rate : mPotChargeRates)
				if (rate > 0x40000000) throw ATInvalidSaveStateException();
			for (uint32 position : mPotHiPositions)
				if (position > 0x7FFFFFFF) throw ATInvalidSaveStateException();
		}
		if (mbSerialStatePresent &&
			(mSerInEventDelay >= 0x80000000u || mSerOutEventTime >= 0x80000000u
			|| mSerialExtPeriod >= 0x80000000u
			|| (mbSerialSimulateInputPort && !mSerialSimulateInputCyclesPerBit)))
			throw ATInvalidSaveStateException();
		if (mbKeyboardStatePresent) {
			if (mKeyboardIRQDelay > 114 || mKeyboardScanDelay > 114
				|| mKeyScanState > 3 || mKeyScanLatch >= 64
				|| mKeyQueue.size() > 1024 * 1024)
				throw ATInvalidSaveStateException();
			for (uint16 row : mKeyMatrix)
				if (row & ~0x1FF) throw ATInvalidSaveStateException();
			for (uint16 row : mEffectiveKeyMatrix)
				if (row & ~0x1FF) throw ATInvalidSaveStateException();
		}
		if (mClock15Offset >= 114)
			throw ATInvalidSaveStateException();

		if (mClock64Offset >= 28)
			throw ATInvalidSaveStateException();

		if (mPoly9Offset >= 511)
			throw ATInvalidSaveStateException();

		if (mPoly17Offset >= 131071)
			throw ATInvalidSaveStateException();

		if (mRendererState.mPoly4Offset >= 15)
			throw ATInvalidSaveStateException();

		if (mRendererState.mPoly5Offset >= 31)
			throw ATInvalidSaveStateException();

		if (mRendererState.mPoly9Offset >= 511)
			throw ATInvalidSaveStateException();

		if (mRendererState.mPoly17Offset >= 131071)
			throw ATInvalidSaveStateException();

		for(const auto& c : mTimerCounters) {
			if (!c || c > 256)
				throw ATInvalidSaveStateException();
		}

		// SAVESTATE ROUND-TRIP BUG FIX (AltirraSDL netplay, 2026-04-19)
		//
		// The upstream bound here used to be `c > 3`, matching the
		// "normal" path in pokey.cpp (~line 1859) where
		// `mCounterBorrow[channel] = ticksLeft` with `ticksLeft ∈
		// [0,3]`.  But pokey.cpp line ~1920 (the hi-linked-timer
		// borrow-land path for linked 16-bit timers) writes
		// `mCounterBorrow[channel] = ticksLeft + 3` — legitimately
		// setting the counter to values up to 6.  The serializer
		// would happily write a 4/5/6 to disk, and the deserializer
		// would then reject the state as "invalid" on load.
		//
		// In single-player Altirra this almost never bites because
		// users rarely save a state during the sub-cycle window the
		// hi-linked borrow occupies.  Netplay snapshot capture DOES
		// hit it regularly: the host pauses the sim between Load()
		// and serialize, and games like World Karate Championship
		// (music driver running 16-bit linked channels) sit in the
		// borrow window when we pause.  Joiner then gets a valid zip
		// that fails its own deserializer — "The saved state data is
		// invalid." blocking every netplay session.
		//
		// Fix: widen the valid range to [0,6] to match what the
		// serializer can actually produce.  Any future widening in
		// pokey.cpp MUST bump this bound too.
		for(const auto& c : mTimerBorrowCounters) {
			if (c > 6)
				throw ATInvalidSaveStateException();
		}

		for(const auto& c : mTwoToneResetCounters) {
			if (c > 2)
				throw ATInvalidSaveStateException();
		}

		if (mSerInCounter > 19)
			throw ATInvalidSaveStateException();

		if (mSerOutEventTime > 10000000)
			throw ATInvalidSaveStateException();

		if (mSerOutCounter > 20)
			throw ATInvalidSaveStateException();
	}
}

template<ATExchanger T>
void ATSaveStatePokey::Exchange(T& rw) {
	rw.TransferArray("audf", mAUDF);
	rw.TransferArray("audc", mAUDC);
	rw.Transfer("audctl", &mAUDCTL);
	rw.Transfer("irqen", &mIRQEN);
	rw.Transfer("irqst", &mIRQST);
	rw.Transfer("skctl", &mSKCTL);
	rw.Transfer("skstat", &mSKSTAT);
	rw.Transfer("allpot", &mALLPOT);
	rw.Transfer("kbcode", &mKBCODE);
	rw.Transfer("internal_state", &mpInternalState);
	rw.Transfer("stereo_pair", &mpStereoPair);
	rw.Transfer("quad_pair3", &mpQuadPair3);
	rw.Transfer("quad_pair4", &mpQuadPair4);
	rw.TransferArray("pot", mPOT);
}

template void ATSaveStatePokeyInternal::Exchange(ATSerializer&);
template void ATSaveStatePokeyInternal::Exchange(ATDeserializer&);
template void ATSaveStatePokey::Exchange(ATSerializer&);
template void ATSaveStatePokey::Exchange(ATDeserializer&);
