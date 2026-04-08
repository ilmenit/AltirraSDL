//	Altirra - Atari 800/800XL/5200 emulator
//	PAL artifacting acceleration - SSE2 intrinsics
//	Copyright (C) 2009-2011 Avery Lee
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

#if defined(VD_CPU_X86) || defined(VD_CPU_AMD64)
#include <intrin.h>
#include <emmintrin.h>
#endif

#ifdef VD_CPU_AMD64
void ATArtifactPALLuma_SSE2(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels) {
	n >>= 3;

	__m128i x0 = _mm_setzero_si128();
	__m128i x1;

	__m128i *dst128 = (__m128i *)dst;
	const __m128i *kernels128 = (const __m128i *)kernels;
	do {
		const uint8 p0 = *src++;
		const uint8 p1 = *src++;
		const uint8 p2 = *src++;
		const uint8 p3 = *src++;

		const __m128i *f0 = kernels128 + 16U*p0;
		const __m128i *f1 = kernels128 + 16U*p1;
		const __m128i *f2 = kernels128 + 16U*p2;
		const __m128i *f3 = kernels128 + 16U*p3;

		x0 = _mm_add_epi16(x0, f0[0]);
		x1 = f0[1];

		x0 = _mm_add_epi16(x0, f1[2]);
		x1 = _mm_add_epi16(x1, f1[3]);

		x0 = _mm_add_epi16(x0, f2[4]);
		x1 = _mm_add_epi16(x1, f2[5]);

		x0 = _mm_add_epi16(x0, f3[6]);
		x1 = _mm_add_epi16(x1, f3[7]);

		*dst128++ = x0;

		const uint8 p4 = *src++;
		const uint8 p5 = *src++;
		const uint8 p6 = *src++;
		const uint8 p7 = *src++;

		const __m128i *f4 = kernels128 + 16U*p4;
		const __m128i *f5 = kernels128 + 16U*p5;
		const __m128i *f6 = kernels128 + 16U*p6;
		const __m128i *f7 = kernels128 + 16U*p7;

		x1 = _mm_add_epi16(x1, f4[8]);
		x0 = f4[9];

		x1 = _mm_add_epi16(x1, f5[10]);
		x0 = _mm_add_epi16(x0, f5[11]);

		x1 = _mm_add_epi16(x1, f6[12]);
		x0 = _mm_add_epi16(x0, f6[13]);

		x1 = _mm_add_epi16(x1, f7[14]);
		x0 = _mm_add_epi16(x0, f7[15]);

		*dst128++ = x1;
	} while(--n);

	*dst128++ = x0;
}

void ATArtifactPALLumaTwin_SSE2(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels) {
	n >>= 3;

	__m128i x0 = _mm_setzero_si128();
	__m128i x1;

	__m128i *dst128 = (__m128i *)dst;
	const __m128i *kernels128 = (const __m128i *)kernels;
	do {
		const uint8 p0 = src[0];
		const uint8 p2 = src[2];

		const __m128i *f0 = kernels128 + 8U*p0;
		const __m128i *f2 = kernels128 + 8U*p2;

		x0 = _mm_add_epi16(x0, f0[0]);
		x1 = f0[1];

		x0 = _mm_add_epi16(x0, f2[2]);
		x1 = _mm_add_epi16(x1, f2[3]);

		*dst128++ = x0;

		const uint8 p4 = src[4];
		const uint8 p6 = src[6];

		const __m128i *f4 = kernels128 + 8U*p4;
		const __m128i *f6 = kernels128 + 8U*p6;

		x1 = _mm_add_epi16(x1, f4[4]);
		x0 = f4[5];

		x1 = _mm_add_epi16(x1, f6[6]);
		x0 = _mm_add_epi16(x0, f6[7]);

		*dst128++ = x1;
		src += 8;
	} while(--n);

	*dst128++ = x0;
}

