#include <stdafx.h>

#define INITGUID

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
#include "videowriter.h"
#include "aviwriter.h"
#include "gtia.h"
#include "uirender.h"

#include <vd2/system/w32assist.h>

#include <windows.h>
#include <mfidl.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <uuids.h>

///////////////////////////////////////////////////////////////////////////////

class IATVideoEncoder {
public:
	virtual ~IATVideoEncoder() {}

	virtual void Compress(const VDPixmap& px, bool intra, bool encodeAll) = 0;

	virtual uint32 GetEncodedLength() const = 0;
	virtual const void *GetEncodedData() const = 0;

	virtual bool GetDebugInfo(ATVideoRecordingDebugInfo& debugInfo) { return false; }
};

///////////////////////////////////////////////////////////////////////////////

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

	// The pack buffer is extended by 15 bytes so we can pad it at the beginning to
	// align the main image data for the frame (see mEncodedOffset).
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

	// The regular motion vector buffer is padded slightly, which we must skip.
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
	// The inter frame header consists of:
	// - one byte for inter frame
	// - two motion vector bytes per block
	// - two additional bytes if block count is odd
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

class ATAVIEncoder final : public IATMediaEncoder {
public:
	ATAVIEncoder(const wchar_t *filename, ATVideoEncoding venc, uint32 w, uint32 h, const VDFraction& frameRate, const uint32 *palette, double samplingRate, bool stereo, bool encodeAllFrames);

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

ATAVIEncoder::ATAVIEncoder(const wchar_t *filename, ATVideoEncoding venc, uint32 w, uint32 h, const VDFraction& frameRate, const uint32 *palette, double samplingRate, bool stereo, bool encodeAllFrames) {
	mbEncodeAllFrames = encodeAllFrames;
	mKeyCounter = 0;

	if (venc == kATVideoEncoding_Raw)
		mKeyInterval = 1;
	else
		mKeyInterval = 60;

	mFile = VDCreateMediaOutputAVIFile();

	mVideoStream = mFile->createVideoStream();
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
	mAudioStream->partialWriteBegin(IVDMediaOutputStream::kFlagKeyFrame, bytes, samples);
}

void ATAVIEncoder::WriteAudio(const sint16 *data, uint32 bytes) {
	mAudioStream->partialWrite(data, bytes);
}

void ATAVIEncoder::EndAudioFrame() {
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

///////////////////////////////////////////////////////////////////////////////

class ATMFSampleAllocatorW32 final : public IMFSinkWriterCallback {
public:
	LPVOID AddSample(IMFSample *sampleAdoptRef);
	bool AllocateCachedSample(IMFSample **sample);
	HRESULT WaitForFinalize();
	void Shutdown();

public: // IUnknown
	DWORD STDMETHODCALLTYPE AddRef() override;
	DWORD STDMETHODCALLTYPE Release() override;
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **ppvObj) override;

public:	// IMFSinkWriterCallback
	HRESULT STDMETHODCALLTYPE OnFinalize(HRESULT hrStatus) override;
	HRESULT STDMETHODCALLTYPE OnMarker(DWORD dwStreamIndex, LPVOID pvContext) override;

private:
	uint32 mSampleQueueMax = 32;

	VDAtomicInt mRefCount { 0 };
	VDSignal mFinalized;
	HRESULT mFinalizationResult = S_OK;

	VDCriticalSection mMutex;
	uint32 mSampleQueueBeginId = 0;
	uint32 mSampleQueueNextInUseId = 0;
	uint32 mSampleQueueEndId = 0;
	vdfastdeque<IMFSample *> mSamples;
};

LPVOID ATMFSampleAllocatorW32::AddSample(IMFSample *sampleAdoptRef) {
	IMFSample *sampleToDrop = nullptr;
	uint32 fenceId = 0;
	vdsynchronized(mMutex) {
		VDASSERT(mSampleQueueEndId == (uint32)(mSampleQueueBeginId + mSamples.size()));
		VDASSERT((uint32)(mSampleQueueNextInUseId - mSampleQueueBeginId) <= (uint32)(mSampleQueueEndId - mSampleQueueBeginId));

		mSamples.push_back(sampleAdoptRef);

		if (mSamples.size() > mSampleQueueMax) {
			sampleToDrop = mSamples.front();
			mSamples.pop_front();

			++mSampleQueueBeginId;
			++mSampleQueueNextInUseId;
		}

		fenceId = ++mSampleQueueEndId;
	}

	if (sampleToDrop)
		sampleToDrop->Release();

	return (LPVOID)(uintptr)fenceId;
}

bool ATMFSampleAllocatorW32::AllocateCachedSample(IMFSample **sample) {
	IMFSample *reclaimedSample = nullptr;

	vdsynchronized(mMutex) {
		VDASSERT(mSampleQueueEndId == (uint32)(mSampleQueueBeginId + mSamples.size()));
		VDASSERT((uint32)(mSampleQueueNextInUseId - mSampleQueueBeginId) <= (uint32)(mSampleQueueEndId - mSampleQueueBeginId));

		if (mSampleQueueNextInUseId != mSampleQueueBeginId) {
			reclaimedSample = mSamples.front();
			mSamples.pop_front();

			++mSampleQueueBeginId;
		}
	}

	if (!reclaimedSample)
		return false;

	*sample = reclaimedSample;
	return true;
}

HRESULT ATMFSampleAllocatorW32::WaitForFinalize() {
	WaitForSingleObjectEx((HANDLE)mFinalized.getHandle(), INFINITE, TRUE);

	return mFinalizationResult;
}

void ATMFSampleAllocatorW32::Shutdown() {
	vdfastdeque<IMFSample *> samples;

	vdsynchronized(mMutex) {
		samples.swap(mSamples);
	}

	for(IMFSample *sample : samples)
		sample->Release();
}

DWORD STDMETHODCALLTYPE ATMFSampleAllocatorW32::AddRef() {
	VDASSERT(mRefCount < 1000000);
	return ++mRefCount;
}

DWORD STDMETHODCALLTYPE ATMFSampleAllocatorW32::Release() {
	uint32 rc = --mRefCount;
	VDASSERT(rc >= 0 && rc < 1000000);

	if (!rc)
		delete this;

	return rc;
}

HRESULT STDMETHODCALLTYPE ATMFSampleAllocatorW32::QueryInterface(REFIID iid, void **ppvObj) {
	if (!ppvObj)
		return E_POINTER;

	if (iid == IID_IUnknown) {
		*ppvObj = static_cast<IUnknown *>(this);
	} else if (iid == __uuidof(IMFSinkWriterCallback)) {
		*ppvObj = static_cast<IMFSinkWriterCallback *>(this);
	} else {
		*ppvObj = nullptr;
		return E_NOINTERFACE;
	}

	AddRef();
	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATMFSampleAllocatorW32::OnFinalize(HRESULT hrStatus) {
	mFinalizationResult = hrStatus;

	mFinalized.signal();
	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATMFSampleAllocatorW32::OnMarker(DWORD dwStreamIndex, LPVOID pvContext) {
	uint32 sampleFenceId = (uint32)(uintptr)pvContext;

	vdsynchronized(mMutex) {
		uint32 newOffset = sampleFenceId - mSampleQueueBeginId;

		// It is legitimate for the fence offset to be outside of the current sample queue window.
		// This happens if Media Foundation decides to buffer more samples than we care to track in
		// our queue, which has a safety limit to prevent a perpetual memory leak if fencing fails.
		// In that case, we will have preemptively advanced the beginning of the queue past submitted
		// fences.

		if (newOffset <= mSampleQueueEndId - mSampleQueueBeginId) {
			uint32 oldOffset = mSampleQueueNextInUseId - mSampleQueueBeginId;

			if (newOffset > oldOffset) {
				mSampleQueueNextInUseId = sampleFenceId;
				VDASSERT((uint32)(mSampleQueueNextInUseId - mSampleQueueBeginId) <= (uint32)(mSampleQueueEndId - mSampleQueueBeginId));
			}
		}
	}

	return S_OK;
}

///////////////////////////////////////////////////////////////////////////////

class ATMediaFoundationEncoderW32 final : public IATMediaEncoder {
public:
	ATMediaFoundationEncoderW32(const wchar_t *filename, ATVideoEncoding venc, uint32 videoBitRate, uint32 audioBitRate, uint32 w, uint32 h, const VDFraction& frameRate, const uint32 *palette, double samplingRate, bool stereo, bool useYUV);
	~ATMediaFoundationEncoderW32();

	sint64 GetCurrentSize() override;

	void WriteVideo(const VDPixmap& px) override;
	void BeginAudioFrame(uint32 bytes, uint32 samples) override;
	void WriteAudio(const sint16 *data, uint32 bytes) override;
	void EndAudioFrame() override;

	bool Finalize(MyError& e) override;

private:
	void Init(const wchar_t *filename, ATVideoEncoding venc, uint32 videoBitRate, uint32 audioBitRate, uint32 w, uint32 h, const VDFraction& frameRate, const uint32 *palette, double samplingRate, bool stereo, bool useYUV);
	void Shutdown();

	bool mbMFInited = false;
	vdrefptr<IMFSinkWriter> mpSinkWriter;

	VDPixmapLayout mVideoFrameLayout {};
	uint32 mVideoFrameSize = 0;
	uint32 mVideoFrameCount = 0;
	VDFraction mVideoFrameRate { 0, 0 };
	LONGLONG mVideoNextSampleTime = 0;

	VDPixmapCachedBlitter mVideoBlitter;

	vdrefptr<IMFMediaBuffer> mpAudioBuffer;
	BYTE *mpAudioDst = nullptr;
	BYTE *mpAudioDstEnd = nullptr;
	uint64 mAudioSamplesWritten = 0;
	LONGLONG mAudioNextSampleTime = 0;
	bool mbAudioConvertToStereo = false;
	uint32 mAudioSampleSize = 0;

	vdrefptr<ATMFSampleAllocatorW32> mpVideoSampleAllocator;

	DWORD mVideoStreamIndex = 0;
	DWORD mAudioStreamIndex = 0;

	HMODULE mhmodMFPlat = nullptr;
	HMODULE mhmodMFReadWrite = nullptr;

	decltype(&MFStartup) mpfnMFStartup = nullptr;
	decltype(&MFShutdown) mpfnMFShutdown = nullptr;
	decltype(&MFCreateMemoryBuffer) mpfnMFCreateMemoryBuffer = nullptr;
	decltype(&MFCreateAlignedMemoryBuffer) mpfnMFCreateAlignedMemoryBuffer = nullptr;
	decltype(&MFCreateMediaType) mpfnMFCreateMediaType = nullptr;
	decltype(&MFCreateSample) mpfnMFCreateSample = nullptr;
	decltype(&MFCreateAttributes) mpfnMFCreateAttributes = nullptr;
	decltype(&MFCreateSinkWriterFromURL) mpfnMFCreateSinkWriterFromURL = nullptr;

	class HRVerify {
	public:
		HRVerify(const wchar_t *label = L"Media encoding failed") : mpLabel(label) {}

		void operator+=(HRESULT hr) const;

	private:
		const wchar_t *mpLabel = nullptr;
	};
};

void ATMediaFoundationEncoderW32::HRVerify::operator+=(HRESULT hr) const {
	if (FAILED(hr)) {
		const char *name = nullptr;

		switch(hr) {
#define X(code) case code: name = #code; break;
			X(MF_E_PLATFORM_NOT_INITIALIZED)
			X(MF_E_BUFFERTOOSMALL)
			X(MF_E_INVALIDREQUEST)
			X(MF_E_INVALIDSTREAMNUMBER)
			X(MF_E_INVALIDMEDIATYPE)
			X(MF_E_NOTACCEPTING)
			X(MF_E_NOT_INITIALIZED)
			X(MF_E_UNSUPPORTED_REPRESENTATION)
			X(MF_E_NO_MORE_TYPES)
			X(MF_E_UNSUPPORTED_SERVICE)
			X(MF_E_UNEXPECTED)
			X(MF_E_INVALIDNAME)
			X(MF_E_INVALIDTYPE)
			X(MF_E_INVALID_FILE_FORMAT)
			X(MF_E_INVALIDINDEX)
			X(MF_E_INVALID_TIMESTAMP)
			X(MF_E_UNSUPPORTED_SCHEME)
			X(MF_E_UNSUPPORTED_BYTESTREAM_TYPE)
			X(MF_E_UNSUPPORTED_TIME_FORMAT)
			X(MF_E_NO_SAMPLE_TIMESTAMP)
			X(MF_E_NO_SAMPLE_DURATION)
			X(MF_E_INVALID_STREAM_DATA)
			X(MF_E_RT_UNAVAILABLE)
			X(MF_E_UNSUPPORTED_RATE)
			X(MF_E_THINNING_UNSUPPORTED)
			X(MF_E_REVERSE_UNSUPPORTED)
			X(MF_E_UNSUPPORTED_RATE_TRANSITION)
			X(MF_E_RATE_CHANGE_PREEMPTED)
			X(MF_E_NOT_FOUND)
			X(MF_E_NOT_AVAILABLE)
			X(MF_E_NO_CLOCK)
			X(MF_E_MULTIPLE_BEGIN)
			X(MF_E_MULTIPLE_SUBSCRIBERS)
			X(MF_E_TIMER_ORPHANED)
			X(MF_E_STATE_TRANSITION_PENDING)
			X(MF_E_UNSUPPORTED_STATE_TRANSITION)
			X(MF_E_UNRECOVERABLE_ERROR_OCCURRED)
			X(MF_E_SAMPLE_HAS_TOO_MANY_BUFFERS)
			X(MF_E_SAMPLE_NOT_WRITABLE)
			X(MF_E_INVALID_KEY)
			X(MF_E_BAD_STARTUP_VERSION)
			X(MF_E_UNSUPPORTED_CAPTION)
			X(MF_E_INVALID_POSITION)
			X(MF_E_ATTRIBUTENOTFOUND)
			X(MF_E_PROPERTY_TYPE_NOT_ALLOWED)
			X(MF_E_PROPERTY_TYPE_NOT_SUPPORTED)
			X(MF_E_PROPERTY_EMPTY)
			X(MF_E_PROPERTY_NOT_EMPTY)
			X(MF_E_PROPERTY_VECTOR_NOT_ALLOWED)
			X(MF_E_PROPERTY_VECTOR_REQUIRED)
			X(MF_E_OPERATION_CANCELLED)
			X(MF_E_BYTESTREAM_NOT_SEEKABLE)
			X(MF_E_DISABLED_IN_SAFEMODE)
			X(MF_E_CANNOT_PARSE_BYTESTREAM)
			X(MF_E_SOURCERESOLVER_MUTUALLY_EXCLUSIVE_FLAGS)
			X(MF_E_MEDIAPROC_WRONGSTATE)
			X(MF_E_RT_THROUGHPUT_NOT_AVAILABLE)
			X(MF_E_RT_TOO_MANY_CLASSES)
			X(MF_E_RT_WOULDBLOCK)
			X(MF_E_NO_BITPUMP)
			X(MF_E_RT_OUTOFMEMORY)
			X(MF_E_RT_WORKQUEUE_CLASS_NOT_SPECIFIED)
			X(MF_E_INSUFFICIENT_BUFFER)
			X(MF_E_CANNOT_CREATE_SINK)
			X(MF_E_BYTESTREAM_UNKNOWN_LENGTH)
			X(MF_E_SESSION_PAUSEWHILESTOPPED)
			X(MF_E_FORMAT_CHANGE_NOT_SUPPORTED)
			X(MF_E_INVALID_WORKQUEUE)
			X(MF_E_DRM_UNSUPPORTED)
			X(MF_E_UNAUTHORIZED)
			X(MF_E_OUT_OF_RANGE)
			X(MF_E_INVALID_CODEC_MERIT)
			X(MF_E_HW_MFT_FAILED_START_STREAMING)
			X(MF_E_OPERATION_IN_PROGRESS)
			X(MF_E_HARDWARE_DRM_UNSUPPORTED)
			X(MF_E_DURATION_TOO_LONG)
			X(MF_E_OPERATION_UNSUPPORTED_AT_D3D_FEATURE_LEVEL)
			X(MF_E_UNSUPPORTED_MEDIATYPE_AT_D3D_FEATURE_LEVEL)
			X(MF_E_ASF_PARSINGINCOMPLETE)
			X(MF_E_ASF_MISSINGDATA)
			X(MF_E_ASF_INVALIDDATA)
			X(MF_E_ASF_OPAQUEPACKET)
			X(MF_E_ASF_NOINDEX)
			X(MF_E_ASF_OUTOFRANGE)
			X(MF_E_ASF_INDEXNOTLOADED)
			X(MF_E_ASF_TOO_MANY_PAYLOADS)
			X(MF_E_ASF_UNSUPPORTED_STREAM_TYPE)
			X(MF_E_ASF_DROPPED_PACKET)
			X(MF_E_NO_EVENTS_AVAILABLE)
			X(MF_E_INVALID_STATE_TRANSITION)
			X(MF_E_END_OF_STREAM)
			X(MF_E_SHUTDOWN)
			X(MF_E_MP3_NOTFOUND)
			X(MF_E_MP3_OUTOFDATA)
			X(MF_E_MP3_NOTMP3)
			X(MF_E_MP3_NOTSUPPORTED)
			X(MF_E_NO_DURATION)
			X(MF_E_INVALID_FORMAT)
			X(MF_E_PROPERTY_NOT_FOUND)
			X(MF_E_PROPERTY_READ_ONLY)
			X(MF_E_PROPERTY_NOT_ALLOWED)
			X(MF_E_MEDIA_SOURCE_NOT_STARTED)
			X(MF_E_UNSUPPORTED_FORMAT)
			X(MF_E_MP3_BAD_CRC)
			X(MF_E_NOT_PROTECTED)
			X(MF_E_MEDIA_SOURCE_WRONGSTATE)
			X(MF_E_MEDIA_SOURCE_NO_STREAMS_SELECTED)
			X(MF_E_CANNOT_FIND_KEYFRAME_SAMPLE)
			X(MF_E_UNSUPPORTED_CHARACTERISTICS)
			X(MF_E_NO_AUDIO_RECORDING_DEVICE)
			X(MF_E_AUDIO_RECORDING_DEVICE_IN_USE)
			X(MF_E_AUDIO_RECORDING_DEVICE_INVALIDATED)
			X(MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED)
			X(MF_E_VIDEO_RECORDING_DEVICE_PREEMPTED)
			X(MF_E_NETWORK_RESOURCE_FAILURE)
			X(MF_E_NET_WRITE)
			X(MF_E_NET_READ)
			X(MF_E_NET_REQUIRE_NETWORK)
			X(MF_E_NET_REQUIRE_ASYNC)
			X(MF_E_NET_BWLEVEL_NOT_SUPPORTED)
			X(MF_E_NET_STREAMGROUPS_NOT_SUPPORTED)
			X(MF_E_NET_MANUALSS_NOT_SUPPORTED)
			X(MF_E_NET_INVALID_PRESENTATION_DESCRIPTOR)
			X(MF_E_NET_CACHESTREAM_NOT_FOUND)
			X(MF_E_NET_REQUIRE_INPUT)
			X(MF_E_NET_REDIRECT)
			X(MF_E_NET_REDIRECT_TO_PROXY)
			X(MF_E_NET_TOO_MANY_REDIRECTS)
			X(MF_E_NET_TIMEOUT)
			X(MF_E_NET_CLIENT_CLOSE)
			X(MF_E_NET_BAD_CONTROL_DATA)
			X(MF_E_NET_INCOMPATIBLE_SERVER)
			X(MF_E_NET_UNSAFE_URL)
			X(MF_E_NET_CACHE_NO_DATA)
			X(MF_E_NET_EOL)
			X(MF_E_NET_BAD_REQUEST)
			X(MF_E_NET_INTERNAL_SERVER_ERROR)
			X(MF_E_NET_SESSION_NOT_FOUND)
			X(MF_E_NET_NOCONNECTION)
			X(MF_E_NET_CONNECTION_FAILURE)
			X(MF_E_NET_INCOMPATIBLE_PUSHSERVER)
			X(MF_E_NET_SERVER_ACCESSDENIED)
			X(MF_E_NET_PROXY_ACCESSDENIED)
			X(MF_E_NET_CANNOTCONNECT)
			X(MF_E_NET_INVALID_PUSH_TEMPLATE)
			X(MF_E_NET_INVALID_PUSH_PUBLISHING_POINT)
			X(MF_E_NET_BUSY)
			X(MF_E_NET_RESOURCE_GONE)
			X(MF_E_NET_ERROR_FROM_PROXY)
			X(MF_E_NET_PROXY_TIMEOUT)
			X(MF_E_NET_SERVER_UNAVAILABLE)
			X(MF_E_NET_TOO_MUCH_DATA)
			X(MF_E_NET_SESSION_INVALID)
			X(MF_E_OFFLINE_MODE)
			X(MF_E_NET_UDP_BLOCKED)
			X(MF_E_NET_UNSUPPORTED_CONFIGURATION)
			X(MF_E_NET_PROTOCOL_DISABLED)
			X(MF_E_NET_COMPANION_DRIVER_DISCONNECT)
			X(MF_E_ALREADY_INITIALIZED)
			X(MF_E_BANDWIDTH_OVERRUN)
			X(MF_E_LATE_SAMPLE)
			X(MF_E_FLUSH_NEEDED)
			X(MF_E_INVALID_PROFILE)
			X(MF_E_INDEX_NOT_COMMITTED)
			X(MF_E_NO_INDEX)
			X(MF_E_CANNOT_INDEX_IN_PLACE)
			X(MF_E_MISSING_ASF_LEAKYBUCKET)
			X(MF_E_INVALID_ASF_STREAMID)
			X(MF_E_STREAMSINK_REMOVED)
			X(MF_E_STREAMSINKS_OUT_OF_SYNC)
			X(MF_E_STREAMSINKS_FIXED)
			X(MF_E_STREAMSINK_EXISTS)
			X(MF_E_SAMPLEALLOCATOR_CANCELED)
			X(MF_E_SAMPLEALLOCATOR_EMPTY)
			X(MF_E_SINK_ALREADYSTOPPED)
			X(MF_E_ASF_FILESINK_BITRATE_UNKNOWN)
			X(MF_E_SINK_NO_STREAMS)
			X(MF_E_METADATA_TOO_LONG)
			X(MF_E_SINK_NO_SAMPLES_PROCESSED)
			X(MF_E_SINK_HEADERS_NOT_FOUND)
			X(MF_E_VIDEO_REN_NO_PROCAMP_HW)
			X(MF_E_VIDEO_REN_NO_DEINTERLACE_HW)
			X(MF_E_VIDEO_REN_COPYPROT_FAILED)
			X(MF_E_VIDEO_REN_SURFACE_NOT_SHARED)
			X(MF_E_VIDEO_DEVICE_LOCKED)
			X(MF_E_NEW_VIDEO_DEVICE)
			X(MF_E_NO_VIDEO_SAMPLE_AVAILABLE)
			X(MF_E_NO_AUDIO_PLAYBACK_DEVICE)
			X(MF_E_AUDIO_PLAYBACK_DEVICE_IN_USE)
			X(MF_E_AUDIO_PLAYBACK_DEVICE_INVALIDATED)
			X(MF_E_AUDIO_SERVICE_NOT_RUNNING)
			X(MF_E_AUDIO_BUFFER_SIZE_ERROR)
			X(MF_E_AUDIO_CLIENT_WRAPPER_SPOOF_ERROR)
			X(MF_E_TOPO_INVALID_OPTIONAL_NODE)
			X(MF_E_TOPO_CANNOT_FIND_DECRYPTOR)
			X(MF_E_TOPO_CODEC_NOT_FOUND)
			X(MF_E_TOPO_CANNOT_CONNECT)
			X(MF_E_TOPO_UNSUPPORTED)
			X(MF_E_TOPO_INVALID_TIME_ATTRIBUTES)
			X(MF_E_TOPO_LOOPS_IN_TOPOLOGY)
			X(MF_E_TOPO_MISSING_PRESENTATION_DESCRIPTOR)
			X(MF_E_TOPO_MISSING_STREAM_DESCRIPTOR)
			X(MF_E_TOPO_STREAM_DESCRIPTOR_NOT_SELECTED)
			X(MF_E_TOPO_MISSING_SOURCE)
			X(MF_E_TOPO_SINK_ACTIVATES_UNSUPPORTED)
			X(MF_E_SEQUENCER_UNKNOWN_SEGMENT_ID)
			X(MF_E_NO_SOURCE_IN_CACHE)
			X(MF_E_TRANSFORM_TYPE_NOT_SET)
			X(MF_E_TRANSFORM_STREAM_CHANGE)
			X(MF_E_TRANSFORM_INPUT_REMAINING)
			X(MF_E_TRANSFORM_PROFILE_MISSING)
			X(MF_E_TRANSFORM_PROFILE_INVALID_OR_CORRUPT)
			X(MF_E_TRANSFORM_PROFILE_TRUNCATED)
			X(MF_E_TRANSFORM_PROPERTY_PID_NOT_RECOGNIZED)
			X(MF_E_TRANSFORM_PROPERTY_VARIANT_TYPE_WRONG)
			X(MF_E_TRANSFORM_PROPERTY_NOT_WRITEABLE)
			X(MF_E_TRANSFORM_PROPERTY_ARRAY_VALUE_WRONG_NUM_DIM)
			X(MF_E_TRANSFORM_PROPERTY_VALUE_SIZE_WRONG)
			X(MF_E_TRANSFORM_PROPERTY_VALUE_OUT_OF_RANGE)
			X(MF_E_TRANSFORM_PROPERTY_VALUE_INCOMPATIBLE)
			X(MF_E_TRANSFORM_NOT_POSSIBLE_FOR_CURRENT_OUTPUT_MEDIATYPE)
			X(MF_E_TRANSFORM_NOT_POSSIBLE_FOR_CURRENT_INPUT_MEDIATYPE)
			X(MF_E_TRANSFORM_NOT_POSSIBLE_FOR_CURRENT_MEDIATYPE_COMBINATION)
			X(MF_E_TRANSFORM_CONFLICTS_WITH_OTHER_CURRENTLY_ENABLED_FEATURES)
			X(MF_E_TRANSFORM_NEED_MORE_INPUT)
			X(MF_E_TRANSFORM_NOT_POSSIBLE_FOR_CURRENT_SPKR_CONFIG)
			X(MF_E_TRANSFORM_CANNOT_CHANGE_MEDIATYPE_WHILE_PROCESSING)
			X(MF_E_UNSUPPORTED_D3D_TYPE)
			X(MF_E_TRANSFORM_ASYNC_LOCKED)
			X(MF_E_TRANSFORM_CANNOT_INITIALIZE_ACM_DRIVER)
			X(MF_E_TRANSFORM_STREAM_INVALID_RESOLUTION)
			X(MF_E_TRANSFORM_ASYNC_MFT_NOT_SUPPORTED)
			X(MF_E_TRANSFORM_EXATTRIBUTE_NOT_SUPPORTED)
			X(MF_E_LICENSE_INCORRECT_RIGHTS)
			X(MF_E_LICENSE_OUTOFDATE)
			X(MF_E_LICENSE_REQUIRED)
			X(MF_E_DRM_HARDWARE_INCONSISTENT)
			X(MF_E_NO_CONTENT_PROTECTION_MANAGER)
			X(MF_E_LICENSE_RESTORE_NO_RIGHTS)
			X(MF_E_BACKUP_RESTRICTED_LICENSE)
			X(MF_E_LICENSE_RESTORE_NEEDS_INDIVIDUALIZATION)
			X(MF_E_COMPONENT_REVOKED)
			X(MF_E_TRUST_DISABLED)
			X(MF_E_WMDRMOTA_NO_ACTION)
			X(MF_E_WMDRMOTA_ACTION_ALREADY_SET)
			X(MF_E_WMDRMOTA_DRM_HEADER_NOT_AVAILABLE)
			X(MF_E_WMDRMOTA_DRM_ENCRYPTION_SCHEME_NOT_SUPPORTED)
			X(MF_E_WMDRMOTA_ACTION_MISMATCH)
			X(MF_E_WMDRMOTA_INVALID_POLICY)
			X(MF_E_POLICY_UNSUPPORTED)
			X(MF_E_OPL_NOT_SUPPORTED)
			X(MF_E_TOPOLOGY_VERIFICATION_FAILED)
			X(MF_E_SIGNATURE_VERIFICATION_FAILED)
			X(MF_E_DEBUGGING_NOT_ALLOWED)
			X(MF_E_CODE_EXPIRED)
			X(MF_E_GRL_VERSION_TOO_LOW)
			X(MF_E_GRL_RENEWAL_NOT_FOUND)
			X(MF_E_GRL_EXTENSIBLE_ENTRY_NOT_FOUND)
			X(MF_E_KERNEL_UNTRUSTED)
			X(MF_E_PEAUTH_UNTRUSTED)
			X(MF_E_NON_PE_PROCESS)
			X(MF_E_REBOOT_REQUIRED)
			X(MF_E_GRL_INVALID_FORMAT)
			X(MF_E_GRL_UNRECOGNIZED_FORMAT)
			X(MF_E_ALL_PROCESS_RESTART_REQUIRED)
			X(MF_E_PROCESS_RESTART_REQUIRED)
			X(MF_E_USERMODE_UNTRUSTED)
			X(MF_E_PEAUTH_SESSION_NOT_STARTED)
			X(MF_E_PEAUTH_PUBLICKEY_REVOKED)
			X(MF_E_GRL_ABSENT)
			X(MF_E_PE_UNTRUSTED)
			X(MF_E_PEAUTH_NOT_STARTED)
			X(MF_E_INCOMPATIBLE_SAMPLE_PROTECTION)
			X(MF_E_PE_SESSIONS_MAXED)
			X(MF_E_HIGH_SECURITY_LEVEL_CONTENT_NOT_ALLOWED)
			X(MF_E_TEST_SIGNED_COMPONENTS_NOT_ALLOWED)
			X(MF_E_ITA_UNSUPPORTED_ACTION)
			X(MF_E_ITA_ERROR_PARSING_SAP_PARAMETERS)
			X(MF_E_POLICY_MGR_ACTION_OUTOFBOUNDS)
			X(MF_E_BAD_OPL_STRUCTURE_FORMAT)
			X(MF_E_ITA_UNRECOGNIZED_ANALOG_VIDEO_PROTECTION_GUID)
			X(MF_E_NO_PMP_HOST)
			X(MF_E_ITA_OPL_DATA_NOT_INITIALIZED)
			X(MF_E_ITA_UNRECOGNIZED_ANALOG_VIDEO_OUTPUT)
			X(MF_E_ITA_UNRECOGNIZED_DIGITAL_VIDEO_OUTPUT)
			X(MF_E_RESOLUTION_REQUIRES_PMP_CREATION_CALLBACK)
			X(MF_E_INVALID_AKE_CHANNEL_PARAMETERS)
			X(MF_E_CONTENT_PROTECTION_SYSTEM_NOT_ENABLED)
			X(MF_E_UNSUPPORTED_CONTENT_PROTECTION_SYSTEM)
			X(MF_E_DRM_MIGRATION_NOT_SUPPORTED)
			X(MF_E_HDCP_AUTHENTICATION_FAILURE)
			X(MF_E_HDCP_LINK_FAILURE)
			X(MF_E_CLOCK_INVALID_CONTINUITY_KEY)
			X(MF_E_CLOCK_NO_TIME_SOURCE)
			X(MF_E_CLOCK_STATE_ALREADY_SET)
			X(MF_E_CLOCK_NOT_SIMPLE)
			X(MF_E_CLOCK_AUDIO_DEVICE_POSITION_UNEXPECTED)
			X(MF_E_CLOCK_AUDIO_RENDER_POSITION_UNEXPECTED)
			X(MF_E_CLOCK_AUDIO_RENDER_TIME_UNEXPECTED)
			X(MF_E_NO_MORE_DROP_MODES)
			X(MF_E_NO_MORE_QUALITY_LEVELS)
			X(MF_E_DROPTIME_NOT_SUPPORTED)
			X(MF_E_QUALITYKNOB_WAIT_LONGER)
			X(MF_E_QM_INVALIDSTATE)
			X(MF_E_TRANSCODE_NO_CONTAINERTYPE)
			X(MF_E_TRANSCODE_PROFILE_NO_MATCHING_STREAMS)
			X(MF_E_TRANSCODE_NO_MATCHING_ENCODER)
			X(MF_E_TRANSCODE_INVALID_PROFILE)
			X(MF_E_ALLOCATOR_NOT_INITIALIZED)
			X(MF_E_ALLOCATOR_NOT_COMMITED)
			X(MF_E_ALLOCATOR_ALREADY_COMMITED)
			X(MF_E_STREAM_ERROR)
			X(MF_E_INVALID_STREAM_STATE)
			X(MF_E_HW_STREAM_NOT_CONNECTED)
			X(MF_E_NO_CAPTURE_DEVICES_AVAILABLE)
			X(MF_E_CAPTURE_SINK_OUTPUT_NOT_SET)
			X(MF_E_CAPTURE_SINK_MIRROR_ERROR)
			X(MF_E_CAPTURE_SINK_ROTATE_ERROR)
			X(MF_E_CAPTURE_ENGINE_INVALID_OP)
			X(MF_E_CAPTURE_ENGINE_ALL_EFFECTS_REMOVED)
			X(MF_E_CAPTURE_SOURCE_NO_INDEPENDENT_PHOTO_STREAM_PRESENT)
			X(MF_E_CAPTURE_SOURCE_NO_VIDEO_STREAM_PRESENT)
			X(MF_E_CAPTURE_SOURCE_NO_AUDIO_STREAM_PRESENT)
			X(MF_E_CAPTURE_SOURCE_DEVICE_EXTENDEDPROP_OP_IN_PROGRESS)
			X(MF_E_CAPTURE_PROPERTY_SET_DURING_PHOTO)
			X(MF_E_CAPTURE_NO_SAMPLES_IN_QUEUE)
			X(MF_E_HW_ACCELERATED_THUMBNAIL_NOT_SUPPORTED)
			X(MF_E_UNSUPPORTED_CAPTURE_DEVICE_PRESENT)
			X(MF_E_TIMELINECONTROLLER_UNSUPPORTED_SOURCE_TYPE)
			X(MF_E_TIMELINECONTROLLER_NOT_ALLOWED)
			X(MF_E_TIMELINECONTROLLER_CANNOT_ATTACH)
			X(MF_E_MEDIA_EXTENSION_APPSERVICE_CONNECTION_FAILED)
			X(MF_E_MEDIA_EXTENSION_APPSERVICE_REQUEST_FAILED)
			X(MF_E_MEDIA_EXTENSION_PACKAGE_INTEGRITY_CHECK_FAILED)
			X(MF_E_MEDIA_EXTENSION_PACKAGE_LICENSE_INVALID)
#undef X
			default:
				break;
		}

		if (name)
			throw VDException(L"%ls: %hs (%08X)", mpLabel, name, hr);

		throw VDWin32Exception(L"%ls: %%s", hr, mpLabel);
	}
}

ATMediaFoundationEncoderW32::ATMediaFoundationEncoderW32(const wchar_t *filename, ATVideoEncoding venc, uint32 videoBitRate, uint32 audioBitRate, uint32 w, uint32 h, const VDFraction& frameRate, const uint32 *palette, double samplingRate, bool stereo, bool useYUV) {
	try {
		Init(filename, venc, videoBitRate, audioBitRate, w, h, frameRate, palette, samplingRate, stereo, useYUV);
	} catch(...) {
		MyError e;
		Finalize(e);
		throw;
	}
}

ATMediaFoundationEncoderW32::~ATMediaFoundationEncoderW32() {
	Shutdown();
}

void ATMediaFoundationEncoderW32::Init(const wchar_t *filename, ATVideoEncoding venc,
	uint32 videoBitRate, uint32 audioBitRate,
	uint32 w, uint32 h, const VDFraction& frameRate, const uint32 *palette, double samplingRate, bool stereo,
	bool useYUV)
{
	HRVerify verify;

	if (!VDIsAtLeastVistaW32())
		throw MyError("Cannot encode in this format as Media Foundation services are not available on this version of Windows.");

	mhmodMFPlat = VDLoadSystemLibraryW32("MFPlat.dll");
	if (!mhmodMFPlat)
		throw MyWin32Error("Unable to load MFPlat.dll: %%s", GetLastError());

	mhmodMFReadWrite = VDLoadSystemLibraryW32("MFReadWrite.dll");
	if (!mhmodMFReadWrite)
		throw MyWin32Error("Unable to load MFReadWrite.dll: %%s", GetLastError());

	const auto ResolveImport = [](auto*& fnptr, HMODULE hmod, const char *name) {
		fnptr = (std::remove_reference_t<decltype(fnptr)>)GetProcAddress(hmod, name);
		if (!fnptr)
			throw MyError("Unable to initialize Media Foundation: could not resolve function %s().", name);
	};
	
	ResolveImport(mpfnMFStartup,					mhmodMFPlat, "MFStartup");
	ResolveImport(mpfnMFShutdown,					mhmodMFPlat, "MFShutdown");
	ResolveImport(mpfnMFCreateMemoryBuffer,			mhmodMFPlat, "MFCreateMemoryBuffer");
	ResolveImport(mpfnMFCreateAlignedMemoryBuffer,	mhmodMFPlat, "MFCreateAlignedMemoryBuffer");
	ResolveImport(mpfnMFCreateMediaType,			mhmodMFPlat, "MFCreateMediaType");
	ResolveImport(mpfnMFCreateSample,				mhmodMFPlat, "MFCreateSample");
	ResolveImport(mpfnMFCreateAttributes,			mhmodMFPlat, "MFCreateAttributes");
	ResolveImport(mpfnMFCreateSinkWriterFromURL,	mhmodMFReadWrite, "MFCreateSinkWriterFromURL");

	mVideoFrameRate = frameRate;

	if (useYUV) {
		// We alias NV12 as Y8 in our layout -- we'll be using a custom blitter for this anyway.
		mVideoFrameSize = VDPixmapCreateLinearLayout(mVideoFrameLayout, nsVDPixmap::kPixFormat_Y8, w, h + (h >> 1), 1);

		// Round the frame size up to a multiple of 16 and add another 32 bytes. This lets us
		// safely overwrite up to a whole xmmword during the chroma conversion process for speed.
		mVideoFrameSize = (mVideoFrameSize + 15 + 32) & ~15;
	} else {
		mVideoFrameSize = VDPixmapCreateLinearLayout(mVideoFrameLayout, nsVDPixmap::kPixFormat_XRGB8888, w, h, 4);
		VDPixmapLayoutFlipV(mVideoFrameLayout);
	}

	verify += mpfnMFStartup(MF_VERSION, MFSTARTUP_LITE);
	mbMFInited = true;

	vdrefptr<IMFAttributes> sinkWriterAttributes;

	verify += mpfnMFCreateAttributes(~sinkWriterAttributes, 1);

	// Enable hardware encoders if possible as they are MUCH faster. Note that it is critical that we pass
	// YUV input in this case. The RGB32>YV12 converter in msvproc.dll is somewhat OK, but the RGB32>NV12
	// converter is unvectorized and very slow, and it is hit if the Intel hardware encoder is used. We
	// can do this much faster as we can convert to YUV pre-upscale with SSE2 code.
	//
	verify += sinkWriterAttributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, 1);

	// force the container type regardless of extension
	verify += sinkWriterAttributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE,
		venc == kATVideoEncoding_H264_AAC || venc == kATVideoEncoding_H264_MP3 ? MFTranscodeContainerType_MPEG4 : MFTranscodeContainerType_ASF);

	// create our sample allocator and bind it as a callback
	mpVideoSampleAllocator = new ATMFSampleAllocatorW32;

	verify += sinkWriterAttributes->SetUnknown(MF_SINK_WRITER_ASYNC_CALLBACK, mpVideoSampleAllocator);

	// create a sink writer
	verify += mpfnMFCreateSinkWriterFromURL(filename, nullptr, sinkWriterAttributes, ~mpSinkWriter);
	sinkWriterAttributes.clear();

	HRVerify sinkVerify(L"Media encoding setup failed");
	vdrefptr<IMFMediaType> mediaTypeOut;
	sinkVerify += mpfnMFCreateMediaType(~mediaTypeOut);

	sinkVerify += mediaTypeOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);

