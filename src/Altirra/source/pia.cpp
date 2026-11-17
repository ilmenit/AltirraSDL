//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2012 Avery Lee
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

#include <stdafx.h>
#include <vd2/system/binary.h>
#include <vd2/system/bitmath.h>
#include <at/atcore/scheduler.h>
#include <at/atcore/snapshotimpl.h>
#include <at/attest/test.h>
#include "pia.h"
#include "irqcontroller.h"
#include "console.h"
#include "savestate.h"
#include "trace.h"

#if VD_CPU_ARM64
#include <arm_neon.h>
#endif

///////////////////////////////////////////////////////////////////////////////

#if VD_CPU_X86 || VD_CPU_X64
	struct ATPIAEmulator::CountedMask32::Impl {
		struct BitMasks {
			__m128i v0;
			__m128i v1;
		};

		static BitMasks ExpandMask(uint32 mask) {
			__m128i v = _mm_cvtsi32_si128(mask);

			v = _mm_unpacklo_epi8(v, v);
			v = _mm_unpacklo_epi8(v, v);

			__m128i v0 = _mm_shuffle_epi32(v, 0x50);
			__m128i v1 = _mm_shuffle_epi32(v, 0xFA);

			__m128i vbitmask  = _mm_set_epi8(-128, 64, 32, 16, 8, 4, 2, 1, -128, 64, 32, 16, 8, 4, 2, 1);
			v0 = _mm_and_si128(v0, vbitmask);
			v1 = _mm_and_si128(v1, vbitmask);

			__m128i vzero = _mm_setzero_si128();
			return BitMasks {
				_mm_cmpeq_epi8(v0, vzero),
				_mm_cmpeq_epi8(v1, vzero)
			};
		}
	};

	void ATPIAEmulator::CountedMask32::AddZeroBits(uint32 mask) {
		const Impl::BitMasks bitMasks = Impl::ExpandMask(mask);

		_mm_store_si128((__m128i *)mCounts + 0, _mm_sub_epi8(_mm_load_si128((const __m128i *)mCounts + 0), bitMasks.v0));
		_mm_store_si128((__m128i *)mCounts + 1, _mm_sub_epi8(_mm_load_si128((const __m128i *)mCounts + 1), bitMasks.v1));
	}

	void ATPIAEmulator::CountedMask32::RemoveZeroBits(uint32 mask) {
		const Impl::BitMasks bitMasks = Impl::ExpandMask(mask);

		_mm_store_si128((__m128i *)mCounts + 0, _mm_add_epi8(_mm_load_si128((const __m128i *)mCounts + 0), bitMasks.v0));
		_mm_store_si128((__m128i *)mCounts + 1, _mm_add_epi8(_mm_load_si128((const __m128i *)mCounts + 1), bitMasks.v1));
	}

	uint32 ATPIAEmulator::CountedMask32::ReadZeroMask() const {
		const __m128i vzero = _mm_setzero_si128();
		const uint32 mask1 = _mm_movemask_epi8(_mm_cmpeq_epi8(_mm_load_si128((const __m128i *)mCounts + 0), vzero));
		const uint32 mask2 = _mm_movemask_epi8(_mm_cmpeq_epi8(_mm_load_si128((const __m128i *)mCounts + 1), vzero));

		return mask1 + (mask2 << 16);
	}
#elif VD_CPU_ARM64
	struct ATPIAEmulator::CountedMask32::Impl {
		static uint8x16x2_t ExpandMask(uint32 mask) {
			static constexpr uint8 kBitMask[16] { 1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128 };
			uint8x16_t vbmask = vld1q_u8(kBitMask);
			uint8x8_t v = vreinterpret_u8_u32(vmov_n_u32(mask));

			uint8x16x2_t r;
			r.val[0] = vceqq_u8(vandq_u8(vcombine_u8(vdup_lane_u8(v, 0), vdup_lane_u8(v, 1)), vbmask), vmovq_n_u8(0));
			r.val[1] = vceqq_u8(vandq_u8(vcombine_u8(vdup_lane_u8(v, 2), vdup_lane_u8(v, 3)), vbmask), vmovq_n_u8(0));
			return r;
		}
	};

	void ATPIAEmulator::CountedMask32::AddZeroBits(uint32 mask) {
		const uint8x16x2_t vmasks = Impl::ExpandMask(mask);
		vst1q_u8(mCounts +  0, vsubq_u8(vld1q_u8(mCounts +  0), vmasks.val[0]));
		vst1q_u8(mCounts + 16, vsubq_u8(vld1q_u8(mCounts + 16), vmasks.val[1]));
	}

	void ATPIAEmulator::CountedMask32::RemoveZeroBits(uint32 mask) {
		const uint8x16x2_t vmasks = Impl::ExpandMask(mask);
		vst1q_u8(mCounts +  0, vaddq_u8(vld1q_u8(mCounts +  0), vmasks.val[0]));
		vst1q_u8(mCounts + 16, vaddq_u8(vld1q_u8(mCounts + 16), vmasks.val[1]));
	}

	uint32 ATPIAEmulator::CountedMask32::ReadZeroMask() const {
		static constexpr uint8 kBitMask[8] { 1, 2, 4, 8, 16, 32, 64, 128 };
		uint8x8_t vbmask = vld1_u8(kBitMask);

		uint32 m0 = vaddv_u8(vand_u8(vceq_u8(vld1_u8(mCounts +  0), vmov_n_u8(0)), vbmask));
		uint32 m1 = vaddv_u8(vand_u8(vceq_u8(vld1_u8(mCounts +  8), vmov_n_u8(0)), vbmask));
		uint32 m2 = vaddv_u8(vand_u8(vceq_u8(vld1_u8(mCounts + 16), vmov_n_u8(0)), vbmask));
		uint32 m3 = vaddv_u8(vand_u8(vceq_u8(vld1_u8(mCounts + 24), vmov_n_u8(0)), vbmask));

		return m0 + (m1 << 8) + (m2 << 16) + (m3 << 24);
	}
