//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2022 Avery Lee
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

#include <stdafx.h>
#include <vd2/system/vdtypes.h>

template<int T_LogStep>
void ATFFT_DIT_Radix2_Scalar(float *y0, const float *w, int N) {
	const int step = 1 << T_LogStep;
	float *__restrict y = y0;
	const float *w1 = w + 2;

	for(int i=0; i<N; i += step*2) {
		const float *__restrict w2 = w1;

		{
			float r0 = y[i];
			float i0 = y[i+1];
			float r1 = y[i+step];
			float i1 = y[i+step+1];

			y[i] = r0 + r1;
			y[i+1] = i0 + i1;
			y[i+step] = r0 - r1;
			y[i+step+1] = i0 - i1;
		}

		for(int j=2; j<step; j += 2) {
			float r0 = y[i+j];
			float i0 = y[i+j+1];
			float r1 = y[i+j+step];
			float i1 = y[i+j+step+1];

			float rw = *w2++;
			float iw = *w2++;
			float r2 = r1*rw - i1*iw;
			float i2 = r1*iw + i1*rw;

			y[i+j] = r0 + r2;
			y[i+j+1] = i0 + i2;
			y[i+j+step] = r0 - r2;
			y[i+j+step+1] = i0 - i2;
		}
	}
}

void ATFFT_DIT_Radix8_Scalar(float *dst0, const float *y0, const uint32 *order0, int N) {
	constexpr float r2d2 = 0.70710678118654752440084436210485f;
	const uint32 *__restrict order = order0;
	const int step0 = N/8;
	const float *__restrict y = y0;
	float *__restrict dst = dst0;

	for(int i=0; i<N/16; ++i) {
		const float r0 = y[0 * step0];
		const float i0 = y[0 * step0 + 1];
		const float r1 = y[4 * step0];
		const float i1 = y[4 * step0 + 1];
		const float r2 = y[2 * step0];
		const float i2 = y[2 * step0 + 1];
		const float r3 = y[6 * step0];
		const float i3 = y[6 * step0 + 1];
		const float r4 = y[1 * step0];
		const float i4 = y[1 * step0 + 1];
		const float r5 = y[5 * step0];
		const float i5 = y[5 * step0 + 1];
		const float r6 = y[3 * step0];
		const float i6 = y[3 * step0 + 1];
		const float r7 = y[7 * step0];
		const float i7 = y[7 * step0 + 1];
		y += 2;

		const float a0 = r0 + r1;
		const float b0 = i0 + i1;
		const float a1 = r0 - r1;
		const float b1 = i0 - i1;
		const float a2 = r2 + r3;
		const float b2 = i2 + i3;
		const float a3 = r2 - r3;
		const float b3 = i2 - i3;
		const float a4 = r4 + r5;
		const float b4 = i4 + i5;
		const float a5 = r4 - r5;
		const float b5 = i4 - i5;
		const float a6 = r6 + r7;
		const float b6 = i6 + i7;
		const float a7 = r6 - r7;
		const float b7 = i6 - i7;

		const float c0 = a0 + a2;
		const float d0 = b0 + b2;
		const float c2 = a0 - a2;
		const float d2 = b0 - b2;
		const float c1 = a1 + b3;
		const float d1 = b1 - a3;
		const float c3 = a1 - b3;
		const float d3 = b1 + a3;

		const float c4 = a4 + a6;
		const float d4 = b4 + b6;
		const float c6 = a4 - a6;
		const float d6 = b4 - b6;
		const float c5 = a5 + b7;
		const float d5 = b5 - a7;
		const float c7 = a5 - b7;
		const float d7 = b5 + a7;

		const float e0 = c0 + c4;
		const float f0 = d0 + d4;
		const float e4 = c0 - c4;
		const float f4 = d0 - d4;

		const float e2 = c2 + d6;
		const float f2 = d2 - c6;
		const float e6 = c2 - d6;
		const float f6 = d2 + c6;

		const float u5 = (d5 + c5)*r2d2;
		const float v5 = (d5 - c5)*r2d2;
		const float e1 = c1 + u5;
		const float f1 = d1 + v5;
		const float e5 = c1 - u5;
		const float f5 = d1 - v5;

		const float u7 = (c7 - d7)*r2d2;
		const float v7 = (c7 + d7)*r2d2;
		const float e3 = c3 - u7;
		const float f3 = d3 - v7;
		const float e7 = c3 + u7;
		const float f7 = d3 + v7;

		float *__restrict dst2 = &dst[*order++];
		dst2[ 0] = e0;
		dst2[ 1] = f0;
		dst2[ 2] = e1;
		dst2[ 3] = f1;
		dst2[ 4] = e2;
		dst2[ 5] = f2;
		dst2[ 6] = e3;
		dst2[ 7] = f3;
		dst2[ 8] = e4;
		dst2[ 9] = f4;
		dst2[10] = e5;
		dst2[11] = f5;
		dst2[12] = e6;
		dst2[13] = f6;
		dst2[14] = e7;
		dst2[15] = f7;
	}
}

