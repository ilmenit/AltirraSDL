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
    a.frame(120)                # advance 120 frames (~2 seconds)
    a.key("SPACE")              # press SPACE
    a.frame(60)
    png = a.screenshot()        # capture the current frame
    open("title.png", "wb").write(png)
```

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

On startup the server prints two lines to stderr:

```
[bridge] listening on tcp:127.0.0.1:54321
[bridge] token-file: /tmp/altirra-bridge-12345.token
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
    atb_frame(c, 60);
    atb_close(c);
    return 0;
}
```

Compile:

```sh
cc -std=c99 -Isdk/c myclient.c sdk/c/altirra_bridge.c -o myclient
```

## Writing your own client — any other language

The wire protocol is just newline-delimited JSON over TCP. **Any
language with sockets and a JSON parser can drive the bridge
natively** — no FFI, no shared library, no C toolchain needed.

See [`docs/WRITING_A_CLIENT.md`](docs/WRITING_A_CLIENT.md) for the
protocol distilled to ten bullets plus three complete working
clients (~80 lines each, stdlib only) in **Rust**, **Free Pascal**,
and **Go**. Adapting them to Zig, Lua, Ruby, C#, Java, Nim, etc. is
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
| `docs/WRITING_A_CLIENT.md`| Write a native client in any language (Rust, Pascal, Go worked examples) |
| `docs/EMULATOR_AS_DEVICE.md`| Three patterns for using the Atari as a display / renderer: bare-metal canvas, boot-a-xex, live graphics editor (Graph2Font-class) |
| `sdk/python/README.md`    | Python client package overview                   |
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