#else
	void ATPIAEmulator::CountedMask32::AddZeroBits(uint32 mask) {
		for(int i=0; i<32; ++i) {
			if (!(mask & (1 << i)))
				++mCounts[i];
		}
	}

	void ATPIAEmulator::CountedMask32::RemoveZeroBits(uint32 mask) {
		for(int i=0; i<32; ++i) {
			if (!(mask & (1 << i)))
				--mCounts[i];
		}
	}

	uint32 ATPIAEmulator::CountedMask32::ReadZeroMask() const {
		uint32 v = 0;

		for(int i=0; i<32; ++i) {
			if (!mCounts[i])
				v += (1 << i);
		}

		return v;
	}
#endif

///////////////////////////////////////////////////////////////////////////////

ATPIAEmulator::ATPIAEmulator()
	: mpScheduler(nullptr)
	, mpFloatingInputs(nullptr)
	, mInput(0xFFFFFFFF)
	, mOutput(0xFFFFFFFF)
	, mPortOutput(0)
	, mPortDirection(0)
	, mPORTACTL(0)
	, mPORTBCTL(0)
	, mbPIAEdgeA(false)
	, mbPIAEdgeB(false)
	, mbCA1(true)
	, mbCA2(true)
	, mbCB1(true)
	, mPIACB2(kPIACS_Floating)
	, mOutputReportMask(0)
	, mInputAllocBitmap{0}
	, mIntOutputs{}
{
	// reserve last input so we only allow 255 inputs
	mInputAllocBitmap[7] = UINT32_C(0x80000000);
}

ATPIAEmulator::~ATPIAEmulator() {
	delete[] mpExtInputs;
}

int ATPIAEmulator::AllocInput() {
	for(unsigned i=0; i<8; ++i) {
		if (mInputAllocBitmap[i] != UINT32_C(0xFFFFFFFF)) {
			unsigned subIdx = VDFindLowestSetBitFast(~mInputAllocBitmap[i]);
			unsigned idx = (i << 5) + subIdx;

			mInputAllocBitmap[i] |= (1 << subIdx);

			if (idx >= vdcountof(mIntInputs) && !mpExtInputs)
				mpExtInputs = new uint32[256 - vdcountof(mIntInputs)];

			GetInput(idx) = UINT16_C(0x3FFFF);
			return idx;
		}
	}

	VDASSERT(!"PIA inputs exhausted.");
	return -1;
}

void ATPIAEmulator::FreeInput(int index) {
	if (index >= 0) {
		const uint32 allocField = (unsigned)index >> 5;
		const uint32 allocBit = UINT32_C(1) << (index & 31);

		VDASSERT(mInputAllocBitmap[allocField] & allocBit);

		if (mInputAllocBitmap[allocField] & allocBit) {
			mInputAllocBitmap[allocField] &= ~allocBit;

			SetInput(index, ~UINT32_C(0));
		}
	}
}

void ATPIAEmulator::SetInput(int index, uint32 rval) {
	SetInputBits(index, rval, ~UINT32_C(0));
}

void ATPIAEmulator::SetInputBits(int index, uint32 rval, uint32 mask) {
	VDASSERT(index < 255);

	if (index < 0)
		return;

	uint32& inputSlot = GetInput(index);

	rval = inputSlot ^ ((rval ^ inputSlot) & (mask & 0x3FFFF));

	if (rval != inputSlot) {
		mInputState.RemoveZeroBits(inputSlot);
		mInputState.AddZeroBits(rval);

		inputSlot = rval;

		uint32 v = mInputState.ReadZeroMask();

		if (mInput != v) {
			const uint32 delta = mInput ^ v;

			mInput = v;

			if (delta & 0xFF)
				UpdateTraceInputA();

			UpdateOutput();
		}
	}
}

