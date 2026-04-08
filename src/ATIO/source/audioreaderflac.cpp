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

#include <intrin.h>
#include <stdafx.h>
#include <vd2/system/binary.h>
#include <vd2/system/cpuaccel.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <at/atio/audioreaderflac.h>

#ifdef _MSC_VER
#pragma runtime_checks("", off)
#pragma check_stack(off)
#pragma optimize("gt", on)
#endif

///////////////////////////////////////////////////////////////////////////////

#if VD_CPU_X86 || VD_CPU_X64
	uint16 ATFLACUpdateCRC16_PCMUL(uint16 crc16, const void *buf, size_t n);
	void ATFLACReconstructLPC_Narrow_SSE2(sint32 *y, uint32 n, const sint32 *lpcCoeffs, int qlpShift, int order);
	void ATFLACReconstructLPC_Narrow_SSSE3(sint32 *y, uint32 n, const sint32 *lpcCoeffs, int qlpShift, int order);
	void ATFLACReconstructLPC_Medium_SSSE3(sint32 *y, uint32 n, const sint32 *lpcCoeffs, int qlpShift, int order);
#elif VD_CPU_ARM64
	void ATFLACReconstructLPC_Narrow_NEON(sint32 *y, uint32 n, const sint32 *lpcCoeffs, int qlpShift, int order);
	void ATFLACReconstructLPC_Medium_NEON(sint32 *y, uint32 n, const sint32 *lpcCoeffs, int qlpShift, int order);
	void ATFLACReconstructLPC_Wide_NEON(sint32 *y, uint32 n, const sint32 *lpcCoeffs, int qlpShift, int order);
	uint16 ATFLACUpdateCRC16_Crypto(uint16 crc16, const void *buf, size_t n);
#endif

///////////////////////////////////////////////////////////////////////////////

class ATFLACDecodeException final : public MyError {
public:
	using MyError::MyError;
};

///////////////////////////////////////////////////////////////////////////////

struct ATAudioReaderFLAC::CRC8Table {
	uint8 tab[256];

	constexpr CRC8Table() {
		tab[0] = 0;
		uint8 bitval = 0x07;

		for(int step = 1; step < 0x100; step *= 2) {
			for(int i = 0; i < step; ++i)
				tab[i + step] = tab[i] ^ bitval;

			bitval = (bitval << 1) ^ (bitval & 0x80 ? 0x07 : 0);
		}
	}
};

struct ATAudioReaderFLAC::CRC16Table {
	uint16 tab[256];	// swap16(i*x^8 mod P)

	constexpr CRC16Table() {
		tab[0] = 0;
		uint16 bitval = 0x8005;

		for(int step = 1; step < 0x100; step *= 2) {
			// byte swap the entire table so we can XOR at the bottom of the register
			// for update speed
			uint16 xormask = (bitval << 8) + (bitval >> 8);

			for(int i = 0; i < step; ++i)
				tab[i + step] = tab[i] ^ xormask;

			bitval = (bitval << 1) ^ (bitval & 0x8000 ? 0x8005 : 0);
		}
	}
};

struct ATAudioReaderFLAC::CRC16x2Table {
	uint16 tablo[256];	// i*x^16 mod P
	uint16 tabhi[256];	// i*x^24 mod P

	constexpr CRC16x2Table() {
		constexpr uint16 P = 0x8005;
		uint16 bitval = P;

		tablo[0] = 0;
		for(int step = 1; step < 0x100; step *= 2) {
			// byte swap the entire table so we can XOR at the bottom of the register
			// for update speed
			uint16 xormask = (bitval << 8) + (bitval >> 8);

			for(int i = 0; i < step; ++i)
				tablo[i + step] = tablo[i] ^ xormask;

			bitval = (bitval << 1) ^ (bitval & 0x8000 ? P : 0);
		}

		tabhi[0] = 0;
		for(int step = 1; step < 0x100; step *= 2) {
			// byte swap the entire table so we can XOR at the bottom of the register
			// for update speed
			uint16 xormask = (bitval << 8) + (bitval >> 8);

			for(int i = 0; i < step; ++i)
				tabhi[i + step] = tabhi[i] ^ xormask;

			bitval = (bitval << 1) ^ (bitval & 0x8000 ? P : 0);
		}
	}
};

struct ATAudioReaderFLAC::CRCTables {
	static constexpr CRC8Table kCRC8Table {};
	static constexpr CRC16Table kCRC16Table {};
	static constexpr CRC16x2Table kCRC16x2Table {};
};

constexpr ATAudioReaderFLAC::CRC8Table ATAudioReaderFLAC::CRCTables::kCRC8Table;
constexpr ATAudioReaderFLAC::CRC16Table ATAudioReaderFLAC::CRCTables::kCRC16Table;
constexpr ATAudioReaderFLAC::CRC16x2Table ATAudioReaderFLAC::CRCTables::kCRC16x2Table;

///////////////////////////////////////////////////////////////////////////////

VDFORCEINLINE uint32 ATAudioReaderFLAC::BitReader::GetBits(uint32 n) {
	if (mBitPos + (sint32)n > 0)
		[[unlikely]] Refill();

	uint32 v = (uint32)(VDSwizzleU32(*(const uint32_t *)(mpSrcEnd + (mBitPos >> 3))) << (mBitPos & 7));
	mBitPos += n;

	return v >> (32 - n);
}

VDFORCEINLINE uint32 ATAudioReaderFLAC::BitReader::GetBitsLong(uint32 n) {
	if (mBitPos + (sint32)n > 0)
		[[unlikely]] Refill();

	// VS2022 generates terrible x86 code for a 64-bit variable shift because it
	// doesn't realize that x&7 < 32, so force a shld instruction.

#ifdef VD_CPU_X86
	uint64 v = (uint64)__ll_lshift(VDSwizzleU64(*(const uint64_t *)(mpSrcEnd + (mBitPos >> 3))), mBitPos & 7);
#else
	uint64 v = (uint64)(VDSwizzleU64(*(const uint64_t *)(mpSrcEnd + (mBitPos >> 3))) << (mBitPos & 7));
#endif

	mBitPos += n;

	return (uint32)(v >> (64 - n));
}

VDFORCEINLINE sint32 ATAudioReaderFLAC::BitReader::GetBitsSigned(uint32 n) {
	if (mBitPos + (sint32)n > 0)
		[[unlikely]] Refill();

	sint32 v = (sint32)(VDSwizzleU32(*(const uint32_t *)(mpSrcEnd + (mBitPos >> 3))) << (mBitPos & 7));
	mBitPos += n;

	return v >> (32 - n);
}

