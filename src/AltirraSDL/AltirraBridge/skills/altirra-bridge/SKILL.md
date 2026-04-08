---
name: altirra-bridge
description: Use this skill when controlling the AltirraSDL Atari emulator programmatically — driving the simulator from a script, capturing screenshots from a running Atari, injecting joystick/keyboard input, frame-stepping for deterministic testing, reading or writing CPU/memory/chip state, setting breakpoints or watchpoints, profiling 6502 code, loading symbols, or building automated tests against an Atari binary. Triggers on phrases like "AltirraBridge", "AltirraSDL --bridge", "AltirraBridgeServer", "Altirra Python SDK", "headless Atari emulator", "screenshot the Atari", "automate AltirraSDL", "drive the emulator from Python", "Atari debugger over a socket", "altirra_bridge package", as well as on tasks involving the bridge protocol, the Python `altirra_bridge` package, or the C `altirra_bridge.h` SDK.
---

# AltirraBridge

AltirraBridge is a JSON-over-socket scripting interface bolted onto
the AltirraSDL Atari emulator. It exposes the simulator core
(CPU/ANTIC/GTIA/POKEY/PIA), the debugger, the profiler, and the
display pipeline to client scripts. Two SDKs ship in the box:
**Python** (`altirra_bridge` package, pure stdlib) and **C**
(`altirra_bridge.h` / `.c`, single-file, libc + Winsock only).

This skill teaches you how to drive the bridge. It does not teach
6502 reverse-engineering — that's a separate methodology question.

## When to use this skill

- The user mentions AltirraBridge, AltirraSDL `--bridge`,
  AltirraBridgeServer, or the bridge protocol.
- The user wants to script an Atari emulator: capture screenshots,
  inject input, save/load state, run tests against a `.xex`/`.atr`,
  set breakpoints, profile a routine.
- The user is writing automated tests, AI gameplay agents, or
  reverse-engineering tooling against AltirraSDL.
- The user references the Python `altirra_bridge` package or the
  C `altirra_bridge.h` header.

If the task is "explain what an LDA #$00 does" or "decode this 6502
hex dump", that's general 6502 knowledge — this skill won't help.

## Two server targets

1. **`AltirraSDL --bridge`** — full GUI emulator with the bridge
   exposed alongside it. Use when you want to watch the Atari while
   it's being scripted, or when the user is debugging the bridge
   itself.
2. **`AltirraBridgeServer`** — headless lean-SDK build. No window,
   no fonts, no librashader, no input layer. ~30% smaller binary.
   Use when running automated tests, in CI, or as a backend service.

Both speak the **same protocol** and accept the **same SDK clients**.
The user can switch between them transparently.

Both need to be invoked with `--bridge` (or `--bridge=tcp:127.0.0.1:0`)
to enable the listening socket. On startup the server prints two
lines to **stderr**:

```
[Bridge] listening on tcp:127.0.0.1:54321
[Bridge] token-file: /tmp/altirra-bridge-12345.token
```

The token file contains the bound address on line 1 and a 128-bit
hex session token on line 2. Every client must read this file to
connect.

## Quick start — Python

```python
from altirra_bridge import AltirraBridge

with AltirraBridge.from_token_file("/tmp/altirra-bridge-12345.token") as a:
    a.boot("/path/to/game.xex")
    a.frame(120)                          # let the game initialize
    png = a.screenshot()                  # PNG bytes
    open("title.png", "wb").write(png)
    a.joy(0, "right", fire=True)
    a.frame(60)
    print(a.regs())                       # CPU state snapshot
```

`from_token_file` does connect + HELLO in one call. The context
manager closes the socket cleanly even on exceptions.

## Quick start — C

```c
#include "altirra_bridge.h"
atb_client_t* c = atb_create();
atb_connect_token_file(c, "/tmp/altirra-bridge-12345.token");
atb_boot(c, "/path/to/game.xex");
atb_frame(c, 120);

unsigned char* png; size_t len; unsigned int w, h;
atb_screenshot_inline(c, &png, &len, &w, &h);
fwrite(png, 1, len, fopen("title.png", "wb"));
free(png);

atb_close(c);
```

## Key concepts

- **Frame gate.** `frame(N)` resumes the simulator, lets it run
  exactly `N` frames, then re-pauses. This is how you get
  deterministic timing for automated tests. The next command after
  `frame()` blocks server-side until the gate releases.
- **Pause preserves on reset.** `cold_reset()` and `warm_reset()`
  restore the simulator's pause state — a paused emulator stays
  paused after reset. This is a deliberate invariant; rely on it.
- **Debug-safe reads.** `peek()` / `memdump()` read through the
  debugger's side-effect-free path. Reading `$D000-$D7FF` does NOT
  trigger ANTIC/GTIA/POKEY register effects, the way a normal LDA
  would on real hardware.
- **Inline binary payloads.** Every command that returns binary data
  (`SCREENSHOT`, `RAWSCREEN`, `MEMDUMP`) supports both inline base64
  and a server-side `path=` mode. Inline works over `adb forward` on
  Android with no shared filesystem; path is faster on local UDS.