uint32 ATPIAEmulator::RegisterDynamicInput(bool portb, vdfunction<uint8()> fn) {
	auto& fns = mDynamicInputs[portb];
	auto& tokens = mDynamicInputTokens[portb];
	
	uint32 token;
	
	for(;;) {
		token = ++mDynamicTokenCounter;

		if (!token)
			continue;

		if (std::find(tokens.begin(), tokens.end(), token) == tokens.end())
			break;
	}

	fns.emplace_back(std::move(fn));
	tokens.push_back(token);

	if (portb)
		mbHasDynamicBInputs = true;
	else
		mbHasDynamicAInputs = true;

	return token;
}

void ATPIAEmulator::UnregisterDynamicInput(bool portb, uint32 token) {
	if (!token)
		return;

	auto& tokens = mDynamicInputTokens[portb];

	auto it = std::find(tokens.begin(), tokens.end(), token);
	if (it != tokens.end()) {
		const ptrdiff_t pos = it - tokens.begin();

		*it = tokens.back();
		tokens.pop_back();

		auto& fns = mDynamicInputs[portb];
		auto it2 = fns.begin() + pos;
		
		if (&*it2 != &fns.back())
			*it2 = std::move(mDynamicInputs->back());

		fns.pop_back();

		if (fns.empty()) {
			if (portb)
				mbHasDynamicBInputs = false;
			else
				mbHasDynamicAInputs = false;
		}
	} else {
		VDFAIL("Invalid dynamic token.");
	}
}

int ATPIAEmulator::AllocOutput(ATPIAOutputFn fn, void *ptr, uint32 changeMask) {
	const uint32 numOutputSlots = vdcountof(mIntOutputs) + (uint32)mExtOutputs.size();

	while(mOutputNextAllocIndex < numOutputSlots) {
		if (!GetOutput(mOutputNextAllocIndex).mChangeMask)
			break;

		++mOutputNextAllocIndex;
	}

	if (mOutputNextAllocIndex >= numOutputSlots)
		mExtOutputs.emplace_back(OutputEntry{});

	const uint32 outputSlot = mOutputNextAllocIndex;
	
	changeMask &= 0x3FFFF;

	OutputEntry& output = GetOutput(outputSlot);
	output.mChangeMask = changeMask | UINT32_C(0x80000000);
	output.mpFn = fn;
	output.mpData = ptr;

	mOutputReportMask |= changeMask;

	return (int)outputSlot;
}

void ATPIAEmulator::ModifyOutputMask(int index, uint32 changeMask) {
	if (index < 0)
		return;

	auto& output = GetOutput((unsigned)index);
	VDASSERT(output.mChangeMask);

	changeMask = (changeMask & 0x3FFFF) | UINT32_C(0x80000000);

	if (output.mChangeMask != changeMask) {
		output.mChangeMask = changeMask;

		RecomputeOutputReportMask();
	}
}

void ATPIAEmulator::FreeOutput(int index) {
	if (index >= 0) {
		if (mOutputNextAllocIndex > (unsigned)index)
			mOutputNextAllocIndex = (unsigned)index;

		GetOutput((unsigned)index).mChangeMask = 0;

		while(!mExtOutputs.empty() && !mExtOutputs.back().mChangeMask)
			mExtOutputs.pop_back();

		RecomputeOutputReportMask();
	}
}

void ATPIAEmulator::SetTraceContext(ATTraceContext *context) {
	if (mpTraceContext == context)
		return;

	mpTraceContext = context;

	if (context) {
		ATTraceGroup *group = context->mpCollection->AddGroup(L"PIA");

		mpTraceCRA = group->AddFormattedChannel(context->mBaseTime, context->mBaseTickScale, L"CRA");
		mpTraceCRB = group->AddFormattedChannel(context->mBaseTime, context->mBaseTickScale, L"CRB");
		mpTraceInputA = group->AddFormattedChannel(context->mBaseTime, context->mBaseTickScale, L"Input A");
	} else {
		const uint64 t = mpScheduler->GetTick64();

		if (mpTraceCRA) {
			mpTraceCRA->TruncateLastEvent(t);
			mpTraceCRA = nullptr;
		}

		if (mpTraceCRB) {
			mpTraceCRB->TruncateLastEvent(t);
			mpTraceCRB = nullptr;
		}

		if (mpTraceInputA) {
			mpTraceInputA->TruncateLastEvent(t);
			mpTraceInputA = nullptr;
		}
	}
}

void ATPIAEmulator::SetIRQHandler(vdfunction<void(uint32, bool)> fn) {
	mpIRQHandler = std::move(fn);
}

