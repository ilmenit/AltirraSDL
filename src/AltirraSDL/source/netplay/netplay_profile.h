// Altirra SDL3 netplay - Session Profile
//
// Forces a canonical, fully-deterministic emulator configuration on
// both peers for the duration of a netplay session, and restores the
// user's normal configuration on every session-end path (clean Bye,
// peer disconnect, snapshot apply failure, app shutdown, process
// crash).
//
// The session profile lives in the registry under
// Profiles\<kNetplayProfileId> and is rewritten on every BeginSession.
// It owns the categories whose values affect simulator determinism
// (Hardware, Firmware, Acceleration, Devices, StartupConfig,
// MountedImages, NVRAM, Boot, Debugging) and inherits the rest from
// the user's parent profile (Color, View, Input, InputMaps, Speed,
// Sound, FullScreen, Environment) so palette / keymap / window /
// audio settings are unchanged.
//
// Per-game variables — the only thing the host can configure per
// hosted game — are: hardware mode, memory mode, video standard,
// BASIC enable, kernel ROM (CRC32), BASIC ROM (CRC32).  CPU mode is
// locked to 6502 in the canonical set.  All acceleration patches,
// accuracy flags, and randomization toggles are pinned to fixed
// values; no devices are attached.

#pragma once

#include <cstdint>

namespace ATNetplayProfile {

// Bumped any time the canonical value set or the Per-Game-Overrides
// wire layout changes.  Cross-checked at handshake — peers with
// mismatched canonical-profile versions refuse to share a session
// (the canonical set defines what the simulator looks like at frame
// 0; a mismatch is a guaranteed desync at frame 0).  Tracked
// independently of kProtocolVersion so the wire format and the
// canonical values can evolve separately.
constexpr uint16_t kCanonicalProfileVersion = 1;

// Master RNG seed both peers apply via SetLockedRandomSeed after
// loading the profile.  Was previously the per-session masterSeed
// field in NetBootConfig; promoted to a constant because divergent
// seeds are a guaranteed desync source and there's no scenario
// where the host would want to advertise a different value.
constexpr uint32_t kLockedRandomSeed = 0xA7C0BEEFu;

// Stable id for the netplay session profile under Profiles\.
// Outside the user's typical generated-id range
// (ATSettingsGenerateProfileId mixes time bits into the id).
// _Visible is set false so the profile never shows up in the
// Profiles submenu / chooser.
constexpr uint32_t kNetplayProfileId = 0xA7C0BE01u;

// The 6 per-game variables shipped on the wire and applied on top of
// the canonical profile.  CPU mode intentionally absent — locked to
// 6502 in the canonical set; 65C816-specific carts are out of scope
// for netplay v1.
struct PerGameOverrides {
	uint8_t  hardwareMode  = 0;   // ATHardwareMode (800 / 800XL / 130XE / XEGS / 5200 / 1200XL)
	uint8_t  memoryMode    = 0;   // ATMemoryMode
	uint8_t  videoStandard = 0;   // ATVideoStandard (NTSC / PAL / SECAM / NTSC50 / PAL60)
	uint8_t  basicEnabled  = 0;   // 0/1
	uint32_t kernelCRC32   = 0;   // 0 = canonical default for hardwareMode
	uint32_t basicCRC32    = 0;   // 0 = canonical default
};

// Resolve "use canonical default" markers (kernelCRC32 == 0,
// basicCRC32 == 0) in `ov` to the local emulator's actual default
// firmware CRC32 values.  Called by the HOST at offer-build time and
// at session-begin time so the wire always carries an explicit CRC,
// the joiner runs the same CRC-lookup path the host does, and a
// peer that doesn't have the firmware fails clean instead of
// silently cold-booting a different ROM.
//
// Built-in Altirra kernels routinely change CRC32 between releases
// (firmwaredetect.cpp ATKnownFirmware table grows / shifts), and the
// "default kernel" for a hardware mode reflects each peer's
// firmwaremanager preferences anyway.  Without resolution, two
// peers passing kernelCRC32 == 0 will silently cold-boot with
// different ROMs and desync at frame 0.  Idempotent: non-zero CRCs
// are left alone, and a successful resolve makes a second call a
// no-op.  No-op if the firmware manager is unavailable or if the
// default lookup fails — those failures surface later as
// BeginSession's existing "kernel CRC32 X not installed" error path.
void ResolveDefaultFirmwareCRCs(PerGameOverrides& ov);

// True between BeginSession and EndSession.  Used by UI gates to
// disable Configure System and the Profile chooser while a session
// is live (the user must not be able to edit the canonical profile).
bool IsActive();

// Snapshot the user's current profile id, save their live state
// into it, write the canonical profile (with per-game overrides)
// into the netplay profile key, switch to the netplay profile, and
// ColdReset the simulator.  Atomic with respect to settings: on any
// failure, no user-visible state is mutated.  Returns false on
// failure (lock-file write failure, ATLoadSettings exception); the
// caller should surface a user-visible error and abandon the join.
//
// The caller is responsible for the subsequent g_sim.UnloadAll +
// g_sim.Load(image) + g_sim.ColdReset + g_sim.Pause that loads the
// hosted game image.
bool BeginSession(const PerGameOverrides& overrides);

// Restore the user's pre-session profile and ColdReset the
// simulator.  Idempotent — safe to call multiple times and safe to
// call when no session is active.  All session-end paths funnel
// here: clean Leave, peer Bye, snapshot apply failure, app shutdown.
void EndSession();

// Crash recovery — call once at app startup, between
// ATLoadDefaultProfiles() and ATSettingsLoadLastProfile().  If a
// netplay session lock file is present (process died mid-session),
// rewrites Profiles\Current profile to the recorded pre-session id
// so the subsequent ATSettingsLoadLastProfile picks the right
// profile, and deletes the lock file.  Returns true if recovery
// fired.
bool RecoverFromCrash();

} // namespace ATNetplayProfile
