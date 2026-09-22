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

// Windows half of the cooperative lazy-timer drain.
//
// The non-Win32 backends implement VDLazyTimer with a cooperative
// scheduler that only dispatches callbacks when the host loop calls
// VDLazyTimerTick() (src/system/source/time_sdl3.cpp).  The Win32
// backend does not need that: VDLazyTimer::SetOneShotFn/SetPeriodicFn
// go through SetTimer(NULL, 0, delay, TIMERPROC), so WM_TIMER is posted
// to the calling thread's message queue and DispatchMessage() invokes
// the thunk directly.  The SDL3 host loop already pumps that queue, so
// the callbacks fire on the main thread exactly as <vd2/system/time.h>
// promises, with nothing for the host loop to drain.
//
// This no-op exists so the drain is unconditional at every call site --
// main_sdl3.cpp and AltirraBridgeServer/main_bridge.cpp call
// VDLazyTimerTick() once per iteration on every platform, with no
// #ifdef.  Do not "optimise" it away by guarding the call sites: the
// point is that the host loop contract is the same everywhere, and a
// platform that later needs real work here can put it in this file.
//
// This is a fork-owned file; upstream Altirra has no equivalent because
// its Win32 message pump is the event loop.

extern "C" void VDLazyTimerTick() {
}