void ATPIAEmulator::Init(ATScheduler *scheduler) {
	mpScheduler = scheduler;
}

void ATPIAEmulator::ColdReset() {
	if (mpFloatingInputs)
		memset(mpFloatingInputs->mFloatTimers, 0, sizeof mpFloatingInputs->mFloatTimers);

	WarmReset();
}

void ATPIAEmulator::ResetFloatingInputTimers() {
	if (mpFloatingInputs)
		memset(mpFloatingInputs->mFloatTimers, 0, sizeof mpFloatingInputs->mFloatTimers);
}

void ATPIAEmulator::WarmReset() {
	// need to do this to float inputs
	SetPortBDirection(0);

	mPortOutput = kATPIAOutput_CA2 | kATPIAOutput_CB2;
	mPortDirection = kATPIAOutput_CA2 | kATPIAOutput_CB2;
	SetCRA(0);
	SetCRB(0);
	mbPIAEdgeA = false;
	mbPIAEdgeB = false;
	mPIACB2 = kPIACS_Floating;

	NegateIRQs(kATIRQSource_PIAA1 | kATIRQSource_PIAA2 | kATIRQSource_PIAB1 | kATIRQSource_PIAB2);

	UpdateOutput();
}

void ATPIAEmulator::SetCA1(bool level) {
	if (mbCA1 == level)
		return;

	mbCA1 = level;

	// check that the interrupt isn't already active
	if (mPORTACTL & 0x80)
		return;

	// check if we have the correct transition
	if (mPORTACTL & 0x02) {
		if (!level)
			return;
	} else {
		if (level)
			return;
	}

	// set interrupt flag
	SetCRA(mPORTACTL | 0x80);

	// assert IRQ if enabled
	if ((mPORTACTL & 0x01) && mpIRQHandler)
		AssertIRQs(kATIRQSource_PIAA1);
}

void ATPIAEmulator::SetCA2(bool level) {
	if (mbCA2 == level)
		return;

	mbCA2 = level;

	// check that the interrupt isn't already active and that input mode is
	// enabled
	if (mPORTACTL & 0x60)
		return;

	// check if we have the correct transition
	if (mPORTACTL & 0x10) {
		if (!level)
			return;
	} else {
		if (level)
			return;
	}

	// set interrupt flag
	SetCRA(mPORTACTL | 0x40);

	// assert IRQ if enabled
	if ((mPORTACTL & 0x08) && mpIRQHandler)
		AssertIRQs(kATIRQSource_PIAA2);
}

void ATPIAEmulator::SetCB1(bool level) {
	if (mbCB1 == level)
		return;

	mbCB1 = level;

	// check that the interrupt isn't already active
	if (mPORTBCTL & 0x80)
		return;

	// check if we have the correct transition
	if (mPORTBCTL & 0x02) {
		if (!level)
			return;
	} else {
		if (level)
			return;
	}

	// set interrupt flag
	SetCRB(mPORTBCTL | 0x80);

	// assert IRQ if enabled
	if ((mPORTBCTL & 0x01) && mpIRQHandler)
		AssertIRQs(kATIRQSource_PIAB1);
}

uint8 ATPIAEmulator::DebugReadByte(uint8 addr) const {
	switch(addr & 0x03) {
	case 0x00:
	default:
		// Port A reads the actual state of the output lines.
		return mPORTACTL & 0x04
			? (uint8)((mOutput | ~mPortDirection) & ComputePortAInput())
			: (uint8)mPortDirection;

	case 0x01:
		// return DDRB if selected
		if (!(mPORTBCTL & 0x04))
			return (uint8)(mPortDirection >> 8);

		// Port B reads output bits instead of input bits for those selected as output. No ANDing with input.
		{
			uint8 pb = ComputePortBInput();

			pb = (uint8)(((pb ^ (mOutput >> 8)) & (mPortDirection >> 8)) ^ pb);

			// If we have floating bits, roll them in.
			if (mpFloatingInputs) {
				const uint8 visibleFloatingBits = mpFloatingInputs->mFloatingInputMask & ~(mPortDirection >> 8);
				
				if (visibleFloatingBits) {
					const uint64 t64 = mpFloatingInputs->mpScheduler->GetTick64();
					uint8 bit = 0x01;

					for(int i=0; i<8; ++i, (bit += bit)) {
						if (visibleFloatingBits & bit) {
							// Turn off this bit if we've passed the droop deadline.
							if (t64 >= mpFloatingInputs->mFloatTimers[i])
								pb &= ~bit;
						}
					}
				}
			}

			return pb;
		}

	case 0x02:
		return mPORTACTL;

	case 0x03:
		return mPORTBCTL;
	}
}

