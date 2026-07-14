//	Altirra - Atari 800/800XL/5200 emulator
//	SDL3 video recording - portable AVI encoders (Raw, RLE, ZMBV)
//
//	Phase 3e note: this file was a candidate for splitting under the
//	Phase 3 size-reduction pass, but re-inspection showed a single
//	anonymous namespace wrapping a tightly coupled AVI muxer plus
//	RLE/ZMBV codec state with no natural banner-aligned seams.  Splitting
//	would scatter related codec state across files for negligible
//	maintainability win.  Intentionally left intact.
//	Copyright (C) 2008-2026 Avery Lee
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
//	This file contains the platform-portable portions of videowriter.cpp:
//	- IATVideoEncoder / ATVideoEncoderRaw / ATVideoEncoderRLE / ATVideoEncoderZMBV
//	- IATMediaEncoder / ATAVIEncoder
//	- ATVideoWriter (the orchestrator that wires GTIA video tap + audio tap to AVI)
//
//	The Windows Media Foundation encoder (WMV/H.264) is excluded — only AVI-based
//	encodings (Raw, RLE, ZMBV) are available on the SDL3 build.

#include <stdafx.h>

#include <numeric>

#if defined(VD_CPU_X86) || defined(VD_CPU_AMD64)
	#include <emmintrin.h>
#endif

#include <vd2/system/cpuaccel.h>
#include <vd2/system/error.h>
#include <vd2/system/fraction.h>
#include <vd2/system/int128.h>
#include <vd2/system/math.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/zip.h>
#include <vd2/Kasumi/blitter.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/Kasumi/resample.h>
#include <at/ataudio/audiofilters.h>
#include <at/ataudio/audiooutput.h>

#if defined(VD_CPU_X86) || defined(VD_CPU_AMD64)
	#include <at/atcore/intrin_sse2.h>
#elif defined(VD_CPU_ARM64)
	#include <at/atcore/intrin_neon.h>
#endif

#include <at/atio/wav.h>
#include <vd2/system/text.h>
#include "videowriter.h"
#include "aviwriter.h"
#include "gtia.h"
#include "uirender.h"

#ifdef ALTIRRA_ENABLE_FFMPEG_RECORDING
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#endif

///////////////////////////////////////////////////////////////////////////////
// IATVideoEncoder — base interface for video frame compression

class IATVideoEncoder {
public:
	virtual ~IATVideoEncoder() {}

	virtual void Compress(const VDPixmap& px, bool intra, bool encodeAll) = 0;

	virtual uint32 GetEncodedLength() const = 0;
	virtual const void *GetEncodedData() const = 0;

	virtual bool GetDebugInfo(ATVideoRecordingDebugInfo& debugInfo) { return false; }
};

///////////////////////////////////////////////////////////////////////////////
// ATVideoEncoderRaw — uncompressed RGB/indexed AVI frames

class ATVideoEncoderRaw : public IATVideoEncoder {
public:
	ATVideoEncoderRaw(uint32 w, uint32 h, int format);
	void Compress(const VDPixmap& px, bool intra, bool encodeAll);

	uint32 GetEncodedLength() const { return mEncodedLength; }
	const void *GetEncodedData() const { return mBuffer.data(); }

protected:
	vdfastvector<uint8, vdaligned_alloc<uint8> > mBuffer;
	vdfastvector<uint8, vdaligned_alloc<uint8> > mBufferRef;
	VDPixmapLayout mLayout;
	uint32 mEncodedLength;
};

ATVideoEncoderRaw::ATVideoEncoderRaw(uint32 w, uint32 h, int format) {
	uint32 size = VDPixmapCreateLinearLayout(mLayout, format, w, h, 4);
	VDPixmapLayoutFlipV(mLayout);

	mBuffer.resize(size);
	mBufferRef.resize(size);
}

void ATVideoEncoderRaw::Compress(const VDPixmap& px, bool intra, bool encodeAll) {
	mBufferRef.swap(mBuffer);

	VDPixmap pxbuf = VDPixmapFromLayout(mLayout, mBuffer.data());
	VDPixmap pxref = VDPixmapFromLayout(mLayout, mBufferRef.data());

	VDPixmapBlt(pxbuf, px);

	if (!intra && !encodeAll) {
		const uint8 *src = (const uint8 *)pxbuf.data;
		const uint8 *ref = (const uint8 *)pxref.data;
		const uint32 w = pxbuf.w;
		const uint32 h = pxbuf.h;
		const uint32 bpr = mLayout.format == nsVDPixmap::kPixFormat_RGB888 ? 3*w : w;

		mEncodedLength = 0;
		for(uint32 y=0; y<h; ++y) {
			if (memcmp(src, ref, bpr)) {
				mEncodedLength = (uint32)mBuffer.size();
				break;
			}

			src += pxbuf.pitch;
			ref += pxbuf.pitch;
		}
	} else {
		mEncodedLength = (uint32)mBuffer.size();
	}
}

///////////////////////////////////////////////////////////////////////////////
// ATVideoEncoderRLE — Run-Length Encoding for 8-bit indexed AVI

class ATVideoEncoderRLE : public IATVideoEncoder {
public:
	ATVideoEncoderRLE(uint32 w, uint32 h);
	void Compress(const VDPixmap& px, bool intra, bool encodeAll);

	uint32 GetEncodedLength() const { return mEncodedLength; }
	const void *GetEncodedData() const { return mPackBuffer.data(); }

protected:
	void CompressIntra8();
	void CompressInter8(bool encodeAll);

	uint32 mWidth;
	uint32 mHeight;
	uint32 mEncodedLength;

	vdfastvector<uint8> mPackBuffer;
	VDPixmapBuffer	mBuffer;
	VDPixmapBuffer	mBufferRef;
};

ATVideoEncoderRLE::ATVideoEncoderRLE(uint32 w, uint32 h) {
	mWidth = w;
	mHeight = h;

	mPackBuffer.resize(w * h * 2);

	VDPixmapLayout layout;
	VDPixmapCreateLinearLayout(layout, nsVDPixmap::kPixFormat_Pal8, w, h, 16);
	mBuffer.init(layout);
	mBufferRef.init(layout);
}

void ATVideoEncoderRLE::Compress(const VDPixmap& px, bool intra, bool encodeAll) {
	mBuffer.swap(mBufferRef);
	VDPixmapBlt(mBuffer, px);

	if (intra)
		CompressIntra8();
	else
		CompressInter8(encodeAll);
}

void ATVideoEncoderRLE::CompressIntra8() {
	uint8 *dst0 = mPackBuffer.data();
	uint8 *dst = dst0;

	const uint32 w = mWidth;
	const uint32 h = mHeight;
	const uint8 *src = (const uint8 *)mBuffer.data + mBuffer.pitch * (h - 1);

	for(uint32 y = 0; y < h; ++y) {
		uint32 x = 0;

		// check if we can skip the scan line
		while(x < w) {
			uint32 x2 = x;
			bool rle = false;

			while(x2 < w) {
				if (src[x2] == src[x2+1] && src[x2+1] == src[x2+2] && x2 + 2 < w) {
					rle = true;
					break;
				}

				++x2;
			}

			uint32 literalLen = x2 - x;
			if (literalLen) {
				if (literalLen < 3) {
					*dst++ = 1;
					*dst++ = src[x++];
					if (literalLen == 2) {
						*dst++ = 1;
						*dst++ = src[x++];
					}
				} else {
					while(literalLen) {
						uint32 tc = literalLen;
						if (tc > 255) {
							if (tc > 256)
								tc = 254;	// not an error - avoid wasting a byte
							else
								tc = 252;
						}

						literalLen -= tc;

						*dst++ = 0;
						*dst++ = (uint8)tc;
						memcpy(dst, &src[x], tc);
						dst += tc;
						x += tc;

						if (tc & 1)
							*dst++ = 0;
					}
				}
			}

			if (rle) {
				uint8 c = src[x2];

				x2 += 3;
				while(x2 < w && src[x2] == c)
					++x2;

				uint32 runLen = x2 - x;
				while(runLen) {
					uint32 tc = runLen;
					if (tc > 255) {
						if (tc > 256)
							tc = 254;	// not an error - avoid wasting a byte
						else
							tc = 252;
					}

					runLen -= tc;

					*dst++ = (uint8)tc;
					*dst++ = c;
				}

				x = x2;
			}
		}

		// write EOL or EOF
		*dst++ = 0;
		*dst++ = (y == h - 1) ? 1 : 0;

		src -= mBuffer.pitch;
	}

	// write frame
	mEncodedLength = (uint32)(dst - dst0);
}

void ATVideoEncoderRLE::CompressInter8(bool encodeAll) {
	uint8 *dst0 = mPackBuffer.data();
	uint8 *dst = dst0;

	const uint32 w = mWidth;
	const uint32 h = mHeight;
	const uint8 *src = (const uint8 *)mBuffer.data + mBuffer.pitch * (h - 1);
	const uint8 *ref = (const uint8 *)mBufferRef.data + mBufferRef.pitch * (h - 1);

	uint32 lastx = 0;
	uint32 lasty = 0;

	for(uint32 y = 0; y < h; ++y) {
		uint32 x = 0;
		uint32 xl = w;

		// determine right border
		while(xl > 0 && src[xl-1] == ref[xl - 1])
			--xl;

		// check if we can skip the scan line
		while(x < xl) {
			uint32 x2 = x;
			bool rle = false;
			bool copy = false;

			while(x2 < xl) {
				if (src[x2] == src[x2+1] && src[x2+1] == src[x2+2] && x2 + 2 < xl) {
					rle = true;
					break;
				}

				if (src[x2] == ref[x2] && (x2 + 1 >= xl || (src[x2+1] == ref[x2+1] && (x2 + 2 >= xl || src[x2+2] == ref[x2+2])))) {
					copy = true;
					break;
				}

				++x2;
			}

			uint32 literalLen = x2 - x;
			if ((literalLen || rle) && (y != lasty || x != lastx)) {
				// check if we need to encode an EOL
				if (x < lastx) {
					*dst++ = 0;
					*dst++ = 0;
					lastx = 0;
					++lasty;
				}

				// encode a skip
				while(x != lastx || y != lasty) {
					uint32 dx = x - lastx;
					uint32 dy = y - lasty;

					if (dx > 255)
						dx = 255;

					if (dy > 255)
						dy = 255;

					*dst++ = 0;
					*dst++ = 2;
					*dst++ = (uint8)dx;
					*dst++ = (uint8)dy;

					lastx += dx;
					lasty += dy;
				}
			}

			if (literalLen) {
				if (literalLen < 3) {
					*dst++ = 1;
					*dst++ = src[x++];
					if (literalLen == 2) {
						*dst++ = 1;
						*dst++ = src[x++];
					}
				} else {
					while(literalLen) {
						uint32 tc = literalLen;
						if (tc > 255) {
							if (tc > 256)
								tc = 254;	// not an error - avoid wasting a byte
							else
								tc = 252;
						}

						literalLen -= tc;

						*dst++ = 0;
						*dst++ = (uint8)tc;
						memcpy(dst, &src[x], tc);
						dst += tc;
						x += tc;

						if (tc & 1)
							*dst++ = 0;
					}
				}

				lastx = x;
			}

			if (rle) {
				uint8 c = src[x2];

				x2 += 3;
				while(x2 < xl && src[x2] == c)
					++x2;

				uint32 runLen = x2 - x;
				while(runLen) {
					uint32 tc = runLen;
					if (tc > 255) {
						if (tc > 256)
							tc = 254;	// not an error - avoid wasting a byte
						else
							tc = 252;
					}

					runLen -= tc;

					*dst++ = (uint8)tc;
					*dst++ = c;
				}

				lastx = x2;
				x = x2;
			} else if (copy) {
				x = x2;
				while(src[x] == ref[x] && x < xl)
					++x;
			}
		}

		src -= mBuffer.pitch;
		ref -= mBufferRef.pitch;
	}

	if (dst != dst0 || encodeAll) {
		// write EOF
		*dst++ = 0;
		*dst++ = 1;
	}

	mEncodedLength = (uint32)(dst - dst0);
}

