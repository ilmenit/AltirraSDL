# River Raid — analysis TODO

## Current phase
Phase 8: Hand off — complete.

## Done
- [x] Phase 0: read PROMPT.md, confirm prerequisites
- [x] Phase 1: static XEX parse, segment list, sha256 baseline
  - 3 segments: main $4080-$60FF (8320B), init $0400-$0419 (26B), INITAD→$0400
  - SHA-256: 3924596cf04e1c7f97af9688a125c8a7e0b0d686dbaa40820c812cc1c406e704
  - Self-relocating: copies $4100-$60FF → $A000-$BFFF, game lives at $A000-$BFFF
- [x] Phase 1.5: Fix bridge server — implemented CONFIG command for BASIC disable
  - Added CONFIG bridge command (basic/machine/memory/debugbrkrun)
  - Added --no-basic CLI flag to AltirraBridgeServer
  - Fixed headless server build (oshelper_stubs.cpp)
  - Rebuilt server, verified River Raid boots with BASIC disabled
- [x] Phase 2: boot + title snapshot
  - Title screen captured, RAM dumped
  - Game uses custom DLI kernel at $B500 (scanline multiplexer)
  - VBI at $B2A5 handles sound + game state
  - Display list at $3F00, 172 entries, ANTIC mode 14
- [x] Phase 3: profiling
  - Flat insns profile: DLI at $B500 consumes 37% of CPU
  - Hot loop $B567 (WSYNC per-scanline): 22% of CPU
  - Call-graph captured, 23 nodes identified
  - 29 JSR subroutines, 35 JMP targets catalogued
  - 4329 instructions disassembled in full $A000-$BFFF range
- [x] Phase 4: recursive descent — 4329 instructions disassembled, code/data classified
- [x] Phase 5: variables and labels — 70+ ZP vars mapped, 6-slot entity table, full HW register map
- [x] Phase 6: topical analysis — findings.md, game_manual.md, 10 notes files
- [x] Phase 7: byte-perfect round-trip — SHA-256 verified match
- [x] Phase 8: hand off

## Open questions for the user
(none — analysis complete)

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
