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

#include <stdafx.h>

#if VD_CPU_ARM64
#include <arm_neon.h>

// workaround for missing intrinsics with /Zc:arm64-aliased-neon-types- and broken types without
#if VD_COMPILER_MSVC
#ifdef vmull_p64
#undef vmull_p64
#endif

#define vmull_p64(src1, src2) __n128_to_poly64x2_t(neon_pmull_64(__uint64ToN64_v(src1), __uint64ToN64_v(src2)))

#ifndef vreinterpretq_p64_p128
#define vreinterpretq_p64_p128(x) __n128_to_poly64x2_t(x)
#endif

#ifndef vmull_high_p64
#define vmull_high_p64(src1, src2) __n128_to_poly64x2_t(neon_pmull2_64(__poly64x2_t_to_n128(src1), __poly64x2_t_to_n128(src2)))
#endif
#endif

template<int Order>
void ATFLACReconstructLPC_Narrow_NEON_Impl(sint32 *__restrict y, uint32 n, const sint32 *__restrict lpcCoeffs, int qlpShift) {
	int16x8_t coeffz = vmovq_n_s16(0);
	int16x8_t coeff0 = coeffz;
	int16x8_t coeff1;
	
	// load coefficients and pack to s16
	const auto loadPack = [](const sint32 *p) {
		return vcombine_s16(vqmovn_s32(vld1q_s32(p)), vqmovn_s32(vld1q_s32(p + 4)));
	};

	coeff1 = loadPack(lpcCoeffs);

	if constexpr (Order > 8) {
		coeff0 = coeff1;
		coeff1 = loadPack(lpcCoeffs + 8);
	}

	// left pack the coefficients
	if constexpr ((Order & 7) != 0) {
		coeff1 = vextq_s16(coeff0, coeff1, Order & 7);

		if constexpr (Order > 8)
			coeff0 = vextq_s16(coeffz, coeff0, Order & 7);
	}

	// preload the pipeline with the warm-up samples
	int32x4_t pipez = vmovq_n_s32(0);
	int32x4_t pipe0 = pipez;
	int32x4_t pipe1 = pipez;
	int32x4_t pipe2 = pipez;
	int32x4_t pipe3 = pipez;

	for(uint32 i = 0; i < Order; ++i) {
		const sint16 v = y[i];

		{
		                          pipe3 = vextq_s32(pipe2, pipe3, 3);
		if constexpr (Order >  4) pipe2 = vextq_s32(pipe1, pipe2, 3);
		if constexpr (Order >  8) pipe1 = vextq_s32(pipe0, pipe1, 3);
		if constexpr (Order > 12) pipe0 = vextq_s32(pipez, pipe0, 3);
		}

		{
		                          pipe3 = vmlal_high_n_s16(pipe3, coeff1, v);
		if constexpr (Order >  4) pipe2 = vmlal_n_s16(pipe2, vget_low_s16(coeff1), v);
		if constexpr (Order >  8) pipe1 = vmlal_high_n_s16(pipe1, coeff0, v);
		if constexpr (Order > 12) pipe0 = vmlal_n_s16(pipe0, vget_low_s16(coeff0), v);
		}
	}

	int32x2_t vshift = vmov_n_s32(-qlpShift);

	// run the pipeline -- note that we must specifically NOT using saturating adds here,
	// as we can rely on temporary overflows getting corrected back into range for the final
	// sum pre-shift
	for(uint32 i = 0; i < n; ++i) {
		// apply quantization shift to get prediction, then add residual
		int32x2_t v = vadd_s32(vshl_s32(vget_high_s32(pipe3), vshift), vmov_n_s32(y[i + Order]));

		// write reconstructed value
		y[i + Order] = vget_lane_s32(v, 1);

		// shift pipeline
		{
		                          pipe3 = vextq_s32(pipe2, pipe3, 3);
		if constexpr (Order >  4) pipe2 = vextq_s32(pipe1, pipe2, 3);
		if constexpr (Order >  8) pipe1 = vextq_s32(pipe0, pipe1, 3);
		if constexpr (Order > 12) pipe0 = vextq_s32(pipez, pipe0, 3);
		}

		// add in new sample
		{
		                          pipe3 = vmlal_high_lane_s16(pipe3, coeff1, vreinterpret_s16_s32(v), 2);
		if constexpr (Order >  4) pipe2 = vmlal_lane_s16     (pipe2, vget_low_s16(coeff1), vreinterpret_s16_s32(v), 2);
		if constexpr (Order >  8) pipe1 = vmlal_high_lane_s16(pipe1, coeff0, vreinterpret_s16_s32(v), 2);
		if constexpr (Order > 12) pipe0 = vmlal_lane_s16     (pipe0, vget_low_s16(coeff0), vreinterpret_s16_s32(v), 2);
		}
	}
}

