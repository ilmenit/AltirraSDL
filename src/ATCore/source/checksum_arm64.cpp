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
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#include <stdafx.h>

#if VD_CPU_ARM64

#include <vd2/system/binary.h>
#include <vd2/system/cpuaccel.h>
#include <at/atcore/checksum.h>
#include <at/atcore/internal/checksum.h>
#include <intrin.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <arm_neon.h>

void ATChecksumUpdateSHA256_NEON(ATChecksumStateSHA256& VDRESTRICT state, const void* src, size_t numBlocks) {
	using namespace nsATChecksum;

	alignas(16) uint32 W[64];

	const unsigned char* VDRESTRICT src2 = (const unsigned char*)src;

	while(numBlocks--) {
		uint32x4_t v0 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(src2     )));
		uint32x4_t v1 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(src2 + 16)));
		uint32x4_t v2 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(src2 + 32)));
		uint32x4_t v3 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(src2 + 48)));

		vst1q_u32(&W[ 0], vaddq_u32(v0, vld1q_u32(&K[ 0])));
		vst1q_u32(&W[ 4], vaddq_u32(v1, vld1q_u32(&K[ 4])));
		vst1q_u32(&W[ 8], vaddq_u32(v2, vld1q_u32(&K[ 8])));
		vst1q_u32(&W[12], vaddq_u32(v3, vld1q_u32(&K[12])));

		for(uint32 i = 16; i < 64; i += 4) {
			uint32x4_t back16 = v0;
			uint32x4_t back15 = vextq_u32(v0, v1, 1);
			uint32x4_t back7 = vextq_u32(v2, v3, 1);

			// ((W[i-15] >>> 7) ^ (W[i-15] >>> 18) ^ (W[i-15] >> 3)
			back15 = veorq_u32(
				veorq_u32(
					vorrq_u32(vshrq_n_u32(back15, 7), vshlq_n_u32(back15, 32 - 7)),
					vorrq_u32(vshrq_n_u32(back15, 18), vshlq_n_u32(back15, 32 - 18))
				),
				vshrq_n_u32(back15, 3)
			);

			uint32x4_t vt = vaddq_u32(vaddq_u32(back16, back7), back15);

			// compute low two words of ((W[i-2] >>> 17) ^ (W[i-2] >>> 19) ^ (W[i-2] >> 10))
			uint64x2_t back2lo = vreinterpretq_u64_u32(vzip2q_u32(v3, v3));
			uint32x4_t v4lo = veorq_u32(
				vreinterpretq_u32_u64(veorq_u64(vshrq_n_u64(back2lo, 17), vshrq_n_u64(back2lo, 19))),
				vshrq_n_u32(vreinterpretq_u32_u64(back2lo), 10)
			);
			v4lo = vaddq_u32(vuzp1q_u32(v4lo, v4lo), vt);

			// compute high two words of ((W[i-2] >>> 17) ^ (W[i-2] >>> 19) ^ (W[i-2] >> 10))
			uint64x2_t back2hi = vreinterpretq_u64_u32(vzip1q_u32(v4lo, v4lo));
			uint32x4_t v4hi = veorq_u32(
				vreinterpretq_u32_u64(veorq_u64(vshrq_n_u64(back2hi, 17), vshrq_n_u64(back2hi, 19))),
				vshrq_n_u32(vreinterpretq_u32_u64(back2hi), 10)
			);
			v4hi = vaddq_u32(vuzp1q_u32(v4hi, v4hi), vt);

			uint32x4_t v4 = vcombine_u32(vget_low_u32(v4lo), vget_high_u32(v4hi));

			vst1q_u32(&W[i], vaddq_u32(v4, vld1q_u32(&K[i])));

			v0 = v1;
			v1 = v2;
			v2 = v3;
			v3 = v4;
		}

		src2 += 64;

		ATChecksumStateSHA256 state2(state);

		for(uint32 i = 0; i < 64; ++i) {
			uint32 T1
				= state2.H[7]
				+ (_rotr(state2.H[4], 6) ^ _rotr(state2.H[4], 11) ^ _rotr(state2.H[4], 25))
				+ (state2.H[6] ^ ((state2.H[5] ^ state2.H[6]) & state2.H[4])) + W[i];

			uint32 T2
				= (_rotr(state2.H[0], 2) ^ _rotr(state2.H[0], 13) ^ _rotr(state2.H[0], 22))
				+ ((state2.H[0] & state2.H[1]) ^ (state2.H[0] & state2.H[2]) ^ (state2.H[1] & state2.H[2]));

			state2.H[7] = state2.H[6];
			state2.H[6] = state2.H[5];
			state2.H[5] = state2.H[4];
			state2.H[4] = state2.H[3] + T1;
			state2.H[3] = state2.H[2];
			state2.H[2] = state2.H[1];
			state2.H[1] = state2.H[0];
			state2.H[0] = T1 + T2;
		}

		state.H[0] += state2.H[0];
		state.H[1] += state2.H[1];
		state.H[2] += state2.H[2];
		state.H[3] += state2.H[3];
		state.H[4] += state2.H[4];
		state.H[5] += state2.H[5];
		state.H[6] += state2.H[6];
		state.H[7] += state2.H[7];
	}
}

