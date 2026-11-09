//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2014 Avery Lee
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

#ifndef f_AT_ATCORE_DEVICE_H
#define f_AT_ATCORE_DEVICE_H

#include <vd2/system/unknown.h>
#include <vd2/system/VDString.h>
#include <vd2/system/vdstl.h>

class IVDStream;
class ATMemoryManager;
class ATFirmwareManager;
class ATIRQController;
class ATScheduler;
class ATPropertySet;
class IATDevice;
class IATDeviceParent;
class IATUIRenderer;
class IATAudioMixer;
class ATConsoleOutput;
class ATPIAEmulator;
class IATDeviceCartridge;
class IATDeviceIndicatorManager;
struct ATTraceContext;
class IATDeviceManager;

typedef void (*ATDeviceFactoryFn)(const ATPropertySet& pset, IATDevice **);

enum ATDeviceDefFlag : uint32 {
	// Suggest a reboot on device plug/unplug. Note that a reboot is _not_ required; device
	// implementations must support continued execution without rebooting, though the device
	// may be in a useless state.
	kATDeviceDefFlag_RebootOnPlug = 0x00000001,

	// An internal device is automatically added by the emulator and cannot be removed by
	// the user. It is still configurable and persistable unless the Hidden flag is set.
	kATDeviceDefFlag_Internal = 0x00000002,

	// A hidden device is not visible in the device tree.
	kATDeviceDefFlag_Hidden = 0x00000004,
};

/// Describes static information about the device type. This is constant for all
/// instances of a device.
struct ATDeviceDefinition {
	const char *mpTag;					// Internal/serializable name for device.
	const char *mpConfigTag;			// Optional configuration type for device (null = no config).
	const wchar_t *mpName;				// Readable name of device.
	ATDeviceFactoryFn mpFactoryFn;		// Factory for creating device.
	uint32 mFlags;						// ATDeviceDefFlag*
	// Space-delimited compatible bus types, or null for a root device.
	const char *mpChildTypes;

	bool SupportsChildType(const char *type) const;
};

/// Dynamic information about a device instance (not a device type).
struct ATDeviceInfo {
	const ATDeviceDefinition *mpDef;
};

class IATDeviceMemMap {
public:
	enum { kTypeID = "admm"_vdfcctypeid };

	virtual void InitMemMap(ATMemoryManager *memmap) = 0;

	/// Enumerates memory regions used by the device in the form [lo, hi] for
	/// indices starting from 0. Returns false if there are no more regions.
	/// For instance, [0xD500, 0xD5FF] indicates full use of the CCTL region.
	///
	/// This routine should indicate what regions may be used, not what regions
	/// are currently in use. This is used by the HLE system to try to determine
	/// what regions are safe to use for callbacks. In particular, this routine
	/// needs to report overlaps in the $D1xx and $D500-D7FF regions.
	///
	virtual bool GetMappedRange(uint32 index, uint32& lo, uint32& hi) const = 0;
};

enum class ATDeviceFirmwareStatus : uint8 {
	OK,
	Missing,
	Invalid
};

class IATDeviceFirmware {
public:
	enum { kTypeID = "adfw"_vdfcctypeid };

	virtual void InitFirmware(ATFirmwareManager *fwman) = 0;

	// Reload firmware from firmware manager. Returns true if firmware was actually
	// changed.
	virtual bool ReloadFirmware() = 0;

	virtual const wchar_t *GetWritableFirmwareDesc(uint32 idx) const = 0;
	virtual bool IsWritableFirmwareDirty(uint32 idx) const = 0;
	virtual void SaveWritableFirmware(uint32 idx, IVDStream& stream) = 0;

	virtual ATDeviceFirmwareStatus GetFirmwareStatus() const = 0;
};

class IATDeviceIRQSource {
public:
	enum { kTypeID = "adir"_vdfcctypeid };

	virtual void InitIRQSource(ATIRQController *irqc) = 0;
};

class IATDeviceSchedulingService {
public:
	static const uint32 kTypeID = "IATDeviceSchedulingService"_vdtypeid;

	virtual ATScheduler *GetMachineScheduler() const = 0;
	virtual ATScheduler *GetSlowScheduler() const = 0;
};

class IATDeviceScheduling {
public:
	enum { kTypeID = "adsc"_vdfcctypeid };

	// The first scheduler is the fast scheduler, which runs at cycle granularity
	// and provides a common timebase for the emulator.
	//
	// The second scheduler is the slow scheduler. It runs at scanline rate (114
	// cycles) and has undefined phase relative to the first scheduler. It's designed
	// for tasks that can afford to run with a small amount of jitter and longer
	// delays, to take load off the fast scheduler (which has to deal with a lot
	// of REALLY high speed events).
	//
	virtual void InitScheduling(ATScheduler *sch, ATScheduler *slowsch) = 0;
};

