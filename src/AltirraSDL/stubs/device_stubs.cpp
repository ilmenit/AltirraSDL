//	Altirra SDL3 frontend - Device and hardware stubs
//	Stub implementations for Win32-only hardware devices and
//	IDE/VHD disk images.

#include <stdafx.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/refcount.h>
#include <vd2/system/VDString.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/function.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/device.h>
#include "devicemanager.h"
#include <at/atcore/enumparseimpl.h>
#include "cpuheatmap.h"
#include "directorywatcher.h"
#include "idephysdisk.h"
#include "oshelper.h"

// ATCPUHeatMap — cpuheatmap.cpp is now compiled (no Win32 deps).

// ATCS8900AEmulator now provided by cs8900a.cpp (re-included after the
// audit confirmed it has no Win32 deps). Used by DragonCart and
// MyIDE-II Ethernet adapters via their internal ATEthernetBus.

// ATDirectoryWatcher implementation now lives in
// source/os/directorywatcher_sdl3.cpp (portable polling-based variant
// that mirrors the Win32 RunPollThread fallback path).

// IDE Physical Disk implementation now lives in
// source/os/idephysdisk_sdl3.cpp (POSIX raw block device passthrough,
// read-only). g_ATDeviceDefIDEPhysDisk + ATIDEIsPhysicalDiskPath +
// ATIDEGetPhysicalDiskSize + ATIDEPhysicalDisk all come from there.
//
// idevhdimage.cpp is now compiled directly (no longer stubbed)

// ATCreateModemDriverTCP now provided by source/os/modemtcp_sdl3.cpp
// (POSIX sockets + poll(); replaces the Winsock-only modemtcp.cpp).

// ============================================================
// Device definitions stubs (from excluded files)
// ============================================================
//
// Each of these device factories returns null instead of constructing
// a real device. The user sees the device in the Add Device picker
// but selecting it produces an empty entry. Per-platform follow-up:
//
//   * g_ATDeviceDefPipeSerial — Win32 named pipes for serial
//     bridging. POSIX FIFOs (mkfifo) are the natural mapping.
//
// g_ATDeviceDefCustom now provided by customdevice.cpp.
// g_ATDeviceDefIDEPhysDisk now provided by idephysdisk_sdl3.cpp.
// g_ATDeviceDefIDEVHDImage now provided by idevhdimage.cpp.
// g_ATDeviceDefMidiMate    now provided by midimate_sdl3.cpp.

static void ATCreateDeviceNullStub(const ATPropertySet&, IATDevice **pp) { if (pp) *pp = nullptr; }

extern const ATDeviceDefinition g_ATDeviceDefPipeSerial  = { "pipeser",  nullptr, L"Pipe Serial",   ATCreateDeviceNullStub };


// ATSetProcessEfficiencyMode: no-op on POSIX for now. The P2 plan is a
// per-platform implementation (Windows: SetProcessAffinityMask; macOS:
// pthread_set_qos_class_self_np; Linux/Android: sched_setaffinity keyed
// off /sys/devices/system/cpu/cpu*/cpu_capacity). The enum table itself
// now lives in source/os/oshelper_sdl3.cpp.
enum class ATProcessEfficiencyMode : uint8;  // forward declaration
void ATSetProcessEfficiencyMode(ATProcessEfficiencyMode) {}