uint8 ATPIAEmulator::ReadByte(uint8 addr) {
	switch(addr & 0x03) {
	case 0x00:
		if (mPORTACTL & 0x04) {
			// Reading the PIA port A data register negates port A interrupts.
			SetCRA(mPORTACTL & 0x3F);

			NegateIRQs(kATIRQSource_PIAA1 | kATIRQSource_PIAA2);
		}
		break;

	case 0x01:
		if (mPORTBCTL & 0x04) {
			// Reading the PIA port A data register negates port B interrupts.
			SetCRB(mPORTBCTL & 0x3F);

			NegateIRQs(kATIRQSource_PIAB1 | kATIRQSource_PIAB2);
		}

		break;

	case 0x02:
		return mPORTACTL;
	case 0x03:
		return mPORTBCTL;
	}

	return DebugReadByte(addr);
}

void ATPIAEmulator::WriteByte(uint8 addr, uint8 value) {
	switch(addr & 0x03) {
	case 0x00:
		{
			uint32 delta;
			if (mPORTACTL & 0x04) {
				delta = (mPortOutput ^ value) & 0xff;
				if (!delta)
					return;

				mPortOutput ^= delta;
			} else {
				delta = (mPortDirection ^ value) & 0xff;

				if (!delta)
					return;

				mPortDirection ^= delta;
			}

			UpdateOutput();
		}
		break;
	case 0x01:
		{
			uint32 delta;
			if (mPORTBCTL & 0x04) {
				delta = (mPortOutput ^ ((uint32)value << 8)) & 0xff00;
				if (!delta)
					return;

				mPortOutput ^= delta;
			} else {
				if (!SetPortBDirection(value))
					return;
			}

			UpdateOutput();
		}
		break;
	case 0x02:
		switch(value & 0x38) {
			case 0x00:
			case 0x08:
			case 0x28:
				mbPIAEdgeA = false;
				break;

			case 0x10:
			case 0x18:
				if (mbPIAEdgeA) {
					mbPIAEdgeA = false;
					SetCRA(mPORTACTL | 0x40);
				}
				break;

			case 0x20:
			case 0x38:
				break;

			case 0x30:
				mbPIAEdgeA = true;
				break;
		}

		{
			uint8 cra = mPORTACTL;

			if (value & 0x20)
				cra &= ~0x40;

			SetCRA((cra & 0xc0) + (value & 0x3f));
		}

		if (mpIRQHandler) {
			if ((mPORTACTL & 0x68) == 0x48)
				AssertIRQs(kATIRQSource_PIAA2);
			else
				NegateIRQs(kATIRQSource_PIAA2);

			if ((mPORTACTL & 0x81) == 0x81)
				AssertIRQs(kATIRQSource_PIAA1);
			else
				NegateIRQs(kATIRQSource_PIAA1);
		}

		UpdateCA2();
		break;
	case 0x03:
		switch(value & 0x38) {
			case 0x00:
			case 0x08:
			case 0x10:
			case 0x18:
				if (mbPIAEdgeB) {
					mbPIAEdgeB = false;
					SetCRB(mPORTBCTL | 0x40);
				}

				mPIACB2 = kPIACS_Floating;
				break;

			case 0x20:
				mbPIAEdgeB = false;
				break;

			case 0x28:
				mbPIAEdgeB = false;
				mPIACB2 = kPIACS_High;
				break;

			case 0x30:
				mbPIAEdgeB = false;
				mPIACB2 = kPIACS_Low;
				break;

			case 0x38:
				if (mPIACB2 == kPIACS_Low)
					mbPIAEdgeB = true;

				mPIACB2 = kPIACS_High;
				break;
		}

		{
			uint8 crb = mPORTBCTL;

			if (value & 0x20)
				crb &= ~0x40;

			SetCRB((crb & 0xc0) + (value & 0x3f));
		}

		if (mpIRQHandler) {
			if ((mPORTBCTL & 0x68) == 0x48)
				AssertIRQs(kATIRQSource_PIAB2);
			else
				NegateIRQs(kATIRQSource_PIAB2);

			if ((mPORTBCTL & 0x81) == 0x81)
				AssertIRQs(kATIRQSource_PIAB1);
			else
				NegateIRQs(kATIRQSource_PIAB1);
		}

		UpdateCB2();
		break;
	}
}

void ATPIAEmulator::SetPortBFloatingInputs(ATPIAFloatingInputs *inputs) {
	mpFloatingInputs = inputs;

	if (inputs)
		memset(inputs->mFloatTimers, 0, sizeof inputs->mFloatTimers);
}

void ATPIAEmulator::GetState(ATPIAState& state) const {
	state.mCRA = mPORTACTL;
	state.mCRB = mPORTBCTL;
	state.mDDRA = (uint8)mPortDirection;
	state.mDDRB = (uint8)(mPortDirection >> 8);
	state.mORA = (uint8)mPortOutput;
	state.mORB = (uint8)(mPortOutput >> 8);
}

