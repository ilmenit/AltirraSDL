//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2020 Avery Lee
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
#include <ratio>
#include <chrono>
#include "disk.h"
#include "diskprofile.h"

namespace {
	// Cycles/second. This is only correct for NTSC, but it's close enough
	// for PAL for disk emulation purposes.
	constexpr int kCyclesPerSecond = 7159090/4;
	constexpr double kCyclesPerSecondD = 7159090.0/4.0;

	// Cycles for a fake rotation as seen by the FDC. Neither the 810 nor the 1050
	// use the real index pulse; they fake it with the RIOT.
	//
	// 810: ~522ms
	// 1050: ~236ms
	//
	const int kCyclesPerFakeRot_810			= (kCyclesPerSecond * 522 + 5000) / 10000;
	const int kCyclesPerFakeRot_1050		= (kCyclesPerSecond * 236 + 5000) / 10000;

	///////////////////////////////////////////////////////////////////////////////////
	// SIO timing parameters
	//
	// WARNING: KARATEKA IS VERY SENSITIVE TO THESE PARAMETERS AS IT HAS STUPIDLY
	//			CLOSE PHANTOM SECTORS.
	//
	// Actual 1050 numbers (to about 0.13ms, or VCOUNT precision):
	//	- End of checksum byte to end of ACK byte: ~0.76ms
	//	- End of complete byte to start of data byte: ~0.5-0.63ms
	//	- Start of data byte to end of checksum byte: ~70.9ms
	//

	// The number of cycles per byte sent over the SIO bus -- approximately 19200 baud.
	//
	// 810: 26 cycles/bit, 265 cycles/byte @ 500KHz
	// 1050: 51 cycles/bit, 549 cycles/byte @ 1MHz
	// US Doubler low speed: 53 cycles/bit, 534 cycles/byte @ 1MHz
	// US Doubler high speed: 19 cycles/bit, 220 cycles/byte @ 1MHz
	// Speedy 1050 low speed: 52 cycles/bit, 525 cycles/byte @ 1MHz
	// Speedy 1050 high speed: 18 cycles/bit, 214 cycles/byte @ 1MHz
	// XF551 low speed: 29 cycles/bit, 290 cycles/byte @ 555.5KHz
	// XF551 high speed: 14 cycles/bit, 140 cycles/byte @ 555.5KHz
	//
	// IndusGT numbers courtesy of sup8pdct (http://atariage.com/forums/topic/131515-indus-gt-rom-disassembly-pt-2/):
	//	
	//	208 t states /19200 $28 pokey divisor 18866-pal
	//	104 t states 1st version of syncromesh /38400 $10 pokey divisor 38553-pal 38847-ntsc
	//	76 t states usd version of rom /52631 $A pokey divisor 52160-pal 52558-ntsc
	//	58 t states 2nd version of syncromesh /68965 $6 pokey divisor 68209-pal 68730-ntsc
	//
	static const int kCyclesPerSIOByte_810 = 949;
	static const int kCyclesPerSIOByte_1050 = 984;			// calibrated against real 1050 -- ~70.9ms/129b
	static const int kCyclesPerSIOByte_PokeyDiv0 = 140;
	static const int kCyclesPerSIOByte_57600baud = 311;
	static const int kCyclesPerSIOByte_USDoubler = 956;
	static const int kCyclesPerSIOByte_USDoubler_Fast = 394;
	static const int kCyclesPerSIOByte_Speedy1050 = 940;
	static const int kCyclesPerSIOByte_Speedy1050_Fast = 383;
	static const int kCyclesPerSIOByte_XF551 = 934;
	static const int kCyclesPerSIOByte_XF551_Fast = 450;
	static const int kCyclesPerSIOByte_Happy = 967;						// 540 cycles/byte @ 1MHz
	static const int kCyclesPerSIOByte_Happy_Native_Fast = 564;			// 315 cycles/byte @ 1MHz
	static const int kCyclesPerSIOByte_Happy_USD_Fast = 394;
	static const int kCyclesPerSIOByte_1050Turbo = 982;
	static const int kCyclesPerSIOByte_1050Turbo_Fast = 260;
	static const int kCyclesPerSIOBit_810 = 94;
	static const int kCyclesPerSIOBit_1050 = 91;
	static const int kCyclesPerSIOBit_PokeyDiv0 = 14;
	static const int kCyclesPerSIOBit_57600baud = 31;
	static const int kCyclesPerSIOBit_USDoubler = 95;
	static const int kCyclesPerSIOBit_USDoubler_Fast = 34;
	static const int kCyclesPerSIOBit_Speedy1050 = 93;
	static const int kCyclesPerSIOBit_Speedy1050_Fast = 32;
	static const int kCyclesPerSIOBit_XF551 = 93;
	static const int kCyclesPerSIOBit_XF551_Fast = 45;