void ATFFT_DIT_R2C_Scalar(float *dst0, const float *src0, const float *w, size_t N) {
	float *__restrict dst = dst0;
	const float *__restrict src = src0;
	const float *__restrict w2 = w + 2;

	float rz = src[0];
	float iz = src[1];
	dst[0] = rz + iz;
	dst[1] = rz - iz;

	for(int i=2; i<(int)(N/2); i += 2) {
		float r0 = src[i];
		float i0 = src[i+1];
		float r1 = src[N-i];
		float i1 = src[N-i+1];

		// Xe[n] = (Z[k] + Z*[N/2-k])/2
		float r2 = (r1 + r0)*0.5f;
		float i2 = (i0 - i1)*0.5f;

		// Xo[n] = -j(Z[k] - Z*[N/2-k])/2
		float r3 = (i0 + i1)*0.5f;
		float i3 = (r1 - r0)*0.5f;

		float rw = *w2++;
		float iw = *w2++;
		float r4 = r3*rw - i3*iw;
		float i4 = r3*iw + i3*rw;

		dst[i] = r2 + r4;
		dst[i+1] = i2 + i4;
		dst[N-i] = r2 - r4;
		dst[N-i+1] = i4 - i2;
	}

	dst[N/2] = src[N/2];
	dst[N/2+1] = -src[N/2+1];
}

void ATFFT_DIF_C2R_Scalar(float *dst0, const float *x, const float *w, size_t N) {
	float *__restrict dst = dst0;

	float rz = x[0];
	float iz = x[1];
	dst[0] = (rz + iz) * 0.5f;
	dst[1] = (rz - iz) * 0.5f;

	dst[N/2] = x[N/2];
	dst[N/2+1] = -x[N/2+1];

	w += 2;

	for(int i=2; i<(int)(N/2); i += 2) {
		float r0 = x[i];
		float i0 = x[i+1];
		float r1 = x[N-i];
		float i1 = x[N-i+1];

		// Xe[n] = (X[k] + X*[N/2-k])/2
		float r2 = (r0 + r1)*0.5f;
		float i2 = (i0 - i1)*0.5f;

		// Xo[n] = j(X[k] - X*[N/2-k])/2 * w[k]
		float r3 = (i0 + i1)*-0.5f;
		float i3 = (r0 - r1)*0.5f;

		float rw = *w++;
		float iw = *w++;
		float r4 = r3*rw + i3*iw;
		float i4 = i3*rw - r3*iw;

		dst[i] = r2 + r4;
		dst[i+1] = i2 + i4;
		dst[N-i] = r2 - r4;
		dst[N-i+1] = i4 - i2;
	}
}


template<int T_LogStep>
void ATFFT_DIF_Radix2_Scalar(float *dst0, const float *w, int N) {
	constexpr int step = 1 << T_LogStep;

	float *__restrict dst = dst0;

	for(int i=0; i<N; i += step*2) {
		const float *__restrict w2 = w;
		for(int j=0; j<step; j += 2) {
			float r0 = dst[0];
			float i0 = dst[1];
			float r1 = dst[step];
			float i1 = dst[step+1];

			float rw = *w2++;
			float iw = *w2++;
			float r2 = r0 - r1;
			float i2 = i0 - i1;

			dst[0] = r0 + r1;
			dst[1] = i0 + i1;
			dst[step] = i2*iw + r2*rw;
			dst[step+1] = i2*rw - r2*iw;
			dst += 2;
		}

		dst += step;
	}
}