	switch(venc) {
		case kATVideoEncoding_WMV7:
			sinkVerify += mediaTypeOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_WMV1);
			break;

		case kATVideoEncoding_WMV9:
			sinkVerify += mediaTypeOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_WMV3);
			break;

		case kATVideoEncoding_H264_AAC:
		case kATVideoEncoding_H264_MP3:
			sinkVerify += mediaTypeOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
			break;
	}

	sinkVerify += mediaTypeOut->SetUINT32(MF_MT_AVG_BITRATE, std::clamp<uint32>(videoBitRate, 500000, 8000000));
	sinkVerify += mediaTypeOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	sinkVerify += MFSetAttributeSize(mediaTypeOut, MF_MT_FRAME_SIZE, w, h);
	sinkVerify += MFSetAttributeRatio(mediaTypeOut, MF_MT_FRAME_RATE, frameRate.getHi(), frameRate.getLo());
	sinkVerify += MFSetAttributeRatio(mediaTypeOut, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

	sinkVerify += mpSinkWriter->AddStream(mediaTypeOut, &mVideoStreamIndex);
	mediaTypeOut.clear();

	HRVerify videoVerify(L"Video encoding setup failed");
	vdrefptr<IMFMediaType> mediaTypeIn;
	videoVerify += mpfnMFCreateMediaType(~mediaTypeIn);

	videoVerify += mediaTypeIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);

	if (useYUV) {
		videoVerify += mediaTypeIn->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
		videoVerify += mediaTypeIn->SetUINT32(MF_MT_VIDEO_CHROMA_SITING, MFVideoChromaSubsampling_MPEG2);
		videoVerify += mediaTypeIn->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
		videoVerify += mediaTypeIn->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709);
	} else {
		videoVerify += mediaTypeIn->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
	}

	videoVerify += mediaTypeIn->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	videoVerify += mediaTypeIn->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);

	// This shouldn't be necessary since RGB32 defaults to bottom-up, but the WMV encoder flips the
	// video if it is not explicitly set.
	videoVerify += mediaTypeIn->SetUINT32(MF_MT_DEFAULT_STRIDE, useYUV ? w : (UINT32)0 - w*4);

	videoVerify += MFSetAttributeSize(mediaTypeIn, MF_MT_FRAME_SIZE, w, h);
	videoVerify += MFSetAttributeRatio(mediaTypeIn, MF_MT_FRAME_RATE, frameRate.getHi(), frameRate.getLo());
	videoVerify += MFSetAttributeRatio(mediaTypeIn, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

	videoVerify += mpSinkWriter->SetInputMediaType(mVideoStreamIndex, mediaTypeIn, nullptr);
	mediaTypeIn.clear();

	////////////////////////////////////////

	// WMAv8 has few mono-only modes, so force stereo
	mbAudioConvertToStereo = false;

	if (venc != kATVideoEncoding_H264_AAC && venc != kATVideoEncoding_H264_MP3 && !stereo) {
		mbAudioConvertToStereo = true;
		stereo = true;
	}

	const uint32 samplesPerSecond = 48000;
	const uint32 numChannels = stereo ? 2 : 1;

	mAudioSampleSize = numChannels * sizeof(sint16);

	HRVerify audioVerify(L"Audio encoding setup failed");
	audioVerify += mpfnMFCreateMediaType(~mediaTypeOut);

	audioVerify += mediaTypeOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	audioVerify += mediaTypeOut->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, samplesPerSecond);
	audioVerify += mediaTypeOut->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, numChannels);

	if (venc == kATVideoEncoding_H264_AAC) {
		audioVerify += mediaTypeOut->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
		audioVerify += mediaTypeOut->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);

		// The AAC encoder only accepts 12000, 16000, 20000, and 24000.
		audioVerify += mediaTypeOut->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, ((std::clamp<uint32>(audioBitRate, 96000, 192000) + 16000) / 32000) * 4000);
	} else if (venc == kATVideoEncoding_H264_MP3) {
		audioVerify += mediaTypeOut->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_MP3);

		// The MP3 Encoder MF filter only supports up to 128kbps in mono.
		const uint32 bitrate = ((std::clamp<uint32>(audioBitRate, 64000, numChannels > 1 ? 256000 : 128000) + 16000) / 32000) * 32000;
		audioVerify += mediaTypeOut->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, bitrate >> 3);

		MPEGLAYER3WAVEFORMAT wf;

		wf.wID = MPEGLAYER3_ID_MPEG;
		wf.fdwFlags = MPEGLAYER3_FLAG_PADDING_OFF;
		wf.nBlockSize = (144 * bitrate) / samplesPerSecond;
		wf.nFramesPerBlock = 1;
		wf.nCodecDelay = 0;

		audioVerify += mediaTypeOut->SetBlob(MF_MT_USER_DATA, (const UINT8 *)&wf.wID, 12);
		audioVerify += mediaTypeOut->SetUINT32(MF_MT_AUDIO_PREFER_WAVEFORMATEX, 1);
		audioVerify += mediaTypeOut->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 1);
	} else {
		audioVerify += mediaTypeOut->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_WMAudioV8);
		audioVerify += mediaTypeOut->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE);
		audioVerify += mediaTypeOut->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);

		// The WMA codecs are very picky about byte rate and block alignment. These values were
		// determined by checking the output types from the filter. Unfortunately, we can't do this
		// with Sink Writer as it creates the transform and sets the output type first on stream add.
		static constexpr struct WMAProfile {
			uint32 mByteRate;
			uint32 mBlockAlignment;
		} kWMAProfiles[] = {
			{ 24000, 8192 },
			{ 20001, 6827 },
			{ 16002, 5462 },
			{ 12000, 4096 },
		};

		const auto& profile = kWMAProfiles[(std::clamp<uint32>(audioBitRate, 96000, 192000) - 96000 + 16000) / 32000];

		audioVerify += mediaTypeOut->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, profile.mByteRate);
		audioVerify += mediaTypeOut->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, profile.mBlockAlignment);
	}

	audioVerify += mpSinkWriter->AddStream(mediaTypeOut, &mAudioStreamIndex);
	mediaTypeOut.clear();

	audioVerify += mpfnMFCreateMediaType(~mediaTypeIn);

	audioVerify += mediaTypeIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	audioVerify += mediaTypeIn->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
	audioVerify += mediaTypeIn->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
	audioVerify += mediaTypeIn->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, samplesPerSecond);
	audioVerify += mediaTypeIn->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, samplesPerSecond * mAudioSampleSize);
	audioVerify += mediaTypeIn->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, numChannels);
	audioVerify += mediaTypeIn->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, mAudioSampleSize);

	if (venc == kATVideoEncoding_H264_MP3)
		audioVerify += mediaTypeIn->SetUINT32(MF_MT_AUDIO_PREFER_WAVEFORMATEX, 1);

	audioVerify += mpSinkWriter->SetInputMediaType(mAudioStreamIndex, mediaTypeIn, nullptr);
	mediaTypeIn.clear();

	////////////////////////////////////////

	verify += mpSinkWriter->BeginWriting();
}