///////////////////////////////////////////////////////////////////////////////
// ATVideoEncoderZMBV — Zipped Motion Block Vector (lossless, good compression)

class ATVideoEncoderZMBV : public IATVideoEncoder {
public:
	ATVideoEncoderZMBV(uint32 w, uint32 h, bool rgb32);
	void Compress(const VDPixmap& px, bool intra, bool encodeAll) override;

	uint32 GetEncodedLength() const override { return mEncodedLength; }
	const void *GetEncodedData() const override { return mPackBuffer.data() + mEncodedOffset; }

	bool GetDebugInfo(ATVideoRecordingDebugInfo& debugInfo) override;

protected:
	void CompressIntra8(const VDPixmap& px);
	void CompressInter8(bool encodeAll);

	uint32 mWidth = 0;
	uint32 mHeight = 0;
	bool mbRgb32 = false;
	uint32 mEncodedLength = 0;
	uint32 mEncodedOffset = 0;

	vdfastvector<uint8, vdaligned_alloc<uint8> > mPackBuffer;
	vdfastvector<uint8, vdaligned_alloc<uint8> > mBuffer;
	vdfastvector<uint8, vdaligned_alloc<uint8> > mBufferRef;

	struct MotionVector {
		sint8 x;
		sint8 y;

		bool operator==(const MotionVector& v) const {
			return !((x ^ v.x) | (y ^ v.y));
		}

		bool operator!=(const MotionVector& v) const {
			return !!((x ^ v.x) | (y ^ v.y));
		}

		MotionVector offset(sint8 dx, sint8 dy) const {
			return MotionVector { (sint8)(x+dx), (sint8)(y+dy) };
		}
	};

	vdfastvector<MotionVector> mVecBuffer;
	vdfastvector<MotionVector> mVecBufferPrev;

	VDPixmapLayout	mLayout;

	VDMemoryBufferStream mDeflateOutputBuffer;
	VDDeflateStream mDeflateStream;
};

ATVideoEncoderZMBV::ATVideoEncoderZMBV(uint32 w, uint32 h, bool rgb32)
	: mDeflateStream(mDeflateOutputBuffer, VDDeflateChecksumMode::None, VDDeflateCompressionLevel::Quick)
{
	mWidth = w;
	mHeight = h;
	mbRgb32 = rgb32;

	mPackBuffer.resize((rgb32 ? w * h * 8 : w * h * 2) + 15);

	mLayout.format = rgb32 ? nsVDPixmap::kPixFormat_XRGB8888 : nsVDPixmap::kPixFormat_Pal8;
	mLayout.w = w;
	mLayout.h = h;
	mLayout.palette = NULL;
	mLayout.pitch = (w + 47) & ~15;

	if (rgb32)
		mLayout.pitch *= 4;

	mLayout.data = mLayout.pitch * 16 + (rgb32 ? 64 : 16);
	mLayout.data2 = 0;
	mLayout.data3 = 0;
	mLayout.pitch2 = 0;
	mLayout.pitch3 = 0;

	uint32 size = (uint32)mLayout.pitch * (mLayout.h + 32);
	mBuffer.resize(size, 0);
	mBufferRef.resize(size, 0);

	uint32 blkw = (w + 15) >> 4;
	uint32 blkh = (h + 15) >> 4;

	MotionVector v0 = { 0, 0 };
	mVecBuffer.resize(blkw * (blkh + 1) + 1, v0);
	mVecBufferPrev.resize(blkw * (blkh + 1) + 1, v0);
}

void ATVideoEncoderZMBV::Compress(const VDPixmap& px, bool intra, bool encodeAll) {
	mBuffer.swap(mBufferRef);
	mVecBuffer.swap(mVecBufferPrev);

	const VDPixmap& pxdst = VDPixmapFromLayout(mLayout, mBuffer.data());
	VDPixmapBlt(pxdst, px);

	if (mbRgb32) {
		uint8 *dstrow = (uint8 *)pxdst.data;
		for(uint32 y = 0; y < mHeight; ++y) {
			uint8 *dst = dstrow;

			for(uint32 x = 0; x < mWidth; ++x) {
				dst[3] = dst[2];
				dst += 4;
			}

			dstrow += pxdst.pitch;
		}
	}

	if (intra)
		CompressIntra8(px);
	else
		CompressInter8(encodeAll);
}

bool ATVideoEncoderZMBV::GetDebugInfo(ATVideoRecordingDebugInfo& debugInfo) {
	debugInfo.mImageWidth = mWidth;
	debugInfo.mImageHeight = mHeight;
	debugInfo.mBlockWidth = 16;
	debugInfo.mBlockHeight = 16;
	debugInfo.mNumBlocksX = (mWidth + 15) >> 4;
	debugInfo.mNumBlocksY = (mHeight + 15) >> 4;

	debugInfo.mMotionVectors.resize(debugInfo.mNumBlocksX * debugInfo.mNumBlocksY);

	ATVideoRecordingDebugInfo::MotionVector *mvdst = debugInfo.mMotionVectors.data();
	const MotionVector *mvsrc = &mVecBuffer[debugInfo.mNumBlocksX + 1];

	for(uint32 y = 0; y < debugInfo.mNumBlocksY; ++y) {
		for(uint32 x = 0; x < debugInfo.mNumBlocksX; ++x) {
			mvdst->mX = mvsrc->x;
			mvdst->mY = mvsrc->y;
			++mvdst;
			++mvsrc;
		}
	}

	return true;
}

void ATVideoEncoderZMBV::CompressIntra8(const VDPixmap& px) {
	// header is 7 bytes, so add 1 byte to align everything nicely
	mEncodedOffset = 1;

	uint8 *dst0 = mPackBuffer.data() + mEncodedOffset;
	uint8 *dst = dst0;

	const uint32 w = mWidth;
	const uint32 h = mHeight;
	const uint8 *src = mBuffer.data() + mLayout.data;

	*dst++ = 0x01;	// intra
	*dst++ = 0x00;	// major
	*dst++ = 0x01;	// minor
	*dst++ = 0x01;	// zlib compressed
	*dst++ = mbRgb32 ? 0x08 : 0x04;	// 8-bit / 32-bit
	*dst++ = 16;	// 16x16 blocks
	*dst++ = 16;

	uint8 *base = dst;

	if (mbRgb32) {
		VDMemcpyRect(dst, w*4, src, mLayout.pitch, w * 4, h);
		dst += w * h * 4;
	} else {
		for(int i=0; i<256; ++i) {
			const uint32 c = px.palette[i];

			*dst++ = (uint8)(c >> 16);
			*dst++ = (uint8)(c >>  8);
			*dst++ = (uint8)(c >>  0);
		}

		VDMemcpyRect(dst, w, src, mLayout.pitch, w, h);
		dst += w * h;
	}

	// zlib compress frame
	static constexpr uint8 kZlibHeader[2] {
		0x78,	// 32K window, Deflate
		0xDA,	// maximum compression, no dictionary, check offset = 0x1A
	};

	mDeflateOutputBuffer.Clear();
	mDeflateOutputBuffer.Write(kZlibHeader, 2);

	mDeflateStream.Reset(VDDeflateCompressionLevel::Quick);
	mDeflateStream.Write(base, dst-base);
	mDeflateStream.FlushToByteBoundary();

	const auto zdata = mDeflateOutputBuffer.GetBuffer();
	size_t zdataLen = zdata.size();
	if (mPackBuffer.size() < zdataLen + 8) {
		mPackBuffer.resize(zdataLen + 8);

		base = mPackBuffer.data() + 8;
	}

	memcpy(base, zdata.data(), zdataLen);

	// write frame
	mEncodedLength = zdataLen + 7;
}

namespace {
	static const uint32 kMasks[28]={
		0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x000000ff, 0x0000ffff, 0x00ffffff, 0xffffffff,
		0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
		0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
		0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
	};

#if defined(VD_CPU_X86) || defined(VD_CPU_AMD64)
	int BlockDiff16_8_SSE2(const uint8 *src, const uint8 *ref, ptrdiff_t pitch, uint32 w, uint32 h) {
		static const VDALIGN(16) uint32 _m0[4] = { 0x55555555, 0x55555555, 0x55555555, 0x55555555 };
		static const VDALIGN(16) uint32 _m1[4] = { 0x33333333, 0x33333333, 0x33333333, 0x33333333 };
		static const VDALIGN(16) uint32 _m2[4] = { 0x0f0f0f0f, 0x0f0f0f0f, 0x0f0f0f0f, 0x0f0f0f0f };
		__m128i m0 = *(const __m128i *)_m0;
		__m128i m1 = *(const __m128i *)_m1;
		__m128i m2 = *(const __m128i *)_m2;
		__m128i zero = _mm_setzero_si128();
		__m128i err = zero;

		for(uint32 y=0; y<h; ++y) {
			__m128i a = *(__m128i *)src;
			__m128i b0 = _mm_loadl_epi64((const __m128i *)ref);
			__m128i b1 = _mm_loadl_epi64((const __m128i *)(ref + 8));
			__m128i b = _mm_unpacklo_epi64(b0, b1);
			__m128i e = _mm_xor_si128(a, b);

			e = _mm_sub_epi8(e, _mm_and_si128(_mm_srli_epi16(e, 1), m0));
			e = _mm_add_epi8(_mm_and_si128(e, m1), _mm_and_si128(_mm_srli_epi16(e, 2), m1));
			e = _mm_add_epi8(_mm_and_si128(e, m2), _mm_and_si128(_mm_srli_epi16(e, 4), m2));
			err = _mm_add_epi8(e, err);

			ref += pitch;
			src += pitch;
		}

		err = _mm_sad_epu8(err, zero);
		err = _mm_add_epi32(err, _mm_srli_si128(err, 8));

		return _mm_cvtsi128_si32(err);
	}