void ATArtifactPALChroma_SSE2(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels) {
	__m128i x0 = _mm_setzero_si128();
	__m128i x1 = _mm_setzero_si128();
	__m128i x2 = _mm_setzero_si128();
	__m128i x3;

	__m128i *dst128 = (__m128i *)dst;
	const __m128i *kernels128 = (const __m128i *)kernels;

	uint32 n2 = n >> 4;
	do {
		const uint8 p0 = *src++;
		const uint8 p1 = *src++;
		const uint8 p2 = *src++;
		const uint8 p3 = *src++;

		const __m128i *f0 = kernels128 + 32U*p0;
		const __m128i *f1 = kernels128 + 32U*p1;
		const __m128i *f2 = kernels128 + 32U*p2;
		const __m128i *f3 = kernels128 + 32U*p3;

		x0 = _mm_add_epi16(x0, f0[0]);
		x1 = _mm_add_epi16(x1, f0[1]);
		x2 = _mm_add_epi16(x2, f0[2]);
		x3 = f0[3];

		x0 = _mm_add_epi16(x0, f1[4]);
		x1 = _mm_add_epi16(x1, f1[5]);
		x2 = _mm_add_epi16(x2, f1[6]);
		x3 = _mm_add_epi16(x3, f1[7]);

		x0 = _mm_add_epi16(x0, f2[8]);
		x1 = _mm_add_epi16(x1, f2[9]);
		x2 = _mm_add_epi16(x2, f2[10]);
		x3 = _mm_add_epi16(x3, f2[11]);

		x0 = _mm_add_epi16(x0, f3[12]);
		x1 = _mm_add_epi16(x1, f3[13]);
		x2 = _mm_add_epi16(x2, f3[14]);
		x3 = _mm_add_epi16(x3, f3[15]);

		*dst128++ = x0;

		const uint8 p4 = *src++;
		const uint8 p5 = *src++;
		const uint8 p6 = *src++;
		const uint8 p7 = *src++;

		const __m128i *f4 = kernels128 + 32U*p4;
		const __m128i *f5 = kernels128 + 32U*p5;
		const __m128i *f6 = kernels128 + 32U*p6;
		const __m128i *f7 = kernels128 + 32U*p7;

		x1 = _mm_add_epi16(x1, f4[16]);
		x2 = _mm_add_epi16(x2, f4[17]);
		x3 = _mm_add_epi16(x3, f4[18]);
		x0 = f4[19];

		x1 = _mm_add_epi16(x1, f5[20]);
		x2 = _mm_add_epi16(x2, f5[21]);
		x3 = _mm_add_epi16(x3, f5[22]);
		x0 = _mm_add_epi16(x0, f5[23]);

		x1 = _mm_add_epi16(x1, f6[24]);
		x2 = _mm_add_epi16(x2, f6[25]);
		x3 = _mm_add_epi16(x3, f6[26]);
		x0 = _mm_add_epi16(x0, f6[27]);

		x1 = _mm_add_epi16(x1, f7[28]);
		x2 = _mm_add_epi16(x2, f7[29]);
		x3 = _mm_add_epi16(x3, f7[30]);
		x0 = _mm_add_epi16(x0, f7[31]);

		*dst128++ = x1;

		const uint8 p8 = *src++;
		const uint8 p9 = *src++;
		const uint8 p10 = *src++;
		const uint8 p11 = *src++;

		const __m128i *f8 = kernels128 + 32U*p8;
		const __m128i *f9 = kernels128 + 32U*p9;
		const __m128i *f10 = kernels128 + 32U*p10;
		const __m128i *f11 = kernels128 + 32U*p11;

		x2 = _mm_add_epi16(x2, f8[0]);
		x3 = _mm_add_epi16(x3, f8[1]);
		x0 = _mm_add_epi16(x0, f8[2]);
		x1 = f8[3];

		x2 = _mm_add_epi16(x2, f9[4]);
		x3 = _mm_add_epi16(x3, f9[5]);
		x0 = _mm_add_epi16(x0, f9[6]);
		x1 = _mm_add_epi16(x1, f9[7]);

		x2 = _mm_add_epi16(x2, f10[8]);
		x3 = _mm_add_epi16(x3, f10[9]);
		x0 = _mm_add_epi16(x0, f10[10]);
		x1 = _mm_add_epi16(x1, f10[11]);

		x2 = _mm_add_epi16(x2, f11[12]);
		x3 = _mm_add_epi16(x3, f11[13]);
		x0 = _mm_add_epi16(x0, f11[14]);
		x1 = _mm_add_epi16(x1, f11[15]);

		*dst128++ = x2;

		const uint8 p12 = *src++;
		const uint8 p13 = *src++;
		const uint8 p14 = *src++;
		const uint8 p15 = *src++;

		const __m128i *f12 = kernels128 + 32U*p12;
		const __m128i *f13 = kernels128 + 32U*p13;
		const __m128i *f14 = kernels128 + 32U*p14;
		const __m128i *f15 = kernels128 + 32U*p15;

		x3 = _mm_add_epi16(x3, f12[16]);
		x0 = _mm_add_epi16(x0, f12[17]);
		x1 = _mm_add_epi16(x1, f12[18]);
		x2 = f12[19];

		x3 = _mm_add_epi16(x3, f13[20]);
		x0 = _mm_add_epi16(x0, f13[21]);
		x1 = _mm_add_epi16(x1, f13[22]);
		x2 = _mm_add_epi16(x2, f13[23]);

		x3 = _mm_add_epi16(x3, f14[24]);
		x0 = _mm_add_epi16(x0, f14[25]);
		x1 = _mm_add_epi16(x1, f14[26]);
		x2 = _mm_add_epi16(x2, f14[27]);

		x3 = _mm_add_epi16(x3, f15[28]);
		x0 = _mm_add_epi16(x0, f15[29]);
		x1 = _mm_add_epi16(x1, f15[30]);
		x2 = _mm_add_epi16(x2, f15[31]);

		*dst128++ = x3;
	} while(--n2);

	if (n & 8) {
		const uint8 p0 = *src++;
		const uint8 p1 = *src++;
		const uint8 p2 = *src++;
		const uint8 p3 = *src++;

		const __m128i *f0 = kernels128 + 32U*p0;
		const __m128i *f1 = kernels128 + 32U*p1;
		const __m128i *f2 = kernels128 + 32U*p2;
		const __m128i *f3 = kernels128 + 32U*p3;

		x0 = _mm_add_epi16(x0, f0[0]);
		x1 = _mm_add_epi16(x1, f0[1]);
		x2 = _mm_add_epi16(x2, f0[2]);
		x3 = f0[3];

		x0 = _mm_add_epi16(x0, f1[4]);
		x1 = _mm_add_epi16(x1, f1[5]);
		x2 = _mm_add_epi16(x2, f1[6]);
		x3 = _mm_add_epi16(x3, f1[7]);

		x0 = _mm_add_epi16(x0, f2[8]);
		x1 = _mm_add_epi16(x1, f2[9]);
		x2 = _mm_add_epi16(x2, f2[10]);
		x3 = _mm_add_epi16(x3, f2[11]);

		x0 = _mm_add_epi16(x0, f3[12]);
		x1 = _mm_add_epi16(x1, f3[13]);
		x2 = _mm_add_epi16(x2, f3[14]);
		x3 = _mm_add_epi16(x3, f3[15]);

		*dst128++ = x0;

		const uint8 p4 = *src++;
		const uint8 p5 = *src++;
		const uint8 p6 = *src++;
		const uint8 p7 = *src++;

		const __m128i *f4 = kernels128 + 32U*p4;
		const __m128i *f5 = kernels128 + 32U*p5;
		const __m128i *f6 = kernels128 + 32U*p6;
		const __m128i *f7 = kernels128 + 32U*p7;

		x1 = _mm_add_epi16(x1, f4[16]);
		x2 = _mm_add_epi16(x2, f4[17]);
		x3 = _mm_add_epi16(x3, f4[18]);
		x0 = f4[19];

		x1 = _mm_add_epi16(x1, f5[20]);
		x2 = _mm_add_epi16(x2, f5[21]);
		x3 = _mm_add_epi16(x3, f5[22]);
		x0 = _mm_add_epi16(x0, f5[23]);

		x1 = _mm_add_epi16(x1, f6[24]);
		x2 = _mm_add_epi16(x2, f6[25]);
		x3 = _mm_add_epi16(x3, f6[26]);
		x0 = _mm_add_epi16(x0, f6[27]);

		x1 = _mm_add_epi16(x1, f7[28]);
		x2 = _mm_add_epi16(x2, f7[29]);
		x3 = _mm_add_epi16(x3, f7[30]);
		x0 = _mm_add_epi16(x0, f7[31]);

		*dst128++ = x1;
		*dst128++ = x2;
	} else
		*dst128++ = x0;
}

