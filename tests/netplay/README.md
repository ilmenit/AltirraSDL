# Netplay network simulation benchmark

Run `python3 tests/netplay/benchmark.py` from the repository. Set `CXX` if
`g++` or `clang++` is not on PATH. `--baseline <git-ref>` selects the old
core; the default is the previously released `0a011be1`. Source copies,
executables and CSV reports stay in `build/_scratch/netplay-benchmark`.

Each deterministic case models thirty seconds of two peers with changing
input, 50 or 60 frames/second, 30/60/100/150 ms RTT, independent uniform
-10..+10 ms one-way jitter, 1% datagram loss and a 1 ms network clock.
Packets are emitted after advancement and periodically while stalled.
The baseline uses its original recent-only window. Current runs include
separate historical repair packets when necessary. The same random seed
is used per case; different transmission schedules consume the seeded
loss stream differently.

The compared configurations are old four-frame delay, repaired four-frame
delay, and two-frame delay with an eight-frame rollback window. Zero RTT
stamps disable automatic delay changes to isolate those configurations.

`stall_peer_ms` sums both peers' stalled milliseconds; `replayed_frames`
counts extra simulated frames on both sides. `local_input_to_first_apply_ms`
is perceived local input delay, including speculative application. It is
not the time when the remote peer confirms that frame. Divergence is
reported as frame + 1, reserving zero for no detected divergence.

The fixture stores a small synthetic deterministic state and validates
confirmed frame histories. Its CPU column measures only this scheduling,
packet queue and synthetic replay loop. It does not measure Altirra save
states, game emulation, rendering, audio, real network stacks or network
connection setup. Real emulator checkpoint and two-process integration
measurements must be reported separately. Historical failures remain in
the CSV; only divergence in current configurations causes a failing exit.

## Actual emulator sessions

Build AltirraSDL and the standalone lobby, then run:

```bash
python3 tests/netplay/emulator_session.py --disk-rw --video pal --mode automatic --rtt-ms 60 --loss 0.01 --seconds 20 --resync-test
```

Use `--emu` and `--lobby` to select their executable paths. Repeat with
`--video ntsc` and `--mode delay`. The fixture starts a private loopback
lobby and two emulator processes, forces the real UDP relay through a
seeded delay/loss proxy, and changes keyboard and console input. Artifacts
under `build/_scratch/netplay-emulator` retain logs, frame/rollback/RTT
telemetry, resource accounting where `/proc` is available, and failure
screenshots. Frame pacing is real time by default; `--pacing unlimited`
is useful for stress tests and changes the relation to wall-clock delay.

The default image is an original XEX that reads Atari input and RANDOM
registers. `--image` accepts a local boot image. `--disk-rw` generates an
original bootable ATR that repeatedly writes input-dependent data to
sector 4, reads it into another buffer, and compares every byte. Its
marker and guest error counters must remain valid. No game download or
external lobby is involved.

Normal runs reject every desync or resync. `--resync-test` deliberately
changes one host RAM byte after the normal interval, requires exactly one
resync, and continues changing input for at least 1000 further frames.
It also checks that the lobby session identity and endpoint survive, and
that Leave restores the original disk, cartridge and cassette state.