enum ATDeviceButton : uint32 {
	kATDeviceButton_BlackBoxDumpScreen,
	kATDeviceButton_BlackBoxMenu,
	kATDeviceButton_CartridgeResetBank,
	kATDeviceButton_CartridgeSwitch,
	kATDeviceButton_IDEPlus2SwitchDisks,
	kATDeviceButton_IDEPlus2WriteProtect,
	kATDeviceButton_IDEPlus2SDX,
	kATDeviceButton_IndusGTTrack,
	kATDeviceButton_IndusGTId,
	kATDeviceButton_IndusGTError,
	kATDeviceButton_IndusGTBootCPM,
	kATDeviceButton_IndusGTChangeDensity,
	kATDeviceButton_HappySlow,
	kATDeviceButton_HappyWPEnable,
	kATDeviceButton_HappyWPDisable,
	kATDeviceButton_ATR8000Reset,
	kATDeviceButton_XELCFSwap,
	kATDeviceButton_CloseFiles,
	kATDeviceButton_CloseNetworkConnections
};

class IATDeviceButtons {
public:
	enum { kTypeID = "adbt"_vdfcctypeid };

	virtual uint32 GetSupportedButtons() const = 0;
	virtual bool IsButtonDepressed(ATDeviceButton idx) const = 0;
	virtual void ActivateButton(ATDeviceButton idx, bool state) = 0;
};

class IATDeviceIndicators {
public:
	enum { kTypeID = "adin"_vdfcctypeid };

	virtual void InitIndicators(IATDeviceIndicatorManager *r) = 0;
};

class IATDeviceAudioOutput {
public:
	enum { kTypeID = "adao"_vdfcctypeid };

	virtual void InitAudioOutput(IATAudioMixer *output) = 0;
};

class IATDeviceDiagnostics {
public:
	enum { kTypeID = "addd"_vdfcctypeid };

	virtual void DumpStatus(ATConsoleOutput& output) = 0;
};

struct ATDeviceSystemInfo {
	// true if computer has joystick ports 3/4
	bool mbHasPort34 : 1 = false;

	// true if computer produces +12V on SIO port
	bool mbHasSIO12V : 1 = false;

	// true if computer has 9V AC power input
	bool mbHas9VACPower : 1 = false;
};

class IATDeviceSystemInfo {
public:
	static inline constexpr auto kTypeID = "IATDeviceSystemInfo"_vdtypeid;

	virtual const ATDeviceSystemInfo& GetSystemInfo() const = 0;
};

class IATDeviceNativePathMapping {
public:
	static constexpr auto kTypeID = "IATDeviceNativePathMapping"_vdtypeid;

	// Return true if this device can currently support path mapping. This is
	// for devices that may not support path mapping temporarily due to settings,
	// to work around restrictions on adding and removing interfaces dynamically.
	virtual bool CanSupportPathMapping() const = 0;

	// Attempt to map a native OS path to a CIO path through a host file mapping
	// device.
	virtual bool MapNativePathToCIOPath(VDStringA& cioPath, const wchar_t *nativePath) const = 0;
};

class IATDevice : public IVDRefUnknown {
public:
	enum { kTypeID = "adev"_vdfcctypeid };

	virtual void SetManager(IATDeviceManager *devMgr) = 0;
	virtual IATDeviceParent *GetParent() = 0;
	virtual sint32 GetParentBusId() = 0;
	virtual void SetParent(IATDeviceParent *parent, sint32 busId) = 0;
	virtual void GetDeviceInfo(ATDeviceInfo& info) = 0;
	virtual void GetSettingsBlurb(VDStringW& buf) = 0;
	virtual void GetSettings(ATPropertySet& settings) = 0;
	virtual bool SetSettings(const ATPropertySet& settings) = 0;
	virtual void Init() = 0;
	virtual void Shutdown() = 0;

	// Return recommended power-on delay, in tenths of seconds. If the power-on delay is
	// set to 'auto', the simulator will wait the maximum of all device recommended
	// delays.
	virtual uint32 GetComputerPowerOnDelay() const = 0;

	virtual void WarmReset() = 0;
	virtual void ColdReset() = 0;
	virtual void ComputerColdReset() = 0;
	virtual void PeripheralColdReset() = 0;

	virtual void SetTraceContext(ATTraceContext *context) = 0;

	virtual bool GetErrorStatus(uint32 idx, VDStringW& error) = 0;

	// Return true if this device has no state to save or restore in a save state, and
	// thus is OK for save states even if it doesn't implement the save state interface.
	virtual bool IsSaveStateAgnostic() const = 0;
};

class IATDeviceManager {
public:
	virtual void *GetService(uint32 iid) = 0;

	template<typename T>
	T *GetService() {
		return (T *)GetService(T::kTypeID);
	}

	virtual void NotifyDeviceStatusChanged(IATDevice& dev) = 0;
};

#endif
