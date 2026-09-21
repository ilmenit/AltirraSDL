#ifndef ALTIRRA_LIBRETRO_COMMON_H
#define ALTIRRA_LIBRETRO_COMMON_H

#include <stdint.h>

static constexpr uint32_t kLibretroSampleRate = 48000;

// Drains the cooperative VDLazyTimer scheduler in libretro_time.cpp.
// Called once per retro_run; see the comment block in that file.
void ATLibretroLazyTimerTick();

#endif
