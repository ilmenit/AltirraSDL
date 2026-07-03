# AltirraBridge Protocol — v1

This document is the wire contract. Every client SDK and every
language binding implements what's described here. The protocol is
versioned via the `protocol` field in the `HELLO` response; clients
should refuse to talk to a server that announces a higher major
version than they understand.

## Overview

AltirraBridge is a request/response protocol over a single byte
stream (loopback TCP by default; POSIX UDS optionally). Both ends are
single-threaded:

- The **client** sends one command at a time and waits for the
  response before sending the next.
- The **server** processes commands inside the AltirraSDL main loop,
  one at a time, on the SDL3 main thread. There is no concurrent
  command execution and no need for client-side locking beyond
  "don't share one client across threads".

There are no out-of-band messages, no server-initiated events, no
keep-alives. If you need long-running observation (e.g. "tell me when
this address is written"), use breakpoints/tracepoints (Phase 5) and
poll their results.

## Transport

### Address forms

The server's `--bridge` argument and every client SDK accept the
same address forms:

| Form                          | Platforms          | Notes                                  |
|-------------------------------|--------------------|----------------------------------------|
| `--bridge`                    | all                | shorthand for `tcp:127.0.0.1:0`         |
| `tcp:127.0.0.1:PORT`          | all                | loopback TCP; default                   |
| `tcp:127.0.0.1:0`             | all                | OS picks an ephemeral port              |
| `unix:/path/to/socket`        | Linux, macOS       | filesystem UDS                          |
| `unix-abstract:NAME`          | Linux, Android     | abstract-namespace UDS                  |

The server **always binds 127.0.0.1**, never `0.0.0.0`, regardless
of what the client passes. Remote-network exposure is by design
impossible.

On Android, the bridge runs inside the app process and is reachable
from a host machine via `adb forward tcp:PORT tcp:PORT`.

### Token file

On startup the server generates a 128-bit random session token and
writes it to `<tmpdir>/altirra-bridge-<pid>.token` (mode `0600` on
POSIX). The file format is two lines:

```
<bound-address>
<32-hex-token>
```

The bound address line is exactly the form the client passes to its
`connect()` call (e.g. `tcp:127.0.0.1:54321`).

The server also logs the connection details to **stderr** (so a client
capturing stdout doesn't have to filter):

```
[bridge] listening on tcp:127.0.0.1:54321
[bridge] token-file: /tmp/altirra-bridge-12345.token
[bridge] log-file: /tmp/altirra-bridge-12345.log
[bridge] token: 9ec0...e4
```

The persistent log file is written next to the token file and records
startup configuration, selected firmware, client connect/disconnect,
state-changing bridge commands, BOOT requests, and failed commands. The
session token is intentionally not written to the persistent log.

### Framing

Every command from client to server is one line of UTF-8 terminated
by `\n`. Bare `\r\n` is also accepted (the trailing `\r` is
stripped). Empty lines are no-ops and are not responded to — useful
as keepalives if you need them.

Every response from server to client is one line of UTF-8 JSON
terminated by `\n`. The response always starts with `{` and is
always a single object containing at least an `"ok"` field. There
are no multi-line responses, no streamed payloads.

Maximum line length is unbounded by the protocol. Phase 5+ commands
that return memory dumps or instruction histories will produce
hundred-kilobyte responses; client implementations must read until
`\n`, not into a fixed buffer.

## Commands

Commands are space-separated tokens: a verb in uppercase followed by
zero or more arguments. Quoting/escaping for arguments containing
spaces is **not** supported in v1; commands that need binary or
spaces use base64 inline payloads or `path:`-on-server arguments
(introduced in later phases).

Numeric arguments accept three forms:

- decimal: `60`, `4096`
- C hex: `0x3c`, `0x1000`
- Atari hex: `$3c`, `$1000`

### Authentication

The first command after connect **must** be `HELLO`. Until it
succeeds, every other command returns `{"ok":false,"error":"auth
required"}` and is **not** processed.

#### `HELLO <token>`

Authenticate with the session token from the token file.

Response on success:

```json
{"ok":true,"protocol":1,"server":"AltirraSDL","paused":false}
```

| Field      | Type    | Meaning                                  |
|------------|---------|------------------------------------------|
| `protocol` | integer | Protocol major version. v1 here.         |
| `server`   | string  | Server identifier (always `AltirraSDL`). |
| `paused`   | boolean | Whether the simulator is currently paused. |

Response on bad token:

```json
{"ok":false,"error":"bad token"}
```

The connection is **not** closed by the server on bad token —
clients can retry. (Repeated wrong tokens may be rate-limited in a
later version.)

### Phase 1 commands

#### `PING`

Liveness check. Returns `{"ok":true}`. Does not advance emulation.

#### `PAUSE`

Pause the simulator. Cancels any active `FRAME` gate. Returns
`{"ok":true}`. Idempotent.

#### `RESUME`

Resume free-running emulation. Cancels any active `FRAME` gate.
Returns `{"ok":true}`. Idempotent.

#### `FRAME [n]`

Run `n` frames then re-pause. `n` defaults to `1`; must be in
`[1, 1000000]`.

```
> FRAME 60
< {"ok":true,"frames":60}
```

The response is sent **immediately**, before the frames have
actually been emulated. The server's frame-gate counter then
decrements once per frame in the main loop and re-pauses the
simulator when the counter hits zero.

This means that the client's *next* command after `FRAME` is
implicitly synchronous on the gate: the simulator is still running
during it, but commands are dispatched on the same main thread that
runs frames, so the next command will be processed only between
frames. To wait for the gate to fully release before issuing the
next command, send a `PING` (or any read command) — its response
will not be sent until the gate has released and the simulator is
paused again.

In GUI mode, the normal frame pacer controls wall-clock speed, so
gated frames complete at the selected PAL/NTSC rate. In headless or
offscreen mode, and in `AltirraBridgeServer`, the default is
`--pacing=unlimited`: no wall-clock sleep is inserted, so the same
emulated PAL/NTSC frame timing is preserved inside the simulator, but
frames are produced as fast as the host CPU can run them. Pass
`--pacing=realtime` to a headless target to sleep to the selected
PAL/NTSC frame rate instead.

`AltirraBridgeServer` is also settings-isolated by default: it does not
read the Windows registry or `settings.ini`, and starts from code-defined
XL/XE, 320K, PAL defaults. Use `AltirraBridgeServer --settings=user` to
opt into AltirraSDL settings/profile data. `AltirraSDL --bridge` remains
the full frontend and always uses normal app settings.

If the simulator was paused before `FRAME`, it is `Resume()`d
implicitly, run for `n` frames, then `Pause()`d again. If it was
already running, the gate just sets a "pause after `n` more frames"
deadline.

#### `QUIT`

Ask the AltirraSDL process to exit cleanly. The server flips the
main loop's `g_running` flag; the simulator finishes its current
frame, the bridge socket closes, settings are saved, and AltirraSDL
exits. Returns `{"ok":true}` before the actual exit.

`QUIT` is gated by authentication like every other command — an
unauthenticated client cannot kill the emulator.

### Phase 2 commands — state read

All Phase 2 commands are pure reads. They take a snapshot of CPU,
memory, or chip state without modifying anything. They wait for the
frame gate (so e.g. `frame(60); regs()` returns the registers as of
the end of the 60-frame run, not the start).

#### `REGS`

CPU registers, decoded status flags, cycle counter, and CPU mode.

```json
{
  "ok": true,
  "PC":     "$516d",
  "A":      "$98",
  "X":      "$0e",
  "Y":      "$40",
  "S":      "$ff",
  "P":      "$33",
  "flags":  "---B--ZC",
  "cycles": 3490645,
  "mode":   "6502"
}
```

| Field    | Type    | Notes                                                       |
|----------|---------|-------------------------------------------------------------|
| `PC`     | hex16   | Program counter.                                            |
| `A`/`X`/`Y`/`S`/`P` | hex8 | Accumulator / index regs / stack pointer / status. |
| `flags`  | string  | 8-char decode of `P`. See below.                             |
| `cycles` | integer | Cycle counter since last reset. Wraps at 2³² (~71 minutes). |
| `mode`   | string  | `"6502"`, `"65C02"`, or `"65C816"`.                         |

The `flags` string is `NV-BDIZC` for 6502/65C02/65C816 emulation mode
and `NVMXDIZC` for 65C816 native mode (bits 4-5 are X/M instead of
B/unused). Set bits are uppercase letters; clear bits are `-`.

#### `PEEK addr [length]`

Read `length` bytes (default 1, max 16384) starting at `addr`. Uses
the CPU's view of memory: PORTB banking, cartridge mapping and OS
ROM overlay are all applied, exactly matching what the debugger
memory pane shows. The "Debug" prefix bypasses I/O register side
effects, so reading $D000-$D7FF does NOT trigger ANTIC/GTIA/POKEY
state changes.

`addr` must be 0..$FFFF; the range must not cross $FFFF. For raw
banked-memory access (RAMBO/130XE/cartridge banks bypassing PORTB),
see `PEEK_BANK` in Phase 5.

```json
{"ok":true,"addr":"$0080","length":16,"data":"00000018781f00..."}
```

`data` is a lowercase hex string with no separators, two characters
per byte, in increasing-address order.

#### `PEEK16 addr`

Convenience for the common "read a 16-bit pointer" case. Reads two
bytes at `addr` and `addr+1` and returns the little-endian word.
`addr` must be 0..$FFFE.

```json
{"ok":true,"addr":"$02e0","value":"$0000"}
```

#### `ANTIC`

ANTIC register state plus current display list pointer and beam
position.

```json
{
  "ok": true,
  "DMACTL": "$3d", "CHACTL": "$02",
  "DLISTL": "$14", "DLISTH": "$30",
  "HSCROL": "$00", "VSCROL": "$00",
  "PMBASE": "$2c", "CHBASE": "$e0",
  "NMIEN":  "$c0",
  "DLIST":  "$3014",
  "beam_x": 0,     "beam_y": 248
}
```

#### `GTIA`

All 32 GTIA registers ($D000-$D01F), each named, plus the current
console-switch input state.

```json
{
  "ok": true,
  "HPOSP0": "$40", "HPOSP1": "$60", ...,
  "COLPF0": "$08", "COLPF1": "$5c", "COLPF2": "$50", "COLPF3": "$14",
  "COLBK":  "$50",
  "PRIOR":  "$01", "VDELAY": "$00", "GRACTL": "$03",
  "HITCLR": "$00", "CONSOL": "$0f",
  "consol_in": "$07"
}
```

`HITCLR` and `CONSOL` (write side) reflect the **last value written**
to the corresponding register, since both are write-trigger or
write-only on hardware. They are included for debugging
completeness; for actual collision state and console-switch input,
use the dedicated read-side fields (`consol_in` here; collision
register access in Phase 5).

#### `POKEY`

POKEY register state. Includes the four audio channels' frequency
and control bytes (`AUDF1..4`, `AUDC1..4`), plus `AUDCTL`, `IRQEN`,
`SKCTL`, and the write-trigger registers (`STIMER`, `SKREST`,
`POTGO`, `SEROUT`) which reflect the last value written.

```json
{
  "ok": true,
  "AUDF1": "$00", "AUDC1": "$00", "AUDF2": "$00", "AUDC2": "$00",
  "AUDF3": "$28", "AUDC3": "$00", "AUDF4": "$00", "AUDC4": "$00",
  "AUDCTL": "$28",
  "STIMER": "$00", "SKREST": "$00", "POTGO": "$01", "SEROUT": "$00",
  "IRQEN":  "$c0", "SKCTL":  "$13"
}
```

The KBCODE / RANDOM / IRQST / SKSTAT read-side registers and the
POT0..POT7 inputs are not in this dump; access them via dedicated
commands in Phase 5.

#### `PIA`

PIA register state plus computed port outputs (with DDR applied).

```json
{
  "ok": true,
  "ORA":  "$00", "DDRA": "$00", "CRA": "$3c",
  "ORB":  "$7f", "DDRB": "$ff", "CRB": "$3c",
  "PORTA_OUT": "$ff", "PORTB_OUT": "$7f"
}
```

#### `DLIST`

Walk the active ANTIC display list and return decoded entries.
Each entry has the same `addr`/`ctl`/`mode`/`kind`/`dli` fields,
plus mode-specific fields determined by the `kind`:

| `kind`     | mode   | Extra fields                                            |
|------------|--------|---------------------------------------------------------|
| `blank`    | 0      | `blank_lines` (1..8)                                    |
| `jmp`      | 1      | `jvb=false`, `target` (jump destination, hex16)         |
| `jvb`      | 1      | `jvb=true`,  `target` (jump destination, hex16)         |
| `graphics` | 2..15  | `lms`, `hscrol`, `vscrol`, `pf`, `dmactl`, `chbase`     |

```json
{
  "ok": true,
  "dlist": "$3014",
  "entries": [
    {"addr":"$3014","ctl":"$70","mode":0,"dli":false,"kind":"blank","blank_lines":8},
    {"addr":"$3015","ctl":"$70","mode":0,"dli":false,"kind":"blank","blank_lines":8},
    {"addr":"$301d","ctl":"$42","mode":2,"dli":false,"kind":"graphics","lms":true,"hscrol":false,"vscrol":false,"pf":"$8c00","dmactl":"$3d","chbase":224}
  ]
}
```

The `dli` field is meaningful for all kinds. The `lms` /
`hscrol` / `vscrol` flags are only meaningful (and only present)
for graphics modes. Mode-1 entries also include the jump target,
read directly from the bytes following the instruction.

#### `HWSTATE`

Combined snapshot in one round trip: CPU + ANTIC + GTIA + POKEY +
PIA. Equivalent to issuing `REGS` + `ANTIC` + `GTIA` + `POKEY` +
`PIA` separately, but atomic with respect to the simulator (all
five chips snapshotted between the same two `Advance()` calls).

```json
{
  "ok": true,
  "cpu":   { ... same as REGS ... },
  "antic": { ... same as ANTIC ... },
  "gtia":  { ... same as GTIA ... },
  "pokey": { ... same as POKEY ... },
  "pia":   { ... same as PIA ... }
}
```

#### `PALETTE`

GTIA's analysis palette: 256 RGB24 entries packed into a flat hex
string. Use it to convert raw GTIA color indices (0..255) into RGB
values for display, screenshot tools, or rendering.

```json
{
  "ok": true,
  "entries": 256,
  "format":  "rgb24",
  "data":    "00000011111125252535353546464657..."
}
```

`data` is `entries * 3` bytes, hex-encoded; bytes are R0, G0, B0,
R1, G1, B1, ... in increasing color-index order. The palette is
consistent across calls within a session (it depends only on the
chosen color preset, which is settings-loaded at startup).

### Phase 3 commands — state write & input injection

Phase 3 adds memory writes, joystick/keyboard/console-switch input
injection, the boot/reset/save-state lifecycle commands.

All input-injection commands acquire state in the simulator that
the bridge owns and is responsible for releasing. On client
disconnect (clean or otherwise) the bridge automatically releases:
joystick directions for all 4 ports, fire buttons, console
switches, and the POKEY key matrix. The bridge holds its own
PIA input slot allocated at startup, so injected input does not
fight with real-gamepad input from the host.

#### `POKE addr value`

Write one byte. `addr` is 0..$FFFF; `value` is 0..$FF. Uses the
debug-safe write path: writes to I/O register addresses
($D000-$D7FF) update the underlying latch *without* invoking the
ANTIC/GTIA/POKEY register write handlers, so reads don't trigger
side effects. To deliberately trigger hardware side effects (e.g.
to switch banks via `PORTB`), use a future Phase 5 `POKE_HW`
command.

```json
{"ok":true,"addr":"$0600","value":"$ab"}
```

#### `POKE16 addr value`

Convenience: write a little-endian 16-bit word. `addr` 0..$FFFE.

```json
{"ok":true,"addr":"$0602","value":"$1234"}
```

#### `MEMDUMP addr length`

Read `length` bytes (1..65536) from `addr` and return them inline
as base64. Same debug-safe read path as `PEEK` (CPU view, banking
applied, no I/O side effects). The base64 wire format makes this
work over `adb forward` on Android with no shared filesystem.

```json
{"ok":true,"addr":"$0700","length":64,"format":"base64","data":"AAECAwQF..."}
```

#### `MEMLOAD addr base64data`

Write the base64-decoded payload starting at `addr`. The full
range must fit within $0000-$FFFF. Bytes are written via the
debug-safe path (no I/O side effects).

```json
{"ok":true,"addr":"$0700","length":64}
```

#### `JOY port direction [fire]`

Inject joystick state for `port` (0..3 = joystick 1..4).

`direction` accepts named directions in two equivalent forms:

| Named | Compass | 4-bit nibble (UDLR) |
|---|---|---|
| `centre`/`none` | `c` | 0000 |
| `up` | `n` | 0001 |
| `down` | `s` | 0010 |
| `left` | `w` | 0100 |
| `right` | `e` | 1000 |
| `upleft` | `nw` | 0101 |
| `upright` | `ne` | 1001 |
| `downleft` | `sw` | 0110 |
| `downright` | `se` | 1010 |

Optional `fire` (or `1`/`true`/`down`/`press`) holds the trigger
button down. Omit or pass `release`/`0`/`false` to release.

```json
{"ok":true,"port":0,"dir":"upright","fire":true}
```

The injected state shows up in the PIA `PORTA` (or `PORTB` for
ports 2-3) read-side bits and in the GTIA `TRIG0..3` registers.

#### `KEY name [shift] [ctrl]`

Push one keystroke into POKEY's queue. `name` is a key identifier
from the bridge's key table:

- Letters: `A`..`Z` (case-insensitive — letters are the on-key,
  not capital letters; combine with `shift` to type a capital).
- Digits: `0`..`9`.
- Special: `RETURN` / `ENTER`, `SPACE`, `ESC` / `ESCAPE`,
  `TAB`, `BACKSPACE` / `DELETE` / `DEL`, `MINUS`, `EQUALS`,
  `COMMA`, `PERIOD`, `SLASH`, `SEMICOLON`, `CAPS` / `CAPSLOCK`,
  `HELP` (XL/XE Help key).

Optional words `shift` and/or `ctrl` set the corresponding KBCODE
modifier bits (POKEY KBCODE bit 7 = shift, bit 6 = ctrl). The
underlying scan code values come straight from
`src/AltirraSDL/source/input/input_sdl3.cpp`'s SDL3 keyboard
mapping table.

```json
{"ok":true,"name":"a","kbcode":"$bf"}
```

#### `CONSOL [start] [select] [option]`

Hold console switches down (active-low). Each named token
presses one switch; switches not named are released. Pass no
tokens to release all three.

```json
{"ok":true,"consol":"$04","start":true,"select":true,"option":false}
```

The `consol` field is the raw active-low byte: bit 0 = START,
bit 1 = SELECT, bit 2 = OPTION; clear bit means pressed.

#### `BOOT path`

Load and boot a media file (XEX, ATR, CAS, CAR, BIN). In
`AltirraSDL --bridge`, the path is queued via the SDL3 deferred-action
system and processed at the start of the next main-loop frame. In
`AltirraBridgeServer`, the media dispatch itself is synchronous.

For XEX/program images, a successful `BOOT` response means the emulator
accepted the image and armed the OS/HLE program loader. It does not mean
the Atari-side load has finished or that RUNAD has executed. To wait
before reading state or capturing a screenshot, send `FRAME N` and then
the read command. Typical cold XEX boots need hundreds of frames; use
`FRAME 240` or `FRAME 300` as a conservative starting point.

```json
{"ok":true,"path":"/path/to/game.xex"}
```

Path tokenisation note: until quoted-string parsing arrives in
Phase 5, paths containing spaces work via the bridge re-joining
trailing tokens with single spaces — but only one consecutive
space is preserved. Stick to space-free paths for reliability.

#### `MOUNT drive path`

Mount a disk image into a drive slot (0..14, where 0 = D1:).
Same async semantics as `BOOT`. Same path tokenisation caveat.

```json
{"ok":true,"drive":0,"path":"/path/to/disk.atr"}
```

#### `COLD_RESET`

Cold-reset the simulator (equivalent to a power-cycle). Per the
CLAUDE.md invariant that hardware-state changes must NOT silently
change running state, the bridge captures the simulator's pause
flag before the reset and explicitly restores it afterward — a
script that resets a paused simulator gets a paused simulator
back. The CPU cycle counter is NOT zeroed (it's monotonic and
useful for cross-reset timing).

```json
{"ok":true}
```

#### `WARM_RESET`

Warm-reset the simulator (equivalent to pressing the Reset key
on a real Atari). Same pause-preservation semantics as
`COLD_RESET`.

```json
{"ok":true}
```

#### Save states — `STATE_SAVE` / `STATE_LOAD` / `STATE_LIST` / `STATE_DROP`

All save-state commands run **synchronously**: by the time the
response arrives, the operation has finished. No `FRAME 1` gating
is required (clients that send one anyway will see the same state;
it costs one frame of advance).

Pause state is preserved across `STATE_LOAD` per the bridge's
[CLAUDE invariant](#pause-state): a paused simulator stays paused,
a running simulator stays running. The captured snapshot's own
running flag is not honoured -- use `RESUME` / `PAUSE` after the
load if you want a particular state.

Three modes are supported on `STATE_SAVE` and `STATE_LOAD`:

| Mode    | When to use                                                  |
|---------|--------------------------------------------------------------|
| **Path**   | Long-lived snapshots, cross-session persistence, sharing |
| **Slot**   | Session-scope checkpoint/rewind loops (no disk I/O)      |
| **Inline** | Client and server don't share a filesystem (Android, ssh) |

The on-disk format and the slot/inline blobs are byte-equivalent
`.altstate2` payloads -- a slot can be flushed to disk after the
fact, a file can be slurped into a slot for fast reuse.

All save/load responses include the following metadata:

| Field    | Type    | Description                                  |
|----------|---------|----------------------------------------------|
| `mode`   | string  | `"path"` / `"slot"` / `"inline"`             |
| `size`   | number  | size of the serialized blob, in bytes        |
| `cycle`  | number  | CPU cycle counter at the moment of save/load |
| `pc`     | hex     | CPU program counter, e.g. `"$e2a4"`          |
| `machine`| string  | hardware mode (`"800XL"`, `"5200"`, ...)     |
| `memory` | string  | RAM size (`"320K"`, `"64K"`, ...)            |
| `basic`  | bool    | whether built-in BASIC is enabled            |

The cycle counter is monotonic and does not reset across loads --
useful for "time since X" measurements, not for identifying a
specific snapshot.

##### `STATE_SAVE <path>`  /  `STATE_SAVE path=<path>`

Write the snapshot to a server-side `.altstate2` file. The
positional form is backward-compatible with the v1 wire protocol;
the `path=` form is the new explicit syntax. Paths containing
spaces are only supported via the positional form.

```json
{"ok":true,"mode":"path","path":"/tmp/state.altstate2",
 "size":54171,"cycle":4294681990,"pc":"$060f",
 "machine":"800XL","memory":"1088K","basic":false}
```

##### `STATE_SAVE slot=<name>`

Store the snapshot in an in-memory slot under `name`. The slot
survives until `STATE_DROP` is called or the server exits. Saving
to a slot that already exists overwrites it (no warning).

```json
{"ok":true,"mode":"slot","slot":"checkpoint_1",
 "size":54176,"cycle":4294675270,"pc":"$060f",
 "machine":"800XL","memory":"1088K","basic":false}
```

##### `STATE_SAVE inline=true`

Serialize the snapshot and return it base64-encoded in the
`data` field of the response.

```json
{"ok":true,"mode":"inline","encoding":"base64",
 "size":54198,"cycle":6481719,"pc":"$060f",
 "machine":"800XL","memory":"1088K","basic":false,
 "data":"UEsDBBQAAAAIAAAA..."}
```

##### `STATE_LOAD <path>`  /  `STATE_LOAD path=<path>`

Load a snapshot from a server-side file. The simulator's chip
state, RAM, and media mounts are replaced. Running state is
preserved (see invariant above).

```json
{"ok":true,"mode":"path","path":"/tmp/state.altstate2",
 "size":54171,"cycle":4294705510,"pc":"$060f",
 "machine":"800XL","memory":"1088K","basic":false}
```

##### `STATE_LOAD slot=<name>`

Load from an in-memory slot. Returns an `ok:false` error if no
slot with that name exists.

```json
{"ok":true,"mode":"slot","slot":"checkpoint_1", ...}
```

##### `STATE_LOAD data=<base64>` (or `STATE_LOAD inline=true data=<base64>`)

Load from an inline base64 blob -- typically one produced earlier
by `STATE_SAVE inline=true`.

```json
{"ok":true,"mode":"inline","size":54198, ...}
```

##### `STATE_LIST`

Enumerate every in-memory slot. The list is sorted by name.

```json
{"ok":true,"slots":[
  {"name":"checkpoint_1","size":54176,"cycle":4294675270,
   "pc":"$060f","machine":"800XL","memory":"1088K","basic":false}
],"count":1}
```

##### `STATE_DROP <name>` / `STATE_DROP slot=<name>` / `STATE_DROP all=true`

Remove one slot by name, or every slot at once with `all=true`.
The response reports the number of slots actually removed.

```json
{"ok":true,"dropped":1}
```

#### `CONFIG`

Query the full simulator configuration.

```json
{"ok":true,"basic":false,"machine":"800XL","memory":"320K","video":"ntsc","stereo":false,"vbxe":false,"covox":false,"rapidus":false,"addons":"off","exeloadmode":"default","debugbrkrun":false}
```

#### `CONFIG key`

Query a single config key.

```json
{"ok":true,"machine":"800XL"}
```

#### `CONFIG key value`

Set a config key. Returns the full config state after the set.

| Key           | Values                                                          | Notes                       |
|---------------|-----------------------------------------------------------------|-----------------------------|
| `basic`       | `true`, `false`, `on`, `off`, `1`, `0`                          | Does not trigger cold reset |
| `machine`     | `800`, `800XL`, `1200XL`, `130XE`, `XEGS`, `1400XL`, `5200`   | Triggers cold reset         |
| `memory`      | `8K`..`1088K` (see below)                                       | Triggers cold reset         |
| `video`       | `ntsc`, `pal`, `secam`, `pal60`, `ntsc50`                       | Triggers cold reset         |
| `selftest`    | `true`, `false`, `on`, `off`, `1`, `0`                          | Does not trigger cold reset |
| `fastboot`    | `true`, `false`, `on`, `off`, `1`, `0`                          | Does not trigger cold reset |
| `fppatch`     | `true`, `false`, `on`, `off`, `1`, `0`                          | Does not trigger cold reset |
| `stereo`      | `true`, `false`, `on`, `off`, `1`, `0`                          | Dual POKEY; cold reset      |
| `stereomono`  | `true`, `false`, `on`, `off`, `1`, `0`                          | Downmix dual POKEY to mono  |
| `addons`      | `on`, `off`, `modern`, `stock`                                  | Batch modern add-ons; `off`/`stock` leave memory size unchanged |
| `vbxe`, `covox`, `soundboard`, `rapidus`, `slightsid` | `on`, `off`                 | Device aliases; cold reset  |
| `siopatch`    | `on`, `safe`, `off`                                             | Disk/cassette SIO patch     |
| `burstio`, `accuratedisk`, `casautoboot`, `casautobasicboot` | booleans | CLI parity toggles          |
| `artifact`    | `none`, `ntsc`, `ntschi`, `pal`, `palhi`, `auto`, `autohi`      | Artifacting mode            |
| `axlonmemsize`| `none`, `64K`, `128K`, `256K`, `512K`, `1024K`, `2048K`, `4096K`| Cold reset                  |
| `highbanks`   | `na`, `0`, `1`, `3`, `15`, `63`, `255`                          | Cold reset                  |
| `randmem`, `randdelay` | booleans                                                 | Program-load determinism    |
| `diskemu`     | `generic`, `fastest`, `810`, `1050`, `xf551`, `usdoubler`, etc. | All disk drives             |
| `exeloadmode` | `default`, `type3poll`, `deferred`, `diskboot`                  | Program-image loader mode   |
| `kernel`      | `default`, `lle`, `llexl`, `hle`, `osa`, `osb`, `xl`, path/id   | Triggers cold reset         |
| `basicrom`    | `default`, `atbasic`, `reva`, `revb`, `revc`, path/id           | Triggers cold reset         |
| `debugbrkrun` | `true`, `false`, `on`, `off`, `1`, `0`                          | Break at EXE run address    |

Memory modes: `8K`, `16K`, `24K`, `32K`, `40K`, `48K`, `52K`, `64K`,
`128K`, `256K`, `320K`, `320K_Compy`, `576K`, `576K_Compy`, `1088K`.

Keys and values are case-insensitive. `kernel_id` / `basic_id` are the
configured firmware references; `actual_kernel_id` / `actual_basic_id`
show the firmware selected after default/autoselect resolution. Setting
`machine`, `memory`, `video`, `kernel`, or `basicrom` triggers a cold
reset with pause-state preservation (same semantics as `COLD_RESET`).

#### `DEVICE_LIST`

List installed top-level devices and bridge quick-add device tags.

```json
{"ok":true,"installed":[{"tag":"vbxe","name":"VideoBoard XE"}],
 "quick":[{"tag":"vbxe","present":true,"reboot_on_plug":false}]}
```

#### `DEVICE_GET tag`

Query a device by tag. `settings` is an object containing the device's
current `ATPropertySet` values when the device is present.

```json
{"ok":true,"tag":"vbxe","known":true,"present":true,"reset":false,
 "settings":{"version":126,"alt_page":true}}
```

#### `DEVICE_SET tag on|off [key=value...]`

Add, reconfigure, or remove a device. Common quick tags have the same
defaults as Gaming Mode/Desktop device config:

- `vbxe`: `version=126`, optional `base=d600|d700`, `shared_mem=true|false`
- `covox`: `base=d600`, `size=100`, `channels=4`
- `soundboard`: `version=120`, `base=d2c0`
- `rapidus`, `slightsid`: no default properties

Examples:

```
DEVICE_SET vbxe on version=126 base=d700 shared_mem=true
DEVICE_SET covox on base=d600 size=100 channels=4
DEVICE_SET rapidus off
```

Option tokens are whitespace-separated `key=value` pairs; values cannot
contain spaces. Successful device changes cold-reset the machine while
preserving pause state and report `"reset":true`.

#### `DEVICE_REMOVE tag` / `DEVICE_CLEAR`

Remove one non-internal device, or all non-internal devices. Both
preserve pause state and cold-reset when anything changes.

### Phase 4 commands — rendering

All Phase 4 commands return either an inline base64-encoded payload
or write to a server-side filesystem path. Inline mode is the default
and works over `adb forward` on Android with no shared filesystem.
Path mode is faster on local UDS or shared-disk setups.

The headless `AltirraBridgeServer` target installs a null
`IVDVideoDisplay` so GTIA produces frames even without a real
display backend; the rendering commands work identically in headless
and GUI mode.

#### `SCREENSHOT [path=FILE] [inline=true|false]`

Capture the last GTIA frame as a PNG. Default mode is inline.

```json
{"ok":true,"width":336,"height":240,"format":"png",
 "encoding":"base64","data":"iVBORw0KGgoAAAANSUhEUgAA..."}
```

With `path=FILE`:

```json
{"ok":true,"width":336,"height":240,"format":"png",
 "path":"/tmp/frame.png","bytes":242243}
```

The PNG encoder is vendored in the bridge module — it's a
self-contained store-block deflate writer. No external image
library is required on the server side, and the output is bit-for-
bit deterministic for a given pixel buffer.

#### `RAWSCREEN [path=FILE] [inline=true|false]`

Capture the last GTIA frame as a raw XRGB8888 buffer. Each 32-bit
word is `0x00RRGGBB` in native little-endian order, so on the wire
the bytes are `B, G, R, 0` per pixel. `stride == width * 4`.

```json
{"ok":true,"width":336,"height":240,"format":"xrgb8888",
 "endian":"little","stride":1344,
 "encoding":"base64","data":"AAAAAA..."}
```

Use this when you want to apply your own post-processing or feed
the pixels to a non-PNG codec. The total payload size is
`width * height * 4` bytes.

#### `RENDER_FRAME`

Convenience alias for `SCREENSHOT inline=true`. Reserved for a
future state-override extension; rejects unknown options today
so a client coded against a newer server fails loudly against an
older one.

```json
{"ok":true,"width":336,"height":240,"format":"png",
 "encoding":"base64","data":"iVBORw0KGgoAAA..."}
```

### Phase 5a commands — debugger introspection

Phase 5a wraps Altirra's existing debugger primitives. Every
command in this section reads state without modifying it; nothing
here halts the simulator. The bridge calls `ATInitDebugger()` at
server startup so commands work in both headless and GUI mode.

#### `DISASM addr [count]`

Disassemble `count` instructions starting at `addr` (default 1,
cap 1024). Uses `ATDisassembleInsn` — symbol resolution
(`COLDSV`/`SIOV`/etc.), 6502/65C02/65C816 mode auto-selection, and
illegal-opcode decoding all come from Altirra's battle-tested
disassembler.

```json
{"ok":true,"insns":[
  {"addr":"$e477","length":3,"bytes":["4c","d8","ee"],
   "text":"COLDSV  jmp $EED8    [$EED8] = $78","next":"$e47a"},
  {"addr":"$e47a","length":3,"bytes":["4c","b3","ed"],
   "text":"RBLOKV  jmp $EDB3    [$EDB3] = $A2","next":"$e47d"}
]}
```

#### `HISTORY [count]`

Return the last `count` CPU history entries (default 64, cap 4096),
oldest first. Backed by Altirra's 131072-entry circular history
buffer. **No pyA8 equivalent — this is the single biggest RE
primitive AltirraBridge exposes.**

```json
{"ok":true,"count":3,"entries":[
  {"cycle":4294246120,"ucycle":4294246120,"pc":"$fe79","op":"$ad",
   "a":"$ff","x":"$ff","y":"$17","s":"$f3","p":"$b1","ea":"$0014",
   "irq":false,"nmi":false}
]}
```

#### `EVAL expr`

Evaluate a debugger expression against the current CPU state. The
expression syntax is the same one Altirra's `.printf` and
conditional breakpoints use: `pc`/`a`/`x`/`y`/`s`/`p` registers
(lowercase), `db($80)` byte deref, `dw($fffc)` word deref, symbol
names, arithmetic and bitwise operators.

```json
{"ok":true,"expr":"dw($fffc)","value":61115,"hex":"$eebb"}
```

Errors propagate the parser's diagnostic:

```json
{"ok":false,"error":"EVAL: Unable to parse expression 'A+1': Unable to resolve symbol \"A\""}
```

#### `CALLSTACK [count]`

JSR/RTS call-stack walk via `IATDebugger::GetCallStack` (default
64, cap 256). Returns frames in deepest-first order; each frame is
a return PC, the stack pointer at that frame, and the saved status
flags.

```json
{"ok":true,"count":4,"frames":[
  {"pc":"$fe7e","sp":"$f3","p":"$33"},
  {"pc":"$fa3f","sp":"$f5","p":"$33"},
  {"pc":"$fa15","sp":"$f7","p":"$33"},
  {"pc":"$e5f5","sp":"$f9","p":"$33"}
]}
```

#### `MEMMAP`

Decoded memory layout derived from PIA `PORTB` + cartridge state.
This is **not** a per-byte probe; it's the same high-level summary
the Windows debugger memory-map pane shows. Each region has
`name`, `lo`, `hi`, `kind` (`ram` / `rom` / `io`), and a free-form
`note` describing the decision (e.g. "PORTB bit0=1").

```json
{"ok":true,"portb":"$fd","regions":[
  {"name":"low RAM","lo":"$0000","hi":"$3fff","kind":"ram"},
  {"name":"bank RAM","lo":"$4000","hi":"$7fff","kind":"ram",
   "note":"130XE bank window (see BANK_INFO)"},
  {"name":"BASIC","lo":"$a000","hi":"$bfff","kind":"rom","note":"PORTB bit1=0"},
  {"name":"hardware","lo":"$d000","hi":"$d7ff","kind":"io",
   "note":"GTIA/POKEY/PIA/ANTIC"},
  {"name":"OS area","lo":"$d800","hi":"$ffff","kind":"rom",
   "note":"OS ROM (PORTB bit0=1)"}
]}
```

#### `BANK_INFO`

PORTB-decoded XL/XE banking control bits.

```json
{"ok":true,"portb":"$fd","os_rom":true,"basic_enabled":true,
 "selftest_rom":false,"cpu_bank_at_window":false,
 "antic_bank_at_window":false,"xe_bank":3}
```

`xe_bank` is the 2-bit (bits 2-3 of PORTB) 130XE bank select. Bit
6 of PORTB is unused on stock 130XE; aftermarket banking schemes
(Rambo 256K, etc.) repurpose it. Read raw `portb` for those.

#### `CART_INFO`

Cartridge mapper info. `mode` is the numeric `ATCartridgeMode`
value from `cartridgetypes.h`; clients map it to a human name via
their own table (the SDK ships one).

```json
{"ok":true,"present":false,"mode":0,"size":0,"bank":-1}
```

#### `PMG`

Decoded GTIA player/missile state from the register write-side
shadow plus the collision read-side.

```json
{"ok":true,
 "hposp":["$00","$00","$00","$00"],
 "hposm":["$00","$00","$00","$00"],
 "sizep":["$00","$00","$00","$00"], "sizem":"$00",
 "grafp":["$00","$00","$00","$00"], "grafm":"$00",
 "colpm":["$00","$00","$00","$00"],
 "colpf":["$00","$00","$00","$00"],
 "colbk":"$00", "prior":"$00", "vdelay":"$00", "gractl":"$00",
 "coll_mpf":["$00","$00","$00","$00"],
 "coll_ppf":["$00","$00","$00","$00"],
 "coll_mpl":["$00","$00","$00","$00"],
 "coll_ppl":["$00","$00","$00","$00"]}
```

#### `AUDIO_STATE`

Decoded POKEY per-channel audio state, including AUDCTL flag
decoding (16-bit channel pairs, fast 1.79 MHz clock per channel,
high-pass routes, base clock). Each channel also reports the effective
POKEY timer period in machine cycles and the timer-derived waveform
frequency in Hz. `freq_hz` is `null` when the channel has no waveform
output; in 16-bit joined modes, the combined frequency is reported on
channel 2 or 4.

```json
{"ok":true,"audctl":"$28",
 "nine_bit_poly":false,"join_1_2":false,"join_3_4":false,
 "highpass_1_3":false,"highpass_2_4":false,"base_15khz":false,
 "channels":[
   {"channel":1,"audf":"$00","audc":"$00","volume":0,
    "volume_only":false,"distortion":0,"clock":"64kHz",
    "period_cycles":28,"freq_hz":null},
   ...
 ]}
```

### Phase 5b commands — breakpoints, symbols, profiler, verifier

#### `BP_SET addr [condition=EXPR]`

Set a PC breakpoint. With `condition`, the debugger only halts
when the expression is non-zero at the PC. Returns the assigned
breakpoint id.

```json
{"ok":true,"id":0,"addr":"$e477","conditional":false}
```

The condition runs through Altirra's expression evaluator (same
syntax as `EVAL`). The condition value cannot contain spaces;
clients should avoid space-bearing expressions until quoted-token
support arrives.

#### `BP_CLEAR id`

Clear a single breakpoint by id. Returns `{"ok":true,"id":N,"cleared":true}`
or an error if the id wasn't allocated.

#### `BP_CLEAR_ALL`

Clear every user breakpoint. Returns `{"ok":true}`.

#### `BP_LIST`

List every active breakpoint with its kind, address, and flags.

```json
{"ok":true,"count":1,"breakpoints":[
  {"id":0,"addr":"$e477","length":1,"pc":true,
   "read":false,"write":false,
   "oneshot":false,"continue_on_hit":false,"conditional":false}
]}
```

#### `WATCH_SET addr [mode=r|w|rw]`

Set an access (read/write) breakpoint. The underlying Altirra API
is per-byte and per-direction, so:
- `mode=r` and `mode=w` create one breakpoint, return one id.
- `mode=rw` creates **two** breakpoints (one read, one write) and
  returns both ids; if the second creation fails the first is
  rolled back so callers never see orphan state.
- Range watches (`len=N`) are **not** supported; set one
  `WATCH_SET` per address.

```json
{"ok":true,"ids":[0,1],"addr":"$d40b","mode":"rw"}
```

The breakpoint ids returned share the same pool as `BP_SET`, so
`BP_CLEAR id` and `BP_LIST` work on watchpoints too.

#### `SYM_LOAD path`

Load a symbol file via `IATDebugger::LoadSymbols`. Auto-detects
MADS `.lab`/`.lbl`/`.lst`, DEBUG.COM, Altirra `.sym`, and generic
text symbol formats. Returns the assigned module id.

```json
{"ok":true,"path":"/tmp/game.lab","module_id":3}
```

#### `SYM_RESOLVE name`

Forward lookup: symbol name → address.

```json
{"ok":true,"name":"SIOV","value":58457,"hex":"$e459"}
```

#### `SYM_LOOKUP addr [flags=rwx]`

Reverse lookup: closest symbol at or before `addr`. `flags`
restricts by symbol kind (`r`/`w`/`x` or any combination); default
matches any.

```json
{"ok":true,"addr":"$e477","name":"COLDSV","base":"$e477","offset":0}
```

#### `MEMSEARCH hexpattern [start=$XXXX] [end=$XXXX]`

Linear scan over the CPU view of memory for a byte pattern.
`hexpattern` is an even-length string of hex digits with no
separators. `start` defaults to `$0000`, `end` to `$10000`. Hit
count is capped at 1024.

```json
{"ok":true,"count":1,"hits":["$e477"]}
```

#### `PROFILE_START [mode=insns|functions|callgraph|basicblock]`

Start the CPU profiler. The default mode is `insns` (per-address
hot list). `callgraph` mode is required for `PROFILE_DUMP_TREE`.

```json
{"ok":true,"mode":"insns"}
```

The profiler is allocated lazily by `ATSimulator::SetProfilingEnabled`;
the bridge calls it for you.

#### `PROFILE_STOP`

Stop the profiler. `PROFILE_DUMP*` is **only valid after this** —
calling it on a running profiler is rejected because the underlying
session-take is destructive and would corrupt the in-progress
collector state.

#### `PROFILE_STATUS`

```json
{"ok":true,"enabled":true,"running":false}
```

#### `PROFILE_DUMP [top=N]`

Flat dump of the top `N` (default 32, cap 4096) hot addresses,
sorted by cycle cost descending. Works for `insns` / `functions` /
`basicblock` modes.

```json
{"ok":true,"total_cycles":213408,"total_insns":50345,"count":3,
 "hot":[
   {"addr":"$fe79","cycles":59471,"insns":10070,"calls":0},
   {"addr":"$fe75","cycles":44575,"insns":10069,"calls":0},
   {"addr":"$fe7e","cycles":44551,"insns":10069,"calls":0}
 ]}
```

**Calling `PROFILE_DUMP` is one-shot.** The underlying
`ATCPUProfiler::GetSession` uses `std::move(mSession)` — a second
call returns `count: 0`. To get fresh data, restart the profiler.

#### `PROFILE_DUMP_TREE`

Hierarchical call tree from `callgraph` mode. Each node has its
context index, parent context, function entry address, and both
exclusive and inclusive cycle/instruction counters (rolled up via
`ATProfileComputeInclusiveStats`). Walk the parent chain to render
the tree.

```json
{"ok":true,"count":8,"nodes":[
  {"ctx":0,"parent":0,"addr":"$0000","calls":0,
   "excl_cycles":0,"excl_insns":0,
   "incl_cycles":120145,"incl_insns":40220},
  ...
]}
```

#### `VERIFIER_STATUS`

```json
{"ok":true,"enabled":true,"flags":"$208"}
```

#### `VERIFIER_SET flags=HEX|off`

Enable the CPU verifier with the given flag bitmask, or disable it
entirely with `flags=off` / `flags=0`. Bit meanings (from
`src/Altirra/h/verifier.h`):

| Bit    | Meaning                           |
|--------|-----------------------------------|
| `0x01` | Undocumented kernel entry         |
| `0x02` | Recursive NMI                     |
| `0x04` | Interrupt register save/restore   |
| `0x08` | 64K address-space wrap            |
| `0x10` | Abnormal DMA                      |
| `0x20` | Address zero                      |
| `0x40` | Loading over display list         |
| `0x80` | Calling-convention violations     |
| `0x100`| Non-canonical hardware address    |
| `0x200`| Stack wrap                        |
| `0x400`| Stack in zero page (65C816)       |

Violations are reported through Altirra's debugger console output.
The bridge does **not** expose a structured violation log in v1
(no public log-sink API); enable the verifier and watch the
debugger output via the standard Altirra channels.

## Errors

All error responses share the shape:

```json
{"ok":false,"error":"<human-readable message>"}
```

The `error` string is intended for humans (logs, exception messages).
Clients should not parse it. Future protocol versions may add an
`"error_code"` field for programmatic dispatch — until then, use
HTTP-style "the error string is opaque".

Categories that map to errors in v1:

| Situation                              | Error string             |
|----------------------------------------|--------------------------|
| Empty command line (`\n` only)         | (no response)            |
| Unknown verb                           | `unknown command: VERB`  |
| Missing required argument              | `<verb>: ...`            |
| Bad numeric format                     | `<verb>: bad ...`        |
| Argument out of range                  | `<verb>: ... too large`  |
| Command issued before `HELLO`          | `auth required`          |
| Wrong token                            | `bad token`              |

## Versioning

This document describes **protocol version 1**. The version number is
returned in the `HELLO` response. Compatibility rules:

- A new minor version may add new commands and may add new fields to
  existing responses. Clients must ignore unknown response fields.
- A new major version may rename or remove commands. Clients must
  refuse to operate against a server with a higher major version
  than they were built against.

## Threat model

- **Local-only by design.** The server binds 127.0.0.1; the protocol
  has no concept of network authentication beyond the local-process
  shared secret in the token file.
- **Token gates command access.** Any local process that can read the
  token file can drive the bridge; the file is mode `0600` on POSIX.
- **`QUIT` requires authentication.** A local attacker without token
  access cannot crash the emulator.
- **No filesystem-touching commands without authentication.** Phase
  3+ commands that read/write files (`MEMDUMP`, `STATE_SAVE`, etc.)
  are gated by `HELLO` like everything else.
- **Not a sandbox.** A client that knows the token has full control
  of the simulator including arbitrary memory writes. The bridge is
  intended for trusted automation, not for exposing untrusted code
  to the emulator.

## Roadmap

Phases 1 through 5b are in `v1`. The remaining gaps (deferred to
`v1.1` or later) are:

- **Tracepoints with format strings** — `BP_SET ... action=log
  format="A=%a"`. Altirra supports this internally
  (`mbContinueExecution=true` + `mpCommand`); the bridge needs a
  tokenizer that handles quoted format strings.
- **`SIO_TRACE on|off|log`** — Altirra's SIO trace toggle is private
  to the debugger implementation; needs a `QueueCommand(".siotrace")`
  dispatch path or a public API addition.
- **`VERIFIER_REPORT` violation log** — no public log sink today; the
  verifier writes only to the debugger console.
- **`SYM_FIND substr`** — substring search across all symbol modules;
  needs multi-module enumeration logic.
- **`MEMDIFF`** — trivial client-side; server-side would just be a
  convenience.
- **`RENDER_FRAME` state-override** — render a frame from a passed-in
  CPU/ANTIC/GTIA state vector. Reserved in the protocol.

See `AltirraBridge/README.md` for the full roadmap and the planned
client SDK growth (Python `loader`/`project`/`asm_writer` modules).
