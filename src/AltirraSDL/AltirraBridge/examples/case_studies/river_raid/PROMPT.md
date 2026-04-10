# Prompt: reverse-engineer river_raid.xex with AltirraBridge

You are reverse engineering expert. Your task is to reverse-engineer the Atari
8-bit binary at `examples/case_studies/river_raid/river_raid.xex` to the
same depth as the `examples/case_studies/behind_jaggi_lines/` and
`examples/case_studies/wizard/` references, and produce a
MADS-assemblable source file that reassembles to a byte-identical
copy of the original.

This is a long-running, multi-step task. Pace yourself, save
intermediate state often, and treat the bridge server as your
primary instrument.

## Working agreement with the user

Assume the user is **not** an experienced reverse engineer or 6502
programmer. They are running this prompt to get the analysis; they
are not here to help you decode instructions. Consequences:

- Explain what you're doing before you do it, in plain language.
- If you are truly stuck — an indirect jump you can't resolve, an
  unlabelled data region you can't classify, a routine whose purpose
  you can't infer — **ask the user for help**, but phrase the
  question so a non-expert can answer. Don't ask "is this a MUL16
  or a MUL8?"; ask "I see a loop at $6200 that takes two numbers
  and produces a bigger one — do you remember seeing anything in
  the game that multiplies, like a score calculation or a position
  update?"
- If the user asks you to stop and pick up later, save your state
  and emit a clear "resume here" checkpoint in `ANALYSIS_TODO.md`
  (see below).
- If a sub-phase is getting too big, split it explicitly: "Phase 4
  is larger than expected; I'm going to stop here, commit partial
  labels, and continue with the remaining routines in the next
  pass." Don't just push through silently.

You may **refuse to speculate**. If you don't know whether a byte
is code or data, say so in `findings.md` under "open questions"
and leave the region unclassified until you have a confirming
observation.

## Hard success criteria

The task is **not done** until all of these are true:

1. **Byte-perfect MADS round-trip.** The reassembled binary must
   be identical to the original. Verify by comparing cryptographic
   hashes. Pick whichever of these works on your OS — they all
   produce the same SHA-256:

   Linux / macOS / Git Bash on Windows:

       sha256sum river_raid.xex river_raid_rebuilt.xex

   Cross-platform (CMake is on every developer box that can build
   this project):

       cmake -E sha256sum river_raid.xex river_raid_rebuilt.xex

   Windows Command Prompt:

       certutil -hashfile river_raid.xex       SHA256
       certutil -hashfile river_raid_rebuilt.xex SHA256

   Or raw byte compare, also OS-independent:

       cmake -E compare_files river_raid.xex river_raid_rebuilt.xex

   Windows `fc /b` also works for the byte compare:

       fc /b river_raid.xex river_raid_rebuilt.xex

   Pure-Python fallback (no shell tools needed):

       python -c "import hashlib; print(hashlib.sha256(open('river_raid.xex','rb').read()).hexdigest())"

   Anything less than a full match — even a one-byte mismatch — is
   failure. Keep iterating until the comparison passes.

2. **100% byte coverage** in `regions.json`. Every byte in every
   loaded segment is classified as code, procedure, data, or one
   of the recognised data sub-types (display list, character set,
   sprite data, sound table, address table, zero-fill, string).
   Zero unclassified bytes.

3. **Every subroutine has a label and a comment.** A second
   analyst opening the project should immediately understand what
   each routine does without re-reading the bytes. For routines
   that form a logical unit (a full game system, an interrupt
   handler, a renderer), mark the range as a **procedure region**
   so an IDE or reader sees the whole block as one thing.

4. **Every persistent variable has a label and a description** in
   `notes/variables.md`. Zero-page bytes used by game logic, shadow
   registers the game overrides, RAM variables for scores, lives,
   positions, timers — all named and described.

5. **Topical analysis** in `notes/` that matches the depth of the
   bjl reference. At minimum: `overview.md`,
   `display_list_architecture.md`, `vbi_architecture.md`,
   `subroutine_map.md`, `variables.md`, `performance_profile.md`,
   plus game-specific topic files covering the systems you
   discover (scrolling, collision, scoring, enemy AI, sound,
   input, level progression, state machines — whichever apply).
   Each is a real prose document, not a stub.