	int BlockDiff16_32_SSE2(const uint8 *src, const uint8 *ref, ptrdiff_t pitch, uint32 w, uint32 h) {
		static const VDALIGN(16) uint32 _m0[4] = { 0x55555555, 0x55555555, 0x55555555, 0x55555555 };
		static const VDALIGN(16) uint32 _m1[4] = { 0x33333333, 0x33333333, 0x33333333, 0x33333333 };
		static const VDALIGN(16) uint32 _m2[4] = { 0x000f0f0f, 0x000f0f0f, 0x000f0f0f, 0x000f0f0f };	// not an error - drop dummy alpha
		__m128i m0 = *(const __m128i *)_m0;
		__m128i m1 = *(const __m128i *)_m1;
		__m128i m2 = *(const __m128i *)_m2;
		__m128i zero = _mm_setzero_si128();
		__m128i err = zero;

		for(uint32 y=0; y<h; ++y) {
			__m128i a0 = *(__m128i *)(src + 0);
			__m128i a1 = *(__m128i *)(src + 16);
			__m128i a2 = *(__m128i *)(src + 32);
			__m128i a3 = *(__m128i *)(src + 48);
			__m128i b0 = _mm_unpacklo_epi64(_mm_loadl_epi64((const __m128i *)(ref +  0)), _mm_loadl_epi64((const __m128i *)(ref +  8)));
			__m128i b1 = _mm_unpacklo_epi64(_mm_loadl_epi64((const __m128i *)(ref + 16)), _mm_loadl_epi64((const __m128i *)(ref + 24)));
			__m128i b2 = _mm_unpacklo_epi64(_mm_loadl_epi64((const __m128i *)(ref + 32)), _mm_loadl_epi64((const __m128i *)(ref + 40)));
			__m128i b3 = _mm_unpacklo_epi64(_mm_loadl_epi64((const __m128i *)(ref + 48)), _mm_loadl_epi64((const __m128i *)(ref + 56)));
			__m128i e0 = _mm_xor_si128(a0, b0);
			__m128i e1 = _mm_xor_si128(a1, b1);
			__m128i e2 = _mm_xor_si128(a2, b2);
			__m128i e3 = _mm_xor_si128(a3, b3);

			e0 = _mm_sub_epi8(e0, _mm_and_si128(m0, _mm_srli_epi16(e0, 1)));
			e1 = _mm_sub_epi8(e1, _mm_and_si128(m0, _mm_srli_epi16(e1, 1)));
			e2 = _mm_sub_epi8(e2, _mm_and_si128(m0, _mm_srli_epi16(e2, 1)));
			e3 = _mm_sub_epi8(e3, _mm_and_si128(m0, _mm_srli_epi16(e3, 1)));

			e0 = _mm_add_epi8(_mm_and_si128(m1, e0), _mm_and_si128(_mm_srli_epi16(e0, 2), m1));
			e1 = _mm_add_epi8(_mm_and_si128(m1, e1), _mm_and_si128(_mm_srli_epi16(e1, 2), m1));
			e2 = _mm_add_epi8(_mm_and_si128(m1, e2), _mm_and_si128(_mm_srli_epi16(e2, 2), m1));
			e3 = _mm_add_epi8(_mm_and_si128(m1, e3), _mm_and_si128(_mm_srli_epi16(e3, 2), m1));

			e0 = _mm_add_epi8(_mm_and_si128(m2, e0), _mm_and_si128(_mm_srli_epi16(e0, 4), m2));
			e1 = _mm_add_epi8(_mm_and_si128(m2, e1), _mm_and_si128(_mm_srli_epi16(e1, 4), m2));
			e2 = _mm_add_epi8(_mm_and_si128(m2, e2), _mm_and_si128(_mm_srli_epi16(e2, 4), m2));
			e3 = _mm_add_epi8(_mm_and_si128(m2, e3), _mm_and_si128(_mm_srli_epi16(e3, 4), m2));

			__m128i e = _mm_adds_epu8(_mm_adds_epu8(e0, e1), _mm_adds_epu8(e2, e3));

			e = _mm_sad_epu8(e, zero);

			err = _mm_add_epi32(err, e);

			ref += pitch;
			src += pitch;
		}

		err = _mm_add_epi32(err, _mm_srli_si128(err, 8));

		return _mm_cvtsi128_si32(err);
	}
#endif

#if defined(VD_CPU_ARM64)
	int BlockDiff16_8_NEON(const uint8 *src, const uint8 *ref, ptrdiff_t pitch, uint32 w, uint32 h) {

		if (h == 16) {
			uint16x8_t err1;
			uint16x8_t err2;

			const ptrdiff_t pitch2 = pitch + pitch;

			err1 = vpaddlq_u8(vcntq_u8(veorq_u8(vld1q_u8(src        ), vld1q_u8(ref        ))));
			err2 = vpaddlq_u8(vcntq_u8(veorq_u8(vld1q_u8(src + pitch), vld1q_u8(ref + pitch)))); src += pitch2; ref += pitch2;
			err1 = vpadalq_u8(err1, vcntq_u8(veorq_u8(vld1q_u8(src        ), vld1q_u8(ref        ))));
			err2 = vpadalq_u8(err2, vcntq_u8(veorq_u8(vld1q_u8(src + pitch), vld1q_u8(ref + pitch)))); src += pitch2; ref += pitch2;
			err1 = vpadalq_u8(err1, vcntq_u8(veorq_u8(vld1q_u8(src        ), vld1q_u8(ref        ))));
			err2 = vpadalq_u8(err2, vcntq_u8(veorq_u8(vld1q_u8(src + pitch), vld1q_u8(ref + pitch)))); src += pitch2; ref += pitch2;
			err1 = vpadalq_u8(err1, vcntq_u8(veorq_u8(vld1q_u8(src        ), vld1q_u8(ref        ))));
			err2 = vpadalq_u8(err2, vcntq_u8(veorq_u8(vld1q_u8(src + pitch), vld1q_u8(ref + pitch)))); src += pitch2; ref += pitch2;
			err1 = vpadalq_u8(err1, vcntq_u8(veorq_u8(vld1q_u8(src        ), vld1q_u8(ref        ))));
			err2 = vpadalq_u8(err2, vcntq_u8(veorq_u8(vld1q_u8(src + pitch), vld1q_u8(ref + pitch)))); src += pitch2; ref += pitch2;
			err1 = vpadalq_u8(err1, vcntq_u8(veorq_u8(vld1q_u8(src        ), vld1q_u8(ref        ))));
			err2 = vpadalq_u8(err2, vcntq_u8(veorq_u8(vld1q_u8(src + pitch), vld1q_u8(ref + pitch)))); src += pitch2; ref += pitch2;
			err1 = vpadalq_u8(err1, vcntq_u8(veorq_u8(vld1q_u8(src        ), vld1q_u8(ref        ))));
			err2 = vpadalq_u8(err2, vcntq_u8(veorq_u8(vld1q_u8(src + pitch), vld1q_u8(ref + pitch)))); src += pitch2; ref += pitch2;
			err1 = vpadalq_u8(err1, vcntq_u8(veorq_u8(vld1q_u8(src        ), vld1q_u8(ref        ))));
			err2 = vpadalq_u8(err2, vcntq_u8(veorq_u8(vld1q_u8(src + pitch), vld1q_u8(ref + pitch)))); src += pitch2; ref += pitch2;

			return vaddvq_u16(vaddq_u16(err1, err2));
		} else {
			uint16x8_t err = vmovq_n_u16(0);

			for(uint32 y=0; y<h; ++y) {
				uint8x16_t a = vld1q_u8(src);
				uint8x16_t b = vld1q_u8(ref);
				uint8x16_t e = vcntq_u8(veorq_u8(a, b));

				err = vpadalq_u8(err, e);

				ref += pitch;
				src += pitch;
			}

			return vaddvq_u16(err);
		}
	}

	int BlockDiff16_32_NEON(const uint8 *src, const uint8 *ref, ptrdiff_t pitch, uint32 w, uint32 h) {
		uint16x8_t err = vmovq_n_u16(0);

		for(uint32 y=0; y<h; ++y) {
			uint8x16x4_t a = vld4q_u8(src);
			uint8x16x4_t b = vld4q_u8(ref);
			uint8x16_t e0 = vcntq_u8(veorq_u8(a.val[0], b.val[0]));
			uint8x16_t e1 = vcntq_u8(veorq_u8(a.val[1], b.val[1]));
			uint8x16_t e2 = vcntq_u8(veorq_u8(a.val[2], b.val[2]));

			err = vpadalq_u8(err, e0);
			err = vpadalq_u8(err, e1);
			err = vpadalq_u8(err, e2);

			ref += pitch;
			src += pitch;
		}

		return vaddvq_u16(err);
	}
#endif

	int BlockDiff_8(const uint8 *src, const uint8 *ref, ptrdiff_t pitch, uint32 w, uint32 h) {
		int err = 0;
		uint32 diff;
		uint32 mask0 = kMasks[w + 11];
		uint32 mask1 = kMasks[w + 7];
		uint32 mask2 = kMasks[w + 3];
		uint32 mask3 = kMasks[w - 1];

		for(uint32 y=0; y<h; ++y) {
			diff = (*(const uint32 *)&src[ 0] ^ *(const uint32 *)&ref[ 0]) & mask0;
			diff -= (diff >> 1) & 0x55555555;
			diff = ((diff & 0xcccccccc) >> 2) + (diff & 0x33333333);
			diff = (diff + (diff >> 4)) & 0x0f0f0f0f;
			err += (diff * 0x01010101) >> 24;

			diff = (*(const uint32 *)&src[ 4] ^ *(const uint32 *)&ref[ 4]) & mask1;
			diff -= (diff >> 1) & 0x55555555;
			diff = ((diff & 0xcccccccc) >> 2) + (diff & 0x33333333);
			diff = (diff + (diff >> 4)) & 0x0f0f0f0f;
			err += (diff * 0x01010101) >> 24;

			diff = (*(const uint32 *)&src[ 8] ^ *(const uint32 *)&ref[ 8]) & mask2;
			diff -= (diff >> 1) & 0x55555555;
			diff = ((diff & 0xcccccccc) >> 2) + (diff & 0x33333333);
			diff = (diff + (diff >> 4)) & 0x0f0f0f0f;
			err += (diff * 0x01010101) >> 24;

			diff = (*(const uint32 *)&src[12] ^ *(const uint32 *)&ref[12]) & mask3;
			diff -= (diff >> 1) & 0x55555555;
			diff = ((diff & 0xcccccccc) >> 2) + (diff & 0x33333333);
			diff = (diff + (diff >> 4)) & 0x0f0f0f0f;
			err += (diff * 0x01010101) >> 24;

			ref += pitch;
			src += pitch;
		}

		return err;
	}

	int BlockDiff_32(const uint8 *src, const uint8 *ref, ptrdiff_t pitch, uint32 w, uint32 h) {
		int err = 0;
		uint32 diff;
		uint32 bytes4 = w*4;

		for(uint32 y=0; y<h; ++y) {
			for(uint32 x=0; x<bytes4; x+=4) {
				diff = (*(const uint32 *)&src[x] ^ *(const uint32 *)&ref[x]);
				diff -= (diff >> 1) & 0x55555555;
				diff = ((diff & 0xcccccccc) >> 2) + (diff & 0x33333333);
				diff = (diff + (diff >> 4)) & 0x000f0f0f;		// not an error - drop dummy alpha
				err += (diff * 0x01010101) >> 24;
			}

			ref += pitch;
			src += pitch;
		}

		return err;
	}

	int BlockDiff16_8(const uint8 *src, const uint8 *ref, ptrdiff_t pitch, uint32 w, uint32 h) {
		int err = 0;
		uint32 diff;

		for(uint32 y=0; y<h; ++y) {
			diff = *(const uint32 *)&src[ 0] ^ *(const uint32 *)&ref[ 0];
			diff -= (diff >> 1) & 0x55555555;
			diff = ((diff & 0xcccccccc) >> 2) + (diff & 0x33333333);
			diff = (diff + (diff >> 4)) & 0x0f0f0f0f;
			err += (diff * 0x01010101) >> 24;

			diff = *(const uint32 *)&src[ 4] ^ *(const uint32 *)&ref[ 4];
			diff -= (diff >> 1) & 0x55555555;
			diff = ((diff & 0xcccccccc) >> 2) + (diff & 0x33333333);
			diff = (diff + (diff >> 4)) & 0x0f0f0f0f;
			err += (diff * 0x01010101) >> 24;

			diff = *(const uint32 *)&src[ 8] ^ *(const uint32 *)&ref[ 8];
			diff -= (diff >> 1) & 0x55555555;
			diff = ((diff & 0xcccccccc) >> 2) + (diff & 0x33333333);
			diff = (diff + (diff >> 4)) & 0x0f0f0f0f;
			err += (diff * 0x01010101) >> 24;

			diff = *(const uint32 *)&src[12] ^ *(const uint32 *)&ref[12];
			diff -= (diff >> 1) & 0x55555555;
			diff = ((diff & 0xcccccccc) >> 2) + (diff & 0x33333333);
			diff = (diff + (diff >> 4)) & 0x0f0f0f0f;
			err += (diff * 0x01010101) >> 24;

			ref += pitch;
			src += pitch;
		}

		return err;
	}

	int BlockDiff16_32(const uint8 *src, const uint8 *ref, ptrdiff_t pitch, uint32 w, uint32 h) {
		int err = 0;
		uint32 diff;

		for(uint32 y=0; y<h; ++y) {
			for(uint32 x=0; x<64; x+=4) {
				diff = *(const uint32 *)&src[x] ^ *(const uint32 *)&ref[x];
				diff -= (diff >> 1) & 0x55555555;
				diff = ((diff & 0xcccccccc) >> 2) + (diff & 0x33333333);
				diff = (diff + (diff >> 4)) & 0x000f0f0f;
				err += (diff * 0x01010101) >> 24;
			}

			ref += pitch;
			src += pitch;
		}

		return err;
	}

	void ComputeXor(uint8 *dst, const uint8 *src, const uint8 *ref, ptrdiff_t pitch, uint32 w, uint32 h) {
		for(uint32 y=0; y<h; ++y) {
			for(uint32 x=0; x<w; ++x)
				*dst++ = src[x] ^ ref[x];

			src += pitch;
			ref += pitch;
		}
	}