void ATMediaFoundationEncoderW32::Shutdown() {
	if (mpSinkWriter) {
		HRESULT hr = mpSinkWriter->Finalize();

		// Since the video sample allocator registers as a callback on the sink writer, the finalize
		// is synchronous and we must wait for it.
		if (SUCCEEDED(hr) && mpVideoSampleAllocator)
			mpVideoSampleAllocator->WaitForFinalize();

		mpSinkWriter.clear();

		mpVideoSampleAllocator->Shutdown();
		mpVideoSampleAllocator = nullptr;
	}

	mpAudioBuffer.clear();

	if (mbMFInited) {
		mbMFInited = false;
		mpfnMFShutdown();
	}

	if (mhmodMFReadWrite) {
		FreeLibrary(mhmodMFReadWrite);
		mhmodMFReadWrite = nullptr;
	}

	if (mhmodMFPlat) {
		FreeLibrary(mhmodMFPlat);
		mhmodMFPlat = nullptr;
	}
}

bool ATMediaFoundationEncoderW32::Finalize(MyError& e) {
	if (mpSinkWriter) {
		try {
			HRVerify verify;
			verify += mpSinkWriter->Finalize();

			// Since the video sample allocator registers as a callback on the sink writer, the finalize
			// is synchronous and we must wait for it.
			if (mpVideoSampleAllocator)
				verify += mpVideoSampleAllocator->WaitForFinalize();

		} catch(VDException& error) {
			e = std::move(error);
		}

		mpSinkWriter.clear();
	}

	return e.empty();
}