6. **`findings.md` matches the depth of `wizard/wizard.pyA8/findings.md`.**
   That reference is ~450 lines of structured notes: game overview,
   core gameplay loop, win condition, loading sequence, full state
   machine dispatch tables, gameplay frame breakdown, scrolling
   engine, entity system, collision model, sound engine, scoring.
   Use it as a concrete template — your `findings.md` should
   resemble it in structure and level of detail, adapted to the
   mechanics river_raid actually has.

7. **`game_manual.md` matches the depth of `wizard/wizard.pyA8/game_manual.md`.**
   That reference is ~300 lines of prose written for a
   **non-technical reader** — game overview, controls, modes of
   play, scoring rules, power-ups, enemies, strategy tips. No hex
   addresses, no disassembly snippets. Somebody who has never
   opened a debugger should be able to read it and understand how
   to play the game. Write yours the same way, based on what you
   observe from actually driving the game through the bridge.

8. **`ANALYSIS_TODO.md` exists and is up to date.** See the next
   section — this is a live checklist, not a post-hoc document.

9. **`findings.md` ends with a clear "Status" section** listing
   what's done, what's left, and any open questions you
   couldn't fully resolve.

If you cannot reach all nine criteria, **do not declare done**.
Stop, update `ANALYSIS_TODO.md` with what's left, and ask the
user how to proceed.

## ANALYSIS_TODO.md — live checklist

**Create this file first, before you do anything else.** It lives
at `examples/case_studies/river_raid/ANALYSIS_TODO.md` and is your
running checklist plus progress log. Format:

```markdown
# River Raid — analysis TODO

## Current phase
(one line: which phase you're in, what you're doing right now)

## Done
- [x] Phase 0: read PROMPT.md, confirm prerequisites
- [x] Phase 1: static XEX parse, segment list, sha256 baseline
- [ ] Phase 2: boot + title snapshot
- ...

## Open questions for the user
(empty until you hit something you need help with)

## Coverage
- bytes classified: N / total (P%)
- routines labelled:  N
- variables named:    N
- notes written:      N files
```

Update it:
- at the start of every phase (mark the phase "in progress"),
- whenever you finish a piece of work (tick it off),
- whenever you hit something you can't resolve (add it under
  "open questions"),
- before any long-running sub-task, so if the context resets the
  next pass knows exactly where to resume.

The TODO file is the single source of truth for "where are we".
`findings.md` is the summary of what was learned; `ANALYSIS_TODO.md`
is the state of the work.

## Prerequisites the user must satisfy before you start

- `river_raid.xex` exists at the path above. If it doesn't, stop
  and tell the user — do not attempt to download or synthesise
  one; the user must provide it from their own legal copy.
- `AltirraBridgeServer` (or `AltirraSDL --bridge`) is running and
  the token-file path is available. If not, ask the user to start
  it; do not start it yourself unless the user has explicitly
  authorised that.
- The `altirra-bridge` Claude Code skill is loaded. If not, run:

      python -m altirra_bridge.install_skills

  and re-launch Claude Code so the skill is discovered. The skill
  is your primary reference for *how to use the SDK*; consult its
  `references/python-quickstart.md` and `references/workflows.md`
  whenever you need to remember an API.
- MADS (the 6502 cross-assembler) is on PATH. Verify with `mads`
  — it should print a usage banner. If not present, the
  round-trip step at the end will fail; install it from
  https://github.com/tebe6502/Mad-Assembler/releases first.
- Python 3.9+ with the `altirra_bridge` package on `PYTHONPATH`.

## Your toolkit — extend it as needed

You have three kinds of tools:

1. **The bridge server** (via the Python or C SDK). Consult
   `skills/altirra-bridge/SKILL.md` for the full command list.
   Commands you will rely on heavily:

   | Command              | When to use it                                    |
   |----------------------|---------------------------------------------------|
   | `BOOT`, `FRAME`      | Load the binary and advance deterministically.   |
   | `MEMDUMP`, `PEEK`    | Snapshot RAM for offline analysis.                |
   | `DISASM`             | The canonical 6502 disassembler. Do not write your own. |
   | `HISTORY`            | Pause the sim, dump the last N executed instructions. The single most powerful primitive the bridge exposes. |
   | `CALLSTACK`          | "Where did this code get called from?"            |
   | `EVAL`               | Debugger expression evaluator: memory deref, registers, symbols, arithmetic. |
   | `BP_SET` (conditional), `WATCH_SET` | Halt on PC, on a condition, or on a memory access. |
   | `PROFILE_START`/`STOP`/`DUMP`/`DUMP_TREE` | Find hot routines and reconstruct the call tree. |
   | `MEMSEARCH`          | Find byte patterns — JSR targets, magic numbers, message strings. |
   | `SYM_LOAD`, `SYM_LOOKUP` | Once you have labels, push them back to Altirra so future `DISASM` output uses them. |
   | `BANK_INFO`, `MEMMAP`, `CART_INFO` | Banking / memory layout. |
   | `PMG`, `AUDIO_STATE` | Decoded sprite + POKEY state, so you don't re-derive chip semantics. |
   | `SCREENSHOT`         | Capture state at decision points. Pin them in `findings.md`. |