void ATFLACReconstructLPC_Narrow_NEON(sint32 *y, uint32 n, const sint32 *lpcCoeffs, int qlpShift, int order) {
	switch(order) {
		case  2: ATFLACReconstructLPC_Narrow_NEON_Impl< 2>(y, n, lpcCoeffs, qlpShift); break;
		case  3: ATFLACReconstructLPC_Narrow_NEON_Impl< 3>(y, n, lpcCoeffs, qlpShift); break;
		case  4: ATFLACReconstructLPC_Narrow_NEON_Impl< 4>(y, n, lpcCoeffs, qlpShift); break;
		case  5: ATFLACReconstructLPC_Narrow_NEON_Impl< 5>(y, n, lpcCoeffs, qlpShift); break;
		case  6: ATFLACReconstructLPC_Narrow_NEON_Impl< 6>(y, n, lpcCoeffs, qlpShift); break;
		case  7: ATFLACReconstructLPC_Narrow_NEON_Impl< 7>(y, n, lpcCoeffs, qlpShift); break;
		case  8: ATFLACReconstructLPC_Narrow_NEON_Impl< 8>(y, n, lpcCoeffs, qlpShift); break;
		case  9: ATFLACReconstructLPC_Narrow_NEON_Impl< 9>(y, n, lpcCoeffs, qlpShift); break;
		case 10: ATFLACReconstructLPC_Narrow_NEON_Impl<10>(y, n, lpcCoeffs, qlpShift); break;
		case 11: ATFLACReconstructLPC_Narrow_NEON_Impl<11>(y, n, lpcCoeffs, qlpShift); break;
		case 12: ATFLACReconstructLPC_Narrow_NEON_Impl<12>(y, n, lpcCoeffs, qlpShift); break;
		case 13: ATFLACReconstructLPC_Narrow_NEON_Impl<13>(y, n, lpcCoeffs, qlpShift); break;
		case 14: ATFLACReconstructLPC_Narrow_NEON_Impl<14>(y, n, lpcCoeffs, qlpShift); break;
		case 15: ATFLACReconstructLPC_Narrow_NEON_Impl<15>(y, n, lpcCoeffs, qlpShift); break;
		case 16: ATFLACReconstructLPC_Narrow_NEON_Impl<16>(y, n, lpcCoeffs, qlpShift); break;
	}
}