	static const int kCyclesPerSIOBit_IndusGT = 94;						// 19138.8 baud (209 T-states/bit)
	static const int kCyclesPerSIOBit_IndusGT_OrigSynchromesh = 47;		// 38461.5 baud (guess)
	static const int kCyclesPerSIOBit_IndusGT_Synchromesh = 47;			// 38461.5 baud (104 T-states/bit)
	static const int kCyclesPerSIOBit_IndusGT_SuperSynchromesh = 26;	// 68965.5 baud

	static const int kCyclesPerSIOByte_IndusGT = 930;
	static const int kCyclesPerSIOByte_IndusGT_OrigSynchromesh = 520;	// 3439.4 bytes/sec (guess)
	static const int kCyclesPerSIOByte_IndusGT_Synchromesh = 520;		// 3439.4 bytes/sec (1163 T-states/byte)
	static const int kCyclesPerSIOByte_IndusGT_SuperSynchromesh = 268;	// 6689.0 bytes/sec (598 T-states/byte)

	static const int kCyclesPerSIOBit_Happy = 95;
	static const int kCyclesPerSIOBit_Happy_Native_Fast = 47;		// Native high speed (26 cycles/bit @ 1MHz)
	static const int kCyclesPerSIOBit_Happy_USD_Fast = 34;			// USDoubler emulation

	static const int kCyclesPerSIOBit_1050Turbo = 91;
	static const int kCyclesPerSIOBit_1050Turbo_Fast = 26;

	// Delay from end of ACK byte until FDC command is sent.
	// 810: ~1608 cycles @ 500KHz = ~5756 cycles @ 1.79MHz.
	static const int kCyclesFDCCommandDelay = 5756;

	// Rotational delay in cycles when accurate sector timing is disabled.
	static const int kCyclesRotationalDelay_Fast = 2000;

	// Delay from end of Complete byte to end of first data byte, at high speed.
	static const int kCyclesToFirstDataHighSpeed = 945;

	// Time from end of sector read to start of Complete byte (FDC reset and checksum):
	//	810: ~2568 cycles @ 500KHz = 9192 cycles
	//	1050: ~270 cycles @ 1MHz = 483 cycles
	//
	static constexpr uint32 kCyclesPostReadDelay_Fast = 1000;
	static constexpr uint32 kCyclesPostReadDelay_810 = 9192;
	static constexpr uint32 kCyclesPostReadDelay_1050 = 483;
}