void ATArtifactPALChromaTwin_SSE2(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels) {
	__m128i x0 = _mm_setzero_si128();
	__m128i x1 = _mm_setzero_si128();
	__m128i x2 = _mm_setzero_si128();
	__m128i x3;

	__m128i *dst128 = (__m128i *)dst;
	const __m128i *kernels128 = (const __m128i *)kernels;
	uint32 n2 = n >> 4;
	do {
		const uint8 p0 = src[0];
		const uint8 p2 = src[2];

		const __m128i *f0 = kernels128 + 16U*p0;
		const __m128i *f2 = kernels128 + 16U*p2;

		x0 = _mm_add_epi16(x0, f0[0]);
		x1 = _mm_add_epi16(x1, f0[1]);
		x2 = _mm_add_epi16(x2, f0[2]);
		x3 = f0[3];

		x0 = _mm_add_epi16(x0, f2[4]);
		x1 = _mm_add_epi16(x1, f2[5]);
		x2 = _mm_add_epi16(x2, f2[6]);
		x3 = _mm_add_epi16(x3, f2[7]);

		*dst128++ = x0;

		const uint8 p4 = src[4];
		const uint8 p6 = src[6];

		const __m128i *f4 = kernels128 + 16U*p4;
		const __m128i *f6 = kernels128 + 16U*p6;

		x1 = _mm_add_epi16(x1, f4[8]);
		x2 = _mm_add_epi16(x2, f4[9]);
		x3 = _mm_add_epi16(x3, f4[10]);
		x0 = f4[11];

		x1 = _mm_add_epi16(x1, f6[12]);
		x2 = _mm_add_epi16(x2, f6[13]);
		x3 = _mm_add_epi16(x3, f6[14]);
		x0 = _mm_add_epi16(x0, f6[15]);

		*dst128++ = x1;

		const uint8 p8 = src[8];
		const uint8 p10 = src[10];

		const __m128i *f8 = kernels128 + 16U*p8;
		const __m128i *f10 = kernels128 + 16U*p10;

		x2 = _mm_add_epi16(x2, f8[0]);
		x3 = _mm_add_epi16(x3, f8[1]);
		x0 = _mm_add_epi16(x0, f8[2]);
		x1 = f8[3];

		x2 = _mm_add_epi16(x2, f10[4]);
		x3 = _mm_add_epi16(x3, f10[5]);
		x0 = _mm_add_epi16(x0, f10[6]);
		x1 = _mm_add_epi16(x1, f10[7]);

		*dst128++ = x2;

		const uint8 p12 = src[12];
		const uint8 p14 = src[14];

		const __m128i *f12 = kernels128 + 16U*p12;
		const __m128i *f14 = kernels128 + 16U*p14;

		x3 = _mm_add_epi16(x3, f12[8]);
		x0 = _mm_add_epi16(x0, f12[9]);
		x1 = _mm_add_epi16(x1, f12[10]);
		x2 = f12[11];

		x3 = _mm_add_epi16(x3, f14[12]);
		x0 = _mm_add_epi16(x0, f14[13]);
		x1 = _mm_add_epi16(x1, f14[14]);
		x2 = _mm_add_epi16(x2, f14[15]);

		*dst128++ = x3;

		src += 16;
	} while(--n2);

	if (n & 8) {
		const uint8 p0 = src[0];
		const uint8 p2 = src[2];

		const __m128i *f0 = kernels128 + 16*p0;
		const __m128i *f2 = kernels128 + 16*p2;

		x0 = _mm_add_epi16(x0, f0[0]);
		x1 = _mm_add_epi16(x1, f0[1]);
		x2 = _mm_add_epi16(x2, f0[2]);
		x3 = f0[3];

		x0 = _mm_add_epi16(x0, f2[4]);
		x1 = _mm_add_epi16(x1, f2[5]);
		x2 = _mm_add_epi16(x2, f2[6]);
		x3 = _mm_add_epi16(x3, f2[7]);

		*dst128++ = x0;

		const uint8 p4 = src[4];
		const uint8 p6 = src[6];

		const __m128i *f4 = kernels128 + 16*p4;
		const __m128i *f6 = kernels128 + 16*p6;

		x1 = _mm_add_epi16(x1, f4[8]);
		x2 = _mm_add_epi16(x2, f4[9]);
		x3 = _mm_add_epi16(x3, f4[10]);
		x0 = f4[11];

		x1 = _mm_add_epi16(x1, f6[12]);
		x2 = _mm_add_epi16(x2, f6[13]);
		x3 = _mm_add_epi16(x3, f6[14]);
		x0 = _mm_add_epi16(x0, f6[15]);

		*dst128++ = x1;
		*dst128++ = x2;
	} else 
		*dst128++ = x0;
}