template<int Order>
void ATFLACReconstructLPC_Medium_NEON_Impl(sint32 *__restrict y, uint32 n, const sint32 *__restrict lpcCoeffs, int qlpShift) {
	int32x4_t coeffz = vmovq_n_s32(0);
	int32x4_t coeff0 = coeffz;
	int32x4_t coeff1 = coeffz;
	int32x4_t coeff2 = coeffz;
	int32x4_t coeff3 = coeffz;
	int32x4_t coeff4 = coeffz;
	int32x4_t coeff5 = coeffz;
	int32x4_t coeff6 = coeffz;
	int32x4_t coeff7 = coeffz;
	
	// load coefficients
	const sint32 *__restrict lpcCoeffsEnd = lpcCoeffs + ((Order + 3) & ~3);
	                          coeff7 = vld1q_s32(&lpcCoeffsEnd[- 4]);
	if constexpr (Order >  4) coeff6 = vld1q_s32(&lpcCoeffsEnd[- 8]);
	if constexpr (Order >  8) coeff5 = vld1q_s32(&lpcCoeffsEnd[-12]);
	if constexpr (Order > 12) coeff4 = vld1q_s32(&lpcCoeffsEnd[-16]);
	if constexpr (Order > 16) coeff3 = vld1q_s32(&lpcCoeffsEnd[-20]);
	if constexpr (Order > 20) coeff2 = vld1q_s32(&lpcCoeffsEnd[-24]);
	if constexpr (Order > 24) coeff1 = vld1q_s32(&lpcCoeffsEnd[-28]);
	if constexpr (Order > 28) coeff0 = vld1q_s32(&lpcCoeffsEnd[-32]);

	// left pack the coefficients
	if constexpr ((Order & 3) != 0) {
		                          coeff7 = vextq_s32(coeff6, coeff7, Order & 3);
		if constexpr (Order >  4) coeff6 = vextq_s32(coeff5, coeff6, Order & 3);
		if constexpr (Order >  8) coeff5 = vextq_s32(coeff4, coeff5, Order & 3);
		if constexpr (Order > 12) coeff4 = vextq_s32(coeff3, coeff4, Order & 3);
		if constexpr (Order > 16) coeff3 = vextq_s32(coeff2, coeff3, Order & 3);
		if constexpr (Order > 20) coeff2 = vextq_s32(coeff1, coeff2, Order & 3);
		if constexpr (Order > 24) coeff1 = vextq_s32(coeff0, coeff1, Order & 3);
		if constexpr (Order > 28) coeff0 = vextq_s32(coeffz, coeff0, Order & 3);
	}

	// preload the pipeline with the warm-up samples
	int32x4_t pipez = vmovq_n_s32(0);
	int32x4_t pipe0 = pipez;
	int32x4_t pipe1 = pipez;
	int32x4_t pipe2 = pipez;
	int32x4_t pipe3 = pipez;
	int32x4_t pipe4 = pipez;
	int32x4_t pipe5 = pipez;
	int32x4_t pipe6 = pipez;
	int32x4_t pipe7 = pipez;

	for(uint32 i = 0; i < Order; ++i) {
		const sint16 v = y[i];

		{
		                          pipe7 = vextq_s32(pipe6, pipe7, 3);
		if constexpr (Order >  4) pipe6 = vextq_s32(pipe5, pipe6, 3);
		if constexpr (Order >  8) pipe5 = vextq_s32(pipe4, pipe5, 3);
		if constexpr (Order > 12) pipe4 = vextq_s32(pipe3, pipe4, 3);
		if constexpr (Order > 16) pipe3 = vextq_s32(pipe2, pipe3, 3);
		if constexpr (Order > 20) pipe2 = vextq_s32(pipe1, pipe2, 3);
		if constexpr (Order > 24) pipe1 = vextq_s32(pipe0, pipe1, 3);
		if constexpr (Order > 28) pipe0 = vextq_s32(pipez, pipe0, 3);
		}

		{
		                          pipe7 = vmlaq_n_s32(pipe7, coeff7, v);
		if constexpr (Order >  4) pipe6 = vmlaq_n_s32(pipe6, coeff6, v);
		if constexpr (Order >  8) pipe5 = vmlaq_n_s32(pipe5, coeff5, v);
		if constexpr (Order > 12) pipe4 = vmlaq_n_s32(pipe4, coeff4, v);
		if constexpr (Order > 16) pipe3 = vmlaq_n_s32(pipe3, coeff3, v);
		if constexpr (Order > 20) pipe2 = vmlaq_n_s32(pipe2, coeff2, v);
		if constexpr (Order > 24) pipe1 = vmlaq_n_s32(pipe1, coeff1, v);
		if constexpr (Order > 28) pipe0 = vmlaq_n_s32(pipe0, coeff0, v);
		}
	}

	int32x2_t vshift = vmov_n_s32(-qlpShift);

	// run the pipeline -- note that we must specifically NOT using saturating adds here,
	// as we can rely on temporary overflows getting corrected back into range for the final
	// sum pre-shift
	for(uint32 i = 0; i < n; ++i) {
		// apply quantization shift to get prediction, then add residual
		int32x2_t v = vadd_s32(vshl_s32(vget_high_s32(pipe7), vshift), vmov_n_s32(y[i + Order]));

		// write reconstructed value
		y[i + Order] = vget_lane_s32(v, 1);

		// shift pipeline
		{
		                          pipe7 = vextq_s32(pipe6, pipe7, 3);
		if constexpr (Order >  4) pipe6 = vextq_s32(pipe5, pipe6, 3);
		if constexpr (Order >  8) pipe5 = vextq_s32(pipe4, pipe5, 3);
		if constexpr (Order > 12) pipe4 = vextq_s32(pipe3, pipe4, 3);
		if constexpr (Order > 16) pipe3 = vextq_s32(pipe2, pipe3, 3);
		if constexpr (Order > 20) pipe2 = vextq_s32(pipe1, pipe2, 3);
		if constexpr (Order > 24) pipe1 = vextq_s32(pipe0, pipe1, 3);
		if constexpr (Order > 28) pipe0 = vextq_s32(pipez, pipe0, 3);
		}

		// add in new sample
		{
		                          pipe7 = vmlaq_lane_s32(pipe7, coeff7, v, 1);
		if constexpr (Order >  4) pipe6 = vmlaq_lane_s32(pipe6, coeff6, v, 1);
		if constexpr (Order >  8) pipe5 = vmlaq_lane_s32(pipe5, coeff5, v, 1);
		if constexpr (Order > 12) pipe4 = vmlaq_lane_s32(pipe4, coeff4, v, 1);
		if constexpr (Order > 16) pipe3 = vmlaq_lane_s32(pipe3, coeff3, v, 1);
		if constexpr (Order > 20) pipe2 = vmlaq_lane_s32(pipe2, coeff2, v, 1);
		if constexpr (Order > 24) pipe1 = vmlaq_lane_s32(pipe1, coeff1, v, 1);
		if constexpr (Order > 28) pipe0 = vmlaq_lane_s32(pipe0, coeff0, v, 1);
		}
	}
}

