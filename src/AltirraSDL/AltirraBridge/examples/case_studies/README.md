# Reverse-engineering case studies

Worked examples of using a Python-driven Atari emulator bridge to
reverse-engineer commercial 8-bit games. Each subdirectory is a
complete RE project's *analysis artifacts* — labels, comments,
xrefs, call graphs, findings notes, screenshots — minus the actual
game binaries and any derivative reassembled output.

These were originally produced against the **pyA8** reference
project (the precursor to AltirraBridge) but the analysis is
emulator-independent. They show what the deliverable of an
8-bit-game RE workflow looks like:

| Project | Game | Analysis maturity |
|---|---|---|
| `wizard/` | Wizard (1981, Penguin Software) | game logic fully understood, ~50 routines traced, 190 labelled symbols |
| `koronis_rift/` | Koronis Rift (1985, Lucasfilm Games) | banking + dual-CPU rendering loop reverse-engineered |
| `behind_jaggi_lines/` | Behind Jaggi Lines! | core loop analyzed, sprite/collision system documented |
| `project_m/` | Project M (homebrew, 2.0) | call graph + xrefs extracted |

## What's in each project

- `findings.md` — narrative notes from the analysis. Game overview,
  state machines, key memory locations, gotchas.
- `*.pyA8/project.json` — the RE project file: segment list,
  metadata, banking info.
- `*.pyA8/regions.json` — labelled memory regions (code/data/strings).
- `*.pyA8/comments.json` — per-address comments.
- `*.pyA8/xrefs.json` — cross-references (callers/callees).
- `*.pyA8/call_graph.json` — function call relationships.
- `*.pyA8/notes/` — free-form analyst notes per area of interest.
- `*.png` — screenshots captured at key analysis points.
- `deep_analyze.py` (where present) — the analyst's custom Python
  script for that game.

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

## How to use these as references

1. **Read `findings.md` first.** It's the human summary — what the
   game does, how the code is structured, what the analyst found.
2. **Skim `regions.json`** to see how the code/data layout was
   eventually understood.
3. **Cross-reference with the screenshots** to see what state the
   game was in when each insight landed.
4. **Note the workflow shape**, not the specific addresses. The
   addresses only mean anything if you have your own legally-obtained
   copy of the same game; the *workflow* (how to find a state
   machine, how to label routines, how to use cross-references) is
   transferable to any 8-bit RE target.

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