VDFORCEINLINE sint32 ATAudioReaderFLAC::BitReader::GetBitsSignedLong(uint32 n) {
	if (mBitPos + (sint32)n > 0)
		[[unlikely]] Refill();

#ifdef VD_CPU_X86
	sint64 v = __ll_lshift(VDSwizzleU64(*(const uint64_t *)(mpSrcEnd + (mBitPos >> 3))), mBitPos & 7);
#else
	sint64 v = (sint64)(VDSwizzleU64(*(const uint64_t *)(mpSrcEnd + (mBitPos >> 3))) << (mBitPos & 7));
#endif

	mBitPos += n;

	return (sint32)(v >> (64 - n));
}

template<bool T_HaveLzcnt>
VDFORCEINLINE uint32 ATAudioReaderFLAC::BitReader::GetUnaryValue() {
	uint32 count = 0;

	// If we don't already have one bits in the accumulator, do byte scan. There is apparently
	// no limit to the length of a unary-coded value, so we must be prepared to scan 0 bits
	// until the edge of the universe. The refill routine guarantees that there are at least
	// 64 zero bits beyond the current buffer, which will force us to fall into the slow path
	// below if we're about to hit the end -- so the '1' bit that terminates the run is
	// guaranteed to be within the valid buffer.
	uint32 bitAccum = (uint32)(VDSwizzleU32(*(const uint32_t *)(mpSrcEnd + (mBitPos >> 3))) << (mBitPos & 7));

	if (!bitAccum) [[unlikely]] {
		count = -mBitPos & 7;
		mBitPos += count;

		for(;;) {
			sint32 negn = mBitPos >> 3;
			uint32 n = (uint32)-negn;

			const uint8 *p = &mpSrcEnd[negn];
			for(uint32 i = 0; i < n; ++i) {
				uint8 c = p[i];

				if (c) {
					count += i*8;
					mBitPos += i*8;
					bitAccum = c << 24;
					break;
				}
			}

			if (bitAccum)
				break;

			count += n * 8;
			mBitPos += n * 8;

			Refill();
		}
	}

	// at this point we are guaranteed to have a one bit in the accumulator
	uint32 accumZeroes;

#if VD_CPU_X86 || VD_CPU_X64
	if constexpr (T_HaveLzcnt) {
		#if defined(VD_COMPILER_CLANG)
			accumZeroes = [](uint32 v) __attribute__((target("lzcnt"))) {
				return _lzcnt_u32(v);
			}(bitAccum);
		#elif defined(VD_COMPILER_GCC)
			accumZeroes = __builtin_clz(bitAccum);
		#else
			accumZeroes = _lzcnt_u32(bitAccum);
		#endif
	} else
#endif
	{
		unsigned long oneBitPos;
#ifdef _MSC_VER
		_BitScanReverse(&oneBitPos, bitAccum);
#else
		oneBitPos = 31 - __builtin_clz(bitAccum);
#endif

#if VD_CPU_ARM64
		// This is more optimal for ARM64, as the compiler doesn't realize
		// that bitAccum != 0 ==> _BSR() can't return 32, so we need to use
		// 32-x to get a clean CLZ instruction out.
		accumZeroes = 31 - oneBitPos;
#else
		// This is more optimal for x86.
		accumZeroes = oneBitPos ^ 31;
#endif
	}

	count += accumZeroes;
	mBitPos += accumZeroes + 1;

	return count;
}

VDFORCEINLINE void ATAudioReaderFLAC::BitReader::Refill() {
	const auto [pos, end] = mParent->Refill(*this);

	mBitPos = pos;
	mpSrcEnd = end;
}

///////////////////////////////////////////////////////////////////////////////

ATAudioReaderFLAC::ATAudioReaderFLAC(IVDRandomAccessStream& stream)
	: mStream(stream)
{
}

void ATAudioReaderFLAC::SetVerifyEnabled(bool enabled) {
	mbVerify = enabled;
}

void ATAudioReaderFLAC::Init() {
	mDataLen = (uint64)mStream.Length();
	ParseHeader();
}

uint64 ATAudioReaderFLAC::GetDataSize() const {
	return mDataLen;
}

uint64 ATAudioReaderFLAC::GetDataPos() const {
	return mBasePos + mPos;
}

ATAudioReadFormatInfo ATAudioReaderFLAC::GetFormatInfo() const {
	ATAudioReadFormatInfo info {};

	info.mSamplesPerSec = mStreamInfoSampleRate;
	info.mChannels = mStreamInfoChannels;

	return info;
}

uint32 ATAudioReaderFLAC::ReadStereo16(sint16 *dst, uint32 n) {
	uint32 actual = 0;

	while(n) {
		if (mCurBlockPos >= mCurBlockSize) {
			if (!ParseFrame())
				break;
		}

		uint32 tc = std::min<uint32>(n, mCurBlockSize - mCurBlockPos);
		if (tc) {
			const sint32 *left = mBlocks.data() + mCurBlockPos;
			const sint32 *right = mStreamInfoChannels > 1 ? left + mCurBlockSize : left;

			uint32 bps = mStreamInfoBitsPerSample;
			if (bps > 16) {
				int rshift = bps - 16;

				for(uint32 i = 0; i < tc; ++i) {
					dst[0] = (sint16)(*left++ >> rshift);
					dst[1] = (sint16)(*right++ >> rshift);
					dst += 2;
				}
			} else if (bps < 16) {
				int lshift = 16 - bps;

				for(uint32 i = 0; i < tc; ++i) {
					dst[0] = (sint16)(*left++ << lshift);
					dst[1] = (sint16)(*right++ << lshift);
					dst += 2;
				}
			} else {
				for(uint32 i = 0; i < tc; ++i) {
					dst[0] = (sint16)*left++;
					dst[1] = (sint16)*right++;
					dst += 2;
				}
			}

			n -= tc;
			mCurBlockPos += tc;
			actual += tc;
		}
	}

	return actual;
}