#if 0
void ATChecksumUpdateSHA256_Crypto(ATChecksumStateSHA256& VDRESTRICT state, const void* src, size_t numBlocks) {
	using namespace nsATChecksum;

	const char* VDRESTRICT src2 = (const char*)src;

	uint32x4_t state0 = vld1q_u32(&state.H[0]);
	uint32x4_t state1 = vld1q_u32(&state.H[4]);

#define DO_ROUND(wk) (void)((w = (wk)),(tmp = lostate),(lostate = vsha256hq_u32(lostate, histate, wk)),(histate = vsha256h2q_u32(histate, tmp, wk)))

	while(numBlocks--) {
		uint32x4_t lostate = state0;
		uint32x4_t histate = state1;
		uint32x4_t tmp;
		uint32x4_t w;

		uint32x4_t v0 = vreinterpretq_u8_u32(vrev32q_u8(vld1q_u8_ex(src2     , 1)));

		uint32x4_t v1 = vreinterpretq_u8_u32(vrev32q_u8(vld1q_u8_ex(src2 + 16, 1)));

		uint32x4_t v2 = vreinterpretq_u8_u32(vrev32q_u8(vld1q_u8_ex(src2 + 32, 1)));
		DO_ROUND(vaddq_u32(v0, vld1q_u32(&K[ 0])));

		uint32x4_t v3 = vreinterpretq_u8_u32(vrev32q_u8(vld1q_u8_ex(src2 + 48, 1)));
		DO_ROUND(vaddq_u32(v1, vld1q_u32(&K[ 4])));

		uint32x4_t v4  = vsha256su1q_u32(vsha256su0q_u32(v0, v1), v2, v3);
		DO_ROUND(vaddq_u32(v2, vld1q_u32(&K[ 8])));

		uint32x4_t v5  = vsha256su1q_u32(vsha256su0q_u32(v1, v2), v3, v4);
		DO_ROUND(vaddq_u32(v3, vld1q_u32(&K[12])));

		uint32x4_t v6  = vsha256su1q_u32(vsha256su0q_u32(v2, v3), v4, v5);
		DO_ROUND(vaddq_u32(v4, vld1q_u32(&K[16])));

		uint32x4_t v7  = vsha256su1q_u32(vsha256su0q_u32(v3, v4), v5, v6);
		DO_ROUND(vaddq_u32(v5, vld1q_u32(&K[20])));

		uint32x4_t v8  = vsha256su1q_u32(vsha256su0q_u32(v4, v5), v6, v7);
		DO_ROUND(vaddq_u32(v6, vld1q_u32(&K[24])));

		uint32x4_t v9  = vsha256su1q_u32(vsha256su0q_u32(v5, v6), v7, v8);
		DO_ROUND(vaddq_u32(v7, vld1q_u32(&K[28])));

		uint32x4_t v10 = vsha256su1q_u32(vsha256su0q_u32(v6, v7), v8, v9);
		DO_ROUND(vaddq_u32(v8, vld1q_u32(&K[32])));

		uint32x4_t v11 = vsha256su1q_u32(vsha256su0q_u32(v7, v8), v9, v10);
		DO_ROUND(vaddq_u32(v9, vld1q_u32(&K[36])));

		uint32x4_t v12 = vsha256su1q_u32(vsha256su0q_u32(v8, v9), v10, v11);
		DO_ROUND(vaddq_u32(v10, vld1q_u32(&K[40])));

		uint32x4_t v13 = vsha256su1q_u32(vsha256su0q_u32(v9, v10), v11, v12);
		DO_ROUND(vaddq_u32(v11, vld1q_u32(&K[44])));

		uint32x4_t v14 = vsha256su1q_u32(vsha256su0q_u32(v10, v11), v12, v13);
		DO_ROUND(vaddq_u32(v12, vld1q_u32(&K[48])));

		uint32x4_t v15 = vsha256su1q_u32(vsha256su0q_u32(v11, v12), v13, v14);
		DO_ROUND(vaddq_u32(v13, vld1q_u32(&K[52])));
		DO_ROUND(vaddq_u32(v14, vld1q_u32(&K[56])));
		DO_ROUND(vaddq_u32(v15, vld1q_u32(&K[60])));

#undef DO_ROUND

		src2 += 64;
		state0 = vaddq_u32(state0, lostate);
		state1 = vaddq_u32(state1, histate);
	}

	vst1q_u32(&state.H[0], state0);
	vst1q_u32(&state.H[4], state1);
}

