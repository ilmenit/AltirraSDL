# Libretro Core — Implementation Plan

Audit + plan date: **2026-06-30**. Target: `src/AltirraLibretro/` (core
`AltirraLibretro`, output `altirra_libretro.{so,dll,dylib}`). See `CLAUDE.md`
"Merging from Altirra Mainline" and memory notes `project_libretro_core` /
`project_libretro_ux_gaps`.

Supersedes the deleted `RETRO_ARCH_IMPROVEMENTS.md`. Findings come from an
adversarial bug hunt + a libretro API-contract review against canonical
`libretro.h` (RetroArch master), all spot-checked by direct code reads.
**Line numbers drift — re-confirm `file:line` before editing.**

## State of the core

Mature and broadly conformant: core-options v2 + categories + legacy fallback,
disk-control *ext* interface, subsystems (cart+disk), real snapshot savestates,
content-aware 5200 detection, writable disks with safe sidecars, POKE cheats,
keyboard callback + polled fallback, dynamic input descriptors, correct
system/save/firmware directories. The `.info` flags match the implementation.

The work below is **mostly small, localized bug fixes** plus a few opt-in feature
additions. It is *not* a rewrite.

## Guiding principles (keep it pragmatic)

1. **Bugs before features.** Phase 1 is correctness only; ship it first.
2. **Minimal correct fix wins.** Prefer a two-line targeted fix over a refactor
   unless the refactor removes a whole class of the same bug.
3. **Match the proven SDL path.** Where the SDL frontend already solves a problem
   correctly (audio clock, video-standard bucketing), replicate it rather than
   invent.
4. **Don't build what nothing asks for.** See "Explicitly out of scope" — several
   of the audit's "correct-tier" ideas (live memory write-back, per-frame state
   checkpointing, refcounted switch tracking, multi-byte cheat engine) are
   deliberately *not* in this plan.

Effort tags: **S** ≈ <1h, **M** ≈ a few hours, **L** ≈ a day+.

---

# Phase 1 — Correctness bug fixes (ship first)

All confirmed by direct read. All are small and low-risk.

### 1.1 · Audio resampler locked to NTSC clock — wrong pitch + crackle on PAL (the default) — **S**
**Where:** `libretro_audio.cpp:562` sets `SetCyclesPerSecond(1789772.5, 1.0)` (NTSC)
once and never again. The SDL frontend recomputes this per video standard
(`main_pacer.cpp:27-30, 397, 481`); libretro has no equivalent, so PAL/SECAM/NTSC50
run ~0.9% sharp and underrun RetroArch's ±0.5% DRC window. Default standard is
`pal`, so it ships broken.

**Fix:**
1. Add a small helper in `libretro.cpp` mirroring `main_pacer.cpp:335-342`:
   ```cpp
   // constants from src/h/at/atcore/constants.h
   static double MasterClockForStandard(ATVideoStandard vs) {
       if (vs == kATVideoStandard_SECAM) return kATMasterClock_SECAM;
       const bool hz50 = vs != kATVideoStandard_NTSC && vs != kATVideoStandard_PAL60;
       return hz50 ? kATMasterClock_PAL : kATMasterClock_NTSC;
   }
   ```
   (Same bucketing the SDL build uses → correct by construction. Libretro has no
   frame-rate-mode knob, so pass the raw clock, factor `1.0`.)
2. Call `g_sim.GetAudioOutput()->SetCyclesPerSecond(MasterClockForStandard(std), 1.0)`
   - once in `InitSimulator` after `g_sim.LoadROMs()` / audio exists, and
   - whenever the standard changes — in `ApplyPendingResetOptions` right after
     `g_sim.SetVideoStandard(...)` (`libretro.cpp:1920`), and on the AV-info refresh
     path (`~4070`).

**Test:** PAL `.atr` pitch matches the SDL build; no periodic crackle; repeat NTSC
and SECAM.

### 1.2 · `ReleaseInput` leaves held inputs asserted — stuck 5200 key / stuck console switch — **S**
**Where:** `ReleaseInput` (`libretro.cpp:~3140-3214`) is the one release path for
device change / reset / load-unload / vkbd-open.
- `3208-3213` zeroes `padKeyHeldInputCodes` with **no** matching `OnButtonUp` →
  held 5200 keypad button stuck.
- `3182-3194` zeroes `vkbdConsolePulseFrames[i]` with **no**
  `SetConsoleSwitch(bit,false)` → an in-flight vkbd console pulse latches START /
  SELECT / OPTION **on**.

**Fix (minimal, targeted):**
1. Before zeroing the pad-key arrays, release them:
   ```cpp
   if (im) for (uint32 code : g_core.padKeyHeldInputCodes)
       if (code) im->OnButtonUp(0, code);
   ```