void ATAudioReaderFLAC::ParseHeader() {
	uint8 sig[4];
	Read(sig, 4);
	if (memcmp(sig, "fLaC", 4))
		throw ATFLACDecodeException("The FLAC signature was not found at the beginning of the file.");

	// read metadata chunks
	for(;;) {
		// read last+type and length
		uint8 header[4];
		Read(header, 4);

		const uint8 type = header[0] & 0x7F;
		const uint32 len = ((uint32)header[1] << 16) + ((uint32)header[2] << 8) + header[3];

		switch(type) {
			case 0:
				if (len != 34)
					throw ATFLACDecodeException("Unexpected length of STREAMINFO metadata.");

				{
					uint8 sbuf[34];

					Read(sbuf, 34);

					mStreamInfoSampleRate = ((uint32)sbuf[10] << 12)
						+ ((uint32)sbuf[11] << 4)
						+ (sbuf[12] >> 4);

					if (!mStreamInfoSampleRate)
						throw ATFLACDecodeException("Invalid sample rate in STREAMINFO metadata.");

					mStreamInfoChannels = ((sbuf[12] & 0x0E) >> 1) + 1;
					mStreamInfoBitsPerSample = ((sbuf[12] & 0x01) << 4) + (sbuf[13] >> 4) + 1;

					if (mStreamInfoBitsPerSample > 24)
						throw ATFLACDecodeException("Unsupported bit depth in STREAMINFO metadata (>24 bits per sample).");

					mSampleCount = VDReadUnalignedBEU32(&sbuf[14]) + (((uint64)sbuf[13] & 0x0F) << 32);

					memcpy(mMD5, sbuf + 18, sizeof mMD5);
				}
				break;

			default:
				Read(nullptr, len);
				break;
		}

		// check for last bit
		if (header[0] & 0x80)
			break;
	}
}

bool ATAudioReaderFLAC::ParseFrame() {
	if (mbStreamEnded)
		return false;

	uint8 hbuf[16] {};

	// begin CRC-16 at current location
	mCRC16BasePos = mPos;
	mCRC16 = 0;
	mbCRC16Enabled = true;

	// read base header (5 bytes)
	if (!TryRead(hbuf, 1)) {
		mbStreamEnded = true;

		if (mbVerify)
			VerifyEnd();

		return false;
	}

	Read(hbuf+1, 4);
	uint8 *hdst = hbuf + 5;

	// check sync and reserved bit
	if (hbuf[0] != 0xFF || (hbuf[1] & 0xFE) != 0xF8)
		throw ATFLACDecodeException("An invalid frame header was encountered.");

	// read the UTF-8 like block/sample number -- must be done before we pull
	// extension bytes for block size and sample rate
	//
	// 00-7F: 1-byte (      00-7F      )
	// 80-BF: invalid in first byte
	// C0-DF: 2-byte (      80-7FF     )
	// E0-EF: 3-byte (     800-FFFF    )
	// F0-F7: 4-byte (   10000-1FFFFF  )
	// F8-FB: 5-byte (  200000-3FFFFFF )
	// FC-FD: 6-byte ( 4000000-7FFFFFFF)
	// FE:    7-byte (80000000-FFFFFFFFF)

	uint8 numberLeadingByte = hbuf[4];
	if ((numberLeadingByte & 0xC0) == 0x80 || numberLeadingByte == 0xFF)
		throw ATFLACDecodeException("An invalid sample/block number was found in a frame header.");

	uint8 numberTrailingByteCnt = 0;
	if (numberLeadingByte >= 0xC0) {
		for(uint8 v = numberLeadingByte << 1; v & 0x80; v += v)
			++numberTrailingByteCnt;

		Read(hdst, numberTrailingByteCnt);
		hdst += numberTrailingByteCnt;
	}

	// decode block size
	constexpr uint32 kBlockSizeExplicit8 = (uint32)-1;
	constexpr uint32 kBlockSizeExplicit16 = (uint32)-2;
	static constexpr uint32 kBlockSizes[16] {
		0, 192, 576, 1152, 2304, 4608, kBlockSizeExplicit8, kBlockSizeExplicit16, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768
	};

	uint32 blockSize = kBlockSizes[hbuf[2] >> 4];
	switch(blockSize) {
		case kBlockSizeExplicit8:
			Read(hdst, 1);
			blockSize = hdst[0] + 1;
			++hdst;
			break;
		case kBlockSizeExplicit16:
			Read(hdst, 2);
			blockSize = ((uint32)hdst[0] << 8) + hdst[1] + 1;
			hdst += 2;
			break;

		default:
			break;
	}

	// The FLAC specification says that the minimum block size is 16, but neglects to
	// mention that this does NOT apply to the last block. Thus, we must allow short
	// blocks here and validate each sub block against its prediction method to make
	// sure we don't have a block shorter than the prediction delay.
	//
	//if (blockSize < 16)
	//	throw ATFLACDecodeException("An invalid block size was found in a frame header.");

	// decode sample rate
	constexpr uint32 kSampleRateStreamInfo = (uint32)-1;
	constexpr uint32 kSampleRateKHz = (uint32)-2;
	constexpr uint32 kSampleRateHz = (uint32)-3;
	constexpr uint32 kSampleRateDHz = (uint32)-4;
	static constexpr uint32 kSampleRates[16] {
		kSampleRateStreamInfo, 88200, 176400, 192000, 8000, 16000, 22050, 24000, 32000, 44100, 48000, 96000, kSampleRateKHz, kSampleRateHz, kSampleRateDHz, 0
	};

	uint32 sampleRate = kSampleRates[hbuf[2] & 15];
	switch(sampleRate) {
		case kSampleRateStreamInfo:
			sampleRate = mStreamInfoSampleRate;
			break;

		case kSampleRateKHz:
			Read(hdst, 1);
			sampleRate = *hdst * 1000;
			++hdst;
			break;

		case kSampleRateHz:
			Read(hdst, 2);
			sampleRate = ((uint32)hdst[0] << 8) + hdst[1];
			hdst += 2;
			break;

		case kSampleRateDHz:
			Read(hdst, 2);
			sampleRate = (((uint32)hdst[0] << 8) + hdst[1]) * 10;
			hdst += 2;
			break;
	}

	if (!sampleRate)
		throw ATFLACDecodeException("An invalid sample rate was found in a frame header.");

	if (sampleRate != mStreamInfoSampleRate)
		throw ATFLACDecodeException("A frame header was found with a different sample rate than in the stream info.");

	// decode channel assignment
	uint32 channelCode = hbuf[3] >> 4;
	if (channelCode > 10)
		throw ATFLACDecodeException("An invalid channel assignment was found in a frame header.");

	const uint32 channels = channelCode < 8 ? channelCode + 1 : 2;
	if (channels != mStreamInfoChannels)
		throw ATFLACDecodeException("A frame header was found with a different channel count than in the stream info.");

	// decode sample size / bits per sample
	uint32 bitsPerSample = 0;
	switch((hbuf[3] >> 1) & 7) {
		case 0:
			bitsPerSample = mStreamInfoBitsPerSample;
			break;
		case 1:
			bitsPerSample = 8;
			break;
		case 2:
			bitsPerSample = 12;
			break;
		case 4:
			bitsPerSample = 16;
			break;
		case 5:
			bitsPerSample = 20;
			break;
		case 6:
			bitsPerSample = 24;
			break;
		case 3:
		case 7:
			throw ATFLACDecodeException("An invalid sample size code was found in a frame header.");
	}

	if (bitsPerSample != mStreamInfoBitsPerSample)
		throw ATFLACDecodeException("A frame header was found with a different sample size than in the stream info.");

	// read and validate CRC-8
	uint8 crc8;
	Read(&crc8, 1);

	uint8 crc8calc = 0;
	for(const uint8 *p = hbuf; p != hdst; ++p)
		crc8calc = CRCTables::kCRC8Table.tab[crc8calc ^ *p];

	if (crc8 != crc8calc)
		throw ATFLACDecodeException("CRC error occurred in frame header.");

	mCurBlockPos = 0;
	mCurBlockSize = blockSize;
	mBlocks.resize(blockSize * channels + 3);		// extra padding for vector load over

	// Transition from byte reading to bit reading. We need to establish the 64-bit zero tail
	// for GetUnaryValue().

	memset(mBuf + mLimit, 0, 8);

	BitReader bitReader(*this);
	for(uint32 i = 0; i < channels; ++i) {
		// difference channel has an extra bit
		uint32 extraBit = 0;
		if ((channelCode == 8 && i == 1) || (channelCode == 9 && i == 0) || (channelCode == 10 && i == 1))
			extraBit = 1;

		ParseSubFrame(bitReader, std::span(mBlocks.data() + blockSize * i, blockSize), bitsPerSample + extraBit);
	}

	// check for mid-side decoding
	if (channelCode >= 8) {
		std::span leftData(mBlocks.data(), blockSize);
		std::span rightData(mBlocks.data() + blockSize, blockSize);

		if (channelCode == 8)
			DecodeLeftDiff(leftData, rightData);
		else if (channelCode == 9)
			DecodeDiffRight(leftData, rightData);
		else if (channelCode == 10)
			DecodeMidSide(leftData, rightData);
	}

	// recover remaining bytes from bitstream, realigning to bytes
	mPos = (uint32)((bitReader.mpSrcEnd - mBuf) + ((bitReader.mBitPos + 7) >> 3));

	// read CRC-16 (just let it be processed with the rest)
	Read(nullptr, 2);

	// finalize and check CRC-16, then shut it off
	if (mCRC16BasePos < mPos)
		UpdateCRC16(mBuf + mCRC16BasePos, mPos - mCRC16BasePos);

	if (!FinalizeCRC16())
		throw ATFLACDecodeException("Checksum error detected on frame data.");

	mbCRC16Enabled = false;

	// check if we have MD5 verification enabled
	if (mbVerify)
		VerifyBlock();

	return true;
}