#else

VD_CPU_TARGET("+sha2")
void ATChecksumUpdateSHA256_Crypto(ATChecksumStateSHA256& VDRESTRICT state, const void* src, size_t numBlocks) {
	using namespace nsATChecksum;

	const unsigned char* VDRESTRICT src2 = (const unsigned char*)src;

	uint32x4_t state0 = vld1q_u32(&state.H[0]);
	uint32x4_t state1 = vld1q_u32(&state.H[4]);

	// VC++ is unable to convert vld1q_u32() into plain LDR and is unable to use
	// indexed loads with it, so we force a plain load instead. We need these constants
	// hoisted into registers as otherwise the load traffic becomes a significant bottleneck
	// in the inner loop (>2x!).
	const uint32x4_t *VDRESTRICT ksrc = (const uint32x4_t *)K;
	uint32x4_t k0  = ksrc[ 0];
	uint32x4_t k1  = ksrc[ 1];
	uint32x4_t k2  = ksrc[ 2];
	uint32x4_t k3  = ksrc[ 3];
	uint32x4_t k4  = ksrc[ 4];
	uint32x4_t k5  = ksrc[ 5];
	uint32x4_t k6  = ksrc[ 6];
	uint32x4_t k7  = ksrc[ 7];
	uint32x4_t k8  = ksrc[ 8];
	uint32x4_t k9  = ksrc[ 9];
	uint32x4_t k10 = ksrc[10];
	uint32x4_t k11 = ksrc[11];
	uint32x4_t k12 = ksrc[12];
	uint32x4_t k13 = ksrc[13];
	uint32x4_t k14 = ksrc[14];
	uint32x4_t k15 = ksrc[15];

#define DO_ROUND(wk) (void)((w = (wk)),(tmp = lostate),(lostate = vsha256hq_u32(lostate, histate, wk)),(histate = vsha256h2q_u32(histate, tmp, wk)))

	while(numBlocks--) {
		uint32x4_t lostate = state0;
		uint32x4_t histate = state1;
		uint32x4_t tmp;
		uint32x4_t w;

		uint32x4_t v0 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(src2     )));

		uint32x4_t v1 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(src2 + 16)));

		uint32x4_t v2 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(src2 + 32)));
		DO_ROUND(vaddq_u32(v0, k0));

		uint32x4_t v3 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(src2 + 48)));
		DO_ROUND(vaddq_u32(v1, k1));

		uint32x4_t v4  = vsha256su1q_u32(vsha256su0q_u32(v0, v1), v2, v3);
		DO_ROUND(vaddq_u32(v2, k2));

		uint32x4_t v5  = vsha256su1q_u32(vsha256su0q_u32(v1, v2), v3, v4);
		DO_ROUND(vaddq_u32(v3, k3));

		uint32x4_t v6  = vsha256su1q_u32(vsha256su0q_u32(v2, v3), v4, v5);
		DO_ROUND(vaddq_u32(v4, k4));

		uint32x4_t v7  = vsha256su1q_u32(vsha256su0q_u32(v3, v4), v5, v6);
		DO_ROUND(vaddq_u32(v5, k5));

		uint32x4_t v8  = vsha256su1q_u32(vsha256su0q_u32(v4, v5), v6, v7);
		DO_ROUND(vaddq_u32(v6, k6));

		uint32x4_t v9  = vsha256su1q_u32(vsha256su0q_u32(v5, v6), v7, v8);
		DO_ROUND(vaddq_u32(v7, k7));

		uint32x4_t v10 = vsha256su1q_u32(vsha256su0q_u32(v6, v7), v8, v9);
		DO_ROUND(vaddq_u32(v8, k8));

		uint32x4_t v11 = vsha256su1q_u32(vsha256su0q_u32(v7, v8), v9, v10);
		DO_ROUND(vaddq_u32(v9, k9));

		uint32x4_t v12 = vsha256su1q_u32(vsha256su0q_u32(v8, v9), v10, v11);
		DO_ROUND(vaddq_u32(v10, k10));

		uint32x4_t v13 = vsha256su1q_u32(vsha256su0q_u32(v9, v10), v11, v12);
		DO_ROUND(vaddq_u32(v11, k11));

		uint32x4_t v14 = vsha256su1q_u32(vsha256su0q_u32(v10, v11), v12, v13);
		DO_ROUND(vaddq_u32(v12, k12));

		uint32x4_t v15 = vsha256su1q_u32(vsha256su0q_u32(v11, v12), v13, v14);
		DO_ROUND(vaddq_u32(v13, k13));
		DO_ROUND(vaddq_u32(v14, k14));
		DO_ROUND(vaddq_u32(v15, k15));