	void ComputeXor16_8(uint8 *dst, const uint8 *src, const uint8 *ref, ptrdiff_t pitch, uint32 w, uint32 h) {
		for(uint32 y=0; y<h; ++y) {
			*(uint32 *)&dst[ 0] = *(const uint32 *)&src[ 0] ^ *(const uint32 *)&ref[ 0];
			*(uint32 *)&dst[ 4] = *(const uint32 *)&src[ 4] ^ *(const uint32 *)&ref[ 4];
			*(uint32 *)&dst[ 8] = *(const uint32 *)&src[ 8] ^ *(const uint32 *)&ref[ 8];
			*(uint32 *)&dst[12] = *(const uint32 *)&src[12] ^ *(const uint32 *)&ref[12];

			dst += 16;
			src += pitch;
			ref += pitch;
		}
	}

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
	void ComputeXor16_8_SSE2(uint8 *dst, const uint8 *src, const uint8 *ref, ptrdiff_t pitch, uint32 w, uint32 h) {
		for(uint32 y=0; y<h; ++y) {
			_mm_storeu_si128((__m128i *)(dst +  0), _mm_xor_si128(_mm_loadu_si128((const __m128i *)(src +  0)), _mm_loadu_si128((const __m128i *)(ref +  0))));

			dst += 16;
			src += pitch;
			ref += pitch;
		}
	}
#endif

#ifdef VD_CPU_ARM64
	void ComputeXor16_8_NEON(uint8 *dst, const uint8 *src, const uint8 *ref, ptrdiff_t pitch, uint32 w, uint32 h) {
		for(uint32 y=0; y<h; ++y) {
			vst1q_u8(dst, veorq_u8(vld1q_u8(src), vld1q_u8(ref)));

			dst += 16;
			src += pitch;
			ref += pitch;
		}
	}
#endif

	void ComputeXor16_32(uint8 *dst, const uint8 *src, const uint8 *ref, ptrdiff_t pitch, uint32 w, uint32 h) {
		for(uint32 y=0; y<h; ++y) {
			*(uint32 *)&dst[ 0] = (*(const uint32 *)&src[ 0] ^ *(const uint32 *)&ref[ 0]) & 0x00ffffff;
			*(uint32 *)&dst[ 4] = (*(const uint32 *)&src[ 4] ^ *(const uint32 *)&ref[ 4]) & 0x00ffffff;
			*(uint32 *)&dst[ 8] = (*(const uint32 *)&src[ 8] ^ *(const uint32 *)&ref[ 8]) & 0x00ffffff;
			*(uint32 *)&dst[12] = (*(const uint32 *)&src[12] ^ *(const uint32 *)&ref[12]) & 0x00ffffff;
			*(uint32 *)&dst[16] = (*(const uint32 *)&src[16] ^ *(const uint32 *)&ref[16]) & 0x00ffffff;
			*(uint32 *)&dst[20] = (*(const uint32 *)&src[20] ^ *(const uint32 *)&ref[20]) & 0x00ffffff;
			*(uint32 *)&dst[24] = (*(const uint32 *)&src[24] ^ *(const uint32 *)&ref[24]) & 0x00ffffff;
			*(uint32 *)&dst[28] = (*(const uint32 *)&src[28] ^ *(const uint32 *)&ref[28]) & 0x00ffffff;
			*(uint32 *)&dst[32] = (*(const uint32 *)&src[32] ^ *(const uint32 *)&ref[32]) & 0x00ffffff;
			*(uint32 *)&dst[36] = (*(const uint32 *)&src[36] ^ *(const uint32 *)&ref[36]) & 0x00ffffff;
			*(uint32 *)&dst[40] = (*(const uint32 *)&src[40] ^ *(const uint32 *)&ref[40]) & 0x00ffffff;
			*(uint32 *)&dst[44] = (*(const uint32 *)&src[44] ^ *(const uint32 *)&ref[44]) & 0x00ffffff;
			*(uint32 *)&dst[48] = (*(const uint32 *)&src[48] ^ *(const uint32 *)&ref[48]) & 0x00ffffff;
			*(uint32 *)&dst[52] = (*(const uint32 *)&src[52] ^ *(const uint32 *)&ref[52]) & 0x00ffffff;
			*(uint32 *)&dst[56] = (*(const uint32 *)&src[56] ^ *(const uint32 *)&ref[56]) & 0x00ffffff;
			*(uint32 *)&dst[60] = (*(const uint32 *)&src[60] ^ *(const uint32 *)&ref[60]) & 0x00ffffff;

			dst += 64;
			src += pitch;
			ref += pitch;
		}
	}

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
	void ComputeXor16_32_SSE2(uint8 *dst, const uint8 *src, const uint8 *ref, ptrdiff_t pitch, uint32 w, uint32 h) {
		__m128i xorMask = _mm_set1_epi32(0x00FFFFFF);

		for(uint32 y=0; y<h; ++y) {
			_mm_storeu_si128((__m128i *)(dst +  0), _mm_and_si128(_mm_xor_si128(_mm_loadu_si128((const __m128i *)(src +  0)), _mm_loadu_si128((const __m128i *)(ref +  0))), xorMask));
			_mm_storeu_si128((__m128i *)(dst + 16), _mm_and_si128(_mm_xor_si128(_mm_loadu_si128((const __m128i *)(src + 16)), _mm_loadu_si128((const __m128i *)(ref + 16))), xorMask));
			_mm_storeu_si128((__m128i *)(dst + 32), _mm_and_si128(_mm_xor_si128(_mm_loadu_si128((const __m128i *)(src + 32)), _mm_loadu_si128((const __m128i *)(ref + 32))), xorMask));
			_mm_storeu_si128((__m128i *)(dst + 48), _mm_and_si128(_mm_xor_si128(_mm_loadu_si128((const __m128i *)(src + 48)), _mm_loadu_si128((const __m128i *)(ref + 48))), xorMask));

			dst += 64;
			src += pitch;
			ref += pitch;
		}
	}
#endif

#ifdef VD_CPU_ARM64
	void ComputeXor16_32_NEON(uint8 *dst, const uint8 *src, const uint8 *ref, ptrdiff_t pitch, uint32 w, uint32 h) {
		uint8x16_t xorMask = vreinterpretq_u8_u32(vdupq_n_u32(0x00FFFFFF));

		for(uint32 y=0; y<h; ++y) {
			vst1q_u8(dst +  0, vandq_u8(xorMask, veorq_u8(vld1q_u8(src +  0), vld1q_u8(ref +  0))));
			vst1q_u8(dst + 16, vandq_u8(xorMask, veorq_u8(vld1q_u8(src + 16), vld1q_u8(ref + 16))));
			vst1q_u8(dst + 32, vandq_u8(xorMask, veorq_u8(vld1q_u8(src + 32), vld1q_u8(ref + 32))));
			vst1q_u8(dst + 48, vandq_u8(xorMask, veorq_u8(vld1q_u8(src + 48), vld1q_u8(ref + 48))));

			dst += 64;
			src += pitch;
			ref += pitch;
		}
	}
#endif
}

void ATVideoEncoderZMBV::CompressInter8(bool encodeAll) {
	const uint32 w = mWidth;
	const uint32 h = mHeight;
	const uint32 bw = (w + 15) >> 4;
	const uint32 bh = (h + 15) >> 4;
	const uint32 bcount = bw * bh;

	mEncodedOffset = (0 - (1 + 2*(bcount + (bcount & 1)))) & 7;

	uint8 *dst0 = mPackBuffer.data() + mEncodedOffset;
	uint8 *dst = dst0;

	const uint8 *src = mBuffer.data() + mLayout.data;
	const uint8 *ref = mBufferRef.data() + mLayout.data;

	const uint32 bxedge = w >> 4;
	const uint32 byedge = h >> 4;

	*dst++ = 0x00;	// inter

	uint8 *base = dst;

	uint8 *blkdst = dst;
	dst += bcount*2;

	if (bcount & 1) {
		*dst++ = 0;
		*dst++ = 0;
	}

	MotionVector *mvp = mVecBufferPrev.data() + bw + 1;
	MotionVector *mvc = mVecBuffer.data() + bw + 1;
	MotionVector mvcand[16];
	const ptrdiff_t pitch = mLayout.pitch;
	bool delta = false;

	const bool rgb32 = mbRgb32;
	int (*blockDiff)(const uint8 *, const uint8 *, ptrdiff_t, uint32, uint32) = rgb32 ? BlockDiff_32 : BlockDiff_8;
	int (*blockDiff16)(const uint8 *, const uint8 *, ptrdiff_t, uint32, uint32) = rgb32 ? BlockDiff16_32 : BlockDiff16_8;
	void (*computeXor)(uint8 *, const uint8 *, const uint8 *, ptrdiff_t, uint32, uint32) = ComputeXor;
	void (*computeXor16)(uint8 *, const uint8 *, const uint8 *, ptrdiff_t, uint32, uint32) = rgb32 ? ComputeXor16_32 : ComputeXor16_8;

#if defined(VD_CPU_X86) || defined(VD_CPU_AMD64)
	if (SSE2_enabled) {
		blockDiff16 = rgb32 ? BlockDiff16_32_SSE2 : BlockDiff16_8_SSE2;
		computeXor16 = rgb32 ? ComputeXor16_32_SSE2 : ComputeXor16_8_SSE2;
	}
#elif defined(VD_CPU_ARM64)
	blockDiff16 = rgb32 ? BlockDiff16_32_NEON : BlockDiff16_8_NEON;
	computeXor16 = rgb32 ? ComputeXor16_32_NEON : ComputeXor16_8_NEON;
#endif

	for(uint32 by = 0; by < bh; ++by) {
		const uint8 *src2 = src;
		const uint8 *ref2 = ref;
		const uint32 blockh = (by == byedge) ? h & 15 : 16;

		for(uint32 bx = 0; bx < bw; ++bx) {
			const uint32 blockw = (bx == bxedge) ? w & 15 : 16;
			int (*bd)(const uint8 *, const uint8 *, ptrdiff_t, uint32, uint32) = (blockw == 16) ? blockDiff16 : blockDiff;
			MotionVector mvbest = {0, 0};
			int errbest = bd(src2, ref2, pitch, blockw, blockh);

			if (errbest) {
				int mvn = 0;
				mvcand[mvn++] = mvc[-1];
				mvcand[mvn++] = mvc[-(int)bw];
				mvcand[mvn++] = mvp[0];

				uint8 triedMasks[33*5] = {0};

				for(int pass = 0; pass < 20; ++pass) {
					bool improved = false;

					for(int i=0; i<mvn && errbest; ++i) {
						const MotionVector& mv = mvcand[i];

						if (abs(mv.x) > 16 || abs(mv.y) > 16)
							continue;

						int idx = (mv.y + 16) + ((unsigned)(mv.x + 16) >> 3)*5;
						uint8 bit = 1 << ((mv.x + 16) & 7);
						if (triedMasks[idx] & bit)
							continue;

						triedMasks[idx] |= bit;

						int err = bd(src2, ref2 + mv.y * pitch + (rgb32 ? mv.x*4 : mv.x), pitch, blockw, blockh);

						if (err < errbest) {
							mvbest = mv;
							errbest = err;
							improved = true;
						}
					}

					if (!errbest || (pass && !improved))
						break;

					mvn = 0;
					mvcand[mvn++] = mvbest.offset(-1,0);
					mvcand[mvn++] = mvbest.offset(+1,0);
					mvcand[mvn++] = mvbest.offset(0,-1);
					mvcand[mvn++] = mvbest.offset(0,+1);
					mvcand[mvn++] = mvbest.offset(-1,-1);
					mvcand[mvn++] = mvbest.offset(+1,-1);
					mvcand[mvn++] = mvbest.offset(-1,+1);
					mvcand[mvn++] = mvbest.offset(+1,+1);
					mvcand[mvn++] = mvbest.offset(-2,0);
					mvcand[mvn++] = mvbest.offset(+2,0);
					mvcand[mvn++] = mvbest.offset(0,-2);
					mvcand[mvn++] = mvbest.offset(0,+2);
				}
			}

			if (errbest) {
				blkdst[0] = mvbest.x + mvbest.x + 1;
				blkdst[1] = mvbest.y + mvbest.y;

				if (rgb32) {
					if (blockw == 16)
						computeXor16(dst, src2, ref2 + mvbest.y * pitch + mvbest.x*4, pitch, blockw*4, blockh);
					else
						computeXor(dst, src2, ref2 + mvbest.y * pitch + mvbest.x*4, pitch, blockw*4, blockh);

					dst += blockw*blockh*4;
				} else {
					if (blockw == 16)
						computeXor16(dst, src2, ref2 + mvbest.y * pitch + mvbest.x, pitch, blockw, blockh);
					else
						computeXor(dst, src2, ref2 + mvbest.y * pitch + mvbest.x, pitch, blockw, blockh);

					dst += blockw*blockh;
				}

			} else {
				blkdst[0] = mvbest.x + mvbest.x;
				blkdst[1] = mvbest.y + mvbest.y;
			}

			mvc[0] = mvbest;

			if (mvbest.x || mvbest.y || errbest)
				delta = true;

			if (rgb32) {
				src2 += 64;
				ref2 += 64;
			} else {
				src2 += 16;
				ref2 += 16;
			}

			blkdst += 2;
			++mvp;
			++mvc;
		}

		src += mLayout.pitch * 16;
		ref += mLayout.pitch * 16;
	}

	if (!delta && !encodeAll) {
		mEncodedLength = 0;
		return;
	}

	// zlib compress frame
	mDeflateOutputBuffer.Clear();
	mDeflateStream.Write(base, dst - base);
	mDeflateStream.FlushToByteBoundary();

	const auto zdata = mDeflateOutputBuffer.GetBuffer();
	size_t zdataLen = zdata.size();
	if (mPackBuffer.size() - (mEncodedOffset + 1) < zdataLen) {
		mPackBuffer.resize(zdataLen + mEncodedOffset + 1);

		base = mPackBuffer.data() + mEncodedOffset + 1;
	}

	memcpy(base, zdata.data(), zdataLen);

	// write frame
	mEncodedLength = zdataLen + 1;
}