void ATAudioReaderFLAC::DecodeLeftDiff(std::span<sint32> ch0, std::span<sint32> ch1) {
	size_t n = ch1.size();

	for(size_t i = 0; i < n; ++i)
		ch1[i] = ch0[i] - ch1[i];
}

void ATAudioReaderFLAC::DecodeDiffRight(std::span<sint32> ch0, std::span<sint32> ch1) {
	size_t n = ch0.size();

	for(size_t i = 0; i < n; ++i)
		ch0[i] += ch1[i];
}

void ATAudioReaderFLAC::DecodeMidSide(std::span<sint32> ch0, std::span<sint32> ch1) {
	size_t n = ch0.size();

	for(size_t i = 0; i < n; ++i) {
		sint32 mid = ch0[i];
		sint32 side = ch1[i];

		ch0[i] = mid + ((side + 1) >> 1);
		ch1[i] = mid - (side >> 1);
	}
}

void ATAudioReaderFLAC::ParseSubFrame(BitReader& __restrict bitReader, std::span<sint32> buffer, uint32 frameBitsPerSample) {
	if (bitReader.GetBits(1) != 0)
		throw ATFLACDecodeException("An invalid subframe header was encountered.");

	const uint32 subFrameType = bitReader.GetBits(6);
	if ((subFrameType >= 2 && subFrameType < 8) ||
		(subFrameType >= 13 && subFrameType < 32))
	{
		throw ATFLACDecodeException("An invalid subframe type was encountered.");
	}

	uint32 wastedBits = 0;
	
	if (bitReader.GetBits(1)) {
		do {
			++wastedBits;
		} while(!bitReader.GetBits(1));
	}

	if (frameBitsPerSample <= wastedBits)
		throw ATFLACDecodeException("An invalid wasted bit count was encountered.");

	const uint32 encodedBitsPerSample = frameBitsPerSample - wastedBits;

	if (subFrameType == 0)
		ParseConstantSubFrame(bitReader, buffer, encodedBitsPerSample);
	else if (subFrameType == 1)
		ParseVerbatimSubFrame(bitReader, buffer, encodedBitsPerSample);
	else if (subFrameType < 32)
		ParseFixedSubFrame(bitReader, buffer, encodedBitsPerSample, subFrameType - 8);
	else
		ParseLPCSubFrame(bitReader, buffer, encodedBitsPerSample, subFrameType - 31);

	if (wastedBits) {
		for(sint32& v : buffer)
			v <<= wastedBits;
	}
}

void ATAudioReaderFLAC::ParseConstantSubFrame(BitReader& __restrict bitReader, std::span<sint32> buffer, uint32 frameBitsPerSample) {
	sint32 v = bitReader.GetBitsSigned(frameBitsPerSample);

	std::fill(buffer.begin(), buffer.end(), v);
}

void ATAudioReaderFLAC::ParseVerbatimSubFrame(BitReader& __restrict bitReader, std::span<sint32> buffer, uint32 frameBitsPerSample) {
	for(sint32& v : buffer)
		v = bitReader.GetBitsSigned(frameBitsPerSample);
}

void ATAudioReaderFLAC::ParseFixedSubFrame(BitReader& __restrict bitReader, std::span<sint32> buffer, uint32 frameBitsPerSample, uint32 order) {
	if (order > buffer.size())
		throw ATFLACDecodeException("A subframe has a fixed predictor with a higher order than the block size.");

	// read warm-up samples
	for(uint32 i = 0; i < order; ++i)
		buffer[i] = bitReader.GetBitsSigned(frameBitsPerSample);

	// read residuals
	ParseResiduals(bitReader, buffer, order);

	// run predictor
	size_t n = buffer.size() - order;

	switch(order) {
		case 0:
			break;

		case 1:
			{
				sint32 pred = buffer[0];
				for(sint32& v : buffer.subspan(1)) {
					pred += v;
					v = pred;
				}
			}
			break;

		case 2:
			{
				auto it = buffer.begin();
				for(size_t i = 0; i < n; ++i)
					it[i+2] += 2*it[i+1] - it[i];
			}
			break;

		case 3:
			{
				auto y = buffer.begin();
				for(size_t i = 0; i < n; ++i, ++y)
					y[3] += 3*(y[2] - y[1]) + y[0];
			}
			break;

		case 4:
			{
				auto y = buffer.begin();
				for(size_t i = 0; i < n; ++i, ++y)
					y[4] += 4*(y[3] + y[1]) - 6*y[2] - y[0];
			}
			break;
	}
}

