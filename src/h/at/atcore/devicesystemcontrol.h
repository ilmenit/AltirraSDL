//	Altirra - Atari 800/800XL/5200 emulator
//	Core library -- System control interfaces
//	Copyright (C) 2009-2017 Avery Lee
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

//=========================================================================
// System control interface
//
// The system control interface allows a device to control some central
// aspects of the CPU, notably the ROM mappings and the CPU. This is used
// to emulate some behaviors of an MMU or a CPU accelerator.
//

#ifndef f_AT_ATCORE_SYSTEMCONTROL_H
#define f_AT_ATCORE_SYSTEMCONTROL_H

#include <vd2/system/function.h>
#include <vd2/system/unknown.h>
#include <at/atcore/notifylist.h>

class ATMemoryLayer;
class IATDeviceSystemControl;
class ATBusSignal;

class IATSystemController {
public:
	// Reset the CPU (and only the CPU).
	virtual void ResetCPU() = 0;

	// Assert /RESET to reset the entire computer, but not peripherals.
	virtual void ResetComputer() = 0;

	// Assert /ABORT on the 65C816.
	virtual void AssertABORT() = 0;

	// Override the CPU mode to 6502/65C816 with the given multiplier and
	// then reset the CPU. The override is automatically removed when the
	// device is removed.
	virtual void OverrideCPUMode(IATDeviceSystemControl *source, bool use816, uint32 multiplier) = 0;

	// Modify the kernel ROM used by the system controller for kernel ROM
	// mappings ($5000-57FF, $C000-CFFF, $D800-FFFF). This is required for
	// hooks to properly update. The override is removed on null or implicitly
	// on device removal. 'highSpeed' is <0 for force off, >0 for force on,
	// and 0 for default.
	virtual void OverrideKernelMapping(IATDeviceSystemControl *source, const void *kernelROM, sint8 highSpeed, bool priority) = 0;

	// Returns true if U1MB is active and in pre-lock state.
	virtual bool IsU1MBConfigPreLocked() = 0;

	// Notify system controller when U1MB has entered system lock state.
	// This is required to handle temporary disabling of some functions
	// when U1MB is in its funky pre-lock state.
	virtual void OnU1MBConfigPreLocked(bool inPreLockState) = 0;

	// Read the Start/Select/Option buttons. Format is the same as CONSOL:
	// bit 0 = start, bit 1 = select, bit 2 = option.
	virtual uint8 ReadConsoleButtons() const = 0;

	// Return bus signal for stereo (dual POKEY) audio enable.
	virtual ATBusSignal& GetStereoEnableSignal() = 0;

	// Return bus signal for Covox enable.
	virtual ATBusSignal& GetCovoxEnableSignal() = 0;
};

class IATDeviceSystemControl {
public:
	enum : uint32 { kTypeID = "dscn"_vdfcctypeid };

	virtual void InitSystemControl(IATSystemController *sysctrl) = 0;
	virtual void SetROMLayers(
		ATMemoryLayer *layerLowerKernelROM,
		ATMemoryLayer *layerUpperKernelROM,
		ATMemoryLayer *layerBASICROM,
		ATMemoryLayer *layerSelfTestROM,
		ATMemoryLayer *layerGameROM,
		const void *kernelROM) = 0;

	virtual void OnU1MBConfigPreLocked(bool inPreLockState) = 0;
};

class IATCovoxController {
public:
	virtual bool IsCovoxEnabled() const = 0;
	virtual ATNotifyList<const vdfunction<void(bool)> *>& GetCovoxEnableNotifyList() = 0;
};

class IATDeviceCovoxControl {
public:
	enum : uint32 { kTypeID = "dscv"_vdfcctypeid };

	virtual void InitCovoxControl(IATCovoxController& controller) = 0;
};

// POKEY channel-count control (PokeyMax). A device drives the core POKEY
// sound-generator count (mono/stereo/quad) so its Mono/Stereo/Quad setting
// produces real second/third/fourth POKEYs with hardware panning
// (P1+P3 -> left, P2+P4 -> right). The core's user "Dual POKEYs" preference is
// restored when the override is cleared.
class IATPokeyChannelController {
public:
	// mode: 0=mono, 1=stereo, 2=quad. owner identifies the requesting device so
	// a stale Clear from a detached device cannot revert a newer owner.
	virtual void SetExternalPokeyChannelMode(void *owner, uint32 mode) = 0;
	virtual void ClearExternalPokeyChannelMode(void *owner) = 0;

	// PokeyMax $D210 MODE bit0 (Saturate): false = linear same-side sum,
	// true = POKEY saturation curve applied to the summed same-side quad
	// channels (P1+P3 / P2+P4). Hardware default is true. Only affects quad
	// mode; ignored in mono/stereo. owner matches the channel-mode owner.
	virtual void SetExternalPokeySaturation(void *owner, bool pokeyCurve) = 0;
};

class IATDevicePokeyChannelControl {
public:
	// "dspk" = device-system-pokey; verified unique (no collision with the
	// sibling IDs dscn/dscv in this header or any other _vdfcctypeid).
	enum : uint32 { kTypeID = "dspk"_vdfcctypeid };

	virtual void InitPokeyChannelControl(IATPokeyChannelController& controller) = 0;
};

#endif