2. In the console loop, fold the pulse into the release condition so a pulsing
   switch is cleared too:
   ```cpp
   if (g_core.vkbdConsolePulseFrames[i] || g_core.consoleHeld[i]
       || g_core.keyboardConsoleHeld[i])
       g_sim.GetGTIA().SetConsoleSwitch(kConsoleSwitchBits[i], false);
   g_core.vkbdConsolePulseFrames[i] = 0;
   g_core.consoleHeld[i] = g_core.keyboardConsoleHeld[i] = false;
   ```

### 1.3 · vkbd console-pulse expiry clobbers a still-held console switch — **S**
**Where:** `UpdateVkbdConsolePulses` (`libretro.cpp:2927-2928`) does
`SetConsoleSwitch(bit,false)` unconditionally when a pulse hits 0, even if the
joystick or keyboard still holds the same switch (the joystick loop then won't
re-assert because `down == consoleHeld[i]`).

**Fix (minimal):** OR the other two sources instead of forcing off:
```cpp
if (!g_core.vkbdConsolePulseFrames[i])
    g_sim.GetGTIA().SetConsoleSwitch(kConsoleSwitchBits[i],
        g_core.consoleHeld[i] || g_core.keyboardConsoleHeld[i]);
```
*(This two-line fix is preferred over a full "central recompute" refactor — it
removes the bug without touching every call site. See out-of-scope note.)*

### 1.4 · Unserialize can leave a corrupt running machine — **S**
**Where:** `LoadSerializedState` (`libretro.cpp:4215-4219`) applies the snapshot in
place and `Resume()`s even when `ApplySnapshot` returns false. Under rewind/runahead
(unserialize every frame) one bad apply poisons the session.

**Fix (pragmatic):** on failure, reset to a known-good state instead of running a
half-applied machine:
```cpp
const bool ok = g_sim.ApplySnapshot(*snapshot, nullptr);
if (!ok) g_sim.ColdReset();   // never leave a partially-applied machine running
g_sim.Resume();
```
*(Cheaper and simpler than snapshotting current state before every apply — see
out-of-scope.)*

### 1.5 · `retro_run` Advance loop is unbounded — potential hard hang — **S**
**Where:** `libretro.cpp:4418-4427` only breaks on "frame posted" or
`kAdvanceResult_Stopped`; `kAdvanceResult_WaitingForFrame` and a runaway `Running`
are not handled → freezes the whole frontend if a frame is never posted.

**Fix:** break on `WaitingForFrame` and add a safety cap:
```cpp
for (int guard = 0; guard < kMaxAdvancePerFrame; ++guard) {   // e.g. 2,000,000
    const auto r = g_sim.Advance(false);
    if (g_core.nullDisplay && ATLibretroNullVideoDisplayConsumeFramePosted(g_core.nullDisplay)) break;
    if (r == ATSimulator::kAdvanceResult_Stopped) break;
    if (r == ATSimulator::kAdvanceResult_WaitingForFrame) break;
}
```

### 1.6 · `retro_cheat_set` unbounded `resize(index+1)` — OOM on garbage index — **S**
**Where:** `libretro.cpp:4476-4477`. `index` is frontend-controlled.
**Fix:** clamp before resize: `if (index >= kMaxCheats) return;` (`kMaxCheats`
e.g. 4096).

### 1.7 · `DiskReplaceImageIndex` leaves `diskIndex == size()` → can't re-insert — **S**
**Where:** `libretro.cpp:920-923`. After erasing the current last entry, `diskIndex`
is set to `size()` (out of range); the next un-eject's `MountDiskIndex` rejects it.
**Fix:** clamp to a valid index:
```cpp
if (g_core.diskIndex >= g_core.diskImages.size())
    g_core.diskIndex = g_core.diskImages.empty() ? 0
        : (unsigned)g_core.diskImages.size() - 1;
```

### 1.8 · `MakeDirectory` is single-level → settings save can silently fail — **S**
**Where:** `libretro_dirs.cpp:32-41` (`mkdir`/`_mkdir` final component only). If a
parent is missing, `settings.ini` save/load fails silently. The sibling
`EnsureDirectoryPath` (`libretro.cpp:539`) is already recursive.
**Fix:** make `MakeDirectory` create parents (iterate path separators, `mkdir` each),
matching `EnsureDirectoryPath`.

**Phase 1 validation:** build `./build.sh --libretro --libretro-test`; run
`scripts/run-libretro-retroarch-smoke.sh`; manual input/audio/state checks per item.

---

# Phase 2 — Contract alignment & robustness

Lower urgency; each stands alone.