2. **`altirra_bridge` Python package modules.** Use them rather
   than reimplementing:
   - `altirra_bridge.loader.parse_xex` — XEX segment parser.
   - `altirra_bridge.Project` — persistent project state (labels,
     comments, notes).
   - `altirra_bridge.asm_writer.emit_xex` — MADS source exporter
     that uses bridge `DISASM` for the instruction text.

   The reference implementation to aspire to is
   `/home/ilm/Documents/GitHub/pyA8/asm_writer.py` (the original
   pyA8 toolkit for the Atari800 emulator). That asm_writer:
   - Emits **real 6502 mnemonics** for code regions (not `.byte`)
   - Emits `.byte`/`.word`/`.ds` directives for data regions
   - Uses `opt h-` with bracket ORG notation for byte-exact XEX
     headers (including `$FFFF` marker placement)
   - Generates per-segment files with `icl` includes
   - Wraps procedures in `.proc`/`.endp`
   - Has built-in MADS verification (`verify()`)
   If the `altirra_bridge.asm_writer` doesn't support a feature
   you need, **extend it** — don't work around it by emitting
   raw bytes manually.

3. **Your own Python helper script** — create it at
   `examples/case_studies/river_raid/deep_analyze.py` (mirroring
   `examples/case_studies/behind_jaggi_lines/deep_analyze.py` from
   the bjl reference) and **extend it iteratively throughout the
   analysis**. Anything you find yourself doing by hand more than
   twice should go into this script: custom memory scanners,
   cross-reference builders, region classifiers, label batch
   importers, report generators. The script is the reproducible
   record of your analysis — a second analyst should be able to
   run it from a fresh `river_raid.xex` and regenerate the same
   `river_raid.pyA8/` artifacts.

   The bjl `deep_analyze.py` is a good concrete example: it opens
   a `Project`, boots the emulator, snapshots memory, declares the
   game-specific labels and data regions inline as Python code,
   and writes everything back to the project. Copy its shape,
   adapt its content.

   If a helper you need **doesn't exist yet** in the
   `altirra_bridge` package — for example a region-marking helper
   for procedure regions, an xrefs builder, or a call-graph
   extractor — write it. Put general-purpose utilities in the
   package (contribute them back); keep game-specific knowledge in
   your `deep_analyze.py`.

## The two-audience rule: humans AND machines

Your analysis produces two kinds of output. **Both must be
complete.** Neglecting either is a failure.

1. **Human-readable documents** (`findings.md`, `game_manual.md`,
   `notes/*.md`). Prose that a second analyst reads to understand
   the game. Contains hex addresses, tables, architecture
   explanations.

2. **Machine-readable project state** (`project.json` via the
   `Project` object, `regions.json`, `comments.json`,
   `call_graph.json`, `xrefs.json`). Structured data that
   `asm_writer.emit_xex()` reads to produce the labelled,
   commented `.asm` output.

**Every label you discover goes into `proj.label(addr, name)`.
Every comment goes into `proj.comment(addr, text)`.** Writing a
label only in `variables.md` or `subroutine_map.md` but not in
the Project is a bug — the `.asm` output won't have it.

The `.asm` file is the **primary deliverable**, not the markdown.
The markdown explains the analysis; the `.asm` IS the analysis
in executable form. A reader should be able to open the `.asm`
and see:

```asm
cold_start:
    lda #$00                    ; disable all hardware
    sta NMIEN
    sta IRQEN
    sta GRACTL
    sta DMACTL
```

NOT:

```asm
    .byte $A9, $00              ; $A000: lda #$00
    .byte $8D, $0E, $D4        ; $A002: sta NMIEN
```

If the `.asm` output has `.byte` where there should be mnemonics,
the code region wasn't classified or `asm_writer` wasn't used.
Code regions MUST be disassembled through `bridge.disasm()` and
emitted as real 6502 instructions. Data regions (character sets,
sprite graphics, lookup tables) MUST be emitted as `.byte` /
`.word` / `.ds` with appropriate comments.

## Project file formats

Your `river_raid.pyA8/` directory holds the analysis state. The
layout mirrors the references:

```
river_raid.pyA8/
├── project.json      ← segment list, metadata, SHA-256 baseline
├── regions.json      ← per-byte classification (see below)
├── comments.json     ← {address -> comment} map
├── xrefs.json        ← {target -> [callers]} cross-references
├── call_graph.json   ← call relationships, for `PROFILE_DUMP_TREE` enrichment
├── findings.md       ← the narrative summary (wizard-depth)
├── game_manual.md    ← the non-technical player guide (wizard-depth)
├── notes/            ← topical deep-dives
│   ├── overview.md
│   ├── display_list_architecture.md
│   ├── vbi_architecture.md
│   ├── subroutine_map.md
│   ├── variables.md
│   ├── performance_profile.md
│   └── <game-specific topics>.md
├── snapshots/        ← RAM dumps, screenshots (LOCAL ONLY, gitignored)
└── exports/          ← MADS source (LOCAL ONLY, gitignored)
```

### `regions.json` — region kinds

Every byte in every loaded segment is classified with `type` +
optional `hint`. The kinds used by the reference case studies:

- `{"type": "code"}` — instructions, reachable by the
  disassembler.
- `{"type": "proc", "name": "..."}` — a **procedure region**:
  a contiguous range that forms one logical subroutine or system.
  Use this to group related code so `asm_writer` emits a single
  labelled block and a reader sees the procedure as a unit. This
  is the analog of pyA8's `.proc` — if the current
  `altirra_bridge.Project` helper doesn't provide a method for
  it, add one (it's a thin wrapper; just a list of
  `{start, end, name}` records).
- `{"type": "data", "hint": "bytes"}` — unstructured data.
- `{"type": "data", "hint": "string"}` — ASCII / ATASCII text.
- `{"type": "data", "hint": "charset"}` — an ANTIC character set
  (1024 bytes, 128 glyphs × 8 rows).
- `{"type": "data", "hint": "addr_table"}` — a jump / dispatch
  table: pairs of little-endian 16-bit pointers.
- `{"type": "data", "hint": "display_list"}` — an ANTIC display
  list.
- `{"type": "data", "hint": "sprite"}` — PMG data.
- `{"type": "data", "hint": "sound_table"}` — AUDF/AUDC tables
  for POKEY.
- `{"type": "data", "hint": "fill_zero"}` — zero padding.

Add new `hint` values as you discover data you can't fit into the
above. Record every new hint in `findings.md` under "data
conventions".

## Methodology — work through these phases in order

Phases are steps in the workflow, not time boxes. Some are short,
some take many iterations. The goal is coverage, not speed.

### Phase 0 — set up

1. Read this prompt to the end before you start. Confirm all
   prerequisites are satisfied. If any are missing, stop and ask
   the user.
2. Create `ANALYSIS_TODO.md` with the phase list from this
   document and mark Phase 0 complete.
3. Create `examples/case_studies/river_raid/deep_analyze.py` as
   an empty scaffold that opens a `Project` and imports the
   modules you expect to use. You will extend it throughout.

### Phase 1 — static structure

1. Parse the XEX header with `altirra_bridge.loader.parse_xex`.
   Record every segment's `(start, end, size)` and the `runad` /
   `initads` vectors. Write them to `findings.md` as the first
   section.
2. Compute the SHA-256 of `river_raid.xex` (using one of the
   cross-platform methods above) and pin it in `findings.md` and
   in `project.json`. The round-trip step at the end compares
   against this value.
3. Initialise the `Project` skeleton and commit empty
   `regions.json`, `comments.json`, `xrefs.json`, `call_graph.json`.
4. Update `ANALYSIS_TODO.md`.

### Phase 2 — boot and snapshot