///////////////////////////////////////////////////////////////////////////////
// IATMediaEncoder — interface between ATVideoWriter and container format

class IATMediaEncoder {
public:
	virtual ~IATMediaEncoder() = default;

	virtual sint64 GetCurrentSize() = 0;
	virtual bool GetDebugInfo(ATVideoRecordingDebugInfo& debugInfo) { return false; }

	virtual void WriteVideo(const VDPixmap& px) = 0;
	virtual void BeginAudioFrame(uint32 bytes, uint32 samples) = 0;
	virtual void WriteAudio(const sint16 *data, uint32 bytes) = 0;
	virtual void EndAudioFrame() = 0;
	virtual bool Finalize(MyError& e) = 0;
};

///////////////////////////////////////////////////////////////////////////////
// ATAVIEncoder — writes video/audio to AVI container via aviwriter

class ATAVIEncoder final : public IATMediaEncoder {
public:
	ATAVIEncoder(const wchar_t *filename, ATVideoEncoding venc, uint32 w, uint32 h, const VDFraction& frameRate, const uint32 *palette, double samplingRate, bool stereo, bool recordAudio, bool encodeAllFrames);

	sint64 GetCurrentSize() override;
	bool GetDebugInfo(ATVideoRecordingDebugInfo& debugInfo) override;

	void WriteVideo(const VDPixmap& px) override;
	void BeginAudioFrame(uint32 bytes, uint32 samples) override;
	void WriteAudio(const sint16 *data, uint32 bytes) override;
	void EndAudioFrame() override;

	bool Finalize(MyError& e) override;

private:
	uint32 mKeyCounter = 0;
	uint32 mKeyInterval = 0;
	bool mbEncodeAllFrames = false;

	vdautoptr<IVDMediaOutputAVIFile> mFile;
	vdautoptr<IATVideoEncoder> mpVideoEncoder;
	IVDMediaOutputStream *mVideoStream = nullptr;
	IVDMediaOutputStream *mAudioStream = nullptr;
};

ATAVIEncoder::ATAVIEncoder(const wchar_t *filename, ATVideoEncoding venc, uint32 w, uint32 h, const VDFraction& frameRate, const uint32 *palette, double samplingRate, bool stereo, bool recordAudio, bool encodeAllFrames) {
	mbEncodeAllFrames = encodeAllFrames;
	mKeyCounter = 0;

	if (venc == kATVideoEncoding_Raw)
		mKeyInterval = 1;
	else
		mKeyInterval = 60;

	mFile = VDCreateMediaOutputAVIFile();

	mVideoStream = mFile->createVideoStream();
	if (recordAudio)
		mAudioStream = mFile->createAudioStream();

	struct {
		VDAVIBitmapInfoHeader hdr;
		uint32 pal[256];
	} bmf;
	bmf.hdr.biSize			= sizeof bmf.hdr;
	bmf.hdr.biWidth			= w;
	bmf.hdr.biHeight		= h;
	bmf.hdr.biPlanes		= 1;
	bmf.hdr.biXPelsPerMeter	= 3150;
	bmf.hdr.biYPelsPerMeter	= 3150;
	bmf.hdr.biClrUsed		= venc != kATVideoEncoding_ZMBV && palette ? 256 : 0;
	bmf.hdr.biClrImportant	= bmf.hdr.biClrUsed;

	switch(venc) {
		case kATVideoEncoding_Raw:
			bmf.hdr.biBitCount		= palette ? 8 : 24;
			bmf.hdr.biCompression	= VDAVIBitmapInfoHeader::kCompressionRGB;
			bmf.hdr.biSizeImage		= w * h * (palette ? 1 : 3);
			break;

		case kATVideoEncoding_RLE:
			bmf.hdr.biBitCount		= 8;
			bmf.hdr.biCompression	= VDAVIBitmapInfoHeader::kCompressionRLE8;
			bmf.hdr.biSizeImage		= w * h * 2;
			break;

		case kATVideoEncoding_ZMBV:
			bmf.hdr.biCompression	= VDMAKEFOURCC('Z', 'M', 'B', 'V');
			bmf.hdr.biSizeImage		= palette ? w * h * 2 : w * h * 8;
			bmf.hdr.biBitCount = 0;
			break;

		default:
			throw MyError("Unsupported video encoding for AVI output.");
	}

	if (palette && venc != kATVideoEncoding_ZMBV) {
		for(int i=0; i<256; ++i)
			bmf.pal[i] = palette[i] & 0xffffff;
		mVideoStream->setFormat(&bmf, sizeof bmf);
	} else
		mVideoStream->setFormat(&bmf.hdr, sizeof bmf.hdr);

	AVIStreamHeader_fixed hdr;
	hdr.fccType					= VDMAKEFOURCC('v', 'i', 'd', 's');
    hdr.dwFlags					= 0;
    hdr.wPriority				= 0;
    hdr.wLanguage				= 0;
    hdr.dwInitialFrames			= 0;
    hdr.dwScale					= frameRate.getLo();
    hdr.dwRate					= frameRate.getHi();
    hdr.dwStart					= 0;
    hdr.dwLength				= 0;
    hdr.dwSuggestedBufferSize	= 0;
    hdr.dwQuality				= (uint32)-1;
    hdr.dwSampleSize			= 0;
	hdr.rcFrame.left			= 0;
	hdr.rcFrame.top				= 0;
	hdr.rcFrame.right			= w;
	hdr.rcFrame.bottom			= h;

	switch(venc) {
		case kATVideoEncoding_Raw:
			hdr.fccHandler				= VDMAKEFOURCC('D', 'I', 'B', ' ');
			break;

		case kATVideoEncoding_RLE:
			hdr.fccHandler				= VDMAKEFOURCC('m', 'r', 'l', 'e');
			break;

		case kATVideoEncoding_ZMBV:
			hdr.fccHandler				= VDMAKEFOURCC('Z', 'M', 'B', 'V');
			break;

		default:
			hdr.fccHandler				= 0;
			break;
	}

	mVideoStream->setStreamInfo(hdr);

	nsVDWinFormats::WaveFormatEx wf;
	wf.mFormatTag = nsVDWinFormats::kWAVE_FORMAT_PCM;
	wf.mChannels = stereo ? 2 : 1;
	wf.SetSamplesPerSec(48000);
	wf.mBlockAlign = 2 * wf.mChannels;
	wf.SetAvgBytesPerSec(48000 * wf.mBlockAlign);
	wf.mBitsPerSample = 16;
	wf.mSize = 0;

	if (mAudioStream) {
		mAudioStream->setFormat(&wf, offsetof(nsVDWinFormats::WaveFormatEx, mSize));
		hdr.fccType					= VDMAKEFOURCC('a', 'u', 'd', 's');
	    hdr.fccHandler				= 0;
	    hdr.dwFlags					= 0;
	    hdr.wPriority				= 0;
	    hdr.wLanguage				= 0;
	    hdr.dwInitialFrames			= 0;
		hdr.dwScale					= wf.mBlockAlign;
		hdr.dwRate					= wf.GetAvgBytesPerSec();
	    hdr.dwStart					= 0;
	    hdr.dwLength				= 0;
	    hdr.dwSuggestedBufferSize	= 0;
	    hdr.dwQuality				= (uint32)-1;
		hdr.dwSampleSize			= wf.mBlockAlign;
		hdr.rcFrame.left			= 0;
		hdr.rcFrame.top				= 0;
		hdr.rcFrame.right			= 0;
		hdr.rcFrame.bottom			= 0;

		mAudioStream->setStreamInfo(hdr);
	}

	mFile->setBuffering(4194304, 524288, IVDFileAsync::kModeAsynchronous);
	mFile->init(filename);

	switch(venc) {
		case kATVideoEncoding_Raw:
			mpVideoEncoder = new ATVideoEncoderRaw(w, h, palette ? nsVDPixmap::kPixFormat_Pal8 : nsVDPixmap::kPixFormat_RGB888);
			break;

		case kATVideoEncoding_RLE:
			mpVideoEncoder = new ATVideoEncoderRLE(w, h);
			break;

		case kATVideoEncoding_ZMBV:
			mpVideoEncoder = new ATVideoEncoderZMBV(w, h, palette == NULL);
			break;

		default:
			throw MyError("Unsupported video encoding.");
	}
}

sint64 ATAVIEncoder::GetCurrentSize() {
	return mFile->GetCurrentSize();
}

bool ATAVIEncoder::GetDebugInfo(ATVideoRecordingDebugInfo& debugInfo) {
	return mpVideoEncoder && mpVideoEncoder->GetDebugInfo(debugInfo);
}

void ATAVIEncoder::WriteVideo(const VDPixmap& px) {
	bool intra = false;

	if (!mKeyCounter) {
		mKeyCounter = mKeyInterval;
		intra = true;
	}

	--mKeyCounter;

	mpVideoEncoder->Compress(px, intra, mbEncodeAllFrames);

	uint32 len = mpVideoEncoder->GetEncodedLength();
	mVideoStream->write(len && intra ? IVDMediaOutputStream::kFlagKeyFrame : 0, mpVideoEncoder->GetEncodedData(), len, 1);
}

void ATAVIEncoder::BeginAudioFrame(uint32 bytes, uint32 samples) {
	if (mAudioStream)
		mAudioStream->partialWriteBegin(IVDMediaOutputStream::kFlagKeyFrame, bytes, samples);
}

void ATAVIEncoder::WriteAudio(const sint16 *data, uint32 bytes) {
	if (mAudioStream)
		mAudioStream->partialWrite(data, bytes);
}

void ATAVIEncoder::EndAudioFrame() {
	if (mAudioStream)
		mAudioStream->partialWriteEnd();
}

bool ATAVIEncoder::Finalize(MyError& error) {
	if (mVideoStream) {
		try {
			mVideoStream->finish();
		} catch(VDException& e) {
			if (error.empty())
				error = std::move(e);
		}

		mVideoStream = nullptr;
	}

	if (mFile) {
		try {
			mFile->finalize();
		} catch(VDException& e) {
			if (error.empty())
				error = std::move(e);
		}

		mFile.reset();
	}

	mpVideoEncoder.reset();

	return error.empty();
}

#ifdef ALTIRRA_ENABLE_FFMPEG_RECORDING
///////////////////////////////////////////////////////////////////////////////
// ATFFmpegEncoder — writes H.264/AAC MP4 via static FFmpeg + libx264

