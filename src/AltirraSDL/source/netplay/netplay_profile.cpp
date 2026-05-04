// Altirra SDL3 netplay - Session Profile (impl)
//
// See netplay_profile.h for the design rationale.  This file owns:
//   1. Snapshotting the user's pre-session profile id to a lock file
//      so a crash mid-session can be recovered cleanly on the next
//      launch.
//   2. Applying the canonical, fully-deterministic emulator
//      configuration directly to the live simulator (bypassing the
//      registry so neither stale nor incidental writes can corrupt
//      the canonical baseline).
//   3. Restoring the user's pre-session profile via the existing
//      ATSettingsSwitchProfile path on EndSession.

#include <stdafx.h>

#include "netplay_profile.h"

#include "settings.h"
#include "simulator.h"
#include "cpu.h"
#include "cassette.h"
#include "disk.h"
#include "diskinterface.h"
#include "cartridge.h"
#include "firmwaremanager.h"
#include "constants.h"
#include "uiaccessors.h"
#include "devicemanager.h"
#include "gtia.h"
#include "gtiatypes.h"

// SDL3 must be visible before ui_mobile.h (which references SDL_Event
// in its public API).  We don't actually consume any SDL types here,
// but the header transitively requires the declaration.
#include <SDL3/SDL.h>
#include "ui/mobile/ui_mobile.h"  // ATMobileUIState, ATMobileUIScreen, g_mobileState

#include <at/atcore/serializable.h>
#include <at/atio/image.h>           // ATStateLoadContext
#include <vd2/system/refcount.h>

#include <vd2/system/file.h>
#include <vd2/system/registry.h>
#include <vd2/system/VDString.h>
#include <vd2/system/strutil.h>
#include <vd2/system/text.h>
#include <vd2/system/error.h>
#include <at/atcore/logging.h>

#include <SDL3/SDL_filesystem.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

// Defined by registry_sdl3.cpp on Linux/macOS/Android (in-memory
// VDRegistryProviderMemory backed by ~/.config/altirra/settings.ini)
// and stubbed to a no-op by win32_stubs.cpp on Windows AltirraSDL
// (where the Win32 native registry is already persistent).  Forward-
// declared because there is no public header for it.
extern void ATRegistryFlushToDisk();

extern ATSimulator g_sim;
extern ATLogChannel g_ATLCNetplay;
extern VDStringA ATGetConfigDir();
extern ATMobileUIState g_mobileState;  // main_sdl3.cpp:197

// Forward to ui_netplay_state.cpp's cached CRC32 helper.  Declared
// here at file scope (NOT inside ATNetplayProfile) so the linker
// resolves it against the real ATNetplayUI::ComputeFirmwareCRC32
// symbol in the UI module.  Header-free declaration keeps the
// netplay-side dependency surface from pulling in the UI state header.
namespace ATNetplayUI { uint32_t ComputeFirmwareCRC32(uint64_t); }