sint64 ATMediaFoundationEncoderW32::GetCurrentSize() {
	if (mpSinkWriter) {
		MF_SINK_WRITER_STATISTICS stats { sizeof(MF_SINK_WRITER_STATISTICS) };

		HRESULT hr = mpSinkWriter->GetStatistics(MF_SINK_WRITER_ALL_STREAMS, &stats);
		if (SUCCEEDED(hr))
			return stats.qwByteCountProcessed;
	}

	return 0;
}

namespace {
	void BlitChroma444ToNV12_Reference(void *uvdst, ptrdiff_t uvpitch, const void *usrc, ptrdiff_t upitch, const void *vsrc, ptrdiff_t vpitch, uint32 w, uint32 h) {
		uint8 *uvdst8 = (uint8 *)uvdst;
		const uint8 *usrc8 = (uint8 *)usrc;
		const uint8 *vsrc8 = (uint8 *)vsrc;

		while(h--) {
			uint8 *VDRESTRICT uvdst2 = uvdst8;
			uvdst8 += uvpitch;

			const uint8 *VDRESTRICT usrca2 = usrc8;
			usrc8 += upitch;
			const uint8 *VDRESTRICT usrcb2 = usrc8;
			usrc8 += upitch;
			const uint8 *VDRESTRICT vsrca2 = vsrc8;
			vsrc8 += vpitch;
			const uint8 *VDRESTRICT vsrcb2 = vsrc8;
			vsrc8 += vpitch;

			*uvdst2++ = (uint8)(((usrca2[0] + usrcb2[0])*3 + usrca2[1] + usrcb2[1] + 4) >> 3);
			usrca2 += 1;
			usrcb2 += 1;

			*uvdst2++ = (uint8)(((vsrca2[0] + vsrcb2[0])*3 + vsrca2[1] + vsrcb2[1] + 4) >> 3);
			vsrca2 += 1;
			vsrcb2 += 1;

			for(uint32 i=1; i<w; ++i) {
				*uvdst2++ = (uint8)(((usrca2[0] + usrcb2[0]) + (usrca2[1] + usrcb2[1])*2 + (usrca2[2] + usrcb2[2]) + 4) >> 3);
				usrca2 += 2;
				usrcb2 += 2;

				*uvdst2++ = (uint8)(((vsrca2[0] + vsrcb2[0]) + (vsrca2[1] + vsrcb2[1])*2 + (vsrca2[2] + vsrcb2[2]) + 4) >> 3);
				vsrca2 += 2;
				vsrcb2 += 2;
			}
		}
	}

#if VD_CPU_X86 || VD_CPU_X64
	void BlitChroma444ToNV12_SSE2(void *uvdst, ptrdiff_t uvpitch, const void *usrc, ptrdiff_t upitch, const void *vsrc, ptrdiff_t vpitch, uint32 w, uint32 h) {
		uint8 *uvdst8 = (uint8 *)uvdst;
		const uint8 *usrc8 = (uint8 *)usrc;
		const uint8 *vsrc8 = (uint8 *)vsrc;

		uint32 w8 = w >> 3;

		__m128i umask = _mm_set1_epi16(0x00FF);
		__m128i rightAndMask = ATIntrinGetEndMask_SSE2(2 * (w & 7));
		__m128i rightOrMask = _mm_andnot_si128(rightAndMask, _mm_set1_epi8(-0x80));

		while(h--) {
			uint8 *VDRESTRICT uvdst2 = uvdst8;
			uvdst8 += uvpitch;

			const uint8 *VDRESTRICT usrca2 = usrc8;
			usrc8 += upitch;
			const uint8 *VDRESTRICT usrcb2 = usrc8;
			usrc8 += upitch;
			const uint8 *VDRESTRICT vsrca2 = vsrc8;
			vsrc8 += vpitch;
			const uint8 *VDRESTRICT vsrcb2 = vsrc8;
			vsrc8 += vpitch;

			// We take advantage of our known layout here, which is 16-byte aligned chroma scanlines with 16 bytes before
			// and after. This means that the first and last segments might just need some extra masking.
			__m128i prevU = _mm_avg_epu8(_mm_loadu_si128((const __m128i *)(usrca2 - 1)), _mm_loadu_si128((const __m128i *)(usrcb2 - 1)));
			__m128i prevV = _mm_avg_epu8(_mm_loadu_si128((const __m128i *)(vsrca2 - 2)), _mm_loadu_si128((const __m128i *)(vsrcb2 - 2)));
			prevU = _mm_insert_epi16(prevU, 0x8080, 0);
			prevV = _mm_insert_epi16(prevV, 0x8080, 0);

			__m128i curU  = _mm_avg_epu8(_mm_load_si128 ((const __m128i *)(usrca2 + 0)), _mm_load_si128 ((const __m128i *)(usrcb2 + 0)));
			__m128i curV  = _mm_avg_epu8(_mm_loadu_si128((const __m128i *)(vsrca2 - 1)), _mm_loadu_si128((const __m128i *)(vsrcb2 - 1)));
			__m128i nextU = _mm_avg_epu8(_mm_loadu_si128((const __m128i *)(usrca2 + 1)), _mm_loadu_si128((const __m128i *)(usrcb2 + 1)));
			__m128i nextV = _mm_avg_epu8(_mm_load_si128 ((const __m128i *)(vsrca2 + 0)), _mm_load_si128 ((const __m128i *)(vsrcb2 + 0)));
			usrca2 += 16;
			usrcb2 += 16;
			vsrca2 += 16;
			vsrcb2 += 16;

			__m128i u = _mm_avg_epu8(_mm_avg_epu8(prevU, nextU), curU);
			__m128i v = _mm_avg_epu8(_mm_avg_epu8(prevV, nextV), curV);

			__m128i uv = _mm_or_si128(_mm_and_si128(umask, u), _mm_andnot_si128(umask, v));

			_mm_store_si128((__m128i *)uvdst2, uv);
			uvdst2 += 16;

			for(uint32 i = 1; i < w8; ++i) {
				__m128i prevU = _mm_avg_epu8(_mm_loadu_si128((const __m128i *)(usrca2 - 1)), _mm_loadu_si128((const __m128i *)(usrcb2 - 1)));
				__m128i prevV = _mm_avg_epu8(_mm_loadu_si128((const __m128i *)(vsrca2 - 2)), _mm_loadu_si128((const __m128i *)(vsrcb2 - 2)));
				__m128i curU  = _mm_avg_epu8(_mm_load_si128 ((const __m128i *)(usrca2 + 0)), _mm_load_si128 ((const __m128i *)(usrcb2 + 0)));
				__m128i curV  = _mm_avg_epu8(_mm_loadu_si128((const __m128i *)(vsrca2 - 1)), _mm_loadu_si128((const __m128i *)(vsrcb2 - 1)));
				__m128i nextU = _mm_avg_epu8(_mm_loadu_si128((const __m128i *)(usrca2 + 1)), _mm_loadu_si128((const __m128i *)(usrcb2 + 1)));
				__m128i nextV = _mm_avg_epu8(_mm_load_si128 ((const __m128i *)(vsrca2 + 0)), _mm_load_si128 ((const __m128i *)(vsrcb2 + 0)));
				usrca2 += 16;
				usrcb2 += 16;
				vsrca2 += 16;
				vsrcb2 += 16;

				__m128i u = _mm_avg_epu8(_mm_avg_epu8(prevU, nextU), curU);
				__m128i v = _mm_avg_epu8(_mm_avg_epu8(prevV, nextV), curV);

				__m128i uv = _mm_or_si128(_mm_and_si128(umask, u), _mm_andnot_si128(umask, v));

				_mm_store_si128((__m128i *)uvdst2, uv);
				uvdst2 += 16;
			}

			// do leftover bytes
			if (w & 7) {
				__m128i prevU = _mm_avg_epu8(_mm_loadu_si128((const __m128i *)(usrca2 - 1)), _mm_loadu_si128((const __m128i *)(usrcb2 - 1)));
				__m128i prevV = _mm_avg_epu8(_mm_loadu_si128((const __m128i *)(vsrca2 - 2)), _mm_loadu_si128((const __m128i *)(vsrcb2 - 2)));
				__m128i curU  = _mm_avg_epu8(_mm_load_si128 ((const __m128i *)(usrca2 + 0)), _mm_load_si128 ((const __m128i *)(usrcb2 + 0)));
				__m128i curV  = _mm_avg_epu8(_mm_loadu_si128((const __m128i *)(vsrca2 - 1)), _mm_loadu_si128((const __m128i *)(vsrcb2 - 1)));
				__m128i nextU = _mm_avg_epu8(_mm_loadu_si128((const __m128i *)(usrca2 + 1)), _mm_loadu_si128((const __m128i *)(usrcb2 + 1)));
				__m128i nextV = _mm_avg_epu8(_mm_load_si128 ((const __m128i *)(vsrca2 + 0)), _mm_load_si128 ((const __m128i *)(vsrcb2 + 0)));

				nextU = _mm_or_si128(_mm_and_si128(nextU, rightAndMask), rightOrMask);
				nextV = _mm_or_si128(_mm_and_si128(nextV, rightAndMask), rightOrMask);

				usrca2 += 16;
				usrcb2 += 16;
				vsrca2 += 16;
				vsrcb2 += 16;

				__m128i u = _mm_avg_epu8(_mm_avg_epu8(prevU, nextU), curU);
				__m128i v = _mm_avg_epu8(_mm_avg_epu8(prevV, nextV), curV);

				__m128i uv = _mm_or_si128(_mm_and_si128(umask, u), _mm_andnot_si128(umask, v));

				_mm_store_si128((__m128i *)uvdst2, uv);
				uvdst2 += 16;
			}
		}
	}
#endif

#if VD_CPU_ARM64
	void BlitChroma444ToNV12_NEON(void *uvdst, ptrdiff_t uvpitch, const void *usrc, ptrdiff_t upitch, const void *vsrc, ptrdiff_t vpitch, uint32 w, uint32 h) {
		uint8 *uvdst8 = (uint8 *)uvdst;
		const uint8 *usrc8 = (uint8 *)usrc;
		const uint8 *vsrc8 = (uint8 *)vsrc;

		uint32 w8 = w >> 3;

		uint8x16_t umask = vreinterpretq_u8_u16(vmovq_n_u16(0x00FF));
		uint8x16_t rightAndMask = ATIntrinGetEndMask_NEON(2 * (w & 7));
		uint8x16_t rightOrMask = vbicq_u8(vmovq_n_u8(0x80), rightAndMask);

		while(h--) {
			uint8 *VDRESTRICT uvdst2 = uvdst8;
			uvdst8 += uvpitch;

			const uint8 *VDRESTRICT usrca2 = usrc8;
			usrc8 += upitch;
			const uint8 *VDRESTRICT usrcb2 = usrc8;
			usrc8 += upitch;
			const uint8 *VDRESTRICT vsrca2 = vsrc8;
			vsrc8 += vpitch;
			const uint8 *VDRESTRICT vsrcb2 = vsrc8;
			vsrc8 += vpitch;

			// We take advantage of our known layout here, which is 16-byte aligned chroma scanlines with 16 bytes before
			// and after. This means that the first and last segments might just need some extra masking.
			uint8x16_t prevU = vrhaddq_u8(vld1q_u8(usrca2 - 1), vld1q_u8(usrcb2 - 1));
			uint8x16_t prevV = vrhaddq_u8(vld1q_u8(vsrca2 - 2), vld1q_u8(vsrcb2 - 2));
			prevU = vreinterpretq_u8_u16(vsetq_lane_u16(0x8080, vreinterpretq_u16_u8(prevU), 0));
			prevV = vreinterpretq_u8_u16(vsetq_lane_u16(0x8080, vreinterpretq_u16_u8(prevV), 0));

			uint8x16_t curU  = vrhaddq_u8(vld1q_u8(usrca2 + 0), vld1q_u8(usrcb2 + 0));
			uint8x16_t curV  = vrhaddq_u8(vld1q_u8(vsrca2 - 1), vld1q_u8(vsrcb2 - 1));
			uint8x16_t nextU = vrhaddq_u8(vld1q_u8(usrca2 + 1), vld1q_u8(usrcb2 + 1));
			uint8x16_t nextV = vrhaddq_u8(vld1q_u8(vsrca2 + 0), vld1q_u8(vsrcb2 + 0));
			usrca2 += 16;
			usrcb2 += 16;
			vsrca2 += 16;
			vsrcb2 += 16;

			uint8x16_t u = vrhaddq_u8(vrhaddq_u8(prevU, nextU), curU);
			uint8x16_t v = vrhaddq_u8(vrhaddq_u8(prevV, nextV), curV);

			uint8x16_t uv = vbslq_u8(umask, u, v);

			vst1q_u8(uvdst2, uv);
			uvdst2 += 16;

			for(uint32 i = 1; i < w8; ++i) {
				uint8x16_t prevU = vrhaddq_u8(vld1q_u8(usrca2 - 1), vld1q_u8(usrcb2 - 1));
				uint8x16_t prevV = vrhaddq_u8(vld1q_u8(vsrca2 - 2), vld1q_u8(vsrcb2 - 2));
				uint8x16_t curU  = vrhaddq_u8(vld1q_u8(usrca2 + 0), vld1q_u8(usrcb2 + 0));
				uint8x16_t curV  = vrhaddq_u8(vld1q_u8(vsrca2 - 1), vld1q_u8(vsrcb2 - 1));
				uint8x16_t nextU = vrhaddq_u8(vld1q_u8(usrca2 + 1), vld1q_u8(usrcb2 + 1));
				uint8x16_t nextV = vrhaddq_u8(vld1q_u8(vsrca2 + 0), vld1q_u8(vsrcb2 + 0));
				usrca2 += 16;
				usrcb2 += 16;
				vsrca2 += 16;
				vsrcb2 += 16;

				uint8x16_t u = vrhaddq_u8(vrhaddq_u8(prevU, nextU), curU);
				uint8x16_t v = vrhaddq_u8(vrhaddq_u8(prevV, nextV), curV);

				uint8x16_t uv = vbslq_u8(umask, u, v);

				vst1q_u8(uvdst2, uv);
				uvdst2 += 16;
			}

			// do leftover bytes
			if (w & 7) {
				uint8x16_t prevU = vrhaddq_u8(vld1q_u8(usrca2 - 1), vld1q_u8(usrcb2 - 1));
				uint8x16_t prevV = vrhaddq_u8(vld1q_u8(vsrca2 - 2), vld1q_u8(vsrcb2 - 2));
				uint8x16_t curU  = vrhaddq_u8(vld1q_u8(usrca2 + 0), vld1q_u8(usrcb2 + 0));
				uint8x16_t curV  = vrhaddq_u8(vld1q_u8(vsrca2 - 1), vld1q_u8(vsrcb2 - 1));
				uint8x16_t nextU = vrhaddq_u8(vld1q_u8(usrca2 + 1), vld1q_u8(usrcb2 + 1));
				uint8x16_t nextV = vrhaddq_u8(vld1q_u8(vsrca2 + 0), vld1q_u8(vsrcb2 + 0));

				nextU = vorrq_u8(vandq_u8(nextU, rightAndMask), rightOrMask);
				nextV = vorrq_u8(vandq_u8(nextV, rightAndMask), rightOrMask);

				usrca2 += 16;
				usrcb2 += 16;
				vsrca2 += 16;
				vsrcb2 += 16;

				uint8x16_t u = vrhaddq_u8(vrhaddq_u8(prevU, nextU), curU);
				uint8x16_t v = vrhaddq_u8(vrhaddq_u8(prevV, nextV), curV);

				uint8x16_t uv = vbslq_u8(umask, u, v);

				vst1q_u8(uvdst2, uv);
				uvdst2 += 16;
			}
		}
	}
#endif

