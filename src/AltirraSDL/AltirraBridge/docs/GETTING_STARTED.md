# Getting Started with AltirraBridge

AltirraBridge lets you drive AltirraSDL programmatically — frame-step
the emulator, inspect and modify CPU/memory/hardware state, capture
screenshots, inject input, and run the debugger — all from an
external process. Use it for automated testing, AI-driven gameplay,
headless rendering, reverse engineering, and any other workflow
where you'd want to script an emulator.

This document walks through the absolute minimum to get a "PING"
across the wire. For the full protocol see
[`PROTOCOL.md`](PROTOCOL.md).

## 1. Launch AltirraSDL with the bridge enabled

```sh
./AltirraSDL --bridge
```

You should see two log lines on stderr:

```
[bridge] listening on tcp:127.0.0.1:54321
[bridge] token-file: /tmp/altirra-bridge-12345.token
```

The port number is chosen by the OS (you can pin it with
`--bridge=tcp:127.0.0.1:6502`). The token file contains the bound
address and a 128-bit session token; any client process with read
access to that file can connect.

## 2. Connect from Python

```python
from altirra_bridge import AltirraBridge

with AltirraBridge.from_token_file("/tmp/altirra-bridge-12345.token") as a:
    a.ping()
    a.frame(60)        # advance 60 frames then re-pause
    print(a.ping())    # round-trip after frame stepping
```

The Python package is in `sdk/python/`. Install with
`pip install ./sdk/python/`.

## 3. Connect from C

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
    atb_ping(c);
    atb_close(c);
    return 0;
}
```

The C SDK is in `sdk/c/`. It's a single header + single .c file with
no dependencies beyond libc and Winsock. Build with the standalone
CMakeLists in `sdk/c/examples/`.

## 4. Cross-platform notes

- **Linux/macOS desktop**: works out of the box, TCP loopback by
  default. POSIX UDS is also available via `--bridge=unix:/path`.
- **Windows desktop**: works out of the box, TCP loopback by
  default. (Filesystem UDS is not supported on the Windows build.)
- **Android**: launch AltirraSDL with `--bridge=tcp:127.0.0.1:6502`
  (or any fixed port), then on the host machine run
  `adb forward tcp:6502 tcp:6502` and connect to
  `tcp:127.0.0.1:6502`. Server-side filesystem paths
  (`MEMDUMP path:...`, `SCREENSHOT path:...`) are not usable on
  Android because the host and device have different filesystems —
  use the `inline:` payload variants instead (Phase 4+).

## 5. Headless mode (no window, no audio)

For automated testing, CI, RL training pipelines and any other use
case where you want the emulator running but don't want a window
opened or audio playing, AltirraSDL has three options:

### Option A: SDL3 dummy drivers (works today, zero code changes)

SDL3 ships with `offscreen` and `dummy` drivers that allocate
trivial framebuffers and silently drop audio. Set them in the
environment before launching:

```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./AltirraSDL --bridge
```

The simulator runs at full speed, the bridge serves clients
normally, no window appears, no sound plays. This is how the
Phase 1-3 smoke tests run.

### Option B: `--headless` flag (sugar for Option A)

```sh
./AltirraSDL --bridge --headless
```

Equivalent to setting the two env vars above. The flag is a
convenience — it's stripped from argv before SDL3 init, sets the
two env vars (without overriding any user-set values), and passes
control to the normal SDL3 startup path. Same binary as the GUI
build, same dependencies, same code paths — the only difference
is which SDL3 video/audio backends get loaded.

Use this when you want the SDL3 frontend's full feature set
(deferred actions, settings persistence, the same boot path as the
GUI) but no UI on screen.

### Option C: `AltirraBridgeServer` — lean SDK build (no SDL3 / no ImGui / no librashader)

For embedding the emulator in another project where the SDL3 +
ImGui + librashader dependencies are unwelcome, build the
separate `AltirraBridgeServer` target:

```sh
cmake -DALTIRRA_BRIDGE_SERVER=ON ...
cmake --build . --target AltirraBridgeServer
./src/AltirraBridgeServer/AltirraBridgeServer --bridge=tcp:127.0.0.1:6502
```

This produces a much smaller binary that links only the core
emulation libraries (`ATCPU`, `ATEmulation`, `ATDevices`, `ATIO`,
`ATAudio`, `ATNetwork`, `ATNetworkSockets`, `ATDebugger`, `ATVM`,
`ATCore`, `Kasumi`, `vdjson`, `system`) plus the bridge. No SDL3,
no ImGui, no shader stack, no display backends, no input layer,
no UI. Suitable for CI containers, headless servers, RL training
pipelines, and any environment where graphics dependencies are a
non-starter.

The `AltirraBridgeServer` binary speaks the **identical bridge
protocol** as `AltirraSDL --bridge` — clients written against
either binary work against the other. Differences:

- STATE_SAVE / STATE_LOAD / STATE_LIST / STATE_DROP run
  *synchronously* on both targets -- the response carries the
  outcome and no `FRAME 1` is needed. BOOT / MOUNT still run via
  the deferred-action queue on `AltirraSDL --bridge` because the
  triggered cold reset takes ~300 frames to settle; clients use
  `FRAME N` for those after the response arrives. On the headless
  `AltirraBridgeServer` they run synchronously too.
- No window-related commands (none exist in v1 anyway).
- No SDL3 settings file lookup — the bridge server reads ROM paths
  from the same `~/.config/altirra/settings.ini` as AltirraSDL by
  default, but does not write settings on exit.

## 5. What's next

Phase 1 covers `HELLO`, `PING`, `FRAME`, `PAUSE`, `RESUME`, `QUIT`.
The full protocol (memory access, CPU state, hardware state,
rendering, debugger, profiler, symbols, cheat finder, SIO trace)
arrives in Phases 2 through 5. See the `Phasing` section of
[`AltirraBridge/README.md`](../README.md) for the roadmap.