namespace ATNetplayProfile {

namespace {

bool                      g_active        = false;
uint32_t                  g_preProfileId  = 0;

// Pre-session sim state, captured at BeginSession and applied at
// EndSession so the user's running game (paused/running, mounted
// disks, CPU regs, RAM, scheduler tick) is fully restored — not just
// re-loaded from profile + ColdReset.  Profile-only restore is a
// regression for users who flip from local play into a netplay
// session and back; preserving the snapshot keeps them at exactly
// the cycle they were on when they accepted the join.  Lost on
// process crash (in-memory only); the lock-file recovery path falls
// back to settings-only restore, which is correct since RAM/CPU
// state didn't survive the crash anyway.
vdrefptr<IATSerializable> g_preSnapshot;
vdrefptr<IATSerializable> g_preSnapshotInfo;

// Pre-session Gaming Mode UI state.  Captured at BeginSession before
// any live mutation, used ONLY on the BeginSession failure-cleanup
// path (success EndSession deliberately forces gameLoaded=false +
// currentScreen=GameBrowser, since the pre-session running game is
// not currently restored — see netplay_profile.cpp:733-748 comment
// block on the documented-unsafe ApplySnapshot deferral).
//
// Without this restore, post-EndSession the Gaming Mode router
// (ui_mobile.cpp:695-702) sees None+gameLoaded==true and renders the
// emulator view on top of a freshly-cold-reset sim, producing a
// "phantom single player" screen.  Reported 2026-05-02 in the same
// session that surfaced the mbSerInDeferredLoad determinism bug.
//
// Harmless on Desktop builds: g_mobileState is a single global with
// no ifdef guard (main_sdl3.cpp:197), and Desktop never renders
// through the mobile router so the writes are inert.
ATMobileUIScreen          g_preMobileScreen     = ATMobileUIScreen::GameBrowser;
bool                      g_preMobileGameLoaded = false;
bool                      g_preMobileCaptured   = false;

// Path of the media file the netplay handler loaded for this session
// (.atr / .car / .xex / .cas).  Set by RegisterSessionImage from the
// kATDeferred_NetplayHostBoot / NetplayJoinerApply handlers right
// after a successful g_sim.Load.  Cleared in EndSession (after the
// scrub) and on any path that aborts the session before a load
// happened (BeginSession failure, snapshot apply failure).
//
// At EndSession we walk live-sim disk / cart / cassette slots, unmount
// any whose path equals this string, then call ATSaveSettings to flush
// the scrubbed MountedImages back to the user's profile chain.  Without
// this scrub the netplay-loaded image persists into Profiles\<userId>\
// Mounted Images by either of two paths: (a) the user already had it
// loaded pre-session and BeginSession's ATSaveSettings captured it,
// or (b) the snapshot apply / profile reload re-mounts it after the
// canonical session ends.  Either way, the user perceives the image
// as auto-loaded on next launch — which violates the "ephemeral
// session" expectation.
VDStringW g_sessionImagePath;

// Categories the netplay profile owns — see netplay_profile.h.
constexpr ATSettingsCategory kOwnedMask = (ATSettingsCategory)(
	  kATSettingsCategory_Hardware
	| kATSettingsCategory_Firmware
	| kATSettingsCategory_Acceleration
	| kATSettingsCategory_Devices
	| kATSettingsCategory_StartupConfig
	| kATSettingsCategory_MountedImages
	| kATSettingsCategory_NVRAM
	| kATSettingsCategory_Boot
	| kATSettingsCategory_Debugging
);

// ---------------------------------------------------------------------------
// Lock file (crash recovery)
// ---------------------------------------------------------------------------

VDStringA LockFilePath() {
	VDStringA p = ATGetConfigDir();
	if (!p.empty() && p.back() != '/' && p.back() != '\\')
		p += '/';
	p += "netplay_session.lock";
	return p;
}

// Atomic write via .tmp + SDL_RenamePath (same crash-safe pattern as
// game_library.cpp:644-694).  Format is intentionally trivial so a
// malformed file from a future or past Altirra is still parseable
// down to "we know we crashed mid-session" plus a best-effort
// pre-session id.
bool WriteLockFile(uint32_t preProfileId) {
	VDStringA path    = LockFilePath();
	VDStringA tmpPath = path + ".tmp";

	char body[160];
	std::snprintf(body, sizeof body,
		"v1\nprofile=%08X\ncanonical=%u\n",
		(unsigned)preProfileId,
		(unsigned)kCanonicalProfileVersion);

	try {
		VDFileStream fs(tmpPath.c_str(),
			nsVDFile::kWrite | nsVDFile::kCreateAlways
			| nsVDFile::kDenyAll);
		fs.Write(body, (sint32)std::strlen(body));
	} catch (const MyError& e) {
		g_ATLCNetplay("netplay-profile: lock-file write failed: %s",
			e.c_str());
		return false;
	} catch (...) {
		g_ATLCNetplay("netplay-profile: lock-file write failed (unknown)");
		return false;
	}

	if (!SDL_RenamePath(tmpPath.c_str(), path.c_str())) {
		g_ATLCNetplay("netplay-profile: lock-file rename failed");
		// SDL_RemovePath (rather than std::remove) handles UTF-8 path
		// conversion on Windows; std::remove takes ANSI-codepage paths
		// there and would mishandle non-ASCII config dirs.
		SDL_RemovePath(tmpPath.c_str());
		return false;
	}
	return true;
}

void DeleteLockFile() {
	VDStringA path = LockFilePath();
	SDL_RemovePath(path.c_str());
}

// Read the pre-session profile id from an existing lock file.
// Returns false if the file is absent / unreadable / unrecognised
// format; the recovery path then defaults to the global profile.
bool ReadLockFile(uint32_t& outPreProfileId) {
	outPreProfileId = 0;
	VDStringA path = LockFilePath();

	// Check existence with SDL_GetPathInfo first.  VDFileStream's
	// "file not found" path throws VDException, and on the WASM build
	// LTO + Emscripten can fail to honour the catch frame here — the
	// throw escapes all the way out to JS as an uncaught CppException
	// and aborts startup the first time the user opens the page (no
	// lock file yet, every launch hits this).  Native builds catch fine
	// either way; doing the existence check unconditionally keeps the
	// two paths uniform and avoids a noisy log line in VDFileStream.
	SDL_PathInfo info;
	if (!SDL_GetPathInfo(path.c_str(), &info)) return false;
	if (info.type != SDL_PATHTYPE_FILE) return false;

	char buf[256];
	size_t n = 0;
	try {
		VDFileStream fs(path.c_str(),
			nsVDFile::kRead | nsVDFile::kOpenExisting
			| nsVDFile::kDenyNone);
		sint64 sz = fs.Length();
		if (sz <= 0 || sz > (sint64)sizeof buf - 1) return false;
		n = (size_t)sz;
		fs.Read(buf, (sint32)n);
		buf[n] = 0;
	} catch (...) {
		return false;
	}

	// Look for "profile=XXXXXXXX" anywhere in the file.  v1 layout
	// has it on the second line, but a substring search is robust
	// against trivial format drift.
	const char *p = std::strstr(buf, "profile=");
	if (!p) return false;
	p += 8;
	uint32_t v = (uint32_t)std::strtoul(p, nullptr, 16);
	outPreProfileId = v;
	return true;
}

// ---------------------------------------------------------------------------
// Profile registry key plumbing
// ---------------------------------------------------------------------------

// Make sure Profiles\<kNetplayProfileId> exists with the right
// category mask and is hidden from the user's profile chooser.
// Idempotent.
void EnsureNetplayProfileKey() {
	const bool firstTime = !ATSettingsIsValidProfile(kNetplayProfileId);

	// Always (re)assert these — if a future Altirra changes the mask
	// or someone manually edited the registry, this normalises it.
	ATSettingsProfileSetCategoryMask(kNetplayProfileId, kOwnedMask);
	ATSettingsProfileSetSavedCategoryMask(kNetplayProfileId, kOwnedMask);
	ATSettingsProfileSetParent(kNetplayProfileId, 0);   // global
	ATSettingsProfileSetVisible(kNetplayProfileId, false);
	if (firstTime) {
		ATSettingsProfileSetName(kNetplayProfileId,
			L"Online Play (internal)");
	}
}

// Helper: locate a firmware id by CRC32 under a type filter, mirroring
// the FindFirmwareByCRC path in ui_netplay_actions.cpp.  Cheaper than
// the firmwaremanager's own [XXXXXXXX] resolver because it avoids
// re-LoadFirmware-ing every entry on every call (CRC lookup is cached
// upstream by ComputeFirmwareCRC32 in ui_netplay_state.cpp).
uint64 FindKernelByCRC(ATFirmwareManager& fwm, uint32_t crc) {
	if (crc == 0) return 0;
	VDStringW ref;
	ref.sprintf(L"[%08X]", crc);
	return fwm.GetFirmwareByRefString(ref.c_str(),
		[](ATFirmwareType t) { return ATIsKernelFirmwareType(t); });
}

uint64 FindBasicByCRC(ATFirmwareManager& fwm, uint32_t crc) {
	if (crc == 0) return 0;
	VDStringW ref;
	ref.sprintf(L"[%08X]", crc);
	return fwm.GetFirmwareByRefString(ref.c_str(),
		[](ATFirmwareType t) { return t == kATFirmwareType_Basic; });
}

// Fall-back kernel for a hardware mode when the per-game override
// CRC is 0.  Mirrors the dispatch in ApplyMachineConfig
// (ui_netplay_actions.cpp:1398-1408).
uint64 DefaultKernelForHardware(ATFirmwareManager& fwm,
                                ATHardwareMode hwmode) {
	ATFirmwareType defType;
	switch (hwmode) {
		case kATHardwareMode_5200:   defType = kATFirmwareType_Kernel5200; break;
		case kATHardwareMode_800:    defType = kATFirmwareType_Kernel800_OSB; break;
		case kATHardwareMode_XEGS:   defType = kATFirmwareType_KernelXEGS; break;
		case kATHardwareMode_1200XL: defType = kATFirmwareType_Kernel1200XL; break;
		default:                     defType = kATFirmwareType_KernelXL; break;
	}
	return fwm.GetFirmwareOfType(defType, true);
}

// Apply the canonical, fully-deterministic baseline + per-game
// overrides directly to the live simulator, skipping the registry
// entirely.  The netplay profile is in temporary mode for the whole
// session, so any incidental ATSaveSettings is a no-op — the
// canonical values live only on the live sim.
//
// Order matters:
//   1. Detach all devices (no host-IO devices — that's the whole
//      point of the canonical profile).  Wipe mounted media too so
//      a stale disk from the user's pre-session state can't bleed
//      into the joiner's cold boot.
//   2. Set hardware mode FIRST — every other knob's default depends
//      on it.  Resolve the firmware as part of the same step
//      because the kernel-by-hardware-default lookup needs the
//      hardware mode.
//   3. Apply the rest of the canonical values + per-game overrides.
//   4. SetLockedRandomSeed.
//   5. ColdReset (caller-driven; we don't ColdReset here so the
//      caller can stack the game-image Load + ColdReset in one go).
bool ApplyCanonicalProfile(const PerGameOverrides& ov) {
	auto *dm = g_sim.GetDeviceManager();
	if (dm) dm->RemoveAllDevices(false);

	// --- Hardware ---------------------------------------------------
	const ATHardwareMode  hwmode = (ATHardwareMode)ov.hardwareMode;
	const ATMemoryMode    mmode  = (ATMemoryMode)ov.memoryMode;
	const ATVideoStandard vs     = (ATVideoStandard)ov.videoStandard;

	g_sim.SetHardwareMode(hwmode);
	g_sim.SetMemoryMode(mmode);
	g_sim.SetVideoStandard(vs);
	g_sim.SetAxlonMemoryMode(0);
	g_sim.SetAxlonAliasingEnabled(false);
	g_sim.SetHighMemoryBanks(0);
	g_sim.SetMapRAMEnabled(false);
	g_sim.SetUltimate1MBEnabled(false);
	g_sim.SetFloatingIoBusEnabled(false);
	g_sim.SetPreserveExtRAMEnabled(false);
	g_sim.SetMemoryClearMode(kATMemoryClearMode_Zero);

	g_sim.SetShadowROMEnabled(true);
	g_sim.SetShadowCartridgeEnabled(false);

	g_sim.SetCPUMode(kATCPUMode_6502, 1);
	ATCPUEmulator& cpu = g_sim.GetCPU();
	cpu.SetNMIBlockingEnabled(false);
	cpu.SetIllegalInsnsEnabled(true);
	cpu.SetHistoryEnabled(false);
	cpu.SetPathfindingEnabled(false);
	cpu.SetStopOnBRK(false);

	g_sim.GetGTIA().SetCTIAMode(false);
	// CRITICAL: GTIA defect mode's MEMBER default is Type1 (see
	// gtia.h:691 mDefectMode = ATGTIADefectMode::Type1).  If we
	// don't override here, the user's pre-session defect-mode
	// setting (which may differ between peers) silently rides
	// through into the canonical profile and the GTIA renderer
	// stalls/jitters defect-emulation cycles inconsistently across
	// peers — guaranteed desync.  See project_gtia_defect_mode.md.
	g_sim.GetGTIA().SetDefectMode(ATGTIADefectMode::None);

	g_sim.SetDualPokeysEnabled(false);

	// --- Firmware ---------------------------------------------------
	if (ATFirmwareManager *fwm = g_sim.GetFirmwareManager()) {
		uint64 kid = FindKernelByCRC(*fwm, ov.kernelCRC32);
		if (ov.kernelCRC32 != 0 && kid == 0) {
			g_ATLCNetplay("netplay-profile: kernel CRC32 %08X not "
				"installed; cannot apply canonical profile",
				(unsigned)ov.kernelCRC32);
			return false;
		}
		if (kid == 0)
			kid = DefaultKernelForHardware(*fwm, hwmode);
		if (kid) g_sim.SetKernel(kid);

		uint64 bid = FindBasicByCRC(*fwm, ov.basicCRC32);
		if (ov.basicCRC32 != 0 && bid == 0) {
			g_ATLCNetplay("netplay-profile: BASIC CRC32 %08X not "
				"installed; cannot apply canonical profile",
				(unsigned)ov.basicCRC32);
			return false;
		}
		if (bid == 0 && ov.basicCRC32 == 0)
			bid = fwm->GetFirmwareOfType(kATFirmwareType_Basic, true);
		if (bid) g_sim.SetBasic(bid);
	}

	// --- Acceleration -----------------------------------------------
	g_sim.SetCassetteSIOPatchEnabled(true);
	g_sim.SetCassetteAutoBootEnabled(true);
	g_sim.SetCassetteAutoBasicBootEnabled(false);
	g_sim.SetCassetteAutoRewindEnabled(true);
	g_sim.SetCassetteRandomizedStartEnabled(false);
	{
		auto& cas = g_sim.GetCassette();
		cas.SetTurboMode(kATCassetteTurboMode_None);
		cas.SetPolarityMode(kATCassettePolarityMode_Normal);
		cas.SetFSKSpeedCompensationEnabled(false);
		cas.SetCrosstalkReductionEnabled(false);
		cas.SetVBIAvoidanceEnabled(false);
	}
	g_sim.SetFPPatchEnabled(false);          // KEY: was the most common drift
	g_sim.SetFastBootEnabled(true);
	g_sim.SetDiskSIOPatchEnabled(true);
	g_sim.SetDiskSIOOverrideDetectEnabled(true);
	g_sim.SetDiskBurstTransfersEnabled(false);
	g_sim.SetDeviceCIOBurstTransfersEnabled(false);
	g_sim.SetDeviceSIOBurstTransfersEnabled(false);
	for (char c : { 'H', 'P', 'R', 'T' })
		g_sim.SetCIOPatchEnabled(c, false);
	g_sim.SetDeviceSIOPatchEnabled(false);
	g_sim.SetSIOPatchEnabled(true);          // host-IO accel
	g_sim.SetSIOPBIPatchEnabled(false);
	g_sim.SetCIOPBIPatchEnabled(false);

	// --- Disk timing ------------------------------------------------
	for (int i = 0; i < 15; ++i) {
		ATDiskInterface& diskIf = g_sim.GetDiskInterface(i);
		diskIf.SetAccurateSectorTimingEnabled(false);
		ATDiskEmulator& disk = g_sim.GetDiskDrive(i);
		disk.SetEmulationMode(kATDiskEmulationMode_Generic);
	}

	// --- Boot / startup --------------------------------------------
	g_sim.SetHLEProgramLoadMode(kATHLEProgramLoadMode_Default);
	g_sim.SetRandomProgramLaunchDelayEnabled(false);
	g_sim.SetRandomFillEXEEnabled(false);

	// --- StartupConfig ---------------------------------------------
	g_sim.SetBASICEnabled(ov.basicEnabled != 0);
	g_sim.SetKeyboardPresent(true);
	g_sim.SetForcedSelfTest(false);
	g_sim.SetCartridgeSwitch(false);
	g_sim.SetPowerOnDelay(0);

	return true;
}

// Walk the live sim's disk / cartridge / cassette slots and unmount
// any whose path equals g_sessionImagePath.  Then ATSaveSettings
// (MountedImages) so the scrubbed state is flushed to the user's
// profile chain — preventing auto-load on next launch.
//
// MUST be called AFTER ATSettingsSetTemporaryProfileMode(false) so
// the save is not silently no-opped by the temporary-mode guard at
// settings.cpp:1764.  Idempotent — empty path = no-op.  Safe to call
// against media that wasn't in the live sim (each unmount checks the
// path first).  Case-insensitive match handles platform differences
// (macOS / Windows path comparisons drift on case otherwise).
void ScrubSessionMediaFromUserProfile() {
	if (g_sessionImagePath.empty()) return;

	const wchar_t *needle = g_sessionImagePath.c_str();
	bool anyChanged = false;

	for (int i = 0; i < 15; ++i) {
		ATDiskInterface& diskIf = g_sim.GetDiskInterface(i);
		const wchar_t *p = diskIf.GetPath();
		if (p && *p && _wcsicmp(p, needle) == 0) {
			diskIf.UnloadDisk();
			g_sim.GetDiskDrive(i).SetEnabled(false);
			anyChanged = true;
			g_ATLCNetplay("netplay-profile: scrubbed session image "
				"from disk slot %d", i);
		}
	}
	for (int i = 0; i < 2; ++i) {
		ATCartridgeEmulator *cart = g_sim.GetCartridge((uint32)i);
		if (cart) {
			const wchar_t *p = cart->GetPath();
			if (p && *p && _wcsicmp(p, needle) == 0) {
				g_sim.UnloadCartridge((uint32)i);
				anyChanged = true;
				g_ATLCNetplay("netplay-profile: scrubbed session image "
					"from cartridge slot %d", i);
			}
		}
	}
	{
		auto& cas = g_sim.GetCassette();
		const wchar_t *p = cas.GetPath();
		if (p && *p && _wcsicmp(p, needle) == 0) {
			cas.Unload();
			anyChanged = true;
			g_ATLCNetplay("netplay-profile: scrubbed session image "
				"from cassette");
		}
	}

	// Always flush MountedImages — even if anyChanged is false the
	// user might have had stale entries from a prior run that we want
	// to consolidate.  ATSaveSettings will be a no-op if temp mode is
	// somehow still on (defensive); EndSession turns it off before
	// calling here, so the save normally goes through.
	if (anyChanged) {
		ATSaveSettings(kATSettingsCategory_MountedImages);
		// On Linux/macOS/Android the registry is in-memory and only
		// reaches disk via ATRegistryFlushToDisk.  Force it now so a
		// subsequent process crash before the next periodic flush
		// preserves the scrubbed state.  No-op stub on Windows.
		ATRegistryFlushToDisk();
	}

	g_sessionImagePath.clear();
}

} // anonymous

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ResolveDefaultFirmwareCRCs(PerGameOverrides& ov) {
	ATFirmwareManager *fwm = g_sim.GetFirmwareManager();
	if (!fwm) return;

	if (ov.kernelCRC32 == 0) {
		uint64 kid = DefaultKernelForHardware(*fwm,
			(ATHardwareMode)ov.hardwareMode);
		if (kid) {
			uint32_t crc = ATNetplayUI::ComputeFirmwareCRC32(kid);
			if (crc) {
				ov.kernelCRC32 = crc;
				g_ATLCNetplay("netplay-profile: resolved default "
					"kernel for hwmode %d to CRC32 %08X",
					(int)ov.hardwareMode, (unsigned)crc);
			} else {
				g_ATLCNetplay("netplay-profile: WARNING — default "
					"kernel for hwmode %d has no readable CRC32; "
					"peers may diverge",
					(int)ov.hardwareMode);
			}
		}
	}
	if (ov.basicCRC32 == 0) {
		uint64 bid = fwm->GetFirmwareOfType(kATFirmwareType_Basic, true);
		if (bid) {
			uint32_t crc = ATNetplayUI::ComputeFirmwareCRC32(bid);
			if (crc) {
				ov.basicCRC32 = crc;
				g_ATLCNetplay("netplay-profile: resolved default "
					"BASIC to CRC32 %08X", (unsigned)crc);
			} else {
				g_ATLCNetplay("netplay-profile: WARNING — default "
					"BASIC has no readable CRC32; peers may diverge");
			}
		}
	}
}

bool IsActive() {
	return g_active;
}

void RegisterSessionImage(const wchar_t *path) {
	if (!path || !*path) {
		g_sessionImagePath.clear();
		return;
	}
	g_sessionImagePath = path;
	g_ATLCNetplay("netplay-profile: registered session image for "
		"end-of-session scrub");
}

bool BeginSession(const PerGameOverrides& ov) {
	if (g_active) {
		g_ATLCNetplay("netplay-profile: BeginSession called while "
			"already active (defensive)");
		return false;
	}

	uint32_t pre = ATSettingsGetCurrentProfileId();
	// Pathological-state guard: if the live profile is somehow
	// already kNetplayProfileId (a prior crash that didn't run
	// RecoverFromCrash, a manual registry edit, etc.), capturing it
	// as "pre" would make EndSession switch back to the empty
	// canonical profile.  Fall back to the global profile so
	// EndSession at least lands the user somewhere usable.
	if (pre == kNetplayProfileId) {
		g_ATLCNetplay("netplay-profile: live profile id was already "
			"the netplay-profile id at BeginSession; falling back "
			"to global profile for restore");
		pre = 0;
	}

	// Flush user's live state to their profile so a crash mid-session
	// or an EndSession-driven reload restores exactly what they had.
	// TemporaryProfileMode must be false here — we WANT this save.
	if (ATSettingsGetTemporaryProfileMode()) {
		// Defensive: somebody left temporary mode on.  Clear it so
		// the user's real state actually saves; we'll re-arm it
		// below before mutating the live sim.
		ATSettingsSetTemporaryProfileMode(false);
	}
	ATSaveSettings(kATSettingsCategory_All);

	// On Linux/macOS/Android, the registry is in-memory and only
	// hits disk via ATRegistryFlushToDisk (no-op stub on Windows
	// where the Win32 native registry is already persistent).
	// Force a flush HERE so the user's pre-session state is durably
	// on disk before we start mutating the live sim — otherwise a
	// process crash between this save and the next periodic flush
	// (which can be hours away if the user never toggles a setting
	// that triggers one) would leave the user's pre-session changes
	// unrecoverable.  Cheap (one INI write) compared to the cost of
	// losing user state.
	ATRegistryFlushToDisk();

	// Capture a full sim snapshot of the user's pre-session state so
	// EndSession can restore the user's running game (CPU, RAM,
	// scheduler tick, paused/running, mounted media) — not just
	// reload settings + ColdReset.  Failure here is non-fatal: we
	// fall back to the settings-only restore at EndSession.
	g_preSnapshot     = nullptr;
	g_preSnapshotInfo = nullptr;
	try {
		g_sim.CreateSnapshot(~g_preSnapshot, ~g_preSnapshotInfo);
	} catch (...) {
		g_preSnapshot     = nullptr;
		g_preSnapshotInfo = nullptr;
	}

	// Persist the lock file BEFORE we touch anything sim-visible.
	// If this fails we abort with no observable side effects (the
	// snapshot we just captured is harmless to leave in memory; it
	// gets clobbered by the next BeginSession or by app shutdown).
	if (!WriteLockFile(pre)) {
		g_preSnapshot     = nullptr;
		g_preSnapshotInfo = nullptr;
		return false;
	}

	// Capture pre-session Gaming Mode UI state.  Used by the
	// failure-cleanup path below to restore the user's UI context
	// verbatim when no live mutation occurred.  EndSession's success
	// path uses a different (lossy-honest) restore — see comment at
	// EndSession's mobile-state reset block.
	g_preMobileScreen     = g_mobileState.currentScreen;
	g_preMobileGameLoaded = g_mobileState.gameLoaded;
	g_preMobileCaptured   = true;

	EnsureNetplayProfileKey();

	// CRITICAL: update g_ATCurrentProfileId to kNetplayProfileId BEFORE
	// arming temporary mode.  ATSettingsSwitchProfile (used by EndSession
	// to restore the user's profile) early-returns if the requested id
	// equals g_ATCurrentProfileId — so if we leave the in-memory pointer
	// at `pre`, EndSession's SwitchProfile(pre) becomes a no-op: no
	// reload of user settings, no ColdReset, and the live sim keeps
	// wearing the canonical netplay state.  ApplySnapshot then lays the
	// user's pre-session CPU/RAM atop the canonical kernel ROM,
	// producing wild execution and a "Program Error" dialog.
	//
	// Use ATSettingsLoadProfile(id, mask=0) — the (false, 0) ATExchange
	// path early-returns at settings.cpp:1670 so no live state is
	// touched; the ONLY observable side effect is the
	// g_ATCurrentProfileId assignment (plus a no-op ATReloadPortMenus +
	// ATUIUpdateSpeedTiming).  ApplyCanonicalProfile below populates the
	// live sim directly.
	//
	// ATSettingsLoadProfile also clears g_ATProfileTemporary; arm temp
	// mode AFTER it returns.
	ATSettingsLoadProfile(kNetplayProfileId, (ATSettingsCategory)0);

	// From here on every state mutation is in temporary mode so an
	// incidental ATSaveSettings (UI close, profile switch shortcut,
	// app shutdown) doesn't poison the canonical key with live sim
	// state captured mid-session.
	ATSettingsSetTemporaryProfileMode(true);

	g_preProfileId = pre;
	g_active       = true;
	{
		VDRegistryAppKey rk("Profiles", true);
		rk.setInt("Current profile", (int)kNetplayProfileId);
	}
	g_ATLCNetplay("netplay-profile: BeginSession (pre=%08X -> "
		"netplay=%08X, canonical=v%u)",
		(unsigned)pre, (unsigned)kNetplayProfileId,
		(unsigned)kCanonicalProfileVersion);

	// Apply canonical baseline.  On firmware-resolution failure we
	// undo everything cleanly and return false.
	if (!ApplyCanonicalProfile(ov)) {
		// Undo: switch the current-profile pointer back, drop temp
		// mode, reload the user's profile, ColdReset to a known
		// state, restore the pre-session snapshot if we have one,
		// delete the lock file so RecoverFromCrash doesn't fire
		// next launch.
		g_active = false;
		g_preProfileId = 0;
		ATSettingsSwitchProfile(pre);          // saves no-op (temp), loads pre, ColdResets
		ATSettingsSetTemporaryProfileMode(false);
		if (g_preSnapshot) {
			try {
				ATStateLoadContext ctx{};
				ctx.mbAllowKernelMismatch = true;
				g_sim.ApplySnapshot(*g_preSnapshot, &ctx);
				g_sim.Resume();
			} catch (...) {
				// Best-effort: if apply fails the user is on a
				// ColdReset of their pre-session config, which is
				// still a reasonable place to land.
			}
		} else {
			// No snapshot — at least make sure the sim is running
			// since SwitchProfile's ColdReset may have left it in
			// an ambiguous state.
			g_sim.Resume();
		}
		g_preSnapshot     = nullptr;
		g_preSnapshotInfo = nullptr;
		// No netplay image was loaded yet (the deferred handler does
		// the load AFTER BeginSession returns), but clear defensively
		// in case a stale value lingered from a prior aborted session.
		g_sessionImagePath.clear();
		// Restore captured Gaming Mode UI state verbatim — at this
		// point no UI flow has executed since the capture, so the
		// captured values are the correct return target.  This path
		// fires when ApplyCanonicalProfile fails (e.g. firmware
		// resolution failure), so the user should land back exactly
		// where they were before clicking Host/Join.
		if (g_preMobileCaptured) {
			g_mobileState.currentScreen = g_preMobileScreen;
			g_mobileState.gameLoaded    = g_preMobileGameLoaded;
			g_preMobileCaptured = false;
		}
		DeleteLockFile();
		return false;
	}

	// Reset the session-image tracker.  The deferred handler calls
	// RegisterSessionImage after a successful Load, so we start clean.
	g_sessionImagePath.clear();
	g_sim.SetLockedRandomSeed(kLockedRandomSeed);
	// Caller is responsible for the subsequent UnloadAll + Load +
	// ColdReset that brings the game image up — see the rewritten
	// kATDeferred_NetplayHostBoot / NetplayJoinerApply handlers.
	g_sim.ColdReset();

	g_ATLCNetplay("netplay-profile: canonical applied (hw=%d mem=%d "
		"vid=%d basic=%d kernelCRC=%08X basicCRC=%08X)",
		(int)ov.hardwareMode, (int)ov.memoryMode,
		(int)ov.videoStandard, (int)ov.basicEnabled,
		(unsigned)ov.kernelCRC32, (unsigned)ov.basicCRC32);

	return true;
}

void EndSession() {
	if (!g_active) return;

	const uint32_t pre = g_preProfileId;
	g_active = false;
	g_preProfileId = 0;

	// Drop the locked random seed BEFORE the SwitchProfile so the
	// ColdReset inside SwitchProfile uses the user's normal RNG —
	// otherwise the user's restored sim has its memory initialised
	// with the canonical netplay seed pattern instead of fresh
	// randomness.
	g_sim.SetLockedRandomSeed(0);

	// ATSettingsSwitchProfile will:
	//   1. ATSaveSettings(All)   — no-op while in temporary mode (good:
	//                              we don't want the canonical sim
	//                              state written to anything).
	//   2. set g_ATCurrentProfileId = pre
	//   3. ATLoadSettings(All)   — restores the user's exact saved
	//                              state including devices, mounted
	//                              media, acceleration patches,
	//                              palette, etc.
	//   4. ColdReset             — guarantees the live sim reflects
	//                              the loaded settings.
	//   5. updates Profiles\Current profile
	ATSettingsSwitchProfile(pre);

	// Restore normal save behaviour for the user's profile.  Must
	// happen after the SwitchProfile's internal save (which we want
	// to no-op) and before any user-driven save (which we want to
	// land).
	ATSettingsSetTemporaryProfileMode(false);

	// Drop the captured pre-session snapshot.  We deliberately do NOT
	// ApplySnapshot here even though it would restore the user's
	// pre-session running game (CPU/RAM/scheduler/mounts).  Field
	// evidence (2026-04-28): ApplySnapshot on top of the SwitchProfile-
	// loaded sim leaves the CPU in an inconsistent state — within a
	// few frames of resume the kernel jumps into zero page (PC=$000B,
	// illegal opcodes) and the user sees a "Program Error" dialog.
	// Likely root cause is that ApplySnapshot internally re-runs
	// SetHardwareMode / SetMemoryMode / SetVideoStandard on top of the
	// already-ColdReset sim, leaving device timers + scheduler events
	// inconsistent with the restored CPU/RAM.  Until that interaction
	// is fully diagnosed and fixed, the safer landing is the clean
	// SwitchProfile + ColdReset above: user keeps all their settings
	// (devices, kernel, palette, input maps) but loses their pre-
	// session running game.  Most users start a fresh session anyway;
	// the few who were mid-game can re-mount + warm-reset.
	g_preSnapshot     = nullptr;
	g_preSnapshotInfo = nullptr;

	// SwitchProfile's ColdReset left the sim paused only if the user's
	// pre-session profile had a "boot paused" preference; for most
	// users the sim is running.  Resume defensively to guarantee no
	// stuck-paused screen post-session.
	g_sim.Resume();

	// Scrub the netplay-loaded media from BOTH the live sim AND the
	// user's saved profile chain.  This guarantees ephemeral session
	// semantics: the .atr / .car / .cas the netplay handler loaded
	// will not auto-load on next launch, even in the corner case
	// where the user already had it mounted before joining (in which
	// case the snapshot apply / profile reload above would have
	// re-mounted it).  Other pre-session media on different slots is
	// preserved.  Must run AFTER ATSettingsSetTemporaryProfileMode
	// (false) so the ATSaveSettings inside the scrub actually writes.
	ScrubSessionMediaFromUserProfile();

	// Reset Gaming Mode UI state to "no game, on the Game Library".
	// This is intentionally lossy — we don't restore the captured
	// pre-session values because:
	//   * Restoring gameLoaded=true is dishonest: the user's
	//     pre-session game RAM was wiped by SwitchProfile's
	//     ColdReset, and the documented-unsafe ApplySnapshot path
	//     above is deliberately skipped.  Showing the emulator view
	//     (router behaviour for None+gameLoaded) on a freshly
	//     cold-reset sim presents a phantom "single player" screen.
	//   * Restoring currentScreen verbatim risks landing on a stale
	//     overlay (AcceptJoinPrompt, etc.) that's no longer valid.
	// The honest UI state matching the underlying sim is
	// "GameBrowser + gameLoaded=false" — the user can re-launch a
	// game from the library if they want.  Harmless on Desktop
	// (mobile router never runs).
	g_mobileState.gameLoaded    = false;
	g_mobileState.currentScreen = ATMobileUIScreen::GameBrowser;
	g_preMobileCaptured = false;

	DeleteLockFile();

	g_ATLCNetplay("netplay-profile: EndSession (restored=%08X)",
		(unsigned)pre);
}

bool RecoverFromCrash() {
	uint32_t pre = 0;
	if (!ReadLockFile(pre)) return false;

	// Defensive: refuse to restore to the netplay-profile id itself
	// (would leave the user on the empty canonical profile).  Same
	// guard as BeginSession.  Falls back to the global profile.
	if (pre == kNetplayProfileId) pre = 0;

	// The lock file means the previous run died while the netplay
	// profile was active.  Rewrite Profiles\Current profile to the
	// user's pre-session id BEFORE ATSettingsLoadLastProfile fires —
	// so the subsequent load picks up the right settings without us
	// having to reach into the load path.
	{
		VDRegistryAppKey rk("Profiles", true);
		rk.setInt("Current profile", (int)pre);
	}

	// Defensive: if the user crashed in a way that left the netplay
	// profile populated with stale values (shouldn't happen because
	// of TemporaryProfileMode, but belt-and-braces), wipe it so a
	// future Initialize-from-scratch starts clean.
	{
		VDRegistryAppKey root("Profiles", true);
		char keyName[16];
		std::snprintf(keyName, sizeof keyName, "%08X",
			(unsigned)kNetplayProfileId);
		root.removeKeyRecursive(keyName);
	}

	DeleteLockFile();

	g_ATLCNetplay("netplay-profile: RecoverFromCrash — restored "
		"profile %08X (was netplay session)", (unsigned)pre);

	return true;
}

} // namespace ATNetplayProfile