	void BlitChroma420ToNV12_Reference(void *uvdst, ptrdiff_t uvpitch, const void *usrc, ptrdiff_t upitch, const void *vsrc, ptrdiff_t vpitch, uint32 w, uint32 h) {
		uint8 *uvdst8 = (uint8 *)uvdst;
		const uint8 *usrc8 = (uint8 *)usrc;
		const uint8 *vsrc8 = (uint8 *)vsrc;

		while(h--) {
			uint8 *VDRESTRICT uvdst2 = (uint8 *)uvdst8;
			uvdst8 += uvpitch;

			const uint8 *VDRESTRICT usrc2 = (const uint8 *)usrc8;
			usrc8 += upitch;
			const uint8 *VDRESTRICT vsrc2 = (const uint8 *)vsrc8;
			vsrc8 += vpitch;

			for(uint32 i=w; i; --i) {
				*uvdst2++ = *usrc2++;
				*uvdst2++ = *vsrc2++;
			}
		}
	}

#if VD_CPU_X86 || VD_CPU_X64
	void BlitChroma420ToNV12_SSE2(void *uvdst, ptrdiff_t uvpitch, const void *usrc, ptrdiff_t upitch, const void *vsrc, ptrdiff_t vpitch, uint32 w, uint32 h) {
		uint8 *uvdst8 = (uint8 *)uvdst;
		const uint8 *usrc8 = (uint8 *)usrc;
		const uint8 *vsrc8 = (uint8 *)vsrc;

		uint32 w16 = (w + 15) >> 4;

		while(h--) {
			__m128i *VDRESTRICT uvdst2 = (__m128i *)uvdst8;
			uvdst8 += uvpitch;

			const __m128i *VDRESTRICT usrc2 = (const __m128i *)usrc8;
			usrc8 += upitch;
			const __m128i *VDRESTRICT vsrc2 = (const __m128i *)vsrc8;
			vsrc8 += vpitch;

			for(uint32 i=w16; i; --i) {
				__m128i u = *usrc2++;
				__m128i v = *vsrc2++;

				// This may overwrite up to 30 bytes, but we've guaranteed that we have this
				// padding at the end of the buffer.
				_mm_storeu_si128(uvdst2++, _mm_unpacklo_epi8(u, v));
				_mm_storeu_si128(uvdst2++, _mm_unpackhi_epi8(u, v));
			}
		}
	}
#endif

#if VD_CPU_ARM64
	void BlitChroma420ToNV12_NEON(void *uvdst, ptrdiff_t uvpitch, const void *usrc, ptrdiff_t upitch, const void *vsrc, ptrdiff_t vpitch, uint32 w, uint32 h) {
		uint8 *uvdst8 = (uint8 *)uvdst;
		const uint8 *usrc8 = (uint8 *)usrc;
		const uint8 *vsrc8 = (uint8 *)vsrc;

		uint32 w16 = (w + 15) >> 4;

		while(h--) {
			uint8 *VDRESTRICT uvdst2 = uvdst8;
			uvdst8 += uvpitch;

			const uint8 *VDRESTRICT usrc2 = usrc8;
			usrc8 += upitch;
			const uint8 *VDRESTRICT vsrc2 = vsrc8;
			vsrc8 += vpitch;

			for(uint32 i=w16; i; --i) {
				uint8x16_t u = vld1q_u8(usrc2);
				uint8x16_t v = vld1q_u8(vsrc2);

				usrc2 += 16;
				vsrc2 += 16;

				// This may overwrite up to 30 bytes, but we've guaranteed that we have this
				// padding at the end of the buffer.
				vst1q_u8(uvdst2, vzip1q_u8(u, v));
				uvdst2 += 16;
				vst1q_u8(uvdst2, vzip2q_u8(u, v));
				uvdst2 += 16;
			}
		}
	}
#endif

