//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2022 Avery Lee
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
// Device controller port interface
//
// The device port manager provides access to the joystick ports. Both
// inputs and outputs can be connected.
//
// The PIA interface is similar to the device port interface, except that
// the PIA interface is intended for internal devices. It should be used
// instead of the device port interface when internal access to port B is
// required on XL/XE configurations, which are not accessible through the
// device port manager since port 3/4 don't exist.
//

#ifndef f_AT_ATCORE_DEVICEPORT_H
#define f_AT_ATCORE_DEVICEPORT_H

#include <vd2/system/function.h>
#include <vd2/system/unknown.h>

typedef void (*ATPortOutputFn)(void *data, uint32 outputState);

// Controller port interface
//
// Interface for devices designed to hook up to one of the external
// controller ports in the computer, ports 1-4 for the 400/800 models
// and 1-2 for the XL/XE models. This includes the joystick direction
// lines, joystick trigger line, and paddle inputs.
//
// Each device obtains its own controller port interfaces for the ports
// it is connected to. Behind the scenes, the port manager merges the
// values from all of the port interfaces. While it isn't normally
// expected that more than one device would connect to the same port,
// this is supported to allow for graceful transitions between device
// configurations without requiring strict remove-add ordering of
// overlapping devices.
//
// Direction signals are stored as 4-bit masks using the same bit
// ordering and polarity as the PORTA/PORTB registers. In order, bits
// 0-3 correspond to up, down, left and right joystick inputs, with a
// '0' when that input is active.
//
// The controller port interface is referenced counted and is freed
// by releasing the last reference. While normally the controller ports
// should be freed before the port manager shuts down, the port manager
// will neutralize all outstanding controller port instances at its
// shutdown so that they turn into no-ops.
//
class IATDeviceControllerPort {
protected:
	~IATDeviceControllerPort() = default;

public:
	virtual void AddRef() = 0;
	virtual void Release() = 0;
	
	// Returns true if dirty pot emulation is enabled.
	virtual bool IsPotNoiseEnabled() const = 0;

	// Locally enable or disable the port. A disabled port does not drive any
	// inputs or sense outputs. This is independent of the internal system enable
	// for whether ports 3/4 actually exist.
	virtual void SetEnabled(bool enabled) = 0;

	// Change the mask of direction bits to monitor for changes in output.
	virtual void SetDirOutputMask(uint8 mask) = 0;

	// Change the direction signals sent into the controller port. These
	// should normally be high (1) unless driving the lines to a 0 is desired.
	virtual void SetDirInput(uint8 mask) = 0;

	// Change only the specified bits of the direction signals sent into the
	// controller port.
	virtual void SetDirInputBits(uint8 newSignals, uint8 changeMask) = 0;

	// Set the change mask and handler for monitoring changes in direction
	// lines. The handler is called whenever any of the bits in the mask
	// are changed. If callNow is set, the handler is also called immediately
	// to simplify initial processing.
	virtual void SetOnDirOutputChanged(uint8 mask, vdfunction<void()> handler, bool callNow) = 0;

	// Retrieves the current values of direction lines. Only the bits set
	// in the output mask are guaranteed to be valid.
	virtual uint8 GetCurrentDirOutput() const = 0;

	// Sets whether the joystick trigger is held down.
	virtual void SetTriggerDown(bool down) = 0;

	// Optional input capture hook. Real hardware ports ignore keyboard
	// state; a frontend proxy uses this for redirected 5200 controllers.
	virtual void Set5200KeyboardState(const bool matrix[64], bool top) {}

	// Reset a pot input as if nothing were connected.
	virtual void ResetPotPosition(bool potB) = 0;

	// Set pot position in terms of the value returned from POT0-7. Valid values
	// are 1-228 with out of range values being clamped.
	virtual void SetPotPosition(bool potB, sint32 pos) = 0;

	// Set pot position in terms of the value returned from POT0-7, scaled by 64K (1<<16).
	// Value values are (1<<16) - (228<<16) with out of range values being clamped. This
	// is used to provide higher resolution needed when fast pot scan is enabled. Note that
	// the value is still in units of the slow pot scan, and are scaled up by 114 when
	// fast pots are enabled.
	//
	// When grounded=true, the pot lines are grounded instead of simply not providing any
	// current. This is equivalent to a super-maximum (>>228) position and hiPos is
	// ignored. The difference between grounded state and position 228 is that in fast pot
	// mode or too frequent slow scan where the pot capacitors have not been discharged,
	// pos 228 will leave the signal above threshold while a grounded line will pull it
	// back below threshold, which is reflected in ALLPOT and the POT counters.
	virtual void SetPotHiresPosition(bool potB, sint32 hiPos, bool grounded) = 0;

	// Return the number of cycles until the given beam position. The primary use for this
	// is for light pen/gun timing.
	//
	// Normal ranges are [0,114) for xcyc and [0,262/312) for y. Values outside of the
	// range can be given and will be wrapped appropriately; for instance, -1 for X will
	// be translated to 113 on the previous line, and -1 for Y will be wrapped around to
	// the last line. The returned delay is always [1,N] where N is the number of cycles
	// in the frame, with zero never being returned.
	virtual uint32 GetCyclesToBeamPosition(int xcyc, int y) const = 0;
};

class IATDevicePortManager {
public:
	static constexpr uint32 kTypeID = "IATDevicePortManager"_vdtypeid;

	static constexpr uint32 kMask_PortA = 0x00FF;
	static constexpr uint32 kMask_PortB = 0xFF00;

	virtual void AllocControllerPort(int controllerIndex, IATDeviceControllerPort **port) = 0;

	// Allocate a new input to supply signals to the PIA. Returns an input index
	// or -1 if no inputs are available.
	virtual int AllocInput() = 0;

	// Free an input from AllocInput(). Silently ignored for invalid index (-1).
	virtual void FreeInput(int index) = 0;

	// Change the signals supplied to the PIA by an input. Redundant sets are
	// tossed and the call is silently ignored for an invalid index (-1).
	virtual void SetInput(int index, uint32 rval) = 0;

	// Get the current outputs from the PIA.
	virtual uint32 GetOutputState() const = 0;

	// Allocate a new output from the PIA. The output function is called
	// whenever a relevant change occurs, according to the supplied change
	// mask. The function is not called initially, so self-init must occur.
	// -1 is returned if no more output slots are available.
	virtual int AllocOutput(ATPortOutputFn fn, void *ptr, uint32 changeMask) = 0;

	// Modify the change mask used to filter output change notifications to an
	// output.
	virtual void ModifyOutputMask(int index, uint32 changeMask) = 0;

	// Free an output allocated by AllocOutput(). It is OK to call this with
	// the invalid output ID (-1).
	virtual void FreeOutput(int index) = 0;
};

#endif