void ATFLACReconstructLPC_Medium_NEON(sint32 *y, uint32 n, const sint32 *lpcCoeffs, int qlpShift, int order) {
	switch(order) {
		case  2: ATFLACReconstructLPC_Medium_NEON_Impl< 2>(y, n, lpcCoeffs, qlpShift); break;
		case  3: ATFLACReconstructLPC_Medium_NEON_Impl< 3>(y, n, lpcCoeffs, qlpShift); break;
		case  4: ATFLACReconstructLPC_Medium_NEON_Impl< 4>(y, n, lpcCoeffs, qlpShift); break;
		case  5: ATFLACReconstructLPC_Medium_NEON_Impl< 5>(y, n, lpcCoeffs, qlpShift); break;
		case  6: ATFLACReconstructLPC_Medium_NEON_Impl< 6>(y, n, lpcCoeffs, qlpShift); break;
		case  7: ATFLACReconstructLPC_Medium_NEON_Impl< 7>(y, n, lpcCoeffs, qlpShift); break;
		case  8: ATFLACReconstructLPC_Medium_NEON_Impl< 8>(y, n, lpcCoeffs, qlpShift); break;
		case  9: ATFLACReconstructLPC_Medium_NEON_Impl< 9>(y, n, lpcCoeffs, qlpShift); break;
		case 10: ATFLACReconstructLPC_Medium_NEON_Impl<10>(y, n, lpcCoeffs, qlpShift); break;
		case 11: ATFLACReconstructLPC_Medium_NEON_Impl<11>(y, n, lpcCoeffs, qlpShift); break;
		case 12: ATFLACReconstructLPC_Medium_NEON_Impl<12>(y, n, lpcCoeffs, qlpShift); break;
		case 13: ATFLACReconstructLPC_Medium_NEON_Impl<13>(y, n, lpcCoeffs, qlpShift); break;
		case 14: ATFLACReconstructLPC_Medium_NEON_Impl<14>(y, n, lpcCoeffs, qlpShift); break;
		case 15: ATFLACReconstructLPC_Medium_NEON_Impl<15>(y, n, lpcCoeffs, qlpShift); break;
		case 16: ATFLACReconstructLPC_Medium_NEON_Impl<16>(y, n, lpcCoeffs, qlpShift); break;
		case 17: ATFLACReconstructLPC_Medium_NEON_Impl<17>(y, n, lpcCoeffs, qlpShift); break;
		case 18: ATFLACReconstructLPC_Medium_NEON_Impl<18>(y, n, lpcCoeffs, qlpShift); break;
		case 19: ATFLACReconstructLPC_Medium_NEON_Impl<19>(y, n, lpcCoeffs, qlpShift); break;
		case 20: ATFLACReconstructLPC_Medium_NEON_Impl<20>(y, n, lpcCoeffs, qlpShift); break;
		case 21: ATFLACReconstructLPC_Medium_NEON_Impl<21>(y, n, lpcCoeffs, qlpShift); break;
		case 22: ATFLACReconstructLPC_Medium_NEON_Impl<22>(y, n, lpcCoeffs, qlpShift); break;
		case 23: ATFLACReconstructLPC_Medium_NEON_Impl<23>(y, n, lpcCoeffs, qlpShift); break;
		case 24: ATFLACReconstructLPC_Medium_NEON_Impl<24>(y, n, lpcCoeffs, qlpShift); break;
		case 25: ATFLACReconstructLPC_Medium_NEON_Impl<25>(y, n, lpcCoeffs, qlpShift); break;
		case 26: ATFLACReconstructLPC_Medium_NEON_Impl<26>(y, n, lpcCoeffs, qlpShift); break;
		case 27: ATFLACReconstructLPC_Medium_NEON_Impl<27>(y, n, lpcCoeffs, qlpShift); break;
		case 28: ATFLACReconstructLPC_Medium_NEON_Impl<28>(y, n, lpcCoeffs, qlpShift); break;
		case 29: ATFLACReconstructLPC_Medium_NEON_Impl<29>(y, n, lpcCoeffs, qlpShift); break;
		case 30: ATFLACReconstructLPC_Medium_NEON_Impl<30>(y, n, lpcCoeffs, qlpShift); break;
		case 31: ATFLACReconstructLPC_Medium_NEON_Impl<31>(y, n, lpcCoeffs, qlpShift); break;
		case 32: ATFLACReconstructLPC_Medium_NEON_Impl<32>(y, n, lpcCoeffs, qlpShift); break;
	}
}