void ATArtifactPALFinal_SSE2(uint32 *dst, const uint32 *ybuf, const uint32 *ubuf, const uint32 *vbuf, uint32 *ulbuf, uint32 *vlbuf, uint32 n) {
	static const __attribute__((align(16))) sint16 kCoeffU[]={
		-3182*4, -3182*4, -3182*4, -3182*4,	// -co_ug / co_ub * 16384 * 4
		-3182*4, -3182*4, -3182*4, -3182*4
	};

	static const __attribute__((align(16))) sint16 kCoeffV[]={
		-8346*4+0x10000, -8346*4+0x10000, -8346*4+0x10000, -8346*4+0x10000,	// -co_vg / co_vr * 16384 * 4, wrapped around
		-8346*4+0x10000, -8346*4+0x10000, -8346*4+0x10000, -8346*4+0x10000
	};

	n >>= 2;

	const __m128i *usrc = (const __m128i *)ubuf + 1;
	const __m128i *vsrc = (const __m128i *)vbuf + 1;
	const __m128i *ysrc = (const __m128i *)ybuf;
	__m128i *uprev = (__m128i *)ulbuf;
	__m128i *vprev = (__m128i *)vlbuf;

	const __m128i co_u = *(const __m128i *)kCoeffU;
	const __m128i co_v = *(const __m128i *)kCoeffV;

	__m128i *dst128 = (__m128i *)dst;

	do {
		__m128i up = *uprev;
		__m128i vp = *vprev;
		__m128i u = *usrc++;
		__m128i v = *vsrc++;

		*uprev++ = u;
		*vprev++ = v;

		u = _mm_add_epi16(u, up);
		v = _mm_add_epi16(v, vp);

		__m128i y = *ysrc++;

		__m128i r = _mm_add_epi16(y, v);
		__m128i b = _mm_add_epi16(y, u);

		__m128i gv = _mm_subs_epi16(_mm_mulhi_epi16(v, co_v), v);
		__m128i gu = _mm_mulhi_epi16(u, co_u);

		__m128i g = _mm_add_epi16(_mm_add_epi16(y, gu), gv);

		__m128i ir16 = _mm_srai_epi16(r, 6);
		__m128i ig16 = _mm_srai_epi16(g, 6);
		__m128i ib16 = _mm_srai_epi16(b, 6);

		__m128i ir8 = _mm_packus_epi16(ir16, ir16);
		__m128i ig8 = _mm_packus_epi16(ig16, ig16);
		__m128i ib8 = _mm_packus_epi16(ib16, ib16);

		__m128i irb8 = _mm_unpacklo_epi8(ib8, ir8);
		__m128i igg8 = _mm_unpacklo_epi8(ig8, ig8);
		__m128i lopixels = _mm_unpacklo_epi8(irb8, igg8);
		__m128i hipixels = _mm_unpackhi_epi8(irb8, igg8);

		*dst128++ = lopixels;
		*dst128++ = hipixels;
	} while(--n);
}
#endif