### 2.1 · `SYSTEM_RAM` size/data consistency + honest descriptor flags — **S**
**Where:** `retro_get_memory_size` (`4584-4588`) returns `0x10000` even when
`retro_get_memory_data` (`4570-4581`) returns null (no-content / pre-first-frame);
`RegisterMemoryMaps` (`1708-1726`) advertises the whole 0x0000-0xFFFF as **writable**
"System RAM" though the buffer is a per-frame **copy** (writes never reach the core).

**Fix (minimum, correct tier only):**
- `retro_get_memory_size` returns `0` when data would be null (keep the pair
  consistent).
- Mark the descriptor **`RETRO_MEMDESC_CONST`** (read-only) since frontend writes do
  not propagate, and stop implying ROM/I/O is writable RAM. Reads (RetroAchievements,
  cheat *search*) keep working with the documented 1-frame lag.
- Add a one-line comment: frontend memory *writes* are unsupported; use the core's
  own `retro_cheat_set`.

*(The "live read/write pointer into emulated RAM" is deliberately deferred — see
out-of-scope.)*

### 2.2 · Adopt `RETRO_ENVIRONMENT_GET_LOG_INTERFACE` — **S**
**Where:** all diagnostics use `fprintf(stderr)` (e.g. `libretro.cpp:4397`,
`libretro_dirs.cpp`). Invisible in the RetroArch log overlay / on consoles.
**Fix:** fetch `retro_log_callback` in `retro_set_environment`; route through a
`LogPrintf(level, fmt, ...)` wrapper that falls back to `stderr` when unavailable.

### 2.3 · AV-info edge cases — **S**
**Where:** `libretro.cpp:3968-3972`.
- SECAM is bucketed to the PAL frame rate; use `kATFrameRate_SECAM` for SECAM
  (~50.09 vs ~49.86).
- `sample_rate` is the literal `48000.0`; replace with a single
  `kLibretroSampleRate` constant shared with the audio backend's `mSamplingRate`
  default (+ a `static_assert`/comment) so they can't silently diverge.

### 2.4 · vkbd Shift/Ctrl wrapper can drop a physically-held modifier — **S** (rare)
**Where:** `ProcessVkbdEvent` (`libretro.cpp:2876-2887`) emits synthetic Shift/Ctrl
down+up around a vkbd keypress; if the user *also* physically holds that modifier,
the synthetic up releases it.
**Fix:** only emit the synthetic modifier-up if that modifier is not physically held
(check `keyboardHeldCodes` for `RETROK_LSHIFT/RSHIFT` / `RETROK_LCTRL/RCTRL` before
the release). Narrow real-world case (physical keyboard + on-screen keyboard at
once), hence Phase 2.

### 2.5 · Tighten savestate size for rewind/runahead — **M**
**Where:** `retro_serialize_size` returns a fixed 64 MB (`libretro.cpp:225, 4448`);
rewind allocates a ring of 64 MB slots. *Contract-compliant* (never grows) but
wasteful. Real Atari 8-bit state (≤1088K RAM + device state, zipped) is tiny.
**Fix:** on game load, build the cache once, measure the real payload, and set
`serializeFixedSize = min(64MiB, roundUp(payload*2 + header, 64KiB))`. Keep it
constant for the session (monotonic-non-increasing → still compliant). Optionally
skip zip compression to speed up runahead. Re-measure only on cold reset / media
change (which already invalidate the cache).

### 2.6 · Documented non-bugs (no code change) — **S**
- `keyboardCallbackEventSeen` latches for the session (`2942`): only matters for a
  frontend that sends one callback then switches to poll-only — not observed in
  practice. **Document**, don't change.
- Cheats applied twice per frame and accept I/O addresses (`3334`, `4416/4429`):
  the double-apply is the standard "poke each frame" model; restricting addresses is
  unnecessary for normal RAM cheats. **Leave**, add a comment.
- `altirra_performance_tier` `"balanced"` is a dead value (`1134`, tested only at
  `1824/2007`). **Fix trivially:** drop `"balanced"` from `kPerformanceTierValues`
  (keep quality/performance) unless a distinct middle behavior is actually wanted.

---

# Phase 3 — Feature additions (opt-in, larger)

Do these on request / as polish; none are correctness issues.

### 3.1 · ROM / firmware selection (answers "can we pick a ROM?") — **M**
**Current:** `InitSimulator` (`3995`) scans firmware dirs and `LoadROMs()`; the scan
(`RegisterDetectedFirmware`, `275-311`) sets the **first detected per type** as
default (`304`, arbitrary when you have two of a type) and Altirra falls back to
embedded **AltirraOS / AltirraBASIC** when no ROM is present. `altirra_system`
picks the hardware *type*; `altirra_basic` is on/off. The `.info firmware0..6`
entries are RetroArch's missing-files checklist, **not** a selector.
**There is no way to choose among the ROMs the user actually has.**

