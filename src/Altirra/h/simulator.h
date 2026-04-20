//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2014 Avery Lee
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

#ifndef f_AT_SIMULATOR_H
#define f_AT_SIMULATOR_H

#include <vd2/system/vdstl.h>
#include <at/atcore/scheduler.h>
#include <at/ataudio/pokey.h>
#include "cpu.h"
#include "cpumemory.h"
#include "memorymanager.h"
#include "antic.h"
#include "gtia.h"
#include "pia.h"
#include "simeventmanager.h"
#include "constants.h"

enum ATAddressSpace : uint32;
enum ATCartridgePriority : uint8;
enum ATImageType : uint8;

struct ATCartLoadContext;
class ATMemoryManager;
class ATMMUEmulator;
class ATPBIManager;
class ATSIOManager;
class ATUltimate1MBEmulator;
class ATHLEBasicLoader;
class ATHLEProgramLoader;
class IATHLEFPAccelerator;
class ATHLEFastBootHook;
class IATHLECIOHook;
class ATAudioSamplePool;
class ATAudioSamplePlayer;
class ATFirmwareManager;
class ATDeviceManager;
class IATDeviceSIOManager;
class IATDeviceCIOManager;
class IATPrinterOutput;
class IATDebugTarget;
class IATDevice;
class IATDeviceSnapshot;
struct ATCPUHookInitNode;
class ATDiskInterface;
class ATDiskEmulator;
class IVDRandomAccessStream;
class IATImage;
class ATTraceCollection;
struct ATTraceSettings;
struct ATNativeTraceSettings;
class IATAutoSaveManager;
class IATPrinterOutputManager;

enum ATMediaWriteMode : uint8;
struct ATMediaLoadContext;

class ATSaveStateReader;
class ATCassetteEmulator;
class IATJoystickManager;
class ATCartridgeEmulator;
class ATInputManager;
class ATVBXEEmulator;
class ATCPUProfiler;
class ATCPUVerifier;
class ATCPUHeatMap;
class IATAudioOutput;
class ATLightPenPort;
class ATCheatEngine;
class ATMMUEmulator;
class ATAudioMonitor;
class IATVirtualScreenHandler;
struct ATCPUTimestampDecoder;
struct ATStateLoadContext;
struct ATImageLoadContext;
class IATBlobImage;
class IATCartridgeImage;
struct ATTraceContext;

class IATSerializable;
struct ATSnapshotStatus;

enum ATCPUMode : uint8;

struct ATMemoryWriteLogEntry {
	uint32 mCycle;
	uint16 mAddress;
	uint8 mValue;
};

class IATRegisterWriteLogger {
public:
	virtual void LogRegisterWrites(vdspan<const ATMemoryWriteLogEntry> entries) = 0;
};