namespace {
	void ATThrowFFmpegError(int err, const char *what) {
		char errbuf[AV_ERROR_MAX_STRING_SIZE];
		av_strerror(err, errbuf, sizeof errbuf);
		throw MyError("%s: %s", what, errbuf);
	}

	AVSampleFormat ATGetFirstSupportedSampleFormat(const AVCodec *codec) {
		const void *configs = nullptr;
		int numConfigs = 0;
		const int err = avcodec_get_supported_config(nullptr, codec,
			AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, &configs, &numConfigs);
		if (err < 0 || !configs || numConfigs <= 0)
			return AV_SAMPLE_FMT_FLTP;

		return static_cast<const AVSampleFormat *>(configs)[0];
	}
}

class ATFFmpegEncoder final : public IATMediaEncoder {
public:
	ATFFmpegEncoder(const wchar_t *filename, uint32 videoBitRate, uint32 audioBitRate, uint32 w, uint32 h, const VDFraction& frameRate, bool stereo);
	~ATFFmpegEncoder();

	sint64 GetCurrentSize() override;
	void WriteVideo(const VDPixmap& px) override;
	void BeginAudioFrame(uint32 bytes, uint32 samples) override;
	void WriteAudio(const sint16 *data, uint32 bytes) override;
	void EndAudioFrame() override;
	bool Finalize(MyError& e) override;

private:
	void Init(const wchar_t *filename, uint32 videoBitRate, uint32 audioBitRate, uint32 w, uint32 h, const VDFraction& frameRate, bool stereo);
	void Shutdown();
	void EncodeAudioFromFifo(bool flushPartial);
	void DrainVideoPackets();
	void DrainAudioPackets();

	bool mbFinalized = false;
	bool mbRecordAudio = false;
	int mChannels = 0;
	int mAudioFrameSize = 0;
	int64_t mVideoNextPts = 0;
	int64_t mAudioNextPts = 0;

	AVFormatContext *mpFormatCtx = nullptr;
	AVStream *mpVideoStream = nullptr;
	AVStream *mpAudioStream = nullptr;
	AVCodecContext *mpVideoCodecCtx = nullptr;
	AVCodecContext *mpAudioCodecCtx = nullptr;
	AVFrame *mpVideoFrame = nullptr;
	AVPacket *mpPacket = nullptr;
	SwsContext *mpSwsCtx = nullptr;
	SwrContext *mpSwrCtx = nullptr;
	AVAudioFifo *mpAudioFifo = nullptr;

	VDPixmapBuffer mSourceConversionBuffer;
	VDPixmapCachedBlitter mSourceConversionBlitter;
};

ATFFmpegEncoder::ATFFmpegEncoder(const wchar_t *filename, uint32 videoBitRate, uint32 audioBitRate, uint32 w, uint32 h, const VDFraction& frameRate, bool stereo) {
	try {
		Init(filename, videoBitRate, audioBitRate, w, h, frameRate, stereo);
	} catch(...) {
		Shutdown();
		throw;
	}
}

ATFFmpegEncoder::~ATFFmpegEncoder() {
	Shutdown();
}

void ATFFmpegEncoder::Init(const wchar_t *filename, uint32 videoBitRate, uint32 audioBitRate, uint32 w, uint32 h, const VDFraction& frameRate, bool stereo) {
	const AVCodec *videoCodec = avcodec_find_encoder_by_name("libx264");
	if (!videoCodec)
		throw MyError("FFmpeg recording is enabled, but the static build does not contain the libx264 encoder.");

	const bool recordAudio = audioBitRate != 0;
	const AVCodec *audioCodec = nullptr;
	if (recordAudio) {
		audioCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
		if (!audioCodec)
			throw MyError("FFmpeg recording is enabled, but the static build does not contain the AAC encoder.");
	}
	mbRecordAudio = recordAudio;

	std::string filenameUtf8 = VDTextWToU8(filename, (int)wcslen(filename)).c_str();

	int err = avformat_alloc_output_context2(&mpFormatCtx, nullptr, "mp4", filenameUtf8.c_str());
	if (err < 0 || !mpFormatCtx)
		ATThrowFFmpegError(err < 0 ? err : AVERROR_UNKNOWN, "Unable to create MP4 output context");

	mpPacket = av_packet_alloc();
	if (!mpPacket)
		throw MyError("Unable to allocate FFmpeg packet.");

	mpVideoStream = avformat_new_stream(mpFormatCtx, nullptr);
	if (recordAudio)
		mpAudioStream = avformat_new_stream(mpFormatCtx, nullptr);
	if (!mpVideoStream || (recordAudio && !mpAudioStream))
		throw MyError("Unable to create FFmpeg streams.");

	mpVideoCodecCtx = avcodec_alloc_context3(videoCodec);
	if (recordAudio)
		mpAudioCodecCtx = avcodec_alloc_context3(audioCodec);
	if (!mpVideoCodecCtx || (recordAudio && !mpAudioCodecCtx))
		throw MyError("Unable to allocate FFmpeg codec contexts.");

	mpVideoCodecCtx->codec_id = videoCodec->id;
	mpVideoCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
	mpVideoCodecCtx->bit_rate = std::clamp<uint32>(videoBitRate, 500000, 8000000);
	mpVideoCodecCtx->width = (int)w;
	mpVideoCodecCtx->height = (int)h;
	mpVideoCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
	mpVideoCodecCtx->time_base = AVRational{ (int)frameRate.getLo(), (int)frameRate.getHi() };
	mpVideoCodecCtx->framerate = AVRational{ (int)frameRate.getHi(), (int)frameRate.getLo() };
	mpVideoCodecCtx->gop_size = std::max(1, VDRoundToInt(mpVideoCodecCtx->framerate.num / (double)mpVideoCodecCtx->framerate.den));
	mpVideoCodecCtx->max_b_frames = 0;
	if (mpFormatCtx->oformat->flags & AVFMT_GLOBALHEADER)
		mpVideoCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	av_opt_set(mpVideoCodecCtx->priv_data, "preset", "veryfast", 0);

	err = avcodec_open2(mpVideoCodecCtx, videoCodec, nullptr);
	if (err < 0)
		ATThrowFFmpegError(err, "Unable to open H.264 encoder");

	err = avcodec_parameters_from_context(mpVideoStream->codecpar, mpVideoCodecCtx);
	if (err < 0)
		ATThrowFFmpegError(err, "Unable to export H.264 stream parameters");
	mpVideoStream->time_base = mpVideoCodecCtx->time_base;

	if (recordAudio) {
		mChannels = stereo ? 2 : 1;
		mpAudioCodecCtx->codec_id = audioCodec->id;
		mpAudioCodecCtx->codec_type = AVMEDIA_TYPE_AUDIO;
		mpAudioCodecCtx->sample_rate = 48000;
		mpAudioCodecCtx->bit_rate = std::clamp<uint32>(audioBitRate, 64000, 256000);
		mpAudioCodecCtx->time_base = AVRational{ 1, mpAudioCodecCtx->sample_rate };
		av_channel_layout_default(&mpAudioCodecCtx->ch_layout, mChannels);
		mpAudioCodecCtx->sample_fmt = ATGetFirstSupportedSampleFormat(audioCodec);
		if (mpFormatCtx->oformat->flags & AVFMT_GLOBALHEADER)
			mpAudioCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

		err = avcodec_open2(mpAudioCodecCtx, audioCodec, nullptr);
		if (err < 0)
			ATThrowFFmpegError(err, "Unable to open AAC encoder");

		err = avcodec_parameters_from_context(mpAudioStream->codecpar, mpAudioCodecCtx);
		if (err < 0)
			ATThrowFFmpegError(err, "Unable to export AAC stream parameters");
		mpAudioStream->time_base = mpAudioCodecCtx->time_base;

		mpSwrCtx = swr_alloc();
		if (!mpSwrCtx)
			throw MyError("Unable to allocate FFmpeg audio resampler.");

		av_opt_set_chlayout(mpSwrCtx, "in_chlayout", &mpAudioCodecCtx->ch_layout, 0);
		av_opt_set_chlayout(mpSwrCtx, "out_chlayout", &mpAudioCodecCtx->ch_layout, 0);
		av_opt_set_int(mpSwrCtx, "in_sample_rate", mpAudioCodecCtx->sample_rate, 0);
		av_opt_set_int(mpSwrCtx, "out_sample_rate", mpAudioCodecCtx->sample_rate, 0);
		av_opt_set_sample_fmt(mpSwrCtx, "in_sample_fmt", AV_SAMPLE_FMT_S16, 0);
		av_opt_set_sample_fmt(mpSwrCtx, "out_sample_fmt", mpAudioCodecCtx->sample_fmt, 0);
		err = swr_init(mpSwrCtx);
		if (err < 0)
			ATThrowFFmpegError(err, "Unable to initialize FFmpeg audio resampler");

		mAudioFrameSize = mpAudioCodecCtx->frame_size ? mpAudioCodecCtx->frame_size : 1024;

		mpAudioFifo = av_audio_fifo_alloc(mpAudioCodecCtx->sample_fmt, mChannels, mAudioFrameSize * 4);
		if (!mpAudioFifo)
			throw MyError("Unable to allocate FFmpeg audio FIFO.");
	}

	mpVideoFrame = av_frame_alloc();
	if (!mpVideoFrame)
		throw MyError("Unable to allocate FFmpeg video frame.");

	mpVideoFrame->format = mpVideoCodecCtx->pix_fmt;
	mpVideoFrame->width = mpVideoCodecCtx->width;
	mpVideoFrame->height = mpVideoCodecCtx->height;
	err = av_frame_get_buffer(mpVideoFrame, 32);
	if (err < 0)
		ATThrowFFmpegError(err, "Unable to allocate FFmpeg video frame buffer");

	if (!(mpFormatCtx->oformat->flags & AVFMT_NOFILE)) {
		err = avio_open(&mpFormatCtx->pb, filenameUtf8.c_str(), AVIO_FLAG_WRITE);
		if (err < 0)
			ATThrowFFmpegError(err, "Unable to open MP4 output file");
	}

	err = avformat_write_header(mpFormatCtx, nullptr);
	if (err < 0)
		ATThrowFFmpegError(err, "Unable to write MP4 header");
}

sint64 ATFFmpegEncoder::GetCurrentSize() {
	return mpFormatCtx && mpFormatCtx->pb ? avio_tell(mpFormatCtx->pb) : 0;
}

void ATFFmpegEncoder::WriteVideo(const VDPixmap& px) {
	const VDPixmap *src = &px;

	if (px.format != nsVDPixmap::kPixFormat_XRGB8888) {
		if (!mSourceConversionBuffer.format)
			mSourceConversionBuffer.init(px.w, px.h, nsVDPixmap::kPixFormat_XRGB8888);

		mSourceConversionBlitter.Blit(mSourceConversionBuffer, px);
		src = &mSourceConversionBuffer;
	}

	if (!mpSwsCtx) {
		mpSwsCtx = sws_getCachedContext(nullptr,
			src->w, src->h, AV_PIX_FMT_BGR0,
			mpVideoCodecCtx->width, mpVideoCodecCtx->height, mpVideoCodecCtx->pix_fmt,
			SWS_BILINEAR, nullptr, nullptr, nullptr);
		if (!mpSwsCtx)
			throw MyError("Unable to initialize FFmpeg video scaler.");
	}

	int err = av_frame_make_writable(mpVideoFrame);
	if (err < 0)
		ATThrowFFmpegError(err, "Unable to make FFmpeg video frame writable");

	const uint8 *srcData[4] = {
		static_cast<const uint8 *>(src->data),
		nullptr, nullptr, nullptr
	};
	int srcLinesize[4] = {
		(int)src->pitch,
		0, 0, 0
	};

	sws_scale(mpSwsCtx, srcData, srcLinesize, 0, src->h, mpVideoFrame->data, mpVideoFrame->linesize);

	mpVideoFrame->pts = mVideoNextPts++;

	err = avcodec_send_frame(mpVideoCodecCtx, mpVideoFrame);
	if (err < 0)
		ATThrowFFmpegError(err, "Unable to send video frame to H.264 encoder");

	DrainVideoPackets();
}

