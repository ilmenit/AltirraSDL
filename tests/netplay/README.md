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