1. `bridge.boot('river_raid.xex')`, then `bridge.frame(N)` for
   however many frames it takes the game to finish loading and
   reach the title screen. Start conservative and increase until
   `bridge.regs()` stabilises.
2. `bridge.screenshot()` → `title_screen.png`. Pin it in
   `findings.md`.
3. Dump full RAM:
   `bridge.memdump(0x0000, 0x10000)` →
   `river_raid.pyA8/snapshots/title.bin`. **Do not commit
   binaries** to git — the `.gitignore` in this directory
   excludes `snapshots/`.
4. Capture `bridge.regs()`, `bridge.bank_info()`, `bridge.memmap()`,
   `bridge.cart_info()`. River Raid is a simple XEX but confirm
   the banking state before assuming anything.
5. Advance more frames, screenshot again, diff. The bytes that
   changed between snapshots are usually screen RAM and frame
   counters — useful as an early classifier for "this is
   display-side state".

### Phase 3 — profiling

1. Drive the game from the title screen into active gameplay by
   injecting joystick / console / keyboard input. Use `bridge.joy`,
   `bridge.consol`, `bridge.key`. If you don't know which inputs
   the game wants, try the common ones (`consol(start=True)`,
   then cycle through the joystick directions) and read the
   screen back each time. If nothing works, ask the user.
2. Once you're in gameplay, run a call-graph profile:

       bridge.profile_start(mode='callgraph')
       bridge.frame(LOTS)      # long enough to see the game loop repeat
       bridge.profile_stop()
       tree = bridge.profile_dump_tree()

3. Save the merged hot list and the tree as JSON under
   `river_raid.pyA8/snapshots/`.
4. Run a flat insns-mode profile too:

       bridge.profile_start(mode='insns')
       bridge.frame(LOTS)
       bridge.profile_stop()
       hot = bridge.profile_dump(top=500)

5. From the profile output, identify candidate entry points:
   routines with the largest inclusive cycle counts are main
   loops or dispatchers; routines with the largest exclusive
   cycles are inner-loop hot spots. Record each candidate in
   `ANALYSIS_TODO.md`.

### Phase 4 — recursive descent

For each entry point from Phase 3, deepest first by inclusive
cycles:

1. `insns = bridge.disasm(addr, count=N)` — disassemble far
   enough to see the routine's shape.
2. Walk the disassembly. For every `JSR` target, every `JMP`
   target, every branch destination — add it to a worklist.
3. Process the worklist breadth-first. Mark each visited range
   as `type: code` in `regions.json`. When you recognise a full
   subroutine, upgrade it to `type: proc` with a meaningful name.
4. Every memory access (`LDA $XXXX`, `STA $XXXX`) is recorded in
   a side table as you go. Categorise:
   - Hardware registers ($D000-$D7FF) → standard Atari names
     (`COLPF0`, `WSYNC`, `KBCODE`, …). These are free labels.
   - OS shadows ($0200-$03FF) → standard Atari names (`COLOR0`,
     `SDLSTL`, `SDMCTL`, …).
   - Game RAM (everything else) → anonymous for now; named in
     Phase 5 after observing runtime behaviour.

   **Add every label to the Project immediately:**
   ```python
   proj.label(0xD40A, "WSYNC")
   proj.label(0xA000, "cold_start")
   proj.comment(0xA000, "disable all hardware, clear RAM")
   proj.save()
   ```
   Do NOT just write labels in markdown — the Project is the
   source of truth for the `.asm` export.

5. Periodically push your accumulated labels back to Altirra as
   a `.lab` file (`proj.export_lab(path)`) and `bridge.sym_load`
   them. Subsequent `DISASM` output will then contain the symbolic
   names, massively improving readability.
6. **Use `HISTORY` aggressively when static analysis stalls.**
   If you can't tell what a block does from the bytes alone, set
   a breakpoint on its entry address, advance frames until it
   hits, then `bridge.history(N)` to see the exact register and
   memory state leading into it. This is faster than any amount
   of guesswork.

Stopping condition for this phase: your worklist is empty and
your code coverage has plateaued. If coverage is well below 100%
of the loaded segments, look for:
- Indirect jump tables (`JMP ($XXXX)`, `JSR` via a pointer in
  RAM). Resolve targets with `bridge.eval_expr('dw($XX)')` at
  runtime.
- Self-modifying code (uncommon in ROMs but possible).
- Code only reached after specific game events (level
  transitions, death, high-score entry, game over). Drive the
  game to that state and re-profile.

