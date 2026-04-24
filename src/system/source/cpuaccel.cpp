//	VirtualDub - Video processing and capture application
//	System library component
//	Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
//
//	Beginning with 1.6.0, the VirtualDub system library is licensed
//	differently than the remainder of VirtualDub.  This particular file is
//	thus licensed as follows (the "zlib" license):
//
//	This software is provided 'as-is', without any express or implied
//	warranty.  In no event will the authors be held liable for any
//	damages arising from the use of this software.
//
//	Permission is granted to anyone to use this software for any purpose,
//	including commercial applications, and to alter it and redistribute it
//	freely, subject to the following restrictions:
//
//	1.	The origin of this software must not be misrepresented; you must
//		not claim that you wrote the original software. If you use this
//		software in a product, an acknowledgment in the product
//		documentation would be appreciated but is not required.
//	2.	Altered source versions must be plainly marked as such, and must
//		not be misrepresented as being the original software.
//	3.	This notice may not be removed or altered from any source
//		distribution.

// AltirraSDL: this file extends upstream cpuaccel.cpp with ARM64 feature
// detection on Linux (sys/auxv.h, HWCAP_AES / HWCAP_CRC32) and macOS
// (sysctlbyname hw.optional.arm.FEAT_AES / armv8_crc32). Upstream test9
// adds a `vdcpuid` macro to abstract the GCC <cpuid.h> __cpuid signature
// difference; the fork already solves the same problem in
// vd2/system/win32/intrin.h by redefining __cpuid itself to forward to
// __cpuid_count, so call sites in this file work unchanged on MSVC,
// Clang and GCC. Keep both: the fork's intrin.h shim plus upstream's
// existing CPU-feature loop, with the ARM64 platform additions below.
#include <stdafx.h>
#ifdef _WIN32
#include <wtypes.h>
#include <winnt.h>
#elif VD_CPU_ARM64
#if defined(__APPLE__)
#include <sys/sysctl.h>
#else
#include <sys/auxv.h>
#include <asm/hwcap.h>
#endif
#endif
#include <vd2/system/win32/intrin.h>
#include <vd2/system/cpuaccel.h>

long g_lCPUExtensionsEnabled;

extern "C" {
#if VD_CPU_X86 || VD_CPU_X64
	// These x86 SIMD flags are only meaningful on x86/x64.  On other
	// architectures (ARM64 NEON, WebAssembly scalar), the corresponding
	// header (cpuaccel.h) provides compile-time-constant stubs in the
	// global namespace so dependent code compiles and dead-code-
	// eliminates cleanly.
	bool FPU_enabled, MMX_enabled, ISSE_enabled, SSE2_enabled;
#endif
};