#if defined(VD_CPU_X86) || defined(VD_CPU_AMD64)
template<bool T_UseSignedPalette>
void ATArtifactPAL32_SSE2(void *dst, void *delayLine, uint32 n) {
	// For this path, we assume that the alpha channel holds precomputed luminance. This works because
	// the only source of raw RGB32 input is VBXE, and though it outputs 21-bit RGB, it can only do so
	// from 4 x 256 palettes. All we need to do is average the YRGB pixels between the delay line and
	// the current line, and then recorrect the luminance back.

	uint32 *VDRESTRICT dst32 = (uint32 *)dst;
	uint32 *VDRESTRICT delay32 = (uint32 *)delayLine;

	__m128i signbits = _mm_set1_epi32(0x80808080);
	__m128i alphamask = _mm_set1_epi32(0xFF000000);
	__m128i x40b = _mm_set1_epi8(0x40);

	const uint32 n4 = n >> 2;
	const uint32 n1 = n & 3;

	for(uint32 i=0; i<n4; ++i) {
		// The delay line can be relied upon for alignment, the destination not so much.
		// Not that it matters much as VS2017+ force MOVDQU. :(
		__m128i prev = *(__m128i *)delay32;
		__m128i next = _mm_loadu_si128((__m128i *)dst32);

		if (_mm_movemask_epi8(_mm_cmpeq_epi8(prev, next)) != 0xFFFF) {
			*(__m128i *)delay32 = next;

			__m128i avg = _mm_avg_epu8(prev, next);
			__m128i ydiff = _mm_and_si128(_mm_sub_epi8(next, avg), alphamask);
			__m128i ydiff2 = _mm_or_si128(ydiff, _mm_srli_epi16(ydiff, 8));
			__m128i ydiffrgb = _mm_shufflehi_epi16(_mm_shufflelo_epi16(ydiff2, 0xF5), 0xF5);

			__m128i final = _mm_xor_si128(_mm_adds_epi8(_mm_xor_si128(avg, signbits), ydiffrgb), signbits);

			if constexpr (T_UseSignedPalette) {
				final = _mm_subs_epu8(final, x40b);
				final = _mm_adds_epu8(final, final);
			}

			_mm_storeu_si128((__m128i *)dst32, final);
		} else if constexpr (T_UseSignedPalette) {
			__m128i final = next;

			final = _mm_subs_epu8(final, x40b);
			final = _mm_adds_epu8(final, final);

			_mm_storeu_si128((__m128i *)dst32, final);
		}

		delay32 += 4;
		dst32 += 4;
	}

	for(uint32 i=0; i<n1; ++i) {
		uint32 prev32 = *delay32;
		uint32 next32 = *dst32;

		if (prev32 != next32) {
			*delay32 = next32;

			__m128i next = _mm_cvtsi32_si128((int)next32);
			__m128i prev = _mm_cvtsi32_si128((int)prev32);
			__m128i avg = _mm_avg_epu8(prev, next);
			__m128i ydiff = _mm_and_si128(_mm_sub_epi8(next, avg), alphamask);
			__m128i ydiff2 = _mm_or_si128(ydiff, _mm_srli_epi16(ydiff, 8));
			__m128i ydiffrgb = _mm_shufflelo_epi16(ydiff2, 0xF5);

			__m128i final = _mm_xor_si128(_mm_adds_epi8(_mm_xor_si128(avg, signbits), ydiffrgb), signbits);

			if constexpr (T_UseSignedPalette) {
				final = _mm_subs_epu8(final, x40b);
				final = _mm_adds_epu8(final, final);
			}

			*dst32 = (uint32)_mm_cvtsi128_si32(final);
		} else if constexpr (T_UseSignedPalette) {
			__m128i final = _mm_cvtsi32_si128(next32);

			final = _mm_subs_epu8(final, x40b);
			final = _mm_adds_epu8(final, final);

			*dst32 = (uint32)_mm_cvtsi128_si32(final);
		}

		++delay32;
		++dst32;
	}
}

