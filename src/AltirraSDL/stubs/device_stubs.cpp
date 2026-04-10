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
#include "cs8900a.h"
#include "directorywatcher.h"
#include "idephysdisk.h"
#include "modem.h"
#include "oshelper.h"

// ATCPUHeatMap — cpuheatmap.cpp is now compiled (no Win32 deps).

// ============================================================
// ATCS8900AEmulator stubs (cs8900a.cpp excluded)
// ============================================================

ATCS8900AEmulator::ATCS8900AEmulator() : mReceiveReadPtr(0), mReceiveReadLen(0),
	mTransmitWritePtr(0), mTransmitWriteLevel(0), mTransmitWriteLen(0),
	mPacketPtr(0), mPacketPtrAutoInc(0), mTransmitState(kTransmitState_WaitForCmd),
	mReceiveState(kReceiveState_WaitForFrame), mTransmitBufferReadPtr(0),
	mTransmitBufferWritePtr(0), mTransmitBufferLevel(0), mReceiveBufferReadPtr(0),
	mReceiveBufferWritePtr(0), mReceiveBufferLevel(0), mpEthernetSegment(nullptr),
	mEthernetEndpointId(0), mEthernetClockIndex(0) {}
ATCS8900AEmulator::~ATCS8900AEmulator() {}
void ATCS8900AEmulator::Init(IATEthernetSegment*, uint32) {}
void ATCS8900AEmulator::Shutdown() {}
void ATCS8900AEmulator::ColdReset() {}
void ATCS8900AEmulator::WarmReset() {}
uint8 ATCS8900AEmulator::ReadByte(uint8) { return 0xFF; }
uint8 ATCS8900AEmulator::DebugReadByte(uint8) { return 0xFF; }
void ATCS8900AEmulator::WriteByte(uint8, uint8) {}
void ATCS8900AEmulator::ReceiveFrame(const ATEthernetPacket&, ATEthernetFrameDecodedType, const void *) {}

// ============================================================
// ATDirectoryWatcher stubs (directorywatcher.cpp excluded)
// ============================================================

bool ATDirectoryWatcher::sbShouldUsePolling = false;
void ATDirectoryWatcher::SetShouldUsePolling(bool enabled) { sbShouldUsePolling = enabled; }
ATDirectoryWatcher::ATDirectoryWatcher() : mhDir(nullptr), mhExitEvent(nullptr),
	mhDirChangeEvent(nullptr), mpChangeBuffer(nullptr), mChangeBufferSize(0),
	mbRecursive(false), mbAllChanged(false) {}
ATDirectoryWatcher::~ATDirectoryWatcher() {}
void ATDirectoryWatcher::Init(const wchar_t*, bool) {}
void ATDirectoryWatcher::Shutdown() {}
bool ATDirectoryWatcher::CheckForChanges() { return false; }
bool ATDirectoryWatcher::CheckForChanges(vdfastvector<wchar_t>&) { return false; }
void ATDirectoryWatcher::ThreadRun() {}

// ============================================================
// IDE Physical Disk stubs (idephysdisk.cpp excluded)
// ============================================================

bool ATIDEIsPhysicalDiskPath(const wchar_t*) { return false; }
sint64 ATIDEGetPhysicalDiskSize(const wchar_t*) { return 0; }
ATIDEPhysicalDisk::ATIDEPhysicalDisk() : mhDisk(nullptr), mpBuffer(nullptr), mSectorCount(0) {}
ATIDEPhysicalDisk::~ATIDEPhysicalDisk() {}
int ATIDEPhysicalDisk::AddRef() { return ++mRefCount; }
int ATIDEPhysicalDisk::Release() { int n = --mRefCount; if (!n) delete this; return n; }
void *ATIDEPhysicalDisk::AsInterface(uint32) { return nullptr; }
void ATIDEPhysicalDisk::GetDeviceInfo(ATDeviceInfo&) {}
void ATIDEPhysicalDisk::GetSettings(ATPropertySet&) {}
bool ATIDEPhysicalDisk::SetSettings(const ATPropertySet&) { return false; }
void ATIDEPhysicalDisk::Shutdown() {}
ATBlockDeviceGeometry ATIDEPhysicalDisk::GetGeometry() const { return {}; }
uint32 ATIDEPhysicalDisk::GetSerialNumber() const { return 0; }
void ATIDEPhysicalDisk::Init(const wchar_t*) {}
void ATIDEPhysicalDisk::Flush() {}
void ATIDEPhysicalDisk::ReadSectors(void*, uint32, uint32) {}
void ATIDEPhysicalDisk::WriteSectors(const void*, uint32, uint32) {}

// idevhdimage.cpp is now compiled directly (no longer stubbed)

// ============================================================
// Modem TCP driver stub (modemtcp.cpp excluded)
// ============================================================

IATModemDriver *ATCreateModemDriverTCP() { return nullptr; }

// ============================================================
// Device definitions stubs (from excluded files)
// ============================================================

static void ATCreateDeviceNullStub(const ATPropertySet&, IATDevice **pp) { if (pp) *pp = nullptr; }

extern const ATDeviceDefinition g_ATDeviceDefCustom      = { "custom",   nullptr, L"Custom",        ATCreateDeviceNullStub };
extern const ATDeviceDefinition g_ATDeviceDefIDEPhysDisk = { "idephys",  nullptr, L"IDE Phys Disk", ATCreateDeviceNullStub };
// g_ATDeviceDefIDEVHDImage now provided by idevhdimage.cpp
extern const ATDeviceDefinition g_ATDeviceDefMidiMate    = { "midimate", nullptr, L"MidiMate",      ATCreateDeviceNullStub };
extern const ATDeviceDefinition g_ATDeviceDefPipeSerial  = { "pipeser",  nullptr, L"Pipe Serial",   ATCreateDeviceNullStub };


// ATSetProcessEfficiencyMode: no-op on POSIX for now. The P2 plan is a
// per-platform implementation (Windows: SetProcessAffinityMask; macOS:
// pthread_set_qos_class_self_np; Linux/Android: sched_setaffinity keyed
// off /sys/devices/system/cpu/cpu*/cpu_capacity). The enum table itself
// now lives in source/os/oshelper_sdl3.cpp.
enum class ATProcessEfficiencyMode : uint8;  // forward declaration
void ATSetProcessEfficiencyMode(ATProcessEfficiencyMode) {}