template<int Order>
void ATFLACReconstructLPC_Wide_NEON_Impl(sint32 *__restrict y, uint32 n, const sint32 *__restrict lpcCoeffs, int qlpShift) {
	static constexpr int OrderQuads = (Order+3) >> 2;
	int32x4_t coeffs[OrderQuads];
	
	// load coefficients
	for(int i=0; i<OrderQuads; ++i)
		coeffs[i] = vld1q_s32(lpcCoeffs + 4*i);

	for(uint32 i = 0; i < n; ++i) {
		// compute dot product
		int64x2_t vsrc;
		int64x2_t vacc;
		
		                          vsrc = vld1q_s32(y +  0);
		                          vacc = vmull_s32     (      vget_low_s32(vsrc), vget_low_s32(coeffs[0]));
		if constexpr (Order >  2) vacc = vmlal_high_s32(vacc,              vsrc ,              coeffs[0] );

		if constexpr (Order >  4) vsrc = vld1q_s32(y +  4);
		if constexpr (Order >  4) vacc = vmlal_s32     (vacc, vget_low_s32(vsrc), vget_low_s32(coeffs[1]));
		if constexpr (Order >  6) vacc = vmlal_high_s32(vacc,              vsrc ,              coeffs[1] );

		if constexpr (Order >  8) vsrc = vld1q_s32(y +  8);
		if constexpr (Order >  8) vacc = vmlal_s32     (vacc, vget_low_s32(vsrc), vget_low_s32(coeffs[2]));
		if constexpr (Order > 10) vacc = vmlal_high_s32(vacc,              vsrc ,              coeffs[2] );

		if constexpr (Order > 12) vsrc = vld1q_s32(y + 12);
		if constexpr (Order > 12) vacc = vmlal_s32     (vacc, vget_low_s32(vsrc), vget_low_s32(coeffs[3]));
		if constexpr (Order > 14) vacc = vmlal_high_s32(vacc,              vsrc ,              coeffs[3] );

		if constexpr (Order > 16) vsrc = vld1q_s32(y + 16);
		if constexpr (Order > 16) vacc = vmlal_s32     (vacc, vget_low_s32(vsrc), vget_low_s32(coeffs[4]));
		if constexpr (Order > 18) vacc = vmlal_high_s32(vacc,              vsrc ,              coeffs[4] );

		if constexpr (Order > 20) vsrc = vld1q_s32(y + 20);
		if constexpr (Order > 20) vacc = vmlal_s32     (vacc, vget_low_s32(vsrc), vget_low_s32(coeffs[5]));
		if constexpr (Order > 22) vacc = vmlal_high_s32(vacc,              vsrc ,              coeffs[5] );

		if constexpr (Order > 24) vsrc = vld1q_s32(y + 24);
		if constexpr (Order > 24) vacc = vmlal_s32     (vacc, vget_low_s32(vsrc), vget_low_s32(coeffs[6]));
		if constexpr (Order > 26) vacc = vmlal_high_s32(vacc,              vsrc ,              coeffs[6] );

		if constexpr (Order > 28) vsrc = vld1q_s32(y + 28);
		if constexpr (Order > 28) vacc = vmlal_s32     (vacc, vget_low_s32(vsrc), vget_low_s32(coeffs[7]));
		if constexpr (Order > 30) vacc = vmlal_high_s32(vacc,              vsrc ,              coeffs[7] );

		// apply quantization shift to get prediction, then add residual
		int32_t v = y[Order] + (vaddvq_s64(vacc) >> qlpShift);

		// write reconstructed value
		y[Order] = v;
		++y;
	}
}

