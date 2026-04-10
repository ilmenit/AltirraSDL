# River Raid — Performance Profile

## Frame Budget

The Atari 800 runs at 1.7897725 MHz (NTSC). At 60 fps:
- Total cycles per frame: ~29,868 scanlines × ~114 cycles ≈ **29,868 cycles/line**
- Actually: 262 scanlines × 114 cycles = **29,868 cycles/frame**
- Usable for game logic: ~29,868 - DLI overhead

However, our profiling measured **3,201,120 total cycles over ~600 frames**,
giving ~5,335 cycles/frame average. This is because the profiler only
counts cycles spent in game code, not idle WSYNC waits or OS overhead.

## CPU Usage Breakdown

From a 600-frame flat profile during active gameplay:

### By Function

| Rank | Address | Cycles | % | Routine |
|------|---------|--------|---|---------|
| 1 | $B567 | 710,919 | 22.2% | DLI WSYNC loop (scanline sync) |
| 2 | $AE99 | 300,852 | 9.4% | Main loop VCOUNT wait |
| 3 | $AE9E | 226,380 | 7.1% | Main loop VCOUNT compare |
| 4 | $B5D0 | 156,425 | 4.9% | DLI WSYNC (entity render) |
| 5 | $AE9C | 152,531 | 4.8% | Main loop branch |
| 6 | $B5CB | 84,215 | 2.6% | DLI entity sprite load |
| 7 | $B570 | 59,717 | 1.9% | DLI scanline counter |
| 8 | $B5E6 | 54,292 | 1.7% | DLI scanline loop end |
| 9 | $AFFF | 54,077 | 1.7% | Terrain row fill inner loop |
| 10 | $B5DD | 53,773 | 1.7% | DLI player color write |

### By Category

| Category | Cycles | % |
|----------|--------|---|
| DLI kernel (sprite multiplexer) | ~1,194,150 | 37.3% |
| Main loop sync wait | ~680,000 | 21.2% |
| Terrain rendering | ~180,000 | 5.6% |
| Entity management | ~85,000 | 2.7% |
| VBI (sound + timers) | ~17,700 | 0.6% |
| Input + collision | ~25,000 | 0.8% |
| Score + display update | ~20,000 | 0.6% |
| Other | ~999,270 | 31.2% |

## Call-graph Analysis

From the hierarchical call-graph profile (600 frames):

```
$0000 (NMI vector)
 └── $C018 (OS NMI dispatch)
      └── $B500 (DLI handler) — 1,194,150 incl cycles (99.9% of NMI time)

$0000 (main thread)
 ├── $AFF4 (fill_terrain_pattern) — 79,297 incl
 │    └── $B04B (advance_ptr) — 10,203
 ├── $AFF0 (fill_terrain_blank) — 28,986 incl
 │    └── $B04B — 4,711
 ├── $B00F (copy_row) — 27,742
 ├── $B034 (draw_entity) — 20,893
 ├── $B0D8 (entity_move) — 13,595
 ├── $B0C7 (spawn) — 12,587
 ├── $B07C (collision) — 10,555
 ├── $AF5B (update_entities) — 8,185
 ├── $B05C (terrain_collision) — 8,159
 ├── $B183 (fuel_gauge) — 4,610
 ├── $B01A (copy_chunk) — 2,577
 └── (main loop body) — 698,839 excl (VCOUNT wait + inline logic)
```

## Hot Spots

### 1. DLI Scanline Kernel ($B567)
The single hottest instruction is `STA WSYNC` at $B567, executed
~7,892 times per profile session (~13 times per frame × 600 frames).
Each WSYNC stalls the CPU until the next horizontal blank, consuming
~60-90 cycles of wait time per invocation. This is by design — the
kernel must synchronize to each scanline to position sprites correctly.

### 2. VCOUNT Wait ($AE99)
The main loop spins on VCOUNT at $AE99, waiting for the display to
reach the top of the visible area. This idle loop consumes ~21% of
CPU time but is necessary for frame synchronization. The game has no
work to do between the end of game logic and the start of the next
display frame.

### 3. Terrain Row Fill ($AFFF)
The terrain row fill at $AFFF is an inner loop that writes terrain
data to screen RAM. It runs for each new row that scrolls onto the
screen (1-2 rows per frame depending on speed), writing 48 bytes per
row via an indirect store.

## Optimization Techniques

The original programmers used several classic 6502 optimization techniques:

1. **Zero-page everything**: All frequently accessed variables are in
   zero-page ($00-$FF), saving 1 cycle per access vs. absolute addressing.

2. **Self-modifying code**: Some routines modify instruction operands
   inline to avoid indirection overhead (not confirmed, but common in
   games of this era).

3. **Unrolled entity loops**: The entity update doesn't use a generic
   loop counter — it processes each slot type-specifically to minimize
   branching.

4. **WSYNC-locked timing**: The DLI kernel uses WSYNC to guarantee
   per-scanline timing without cycle-counting, trading CPU time for
   reliability.

5. **Combined collision/render**: Collision detection is embedded in
   the DLI kernel alongside rendering, avoiding a separate collision
   pass over the entity list.