void ATArtifactPAL32_SSE2(void *dst, void *delayLine, uint32 n, bool compressExtendedRange) {
	if (compressExtendedRange)
		ATArtifactPAL32_SSE2<true>(dst, delayLine, n);
	else
		ATArtifactPAL32_SSE2<false>(dst, delayLine, n);
}

void ATArtifactPALFinalMono_SSE2(uint32 *dst0, const uint32 *ybuf, uint32 n, const uint32 palette0[256]) {
	const __m128i *VDRESTRICT ysrc = (const __m128i *)ybuf;
	const uint32 *VDRESTRICT palette = palette0;
	uint32 *VDRESTRICT dst = dst0;

	n >>= 2;

	// Compute coefficients.
	//
	// Luma is signed 12.6 and we're going to do a high multiply followed by a rounded halving
	// for a total right shift of 17 bits. This means that we need to convert our tint color to
	// a normalized 5.11 fraction.

	__m128i zero = _mm_setzero_si128();
	__m128i round = _mm_set1_epi16(0x20);
	__m128i maxval = _mm_set1_epi16(0xFF);

	for(uint32 i=0; i<n; ++i) {
		const __m128i y = *ysrc++;

		// convert signed 12.6 to u8
		const __m128i indices = _mm_min_epi16(_mm_max_epi16(_mm_srai_epi16(_mm_add_epi16(y, round), 6), zero), maxval);

		// convert 8 pixels
		dst[0] = palette[_mm_extract_epi16(indices, 0)];
		dst[1] = palette[_mm_extract_epi16(indices, 1)];
		dst[2] = palette[_mm_extract_epi16(indices, 2)];
		dst[3] = palette[_mm_extract_epi16(indices, 3)];
		dst[4] = palette[_mm_extract_epi16(indices, 4)];
		dst[5] = palette[_mm_extract_epi16(indices, 5)];
		dst[6] = palette[_mm_extract_epi16(indices, 6)];
		dst[7] = palette[_mm_extract_epi16(indices, 7)];
		dst += 8;
	}
}

#endif