	void BlitChroma444ToNV12(void *uvdst, ptrdiff_t uvpitch, const void *usrc, ptrdiff_t upitch, const void *vsrc, ptrdiff_t vpitch, uint32 w, uint32 h) {
#if VD_CPU_X86 || VD_CPU_X64
		if (w >= 32 && SSE2_enabled) {
			BlitChroma444ToNV12_SSE2(uvdst, uvpitch, usrc, upitch, vsrc, vpitch, w, h);
			return;
		}
#endif

#if VD_CPU_ARM64
		if (w >= 32) {
			BlitChroma444ToNV12_NEON(uvdst, uvpitch, usrc, upitch, vsrc, vpitch, w, h);
			return;
		}
#endif

		BlitChroma444ToNV12_Reference(uvdst, uvpitch, usrc, upitch, vsrc, vpitch, w, h);
	}

	void BlitChroma420ToNV12(void *uvdst, ptrdiff_t uvpitch, const void *usrc, ptrdiff_t upitch, const void *vsrc, ptrdiff_t vpitch, uint32 w, uint32 h) {
#if VD_CPU_X86 || VD_CPU_X64
		if (SSE2_enabled) {
			BlitChroma420ToNV12_SSE2(uvdst, uvpitch, usrc, upitch, vsrc, vpitch, w, h);
			return;
		}
#endif

#if VD_CPU_ARM64
		if (w >= 32) {
			BlitChroma420ToNV12_NEON(uvdst, uvpitch, usrc, upitch, vsrc, vpitch, w, h);
			return;
		}
#endif

		BlitChroma420ToNV12_Reference(uvdst, uvpitch, usrc, upitch, vsrc, vpitch, w, h);
	}
}