### Phase 5 — variables and labels

For every memory address the code reads or writes:

1. **Cross-reference how it's used.** A byte written only by
   one routine and read only by another is a single-purpose
   variable owned by that pair. A byte read in a VBI handler and
   written by the main loop is a VBI-consumed signal. Describe
   the role, then name it.
2. **Confirm with runtime observation.** Use `WATCH_SET` to halt
   when the byte changes, check `CALLSTACK` to see who's
   writing, and use snapshot-diff classification (`bridge.memdump`
   before and after an in-game event) to find variables that
   change predictably — score increments when you shoot
   something, fuel decreases per frame, lives decrement on
   collision.
3. Name the variable only after you have observed the behaviour.
   A name like `player_x` is a hypothesis until you confirm the
   byte actually changes when the player moves.
4. **Add every confirmed variable to the Project:**
   ```python
   proj.label(0x76, "fuel_level")
   proj.comment(0x76, "fuel gauge: $FF=full, decrements to $00=empty")
   ```
5. Write everything to `notes/variables.md` in the per-range
   table format used by the bjl reference. **The markdown table
   and the Project labels must agree** — if a variable is in
   `variables.md`, it must also be in `proj.labels`.

### Phase 6 — topical analysis

Write a prose document per game subsystem. Required files:

- **`overview.md`** — game identity, memory map summary,
  segment list, 1-2 paragraphs on the main loop, and a high-level
  rendering approach description.
- **`display_list_architecture.md`** — what ANTIC display lists
  are used, which modes, where DLIs fire. Use `bridge.dlist()` to
  dump and decode each live display list.
- **`vbi_architecture.md`** — VBI vector (VVBLKD / VVBLKI)
  hook-up, what runs in immediate vs deferred, cycles consumed.
- **`subroutine_map.md`** — a flat table of every named routine
  with address range, callers, callees, one-line purpose.
- **`variables.md`** — the product of Phase 5.
- **`performance_profile.md`** — where the cycles go. Top hot
  routines from Phase 3, cycle percentage of frame budget,
  non-obvious optimisations the author used.