constexpr ATDiskProfile::ATDiskProfile(ATDiskEmulationMode mode, ATDiskEmulationModeVariant variant) {
	// We need a small delay here between the end of the complete byte and
	// the start of the data frame byte at high speed. For the US Doubler,
	// this is 74 cycles @ 1MHz, or 132 machine cycles. For an Indus GT,
	// Synchromesh has no such delay, but SuperSynchromesh takes around
	// 5500/9724 T-cycles @ 4MHz to compute checksums between C/E and
	// the data frame. The XF551 does not use a delay.
	mCyclesCEToDataFrame = 0;
	mCyclesCEToDataFramePBDiv256 = 0;
	mCyclesCEToDataFrameHighSpeedXF = 0;
	mCyclesCEToDataFrameHighSpeedXFPBDiv256 = 0;
	mCyclesCEToDataFrameHighSpeedUSD = 0;
	mCyclesCEToDataFrameHighSpeedUSDPBDiv256 = 0;

	const auto usToCycles = [](uint32 us) constexpr { return (us * 7159090 + 2000000) / 4000000; };

	int columnIndex;

	switch(mode) {
		case kATDiskEmulationMode_Generic:			columnIndex = 0; break;
		case kATDiskEmulationMode_Generic57600:		columnIndex = 1; break;
		case kATDiskEmulationMode_FastestPossible:	columnIndex = 2; break;
		case kATDiskEmulationMode_810:				columnIndex = 3; break;
		case kATDiskEmulationMode_Happy810:			columnIndex = 4; break;
		case kATDiskEmulationMode_1050:				columnIndex = 5; break;
		case kATDiskEmulationMode_USDoubler:		columnIndex = 6; break;
		case kATDiskEmulationMode_Speedy1050:		columnIndex = 7; break;
		case kATDiskEmulationMode_Happy1050:		columnIndex = 8; break;
		case kATDiskEmulationMode_1050Turbo:		columnIndex = 9; break;
		case kATDiskEmulationMode_XF551:			columnIndex = 10; break;
		case kATDiskEmulationMode_IndusGT:			columnIndex = 11; break;
		default: throw;
	}

	const auto setVal = [columnIndex](auto& cap, auto... caps) {
		using ValueType = std::remove_cvref_t<decltype(cap)>;
		const ValueType capsTable[] { ValueType(caps)... };
		static_assert(vdcountof(capsTable) == 12);

		cap = capsTable[columnIndex];
	};

	using MachineCycle = std::chrono::duration<double, std::ratio<4, 7159090>>;

	const auto setCyc = [columnIndex](uint32& cyc, auto... timings) {
		const MachineCycle mcTimings[] { std::chrono::duration_cast<MachineCycle>(timings)... };
		static_assert(vdcountof(mcTimings) == 12);

		cyc = (uint32)(0.5 + mcTimings[columnIndex].count());
	};

	const auto setCycF = [columnIndex](float& cyc, auto... timings) {
		const MachineCycle mcTimings[] { std::chrono::duration_cast<MachineCycle>(timings)... };
		static_assert(vdcountof(mcTimings) == 12);

		cyc = (float)mcTimings[columnIndex].count();
	};

	using namespace std::chrono_literals;

	//											Generic		Gen57.6		Fastest		810			Happy810	1050		USDoubler	Speedy1050	Happy1050	1050Turbo	XF551		IndusGT
	setVal(mbSupportedNotReady,					true,		true,		true,		false,		false,		true,		true,		true,		true,		true,		false,		true		);
	setVal(mbSupportedCmdHighSpeedXF,			true,		true,		true,		false,		false,		false,		false,		false,		false,		false,		true,		true		);
	setVal(mbSupportedCmdHighSpeedUSD,			false,		true,		true,		false,		false,		false,		true,		true,		true,		false,		false,		false		);
	setVal(mbSupportedCmdPERCOM,				true,		true,		true,		false,		false,		false,		true,		true,		true,		true,		true,		true		);
	setVal(mbSupportedCmdFormatSkewed,			true,		true,		true,		false,		false,		false,		true,		false,		false,		false,		false,		false		);
	setVal(mbSupportedCmdFormatBoot,			false,		false,		false,		false,		false,		false,		false,		false,		false,		false,		false,		false		);
	setVal(mbSupportedCmdGetHighSpeedIndex,		false,		true,		true,		false,		false,		false,		true,		true,		true,		false,		false,		false		);
	setVal(mHighSpeedIndexXF,					16,			16,			16,			0,			0,			0,			0,			0,			0,			0,			16,			0			);
	setVal(mHighSpeedIndexUSD,					0,			8,			0,			0,			0,			0,			10,			9,			10,			6,			0,			0			);

	setCyc(mCyclesPerSIOByte,					530us,		530us,		530us,		530us,		530us,		549us,		534us,		525us,		524us,		520us,		522us,		559.3us		);
	setCyc(mCyclesPerSIOBit,					52us,		52us,		52us,		52us,		52us,		51us,		53us,		52us,		51us,		52us,		52.2us,		52.3us		);

	setCyc(mCyclesPerSIOByteHighSpeedXF,		252us,		252us,		252us,		0us,		0us,		0us,		0us,		0us,		0us,		151us,		252us,		290.8us		);
	setCyc(mCyclesPerSIOByteHighSpeedUSD,		0us,		173.6us,	78.2us,		0us,		220us,		0us,		220us,		214us,		220us,		151us,		0us,		290.8us		);

	float	cyclesPerSIOBitHighSpeedXFF = 1;
	float	cyclesPerSIOBitHighSpeedUSDF = 1;
	setCycF(cyclesPerSIOBitHighSpeedXFF,		25.2us,		25.2us,		25.2us,		0us,		0us,		0us,		0us,		0us,		0us,		0us,		25.2us,		26us		);
	setCycF(cyclesPerSIOBitHighSpeedUSDF,		19us,		17.4us,		7.8us,		0us,		19us,		0us,		19us,		18us,		19us,		14us,		0us,		0us			);

	// 810: 5.3ms step rate.
	// 1050: 20.12ms step rate (two half steps at ~18012 cycles, +/- 1 scanline)
	// Indus GT: 20ms step rate.
	// XF551: 6ms step rate.
	setCyc(mCyclesPerTrackStep,					5.3ms,		5.3ms,		3.0ms,		5.3ms,		5.3ms,		20.12ms,	20.12ms,	8.6ms,		20.12ms,	20.12ms,	6ms,		20ms		);

	int rpm;
	setVal(rpm,									288,		288,		288,		288,		288,		288,		288,		288,		288,		288,		300,		288			);

	mCyclesPerDiskRotation = (uint32)(0.5 + MachineCycle(60.0s / (double)rpm).count());

	setCyc(mCyclesForHeadSettle,				10ms,		10ms,		10ms,		10ms,		10ms,		20ms,		20ms,		20ms,		20ms,		20ms,		20ms,		20ms		);

	// Time for the motor to shut off while idling in the main loop.
	//	810: guesstimate of ~3 seconds
	//	1050: 6.8M cycles @ 1MHz (~6.8s)
	setCyc(mCyclesToMotorOff,					3.06s,		3.06s,		3.06s,		3.06s,		3.06s,		6.8s,		6.8s,		6.8s,		6.8s,		6.8s,		6.8s,		6.8s		);

	// for the baseline, use the 1050's status value and the US Doubler's
	// read PERCOM value
	setCyc(mCyclesACKStopBitToStatusComplete,	410us,		410us,		410us,		410us,		410us,		290us,		250us,		440us,		510us,		790us,		1110us,		650us		);
	setCyc(mCyclesACKStopBitToReadPERCOMComplete,440us,		440us,		440us,		440us,		440us,		440us,		440us,		400us,		620us,		890us,		1160us,		630us		);

	// Delay from command line deasserting to end of ACK byte.
	//
	// 810: ~294 cycles @ 500KHz = ~1053 cycles @ 1.79MHz.
	setCyc(mCyclesToACKSent,					588us,		588us,		588us,		588us,		588us,		279us,		279us,		279us,		279us,		279us,		279us,		279us		);

	// Delay from command line deasserting to end of leading edge of NAK byte.
	//
	// 810:
	//	~700us from stop bit of last command byte to leading edge
	//	~120us from -cmd to leading edge
	//
	// 1050:
	//	~237us from stop bit of last command byte to leading edge
	//	~184us from -cmd to leading edge
	//
	setCyc(mCyclesToNAKFromFrameEnd,			700us,		700us,		700us,		700us,		700us,		237us,		237us,		237us,		237us,		237us,		237us,		237us		);
	setCyc(mCyclesToNAKFromCmdDeassert,			120us,		120us,		120us,		120us,		120us,		184us,		184us,		184us,		184us,		184us,		184us,		184us		);

	//											Generic		Gen57.6		Fastest		810			Happy810	1050		USDoubler	Speedy1050	Happy1050	1050Turbo	XF551		IndusGT
	setCyc(mCyclesPostReadToCE,					120us,		120us,		120us,		5136us,		5136us,		270us,		270us,		270us,		270us,		270us,		270us,		270us		);

	// The 1050 Turbo V3.5 firmware takes 549 cycles @ 1MHz from leading edge of start bit of Complete byte
	// to leading edge of the first data byte. Subtract off the 140 cycles it takes to send the
	// C byte and we have a delay of 409 cycles @ 1MHz.
	//
	// XF551 executes a busy wait loop of 656 cycles @ 0.55MHz between end of C and start of data frame.
	setCyc(mCyclesCEToDataFrameHighSpeedXF,		1180us,		1180us,		1180us,		0us,		0us,		0us,		0us,		0us,		0us,		0us,		1180us,		0us			);
	setCyc(mCyclesCEToDataFrameHighSpeedUSD,	73.8us,		73.8us,		73.8us,		0us,		73.8us,		0us,		73.8us,		73.8us,		73.8us,		409us,		0us,		0us			);

	// The 1050 uses the RIOT to establish a ~260ms receive frame timeout. The others are guesses.
	setCyc(mCyclesToReceiveFrameTimeout,		0us,		0us,		0us,		0us,		0us,		260ms,		0us,		260ms,		260ms,		260ms,		0us,		0us			);

	setVal(mbSeekHalfTracks,					false,		false,		false,		false,		false,		true,		true,		true,		true,		true,		false,		true		);
	setVal(mbRetryMode1050,						false,		false,		false,		false,		false,		true,		true,		true,		true,		true,		true,		true		);
	setVal(mbReverseOnForwardSeeks,				false,		false,		false,		false,		false,		true,		true,		true,		true,		true,		true,		false		);
	setVal(mbWaitForLongSectors,				false,		false,		false,		false,		false,		true,		true,		true,		true,		true,		true,		true		);

	//											Generic		Gen57.6		Fastest		810			Happy810	1050		USDoubler	Speedy1050	Happy1050	1050Turbo	XF551		IndusGT
	setVal(mbWritePercomChangesDensity,			false,		false,		false,		false,		false,		false,		false,		true,		false,		true,		true,		true		);
	setVal(mbDetectDensityOnDiskChange,			true,		true,		true,		false,		false,		true,		true,		true,		true,		true,		false,		true		);

	switch(mode) {
		case kATDiskEmulationMode_Happy810:
		case kATDiskEmulationMode_Speedy1050:
			mbBufferTrackReads = true;
			break;

		case kATDiskEmulationMode_Happy1050:
			mbBufferTrackReads = true;
			mbBufferTrackReadErrors = true;
			mbBufferSector1 = true;
			break;

		case kATDiskEmulationMode_IndusGT:
			mbSupportCommandTruncation = true;
			break;

		case kATDiskEmulationMode_XF551:
			mbRequireCommandDeassertCheck = true;
			break;

		default:
			break;
	}

	switch(variant) {
		case ATDiskEmulationModeVariant::Default:
			break;

		case ATDiskEmulationModeVariant::IndusGTSynchromesh:
			mbSupportedCmdFormatBoot = true;
			mHighSpeedIndexXF = 10;
			mCyclesPerSIOByteHighSpeedXF = kCyclesPerSIOByte_IndusGT_Synchromesh;
			cyclesPerSIOBitHighSpeedXFF = kCyclesPerSIOBit_IndusGT_Synchromesh;
			break;

		case ATDiskEmulationModeVariant::IndusGTSuperSynchromesh:
			mbSupportedCmdFormatBoot = true;
			mHighSpeedIndexXF = 6;
			mCyclesPerSIOByteHighSpeedXF = kCyclesPerSIOByte_IndusGT_SuperSynchromesh;
			cyclesPerSIOBitHighSpeedXFF = kCyclesPerSIOBit_IndusGT_SuperSynchromesh;
			mCyclesCEToDataFrameHighSpeedXF = 571;
			mCyclesCEToDataFrameHighSpeedXFPBDiv256 = 3780;
			break;
	}

	mCyclesToFDCCommand = mCyclesToACKSent + kCyclesFDCCommandDelay;
	mCyclesPerSIOBitHighSpeedXF = (uint32)(0.5f + cyclesPerSIOBitHighSpeedXFF);
	mCyclesPerSIOBitHighSpeedUSD = (uint32)(0.5f + cyclesPerSIOBitHighSpeedUSDF);

	mHighSpeedUSDCmdFrameRateLo = 0;
	mHighSpeedUSDCmdFrameRateHi = 0;

	if (mbSupportedCmdHighSpeedUSD) {
		// Permitted rate divisors are within +/-5% of the actual bit transmission rate. Beyond that, POKEY
		// skews more than one half bit from the start to stop bit and won't receive reliably.
		int minDivisor = VDCeilToInt(cyclesPerSIOBitHighSpeedUSDF * 0.95f);
		int maxDivisor = VDFloorToInt(cyclesPerSIOBitHighSpeedUSDF * 1.05f);

		mHighSpeedUSDCmdFrameRateLo = minDivisor;
		mHighSpeedUSDCmdFrameRateHi = maxDivisor;

		VDASSERT((mHighSpeedIndexUSD*2 + 14) >= mHighSpeedUSDCmdFrameRateLo && (mHighSpeedIndexUSD * 2 + 14) <= mHighSpeedUSDCmdFrameRateHi);
	}
}

template<int... T_Indices>
const ATDiskProfile& ATGetDiskProfile(ATDiskEmulationMode mode, std::integer_sequence<int, T_Indices...>) {
	static constexpr ATDiskProfile sProfiles[] {
		ATDiskProfile(ATDiskEmulationMode(T_Indices))...
	};

	return sProfiles[mode];
}

const ATDiskProfile& ATGetDiskProfile(ATDiskEmulationMode mode) {
	return ATGetDiskProfile(mode, std::make_integer_sequence<int, kATDiskEmulationModeCount>());
}

const ATDiskProfile& ATGetDiskProfileIndusGTSynchromesh() {
	static constexpr ATDiskProfile sProfile(kATDiskEmulationMode_IndusGT, ATDiskEmulationModeVariant::IndusGTSynchromesh);

	return sProfile;
}

const ATDiskProfile& ATGetDiskProfileIndusGTSuperSynchromesh() {
	static constexpr ATDiskProfile sProfile(kATDiskEmulationMode_IndusGT, ATDiskEmulationModeVariant::IndusGTSuperSynchromesh);

	return sProfile;
}
