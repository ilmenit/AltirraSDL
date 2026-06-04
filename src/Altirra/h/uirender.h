//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008-2010 Avery Lee
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

#ifndef f_AT_UIRENDER_H
#define f_AT_UIRENDER_H

#include <vd2/system/function.h>
#include <vd2/system/refcount.h>
#include <at/atcore/deviceindicators.h>

struct VDPixmap;
class ATAudioMonitor;
class ATSlightSIDEmulator;
class IVDDisplayCompositor;
class ATUIManager;
struct ATUIAudioStatus;

class IATUIRenderer : public IVDRefCount, public IATDeviceIndicatorManager {
public:
	virtual bool IsVisible() const = 0;
	virtual void SetVisible(bool visible) = 0;

	virtual void SetCyclesPerSecond(double rate) = 0;

	virtual void SetLedStatus(uint8 ledMask) = 0;
	
	virtual void SetHeldButtonStatus(uint8 consolMask) = 0;

	virtual void SetPendingHoldMode(bool enabled) = 0;
	virtual void SetPendingHeldKey(int key) = 0;
	virtual void SetPendingHeldButtons(uint8 consolMask) = 0;

	virtual void ClearWatchedValue(int index) = 0;

	enum class WatchFormat {
		None,
		Dec,
		Hex8,
		Hex16,
		Hex32
	};

	virtual void SetWatchedValue(int index, uint32 value, WatchFormat format) = 0;

	virtual void SetTracingSize(sint64 size) = 0;

	virtual void SetAudioStatus(const ATUIAudioStatus *status) = 0;

	// Per-POKEY audio monitor/display channel index. 0 = primary (P1),
	// 1 = secondary (P2); 2/3 = PokeyMax quad P3/P4. Mono/stereo only use 0/1.
	virtual void SetAudioMonitor(uint32 index, ATAudioMonitor *monitor) = 0;
	virtual void SetAudioDisplayEnabled(uint32 index, bool enable) = 0;
	virtual void SetAudioScopeEnabled(bool enable) = 0;

	virtual void SetSlightSID(ATSlightSIDEmulator *emu) = 0;

	virtual vdrect32 GetPadArea() const = 0;
	virtual void SetPadInputEnabled(bool enable) = 0;

	virtual void SetFpsIndicator(float fps) = 0;

	enum class StatusPriority : int {
		Status,
		Modem,
		Prompt,

		Max = Prompt
	};

	virtual void SetMessage(StatusPriority priority, const wchar_t *msg) = 0;
	virtual void ClearMessage(StatusPriority priority) = 0;

	virtual void SetHoverTip(int px, int py, const wchar_t *text) = 0;

	virtual void SetPaused(bool paused) = 0;

	virtual void SetUIManager(ATUIManager *mgr) = 0;
	virtual void Relayout(int w, int h) = 0;

	virtual void Update() = 0;

	// Return the amount of height being taken up the the indicators at the bottom.
	virtual sint32 GetIndicatorSafeHeight() const = 0;

	virtual void AddIndicatorSafeHeightChangedHandler(const vdfunction<void()> *pfn) = 0;
	virtual void RemoveIndicatorSafeHeightChangedHandler(const vdfunction<void()> *pfn) = 0;

	virtual void BeginCustomization() = 0;
};

void ATCreateUIRenderer(IATUIRenderer **r);

#endif