- **Bridge owns input.** The bridge has its own PIA input slot, so
  injected joystick/keyboard state does not fight with real-gamepad
  input from the host. Disconnect cleanup automatically releases
  every key, direction, and console switch — a crashed script
  cannot leave the simulator with stuck input.

## Command groups (full reference: `docs/COMMANDS.md`, `docs/PROTOCOL.md`)

| Group              | Commands                                                                       |
|--------------------|--------------------------------------------------------------------------------|
| Lifecycle          | `HELLO PING PAUSE RESUME FRAME QUIT`                                           |
| State read         | `REGS PEEK PEEK16 ANTIC GTIA POKEY PIA DLIST HWSTATE PALETTE`                  |
| State write/input  | `POKE POKE16 MEMDUMP MEMLOAD JOY KEY CONSOL BOOT MOUNT COLD_RESET WARM_RESET STATE_SAVE STATE_LOAD` |
| Rendering          | `SCREENSHOT RAWSCREEN RENDER_FRAME`                                            |
| Introspection      | `DISASM HISTORY EVAL CALLSTACK MEMMAP BANK_INFO CART_INFO PMG AUDIO_STATE`     |
| Breakpoints        | `BP_SET BP_CLEAR BP_CLEAR_ALL BP_LIST WATCH_SET`                               |
| Symbols            | `SYM_LOAD SYM_RESOLVE SYM_LOOKUP MEMSEARCH`                                    |
| Profiler           | `PROFILE_START PROFILE_STOP PROFILE_STATUS PROFILE_DUMP PROFILE_DUMP_TREE`     |
| Verifier           | `VERIFIER_STATUS VERIFIER_SET`                                                 |

## Progressive-disclosure references

When the user's task warrants deeper detail, read from
`references/`:

- **`references/python-quickstart.md`** — Python SDK recipe
  collection. Read this when the user is writing Python against
  the bridge: `AltirraBridge` class, every method, common patterns
  (XEX boot, screenshot, input injection, breakpoint loop,
  profile session).
- **`references/c-quickstart.md`** — C SDK recipe collection.
  Read this when the user is writing C against
  `altirra_bridge.h`: `atb_*` functions, struct layouts, parsing
  the JSON responses for commands without struct outputs.
- **`references/workflows.md`** — task-oriented playbooks. Read
  this when the user describes a *goal* rather than a method:
  "screenshot every level", "find where the score is stored",
  "run the game until it crashes", "render a frame from a
  hand-built display list". Each playbook is a complete script
  the user can adapt.

For full wire-protocol details (request/response schemas, error
formats, versioning, threat model), the canonical reference is
`docs/PROTOCOL.md` — read it directly when SDK methods don't
expose what you need.

## Higher-level Python tools

The `altirra_bridge` package also ships three local-only modules
that complement the bridge for reverse-engineering work:

- **`altirra_bridge.loader`** — XEX file parser. Parses Atari
  binary segments + INITAD/RUNAD vectors. Pure local file work,
  no bridge needed.
- **`altirra_bridge.project`** — persistent RE project state:
  labels, comments, notes, JSON-on-disk. Round-trips with the
  bridge's symbol loader via `Project.export_lab()` /
  `Project.import_from_bridge()`.
- **`altirra_bridge.asm_writer`** — MADS source exporter. Combines
  bridge `DISASM` output with project labels/comments to produce
  reassemblable MADS source.

These exist because the bridge cannot do them server-side (file
parsing, persistence) but they were obvious omissions otherwise.
Any other "should I write a Python helper?" decision should default
to *no* — most things you'd want a helper for are already a single
bridge command call away.

## Things to avoid

- **Don't reimplement Altirra functionality client-side.** If you
  find yourself writing a 6502 disassembler in Python, stop — the
  bridge's `DISASM` command wraps Altirra's own
  `ATDisassembleInsn`, including symbol resolution and 65C816
  mode switching.
- **Don't poll PROFILE_STATUS in a tight loop.** The profiler is
  driven by the simulator scheduler; commands are dispatched
  between frames. Use `profile_start() → frame(N) → profile_stop()
  → profile_dump()` instead.
- **Don't call `PROFILE_DUMP*` while the profiler is running.**
  The underlying `GetSession` is destructive (it `std::move`s
  the session out of the builder). The bridge will reject a
  running-state dump with a clear error, but the right pattern
  is `STOP` first, `DUMP` second.
- **`PROFILE_DUMP*` is one-shot per session.** Calling it twice
  returns empty data. Restart the profiler to collect new data.
- **Don't try to set range watches.** Altirra's access-breakpoint
  API is per-byte. `WATCH_SET ... len=N` is rejected; set one
  watch per address instead.
- **Don't assume `WATCH_SET mode=rw` returns one id.** It creates
  *two* breakpoints (one read, one write) and returns both ids in
  a list. The Python SDK reflects this in its return type.
- **Don't expect SIO trace, verifier violation logs, or
  tracepoint format strings yet.** These are roadmap items
  awaiting public Altirra core APIs; they appear in the bridge's
  "deferred" list and are not in the v1 protocol.