void ATFFT_DIF_Radix8_Scalar(float *dst0, const float *src0, const uint32 *order0, int N) {
	constexpr float r2d2 = 0.70710678118654752440084436210485f;

	const uint32 *__restrict order = order0;
	float *__restrict dst = dst0;
	const float *__restrict src = src0;

	for(int i=0; i<N/16; ++i) {
		const float *__restrict src2 = src + (*order++);
		float r0 = src2[ 0], i0 = src2[ 1];
		float r1 = src2[ 2], i1 = src2[ 3];
		float r2 = src2[ 4], i2 = src2[ 5];
		float r3 = src2[ 6], i3 = src2[ 7];
		float r4 = src2[ 8], i4 = src2[ 9];
		float r5 = src2[10], i5 = src2[11];
		float r6 = src2[12], i6 = src2[13];
		float r7 = src2[14], i7 = src2[15];

		// DFT-8 stage
		float ar0 = r0 + r4, ai0 = i0 + i4;
		float ar1 = r1 + r5, ai1 = i1 + i5;
		float ar2 = r2 + r6, ai2 = i2 + i6;
		float ar3 = r3 + r7, ai3 = i3 + i7;
		float ar4 = r0 - r4, ai4 = i0 - i4;
		float ar5 = r1 - r5, ai5 = i1 - i5;
		float ar6 = r2 - r6, ai6 = i2 - i6;
		float ar7 = r3 - r7, ai7 = i3 - i7;

		// *w(1,8) = (1+j)*sqrt(2)/2
		// *w(2,8) = j
		// *w(3,8) = (-1+j)*sqrt(2)/2
		float ar5t = (ar5 - ai5)*r2d2;
		float ai5t = (ar5 + ai5)*r2d2;
		float ar6t = -ai6;
		float ai6t = ar6;
		float ar7t = (-ar7 - ai7)*r2d2;
		float ai7t = (-ai7 + ar7)*r2d2;


		// DFT-4 stage
		float br0 = ar0 + ar2, bi0 = ai0 + ai2;
		float br1 = ar1 + ar3, bi1 = ai1 + ai3;
		float br2 = ar0 - ar2, bi2 = ai0 - ai2;
		float br3 = ar1 - ar3, bi3 = ai1 - ai3;
		float br4 = ar4 + ar6t,bi4 = ai4 + ai6t;
		float br5 = ar5t+ ar7t,bi5 = ai5t+ ai7t;
		float br6 = ar4 - ar6t,bi6 = ai4 - ai6t;
		float br7 = ar5t- ar7t,bi7 = ai5t- ai7t;

		// DFT-2 stage
		float cr0 = br0 + br1, ci0 = bi0 + bi1;
		float cr1 = br0 - br1, ci1 = bi0 - bi1;
		float cr2 = br2 - bi3, ci2 = bi2 + br3;		// +j twiddle from prev stage
		float cr3 = br2 + bi3, ci3 = bi2 - br3;		// +j twiddle from prev stage
		float cr4 = br4 + br5, ci4 = bi4 + bi5;
		float cr5 = br4 - br5, ci5 = bi4 - bi5;
		float cr6 = br6 - bi7, ci6 = bi6 + br7;		// +j twiddle from prev stage
		float cr7 = br6 + bi7, ci7 = bi6 - br7;		// +j twiddle from prev stage

		float *__restrict dstb0 = dst; dst += 2;
		float *__restrict dstb1 = dstb0 + (N/8)*4;
		float *__restrict dstb2 = dstb0 + (N/8)*2;
		float *__restrict dstb3 = dstb0 + (N/8)*6;
		float *__restrict dstb4 = dstb0 + (N/8)*1;
		float *__restrict dstb5 = dstb0 + (N/8)*5;
		float *__restrict dstb6 = dstb0 + (N/8)*3;
		float *__restrict dstb7 = dstb0 + (N/8)*7;

		dstb0[0] = cr0;	dstb0[1] = ci0;
		dstb1[0] = cr1;	dstb1[1] = ci1;
		dstb2[0] = cr2;	dstb2[1] = ci2;
		dstb3[0] = cr3;	dstb3[1] = ci3;
		dstb4[0] = cr4;	dstb4[1] = ci4;
		dstb5[0] = cr5;	dstb5[1] = ci5;
		dstb6[0] = cr6;	dstb6[1] = ci6;
		dstb7[0] = cr7;	dstb7[1] = ci7;
	}
}

/////////////////////////////////////////////////////////////////////////////