void ATFFmpegEncoder::BeginAudioFrame(uint32, uint32) {
}

void ATFFmpegEncoder::WriteAudio(const sint16 *data, uint32 bytes) {
	if (!mbRecordAudio)
		return;

	const int samples = (int)(bytes / (sizeof(sint16) * mChannels));

	uint8 **converted = nullptr;
	int convertedLinesize = 0;
	int err = av_samples_alloc_array_and_samples(&converted, &convertedLinesize,
		mChannels, samples, mpAudioCodecCtx->sample_fmt, 0);
	if (err < 0)
		ATThrowFFmpegError(err, "Unable to allocate converted audio buffer");

	const uint8 *input[] = {
		reinterpret_cast<const uint8 *>(data)
	};

	const int convertedSamples = swr_convert(mpSwrCtx, converted, samples, input, samples);
	if (convertedSamples < 0) {
		av_freep(&converted[0]);
		av_freep(&converted);
		ATThrowFFmpegError(convertedSamples, "Unable to convert audio for AAC encoder");
	}

	err = av_audio_fifo_realloc(mpAudioFifo, av_audio_fifo_size(mpAudioFifo) + convertedSamples);
	if (err < 0) {
		av_freep(&converted[0]);
		av_freep(&converted);
		ATThrowFFmpegError(err, "Unable to grow FFmpeg audio FIFO");
	}

	if (av_audio_fifo_write(mpAudioFifo, reinterpret_cast<void **>(converted), convertedSamples) != convertedSamples) {
		av_freep(&converted[0]);
		av_freep(&converted);
		throw MyError("Unable to write audio samples into FFmpeg FIFO.");
	}

	av_freep(&converted[0]);
	av_freep(&converted);

	EncodeAudioFromFifo(false);
}

void ATFFmpegEncoder::EndAudioFrame() {
}

void ATFFmpegEncoder::EncodeAudioFromFifo(bool flushPartial) {
	if (!mbRecordAudio)
		return;

	const bool variableFrameSize = (mpAudioCodecCtx->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE) != 0;

	while(av_audio_fifo_size(mpAudioFifo) >= mAudioFrameSize || (flushPartial && av_audio_fifo_size(mpAudioFifo) > 0)) {
		const int available = av_audio_fifo_size(mpAudioFifo);
		const int frameSamples =
			(flushPartial && variableFrameSize) ? available
			: (flushPartial ? std::max(available, mAudioFrameSize) : mAudioFrameSize);

		AVFrame *frame = av_frame_alloc();
		if (!frame)
			throw MyError("Unable to allocate FFmpeg audio frame.");

		frame->nb_samples = frameSamples;
		frame->format = mpAudioCodecCtx->sample_fmt;
		frame->sample_rate = mpAudioCodecCtx->sample_rate;
		av_channel_layout_copy(&frame->ch_layout, &mpAudioCodecCtx->ch_layout);

		int err = av_frame_get_buffer(frame, 0);
		if (err < 0) {
			av_frame_free(&frame);
			ATThrowFFmpegError(err, "Unable to allocate FFmpeg audio frame buffer");
		}

		err = av_frame_make_writable(frame);
		if (err < 0) {
			av_frame_free(&frame);
			ATThrowFFmpegError(err, "Unable to make FFmpeg audio frame writable");
		}

		const int toRead = std::min(available, frameSamples);
		if (av_audio_fifo_read(mpAudioFifo, reinterpret_cast<void **>(frame->data), toRead) != toRead) {
			av_frame_free(&frame);
			throw MyError("Unable to read audio samples from FFmpeg FIFO.");
		}

		if (toRead < frameSamples)
			av_samples_set_silence(frame->data, toRead, frameSamples - toRead, mChannels, mpAudioCodecCtx->sample_fmt);

		frame->pts = mAudioNextPts;
		mAudioNextPts += frameSamples;

		err = avcodec_send_frame(mpAudioCodecCtx, frame);
		av_frame_free(&frame);
		if (err < 0)
			ATThrowFFmpegError(err, "Unable to send audio frame to AAC encoder");

		DrainAudioPackets();
	}
}

void ATFFmpegEncoder::DrainVideoPackets() {
	for(;;) {
		int err = avcodec_receive_packet(mpVideoCodecCtx, mpPacket);
		if (err == AVERROR(EAGAIN) || err == AVERROR_EOF)
			break;
		if (err < 0)
			ATThrowFFmpegError(err, "Unable to receive H.264 packet");

		av_packet_rescale_ts(mpPacket, mpVideoCodecCtx->time_base, mpVideoStream->time_base);
		mpPacket->stream_index = mpVideoStream->index;

		err = av_interleaved_write_frame(mpFormatCtx, mpPacket);
		av_packet_unref(mpPacket);
		if (err < 0)
			ATThrowFFmpegError(err, "Unable to write H.264 packet to MP4");
	}
}

void ATFFmpegEncoder::DrainAudioPackets() {
	if (!mbRecordAudio)
		return;

	for(;;) {
		int err = avcodec_receive_packet(mpAudioCodecCtx, mpPacket);
		if (err == AVERROR(EAGAIN) || err == AVERROR_EOF)
			break;
		if (err < 0)
			ATThrowFFmpegError(err, "Unable to receive AAC packet");

		av_packet_rescale_ts(mpPacket, mpAudioCodecCtx->time_base, mpAudioStream->time_base);
		mpPacket->stream_index = mpAudioStream->index;

		err = av_interleaved_write_frame(mpFormatCtx, mpPacket);
		av_packet_unref(mpPacket);
		if (err < 0)
			ATThrowFFmpegError(err, "Unable to write AAC packet to MP4");
	}
}

bool ATFFmpegEncoder::Finalize(MyError& error) {
	if (mbFinalized)
		return error.empty();

	mbFinalized = true;

	try {
		if (mbRecordAudio) {
			EncodeAudioFromFifo(true);

			int err = avcodec_send_frame(mpAudioCodecCtx, nullptr);
			if (err < 0)
				ATThrowFFmpegError(err, "Unable to flush AAC encoder");
			DrainAudioPackets();
		}

		int err = avcodec_send_frame(mpVideoCodecCtx, nullptr);
		if (err < 0)
			ATThrowFFmpegError(err, "Unable to flush H.264 encoder");
		DrainVideoPackets();

		err = av_write_trailer(mpFormatCtx);
		if (err < 0)
			ATThrowFFmpegError(err, "Unable to finalize MP4 output");
	} catch(VDException& e) {
		if (error.empty())
			error = std::move(e);
	}

	Shutdown();
	return error.empty();
}

void ATFFmpegEncoder::Shutdown() {
	if (mpAudioFifo) {
		av_audio_fifo_free(mpAudioFifo);
		mpAudioFifo = nullptr;
	}

	if (mpSwrCtx) {
		swr_free(&mpSwrCtx);
	}

	if (mpSwsCtx) {
		sws_freeContext(mpSwsCtx);
		mpSwsCtx = nullptr;
	}

	if (mpVideoFrame) {
		av_frame_free(&mpVideoFrame);
	}

	if (mpPacket) {
		av_packet_free(&mpPacket);
	}

	if (mpVideoCodecCtx) {
		avcodec_free_context(&mpVideoCodecCtx);
	}

	if (mpAudioCodecCtx) {
		avcodec_free_context(&mpAudioCodecCtx);
	}

	if (mpFormatCtx) {
		if (!(mpFormatCtx->oformat->flags & AVFMT_NOFILE) && mpFormatCtx->pb)
			avio_closep(&mpFormatCtx->pb);
		avformat_free_context(mpFormatCtx);
		mpFormatCtx = nullptr;
	}
}
#endif

///////////////////////////////////////////////////////////////////////////////
// ATVideoWriter — main orchestrator: receives GTIA frames and audio samples,
// resamples, and feeds to media encoder

class ATVideoWriter final : public IATVideoWriter, public IATGTIAVideoTap, public IATAudioTap {
public:
	ATVideoWriter();
	~ATVideoWriter();

	IATGTIAVideoTap *AsVideoTap() override { return this; }
	IATAudioTap *AsAudioTap() override { return this; }

	void CheckExceptions() override;

	void Init(const wchar_t *filename, ATVideoEncoding venc,
		uint32 videoBitRate, uint32 audioBitRate,
		uint32 w, uint32 h, const VDFraction& frameRate, double pixelAspectRatio,
		ATVideoRecordingResamplingMode resamplingMode,
		ATVideoRecordingScalingMode scalingMode,
		const uint32 *palette, double samplingRate, bool stereo, bool recordAudio, double timestampRate, bool halfRate, bool encodeAllFrames, IATUIRenderer *r) override;
	void Shutdown() override;

	bool IsPaused() const override { return mbPaused; }
	void Pause() override;
	void Resume() override;

	bool GetDebugInfo(ATVideoRecordingDebugInfo& debugInfo) override;

public:
	void WriteFrame(const VDPixmap& px, uint64 timestampStart, uint64 timestampEnd, float par) override;
	void WriteRawAudio(const float *left, const float *right, uint32 count, uint32 timestamp) override;

protected:
	void RaiseError(MyError&& e);

	bool mbStereo;
	bool mbRecordAudio;
	bool mbHalfRate;
	bool mbErrorState;
	bool mbPaused = false;

	bool	mbFirstVideoTimestampSet = false;
	bool	mbVideoPreskipTimestampSet = false;
	bool	mbAudioPreskipSet = false;

	uint64	mFirstVideoTimestamp = 0;
	uint64	mSyncVideoTimestamp = 0;

	sint32	mSyncAudioPreskip = 0;
	uint32	mSyncVideoPreskip = 0;

	double	mFrameRate;
	double	mSamplingRate;
	double	mTimestampRate;

	uint64	mVideoFramesWritten = 0;
	uint64	mAudioSamplesWritten = 0;

	vdrect32f mVideoDestRect;

	IATUIRenderer	*mpUIRenderer = nullptr;

	vdautoptr<IATMediaEncoder> mpMediaEncoder;

	VDPixmapCachedBlitter mVideoColorConversionBlitter;
	vdautoptr<IVDPixmapResampler> mpVideoResampler;
	VDPixmapCachedBlitter mVideoPostResampleCcBlitter;
	VDPixmapBuffer mVideoColorConversionBuffer;
	VDPixmapBuffer mVideoResampleBuffer;
	VDPixmapBuffer mVideoPostResampleCcBuffer;

	MyError	mError;

	enum { kResampleBufferSize = 4096 };

	uint32	mResampleLevel;
	uint64	mResampleAccum;
	uint64	mResampleRate;
	float	mResampleBuffers[2][4096];
};

ATVideoWriter::ATVideoWriter() {
}

ATVideoWriter::~ATVideoWriter() {
}

void ATVideoWriter::CheckExceptions() {
	if (!mbErrorState)
		return;

	if (!mError.empty()) {
		VDException e(std::move(mError));

		throw e;
	}
}