void ATPIAEmulator::DumpState() {
	ATPIAState state;
	GetState(state);

	static const char *const kCAB2Modes[] = {
		"-edge sense",
		"-edge sense w/IRQ",
		"+edge sense",
		"+edge sense w/IRQ",
		"read handshake w/CA1",
		"read pulse",
		"low / on",
		"high / off",
	};

	ATConsolePrintf("Port A control:   %02X (%s, motor line: %s, proceed line: %cedge%s)\n"
		, state.mCRA
		, state.mCRA & 0x04 ? "IOR" : "DDR"
		, kCAB2Modes[(state.mCRA >> 3) & 7]
		, state.mCRA & 0x02 ? '+' : '-'
		, state.mCRA & 0x01 ? " w/IRQ" : ""
		);
	ATConsolePrintf("Port A direction: %02X%s\n"
		, state.mDDRA
		, state.mDDRA == 0 ? " (all inputs)" : state.mDDRA == 0xFF ? " (all outputs)" : ""
	);
	ATConsolePrintf("Port A output:    %02X\n", state.mORA);
	ATConsolePrintf("Port A input:     %02X\n", ComputePortAInput());
	ATConsolePrintf("Port A edge:      %s\n", mbPIAEdgeA ? "pending" : "none");

	ATConsolePrintf("Port B control:   %02X (%s, command line: %s, interrupt line: %cedge%s)\n"
		, state.mCRB
		, state.mCRB & 0x04 ? "IOR" : "DDR"
		, kCAB2Modes[(state.mCRB >> 3) & 7]
		, state.mCRB & 0x02 ? '+' : '-'
		, state.mCRB & 0x01 ? " w/IRQ" : ""
		);
	ATConsolePrintf("Port B direction: %02X%s\n"
		, state.mDDRB
		, state.mDDRB == 0 ? " (all inputs)" : state.mDDRB == 0xFF ? " (all outputs)" : ""
	);
	ATConsolePrintf("Port B output:    %02X\n", state.mORB);
	ATConsolePrintf("Port B input:     %02X\n", ComputePortBInput());

	if (mpFloatingInputs && mpFloatingInputs->mFloatingInputMask)
		ATConsolePrintf("Port B floating:  %02X\n", mpFloatingInputs->mFloatingInputMask & ~state.mDDRB);

	ATConsolePrintf("Port B edge:      %s\n", mbPIAEdgeB ? "pending" : "none");
}

void ATPIAEmulator::BeginLoadState(ATSaveStateReader& reader) {
	reader.RegisterHandlerMethod(kATSaveStateSection_Arch, VDMAKEFOURCC('P', 'I', 'A', ' '), this, &ATPIAEmulator::LoadStateArch);
	reader.RegisterHandlerMethod(kATSaveStateSection_End, 0, this, &ATPIAEmulator::EndLoadState);
}

void ATPIAEmulator::LoadStateArch(ATSaveStateReader& reader) {
	const uint8 ora = reader.ReadUint8();
	const uint8 orb = reader.ReadUint8();
	const uint8 ddra = reader.ReadUint8();
	const uint8 ddrb = reader.ReadUint8();

	mPortOutput = ((uint32)orb << 8) + ora;
	mPortDirection = ((uint32)ddrb << 8) + ddra + kATPIAOutput_CA2 + kATPIAOutput_CB2;
	mPORTACTL = reader.ReadUint8();
	mPORTBCTL = reader.ReadUint8();
}

void ATPIAEmulator::EndLoadState(ATSaveStateReader& reader) {
	PostLoadState();
}

class ATSaveStatePia final : public ATSnapExchangeObject<ATSaveStatePia, "ATSaveStatePia"> {
public:
	template<typename T>
	void Exchange(T& rw) {
		rw.Transfer("ora", &mState.mORA);
		rw.Transfer("orb", &mState.mORB);
		rw.Transfer("ddra", &mState.mDDRA);
		rw.Transfer("ddrb", &mState.mDDRB);
		rw.Transfer("cra", &mState.mCRA);
		rw.Transfer("crb", &mState.mCRB);
	}

	ATPIAState mState;
};

void ATPIAEmulator::SaveState(IATObjectState **pp) const {
	vdrefptr<ATSaveStatePia> obj(new ATSaveStatePia);

	GetState(obj->mState);

	*pp = obj.release();
}

void ATPIAEmulator::LoadState(const IATObjectState *state) {
	if (state)
		LoadState(atser_cast<const ATSaveStatePia&>(*state));
	else
		LoadState(ATSaveStatePia{});
}