Game-specific files (write one per system you identify — do not
force topics that don't apply, do not omit topics that do):

- **River / terrain**: how the river is drawn, scrolled,
  procedurally generated, or memory-mapped. How the banks of
  the river encode obstacles.
- **Enemies**: spawn, AI, animation, destruction, scoring per
  type. River Raid has jets, helicopters, ships, balloons, fuel
  depots — each may have different behaviour.
- **Collisions**: which hardware collision registers (GTIA
  P0PF / P1PF / … or software), how a hit translates to
  destruction or death, invulnerability frames.
- **Fuel system**: if applicable — how fuel drains, how refuels
  work, what happens at empty.
- **Scoring**: score format (BCD or binary), multipliers,
  extra-life thresholds, high-score storage.
- **Sound engine**: POKEY channel allocation, music data
  format, sound-effect triggering.
- **Input**: joystick + console-switch handling, debounce.
- **State machines**: title screen → game → death → game over →
  high-score entry transitions, with the dispatch tables.
- **Level progression**: if applicable, how difficulty scales.

Each document is **prose with tables and hex addresses where
useful**, not a bullet list. The bjl reference's
`rendering_pipeline.md` is 413 lines; match that depth where
the game has equivalent complexity.

Simultaneously write `findings.md` as the narrative summary.
Use `wizard/wizard.pyA8/findings.md` as a **concrete template**:

- Status line (segments, labels, routines, banks).
- Game overview (what the game is, core gameplay loop).
- Win condition / scoring model.
- Loading sequence table.
- State-machine table(s) with state number → handler address →
  purpose.
- Per-frame gameplay breakdown.
- Subsystem sections matching the notes files.

And write `game_manual.md` as a **non-technical player guide**.
Use `wizard/wizard.pyA8/game_manual.md` as the concrete template:

- Overview paragraph.
- Controls section.
- Modes of play / gameplay modes.
- Enemies and hazards, each described in English.
- Scoring rules, power-ups, extra lives.
- Strategy tips.
- **No hex addresses. No disassembly. No talk of registers.**
  Someone who has never opened a debugger must be able to read
  it and understand how to play the game.

Base the manual on what you actually observe by driving the
game, not on what you infer from the code. Take screenshots at
each distinct gameplay situation and pin them in the manual.

### Phase 7 — MADS export and round-trip

This is the hard test — the binary equality check.

**What the output `.asm` must look like:**

Code regions contain **real 6502 mnemonics**, not `.byte`:
```asm
cold_start:
    lda #$00                    ; disable all hardware
    sta NMIEN
    sta IRQEN
    sta GRACTL
    sta DMACTL
    tay
    sta PBCTL
```

Data regions contain `.byte` / `.word` / `.ds` with comments:
```asm
sprite_shape_data:
    .byte $00,$3C,$7E,$FF,$FF,$7E,$3C,$00  ; helicopter frame 0
    .byte $18,$3C,$7E,$DB,$FF,$24,$24,$42  ; helicopter frame 1
```

Labels appear at every named address. Comments appear inline.
Hardware registers and variables use symbolic equates.

**Steps:**

1. **Ensure the Project is complete.** Before exporting, verify
   that `proj.labels` contains every subroutine, variable, and
   hardware register you discovered. `proj.comments` should have
   every inline comment. Run `proj.export_lab()` and
   `bridge.sym_load()` so Altirra's DISASM output uses your
   labels in operands.

2. **Generate the ASM using `asm_writer`.** The current
   `altirra_bridge.asm_writer.emit_xex()` calls `bridge.disasm()`
   per instruction and merges labels/comments from the Project.

   If the asm_writer doesn't handle your game's XEX structure
   (e.g. inter-segment `$FFFF` markers, self-relocating code,
   INITAD vectors), **extend the asm_writer or write a custom
   export script** that:
   - Uses `opt h-` and bracket ORG notation for byte-exact XEX
     headers: `org [a($FFFF),a($start),a($end)],$start`
   - Calls `bridge.disasm()` for code regions (real mnemonics)
   - Emits `.byte` for data regions (using `regions.json` hints)
   - Wraps procedures in `.proc`/`.endp`
   - Generates equates only for addresses actually referenced

   The reference implementation is
   `/home/ilm/Documents/GitHub/pyA8/asm_writer.py` — study
   `write_all()`, `write_main()`, `_emit_mixed()`, and
   `write_equates()` for the full pattern.

3. Run MADS:

       mads -o:river_raid_rebuilt.xex \
            river_raid.pyA8/exports/main.asm

   Address every error and warning. MADS is strict about
   addressing modes and forward references; fix the source,
   not the assembler.

4. Verify the round-trip using any of the cross-platform methods
   from the "Hard success criteria" section. The two binaries
   must be identical.

5. If they aren't, diagnose:
   - `cmake -E compare_files` will report whether they differ.
   - On POSIX, `cmp -l a b | head -20` shows the first byte
     differences.
   - On Windows, `fc /b a b` does the same.
   - Translate the byte offset to an address using the segment
     header; diff that area between your source and what MADS
     produced.

   Common causes:
   - Wrong addressing mode chosen (`lda $80` vs `lda $80,x` —
     MADS picked zp when you wanted abs or vice versa).
   - Wrong branch encoding (forward vs backward `bne`).
   - Wrong literal byte value in a data run.
   - Missing or extra `org` at a segment boundary.
   - A `.proc` / `.endp` that MADS aligned differently.

   Fix and re-assemble until the hashes match.

### Phase 8 — hand off

1. Update `findings.md` with the final status section: labels,
   routines, variables, classified-byte percentage (should be
   100%), the matched SHA-256.
2. Make sure `game_manual.md` covers every mode and enemy the
   player can encounter.
3. Update `ANALYSIS_TODO.md` — every item ticked, no open
   questions, or a clear list of anything that's still open
   and why.
4. Tell the user the analysis is done. Do not start a new task.
   Point at `findings.md`, `game_manual.md`, and
   `ANALYSIS_TODO.md`.

## Anti-patterns to avoid

- **Don't emit `.byte` where there should be instructions.** The
  `.asm` output for code regions MUST contain real 6502 mnemonics
  (`lda`, `sta`, `jmp`, …), not `.byte $A9, $00`. Use
  `bridge.disasm()` or `asm_writer.emit_segment()` for code
  regions. Only emit `.byte` for classified data regions (sprite
  graphics, lookup tables, character sets, padding).
- **Don't write labels only in markdown.** Every label in
  `variables.md` or `subroutine_map.md` MUST also be in the
  `Project` via `proj.label()`. Every comment MUST be in
  `proj.comment()`. The markdown is documentation; the Project
  is the machine-readable truth that drives the `.asm` export.
- **Don't disassemble in Python.** The bridge's `DISASM` already
  wraps Altirra's real disassembler with full symbol resolution
  and illegal-opcode support. Anything you build client-side
  will be worse and slower.
- **Don't assume code is reachable from the entry vector.** Atari
  games install custom interrupt handlers, use indirect jumps
  through RAM, and frequently have code paths reached only after
  specific input sequences. Drive the game into each mode and
  re-profile. Use `HISTORY` to confirm runtime reachability.
- **Don't name variables before observing them.** A name is a
  hypothesis; confirm with `WATCH_SET` that the byte changes
  when the event occurs before committing the name.
- **Don't stop at "looks reasonable".** Success criterion #1 is
  byte-perfect reassembly. A 99.9% match is still a failure.
- **Don't try to use `SIO_TRACE`, `VERIFIER_REPORT`, or
  tracepoint format strings.** These are deferred items
  (Phase 5c in the bridge roadmap) and not in the v1 protocol.
  The bridge will reject them. Use `BP_SET` + `HISTORY` for
  equivalent observation.
- **Don't call `PROFILE_DUMP*` without `PROFILE_STOP` first.**
  The bridge rejects this — `GetSession` is destructive and
  would corrupt the running collector.
- **Don't call `PROFILE_DUMP*` twice in the same session.** The
  data is moved out of the profiler on first read; the second
  call returns an empty result. Restart the profiler to collect
  new data.
- **Don't guess run-addresses or segment layouts.** Parse the
  XEX header. Trust it.
- **Don't write documentation before the analysis supports it.**
  Prose that isn't grounded in observed behaviour becomes wrong
  as soon as you look deeper. Write topical notes as you confirm
  each system, not as speculative placeholders.
- **Don't commit copyrighted artifacts.** The `.gitignore` in
  this directory already excludes `*.xex`, `*.bin`, `exports/`,
  and `snapshots/`. Keep it that way. Analysis files
  (`regions.json`, `comments.json`, `findings.md`, `notes/*.md`,
  `game_manual.md`) are your own derivative work and are
  trackable; the binary, the RAM dumps, and the reassembled
  source are not.

## Discipline — habits to keep the work honest

- **Save state often.** After every major phase, call
  `Project.save()`. Commit the `river_raid.pyA8/` tree to
  disk, not just in memory.
- **Update `ANALYSIS_TODO.md` at every transition.** If a phase
  stretches across many iterations, add sub-items to its bullet
  and tick them as they finish.
- **Snapshot the simulator before risky operations.** Use
  `bridge.state_save()` before any sequence of input injection
  or game-state manipulation that might leave the sim in an
  awkward place. `bridge.state_load()` gets you back.
- **Take screenshots at decision points.** Pin them in
  `findings.md` with one-line captions. They make the analysis
  reviewable by a human.
- **Check coverage regularly.** How many bytes are still
  unclassified? Whose call tree haven't you walked yet? Work
  isn't done until coverage is 100%.
- **Reconnect cleanly on failure.** If the bridge connection
  drops, use `AltirraBridge.from_token_file` to reconnect,
  `boot()` again, `state_load()` your last checkpoint, and
  continue.
- **Extend `deep_analyze.py` every time you do something by
  hand.** The script is your reproducible record. At the end of
  the analysis, running `python deep_analyze.py` from a fresh
  state should regenerate the entire `river_raid.pyA8/`
  directory.

## When you're done

Your final message to the user should look like this:

> River Raid analysis complete. SHA-256 round-trip verified.
>
> - Bytes classified: 100%
> - Routines named: N
> - Procedures grouped: N
> - Variables documented: N
> - Notes: N files
>
> See `river_raid.pyA8/findings.md` for the technical summary.
> See `river_raid.pyA8/game_manual.md` for the player guide.
> See `river_raid.pyA8/ANALYSIS_TODO.md` for the final checklist.
> The reassembled binary is at
> `river_raid.pyA8/exports/river_raid_rebuilt.xex` and its
> SHA-256 matches the original.

If you cannot say all of that truthfully, you are not done.
Stop, update `ANALYSIS_TODO.md`, and ask the user how to proceed.
