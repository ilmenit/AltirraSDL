# Netplay implementation and measurements — 2026-10-21

Branch: `codex/netplay-reliability-rollback`. Historical comparison:
`0a011be1`. Tests use private local services and original generated programs;
no public listings or commercial game images are required.

## Result

The implementation combines reliable deterministic input exchange with a
small fixed local delay and bounded remote prediction. Automatic mode uses
two frames of delay and eight frames of prediction. Delay-only remains
available under Online Play → Preferences → Hosting.

The measured benefit is lower local input delay and fewer network stalls.
It costs checkpoint memory and replay work. This is a native implementation
using the existing simulator state machinery; it does not integrate the GGPO
SDK. Browser transport remains WebSocket, with its ordered-stream behavior.

## Seeded network comparison

Run `python3 tests/netplay/benchmark.py`. Each case simulates thirty seconds,
changing inputs, 50/60 Hz, 30/60/100/150 ms RTT, uniform ±10 ms one-way jitter
and 1% loss. The same seed is used, although different transmission schedules
consume the loss stream differently. Automatic RTT ratcheting is disabled
in this fixture to compare fixed input buffers.

| 60 Hz, 150 ms RTT | Previous four-frame delay | Repaired four-frame delay | Two-frame delay + rollback |
|---|---:|---:|---:|
| Host/joiner frames advanced | 108 / 108 | 1519 / 1520 | 1800 / 1800 |
| Divergence | Frame 104 | None | None |
| Local input to first application | 78.16 ms before failure | 81.87 ms | 33.33 ms |
| Summed peer stall time | 571 ms before failure | 8764 ms | 0 ms |
| Extra replayed frames | 0 | 0 | 2634 |

All eight current hybrid cases completed without divergence: 1500 frames
at 50 Hz and 1800 at 60 Hz. Their local delays were 40 and 33.33 ms,
respectively, versus roughly 80 and 66.67 ms with four-frame delay.

These are scheduling/input-history measurements using a small synthetic
state. The fixture CPU column is **not emulator CPU performance**.

## Actual disk reads, writes and recovery

Repeat the real-emulator test with:

```bash
python3 tests/netplay/emulator_session.py --disk-rw --video pal --mode automatic --rtt-ms 60 --loss 0.01 --seconds 20 --resync-test
```

Repeat with `--video ntsc` and `--mode delay`. The helper generates a bootable
720-sector ATR, writes input-dependent 128-byte buffers to sector 4 through
OS SIOV, reads the sector into another buffer and compares every byte. Both
players continuously change keyboard and console input. The real relay path
is forced through a seeded delay/loss proxy; direct rescue cannot bypass it.

Completed controls and rollback tests:

- PAL and NTSC delay-only disk boots and repeated verified read/write loops
  pass with 60 ms RTT, ±5 ms one-way jitter and 1% loss.
- NTSC Automatic: 20 seconds, frames 34/38 → 1231/1235, 239 corrections and
  1003 extra replayed frames. Worst correction took 7.905 ms. No spontaneous
  desync/resync; both guest disk status values were success and all guest
  read/write/compare error counters stayed zero.
- PAL Automatic plus deliberate recovery: one host RAM byte was changed to
  force a mismatch. Exactly one resync occurred, then both peers advanced
  at least 1000 more frames while input continued changing. Session ID and
  endpoint remained unchanged; Leave restored the original media and modes.
- NTSC also passed deliberate recovery and another 1000 frames, with no
  second mismatch. Original mounted/unmounted disk modes were restored.

Earlier failures are retained in the scratch artifact index. They drove fixes
to partial-frame prediction, audio clock rewind, keyboard/serial/pot state,
drive/SIO snapshots, startup OPTION holds, pending loaders and teardown.

### Did the old build synchronize disk writes?

Yes, the historical controls support that conclusion for this fixture.
Both peers executed the same disk commands against copies of the same
starting image; there were no separate network messages for sector writes.

The preserved `0a011be1` executable and matching historical server completed
the PAL read/write loop with 1% loss and the NTSC loop without loss. All guest
error counters stayed zero. Two NTSC attempts with 1% loss failed during the
initial image transfer, after 76/77 acknowledgements, before disk boot.
Those are transfer-completion failures, not evidence of broken disk writes.
The new implementation additionally hashes session disk overlays and makes
writes reversible and transferable during resync.

