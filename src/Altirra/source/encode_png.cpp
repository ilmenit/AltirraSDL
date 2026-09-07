#include <stdafx.h>
#include <stdio.h>
#include <algorithm>
#include <numeric>
#include <vd2/system/zip.h>
#include <vd2/system/error.h>
#include <vd2/system/binary.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>
#include "encode_png.h"
#include "common_png.h"

#ifndef VDFORCEINLINE
	#ifdef _MSC_VER
		#define VDFORCEINLINE __forceinline
	#else
		#define VDFORCEINLINE
	#endif
#endif

namespace {
	void PNGPredictEncodeNone(uint8 *dst, const uint8 *row, const uint8 *prevrow, uint32 rowbytes, uint32 bpp) {
		memcpy(dst, row, rowbytes);
	}

	void PNGPredictEncodeSub(uint8 *dst, const uint8 *row, const uint8 *prevrow, uint32 rowbytes, uint32 bpp) {
		for(uint32 i=0; i<bpp; ++i)
			dst[i] = row[i];

		for(uint32 i=bpp; i<rowbytes; ++i)
			dst[i] = row[i] - row[i-bpp];
	}

	void PNGPredictEncodeUp(uint8 *dst, const uint8 *row, const uint8 *prevrow, uint32 rowbytes, uint32 bpp) {
		if (prevrow) {
			for(uint32 i=0; i<rowbytes; ++i)
				dst[i] = row[i] - prevrow[i];
		} else {
			memcpy(dst, row, rowbytes);
		}
	}

	void PNGPredictEncodeAverage(uint8 *dst, const uint8 *row, const uint8 *prevrow, uint32 rowbytes, uint32 bpp) {
		if (prevrow) {
			for(uint32 i=0; i<bpp; ++i)
				dst[i] = row[i] - (prevrow[i]>>1);

			for(uint32 j=bpp; j<rowbytes; ++j)
				dst[j] = row[j] - ((prevrow[j] + row[j-bpp])>>1);
		} else {
			for(uint32 i=0; i<bpp; ++i)
				dst[i] = row[i];

			for(uint32 j=bpp; j<rowbytes; ++j)
				dst[j] = row[j] - (row[j-bpp]>>1);
		}
	}

	void PNGPredictEncodePaeth(uint8 *dst, const uint8 *row, const uint8 *prevrow, uint32 rowbytes, uint32 bpp) {
		using namespace nsVDPNG;

		if (prevrow) {
			for(uint32 i=0; i<bpp; ++i)
				dst[i] = row[i] - PNGPaethPredictor(0, prevrow[i], 0);
			for(uint32 j=bpp; j<rowbytes; ++j)
				dst[j] = row[j] - PNGPaethPredictor(row[j-bpp], prevrow[j], prevrow[j-bpp]);
		} else {
			for(uint32 i=0; i<bpp; ++i)
				dst[i] = row[i];
			for(uint32 j=bpp; j<rowbytes; ++j)
				dst[j] = row[j] - PNGPaethPredictor(row[j-bpp], 0, 0);
		}
	}

	uint32 ComputeSumAbsoluteSignedBytes(const sint8 *src, uint32 len) {
		uint32 sum = 0;
		do {
			sint8 c = *src++;
			sint8 mask = c>>7;

			sum += (c + mask) ^ mask;
		} while(--len);

		return sum;
	}
}

class VDImageEncoderPNG final : public IVDImageEncoderPNG {
public:
	VDImageEncoderPNG();
	~VDImageEncoderPNG();

	void SetPAR(double par) override;
	void Encode(const VDPixmap& px, const void *&p, uint32& len, bool quick) override;

protected:
	vdfastvector<uint8>	mOutput;
	double mPAR = 1;
};

VDImageEncoderPNG::VDImageEncoderPNG() {
}

VDImageEncoderPNG::~VDImageEncoderPNG() {
}

void VDImageEncoderPNG::SetPAR(double par) {
	mPAR = par;
}

