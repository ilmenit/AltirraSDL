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
#include "firmwaremanager.h"
#include "constants.h"
#include "uiaccessors.h"
#include "devicemanager.h"
#include "gtia.h"
#include "gtiatypes.h"

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

	EnsureNetplayProfileKey();

	// From here on every state mutation is in temporary mode so an
	// incidental ATSaveSettings (UI close, profile switch shortcut,
	// app shutdown) doesn't poison the canonical key with live sim
	// state captured mid-session.
	ATSettingsSetTemporaryProfileMode(true);

	// Switch the current profile id directly — NOT via
	// ATSettingsSwitchProfile, which would do a Save+Load round-trip.
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
		DeleteLockFile();
		return false;
	}

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

	// Reapply the pre-session sim snapshot so the user's running
	// game (CPU, RAM, scheduler tick, paused/running) is fully
	// restored — not just brought back to a fresh ColdReset of
	// their pre-session config.  Without this, a user who was
	// playing a local game when they accepted a join request loses
	// their progress when the session ends.
	bool snapshotApplied = false;
	if (g_preSnapshot) {
		try {
			ATStateLoadContext ctx{};
			ctx.mbAllowKernelMismatch = true;
			g_sim.ApplySnapshot(*g_preSnapshot, &ctx);
			g_sim.Resume();
			snapshotApplied = true;
		} catch (...) {
			// Best-effort: if apply fails the user is on a
			// ColdReset of their pre-session config, which is
			// still a reasonable place to land — log only.
			g_ATLCNetplay("netplay-profile: pre-session snapshot "
				"apply failed; user kept on ColdReset of restored profile");
		}
		g_preSnapshot     = nullptr;
		g_preSnapshotInfo = nullptr;
	}
	if (!snapshotApplied) {
		// No snapshot (capture failed at BeginSession) or apply
		// threw — at least guarantee the sim is running so the user
		// doesn't end up on a stuck-paused screen post-session.
		g_sim.Resume();
	}

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
