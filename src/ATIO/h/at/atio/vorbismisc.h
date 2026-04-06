//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2023 Avery Lee
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

#ifndef f_AT_ATIO_VORBISMISC_H
#define f_AT_ATIO_VORBISMISC_H

#include <vd2/system/error.h>

class ATVorbisException : public MyError {
public:
	ATVorbisException(const char *s) { assign(s); }
};

extern const float g_ATVorbisInverseDbTable[];

void ATVorbisRenderFloorLine(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, float *dst, uint32_t limit);
void ATVorbisDecoupleChannels(float *magnitudes, float *angles, uint32_t halfBlockSize);
void ATVorbisOverlapAdd(float *dst, float *prev, const float *window, size_t n2);
void ATVorbisDeinterleaveResidue(float *const *dst, const float *__restrict src, size_t n, size_t numChannels);
uint32 ATVorbisComputeCRC(const void *header, size_t headerLen, const void *data, size_t dataLen);
void ATVorbisConvertF32ToS16(sint16 *dst, const float *src, size_t n);
void ATVorbisConvertF32ToS16x2(sint16 *dst, const float *src1, const float *src2, size_t n);
void ATVorbisConvertF32ToS16Rep2(sint16 *dst, const float *src, size_t n);

#endif