**Fix:** add reset-class core options populated at registration time from
`ATFirmwareManager::GetFirmwareList(out)` filtered by `ATFirmwareInfo::mType`:
- `altirra_os_firmware` — `auto` | `internal` | `<each detected OS by name>`
- `altirra_basic_firmware` — `auto` | `internal` | `<each detected BASIC>`
- `altirra_5200_bios` — `auto` | `<each detected 5200 BIOS>`

Apply via `fwm.SetDefaultFirmware(type, id)` (and `SetSpecificFirmware` where
relevant) on cold reset; set `mbAutoselect = false` on the user's pick so the
autodetect default does not override it. *Caveat:* the value list is dynamic
(depends on files present), so build it after the firmware scan and before
`RegisterCoreOptions`.

### 3.2 · Region `auto` — **S-M**
`altirra_video_standard` has no `auto`; region is never inferred from content even
though hardware mode is (`DetectContentHardwareMode`). Add `auto` that picks NTSC/PAL
from content/database hints where available; keep `pal` as the fallback.

### 3.3 · Controller variants via `RETRO_DEVICE_SUBCLASS` — **M**
Expose paddle-A/B, light pen vs light gun, ST mouse, 5200 analog as
`RETRO_DEVICE_SUBCLASS(base,id)` in `SET_CONTROLLER_INFO` (`~3888-3894`) so they show
in RetroArch's Device-Type menu instead of only core options. **Contract rule:** keep
polling `retro_input_state` with the **base** id; subclass ids are for selection only.

### 3.4 · Ports 3-4 / multitap — **M**
`g_controllerDevices[2]` exposes 2 ports; 400/800 supports 4 joysticks. Add ports
3-4 for 4-player titles.

### 3.5 · Cartridge mapper override — **M**
Mapper comes only from auto-detection; add an option to force a mapper for headerless
`.bin`/`.rom` carts.

### 3.6 · Smaller items — **S each**
- `RETRO_MEMORY_SAVE_RAM`: expose where a config has cart/battery RAM, else document
  the intentional omission.
- `SET_CORE_OPTIONS_DISPLAY` to hide irrelevant options (e.g. disk write mode with no
  disk).
- Env niceties: disk-activity LED (`GET_LED_INTERFACE`), audio buffer status,
  performance level, fastforward override, frame-time callback.

---

# Explicitly out of scope (avoid overengineering)

These were considered and **intentionally rejected** for now — revisit only on real
demand:
- **Live memory write-back path** for `SET_MEMORY_MAPS` / `retro_get_memory_data`
  (route frontend writes into emulated RAM). Bank-switching makes the
  "non-relocating pointer" contract hard; the core's own `retro_cheat_set` already
  covers cheating. Ship 2.1's read-only honesty instead.
- **Pre-apply state checkpoint on every unserialize.** Doubles per-frame cost under
  rewind. 1.4's ColdReset-on-failure is sufficient.
- **Refcounted/centralized console-switch arbiter.** The two minimal fixes (1.2, 1.3)
  remove the bugs; a full arbiter is more machinery than the problem warrants.
- **Multi-byte / compare / freeze cheat engine.** Single-byte POKE matches the Atari
  cheat convention; extend only if users ask.
- **Snapshotting input/console hold state for runahead determinism.** Minor jitter at
  most; not worth the serialization surface.

---

# Suggested sequencing

1. **Phase 1** in one PR (all small bug fixes; audio + input are the headline wins).
2. **Phase 2** in a second PR (memory honesty, log interface, AV-info, savestate
   size, the trivial option/value cleanups).
3. **Phase 3** items individually, prioritizing **3.1 (ROM selection)** and
   **3.2 (region auto)** as the most user-requested, then controller/ports.

# Validation checklist

- **Build/CI:** `./build.sh --libretro --libretro-test`;
  `scripts/run-libretro-retroarch-smoke.sh`; `.github/workflows/libretro-core.yml`
  (8-ABI matrix + boot/video smoke) must stay green.
- **Audio (1.1):** PAL/SECAM/NTSC pitch matches SDL build; no underrun crackle.
- **Input (1.2/1.3):** bind a 5200 pad key and a vkbd console key; change device /
  reset mid-hold → nothing sticks; hold physical START across a vkbd START pulse.
- **State (1.4/2.5):** enable rewind + runahead → no desync, no 64 MB-per-slot blowup.
- **Memory (2.1):** RetroAchievements set loads and reads correctly; size/data never
  disagree (no-content boot included).
- **ROM select (3.1):** with two OS ROMs of the same type present, the chosen option
  selects deterministically; `internal` boots AltirraOS; `auto` matches today's
  behavior.