void ATAudioReaderFLAC::ParseLPCSubFrame(BitReader& __restrict bitReader, std::span<sint32> buffer, uint32 frameBitsPerSample, uint32 order) {
	if (order > buffer.size())
		throw ATFLACDecodeException("A subframe has an LPC predictor with a higher order than the block size.");

	// parse warm-up samples
	for(uint32 i = 0; i < order; ++i)
		buffer[i] = bitReader.GetBitsSigned(frameBitsPerSample);

	// read LPC parameters
	uint32 qlpPrecision = bitReader.GetBits(4) + 1;
	if (qlpPrecision == 16)
		throw ATFLACDecodeException("Invalid QLP coefficient precision specified in LPC-encoded frame.");

	// read LPC quantization shift
	//
	// An oddity of the FLAC format is that the quantization shift is a signed value.
	// This was intended to support shifting the result left, but did not work due to C
	// only accepting positive shifts. The result is that the reference encoder never
	// generates negative shifts and no decoders are known to support it. Sadly, the
	// Xiph docs have not been updated to indicate this.
	//
	// References:
	//  http://lists.xiph.org/pipermail/flac-dev/2009-April/002654.html
	//  http://lists.xiph.org/pipermail/flac-dev/2015-August/005579.html
	//
	sint32 qlpShift = bitReader.GetBitsSigned(5);

	if (qlpShift < 0)
		throw ATFLACDecodeException("Invalid QLP quantization shift: negative shift not supported.");

	alignas(16) sint32 lpcCoeffs[32] {};
	uint32 coeffAbsSum = 0;

	for(uint32 i = 0; i < order; ++i) {
		const sint32 coeff = bitReader.GetBitsSigned(qlpPrecision);
		lpcCoeffs[(order - 1) - i] = coeff;
		coeffAbsSum += (uint32)abs(coeff);
	}

	// read residuals
	ParseResiduals(bitReader, buffer, order);

	// Run LPC predictor.
	//
	// We have to be prepared to use a full 32x32 -> 64 multiply here if high-precision
	// samples and LPC coefficients are in use, since the samples can be 32-bit and
	// the coefficients 16-bit, giving 48-bit products and a max 53-bit fractional
	// prediction. Also, the quantization shift can only be 15-bit, so of this only
	// the lower 48 bits of that can actually determine the final prediction.
	//
	// However, most of the time we are dealing with narrower parameters, such as 16-bit
	// samples, LPC order 12, precision 12, quantization 9. We can avoid 64-bit
	// arithmetic if the prediction size + precision <= 32. If the sample size and LPC
	// coefficient precision are both <= 16, that's even better as we can use
	// 16x16 -> 32 multiplies.
	//
	// Note that there is no requirement for the IIR filter to have unity gain, and it
	// usually doesn't. Typically it is <1 but there doesn't seem to be any requirement
	// against this, particularly as the residual combined with integer arithmetic would
	// prevent a gain>1 filter from blowing up. To be conservative, we compute the
	// min/max output range of the filter and use 32-bit math only if we can guarantee
	// that signed overflow will not occur. The necessary condition is:
	//
	//  log2(sum(abs(coeffs))) + sample_size <= 32
	//
	auto y = buffer.data();
	uint32 n = (uint32)buffer.size() - order;

	if (coeffAbsSum <= (UINT32_C(1) << (32 - frameBitsPerSample))) {
		// Final dot product will not exceed 32-bit before shifting, so we can use
		// 32x32 -> 32 multiplies. There is a catch, though -- intermediate sums might,
		// so we need to ensure that we don't trip UB via a signed overflow. Fortunately,
		// we can convince the compiler to generate a 32-bit IMUL for 64x32 -> 32.
#if VD_CPU_X86 || VD_CPU_X64
		if (order <= 16) {
			// order-1 is not worth vectorizing
			if (frameBitsPerSample <= 16 && order > 1) {
				if (CPUGetEnabledExtensions() & CPUF_SUPPORTS_SSSE3)
					ATFLACReconstructLPC_Narrow_SSSE3(y, n, lpcCoeffs, qlpShift, order);
				else
					ATFLACReconstructLPC_Narrow_SSE2(y, n, lpcCoeffs, qlpShift, order);

				return;
			}
		}
#elif VD_CPU_ARM64
		if (frameBitsPerSample <= 16 && order > 1 && order <= 16) {
			ATFLACReconstructLPC_Narrow_NEON(y, n, lpcCoeffs, qlpShift, order);
			return;
		}

		if (order > 1) {
			ATFLACReconstructLPC_Medium_NEON(y, n, lpcCoeffs, qlpShift, order);
			return;
		}
#endif

		switch(order) {
			case  1: ReconstructLPC< 1>(y, n, lpcCoeffs, qlpShift); break;
			case  2: ReconstructLPC< 2>(y, n, lpcCoeffs, qlpShift); break;
			case  3: ReconstructLPC< 3>(y, n, lpcCoeffs, qlpShift); break;
			case  4: ReconstructLPC< 4>(y, n, lpcCoeffs, qlpShift); break;
			case  5: ReconstructLPC< 5>(y, n, lpcCoeffs, qlpShift); break;
			case  6: ReconstructLPC< 6>(y, n, lpcCoeffs, qlpShift); break;
			case  7: ReconstructLPC< 7>(y, n, lpcCoeffs, qlpShift); break;
			case  8: ReconstructLPC< 8>(y, n, lpcCoeffs, qlpShift); break;
			case  9: ReconstructLPC< 9>(y, n, lpcCoeffs, qlpShift); break;
			case 10: ReconstructLPC<10>(y, n, lpcCoeffs, qlpShift); break;
			case 11: ReconstructLPC<11>(y, n, lpcCoeffs, qlpShift); break;
			case 12: ReconstructLPC<12>(y, n, lpcCoeffs, qlpShift); break;
			case 13: ReconstructLPC<13>(y, n, lpcCoeffs, qlpShift); break;
			case 14: ReconstructLPC<14>(y, n, lpcCoeffs, qlpShift); break;
			case 15: ReconstructLPC<15>(y, n, lpcCoeffs, qlpShift); break;
			case 16: ReconstructLPC<16>(y, n, lpcCoeffs, qlpShift); break;
			case 17: ReconstructLPC<17>(y, n, lpcCoeffs, qlpShift); break;
			case 18: ReconstructLPC<18>(y, n, lpcCoeffs, qlpShift); break;
			case 19: ReconstructLPC<19>(y, n, lpcCoeffs, qlpShift); break;
			case 20: ReconstructLPC<20>(y, n, lpcCoeffs, qlpShift); break;
			case 21: ReconstructLPC<21>(y, n, lpcCoeffs, qlpShift); break;
			case 22: ReconstructLPC<22>(y, n, lpcCoeffs, qlpShift); break;
			case 23: ReconstructLPC<23>(y, n, lpcCoeffs, qlpShift); break;
			case 24: ReconstructLPC<24>(y, n, lpcCoeffs, qlpShift); break;
			case 25: ReconstructLPC<25>(y, n, lpcCoeffs, qlpShift); break;
			case 26: ReconstructLPC<26>(y, n, lpcCoeffs, qlpShift); break;
			case 27: ReconstructLPC<27>(y, n, lpcCoeffs, qlpShift); break;
			case 28: ReconstructLPC<28>(y, n, lpcCoeffs, qlpShift); break;
			case 29: ReconstructLPC<29>(y, n, lpcCoeffs, qlpShift); break;
			case 30: ReconstructLPC<30>(y, n, lpcCoeffs, qlpShift); break;
			case 31: ReconstructLPC<31>(y, n, lpcCoeffs, qlpShift); break;
			case 32: ReconstructLPC<32>(y, n, lpcCoeffs, qlpShift); break;
		}
	} else {
#if VD_CPU_X86 || VD_CPU_X64
		// order-1 is not worth vectorizing
		if (frameBitsPerSample <= 24 && order > 1 && order <= 16) {
			if (coeffAbsSum <= (UINT32_C(1) << 19)) {
				if (CPUGetEnabledExtensions() & CPUF_SUPPORTS_SSSE3) {
					ATFLACReconstructLPC_Medium_SSSE3(y, n, lpcCoeffs, qlpShift, order);
					return;
				}
			}
		}
#elif VD_CPU_ARM64
		if (order > 1) {
			ATFLACReconstructLPC_Wide_NEON(y, n, lpcCoeffs, qlpShift, order);
			return;
		}
#endif

		switch(order) {
			case  1: ReconstructLPC_Wide< 1>(y, n, lpcCoeffs, qlpShift); break;
			case  2: ReconstructLPC_Wide< 2>(y, n, lpcCoeffs, qlpShift); break;
			case  3: ReconstructLPC_Wide< 3>(y, n, lpcCoeffs, qlpShift); break;
			case  4: ReconstructLPC_Wide< 4>(y, n, lpcCoeffs, qlpShift); break;
			case  5: ReconstructLPC_Wide< 5>(y, n, lpcCoeffs, qlpShift); break;
			case  6: ReconstructLPC_Wide< 6>(y, n, lpcCoeffs, qlpShift); break;
			case  7: ReconstructLPC_Wide< 7>(y, n, lpcCoeffs, qlpShift); break;
			case  8: ReconstructLPC_Wide< 8>(y, n, lpcCoeffs, qlpShift); break;
			case  9: ReconstructLPC_Wide< 9>(y, n, lpcCoeffs, qlpShift); break;
			case 10: ReconstructLPC_Wide<10>(y, n, lpcCoeffs, qlpShift); break;
			case 11: ReconstructLPC_Wide<11>(y, n, lpcCoeffs, qlpShift); break;
			case 12: ReconstructLPC_Wide<12>(y, n, lpcCoeffs, qlpShift); break;
			case 13: ReconstructLPC_Wide<13>(y, n, lpcCoeffs, qlpShift); break;
			case 14: ReconstructLPC_Wide<14>(y, n, lpcCoeffs, qlpShift); break;
			case 15: ReconstructLPC_Wide<15>(y, n, lpcCoeffs, qlpShift); break;
			case 16: ReconstructLPC_Wide<16>(y, n, lpcCoeffs, qlpShift); break;
			case 17: ReconstructLPC_Wide<17>(y, n, lpcCoeffs, qlpShift); break;
			case 18: ReconstructLPC_Wide<18>(y, n, lpcCoeffs, qlpShift); break;
			case 19: ReconstructLPC_Wide<19>(y, n, lpcCoeffs, qlpShift); break;
			case 20: ReconstructLPC_Wide<20>(y, n, lpcCoeffs, qlpShift); break;
			case 21: ReconstructLPC_Wide<21>(y, n, lpcCoeffs, qlpShift); break;
			case 22: ReconstructLPC_Wide<22>(y, n, lpcCoeffs, qlpShift); break;
			case 23: ReconstructLPC_Wide<23>(y, n, lpcCoeffs, qlpShift); break;
			case 24: ReconstructLPC_Wide<24>(y, n, lpcCoeffs, qlpShift); break;
			case 25: ReconstructLPC_Wide<25>(y, n, lpcCoeffs, qlpShift); break;
			case 26: ReconstructLPC_Wide<26>(y, n, lpcCoeffs, qlpShift); break;
			case 27: ReconstructLPC_Wide<27>(y, n, lpcCoeffs, qlpShift); break;
			case 28: ReconstructLPC_Wide<28>(y, n, lpcCoeffs, qlpShift); break;
			case 29: ReconstructLPC_Wide<29>(y, n, lpcCoeffs, qlpShift); break;
			case 30: ReconstructLPC_Wide<30>(y, n, lpcCoeffs, qlpShift); break;
			case 31: ReconstructLPC_Wide<31>(y, n, lpcCoeffs, qlpShift); break;
			case 32: ReconstructLPC_Wide<32>(y, n, lpcCoeffs, qlpShift); break;
		}
	}
}