#undef DO_ROUND

		src2 += 64;
		state0 = vaddq_u32(state0, lostate);
		state1 = vaddq_u32(state1, histate);
	}

	vst1q_u32(&state.H[0], state0);
	vst1q_u32(&state.H[4], state1);
}

#endif

void ATChecksumUpdateSHA256_Optimized(ATChecksumStateSHA256& VDRESTRICT state, const void* src, size_t numBlocks) {
	// The NEON version is only about a few percent faster than the scalar implementation, but
	// the Crypto-enabled version is nearly 10x faster. Crypto extensions are technically optional
	// for Windows 10 on ARM according to the ABI, but they're present on the Snapdragon 835 and
	// likely to always be available in practice.
	//
	// Our Crypto-acceleration implementation here is about 2% faster than the system Bcrypt
	// implementation, which is probably just API overhead. Interestingly, the little cores are
	// about 20% faster on a cycles-per-byte basis than the big cores, with the little cores
	// running at 2.12 cpb vs. 2.45 cpb for reasonable sized blocks. Reportedly, the 835 is based
	// on a Kryo 280 CPU core, which has modified Cortex-A53 and Cortex-A73 cores.
	//
	// In both cases, the ARM64 code is much more sensitive to interleaving than the x64 code.
	// Some of this might be poor optimization from MSVC, but mainly it just seems to be that
	// x64 CPUs are way more used to dealing with total crap scheduling-wise.

	if (CPUGetEnabledExtensions() & VDCPUF_SUPPORTS_CRYPTO)
		return ATChecksumUpdateSHA256_Crypto(state, src, numBlocks);
	else
		return ATChecksumUpdateSHA256_NEON(state, src, numBlocks);
}

#endif