void ATFFT_DIT_Radix2_Scalar(float *y0, const float *w4, int N, int logStep) {
	switch(logStep) {
		case  4: ATFFT_DIT_Radix2_Scalar< 4>(y0, w4, N); break;
		case  5: ATFFT_DIT_Radix2_Scalar< 5>(y0, w4, N); break;
		case  6: ATFFT_DIT_Radix2_Scalar< 6>(y0, w4, N); break;
		case  7: ATFFT_DIT_Radix2_Scalar< 7>(y0, w4, N); break;
		case  8: ATFFT_DIT_Radix2_Scalar< 8>(y0, w4, N); break;
		case  9: ATFFT_DIT_Radix2_Scalar< 9>(y0, w4, N); break;
		case 10: ATFFT_DIT_Radix2_Scalar<10>(y0, w4, N); break;
		case 11: ATFFT_DIT_Radix2_Scalar<11>(y0, w4, N); break;
		case 12: ATFFT_DIT_Radix2_Scalar<12>(y0, w4, N); break;
	}
}

void ATFFT_DIF_Radix2_Scalar(float *y0, const float *w4, int N, int logStep) {
	switch(logStep) {
		case  4: ATFFT_DIF_Radix2_Scalar< 4>(y0, w4, N); break;
		case  5: ATFFT_DIF_Radix2_Scalar< 5>(y0, w4, N); break;
		case  6: ATFFT_DIF_Radix2_Scalar< 6>(y0, w4, N); break;
		case  7: ATFFT_DIF_Radix2_Scalar< 7>(y0, w4, N); break;
		case  8: ATFFT_DIF_Radix2_Scalar< 8>(y0, w4, N); break;
		case  9: ATFFT_DIF_Radix2_Scalar< 9>(y0, w4, N); break;
		case 10: ATFFT_DIF_Radix2_Scalar<10>(y0, w4, N); break;
		case 11: ATFFT_DIF_Radix2_Scalar<11>(y0, w4, N); break;
		case 12: ATFFT_DIF_Radix2_Scalar<12>(y0, w4, N); break;
	}
}

/////////////////////////////////////////////////////////////////////////////

void ATFFT_MultiplyAdd_Scalar(float *VDRESTRICT dst, const float *VDRESTRICT src1, const float *VDRESTRICT src2, int N) {
	// first two values are real DC and fsc*0.5, rest are complex
	dst[0] += src1[0] * src2[0];
	dst[1] += src1[1] * src2[1];
	dst += 2;
	src1 += 2;
	src2 += 2;

	const int N2m1 = (N >> 1)-1;
	for(int i=0; i<N2m1; ++i) {
		const float r1 = *src1++;
		const float i1 = *src1++;
		const float r2 = *src2++;
		const float i2 = *src2++;

		*dst++ += r1*r2 - i1*i2;
		*dst++ += r1*i2 + r2*i1;
	}
}

/////////////////////////////////////////////////////////////////////////////

void ATFFT_IMDCT_PreTransform_Scalar(float *dst, const float *src, const float *w, size_t N) {
	const size_t N2 = N / 2;

	const float *VDRESTRICT src1 = src;
	const float *VDRESTRICT src2 = src + N - 1;
	for(size_t i = 0; i < N2; ++i) {
		const float xr = *src1;
		const float xi = *src2;
		src1 += 2;
		src2 -= 2;

		const float wr = *w++;
		const float wi = *w++;

		*dst++ = xr*wr - xi*wi;
		*dst++ = xr*wi + xi*wr;
	}
}

void ATFFT_IMDCT_PostTransform_Scalar(float *dst, const float *src, const float *w, size_t N) {
	// generate 3rd and 2nd quarters of full IMDCT output, in that order
	const size_t N2 = N/2;
	const size_t N4 = N/4;

	const float *VDRESTRICT src1 = src;
	const float *VDRESTRICT src2 = src + N - 2;
	const float *VDRESTRICT w1 = w;
	const float *VDRESTRICT w2 = w1 + N2 - 2;
	float *VDRESTRICT dst1 = dst + N2 - 2;
	float *VDRESTRICT dst2 = dst + N2;
	for(size_t i = 0; i < N4; ++i) {
		const float x1r = src1[0];
		const float x1i = src1[1];
		const float x2r = src2[0];
		const float x2i = src2[1];
		src1 += 2;
		src2 -= 2;

		const float w1r = w1[0];
		const float w1i = w1[1];
		const float w2r = w2[0];
		const float w2i = w2[1];
		w1 += 2;
		w2 -= 2;

		const float p1r = x1r*w1r - x1i*w1i;
		const float p1i = x1r*w1i + x1i*w1r;
		const float p2r = x2r*w2r - x2i*w2i;
		const float p2i = x2r*w2i + x2i*w2r;

		// third quarter
		dst1[0] =  p2i;
		dst1[1] = -p1r;
		dst1 -= 2;

		// second quarter
		dst2[0] =  p1i;
		dst2[1] = -p2r;
		dst2 += 2;
	}
}