void ATFLACReconstructLPC_Wide_NEON(sint32 *y, uint32 n, const sint32 *lpcCoeffs, int qlpShift, int order) {
	switch(order) {
		case  2: ATFLACReconstructLPC_Wide_NEON_Impl< 2>(y, n, lpcCoeffs, qlpShift); break;
		case  3: ATFLACReconstructLPC_Wide_NEON_Impl< 3>(y, n, lpcCoeffs, qlpShift); break;
		case  4: ATFLACReconstructLPC_Wide_NEON_Impl< 4>(y, n, lpcCoeffs, qlpShift); break;
		case  5: ATFLACReconstructLPC_Wide_NEON_Impl< 5>(y, n, lpcCoeffs, qlpShift); break;
		case  6: ATFLACReconstructLPC_Wide_NEON_Impl< 6>(y, n, lpcCoeffs, qlpShift); break;
		case  7: ATFLACReconstructLPC_Wide_NEON_Impl< 7>(y, n, lpcCoeffs, qlpShift); break;
		case  8: ATFLACReconstructLPC_Wide_NEON_Impl< 8>(y, n, lpcCoeffs, qlpShift); break;
		case  9: ATFLACReconstructLPC_Wide_NEON_Impl< 9>(y, n, lpcCoeffs, qlpShift); break;
		case 10: ATFLACReconstructLPC_Wide_NEON_Impl<10>(y, n, lpcCoeffs, qlpShift); break;
		case 11: ATFLACReconstructLPC_Wide_NEON_Impl<11>(y, n, lpcCoeffs, qlpShift); break;
		case 12: ATFLACReconstructLPC_Wide_NEON_Impl<12>(y, n, lpcCoeffs, qlpShift); break;
		case 13: ATFLACReconstructLPC_Wide_NEON_Impl<13>(y, n, lpcCoeffs, qlpShift); break;
		case 14: ATFLACReconstructLPC_Wide_NEON_Impl<14>(y, n, lpcCoeffs, qlpShift); break;
		case 15: ATFLACReconstructLPC_Wide_NEON_Impl<15>(y, n, lpcCoeffs, qlpShift); break;
		case 16: ATFLACReconstructLPC_Wide_NEON_Impl<16>(y, n, lpcCoeffs, qlpShift); break;
		case 17: ATFLACReconstructLPC_Wide_NEON_Impl<17>(y, n, lpcCoeffs, qlpShift); break;
		case 18: ATFLACReconstructLPC_Wide_NEON_Impl<18>(y, n, lpcCoeffs, qlpShift); break;
		case 19: ATFLACReconstructLPC_Wide_NEON_Impl<19>(y, n, lpcCoeffs, qlpShift); break;
		case 20: ATFLACReconstructLPC_Wide_NEON_Impl<20>(y, n, lpcCoeffs, qlpShift); break;
		case 21: ATFLACReconstructLPC_Wide_NEON_Impl<21>(y, n, lpcCoeffs, qlpShift); break;
		case 22: ATFLACReconstructLPC_Wide_NEON_Impl<22>(y, n, lpcCoeffs, qlpShift); break;
		case 23: ATFLACReconstructLPC_Wide_NEON_Impl<23>(y, n, lpcCoeffs, qlpShift); break;
		case 24: ATFLACReconstructLPC_Wide_NEON_Impl<24>(y, n, lpcCoeffs, qlpShift); break;
		case 25: ATFLACReconstructLPC_Wide_NEON_Impl<25>(y, n, lpcCoeffs, qlpShift); break;
		case 26: ATFLACReconstructLPC_Wide_NEON_Impl<26>(y, n, lpcCoeffs, qlpShift); break;
		case 27: ATFLACReconstructLPC_Wide_NEON_Impl<27>(y, n, lpcCoeffs, qlpShift); break;
		case 28: ATFLACReconstructLPC_Wide_NEON_Impl<28>(y, n, lpcCoeffs, qlpShift); break;
		case 29: ATFLACReconstructLPC_Wide_NEON_Impl<29>(y, n, lpcCoeffs, qlpShift); break;
		case 30: ATFLACReconstructLPC_Wide_NEON_Impl<30>(y, n, lpcCoeffs, qlpShift); break;
		case 31: ATFLACReconstructLPC_Wide_NEON_Impl<31>(y, n, lpcCoeffs, qlpShift); break;
		case 32: ATFLACReconstructLPC_Wide_NEON_Impl<32>(y, n, lpcCoeffs, qlpShift); break;
	}
}