## Checkpoint regressions that failed before the fixes

The final review used guest-visible effects, in addition to state hashes:

- Restoring mid-scanline text DMA changed a player/playfield collision from
  `$00` to `$04` in guest RAM and changed CPU results. Native and serialized
  replay now match at both tested beam positions.
- Two pending ANTIC CHBASE writes exposed an incorrectly indexed restore
  queue. Both restore paths now apply both writes at the original cycles.
- 576K and 1088K banked-memory snapshots lost BASIC/self-test ROM latch
  history. Four native/serialized cases now preserve the mappings, guest
  extended-RAM results and subsequent frame execution.

The final PAL read/write/recovery run after these fixes also passed:
`build/_scratch/netplay-emulator/automatic-6p3spklq`. It recovered once from
an intentional mismatch, continued for 1000 frames, and restored the local
session on Leave. No further production change was required by that run.

## CPU and memory cost

Linux x86-64, Intel Core Ultra 7 255H, GCC 16.2.1 Release builds. Actual
sessions used real-time frame pacing and SDL dummy/software video and audio.
CPU time includes the emulator UI, not only emulation, and these short runs
are observations on one machine rather than general performance guarantees.

For the same NTSC disk read/write fixture, 60 ms RTT and 1% loss:

| Per emulator, approximately 20 seconds | Current delay-only | Current Automatic |
|---|---:|---:|
| CPU seconds, host / joiner | 9.96 / 9.98 | 10.32 / 10.39 |
| Resident memory, host / joiner | 57.84 / 57.41 MiB | 61.64 / 61.63 MiB |
| Local input buffer | Four frames (~67 ms) | Two frames (~33 ms) |

Automatic used roughly 4% more total CPU time and around 4 MiB more resident
memory in these runs. The historical executable used roughly 50 MiB in its
PAL control, so the overall branch has additional memory costs beyond the
rollback history. Different hosts, games, renderers and latency can change
these costs substantially. The default retains nine checkpoint slots;
replay is bounded and pauses when the remote horizon is exhausted.

Paused checkpoint microbenchmark, after warming the actual emulator:

| Mean operation | Native local checkpoint | Serialized ZIP/JSON state |
|---|---:|---:|
| Capture | 0.061 ms | 5.019 ms |
| Restore | 0.170 ms | 1.142 ms |

Native capture was about 83 times faster and restore about seven times
faster in this final run. A previous run measured 0.116/0.319 ms for native
capture/restore, so these short timings vary with load and CPU frequency.
This does not include emulating replayed frames. ZIP payload was 283,845
bytes; ordinary rollback does not create that archive.

## Verification and limits

The final integrated Linux build passed 39/39 CTest cases. The expanded
UI/input/profile/loader suite passed 38/38 cases, with both newly appended
ANTIC queue cases also passing (40 UI cases total). The lobby suite passed
3/3 cases and the rebuilt libretro core passed all three option-API variants.
Validation also includes CPU consecutive-cycle snapshot/replay tests
plus 240 conformance cases, active-DMA collision replay, 576K/1088K banked
ROM-latch replay, and local HTTP/UDP/WebSocket server fixtures.
Core protocol, CPU, input capture and COW disk tests also passed ASan/UBSan;
applicable standalone runs included leak checking. Linux SDL and no-SDL
libretro builds and three libretro option-API smoke variants pass.

Android ARMv7 lobby/network and ARM64 HTTP backend syntax checks passed.
Native Windows/macOS execution and an actual browser/WASM run were not
available locally. Browser approval and cancellation have source review and
server/JavaScript regression coverage, which does not replace browser play
testing. No public lobby deployment or branch push is part of these results.

The fast state fingerprint covers base 64 KiB RAM, CPU/device samples and
session disk overlays; it is not an exhaustive hash of every expansion and
internal field. Targeted replay tests also compare mapped guest memory.

Longer WAN sessions, restrictive routers, low-end hardware and audible
speculative-audio behavior still need testing. Already submitted speculative
audio cannot be retracted. Protocol 8 requires matching peer builds and an
updated lobby for authenticated relay paths.

The local artifact index is
`build/_scratch/netplay-atr-proof/result-index.json`; full logs and resource
samples are under `build/_scratch/netplay-emulator/`. The fixtures and their
commands are tracked in [tests/netplay](../tests/netplay/README.md).