void ATPIAEmulator::LoadState(const ATSaveStatePia& piastate) {
	mPortOutput = ((uint32)piastate.mState.mORB << 8) + piastate.mState.mORA;
	mPortDirection = ((uint32)piastate.mState.mDDRB << 8) + piastate.mState.mDDRA + kATPIAOutput_CA2 + kATPIAOutput_CB2;
	mPORTACTL = piastate.mState.mCRA;
	mPORTBCTL = piastate.mState.mCRB;
}

void ATPIAEmulator::PostLoadState() {
	UpdateCA2();
	UpdateCB2();
	UpdateOutput();

	if (mpFloatingInputs)
		memset(mpFloatingInputs->mFloatTimers, 0, sizeof mpFloatingInputs->mFloatTimers);
}

uint32& ATPIAEmulator::GetInput(unsigned slot) {
	return slot < vdcountof(mIntInputs) ? mIntInputs[slot] : mpExtInputs[slot - vdcountof(mIntInputs)];
}

ATPIAEmulator::OutputEntry& ATPIAEmulator::GetOutput(unsigned slot) {
	return slot < vdcountof(mIntOutputs) ? mIntOutputs[slot] : mExtOutputs[slot - vdcountof(mIntOutputs)];
}

void ATPIAEmulator::UpdateCA2() {
	// bits 3-5:
	//	0xx		input (passively pulled high)
	//	100		output - set high by interrupt and low by port A data read
	//	101		output - normally set high, pulse low for one cycle by port A data read
	//	110		output - low
	//	111		output - high
	//
	// Right now we don't emulate the pulse modes.

	if ((mPORTACTL & 0x38) == 0x30)
		mPortOutput &= ~kATPIAOutput_CA2;
	else
		mPortOutput |= kATPIAOutput_CA2;

	UpdateOutput();
}

void ATPIAEmulator::UpdateCB2() {
	// bits 3-5:
	//	0xx		input (passively pulled high)
	//	100		output - set high by interrupt and low by port B data read
	//	101		output - normally set high, pulse low for one cycle by port B data read
	//	110		output - low
	//	111		output - high
	//
	// Right now we don't emulate the pulse modes.

	if ((mPORTBCTL & 0x38) == 0x30)
		mPortOutput &= ~kATPIAOutput_CB2;
	else
		mPortOutput |= kATPIAOutput_CB2;

	UpdateOutput();
}

void ATPIAEmulator::UpdateOutput() {
	const uint32 newOutput = mPortOutput | ~mPortDirection;
	const uint32 delta = mOutput ^ newOutput;

	if (!delta)
		return;

	mOutput = newOutput;

	if (delta & mOutputReportMask) {
		for(const OutputEntry& output : mIntOutputs) {
			if (output.mChangeMask & delta)
				output.mpFn(output.mpData, mOutput);
		}

		for(const OutputEntry& output : mExtOutputs) {
			if (output.mChangeMask & delta)
				output.mpFn(output.mpData, mOutput);
		}
	}
}

void ATPIAEmulator::RecomputeOutputReportMask() {
	mOutputReportMask = 0;

	for(const auto& output : mIntOutputs)
		mOutputReportMask |= output.mChangeMask;

	for(const auto& output : mExtOutputs)
		mOutputReportMask |= output.mChangeMask;

	mOutputReportMask &= 0x3FFFF;
}

bool ATPIAEmulator::SetPortBDirection(uint8 value) {
	const uint32 delta = (mPortDirection ^ ((uint32)value << 8)) & 0xff00;

	if (!delta)
		return false;

	mPortDirection ^= delta;

	// Check if any bits that have transitioned from output (1) to input (0) correspond
	// to floating inputs. If so, we need to update the floating timers.
	if (mpFloatingInputs) {
		const uint8 newlyFloatingInputs = mpFloatingInputs->mFloatingInputMask & (uint8)((delta & ~mPortDirection) >> 8);

		if (newlyFloatingInputs) {
			const uint64 t64 = mpFloatingInputs->mpScheduler->GetTick64();
			const uint8 outputs = (uint8)(mPortOutput >> 8);
			uint8 bit = 0x01;

			// if we have any bits that are transitioning 1 -> floating, update the PRNG
			if (newlyFloatingInputs & outputs) {
				// Wallclock-entropy mix: XORs the absolute scheduler
				// tick into the LFSR seed.  Suppressed during netplay
				// (mbDeterministicSeedMix=true) — see comment in
				// ATPIAFloatingInputs (pia.h).  The LFSR step below
				// still evolves the seed, so the RNG remains usable
				// for in-session floating-input randomization while
				// being reproducible across both peers from the
				// shared netplay locked seed.
				if (!mpFloatingInputs->mbDeterministicSeedMix) {
					mpFloatingInputs->mRandomSeed ^= (uint32)t64;
					mpFloatingInputs->mRandomSeed &= 0x7FFFFFFFU;

					if (!mpFloatingInputs->mRandomSeed)
						mpFloatingInputs->mRandomSeed = 1;
				}
			}

			for(int i=0; i<8; ++i, (bit += bit)) {
				if (newlyFloatingInputs & bit) {
					// Floating bits slowly drift toward 0. Therefore, we need to check whether
					// the output was a 0. If it is, reset the timer to 0 so it stays 0; otherwise,
					// compute a pseudorandom timeout value.
					if (outputs & bit) {
						// pull 16 bits out of 31-bit LFSR
						uint32 rval = mpFloatingInputs->mRandomSeed & 0xffff;
						mpFloatingInputs->mRandomSeed = (rval << 12) ^ (rval << 15) ^ (mpFloatingInputs->mRandomSeed >> 16);

						mpFloatingInputs->mFloatTimers[i] = t64 + mpFloatingInputs->mDecayTimeMin + (uint32)(((uint64)mpFloatingInputs->mDecayTimeRange * rval) >> 16);
					} else {
						mpFloatingInputs->mFloatTimers[i] = 0;
					}
				}
			}
		}
	}

	return true;
}

