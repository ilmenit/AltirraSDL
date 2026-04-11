# River Raid — analysis TODO

## Current phase
Phase 9: **Reopened 2026-04-11.** A re-read of the split asm files in `/home/ilm/Documents/GitHub/river_raid_duo/src/` while preparing a simultaneous-2P fork uncovered several mislabelled functions and variables and several active sprite usages that the original analysis missed. The "phase 8 complete" status was premature. Phase 9 = bridge verification of the corrections and answering the new open questions before any 2P code is written.

## Done
- [x] Phase 0: read PROMPT.md, confirm prerequisites
- [x] Phase 1: static XEX parse, segment list, sha256 baseline
- [x] Phase 1.5: Fix bridge server — implemented CONFIG command for BASIC disable
- [x] Phase 2: boot + title snapshot
- [x] Phase 3: profiling
- [x] Phase 4: recursive descent — 4329 instructions disassembled, code/data classified
- [x] Phase 5: variables and labels (with caveats — see corrections below)
- [x] Phase 6: topical analysis — findings.md, game_manual.md, 10 notes files
- [x] Phase 7: byte-perfect round-trip — SHA-256 verified match
- [x] Phase 8: hand off (premature — see below)

## Reopened 2026-04-11

The DUO 2-player port re-read uncovered that several phase 5/6
labels were wrong. Specifically (also documented in findings.md
header note, notes/sprites.md "What was wrong" §11, and
project.json `notes` array):

- Sprite map was incomplete: M0 has an active writer in
  frame_sync.asm at $AEF1; M2 is positioned at jet_x+4 in dli.asm
  at $B539 as a hit probe. The original "M0/M2/M3 cleared" claim
  was based on incomplete static analysis that missed the
  read-modify-write M0 writer.
- `process_collision_results` ($B07C), `check_terrain_collision`
  ($B05C), and several siblings are misnomers — they're entry
  points into a digit-rendering helper, not collision handlers.
- `play_score_sound` ($B1C0), `update_lives_display` ($B21A),
  and the helpers at $B221/$B223 are misnomers — they touch no
  POKEY register and no lives display.
- `lives_count = $002A` is a countdown timer that triggers
  `player_death`, not a visible life count.
- `player_death` ($A688) is a 5-instruction hook, not the real
  death handler.

The labels in `project.json` have been left in place for backward
compatibility, but the comments now flag every known misnomer.
The notes files have been updated with **CORRECTED** annotations
inline. Phase 9 (bridge verification) is needed to resolve the
remaining open questions before label renames can be applied.

## Open questions reopened 2026-04-11

These are the items to verify in the bridge during phase 9. Each one
has a known investigation method. Cross-reference §16 of
`/home/ilm/Documents/GitHub/river_raid_duo/DUO_DESIGN_DOC.md` for the
DUO-specific framing.

- **What is M0 actually drawing?** `frame_sync.asm:52-59` writes M0
  bits at `$0B00+frame_ctr_copy` gated by `$C1`, with COLPM0 cycled
  through `player_color_cycle` every frame. The motion loop in
  `bullets_terrain.asm:6-65` walks `$C1` like a sprite coordinate.
  Method: bridge runtime DMA sweep on `$0B00-$0BFF` bits 0-1 across
  many frames in different game states; correlate with on-screen
  pixel at HPOSM0.
- **Where do M2 bits come from?** The DLI positions M2 at
  `player_sprite_x + 4` and `$1C` captures M2 vs P3 collisions, so
  M2 must have bitmap bits at the jet's Y row. Static analysis
  hasn't found the writer. Method: bridge runtime DMA sweep on
  `$0B00-$0BFF` bits 4-5 with the jet visible.
- **What does `play_score_sound` ($B1C0) actually do?** It touches
  no POKEY register. It manipulates `$4C`/`$23`/`$7A`/`$7B` state
  flags and stores `$FE → fuel_level` and `$00 → lives_count`.
  Method: trace from a known caller in the bridge; see what state
  transition follows.
- **What does `update_lives_display` ($B21A) actually do?** It
  stores `$FF → frame_counter` and falls into the same $B200 block
  as $B1C0. Method: same approach as above.
- **What is the real lives counter address?** `$002A` is a
  countdown timer (decremented every 8 frames, reset to $25, fires
  `player_death`). The visible lives count is somewhere else.
  Method: watchpoint every ZP byte during a deliberate crash; the
  byte that decrements when the on-screen lives icon visibly
  disappears is the real one.
- **What is the real death handler entry point?** `player_death` at
  $A688 is a 5-instruction hook that toggles `player_number` and
  jumps to `state_start_game`. The actual death processing
  (explosion, lives decrement, level reset) is scattered. Method:
  trace from the `$1B`/`$1D` collision-byte readers in
  `entities.asm` forward.
- **Which `$1A-$22` byte is the fuel-depot capture?** None of the
  current DLI captures obviously corresponds to fuel-depot
  collision. Method: deliberately fly into a depot, sample all
  collision bytes on the frame of the refill.
- **Is `fuel_level` really fuel?** The `$B200` block stores `$FE`
  there from a non-fuel path. Either dual-purpose or mislabelled.
  Method: watchpoint during fuel drain.
- **Is `frame_counter` really a frame counter?** Sound.asm:58
  stores `$FF` there explicitly. Probably not monotonic. Method:
  watchpoint and observe.
- **Is the in-flight bullet M0 or M1 or both?** The original
  analysis observed "the M1 band" growing from the at-rest position.
  But M0 is the slot with the actively-driven motion loop. They
  might be the same observation interpreted two ways. Method:
  separately sweep M0 bits and M1 bits frame-by-frame with fire
  held.

## Open questions for the user
(none — work proceeds via the DUO design doc phase 1 verification plan)

## Coverage
- bytes classified: 8192 / 8192 (100% of game code range $A000-$BFFF)
- routines labelled: 29 subroutines + 35 JMP targets + 2 interrupt handlers = 66
- variables named: 70+ zero-page + 30+ RAM + 20+ hardware registers
- notes written: 10 topical files + findings.md + game_manual.md

## Deliverables
- `river_raid.pyA8/findings.md` — technical analysis summary
- `river_raid.pyA8/game_manual.md` — non-technical player guide
- `river_raid.pyA8/notes/` — 10 topical deep-dive documents
- `river_raid.pyA8/project.json` — segment list, vectors, metadata
- `river_raid.pyA8/regions.json` — byte classification
- `river_raid.pyA8/comments.json` — per-address commentary
- `river_raid.pyA8/exports/main.asm` — MADS assembly source
- `river_raid.pyA8/exports/river_raid_rebuilt.xex` — byte-perfect rebuild
- `river_raid.pyA8/snapshots/` — 20+ screenshots, RAM dumps, profiles
- `ANALYSIS_TODO.md` — this checklist