template<int Order>
void ATAudioReaderFLAC::ReconstructLPC(sint32 *__restrict y, uint32 n, const sint32 *__restrict lpcCoeffs, int qlpShift) {
	for(uint32 i = 0; i < n; ++i) {
		uint32 pred = (uint32)((sint64)y[0] * lpcCoeffs[0]);

		for(uint32 j = 1; j < Order; ++j)
			pred += (uint32)((sint64)y[j] * lpcCoeffs[j]);

		y[Order] += (sint32)pred >> qlpShift;

		++y;
	}
}

template<int Order>
void ATAudioReaderFLAC::ReconstructLPC_Wide(sint32 *__restrict y, uint32 n, const sint32 *__restrict lpcCoeffs, int qlpShift) {
	for(uint32 i = 0; i < n; ++i) {
		sint64 pred = (sint64)y[0] * lpcCoeffs[0];

		for(uint32 j = 1; j < Order; ++j)
			pred += (sint64)y[j] * lpcCoeffs[j];

		y[Order] += (sint32)(pred >> qlpShift);

		++y;
	}
}

void ATAudioReaderFLAC::ParseResiduals(BitReader& __restrict bitReader0, std::span<sint32> buffer, uint32 order) {
	BitReader bitReader(bitReader0);

	uint32 codingMethod = bitReader.GetBits(2);

	if (codingMethod >= 2)
		throw ATFLACDecodeException("Invalid residual coding method specified in subframe.");

	uint32 partitionOrder = bitReader.GetBits(4);
	uint32 partitionCount = 1 << partitionOrder;
	uint32 partitionSize = (uint32)buffer.size() >> partitionOrder;

	if (partitionSize < order || (partitionSize << partitionOrder) != buffer.size())
		throw ATFLACDecodeException("Invalid residual partition size specified for block size.");

	uint32 partitionOffset = order;
	for(uint32 i = 0; i < partitionCount; ++i) {
		uint32 curPartitionSize = i ? partitionSize : partitionSize - order;
		auto partition = buffer.subspan(partitionOffset, curPartitionSize);

		// read Rice parameter and check for escape to raw binary samples
		uint32 riceParam = bitReader.GetBits(codingMethod ? 5 : 4);
		if (riceParam == (codingMethod ? 31 : 15)) {
			// decode raw samples
			uint32 bitsPerSample = bitReader.GetBits(5);

			if (bitsPerSample > 24) {
				for(sint32& v : partition)
					v = bitReader.GetBitsSignedLong(bitsPerSample);
			} else if (bitsPerSample) {
				for(sint32& v : partition)
					v = bitReader.GetBitsSigned(bitsPerSample);
			} else {
				for(sint32& v : partition)
					v = 0;
			}
		} else {
			// decode Rice-Golomb encoded samples
			const auto decode = [=, &bitReader](auto riceParam, auto haveLzcnt) {
				sint32 *__restrict p = partition.data();
				auto bitReader2 = bitReader;
				uint32 literalBits = riceParam;

				if (literalBits > 24) {
					for(uint32 j = 0; j < curPartitionSize; ++j) {
						sint32 enc = bitReader2.GetUnaryValue<haveLzcnt>() << (literalBits - 1);

						// we have to be careful here as the raw magnitude may be 32 bit
						uint32 lit = bitReader2.GetBitsLong(literalBits);
						sint32 v = enc + (sint32)(lit >> 1);

						if (lit & 1)
							v = -v - 1;
		
						*p++ = v;
					}
				} else {
					for(uint32 j = 0; j < curPartitionSize; ++j) {
						sint32 enc = bitReader2.GetUnaryValue<haveLzcnt>() << literalBits;

						if (literalBits)
							enc += bitReader2.GetBits(literalBits);

						sint32 v = enc >> 1;
						if (enc & 1)
							v = -v - 1;

						*p++ = v;
					}
				}

				bitReader = bitReader2;
			};

			const auto decodeUnrollParam = [&](auto riceParam, auto haveLzcnt) {
				switch(riceParam) {
					case  0: decode(std::integral_constant<uint32,  0>(), haveLzcnt); break;
					case  1: decode(std::integral_constant<uint32,  1>(), haveLzcnt); break;
					case  2: decode(std::integral_constant<uint32,  2>(), haveLzcnt); break;
					case  3: decode(std::integral_constant<uint32,  3>(), haveLzcnt); break;
					case  4: decode(std::integral_constant<uint32,  4>(), haveLzcnt); break;
					case  5: decode(std::integral_constant<uint32,  5>(), haveLzcnt); break;
					case  6: decode(std::integral_constant<uint32,  6>(), haveLzcnt); break;
					case  7: decode(std::integral_constant<uint32,  7>(), haveLzcnt); break;
					case  8: decode(std::integral_constant<uint32,  8>(), haveLzcnt); break;
					case  9: decode(std::integral_constant<uint32,  9>(), haveLzcnt); break;
					case 10: decode(std::integral_constant<uint32, 10>(), haveLzcnt); break;
					case 11: decode(std::integral_constant<uint32, 11>(), haveLzcnt); break;
					case 12: decode(std::integral_constant<uint32, 12>(), haveLzcnt); break;
					case 13: decode(std::integral_constant<uint32, 13>(), haveLzcnt); break;
					case 14: decode(std::integral_constant<uint32, 14>(), haveLzcnt); break;
					case 15: decode(std::integral_constant<uint32, 15>(), haveLzcnt); break;
					case 16: decode(std::integral_constant<uint32, 16>(), haveLzcnt); break;
					case 17: decode(std::integral_constant<uint32, 17>(), haveLzcnt); break;
					case 18: decode(std::integral_constant<uint32, 18>(), haveLzcnt); break;
					case 19: decode(std::integral_constant<uint32, 19>(), haveLzcnt); break;
					case 20: decode(std::integral_constant<uint32, 20>(), haveLzcnt); break;
					case 21: decode(std::integral_constant<uint32, 21>(), haveLzcnt); break;
					case 22: decode(std::integral_constant<uint32, 22>(), haveLzcnt); break;
					case 23: decode(std::integral_constant<uint32, 23>(), haveLzcnt); break;
					case 24: decode(std::integral_constant<uint32, 24>(), haveLzcnt); break;
					case 25: decode(std::integral_constant<uint32, 25>(), haveLzcnt); break;
					case 26: decode(std::integral_constant<uint32, 26>(), haveLzcnt); break;
					case 27: decode(std::integral_constant<uint32, 27>(), haveLzcnt); break;
					case 28: decode(std::integral_constant<uint32, 28>(), haveLzcnt); break;
					case 29: decode(std::integral_constant<uint32, 29>(), haveLzcnt); break;
					case 30: decode(std::integral_constant<uint32, 30>(), haveLzcnt); break;
					default:
						decode(riceParam, haveLzcnt);
						break;
				}
			};

#if VD_CPU_X86 || VD_CPU_X64
			if (CPUGetEnabledExtensions() & CPUF_SUPPORTS_LZCNT)
				decodeUnrollParam(riceParam, std::true_type());
			else
#endif
				decodeUnrollParam(riceParam, std::false_type());
		}

		partitionOffset += curPartitionSize;
	}

	bitReader0 = bitReader;
}