void ATPIAEmulator::NegateIRQs(uint32 mask) {
	if (!mpIRQHandler)
		return;

	mpIRQHandler(mask, false);
}

void ATPIAEmulator::AssertIRQs(uint32 mask) {
	if (!mpIRQHandler)
		return;

	mpIRQHandler(mask, true);
}

void ATPIAEmulator::SetCRA(uint8 v) {
	if (mPORTACTL == v)
		return;

	mPORTACTL = v;

	UpdateTraceCRA();
}

void ATPIAEmulator::SetCRB(uint8 v) {
	if (mPORTBCTL == v)
		return;

	mPORTBCTL = v;

	UpdateTraceCRB();
}

void ATPIAEmulator::UpdateTraceCRA() {
	if (!mpTraceCRA)
		return;

	const uint64 t = mpScheduler->GetTick64();
	mpTraceCRA->TruncateLastEvent(t);
	mpTraceCRA->AddOpenTickEventF(t, kATTraceColor_Default, L"%02X", mPORTACTL);
}

void ATPIAEmulator::UpdateTraceCRB() {
	if (!mpTraceCRB)
		return;

	const uint64 t = mpScheduler->GetTick64();
	mpTraceCRB->TruncateLastEvent(t);
	mpTraceCRB->AddOpenTickEventF(t, kATTraceColor_Default, L"%02X", mPORTBCTL);
}

void ATPIAEmulator::UpdateTraceInputA() {
	if (!mpTraceInputA)
		return;

	const uint64 t = mpScheduler->GetTick64();
	mpTraceInputA->TruncateLastEvent(t);
	mpTraceInputA->AddOpenTickEventF(t, kATTraceColor_Default, L"%02X", mInput & 0xFF);
}

uint8 ATPIAEmulator::ComputePortAInput() const {
	uint8 pa = (uint8)mInput;

	if (mbHasDynamicAInputs)
		pa &= ReadDynamicInputs(false);

	return pa;
}

uint8 ATPIAEmulator::ComputePortBInput() const {
	uint8 pb = (uint8)(mInput >> 8);

	if (mbHasDynamicBInputs)
		pb &= ReadDynamicInputs(true);

	return pb;
}

uint8 ATPIAEmulator::ReadDynamicInputs(bool portb) const {
	const auto& fns = mDynamicInputs[portb];

	uint8 v = 0xFF;

	for (const auto& fn : fns)
		v &= fn();

	return v;
}

#ifdef AT_TESTS_ENABLED
	AT_DEFINE_TEST(Emu_PIA) {
		ATPIAEmulator::CountedMask32 cm;

		AT_TEST_ASSERT(cm.ReadZeroMask() == 0xFFFFFFFF);
		for(int i=0; i<32; ++i) {
			uint32 mask = 1 << i;
			cm.AddZeroBits(~mask);

			uint32 r = cm.ReadZeroMask();
			AT_TEST_ASSERTF(r == ~mask, "Failed with mask %08X -> %08X", mask, ~r);

			cm.RemoveZeroBits(~mask);
			r = cm.ReadZeroMask();
			AT_TEST_ASSERTF(r == UINT32_C(0xFFFFFFFF), "Failed with mask %08X -> %08X", mask, ~r);
		}

		cm.AddZeroBits(0xFFFFFEFE);
		cm.AddZeroBits(0xFFFFFFFE);
		AT_TEST_ASSERT(cm.ReadZeroMask() == 0xFFFFFEFE);
		cm.RemoveZeroBits(0xFFFFFEFE);
		AT_TEST_ASSERT(cm.ReadZeroMask() == 0xFFFFFFFE);

		return 0;
	}
#endif
