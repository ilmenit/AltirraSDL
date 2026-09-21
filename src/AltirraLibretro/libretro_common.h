#ifndef ALTIRRA_LIBRETRO_COMMON_H
#define ALTIRRA_LIBRETRO_COMMON_H

#include <stdint.h>

static constexpr uint32_t kLibretroSampleRate = 48000;

// Drains the cooperative VDLazyTimer scheduler in libretro_time.cpp.
// Called once per retro_run; see the comment block in that file.  On
// Windows the core uses libsystem.a's Win32 timer, which dispatches via
// WM_TIMER by itself, so this is a no-op there (libretro_time_win32.cpp).
void ATLibretroLazyTimerTick();

#endif