#if VD_CPU_X86 || VD_CPU_X64
// This code used to use IsProcessorFeaturePresent(), but this function is somewhat
// suboptimal in Win64 -- for one thing, it doesn't return true for MMX, at least
// on Vista 64.
long CPUCheckForExtensions() {
	// check for features register
	long flags = 0;

	int cpuInfo[4];
	__cpuid(cpuInfo, 0);
	if (cpuInfo[0] == 0)
		return flags;

	__cpuid(cpuInfo, 1);

	if (cpuInfo[3] & (1 << 23))
		flags |= CPUF_SUPPORTS_MMX;

	if (cpuInfo[3] & (1 << 25)) {
		// Check if SSE is actually supported.
		bool sseSupported = true;

#ifdef _M_IX86
		__try {
			__asm andps xmm0,xmm0
		} __except(EXCEPTION_EXECUTE_HANDLER) {
			if (_exception_code() == STATUS_ILLEGAL_INSTRUCTION)
				sseSupported = false;
		}
#endif
		
		if (sseSupported) {
			flags |= CPUF_SUPPORTS_SSE | CPUF_SUPPORTS_INTEGER_SSE;

			// EDX[26] = SSE2
			if (cpuInfo[3] & (1 << 26))
				flags |= CPUF_SUPPORTS_SSE2;

			// ECX[0] = SSE3
			if (cpuInfo[2] & 0x00000001)
				flags |= CPUF_SUPPORTS_SSE3;

			// ECX[9] = SSSE3
			if (cpuInfo[2] & 0x00000200)
				flags |= CPUF_SUPPORTS_SSSE3;

			// ECX{12] = FMA
			if (cpuInfo[2] & 0x00001000)
				flags |= VDCPUF_SUPPORTS_FMA;

			// ECX[19] = SSE4.1
			if (cpuInfo[2] & 0x00080000) {
				flags |= CPUF_SUPPORTS_SSE41;

				// ECX[20] = SSE4.2
				// ECX[1] = CLMUL/PCMULQDQ
				//
				// The oldest CPUs that support CLMUL are Intel Pentium 4 Westmere,
				// AMD Bulldozer, and AMD Jaguar. Westmere supports SSE4.2,
				// Bulldozer/Jaguar support AVX. 
				//
				if (cpuInfo[2] & (1 << 20)) {
					flags |= CPUF_SUPPORTS_SSE42;

					if (cpuInfo[2] & (1 << 1))
						flags |= CPUF_SUPPORTS_CLMUL;
				}
			}

			if (cpuInfo[2] & (1 << 23)) {
				flags |= VDCPUF_SUPPORTS_POPCNT;
			}

			// check OSXSAVE and AVX bits
			if ((cpuInfo[2] & ((1 << 27) | (1 << 28))) == ((1 << 27) | (1 << 28))) {
				auto xgetbv0 = []() VD_CPU_TARGET_LAMBDA("xsave") { return (uint32)_xgetbv(0); };
				if ((xgetbv0() & 0x06) == 0x06) {
					flags |= CPUF_SUPPORTS_AVX;

					if (cpuInfo[0] >= 7) {
						int cpuInfo7_0[4];

						__cpuidex(cpuInfo7_0, 7, 0);

						// EBX[3] = BMI1
						// EBX[5] = AVX2
						// EBX[8] = BMI2
						static constexpr uint32 BMI1 = UINT32_C(1) << 3;
						static constexpr uint32 AVX2 = UINT32_C(1) << 5;
						static constexpr uint32 BMI2 = UINT32_C(1) << 8;
						if ((cpuInfo7_0[1] & (AVX2 | BMI1 | BMI2)) == (AVX2 | BMI1 | BMI2)) {
							flags |= CPUF_SUPPORTS_AVX2;

							// EBX[5] = SHA
							if (cpuInfo7_0[1] & (1 << 29)) {
								flags |= CPUF_SUPPORTS_SHA;
							}
						}
					}
				}
			}
		}
	}

	// check for AMD extensions
	__cpuid(cpuInfo, 0x80000000);
	if ((unsigned)cpuInfo[0] >= 0x80000001U) {
		__cpuid(cpuInfo, 0x80000001);

		if (cpuInfo[3] & (1 << 22))
			flags |= CPUF_SUPPORTS_INTEGER_SSE;

		// ECX[5] = LZCNT/ABM
		if (cpuInfo[2] & (1 << 5))
			flags |= CPUF_SUPPORTS_LZCNT;
	}

	return flags;
}
#elif VD_CPU_ARM64
long CPUCheckForExtensions() {
	long flags = 0;

#ifdef _WIN32
	if (IsProcessorFeaturePresent(PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE))
		flags |= VDCPUF_SUPPORTS_CRYPTO;

	if (IsProcessorFeaturePresent(PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE))
		flags |= VDCPUF_SUPPORTS_CRC32;
#elif defined(__APPLE__)
	// macOS ARM64: use sysctlbyname to query CPU capabilities
	{
		int val = 0;
		size_t len = sizeof(val);
		if (sysctlbyname("hw.optional.arm.FEAT_AES", &val, &len, nullptr, 0) == 0 && val)
			flags |= VDCPUF_SUPPORTS_CRYPTO;
		val = 0; len = sizeof(val);
		if (sysctlbyname("hw.optional.armv8_crc32", &val, &len, nullptr, 0) == 0 && val)
			flags |= VDCPUF_SUPPORTS_CRC32;
	}
#else
	// Linux ARM64: use getauxval
	unsigned long hwcap = getauxval(AT_HWCAP);

	if (hwcap & HWCAP_AES)
		flags |= VDCPUF_SUPPORTS_CRYPTO;

	if (hwcap & HWCAP_CRC32)
		flags |= VDCPUF_SUPPORTS_CRC32;
#endif

	return flags;
}
#else
long CPUCheckForExtensions() {
	return 0;
}
#endif

long CPUEnableExtensions(long lEnableFlags) {
	g_lCPUExtensionsEnabled = lEnableFlags;

#if VD_CPU_X86 || VD_CPU_X64
	MMX_enabled = !!(g_lCPUExtensionsEnabled & CPUF_SUPPORTS_MMX);
	ISSE_enabled = !!(g_lCPUExtensionsEnabled & CPUF_SUPPORTS_INTEGER_SSE);
	SSE2_enabled = !!(g_lCPUExtensionsEnabled & CPUF_SUPPORTS_SSE2);
#endif

	return g_lCPUExtensionsEnabled;
}

void VDCPUCleanupExtensions() {
#if defined(VD_CPU_X86) || defined(VD_CPU_AMD64)
	#if defined(VD_CPU_X86)
		if (ISSE_enabled)
			_mm_sfence();

		if (MMX_enabled)
			_mm_empty();
	#elif defined(VD_CPU_AMD64)
		_mm_sfence();
	#endif

	if (g_lCPUExtensionsEnabled & CPUF_SUPPORTS_AVX) {
#if defined(VD_COMPILER_CLANG_OR_GCC)
		[] VD_CPU_TARGET_LAMBDA("avx") {
#endif
		_mm256_zeroupper();
#if defined(VD_COMPILER_CLANG_OR_GCC)
		}();
#endif
	}
#endif
}