bool ATAudioReaderFLAC::TryRead(void *buf, uint32 bytes) {
	while(bytes) {
		uint32 avail = mLimit - mPos;

		if (avail) {
			uint32 tc = std::min(bytes, avail);

			if (buf) {
				memcpy(buf, mBuf + mPos, tc);
				buf = (char *)buf + tc;
			}

			mPos += tc;
			bytes -= tc;

			if (!bytes)
				break;
		}

		// If CRC16 is currently enabled, update the CRC16 with the data we're about to flush.
		if (mbCRC16Enabled)
			UpdateCRC16(mBuf + mCRC16BasePos, mPos - mCRC16BasePos);

		// Refill whole buffer.
		mBasePos += mPos;
		mPos = 0;
		mCRC16BasePos = 0;
		mLimit = ReadRaw(mBuf, kBlockSize);
		if (!mLimit)
			return false;
	}

	return true;
}

void ATAudioReaderFLAC::Read(void *buf, uint32 bytes) {
	if (!TryRead(buf, bytes))
		throw ATFLACDecodeException("Unexpected end of file.");
}

uint32 ATAudioReaderFLAC::ReadRaw(void *buf, uint32 bytes) {
	auto actual = mStream.ReadData(buf, bytes);

	return actual > 0 ? (uint32)actual : 0;
}