class ATSimulator final : ATCPUEmulatorCallbacks,
					ATAnticEmulatorConnections,
					IATPokeyEmulatorConnections,
					IATGTIAEmulatorConnections
{
public:
	ATSimulator();
	~ATSimulator();

	void Init();
	void Shutdown();

	void SetJoystickManager(IATJoystickManager *jm);

	void SetFrontEnd(IATDeviceSnapshot *dev);

	// Reload any currently used ROM images. Returns true if an actively used ROM image was
	// changed.
	bool LoadROMs();

	void PostInterruptingEvent(ATSimulatorEvent ev);
	void NotifyEvent(ATSimulatorEvent ev);

	bool IsPowered() const {
		return mbPowered;
	}

	bool IsRunning() const {
		return mbRunning;
	}

	bool IsPaused() const {
		return mbPaused;
	}

	ATSimulatorEventManager *GetEventManager() { return mpSimEventManager; }
	ATCPUEmulator& GetCPU() { return mCPU; }
	ATCPUEmulatorMemory& GetCPUMemory() { return *mpMemMan; }
	IATDebugTarget *GetDebugTarget() { return mpDebugTarget; }
	ATMemoryManager *GetMemoryManager() { return mpMemMan; }
	ATMMUEmulator *GetMMU() { return mpMMU; }
	ATAnticEmulator& GetAntic() { return mAntic; }
	ATGTIAEmulator& GetGTIA() { return mGTIA; }
	ATPokeyEmulator& GetPokey() { return mPokey; }
	ATPIAEmulator& GetPIA() { return mPIA; }
	ATDiskInterface& GetDiskInterface(int index);
	ATDiskEmulator& GetDiskDrive(int index) { return *mpDiskDrives[index]; }
	ATCassetteEmulator& GetCassette() { return *mpCassette; }
	ATInputManager *GetInputManager() { return mpInputManager; }
	IATJoystickManager *GetJoystickManager() { return mpJoysticks; }
	ATPBIManager& GetPBIManager() const { return *mpPBIManager; }
	ATVBXEEmulator *GetVBXE() { return mpVBXE; }
	ATCartridgeEmulator *GetCartridge(uint32 unit) { return mpCartridge[unit]; }
	IATUIRenderer *GetUIRenderer() { return mpUIRenderer; }
	IATAudioOutput *GetAudioOutput() { return mpAudioOutput; }
	ATLightPenPort *GetLightPenPort() { return mpLightPen; }
	ATUltimate1MBEmulator *GetUltimate1MB() { return mpUltimate1MB; }
	IATVirtualScreenHandler *GetVirtualScreenHandler() { return mpVirtualScreenHandler; }
	ATIRQController *GetIRQController();
	ATFirmwareManager *GetFirmwareManager() { return mpFirmwareManager; }
	ATDeviceManager *GetDeviceManager() { return mpDeviceManager; }
	ATScheduler *GetScheduler() { return &mScheduler; }
	ATScheduler *GetSlowScheduler() { return &mSlowScheduler; }
	IATDeviceSIOManager *GetDeviceSIOManager();
	IATDeviceCIOManager *GetDeviceCIOManager();
	ATHLEProgramLoader *GetProgramLoader() { return mpHLEProgramLoader; }
	IATHLEFPAccelerator *GetFPAccelerator() const { return mpHLEFPAccelerator; }
	IATAutoSaveManager& GetAutoSaveManager() const;
	IATPrinterOutputManager& GetPrinterOutputManager() const;

	bool IsTurboModeEnabled() const { return mbTurbo; }
	bool IsFrameSkipEnabled() const { return mbFrameSkip; }
	ATVideoStandard GetVideoStandard() const { return mVideoStandard; }
	bool IsVideo50Hz() const { return mVideoStandard != kATVideoStandard_NTSC && mVideoStandard != kATVideoStandard_PAL60; }

	ATMemoryMode GetMemoryMode() const { return mMemoryMode; }
	ATKernelMode GetKernelMode() const { return mKernelMode; }
	uint64 GetKernelId() const { return mKernelId; }
	uint64 GetActualKernelId() const { return mActualKernelId; }
	uint64 GetBasicId() const { return mBasicId; }
	uint64 GetActualBasicId() const { return mActualBasicId; }
	ATHardwareMode GetHardwareMode() const { return mHardwareMode; }
	bool IsDiskSIOPatchEnabled() const;
	bool IsDiskSIOOverrideDetectEnabled() const { return mbDiskSIOOverrideDetectEnabled; }
	bool IsDiskAccurateTimingEnabled() const;
	bool IsDiskSectorCounterEnabled() const { return mbDiskSectorCounterEnabled; }
	bool IsCassetteSIOPatchEnabled() const { return mbCassetteSIOPatchEnabled; }
	bool IsCassetteAutoBootEnabled() const { return mbCassetteAutoBootEnabled; }
	bool IsCassetteAutoBasicBootEnabled() const { return mbCassetteAutoBasicBootEnabled; }
	bool IsCassetteAutoRewindEnabled() const;
	bool IsCassetteRandomizedStartEnabled() const { return mbCassetteRandomizedStartEnabled; }
	bool IsFPPatchEnabled() const { return mbFPPatchEnabled; }

	bool IsBASICEnabled() const { return mbBASICEnabled; }
	bool SupportsInternalBasic() const;

	bool IsROMAutoReloadEnabled() const { return mbROMAutoReloadEnabled; }
	bool IsAutoLoadKernelSymbolsEnabled() const { return mbAutoLoadKernelSymbols; }
	bool IsDualPokeysEnabled() const { return mbDualPokeys; }

	bool GetDiskBurstTransfersEnabled() const;
	void SetDiskBurstTransfersEnabled(bool enabled);
	bool GetDeviceCIOBurstTransfersEnabled() const;
	void SetDeviceCIOBurstTransfersEnabled(bool enabled);
	bool GetDeviceSIOBurstTransfersEnabled() const;
	void SetDeviceSIOBurstTransfersEnabled(bool enabled);

	bool HasCIODevice(char c) const;
	bool GetCIOPatchEnabled(char c) const;
	void SetCIOPatchEnabled(char c, bool enabled) const;

	bool IsCIOPBIPatchEnabled() const;
	void SetCIOPBIPatchEnabled(bool enable);
	
	bool IsSIOPatchEnabled() const;
	void SetSIOPatchEnabled(bool enable);

	bool IsSIOPBIPatchEnabled() const;
	void SetSIOPBIPatchEnabled(bool enable);

	bool GetDeviceSIOPatchEnabled() const;
	void SetDeviceSIOPatchEnabled(bool enable) const;

	uint8	GetBankRegister() const;
	int		GetCartBank(uint32 unit) const;

	const uint8 *GetRawMemory() const;
	uint32 RandomizeRawMemory(uint16 start, uint32 count, uint32 seed);

	ATCPUProfiler *GetProfiler() const { return mpProfiler; }
	bool IsProfilingEnabled() const { return mpProfiler != NULL; }
	void SetProfilingEnabled(bool enabled);

	ATCPUVerifier *GetVerifier() const { return mpVerifier; }
	bool IsVerifierEnabled() const { return mpVerifier != NULL; }
	void SetVerifierEnabled(bool enabled);

	ATCPUHeatMap *GetHeatMap() const { return mpHeatMap; }
	bool IsHeatMapEnabled() const { return mpHeatMap != NULL; }
	void SetHeatMapEnabled(bool enabled);

	ATMemoryClearMode GetMemoryClearMode() const { return mMemoryClearMode; }
	void SetMemoryClearMode(ATMemoryClearMode mode) { mMemoryClearMode = mode; }

	bool IsFloatingIoBusEnabled() const { return mbFloatingIoBus; }
	void SetFloatingIoBusEnabled(bool enabled);

	bool IsRandomFillEXEEnabled() const { return mbRandomFillEXEEnabled; }
	void SetRandomFillEXEEnabled(bool enabled);

	bool IsRandomProgramLaunchDelayEnabled() const { return mbRandomizeLaunchDelay; }
	void SetRandomProgramLaunchDelayEnabled(bool enabled) { mbRandomizeLaunchDelay = enabled; }

	ATHLEProgramLoadMode GetHLEProgramLoadMode() const { return mProgramLoadMode; }
	void SetHLEProgramLoadMode(ATHLEProgramLoadMode mode) { mProgramLoadMode = mode; }

	void SetBreakOnFrameEnd(bool enabled) { mbBreakOnFrameEnd = enabled; }
	void SetBreakOnScanline(int scanline) { mBreakOnScanline = scanline; }
	void SetTurboModeEnabled(bool turbo);
	void SetFrameSkipEnabled(bool skip);
	void SetVideoStandard(ATVideoStandard vs);
	void SetMemoryMode(ATMemoryMode mode);
	void SetKernel(uint64 id);
	void SetBasic(uint64 id);
	void SetHardwareMode(ATHardwareMode mode);

	bool IsCPUModeOverridden() const;
	bool IsRapidusEnabled() const;

	void SetCPUMode(ATCPUMode mode, uint32 subCycles);
	ATCPUMode GetCPUMode() const;
	uint32 GetCPUSubCycles() const;

	uint8 GetAxlonMemoryMode() { return mAxlonMemoryBits; }
	void SetAxlonMemoryMode(uint8 bits);

	bool GetAxlonAliasingEnabled() const { return mbAxlonAliasingEnabled; }
	void SetAxlonAliasingEnabled(bool enabled);

	bool GetHighMemoryBanksOverridden() const;
	sint32 GetHighMemoryBanks() { return mHighMemoryBanks; }
	void SetHighMemoryBanks(sint32 bits);

	void SetDiskSIOPatchEnabled(bool enable);
	void SetDiskSIOOverrideDetectEnabled(bool enable);
	void SetDiskAccurateTimingEnabled(bool enable);
	void SetDiskSectorCounterEnabled(bool enable);
	void SetCassetteSIOPatchEnabled(bool enable);
	void SetCassetteAutoBootEnabled(bool enable);
	void SetCassetteAutoBasicBootEnabled(bool enable);
	void SetCassetteAutoRewindEnabled(bool enable);
	void SetCassetteRandomizedStartEnabled(bool enable);
	void SetFPPatchEnabled(bool enable);
	void SetBASICEnabled(bool enable);
	void SetROMAutoReloadEnabled(bool enable);
	void SetAutoLoadKernelSymbolsEnabled(bool enable);
	void SetDualPokeysEnabled(bool enable);

	bool IsFastBootEnabled() const { return mbFastBoot; }
	void SetFastBootEnabled(bool enable);

	bool IsKeyboardPresent() const { return mbKeyboardPresent; }
	void SetKeyboardPresent(bool enable);

	bool IsForcedSelfTest() const { return mbForcedSelfTest; }
	void SetForcedSelfTest(bool enable);

	bool GetCartridgeSwitch() const { return mbCartridgeSwitch; }
	void SetCartridgeSwitch(bool enable);

	bool IsMapRAMEnabled() const { return mbMapRAM; }
	void SetMapRAMEnabled(bool enable);

	bool IsPreserveExtRAMEnabled() const { return mbPreserveExtRAM; }
	void SetPreserveExtRAMEnabled(bool enable) { mbPreserveExtRAM = enable; }

	bool IsUltimate1MBEnabled() const { return mpUltimate1MB != NULL; }
	void SetUltimate1MBEnabled(bool enable);

	ATCheatEngine *GetCheatEngine() { return mpCheatEngine; }
	void SetCheatEngineEnabled(bool enable);

	bool IsAudioMonitorEnabled() const { return mbAudioMonitorEnabled; }
	void SetAudioMonitorEnabled(bool enable);

	bool IsAudioScopeEnabled() const { return mbAudioScopeEnabled; }
	void SetAudioScopeEnabled(bool enable);

	bool IsAudioStatusEnabled() const { return mbAudioStatusEnabled; }
	void SetAudioStatusEnabled(bool enable);

	bool IsVirtualScreenEnabled() const { return mpVirtualScreenHandler != NULL; }
	void SetVirtualScreenEnabled(bool enable);

	vdsize32 GetVirtualScreenSize() const;
	void SetVirtualScreenSize(const vdsize32& size);

	bool GetShadowROMEnabled() const { return mbShadowROM; }
	void SetShadowROMEnabled(bool enabled);
	bool GetShadowCartridgeEnabled() const { return mbShadowCartridge; }
	void SetShadowCartridgeEnabled(bool enabled);

	int GetPendingHeldKey() const { return mPendingHeldKey; }
	void ClearPendingHeldKey();
	void SetPendingHeldKey(uint8 key);

	uint8 GetPendingHeldSwitches() const { return mPendingHeldSwitches; }
	void SetPendingHeldSwitches(uint8 switches);

	int GetPowerOnDelay() const;
	void SetPowerOnDelay(int tenthsOfSeconds);

	ATTraceCollection *GetTraceCollection() const;
	bool GetTracingEnabled() const;
	void StopTracing();
	void StartTracing(const ATTraceSettings& settings);
	void StartNativeTracing(const ATNativeTraceSettings& settings);

	void SetPokeyWriteLogger(IATRegisterWriteLogger *logger);

	uint64 TicksSinceColdReset() const;
	double RealSecondsSinceColdReset() const;
	double SimulatedSecondsSinceColdReset() const;
	uint32 GetColdStartId() const;

	uint32 GetRandomSeed() const;
	void SetRandomSeed(uint32 seed);
	uint32 GetLockedRandomSeed() const;
	void SetLockedRandomSeed(uint32 seed);

	// Netplay determinism: force mRandomSeed, the derived per-subsystem
	// randomization seeds, and the PIA floating-input RNG state to a
	// well-known value WITHOUT a ColdReset (which would clear RAM).
	// Called on both peers at the lockstep-entry edge so PIA floating
	// inputs — which are not serialized in the savestate — stop
	// diverging across peers that seeded from independent rand() calls.
	void ReseedNetplayRandomState(uint32 masterSeed);

	bool GetPotNoiseEnabled() const;
	void SetPotNoiseEnabled(bool enabled);

	void ColdReset();
	void ColdResetComputerOnly();
	void WarmReset();
	void Resume();
	void ResumeSingleCycle();
	void Suspend();
	void Pause();

	void FlushDeferredEvents();

	// The wake function is used to alert the event processing loop that the simulator needs a
	// main thread call back to the OnWake() function. It must be thread-safe.
	void SetWakeFunction(vdfunction<void()> fn);
	void OnWake();

	void GetDirtyStorage(vdfastvector<ATStorageId>& ids, uint32 storageTypeMask = ~(uint32)0) const;
	bool IsStorageDirty(ATStorageId mediaId) const;
	bool IsStoragePresent(ATStorageId mediaId) const;
	void SaveStorage(ATStorageId storageId, const wchar_t *path);

	void SwapDrives(int src, int dst);
	void RotateDrives(int count, int delta);

	void UnloadAll(uint32 storageTypeMask = ~(uint32)0);

	bool Load(const wchar_t *path, ATMediaWriteMode writeMode, ATImageLoadContext *loadCtx);
	bool Load(const wchar_t *origPath, const wchar_t *imagePath, IVDRandomAccessStream& stream, ATMediaWriteMode writeMode, ATImageLoadContext *loadCtx);
	bool Load(const wchar_t *origPath, const wchar_t *imagePath, IATImage *image, ATMediaWriteMode writeMode, ATImageLoadContext *loadCtx);
	bool Load(ATMediaLoadContext& ctx);

	bool IsCartridgeAttached(uint32 index) const;

	void UnloadCartridge(uint32 index);
	bool LoadCartridge(uint32 index, const wchar_t *s, ATCartLoadContext *loadCtx);
	bool LoadCartridge(uint32 index, const wchar_t *origPath, const wchar_t *imagePath, IVDRandomAccessStream&, ATCartLoadContext *loadCtx);
	void LoadCartridge(uint32 index, const wchar_t *origPath, IATCartridgeImage *image);
	void LoadCartridge5200Default();
	void LoadNewCartridge(int mode);
	void LoadCartridgeBASIC();

	enum AdvanceResult {
		kAdvanceResult_Stopped,
		kAdvanceResult_Running,
		kAdvanceResult_WaitingForFrame
	};

	AdvanceResult Advance(bool dropFrame);
	void SetOnAdvanceUnblocked(vdfunction<void()> fn);

	uint32 GetCpuCycleCounter() const;
	uint32 GetTimestamp() const;
	ATCPUTimestampDecoder GetTimestampDecoder() const;

	uint8 DebugReadByte(uint16 address) const;
	uint16 DebugReadWord(uint16 address);
	uint32 DebugRead24(uint16 address);

	uint8 DebugExtReadByte(uint32 address);

	uint8 DebugGlobalReadByte(uint32 address);
	uint16 DebugGlobalReadWord(uint32 address);
	uint32 DebugGlobalRead24(uint32 address);
	void DebugGlobalWriteByte(uint32 address, uint8 value);

	uint8 DebugAnticReadByte(uint16 address);

	bool IsKernelROMLocation(uint16 address) const;

	uint32 ComputeKernelChecksum() const;
	uint32 ComputeKernelCRC32() const;
	uint32 ComputeBasicCRC32() const;

	uint32 GetConfigChangeCounter() const { return mConfigChangeCounter; }

	bool LoadState(ATSaveStateReader& reader, ATStateLoadContext *context);
	void SaveState(const wchar_t *path);

	ATSnapshotStatus GetSnapshotStatus() const;
	void CreateSnapshot(IATSerializable **snapshot, IATSerializable **snapInfo);
	bool ApplySnapshot(const IATSerializable& snapshot, ATStateLoadContext *context);

	void UpdateFloatingBus();
	void UpdateXLCartridgeLine();
	void UpdateKeyboardPresentLine();
	void UpdateForcedSelfTestLine();
	void UpdateCartridgeSwitch();
	void UpdateBanking(uint8 currbank);
	void UpdateVBXEPage();
	void UpdateSoundBoardPage();

private:
	void InternalColdReset(bool computerOnly);
	void InternalWarmReset(bool enableHeldButtons);

	void UnloadProgram();
	void LoadProgram(const wchar_t *path, IATBlobImage *image, ATImageType imageType);

	void LoadStateMachineDesc(ATSaveStateReader& reader);
	void LoadStateRefs(ATSaveStateReader& reader, ATStateLoadContext& ctx);
	void LoadStateSection(ATSaveStateReader& reader, int section);
	void LoadStateMemoryArch(ATSaveStateReader& reader);

	void InitNewCartridge(uint32 index);

	bool UpdateKernel(bool trackChanges, bool forceReload = false);
	bool ReloadU1MBFirmware();
	void InitMemoryMap();
	void RecreateMemLayerPOKEY();
	void ShutdownMemoryMap();
	void UpdateKernelROMSegments();
	void UpdateKernelROMPtrs();
	void UpdateKernelROMSpeeds();

	uint32 CPUGetCycle() override;
	uint32 CPUGetUnhaltedCycle() override;
	uint32 CPUGetUnhaltedAndRDYCycle() override;
	void CPUGetHistoryTimes(ATCPUHistoryEntry * VDRESTRICT he) const override;

	uint8 AnticReadByte(uint32 address) override;
	void AnticAssertNMI_VBI() override;
	void AnticAssertNMI_DLI() override;
	void AnticAssertNMI_RES() override;
	void AnticAssertRDY() override;
	void AnticNegateRDY() override;
	void AnticEndFrame() override;
	void AnticEndScanline() override;
	bool AnticIsNextCPUCycleWrite() override;
	uint8 AnticGetCPUHeldCycleValue() override;
	void AnticForceNextCPUCycleSlow() override;
	void AnticOnVBlank() override;

	uint32 GTIAGetXClock() override;
	uint32 GTIAGetTimestamp() const override;
	uint64 GTIAGetTimestamp64() const override;
	void GTIASetSpeaker(bool newState) override;
	void GTIASelectController(uint8 index, bool potsEnabled) override;
	void GTIARequestAnticSync(int offset) override;
	uint32 GTIAGetLineEdgeTimingId(uint32 offset) const override;
	void PokeyAssertIRQ(bool cpuBased) override;
	void PokeyNegateIRQ(bool cpuBased) override;
	void PokeyBreak() override;
	bool PokeyIsInInterrupt() const override;
	bool PokeyIsKeyPushOK(uint8 c, bool cooldownExpired) const override;

	void ReinitHookPage();
	void SetupPendingHeldButtons();
	void ResetAutoHeldButtons();
	void SetupAutoHeldButtonHook();
	void SetupAutoHeldButtons();

	void InitDevice(IVDUnknown& dev);

	void ResetMemoryBuffer(void *dst, size_t len, uint32 seed);
	void ApplyVideoStandard();

	void InitCassetteAutoBasicBoot();

	void UpdateAudioMonitors();
	void UpdateAudioStatus();
	void UpdatePBIHookDevice();

	bool mbRunning;
	bool mbRunSingleCycle = false;
	bool mbPowered;
	bool mbPaused;
	bool mbBreak;
	bool mbBreakOnFrameEnd;
	bool mbTurbo;
	bool mbFrameSkip;
	ATVideoStandard mVideoStandard;
	ATMemoryClearMode mMemoryClearMode;
	bool mbFloatingIoBus;
	bool mbRandomFillEXEEnabled;
	bool mbRandomizeLaunchDelay = true;
	bool mbDiskSIOOverrideDetectEnabled;
	bool mbDiskSectorCounterEnabled;
	bool mbCassetteSIOPatchEnabled;
	bool mbCassetteAutoBootEnabled;
	bool mbCassetteAutoBasicBootEnabled = false;
	bool mbCassetteAutoBootUseBasic = false;
	bool mbCassetteRandomizedStartEnabled;
	bool mbFPPatchEnabled;
	bool mbBASICEnabled;
	bool mbROMAutoReloadEnabled;
	bool mbAutoLoadKernelSymbols;
	bool mbDualPokeys;
	bool mbFastBoot;
	bool mbKeyboardPresent;
	bool mbForcedSelfTest;
	bool mbCartridgeSwitch;
	bool mbMapRAM;
	bool mbPreserveExtRAM;
	bool mbShadowROM;
	bool mbShadowCartridge;
	bool mbAudioMonitorEnabled = false;
	bool mbAudioScopeEnabled = false;
	bool mbAudioStatusEnabled = false;
	int mBreakOnScanline;
	ATHLEProgramLoadMode mProgramLoadMode {};

	int	mPowerOnDelay = -1;
	uint32	mPoweronDelayCounter = 0;
	int		mStartupDelay;
	int		mStartupDelay2;
	bool	mbStartupHeldKey = false;
	int		mPendingHeldKey = -1;
	uint8	mPendingHeldSwitches = 0;

	ATMemoryMode	mMemoryMode;
	ATKernelMode	mKernelMode;
	uint64			mKernelId;
	uint64			mActualKernelId;
	uint32			mActualKernelFlags;
	uint32			mKernelFlagsMask = 0;
	uint64			mBasicId;
	uint64			mActualBasicId;
	ATHardwareMode	mHardwareMode;
	ATSimulatorEvent	mPendingEvent;

	class PrivateData;
	friend class PrivateData;

	PrivateData		*mpPrivateData;
	ATMemoryManager	*mpMemMan;
	ATMMUEmulator	*mpMMU;
	ATPBIManager	*mpPBIManager;
	ATSIOManager	*mpSIOManager;
	ATCPUHookManager *mpCPUHookManager;
	ATCPUHookInitNode *mpCPUHookInitHook = nullptr;
	ATSimulatorEventManager	*mpSimEventManager;

	ATCPUEmulator	mCPU;
	ATAnticEmulator	mAntic;
	ATGTIAEmulator	mGTIA;
	ATPokeyEmulator	mPokey;
	ATPokeyEmulator	mPokey2;
	IATAudioOutput	*mpAudioOutput;
	ATPokeyTables	*mpPokeyTables;
	ATScheduler		mScheduler;
	ATScheduler		mSlowScheduler;
	ATDiskEmulator	*mpDiskDrives[15];
	ATAudioMonitor	*mpAudioMonitors[2];
	ATCassetteEmulator	*mpCassette;
	IATJoystickManager	*mpJoysticks;
	ATCartridgeEmulator	*mpCartridge[2];
	ATInputManager	*mpInputManager;
	ATLightPenPort *mpLightPen;
	ATVBXEEmulator *mpVBXE;
	ATCheatEngine *mpCheatEngine;
	IATUIRenderer *mpUIRenderer;
	ATUltimate1MBEmulator *mpUltimate1MB;
	IATVirtualScreenHandler *mpVirtualScreenHandler;
	vdsize32 mVirtualScreenSize { 80, 24 };

	ATHLEBasicLoader *mpHLEBasicLoader;
	ATHLEProgramLoader *mpHLEProgramLoader;
	IATHLEFPAccelerator *mpHLEFPAccelerator;
	ATHLEFastBootHook *mpHLEFastBootHook;
	IATHLECIOHook *mpHLECIOHook;

	ATPIAEmulator mPIA;

	uint8	mHookPage;

	uint8	mAxlonMemoryBits;
	bool	mbAxlonAliasingEnabled;
	sint32	mHighMemoryBanks;

	uint32	mConfigChangeCounter;

	const uint8 *mpKernelUpperROM;
	const uint8 *mpKernelLowerROM;
	const uint8 *mpKernelSelfTestROM;
	const uint8 *mpKernel5200ROM;
	uint32	mKernelSymbolsModuleIds[3];

	uint32		mCartModuleIds[3];

	ATCPUProfiler	*mpProfiler;
	ATCPUVerifier	*mpVerifier;
	ATCPUHeatMap	*mpHeatMap;
	IATDebugTarget	*mpDebugTarget;

	ATMemoryLayer	*mpMemLayerLoRAM;
	ATMemoryLayer	*mpMemLayerHiRAM;
	ATMemoryLayer	*mpMemLayerExtendedRAM;
	ATMemoryLayer	*mpMemLayerLowerKernelROM;
	ATMemoryLayer	*mpMemLayerUpperKernelROM;
	ATMemoryLayer	*mpMemLayerBASICROM;
	ATMemoryLayer	*mpMemLayerSelfTestROM;
	ATMemoryLayer	*mpMemLayerGameROM;
	ATMemoryLayer	*mpMemLayerPBIRAM;
	ATMemoryLayer	*mpMemLayerHiddenRAM;
	ATMemoryLayer	*mpMemLayerANTIC;
	ATMemoryLayer	*mpMemLayerGTIA;
	ATMemoryLayer	*mpMemLayerPOKEY;
	ATMemoryLayer	*mpMemLayerPIA;
	ATMemoryLayer	*mpMemLayerIoBusFloat;

	ATFirmwareManager	*mpFirmwareManager;
	ATDeviceManager		*mpDeviceManager;

	vdblock<uint8> mAxlonMemory;
};

#endif