void ATVideoWriter::Init(const wchar_t *filename, ATVideoEncoding venc,
	uint32 videoBitRate,
	uint32 audioBitRate,
	uint32 w, uint32 h, const VDFraction& frameRate, double pixelAspectRatio,
	ATVideoRecordingResamplingMode resamplingMode,
	ATVideoRecordingScalingMode scalingMode,
	const uint32 *palette, double samplingRate, bool stereo, bool recordAudio, double timestampRate, bool halfRate, bool encodeAllFrames, IATUIRenderer *r)
{
	mbStereo = stereo;
	mbRecordAudio = recordAudio;
	mbHalfRate = halfRate;
	mbErrorState = false;
	mbVideoPreskipTimestampSet = false;
	mbAudioPreskipSet = !recordAudio;
	mFrameRate = frameRate.asDouble();
	mSamplingRate = samplingRate;
	mTimestampRate = timestampRate;
	mSyncAudioPreskip = 0;
	mSyncVideoPreskip = 0;

	mResampleLevel = 0;
	mResampleAccum = 0;
	mResampleRate = VDRoundToInt64(4294967296.0 / 48000.0 * samplingRate);

	mpUIRenderer = r;

	VDFraction encodingFrameRate = frameRate;

	if (halfRate)
		encodingFrameRate /= 2;

	float aspectCorrectionRatio = pixelAspectRatio;

	if (fabsf(aspectCorrectionRatio - 1.0f) < 1e-4f)
		aspectCorrectionRatio = 1.0f;

	float dstwf = (float)w * aspectCorrectionRatio;
	float dsthf = (float)h;
	uint32 framew = w;
	uint32 frameh = h;

	switch(scalingMode) {
		case ATVideoRecordingScalingMode::None:
			framew = (uint32)VDCeilToInt(dstwf);
			frameh = (uint32)VDCeilToInt(dsthf);
			break;

		case ATVideoRecordingScalingMode::Scale480Narrow:
			framew = 640;
			frameh = 480;
			break;

		case ATVideoRecordingScalingMode::Scale480Wide:
			framew = 854;
			frameh = 480;
			break;

		case ATVideoRecordingScalingMode::Scale720Narrow:
			framew = 960;
			frameh = 720;
			break;

		case ATVideoRecordingScalingMode::Scale720Wide:
			framew = 1280;
			frameh = 720;
			break;
	}

	// No YUV path in SDL3 build (no Media Foundation)
	bool useYUV = false;

	if (framew != w || frameh != h || (uint32)(0.5f + dstwf) != w || (uint32)(0.5f + dsthf) != h) {
		mpVideoResampler = VDCreatePixmapResampler();

		mVideoResampleBuffer.init(framew, frameh, nsVDPixmap::kPixFormat_XRGB8888);

		memset(mVideoResampleBuffer.base(), 0, mVideoResampleBuffer.size());

		float scale = 1.0f;

		if (scalingMode != ATVideoRecordingScalingMode::None)
			scale = std::min<float>((float)framew / dstwf, (float)frameh / dsthf);

		dstwf *= scale;
		dsthf *= scale;

		const float dstxf = ((float)framew - dstwf) * 0.5f;
		const float dstyf = ((float)frameh - dsthf) * 0.5f;
		vdrect32f dstrect(dstxf, dstyf, (float)framew - dstxf, (float)frameh - dstyf);

		IVDPixmapResampler::FilterMode filterMode;
		switch(resamplingMode) {
			case ATVideoRecordingResamplingMode::Nearest:
				filterMode = IVDPixmapResampler::kFilterPoint;
				break;

			case ATVideoRecordingResamplingMode::SharpBilinear:
				filterMode = IVDPixmapResampler::kFilterSharpLinear;
				mpVideoResampler->SetSharpnessFactors(2.0f, 2.0f);
				break;

			case ATVideoRecordingResamplingMode::Bilinear:
			default:
				filterMode = IVDPixmapResampler::kFilterLinear;
				break;
		}

		mVideoDestRect = dstrect;

		mpVideoResampler->SetFilters(filterMode, filterMode, false);
		VDVERIFY(mpVideoResampler->Init(dstrect, framew, frameh, mVideoResampleBuffer.format, vdrect32f(0, 0, (float)w, (float)h), w, h, mVideoResampleBuffer.format));

		w = framew;
		h = frameh;

		palette = nullptr;
	}

	if (!palette && venc == kATVideoEncoding_RLE)
		throw MyError("RLE encoding is not available as the current emulation video and recording settings require 24-bit video.");

	switch(venc) {
		case kATVideoEncoding_Raw:
		case kATVideoEncoding_RLE:
		case kATVideoEncoding_ZMBV:
			mpMediaEncoder = new ATAVIEncoder(filename, venc, w, h, encodingFrameRate, palette, samplingRate, stereo, recordAudio, encodeAllFrames);
			break;

#ifdef ALTIRRA_ENABLE_FFMPEG_RECORDING
		case kATVideoEncoding_H264_AAC:
			mpMediaEncoder = new ATFFmpegEncoder(filename, videoBitRate, recordAudio ? audioBitRate : 0, w, h, encodingFrameRate, stereo);
			break;
#endif

		default:
			throw MyError("This video encoding format is not available on the SDL3 build.");
	}
}

void ATVideoWriter::Shutdown() {
	if (mpUIRenderer) {
		mpUIRenderer->SetRecordingPosition();
		mpUIRenderer = NULL;
	}

	if (mpMediaEncoder) {
		MyError e;
		if (!mpMediaEncoder->Finalize(e))
			RaiseError(std::move(e));

		mpMediaEncoder.reset();
	}
}

void ATVideoWriter::Pause() {
	if (mbPaused)
		return;

	mbPaused = true;

	if (mpUIRenderer)
		mpUIRenderer->SetRecordingPositionPaused();
}

void ATVideoWriter::Resume() {
	if (!mbPaused)
		return;

	mbPaused = false;

	mbAudioPreskipSet = !mbRecordAudio;
	mbVideoPreskipTimestampSet = false;
	mSyncAudioPreskip = 0;
	mSyncVideoPreskip = 0;
}

bool ATVideoWriter::GetDebugInfo(ATVideoRecordingDebugInfo& debugInfo) {
	if (!mpMediaEncoder || !mpMediaEncoder->GetDebugInfo(debugInfo))
		return false;

	debugInfo.mVideoDestRect = mVideoDestRect;
	return true;
}

void ATVideoWriter::WriteFrame(const VDPixmap& px, uint64 timestamp, uint64 timestampEnd, float par) {
	if (mbErrorState)
		return;

	if (mbPaused)
		return;

	if (!mbAudioPreskipSet) {
		mbVideoPreskipTimestampSet = true;
		mSyncVideoTimestamp = timestamp;
		return;
	}

	if (mSyncVideoPreskip) {
		--mSyncVideoPreskip;
		return;
	}

	if (!mbFirstVideoTimestampSet) {
		mbFirstVideoTimestampSet = true;
		mFirstVideoTimestamp = timestamp;
	}

	++mVideoFramesWritten;

	if (mpUIRenderer)
		mpUIRenderer->SetRecordingPosition((float)((double)(timestamp - mFirstVideoTimestamp) / mTimestampRate), mpMediaEncoder->GetCurrentSize(), false);

	const VDPixmap *pxlast = &px;

	try {
		if (mpVideoResampler) {
			if (pxlast->format != mVideoResampleBuffer.format) {
				if (!mVideoColorConversionBuffer.format)
					mVideoColorConversionBuffer.init(pxlast->w, pxlast->h, mVideoResampleBuffer.format);

				mVideoColorConversionBlitter.Blit(mVideoColorConversionBuffer, *pxlast);
				pxlast = &mVideoColorConversionBuffer;
			}

			mpVideoResampler->Process(mVideoResampleBuffer, *pxlast);
			pxlast = &mVideoResampleBuffer;
		}

		if (mVideoPostResampleCcBuffer.format) {
			mVideoPostResampleCcBlitter.Blit(mVideoPostResampleCcBuffer, *pxlast);
			pxlast = &mVideoPostResampleCcBuffer;
		}

		mpMediaEncoder->WriteVideo(*pxlast);

		if (mbHalfRate)
			mSyncVideoPreskip = 1;
	} catch(MyError& e) {
		RaiseError(std::move(e));
	}
}

void ATVideoWriter::WriteRawAudio(const float *left, const float *right, uint32 count, uint32 timestamp) {
	if (mbErrorState)
		return;

	if (mbPaused)
		return;

	if (!mbRecordAudio) {
		mAudioSamplesWritten += count;
		return;
	}

	if (!mbAudioPreskipSet) {
		if (!mbVideoPreskipTimestampSet)
			return;

		mbAudioPreskipSet = true;

		double offset = (double)(sint32)(mSyncVideoTimestamp - timestamp) / mTimestampRate + 1.0f / mFrameRate
			+ (double)mVideoFramesWritten / (double)mFrameRate
			- (double)mAudioSamplesWritten / (double)mSamplingRate;

		for(;;) {
			mSyncAudioPreskip = VDRoundToInt32(offset * mSamplingRate);

			if (mSyncAudioPreskip >= 0)
				break;

			++mSyncVideoPreskip;
			offset += 1.0f / mFrameRate;
		}
	}

	if (mSyncAudioPreskip) {
		uint32 toSkip = mSyncAudioPreskip;

		if (toSkip >= count) {
			mSyncAudioPreskip -= count;
			return;
		}

		mSyncAudioPreskip = 0;

		left += toSkip;
		if (right)
			right += toSkip;

		count -= toSkip;
	}

	mAudioSamplesWritten += count;

	uint32 outputSamples = 0;
	uint32 newLevel = mResampleLevel + count;

	if (newLevel >= 8) {
		uint64 newMaxValid = ((uint64)(newLevel - 7) << 32) - 1;

		if (newMaxValid > mResampleAccum)
			outputSamples = (uint32)((newMaxValid - mResampleAccum) / mResampleRate);
	}

	sint16 buf[1024];
	try {
		if (outputSamples)
			mpMediaEncoder->BeginAudioFrame(outputSamples*(mbStereo ? 4 : 2), outputSamples);

		uint32 outputSamplesLeft = outputSamples;
		for(;;) {
			// copy in samples
			if (count) {
				uint32 tcIn = kResampleBufferSize - mResampleLevel;

				if (tcIn > count)
					tcIn = count;

				count -= tcIn;

				if (mbStereo) {
					if (right) {
						for(uint32 i=0; i<tcIn; ++i) {
							mResampleBuffers[0][mResampleLevel] = *left++;
							mResampleBuffers[1][mResampleLevel++] = *right++;
						}
					} else {
						for(uint32 i=0; i<tcIn; ++i) {
							mResampleBuffers[0][mResampleLevel] = mResampleBuffers[1][mResampleLevel] = *left++;
							++mResampleLevel;
						}
					}
				} else {
					if (right) {
						for(uint32 i=0; i<tcIn; ++i) {
							mResampleBuffers[0][mResampleLevel++] = 0.5f * (*left++ + *right++);
						}
					} else {
						memcpy(&mResampleBuffers[0][mResampleLevel], left, sizeof(float) * tcIn);
						mResampleLevel += tcIn;
						left += tcIn;
					}
				}
			}

			if (!outputSamplesLeft)
				break;

			// process out samples
			while(mResampleLevel >= 8) {
				uint64 maxValidPoint = ((uint64)(mResampleLevel - 7) << 32) - 1;

				if (maxValidPoint <= mResampleAccum)
					break;

				uint32 tcOut = (uint32)((maxValidPoint - mResampleAccum) / mResampleRate);

				if (!tcOut)
					break;

				if (mbStereo) {
					if (tcOut > 512)
						tcOut = 512;

					mResampleAccum = ATFilterResampleStereo16(buf, mResampleBuffers[0], mResampleBuffers[1], tcOut, mResampleAccum, mResampleRate, true);

					mpMediaEncoder->WriteAudio(buf, 2*sizeof(sint16)*tcOut);
				} else {
					if (tcOut > 1024)
						tcOut = 1024;

					mResampleAccum = ATFilterResampleMono16(buf, mResampleBuffers[0], tcOut, mResampleAccum, mResampleRate, true);
					mpMediaEncoder->WriteAudio(buf, sizeof(sint16)*tcOut);
				}

				outputSamplesLeft -= tcOut;
			}

			// shift resampling buffer if required
			uint32 baseIdx = (uint32)(mResampleAccum >> 32);
			if (baseIdx >= (kResampleBufferSize >> 1)) {
				size_t bytesToMove = sizeof(float) * (mResampleLevel - baseIdx);

				memmove(mResampleBuffers[0], &mResampleBuffers[0][baseIdx], bytesToMove);

				if (mbStereo)
					memmove(mResampleBuffers[1], &mResampleBuffers[1][baseIdx], bytesToMove);

				mResampleAccum = (uint32)mResampleAccum;
				mResampleLevel -= baseIdx;
			}
		}

		if (outputSamples)
			mpMediaEncoder->EndAudioFrame();

		VDASSERT(!count);

	} catch(MyError& e) {
		RaiseError(std::move(e));
	}
}

void ATVideoWriter::RaiseError(MyError&& e) {
	if (!mbErrorState) {
		mbErrorState = true;
		mError = std::move(e);
	}
}

void ATCreateVideoWriter(IATVideoWriter **w) {
	*w = new ATVideoWriter;
}
