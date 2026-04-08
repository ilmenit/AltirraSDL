# Reverse-engineering case studies

This directory holds two kinds of material:

1. **Reference analyses** — four worked RE projects produced
   against the **pyA8** precursor of AltirraBridge. Each is a
   complete project's *analysis artifacts* — labels, comments,
   xrefs, call graphs, findings notes, screenshots — minus the
   copyrighted game binaries and any derivative reassembled
   output. They show what the deliverable of a high-quality
   8-bit-game RE workflow looks like.

2. **Active templates** — directories containing a `PROMPT.md`
   file that drives Claude Code through the same workflow
   against a binary you supply yourself. The prompt references
   the nearest reference analysis and uses it as a quality bar.

## Reference analyses (read-only)

| Project | Game | Analysis depth |
|---|---|---|
| `wizard/` | Wizard (1981, Penguin Software) | game logic fully understood, ~50 routines traced, 190 labelled symbols, 449-line `findings.md`, 295-line `game_manual.md` for non-technical readers |
| `koronis_rift/` | Koronis Rift (1985, Lucasfilm Games) | banking + dual-CPU rendering loop reverse-engineered |
| `behind_jaggi_lines/` | Behind Jaggi Lines! (dev title for Rescue on Fractalus!) | 1576 lines of topic notes across 12 files, 137 named zero-page variables, full 3D rendering pipeline documented |
| `project_m/` | Project M (homebrew, 2.0) | call graph + xrefs extracted |

## Active templates (drive-your-own-analysis)

| Project | Game | What's here |
|---|---|---|
| `river_raid/` | River Raid (1982, Activision / ported to Atari by Carol Shaw) | `PROMPT.md` + `README.md` — a Claude Code prompt that drives the full analysis workflow against a `river_raid.xex` you provide. Output matches the depth of the `wizard/` and `behind_jaggi_lines/` references, with byte-perfect MADS round-trip verification. |

## What's in each reference project

- `findings.md` — narrative technical summary. Game overview,
  state machines, loading sequence, subsystem breakdown,
  performance notes, open questions.
- `game_manual.md` (where present) — a **non-technical** player
  guide written in prose, with no hex addresses or disassembly.
  Someone who has never opened a debugger can read it and
  understand how to play the game. The `wizard/` project has a
  particularly complete example.
- `*.pyA8/project.json` — the RE project file: segment list,
  metadata, banking info, SHA-256 of the original binary.
- `*.pyA8/regions.json` — per-byte classification: `code`,
  `proc` (procedure regions), or `data` with a hint
  (`bytes` / `charset` / `addr_table` / `display_list` /
  `string` / `fill_zero` / ...).
- `*.pyA8/comments.json` — `{address → comment}` map.
- `*.pyA8/xrefs.json` — cross-references (target → callers).
- `*.pyA8/call_graph.json` — call relationships (enriched
  from the profiler's call tree).
- `*.pyA8/notes/` — topical prose documents, one per subsystem
  (VBI architecture, display list architecture, rendering
  pipeline, variables, performance profile, etc.).
- `*.png` — screenshots captured at key analysis points.
- `deep_analyze.py` (where present) — the analyst's custom Python
  helper script. Extended iteratively throughout the analysis
  as a reproducible record of the work.

## What's NOT here, and why

**No `.xex` / `.atr` / `.bin` files of any kind.** The original
games are commercial copyrighted software. The analysis artifacts
in this directory are the analyst's own derivative work
(labels, comments, walkthroughs); they are distributable. The
binaries themselves are not.

**No reassembled output.** Each `*.pyA8/` originally contained an
`exports/` subdirectory with MADS-reassembled source (and in some
cases the resulting `.xex`). Reassembled source for a copyrighted
binary is itself derivative; we don't ship it.

## How to use the reference analyses

1. **Read `findings.md` first.** It's the technical summary —
   what the game does, how the code is structured, what the
   analyst found.
2. **Read `game_manual.md` second** (where it exists). It's
   written for a non-technical reader and shows what kind of
   player-facing output you should be able to produce from a
   well-documented binary.
3. **Skim `regions.json`** to see how the code/data layout was
   eventually understood, including the data sub-type hints.
4. **Skim the `notes/` directory** to see how the analyst
   broke the game into topical documents.
5. **Look at `deep_analyze.py`** (where present) to see what a
   reproducible, iteratively-extended analysis script looks
   like.
6. **Note the workflow shape**, not the specific addresses.
   The addresses only mean anything if you have your own
   legally-obtained copy of the same game; the *workflow* —
   how to find a state machine, how to label routines, how to
   use cross-references, how to verify a round-trip — is
   transferable to any 8-bit RE target.

## How to use the active templates

Each active template (like `river_raid/`) contains a
`PROMPT.md` designed for Claude Code. To run one:

1. Read the directory's `README.md` for legal notes and
   prerequisites.
2. Place your legally-obtained game binary at the path the
   prompt expects (e.g. `river_raid/river_raid.xex`). The
   directory's `.gitignore` excludes these binaries from git
   so you cannot accidentally commit copyrighted material.
3. Start the headless bridge server:
   `./AltirraBridgeServer --bridge=tcp:127.0.0.1:0`
4. Open Claude Code in the repository root and tell it:
   "Read `examples/case_studies/<game>/PROMPT.md` and start
   the analysis."
5. Monitor progress through the project's `ANALYSIS_TODO.md`
   checklist and partial writes to `<game>.pyA8/`.

The prompt is self-contained: it handles phasing, asks for
clarification when a non-expert user can help, and enforces a
byte-perfect MADS round-trip as the hard success criterion.

## Translating to AltirraBridge

The `*.pyA8` format is from the pyA8 reference project, not from
AltirraBridge. To use any of these analyses as a starting point in
an AltirraBridge `Project`, the relevant transformations are:

| pyA8 file | AltirraBridge equivalent |
|---|---|
| `project.json` | `Project.metadata` + segment list (use `altirra_bridge.loader.parse_xex`) |
| `regions.json` (labels) | `Project.label(addr, name)` |
| `comments.json` | `Project.comment(addr, text)` |
| `xrefs.json`, `call_graph.json` | rebuild from the bridge's `DISASM` + `CALLSTACK` (don't import, regenerate) |
| `findings.md`, `notes/` | `Project.add_note(start, end, text)` or keep as standalone Markdown |

A future `altirra_bridge.import_pyA8` helper could automate this if
demand appears. For now, hand-translation is the path.

## Legal note

The analysis files in this directory are released under the same
GPLv2+ licence as the rest of AltirraBridge. They are independent
creative work documenting the *behavior* of programs the analyst
had legitimate access to, not copies or derivatives of those
programs' code. If you are the rights holder for any of the
referenced games and would prefer the corresponding case study to
be removed, open a GitHub issue and it will be removed promptly.