void ATMediaFoundationEncoderW32::WriteVideo(const VDPixmap& px) {
	// sanity check the input buffer
	if (mVideoFrameLayout.format == nsVDPixmap::kPixFormat_Y8) {
		if (px.format != nsVDPixmap::kPixFormat_YUV444_Planar_709 && px.format != nsVDPixmap::kPixFormat_YUV420_Planar_709)
			return;

		if (px.w != mVideoFrameLayout.w || px.h + (px.h >> 1) != mVideoFrameLayout.h)
			return;
	}

	HRVerify verify;

	vdrefptr<IMFSample> sample;
	vdrefptr<IMFMediaBuffer> buf;

	// try to reclaim a sample from the allocator, to reduce memory allocation overhead -- this
	// is significant due to VM remapping
	if (mpVideoSampleAllocator->AllocateCachedSample(~sample)) {
		// we got one -- sanitize it
		DWORD bufferCount = 0;
		verify += sample->GetBufferCount(&bufferCount);

		if (bufferCount > 0) {
			verify += sample->GetBufferByIndex(0, ~buf);

			if (buf) {
				DWORD existingMaxLen = 0;
				verify += buf->GetMaxLength(&existingMaxLen);

				if (existingMaxLen < mVideoFrameSize)
					buf.clear();
			}
		}

		// wipe the sample of buffers and attributes so we start clean -- note that we are
		// holding the buffer so we can reuse it
		sample->RemoveAllBuffers();
		sample->DeleteAllItems();
	}

	if (!buf)
		verify += mpfnMFCreateAlignedMemoryBuffer(mVideoFrameSize, MF_64_BYTE_ALIGNMENT, ~buf);

	BYTE *data = nullptr;
	verify += buf->Lock(&data, nullptr, nullptr);

	if (mVideoFrameLayout.format == nsVDPixmap::kPixFormat_Y8) {
		// Special case for YUV444/YUV420 -> NV12 -- blit the luma plane, then use a custom blitter for the chroma plane.
		VDMemcpyRect(data + mVideoFrameLayout.data, mVideoFrameLayout.pitch, px.data, px.pitch, mVideoFrameLayout.w, mVideoFrameLayout.h);

		if (px.format == nsVDPixmap::kPixFormat_YUV444_Planar_709) {
			BlitChroma444ToNV12(
				data + mVideoFrameLayout.data + mVideoFrameLayout.pitch * ((mVideoFrameLayout.h * 2) / 3),
				mVideoFrameLayout.pitch,
				px.data2,
				px.pitch2,
				px.data3,
				px.pitch3,
				mVideoFrameLayout.w >> 1,
				mVideoFrameLayout.h / 3
			);
		} else {
			BlitChroma420ToNV12(
				data + mVideoFrameLayout.data + mVideoFrameLayout.pitch * ((mVideoFrameLayout.h * 2) / 3),
				mVideoFrameLayout.pitch,
				px.data2,
				px.pitch2,
				px.data3,
				px.pitch3,
				mVideoFrameLayout.w >> 1,
				mVideoFrameLayout.h / 3
			);
		}
	} else {
		mVideoBlitter.Blit(VDPixmapFromLayout(mVideoFrameLayout, data), px);
	}

	verify += buf->Unlock();

	verify += buf->SetCurrentLength(mVideoFrameSize);

	if (!sample)
		verify += mpfnMFCreateSample(~sample);

	verify += sample->AddBuffer(buf);
	buf.clear();

	++mVideoFrameCount;
	LONGLONG endTime = (LONGLONG)(sint64)((vduint128((uint64)mVideoFrameCount * 10000000U) * vduint128(mVideoFrameRate.getLo()) + vduint128(mVideoFrameRate.getHi() >> 1)) / mVideoFrameRate.getHi());

	verify += sample->SetSampleTime(mVideoNextSampleTime);
	verify += sample->SetSampleDuration(endTime - mVideoNextSampleTime);

	mVideoNextSampleTime = endTime;

	verify += mpSinkWriter->WriteSample(mVideoStreamIndex, sample);

	LPVOID fenceId = mpVideoSampleAllocator->AddSample(sample);
	sample.release();

	mpSinkWriter->PlaceMarker(mVideoStreamIndex, fenceId);
}

