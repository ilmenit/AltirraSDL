//	Altirra - Atari 800/800XL/5200 emulator
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

// Windows half of the libretro lazy-timer drain.
//
// libretro_time.cpp provides a thread-based VDLazyTimer that shadows the
// SDL3 one in libsystem.a so the standalone core does not pull in SDL3;
// it is compiled only on non-Windows (see CMakeLists.txt).  On Windows
// the core reuses libsystem.a's self-contained Win32 timer (time.cpp),
// whose VDLazyTimer dispatches through WM_TIMER by itself, so there is
// nothing for retro_run() to drain.
//
// This no-op keeps the ATLibretroLazyTimerTick() call in retro_run
// unconditional, matching the host-loop contract the other platforms
// follow.  See the comment in src/system/source/time_lazytick_win32.cpp.

#include "libretro_common.h"

void ATLibretroLazyTimerTick() {
}
