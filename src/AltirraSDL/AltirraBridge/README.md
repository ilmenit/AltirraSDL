# AltirraBridge

**Script the Atari 8-bit emulator from any language.**

AltirraBridge exposes [Altirra](https://www.virtualdub.org/altirra.html)
— a cycle-accurate Atari 800 / 800XL / 5200 emulator — over a local
socket. You can pause the machine, peek and poke memory, step frames,
capture screenshots, inject keyboard and joystick input, set
breakpoints, and drive the full debugger from Python, C, or any
language that can open a socket.

It ships as a **self-contained cross-platform package** with a
headless emulator binary, SDKs for Python and C, prebuilt example
programs, docs, and a Claude Code skill. Unpack, run, start scripting.

## What can you do with it?

- **Automated testing of Atari software** — boot a XEX, advance N
  frames, assert on memory or the framebuffer, repeat in CI.
- **AI-driven gameplay and reinforcement-learning agents** —
  frame-step the emulator, read the screen as raw pixels, inject
  joystick state, loop.
- **Headless screenshot capture** — render a title screen to PNG in
  a container with no display at all.
- **Reverse engineering** — cycle-accurate instruction history,
  conditional breakpoints, tracepoints, profiler, symbol loading,
  memory search, MADS export. Everything Altirra's built-in
  debugger does, scriptable.
- **SDK-style embedding** — set up CPU/ANTIC/GTIA state, advance a
  frame, read the rendered pixels back. Use Altirra as a pure
  rendering oracle.

## A 30-second example

```python
from altirra_bridge import AltirraBridge

with AltirraBridge.from_token_file("/tmp/altirra-bridge-12345.token") as a:
    a.boot("game.xex")          # load an Atari executable
    a.frame(240)                # wait for OS boot + XEX loader
    a.key("SPACE")              # press SPACE
    a.frame(60)
    png = a.screenshot()        # capture the current frame
    open("title.png", "wb").write(png)
```

`BOOT` starts a normal Atari OS boot. It returns after the media has
been accepted by the emulator, not after the program's RUNAD has
executed. Always wait with `frame(N)` before peeking RAM or taking a
screenshot; sleeping in Python does not advance a paused emulator. Many
XEX files need more than 120 frames from cold boot, especially on PAL
or when the current profile disables fast boot.

The same thing in C, Lua, Rust, or anything else that speaks
newline-terminated JSON over a TCP socket — the wire protocol is
text-based and deliberately trivial.

## Running it (from a prebuilt package)

You have a package like `AltirraBridge-<version>-linux-x86_64.tar.gz`
(or the macOS / Windows equivalent). Unpack it and you'll see:

```
AltirraBridgeServer         headless emulator binary (no GUI, no audio)
sdk/python/                 Python client SDK (stdlib only, no install)
sdk/python/examples/        runnable Python examples
sdk/c/altirra_bridge.{h,c}  single-file C client (libc only)
sdk/c/examples/             C example sources
sdk/c/examples/bin/         prebuilt C example binaries
sdk/pascal/altirra_bridge.pas single-file Free Pascal client (rtl + rtl-extra)
sdk/pascal/examples/        Pascal example sources
docs/                       protocol spec and command reference
examples/case_studies/      reverse-engineering case studies
skills/altirra-bridge/      Claude Code skill
README.md                   this file
BUILD-INFO.txt              version, commit, platform requirements
```

### Step 1 — Launch the server

```sh
./AltirraBridgeServer --bridge=tcp:127.0.0.1:0
```

On startup the server prints the connection details to stderr:

```
[bridge] listening on tcp:127.0.0.1:54321
[bridge] token-file: /tmp/altirra-bridge-12345.token
[bridge] log-file: /tmp/altirra-bridge-12345.log
```

> **What is the token file?** The server picks a random TCP port and
> writes both the address and a 128-bit random session token to a
> small file in your temp directory. Any process that can read that
> file can connect to the bridge — which means file-system
> permissions alone gate access to your emulator. Clients don't need
> to know the port; they just read the path, extract the address and
> token, and connect. The file is deleted on clean shutdown.
>
> You don't have to handle any of this by hand — every SDK has a
> `from_token_file("…")` constructor that does it for you.

> **Port 0** tells the OS to pick any free port. If you want a fixed
> port (for `adb forward` on Android, or to talk across a network),
> use something like `--bridge=tcp:127.0.0.1:6502`.

### Step 2 — Run an example

In a **second terminal**, copy the token-file path from the server's
stderr line and run one of the prebuilt examples:

```sh
# C — no install, no dependencies
./sdk/c/examples/bin/01_ping /tmp/altirra-bridge-12345.token
```

```sh
# Python — stdlib only, no pip install
PYTHONPATH=sdk/python python3 sdk/python/examples/01_hello.py \
    /tmp/altirra-bridge-12345.token
```

Both print a handshake, advance a handful of frames, and exit cleanly.

If those work, you're done — everything else in the package is
variations on the same theme. Read `sdk/python/examples/` and
`sdk/c/examples/` for progressively richer demos (state dump, input
injection, interactive paint on an ANTIC mode D framebuffer), and
`docs/COMMANDS.md` for the full command reference.

### When a screenshot is black

First check that the XEX actually loaded:

```python
with AltirraBridge.from_token_file(token_file) as a:
    print(a.config())
    print(a.boot(r"C:\path\to\game.xex"))
    a.frame(300)
    print(a.regs())
    print(a.peek(0x2f80, 16).hex())
```

If RAM still has the reset fill pattern after `frame(300)`, copy the
server log file. `AltirraBridgeServer` logs the config directory,
selected profile, machine, memory, BASIC state, kernel IDs, bridge
commands that change state, BOOT requests, client connect/disconnect,
and every failed bridge command. The log file is written next to the
token file (`%TEMP%\altirra-bridge-<pid>.log` on Windows,
`/tmp/altirra-bridge-<pid>.log` on Unix-like systems). The token itself
is printed to stderr for emergency manual connection but is not written
to the persistent log.

The standalone server uses the same `settings.ini` profile store as
AltirraSDL, so a previous GUI run can change the bridge's default
profile. That is expected, but ordinary XEX booting does not require
external Atari ROMs: the built-in AltirraOS kernel is used when no
custom kernel is configured.

## Writing your own client — Python

No install required; the package is pure stdlib.

```python
import sys
from altirra_bridge import AltirraBridge

token_file = sys.argv[1]   # the path printed by AltirraBridgeServer

with AltirraBridge.from_token_file(token_file) as a:
    a.ping()                # round-trip health check
    a.pause()
    a.poke(0x600, 0x42)     # write a byte
    a.frame(1)              # step one frame
    print(a.peek(0x600, 1)) # read it back
    print(a.regs())         # CPU state as a dict
```

Optional install so `altirra_bridge` is importable from anywhere:

```sh
pip install ./sdk/python
```

## Writing your own client — C

Two files to drop into your project: `sdk/c/altirra_bridge.h` and
`sdk/c/altirra_bridge.c`. Dependencies: libc, plus ws2_32 on Windows.
No build system required; just add them to your sources.

```c
#include "altirra_bridge.h"
#include <stdio.h>

int main(int argc, char** argv) {
    atb_client_t* c = atb_create();
    if (atb_connect_token_file(c, argv[1]) != ATB_OK) {
        fprintf(stderr, "%s\n", atb_last_error(c));
        return 1;
    }
    atb_ping(c);
    atb_config_set(c, "addons", "modern");
    atb_device_set(c, "vbxe", 1, "version=126 base=d600");
    atb_frame(c, 60);
    atb_close(c);
    return 0;
}
```

Compile:

```sh
cc -std=c99 -Isdk/c myclient.c sdk/c/altirra_bridge.c -o myclient
```

## Reverse-engineering toolkit (Python only)

The Python SDK isn't just an emulator-scripting client — it ships a
complete reverse-engineering toolkit that takes a raw `.xex`, builds
a persistent analysis project, runs an analyzer pass, and emits
labelled, commented MADS source that round-trips back to an
assemblable binary. Three layers:

- **`altirra_bridge.loader.load_xex(path)`** — parses the XEX
  container, returns an `XexImage` with segments, `FFFF` marker
  positions, INITAD vectors, and the RUN address.
- **`altirra_bridge.Project`** — JSON-on-disk project storage with
  `labels`, `comments`, `regions`, `notes`, `copy_sources`, and
  `reconstructed_excludes`. Addresses are stored as `"$xxxx"` hex
  strings so the file is hand-readable.
- **`altirra_bridge.asm_writer.write_all(bridge, image, proj, out_dir)`**
  — emits `main.asm`, `equates.asm`, and per-segment `.asm` files
  using the bridge's DISASM for instruction text and the project's
  labels, comments, and region classifications to shape the output.

```python
from altirra_bridge import AltirraBridge
from altirra_bridge.project import Project
from altirra_bridge.loader import load_xex
from altirra_bridge.asm_writer import write_all, verify

proj  = Project.load("game.pyA8/project.json")
image = load_xex("game.xex")

with AltirraBridge.from_token_file("/tmp/altirra-bridge-12345.token") as br:
    br.boot("game.xex")
    br.pause()
    print(write_all(br, image, proj, "game.pyA8/exports"))
    print(verify(proj, "game.pyA8/exports"))
```

The exporter has **two modes**:

### Byte-exact mode (default)

Each XEX segment is emitted at its load address. Reassembling with
MADS gives you back the **same bytes** as the original XEX, down
to the `$FFFF` markers and the trailing sector padding. Use this
when your goal is round-trip verification — if `verify()` reports
`"VERIFIED: byte-exact match"` you know every label substitution,
addressing-mode choice, and operand encoding matches the original.

### Reconstructed mode (`reconstructed=True`)

Classic mode has one limitation: **self-relocating games** (the
kind where a bootstrap segment loads the game code at one XEX
address and a relocator copies it to a different runtime address
at boot) can't have proper labels on their game code. Project
labels live at *runtime* addresses, but the walker iterates
*XEX-file* addresses, so the two never meet and you end up with
nothing but synthetic `loc_XXXX` labels and no comments next to
the real code.

Reconstructed mode fixes that. You tell the exporter about the
relocation once:

```python
proj.mark_copy_source(
    xex_start=0x4100,
    xex_end=0x60FF,
    runtime_start=0xA000,
    copy_routine=0x4086,    # informational; the relocator body
    runtime_entry=0xA000,   # where the code actually starts running
)

# Optional: drop bootstrap code whose job becomes moot once the
# runtime slice is emitted directly. For a typical relocator:
proj.exclude_from_reconstructed(0x4080, 0x40FF)  # relocator body
proj.exclude_from_reconstructed(0x0400, 0x0419)  # init segment
proj.exclude_from_reconstructed(0x02E2, 0x02E3)  # INITAD vector
proj.save()
```

Then call `write_all(..., reconstructed=True)` (or just leave
`reconstructed=None` — it auto-enables when `copy_sources` is
non-empty). The exporter splits each segment around the declared
copy-source ranges and emits the copy-source bytes at their
runtime address (`org $A000`), with the project regions shifted
into runtime space so code/data classification follows. The XEX
RUN address is replaced with `runtime_entry` so the reconstructed
XEX boots straight into the relocated code without running the
now-vestigial relocator.

Trade-off: the output XEX is **not** byte-identical to the
original (different segment layout, no relocator, no INITAD
bootstrap), but it **does** boot and the generated `.asm` finally
has real labels and inline comments on the game code.

### `.proc` / `.endp` emission

The exporter runs the analyzer pass
(`recursive_descent` → `build_procedures`) on the program memory
before writing segments. Procedures that pass a safety filter
(clean `rts`/`rti` exit, no fall-through, body entirely in one
segment, no external branches to mid-body addresses, no overlap
with other procs, must have a project-label name) are wrapped in
MADS `.proc name` / `.endp` blocks automatically. Unsafe procs
fall back to flat labels.

Pass `emit_procs=False` to `write_all()` to skip the analyzer
pass entirely — useful for speculative label sets where the
walker can't safely trace procedure bodies.

### Analyzer submodules (`altirra_bridge.analyzer`)

If you want to build your own tooling on top of the primitives
the exporter uses, the analyzer is factored into focused
submodules:

| Module | Purpose | Needs live bridge? |
|---|---|:---:|
| `analyzer.hw` | HW register classification, PORTB decode, auto-labelling | no |
| `analyzer.disasm` | Recursive-descent disassembler (`recursive_descent`) | no |
| `analyzer.patterns` | Gap/data classification, address-table scanning | no |
| `analyzer.variables` | Variable cross-ref and reporting | no |
| `analyzer.subroutines` | Single-sub deep analysis, name/comment inference | no |
| `analyzer.procedures` | `build_procedures`, `call_graph_context`, `detect_subsystems` | no |
| `analyzer.sampling` | PC sampling, DLI chain, PORTB monitor, memory diff | **yes** |
| `analyzer.adapter` | `BridgeEmu` wrapper so analyzers get a uniform memory view | — |

All public names re-export from `altirra_bridge.analyzer`. Worked
examples live in `examples/case_studies/`.

## Writing your own client — Free Pascal

A full Free Pascal SDK ships in `sdk/pascal/`. Stdlib only — no
Lazarus, no Indy, no Synapse, no FFI over the C SDK. Works on
Linux, macOS, Windows, and FreeBSD with a stock FPC install.

```pascal
uses
  SysUtils, altirra_bridge;

var
  Bridge: TAltirraBridge;
  Regs:   TCpuState;
begin
  Bridge := TAltirraBridge.Create;
  try
    Bridge.ConnectTokenFile(ParamStr(1));
    Bridge.Pause;
    Bridge.Frame(60);
    Regs := Bridge.Regs;
    Writeln(Format('PC=$%.4x  A=$%.2x', [Regs.PC, Regs.A]));
    Bridge.Joy(0, 'up', True);       // port 0, up + fire
    Bridge.KeyPress('SPACE', False, False);
  finally
    Bridge.Free;
  end;
end.
```

Build (from `sdk/pascal/examples/`):

```sh
fpc -Fu.. 01_ping.pas -o01_ping
```

Runnable examples (1:1 ports of the C ones) live in
`sdk/pascal/examples/`; the full API reference is in
`sdk/pascal/README.md`.

## Writing your own client — any other language

The wire protocol is just newline-delimited JSON over TCP. **Any
language with sockets and a JSON parser can drive the bridge
natively** — no FFI, no shared library, no C toolchain needed.

See [`docs/WRITING_A_CLIENT.md`](docs/WRITING_A_CLIENT.md) for the
protocol distilled to ten bullets plus two complete working
clients (~80 lines each, stdlib only) in **Rust** and **Go**.
Adapting them to Zig, Lua, Ruby, C#, Java, Nim, Swift, etc. is
straightforward.

## Running it with a GUI window (optional)

If you'd like to **see** the emulator while a script drives it — for
debugging, demos, or just sanity-checking — use `AltirraSDL --bridge`
from the regular AltirraSDL release instead of `AltirraBridgeServer`.
The protocol, token-file format, and SDKs are identical; the only
difference is that `AltirraSDL` opens a window and plays audio.

```sh
./AltirraSDL --bridge
```

You can also run `AltirraSDL --bridge --headless` to get the full
SDL3 frontend without a window or audio — useful if you want the
deferred-action queue and settings persistence behavior of the GUI
build but are running on a machine with no display.

## Platform support

| Platform | Transport                                       |
|----------|-------------------------------------------------|
| Linux    | TCP loopback + POSIX Unix domain sockets        |
| macOS    | TCP loopback + POSIX Unix domain sockets        |
| Windows  | TCP loopback                                    |
| Android  | TCP loopback via `adb forward tcp:N tcp:N`      |

For Android, launch with a fixed port
(`--bridge=tcp:127.0.0.1:6502`), then on the host run
`adb forward tcp:6502 tcp:6502` and connect to `127.0.0.1:6502`.
Use the `inline=true` variants of `SCREENSHOT` / `RAWSCREEN` /
`MEMDUMP` so payloads come back over the socket rather than via a
shared filesystem that doesn't exist on Android.

## Documentation map

| File                      | What it covers                                  |
|---------------------------|--------------------------------------------------|
| `README.md` (this file)   | What it is, how to run it, how to write clients |
| `docs/GETTING_STARTED.md` | Five-minute walkthrough with more hand-holding  |
| `docs/COMMANDS.md`        | Quick reference — every command, one line each  |
| `docs/PROTOCOL.md`        | Full wire contract: framing, encoding, semantics|
| `docs/WRITING_A_CLIENT.md`| Write a native client in any language (Rust + Go worked examples; for Python / C / Pascal use the SDKs directly) |
| `docs/EMULATOR_AS_DEVICE.md`| Three patterns for using the Atari as a display / renderer: bare-metal canvas, boot-a-xex, live graphics editor (Graph2Font-class) |
| `sdk/python/README.md`    | Python client package overview                   |
| `sdk/pascal/README.md`    | Free Pascal client package overview               |
| `examples/case_studies/`  | Reverse-engineering walkthroughs using the bridge|
| `skills/altirra-bridge/`  | Claude Code skill for AI-assisted RE workflows   |
| `BUILD-INFO.txt`          | Package version, commit, platform requirements  |

## Status

| Phase | Scope                                                                         | Status |
|-------|-------------------------------------------------------------------------------|--------|
| 1     | Skeleton: `HELLO`, `PING`, `FRAME`, `PAUSE`, `RESUME`, `QUIT`. TCP+UDS, token auth. | done |
| 2     | State read: `REGS`, `PEEK`, `PEEK16`, `ANTIC`, `GTIA`, `POKEY`, `PIA`, `DLIST`, `HWSTATE`, `PALETTE`. | done |
| 3     | State write & input: `POKE`, `MEMDUMP`, `MEMLOAD`, `JOY`, `KEY`, `CONSOL`, `BOOT`, resets, save states. | done |
| 4     | Rendering: `SCREENSHOT`, `RAWSCREEN`, `RENDER_FRAME`.                         | done |
| 5a    | Debugger introspection: `DISASM`, `HISTORY`, `EVAL`, `CALLSTACK`, `MEMMAP`, `BANK_INFO`, `CART_INFO`, `PMG`, `AUDIO_STATE`. | done |
| 5b    | Debugger control: breakpoints, watches, symbols, memsearch, profiler, verifier. | done |
| 6     | SDK polish, finalised C and Python client APIs.                               | done |
| 7     | Higher-level Python tools: XEX loader, RE project persistence, MADS exporter. | done |
| 8     | `altirra-bridge` Claude Code skill + installer, case studies, CI packaging.   | done |
| 5c    | Deferred: tracepoint format strings, `SIO_TRACE`, `VERIFIER_REPORT`, `SYM_FIND`. | pending |

---

## Getting the package

Every push to `main` produces a self-contained cross-platform package
on the [`nightly-bridge`](../../../../releases/tag/nightly-bridge)
release. Tagged releases (`vX.Y.Z`) also get package archives
attached automatically.

| Platform        | Archive                                            |
|-----------------|----------------------------------------------------|
| Linux x86_64    | `AltirraBridge-<ver>-linux-x86_64.tar.gz`          |
| macOS arm64     | `AltirraBridge-<ver>-macos-arm64.tar.gz`           |
| Windows x86_64  | `AltirraBridge-<ver>-windows-x86_64.zip`           |

Each archive is fully self-contained: the headless
`AltirraBridgeServer` binary, both SDKs, prebuilt C example
binaries, docs, case studies, and the Claude Code skill. No
compilation, no install step.

## Building from source

You only need this section if you're hacking on AltirraBridge itself
or targeting a platform the prebuilt packages don't cover.

There are two binaries that speak the bridge protocol:

| Binary                | Dependencies                          | Use case                         |
|-----------------------|---------------------------------------|----------------------------------|
| `AltirraBridgeServer` | Core emulation libs only              | Lean headless SDK build          |
| `AltirraSDL`          | SDL3 + Dear ImGui (+ optional librashader) | GUI emulator; bridge via `--bridge` |

Both live in the same CMake tree. To build the lean server:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DALTIRRA_BRIDGE_SERVER=ON
cmake --build build --target AltirraBridgeServer -j
./build/src/AltirraBridgeServer/AltirraBridgeServer --bridge=tcp:127.0.0.1:0
```

The C SDK examples build from their own standalone CMakeLists so
downstream consumers can copy `sdk/c/` out of the tree and build in
isolation:

```sh
cmake -B build_c_examples -S src/AltirraSDL/AltirraBridge/sdk/c/examples
cmake --build build_c_examples -j
```

## Source layout

The bridge lives at `src/AltirraSDL/AltirraBridge/` in the Altirra
tree:

```
AltirraBridge/
  docs/             protocol spec, command reference, security notes
  sdk/
    c/              single-file C client
      examples/     01_ping.c, 02_peek_regs.c, 03_input.c, 04_paint.c
    python/         pure-stdlib Python client package
      altirra_bridge/
      examples/     01_hello.py, 02_state_dump.py, 03_input.py, 04_paint.py
  examples/
    case_studies/   reverse-engineering walkthroughs
  skills/           Claude Code Skills
  README.md         you are here
```

The C++ server that speaks the protocol lives alongside the frontend
at `src/AltirraSDL/source/bridge/` — three files (`bridge_server`,
`bridge_protocol`, `bridge_transport`) integrated into
`main_sdl3.cpp` via five ~3-line insertions. To remove the bridge
from an AltirraSDL build, configure with `-DALTIRRA_BRIDGE=OFF` or
delete that directory. The Windows-native `Altirra.sln` build is
**completely untouched** by AltirraBridge — no `.vcxproj` references
any bridge file.

## License

GPLv2+, matching Altirra. See [`LICENSE`](LICENSE).