void VDImageEncoderPNG::Encode(const VDPixmap& px, const void *&p, uint32& len, bool quick) {
	using namespace nsVDPNG;

	mOutput.assign(kPNGSignature, kPNGSignature + 8);

	struct IHDR {
		uint32	mChunkLength;
		uint32	mChunkType;
		uint32	mWidth;
		uint32	mHeight;
		uint8	mDepth;
		uint8	mColorType;
		uint8	mCompression;
		uint8	mFilterMethod;
		uint8	mInterlaceMethod;
	} ihdr;

	ihdr.mChunkLength		= VDToBE32(13);
	ihdr.mChunkType			= VDMAKEFOURCC('I', 'H', 'D', 'R');
	ihdr.mWidth				= VDToBE32(px.w);
	ihdr.mHeight			= VDToBE32(px.h);
	ihdr.mDepth				= 8;
	ihdr.mColorType			= 2;		// truecolor
	ihdr.mCompression		= 0;		// Deflate
	ihdr.mFilterMethod		= 0;		// basic adaptive filtering
	ihdr.mInterlaceMethod	= 0;		// no interlacing

	const VDCRCTable& crcTable = VDCRCTable::CRC32;
	uint32 ihdr_crc = VDToBE32(crcTable.CRC(&ihdr.mChunkType, 17));

	mOutput.insert(mOutput.end(), (const uint8 *)&ihdr, (const uint8 *)&ihdr + 21);
	mOutput.insert(mOutput.end(), (const uint8 *)&ihdr_crc, (const uint8 *)&ihdr_crc + 4);

	// encode a pHYs chunk if non-square pixels
	if (mPAR != 1) {
		VDFraction par32(mPAR);

		struct PHYS {
			uint32	mChunkLength = VDToBE32(9);
			uint32	mChunkType = VDMAKEFOURCC('p', 'H', 'Y', 's');
			uint32	mPixelsPerUnitX = 0;
			uint32	mPixelsPerUnitY = 0;
			uint8	mPixelUnits = 0;	// unknown
			uint8	mCRC[4];
		} phys;

		phys.mPixelsPerUnitX = VDToBE32(par32.getLo());
		phys.mPixelsPerUnitY = VDToBE32(par32.getHi());

		VDWriteUnalignedBEU32(phys.mCRC, crcTable.CRC(&phys, 17));

		mOutput.insert(mOutput.end(), (const uint8 *)&phys, (const uint8 *)&phys + 21);
	}

	VDPixmapBuffer pxtmp(px.w, px.h, nsVDPixmap::kPixFormat_RGB888);
	VDPixmapBlt(pxtmp, px);

	VDMemoryBufferStream encOutputStream;

	// way too big for stack
	// MERGE NOTE: test18 always selected Filtered and ignored the public
	// quick-compression argument that the previous encoder honored.
	vdautoptr<VDDeflateStream> enc(new VDDeflateStream(
		encOutputStream,
		VDDeflateChecksumMode::Adler32,
		quick ? VDDeflateCompressionLevel::Quick
			: VDDeflateCompressionLevel::Filtered));

	const uint32 w = pxtmp.w;
	const uint32 rowbytes = w*3;
	vdfastvector<uint8> temprowbuf(rowbytes*5);
	uint8 *tempmem = temprowbuf.data();
	const uint8 *prevrow = NULL;

	for(uint32 y=0; y<(uint32)pxtmp.h; ++y) {
		// swap red and blue for this row
		uint8 *dst = (uint8 *)pxtmp.data + pxtmp.pitch * y;
		const uint8 *src = dst;
		for(uint32 x=w; x; --x) {
			uint8 b = dst[0];
			uint8 r = dst[2];
			dst[0] = r;
			dst[2] = b;
			dst += 3;
		}

		// try all predictors
		static void (*const predictors[])(uint8 *dst, const uint8 *row, const uint8 *prevrow, uint32 rowbytes, uint32 bpp) = {
			PNGPredictEncodeNone,
			PNGPredictEncodeSub,
			PNGPredictEncodeUp,
			PNGPredictEncodeAverage,
			PNGPredictEncodePaeth,
		};

		uint32 best = 0;
		uint32 bestscore = 0xFFFFFFFF;

		for(int i=0; i<5; ++i) {
			uint8 *dst = tempmem + rowbytes * i;
			predictors[i](dst, src, prevrow, rowbytes, 3);

			uint32 score = ComputeSumAbsoluteSignedBytes((const sint8*)dst, rowbytes);
			if (score < bestscore) {
				best = i;
				bestscore = score;
			}
		}

		const uint8 comp = best;
		enc->Write(&comp, 1);
		enc->Write(tempmem + rowbytes * best, rowbytes);

		prevrow = src;
	}

	enc->Finalize();

	const vdspan<const uint8> encoutput = encOutputStream.GetBuffer();

	struct IDAT {
		uint32	mChunkLength;
		uint32	mChunkType;
	} idat;
	idat.mChunkLength		= VDToBE32((uint32)(encoutput.size() + 6));
	idat.mChunkType			= VDMAKEFOURCC('I', 'D', 'A', 'T');

	mOutput.insert(mOutput.end(), (const uint8 *)&idat, (const uint8 *)&idat + 8);

	static const uint8 kZlibHeader[] {
		0x78,	// 32K window, Deflate
		0xDA,	// maximum compression, no dictionary, check offset = 0x1A
	};

	mOutput.append_range(kZlibHeader);
	mOutput.append_range(encoutput);

	alignas(4) uint8 adler32[4];
	VDWriteUnalignedBEU32(adler32, enc->Adler32());
	mOutput.append_range(adler32);

	VDCRCChecker crcChecker(crcTable);
	crcChecker.Process(&idat.mChunkType, 4);
	crcChecker.Process(kZlibHeader, sizeof kZlibHeader);
	crcChecker.Process(encoutput.data(), (sint32)encoutput.size());
	crcChecker.Process(adler32, sizeof adler32);

	alignas(4) uint8 idat_crc[4];
	VDWriteUnalignedBEU32(idat_crc, crcChecker.CRC());
	mOutput.append_range(idat_crc);

	uint8 footer[]={
		0, 0, 0, 0, 'I', 'E', 'N', 'D', 0, 0, 0, 0
	};

	VDWriteUnalignedBEU32(footer+8, crcTable.CRC(footer + 4, 4));

	mOutput.append_range(footer);

	p = mOutput.data();
	len = (uint32)mOutput.size();
}

IVDImageEncoderPNG *VDCreateImageEncoderPNG() {
	return new VDImageEncoderPNG;
}