void ATMediaFoundationEncoderW32::BeginAudioFrame(uint32 bytes, uint32 samples) {
	HRVerify verify;

	VDASSERT(!mpAudioDst);

	if (mbAudioConvertToStereo)
		bytes *= 2;

	if (bytes > 0) {
		verify += mpfnMFCreateMemoryBuffer(bytes, ~mpAudioBuffer);
		verify += mpAudioBuffer->SetCurrentLength(bytes);
		verify += mpAudioBuffer->Lock(&mpAudioDst, nullptr, nullptr);
		mpAudioDstEnd = mpAudioDst + bytes;
	}

	mAudioSamplesWritten += samples;
}

void ATMediaFoundationEncoderW32::WriteAudio(const sint16 *data, uint32 bytes) {
	if (mbAudioConvertToStereo) {
		VDASSERT((bytes & 1) == 0);

		const sint16 *VDRESTRICT src = data;
		sint16 *VDRESTRICT dst = (sint16 *)mpAudioDst;

		for(uint32 i = bytes >> 1; i; --i) {
			dst[0] = dst[1] = *src++;
			dst += 2;
		}

		mpAudioDst = (BYTE *)dst;
	} else {
		memcpy(mpAudioDst, data, bytes);
		mpAudioDst += bytes;
	}
}

void ATMediaFoundationEncoderW32::EndAudioFrame() {
	if (!mpAudioDst)
		return;

	HRVerify verify;

	VDASSERT(mpAudioDst == mpAudioDstEnd);

	verify += mpAudioBuffer->Unlock();

	vdrefptr<IMFSample> sample;

	verify += mpfnMFCreateSample(~sample);

	verify += sample->AddBuffer(mpAudioBuffer);
	mpAudioBuffer.clear();

	LONGLONG sampleEndTime = (sint64)(((vduint128(mAudioSamplesWritten) * vduint128(10000000)) + vduint128(24000)) / (uint32)48000);

	verify += sample->SetSampleTime(mAudioNextSampleTime);
	verify += sample->SetSampleDuration(sampleEndTime - mAudioNextSampleTime);

	mAudioNextSampleTime = sampleEndTime;

	verify += mpSinkWriter->WriteSample(mAudioStreamIndex, sample);

	mpAudioDst = nullptr;
	mpAudioDstEnd = nullptr;
}

///////////////////////////////////////////////////////////////////////////////

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

	// Timestamp of first video frame recorded. Used for recording length reporting purposes.
	uint64	mFirstVideoTimestamp = 0;

	// Timestamp of first video frame received when attempting to resync.
	uint64	mSyncVideoTimestamp = 0;

	// Number of audio samples and video frames remaining to skip to resync.
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
	
	bool useYUV = false;

	switch(venc) {
		case kATVideoEncoding_WMV7:
		case kATVideoEncoding_WMV9:
		case kATVideoEncoding_H264_AAC:
		case kATVideoEncoding_H264_MP3:
			useYUV = true;
			break;

		default:
			break;
	}

	if (useYUV) {
		// Ensure even/odd frame size for 4:2:0 since odd support is not guaranteed in MF (much less defined, really).
		framew = (framew + 1) & ~1;
		frameh = (frameh + 1) & ~1;
	}

	if (framew != w || frameh != h || (uint32)(0.5f + dstwf) != w || (uint32)(0.5f + dsthf) != h) {
		mpVideoResampler = VDCreatePixmapResampler();

		if (useYUV) {
			VDPixmapLayout layout;
			VDPixmapCreateLinearLayout(layout, nsVDPixmap::kPixFormat_YUV444_Planar_709, framew, frameh, 16);
			mVideoResampleBuffer.init(layout, 16);
		} else {
			mVideoResampleBuffer.init(framew, frameh, nsVDPixmap::kPixFormat_XRGB8888);
		}

		memset(mVideoResampleBuffer.base(), 0, mVideoResampleBuffer.size());

		if (useYUV) {
			VDMemset8Rect(mVideoResampleBuffer.data2, mVideoResampleBuffer.pitch2, 0x80, framew, frameh);
			VDMemset8Rect(mVideoResampleBuffer.data3, mVideoResampleBuffer.pitch3, 0x80, framew, frameh);
		}

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
				filterMode = IVDPixmapResampler::kFilterLinear;
				break;
		}

		mVideoDestRect = dstrect;

		mpVideoResampler->SetFilters(filterMode, filterMode, false);
		VDVERIFY(mpVideoResampler->Init(dstrect, framew, frameh, mVideoResampleBuffer.format, vdrect32f(0, 0, (float)w, (float)h), w, h, mVideoResampleBuffer.format));

		w = framew;
		h = frameh;

		palette = nullptr;
	} else if (useYUV) {
		mVideoPostResampleCcBuffer.init(w, h, nsVDPixmap::kPixFormat_YUV420_Planar_709);
	}

	if (!palette && venc == kATVideoEncoding_RLE)
		throw MyError("RLE encoding is not available as the current emulation video and recording settings require 24-bit video.");

	switch(venc) {
		case kATVideoEncoding_Raw:
		case kATVideoEncoding_RLE:
		case kATVideoEncoding_ZMBV:
			mpMediaEncoder = new ATAVIEncoder(filename, venc, w, h, encodingFrameRate, palette, samplingRate, stereo, encodeAllFrames);
			break;

		case kATVideoEncoding_WMV7:
		case kATVideoEncoding_WMV9:
		case kATVideoEncoding_H264_AAC:
		case kATVideoEncoding_H264_MP3:
			mpMediaEncoder = new ATMediaFoundationEncoderW32(filename, venc, videoBitRate, audioBitRate, w, h, encodingFrameRate, palette, samplingRate, stereo, useYUV);
			break;

		default:
			throw MyError("Unimplemented compression mode.");
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

	// Reset sync state so the recorder attempts to resync.
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

		// Compute how much audio we need to skip to get the streams in sync. We do this by computing the
		// error between the next video frame time and the current audio timestamp. If this is negative,
		// we increase the video frame skip and try again (to be safe wrt. roundoff). Note that we are
		// doing this at input sampling rate, not at output sampling rate.
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