ATAudioReaderFLAC::BitPos ATAudioReaderFLAC::Refill(BitPos bitPos) {
	if (mLimit == mPos) {
		// The bit reader may still need up to 64 bits of data still in the buffer,
		// which we are sure to preserve in the red zone before the proper start.
		// This also means that we can't CRC16 those bytes as they may be rewound
		// by the bit reader at the end of the frame due to unused readahead bits.
		uint32 tailLen = std::min<uint32>(mPos, 16);
		uint32 tailPos = mPos - tailLen;

		if (mbCRC16Enabled && mCRC16BasePos < tailPos) {
			UpdateCRC16(mBuf + mCRC16BasePos, tailPos - mCRC16BasePos);
			mCRC16BasePos = 16 - tailLen;
		}

		memmove(mBuf + 16 - tailLen, mBuf + tailPos, tailLen);

		mBasePos += mPos - 16;
		mPos = 16;

		uint32 actual = ReadRaw(mBuf + mPos, kBlockSize);
		if (!actual)
			throw ATFLACDecodeException("Unexpected end of file.");

		mLimit = mPos + actual;
		memset(mBuf + mLimit, 0, 8);
	}

	const uint32 avail = mLimit - mPos;

	bitPos.mpSrcEnd = mBuf + mLimit;
	bitPos.mBitPos -= avail << 3;
	mPos = mLimit;

	return bitPos;
}

void ATAudioReaderFLAC::VerifyBlock() {
	// The FLAC signature is an MD5 digest computed over the signal output of the decoder,
	// after joint stereo decoding, with channel data interleaved and all value little
	// endian, stored with the minimal number of bytes. This means we need to convert to
	// a temporary buffer.
	uint8 verBlock[2048];
	const uint32 channels = mStreamInfoChannels;
	const uint32 bytesPerSample = (mStreamInfoBitsPerSample + 7) >> 3;
	uint32 maxSamples = 512 / channels;

	for(uint32 offset = 0; offset < mCurBlockSize; offset += maxSamples) {
		uint32 tc = std::min<uint32>(mCurBlockSize - offset, maxSamples);

		uint8 *dst = verBlock;
		if (mStreamInfoBitsPerSample <= 8) {
			for(uint32 i = 0; i < tc; ++i) {
				for(uint32 j = 0; j < channels; ++j) {
					*dst++ = mBlocks[offset + i + mCurBlockSize * j];
				}
			}
		} else if (mStreamInfoBitsPerSample <= 16) {
			for(uint32 i = 0; i < tc; ++i) {
				for(uint32 j = 0; j < channels; ++j) {
					*(uint16 *)dst = mBlocks[offset + i + mCurBlockSize * j];
					dst += 2;
				}
			}
		} else if (mStreamInfoBitsPerSample <= 24) {
			for(uint32 i = 0; i < tc; ++i) {
				for(uint32 j = 0; j < channels; ++j) {
					uint32 v = mBlocks[offset + i + mCurBlockSize * j];
					dst[0] = (uint8)v;
					dst[1] = (uint8)(v >> 8);
					dst[2] = (uint8)(v >> 16);
					dst += 3;
				}
			}
		} else {
			for(uint32 i = 0; i < tc; ++i) {
				for(uint32 j = 0; j < channels; ++j) {
					*(uint32 *)dst = mBlocks[offset + i + mCurBlockSize * j];
					dst += 4;
				}
			}
		}

		mMD5Engine.Update(verBlock, tc * channels * bytesPerSample);
	}
}

void ATAudioReaderFLAC::VerifyEnd() {
	ATMD5Digest digest = mMD5Engine.Finalize();

	if (memcmp(digest.digest, mMD5, 16))
		throw ATFLACDecodeException("A decoding error was detected by a checksum mismatch.");
}

void ATAudioReaderFLAC::UpdateCRC16(const uint8 *p, size_t n) {
	if (!n)
		return;

#if VD_CPU_X86 || VD_CPU_X64
	if (CPUGetEnabledExtensions() & CPUF_SUPPORTS_CLMUL)
		mCRC16 = ATFLACUpdateCRC16_PCMUL(mCRC16, p, n);
	else
#elif VD_CPU_ARM64
	if (CPUGetEnabledExtensions() & VDCPUF_SUPPORTS_CRYPTO)
		mCRC16 = ATFLACUpdateCRC16_Crypto(mCRC16, p, n);
	else
#endif
		UpdateCRC16_Scalar(p, n);
}

void ATAudioReaderFLAC::UpdateCRC16_Scalar(const uint8 *__restrict p, size_t n) {
	if (!n)
		return;

	if ((uintptr_t)p & 1) {
		mCRC16 = (mCRC16 >> 8) ^ CRCTables::kCRC16x2Table.tablo[(mCRC16 ^ *p++) & 0xFF];

		if (!--n)
			return;
	}

	size_t n2 = n >> 1;
	while(n2--) {
		uint16 v = *(const uint16 *)p ^ mCRC16;
		p += 2;

		mCRC16 = CRCTables::kCRC16x2Table.tabhi[v & 0xFF] ^ CRCTables::kCRC16x2Table.tablo[v >> 8];
	}

	if (n & 1)
		mCRC16 = (mCRC16 >> 8) ^ CRCTables::kCRC16x2Table.tablo[(mCRC16 ^ p[0]) & 0xFF];
}

bool ATAudioReaderFLAC::FinalizeCRC16() {
	return mCRC16 == 0;
}

#ifdef _MSC_VER
#pragma optimize("", on)
#pragma runtime_checks("", restore)
#pragma check_stack()
#endif

////////////////////////////////////////////////////////////////////////////////

IATAudioReader *ATCreateAudioReaderFLAC(IVDRandomAccessStream& inputStream, bool verify) {
	vdautoptr<ATAudioReaderFLAC> reader(new ATAudioReaderFLAC(inputStream));

	reader->SetVerifyEnabled(verify);
	reader->Init();
	return reader.release();
}
