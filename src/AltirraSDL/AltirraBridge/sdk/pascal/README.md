# altirra_bridge — Free Pascal client

Free Pascal client for the AltirraBridge scripting protocol —
drive [Altirra](https://www.virtualdub.org/altirra.html) (Atari
8-bit emulator) programmatically over a local socket. Use it for
automated testing, AI-driven gameplay, headless screenshot
capture, reverse engineering of Atari binaries, and anything else
where you'd want to script an emulator from Pascal.

**Stdlib only** — no Lazarus, no Indy, no Synapse. The unit
depends exclusively on FPC's `Sockets`, `SysUtils`, and `Classes`
units, which ship with every Free Pascal install (the `Sockets`
unit lives in `rtl-extra`, included in the standard distribution).
Cross-platform: works on Linux, macOS, Windows, FreeBSD, and
Android (FPC's `Sockets` unit wraps Winsock automatically on
Windows).

## Layout

```
altirra_bridge.pas              the single-file client unit
examples/
  01_ping.pas                   "does the socket work?" smoke test
  02_peek_regs.pas              read CPU registers + a peek of zero page
  03_input.pas                  joystick, console switches, keyboard,
                                memory round-trip
  04_paint.pas                  mouse-driven paint tool that uses the
                                Atari itself as a 160x96 framebuffer
                                via ANTIC mode D + a custom display
                                list, with a live rawscreen preview
                                rendered through SDL3
  fetch-sdl3-for-pascal.sh      one-shot git clone of the SDL3-for-Pascal
                                binding used by 04_paint.pas
README.md                       this file
```

Each example is a 1:1 port of the matching `sdk/c/examples/0N_*.c`
source and produces the same output when run against the same
bridge server.

## API surface

The `altirra_bridge` unit exports `TAltirraBridge`, a single class
that owns one TCP connection to a bridge server. Instantiate it,
call `ConnectTokenFile`, then drive the simulator with the
Phase-specific methods:

```pascal
uses
  SysUtils, altirra_bridge;

var
  Bridge: TAltirraBridge;
  Regs:   TCpuState;
begin
  Bridge := TAltirraBridge.Create;
  try
    Bridge.ConnectTokenFile('/tmp/altirra-bridge-12345.token');

    Bridge.Pause;
    Bridge.Frame(60);                 // step 60 frames, re-pause

    Regs := Bridge.Regs;
    Writeln(Format('PC=$%.4x  A=$%.2x',
      [Regs.PC, Regs.A]));

    Bridge.Poke($0600, $42);
    Writeln(Bridge.PeekByte($0600));  // prints 66

    Bridge.Joy(0, 'up', True);        // port 0, up + fire
    Bridge.KeyPress('SPACE', False, False);
    Bridge.Consol(True, False, False);// hold START
  finally
    Bridge.Free;
  end;
end.
```

Methods covered (Phase 1-3):

| Phase | Method                                         | Wire verb             |
|-------|------------------------------------------------|-----------------------|
| 1     | `Ping`                                         | `PING`                |
| 1     | `Pause` / `ResumeEmu` / `Frame(N)`             | `PAUSE / RESUME / FRAME` |
| 2     | `Regs` — returns `TCpuState` record            | `REGS`                |
| 2     | `PeekByte(addr)` / `Peek(addr, len)`           | `PEEK`                |
| 2     | `Peek16(addr)`                                 | `PEEK16`              |
| 2     | `Antic` / `Gtia` / `Pokey` / `Pia`             | `ANTIC / GTIA / POKEY / PIA` (raw JSON) |
| 3     | `Poke(addr, val)` / `Poke16(addr, val)`        | `POKE / POKE16`       |
| 3     | `MemLoad(addr, data, len)` / `MemLoadBytes`    | `MEMLOAD`             |
| 3     | `MemDump(addr, len)` — returns `TBytes`        | `MEMDUMP`             |
| 3     | `Joy(port, dir, fire)`                         | `JOY`                 |
| 3     | `KeyPress(name, shift, ctrl)`                  | `KEY`                 |
| 3     | `Consol(start, select, option)`                | `CONSOL`              |

Everything else in the protocol (rendering, debugger, profiler,
symbols, breakpoints) is accessible through the generic `Rpc`
method and `LastResponse` / `ExtractString` / `ExtractHex` /
`ExtractInt` helpers:

```pascal
var Resp, ColPF0: String;
begin
  Resp   := Bridge.Rpc('GTIA');
  ColPF0 := Bridge.ExtractString(Resp, 'COLPF0');
  // ...
end;
```

Error handling is exception-based: every method that can fail
raises `EBridgeError` with the server's error text or the
underlying socket errno.

## Build

### Unit only

```sh
fpc -FU<output-dir> altirra_bridge.pas
```

This produces `altirra_bridge.ppu` and `altirra_bridge.o` in the
output directory.

### Examples

Each example is a standalone program that uses the unit. From the
`examples/` directory:

```sh
fpc -Fu.. -FU/tmp 01_ping.pas      -o01_ping
fpc -Fu.. -FU/tmp 02_peek_regs.pas -o02_peek_regs
fpc -Fu.. -FU/tmp 03_input.pas     -o03_input
```

`-Fu..` adds the parent directory to the unit search path so the
compiler finds `altirra_bridge.pas`. `-FU/tmp` dumps intermediate
`.ppu`/`.o` files somewhere predictable (FPC defaults to writing
them next to the source, which clutters the tree).

If your FPC install doesn't have the `Sockets` unit compiled by
default (rare, but possible with distribution-trimmed packages),
the unit lives in `rtl-extra`. A one-liner config file to point
FPC at it:

```
-Fu/path/to/fpc/3.2.2/units/x86_64-linux/rtl
-Fu/path/to/fpc/3.2.2/units/x86_64-linux/rtl-extra
```

### Example 04_paint (SDL3 window)

`04_paint.pas` is the one example that needs an extra dependency:
the community-maintained **SDL3 for Pascal** binding from
[github.com/PascalGameDevelopment/SDL3-for-Pascal][sdl3-for-pascal]
(zlib licensed, same as SDL itself). Fetch it with the one-liner
script in `examples/`:

```sh
cd examples
./fetch-sdl3-for-pascal.sh      # clones into ./sdl3-for-pascal
fpc -Fu.. -Fu./sdl3-for-pascal/units 04_paint.pas -o04_paint
```

On non-macOS Unix the SDL3 binding also pulls in the FPC `x11`
units, which are part of the standard Free Pascal distribution
(package `units-x11` in the official tarball, `fpc-units-x11`
in Debian/Ubuntu, bundled with `fpc` in most other distros).

At runtime the example needs `libSDL3.so.0` (Linux), `SDL3.dll`
(Windows), or `libSDL3.dylib` (macOS) on the loader's search
path. On Linux the simplest way is to install the
distribution's SDL3 package (Fedora: `dnf install SDL3`,
Debian: `apt install libsdl3-0`, Arch: `pacman -S sdl3`). If
you're running alongside a build of the main AltirraSDL
repository, point `LD_LIBRARY_PATH` at
`build/<preset>/_deps/sdl3-build/` where CMake dropped the
shipped SDL3.

Run:

```sh
cp ../../assets/g2f.act .              # optional .act palette
./AltirraBridgeServer --bridge=tcp:127.0.0.1:0 &
./04_paint /tmp/altirra-bridge-*.token
```

Controls: click the colour swatches or press `1`/`2`/`3` to
select a colour, click the `X` button or press `C` to clear,
and press `ESC` (or close the window) to quit. Click and drag
on the left canvas to paint — the right side shows the live
Atari frame the emulator is rendering from your edits.

[sdl3-for-pascal]: https://github.com/PascalGameDevelopment/SDL3-for-Pascal

## Run

In one terminal, start a bridge server:

```sh
./AltirraBridgeServer --bridge=tcp:127.0.0.1:0
```

Note the token-file path printed on stderr, then in another
terminal:

```sh
./01_ping      /tmp/altirra-bridge-12345.token
./02_peek_regs /tmp/altirra-bridge-12345.token
./03_input     /tmp/altirra-bridge-12345.token
```

Expected output from `01_ping`:

```
connected. server said: {"ok":true,"protocol":1,"server":"AltirraSDL","paused":false}
ping ok
running 60 frames...
frame returned: {"ok":true,"frames":60}
ping ok (after frame step)
```

`03_input` produces the same PORTA bit patterns (up=$fe, down=$fd,
left=$fb, right=$f7, diagonals, centre=$ff), console switch state
($04 with start+select held, $07 released), keyboard scancodes,
and a successful 64-byte MEMLOAD/MEMDUMP round-trip as its C
counterpart.

## Extending

Adding a new command is a ~10-line exercise. Example — wiring up
`BOOT`:

```pascal
// in interface:
procedure Boot(const Path: String);

// in implementation:
procedure TAltirraBridge.Boot(const Path: String);
begin
  CheckOk(Rpc('BOOT ' + Path), 'BOOT');
end;
```

That's it. The protocol is text-based and stable; see
[`../../docs/COMMANDS.md`](../../docs/COMMANDS.md) for the full
verb list and [`../../docs/PROTOCOL.md`](../../docs/PROTOCOL.md)
for the request/response shape of each.

## See also

- [`../c/`](../c/) — C client SDK (single-file `altirra_bridge.h`
  + `.c`, libc + Winsock only).
- [`../python/`](../python/) — Python client SDK (pure stdlib, no
  pip dependencies).
- [`../../docs/WRITING_A_CLIENT.md`](../../docs/WRITING_A_CLIENT.md)
  — design rationale for shipping native clients per language
  rather than a C ABI shared library, plus minimal
  proof-of-concept clients in Rust, Free Pascal, and Go.