VD_CPU_TARGET("+aes")
VDNOINLINE uint16 ATFLACUpdateCRC16_Crypto(uint16 crc16v, const void *buf, size_t n) {
	const uint8 *__restrict p = (const uint8 *)buf;

	const auto updateSmall = [=](uint8x16_t crc16, const uint8 *__restrict p, size_t n) VD_CPU_TARGET_LAMBDA("+aes") -> uint8x16_t {
		alignas(16) uint8_t buf[16] {};

		memcpy(buf + 16 - n, p, n);

		uint8x16_t v = vrev64q_u8(vld1q_u8(buf));
		v = vextq_u8(v, v, 8);

		// table of x^8^(i+1) mod P
		static constexpr uint32 k[23] {
			0x0100, 0x8005, 0x8603, 0x8017, 0x9403, 0x807B, 0xF803, 0x8113, 0x1006, 0x8663, 0xE017, 0x9543, 0x407E, 0xFF83, 0x8102, 0x0106, 0x8605, 0x8617, 0x9417, 0x947B, 0xF87B, 0xF913, 0x1116
		};

		//    crc16*x^bits mod P
		//	= loquad(crc16)*x^bits + hiquad(crc16)*x^(bits+64) mod P
		poly64x2_t factors = vreinterpretq_p64_u64(vcombine_u64(vmov_n_u64(k[n-1]), vmov_n_u64(k[n+7])));
		uint8x16_t crc16_lo = vreinterpretq_u8_p128(vmull_p64(vgetq_lane_p64(vreinterpretq_p64_u8(crc16), 0), vgetq_lane_p64(factors, 0)));
		uint8x16_t crc16_hi = vreinterpretq_u8_p128(vmull_high_p64(vreinterpretq_p64_u8(crc16), factors));
		return veorq_u8(veorq_u8(v, crc16_lo), crc16_hi);
	};

	// load CRC16 to vector reg
	uint8x16_t crc16 = vreinterpretq_u8_u16(vsetq_lane_u16(crc16v, vmovq_n_u16(0), 0));

	// align the start
	const uint32 startOffset = (uint32)((uintptr_t)p & 15);
	if (startOffset) {
		uint32 preAlign = (uint32)std::min<size_t>(16 - startOffset, n);

		crc16 = updateSmall(crc16, p, preAlign);

		p += preAlign;
		n -= preAlign;
	}

	// fold 128 bits (16 bytes) at a time
	alignas(16) static constexpr uint64_t kFoldConst[2] = { 0x0106, 0x1666 };		// x^128 mod P, x^192 mod P
	poly64x2_t k16 = vreinterpretq_p64_u64(vld1q_u64(kFoldConst));
	size_t nv = n >> 4;
	while(nv--) {
		uint8x16_t v = vrev64q_u8(vld1q_u8(p));
		v = vextq_u8(v, v, 8);
		p += 16;

		uint8x16_t crc16_lo = vreinterpretq_u8_p128(vmull_p64(vgetq_lane_p64(vreinterpretq_p64_u8(crc16), 0), vgetq_lane_p64(k16, 0)));
		uint8x16_t crc16_hi = vreinterpretq_u8_p128(vmull_high_p64(vreinterpretq_p64_u8(crc16), k16));

		crc16 = veorq_u8(veorq_u8(v, crc16_lo), crc16_hi);
	}

	// handle leftover bytes
	if (n & 15)
		crc16 = updateSmall(crc16, p, n & 15);

	const poly64x2_t x64modP = vreinterpretq_p64_u64(vmovq_n_u64(0x8113));
	const poly64_t x48modP = vget_lane_p64(vreinterpret_p64_u64(vmov_n_u64(0x807B)), 0);
	const poly64_t x32modP = vget_lane_p64(vreinterpret_p64_u64(vmov_n_u64(0x8017)), 0);

	// fold from 128-bit to 80-bit: crc16[127..64]*(x^64 mod P) + crc16[63..0]
	uint8x16_t crc16_80 = veorq_u8(vreinterpretq_u8_u64(vsetq_lane_u64(0, vreinterpretq_u64_u8(crc16), 1)), vreinterpretq_u8_p128(vmull_high_p64(vreinterpretq_p64_u8(crc16), x64modP)));

	// fold from 80-bit to 48-bit: crc16[79..48]*(x^48 mod P) + crc16[47..0]
	poly64x1_t crc16_80_high = vreinterpret_p64_u64(vshr_n_u64(vreinterpret_u64_u8(vget_low_u8(vextq_u8(crc16_80, crc16_80, 2))), 32));
	uint8x8_t crc16_48 = veor_u8(vget_low_u8(crc16_80), vget_low_u8(vreinterpretq_u8_p128(vmull_p64(vget_lane_p64(crc16_80_high, 0), x48modP))));

	// fold from 48-bit to 32-bit: crc16[47..32]*(x^32 mod P) + crc16[31..0]
	poly64x1_t crc16_48_high = vreinterpret_p64_u16(vset_lane_u16(0, vreinterpret_u16_u64(vshr_n_u64(vreinterpret_u64_u8(crc16_48), 32)), 1));
	uint8x8_t crc16_32 = veor_u8(crc16_48, vget_low_u8(vreinterpretq_u8_p128(vmull_p64(vget_lane_p64(crc16_48_high, 0), x32modP))));

	// do Barrett reduction from 32-bit to 16-bit
	// T1 = (R / x^16) * (x^32/P)
	// T2 = (T1 / x^16) * P
	// C = R + T2 mod x^16
	poly64_t mu = vget_lane_p64(vreinterpret_p64_u64(vmov_n_u64(0x1FFFB)), 0);
	poly64_t P = vget_lane_p64(vreinterpret_p64_u64(vmov_n_u64(0x18005)), 0);
	auto T1 =
		vget_low_p64(
			vreinterpretq_p64_p128(vmull_p64(
				vget_lane_p64(vreinterpret_p64_u32(
					vset_lane_u32(0, vshr_n_u32(vreinterpret_u32_u8(crc16_32), 16), 1)
				), 0),
				mu
			)
		)
	);
	auto T2 = vget_low_p64(
		vreinterpretq_p64_p128(
			vmull_p64(
				vget_lane_p64(vreinterpret_p64_u64(vshr_n_u64(vreinterpret_u64_p64(T1), 16)), 0),
				P
			)
		)
	);

	auto crc16final = veor_u16(vreinterpret_u16_u8(crc16_32), vreinterpret_u16_p64(T2));
	return vget_lane_u16(crc16final, 0);
}
#endif
