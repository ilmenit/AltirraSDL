# Python SDK quickstart

The `altirra_bridge` package is pure stdlib (`socket`, `json`,
`base64`). No external dependencies. Drop the `altirra_bridge/`
directory anywhere on `PYTHONPATH` and import.

```python
from altirra_bridge import (
    AltirraBridge,    # client connection class
    BridgeError,      # base exception
    AuthError,        # HELLO rejected
    RemoteError,      # server returned ok=false
    RawFrame,         # rawscreen() return type
)
```

## Connecting

The recommended entry point is `from_token_file`:

```python
with AltirraBridge.from_token_file("/tmp/altirra-bridge-12345.token") as a:
    ...
```

It reads the bound address from line 1 of the token file, the
session token from line 2, opens the socket, performs the HELLO
handshake, and returns the ready client. The context manager
closes the socket on exit.

For environments where you already know the address (e.g. Android
via `adb forward`):

```python
a = AltirraBridge()
a.connect("tcp:127.0.0.1:54321")
a.hello("9ec0...e4")
```

## Lifecycle

```python
a.ping()                          # round-trip liveness check
a.pause()                         # halt the simulator
a.resume()                        # resume free-running
a.frame(60)                       # run exactly 60 frames then re-pause
a.quit()                          # ask the server to exit
```

The frame gate is the main timing primitive. After `a.frame(60)`
the simulator runs 60 frames; the next command blocks server-side
until the gate releases. Use it for deterministic test sequencing.

## Reading state

```python
regs    = a.regs()
print(regs["PC"], regs["A"], regs["cycles"])

zero_page = a.peek(0x80, 16)            # bytes
reset_vec = a.peek16(0xfffc)            # int (little-endian word)

ant   = a.antic()                       # decoded ANTIC registers
gtia  = a.gtia()                        # decoded GTIA registers
pokey = a.pokey()                       # decoded POKEY registers
pia   = a.pia()                         # decoded PIA + PORT outputs

dl    = a.dlist()                       # walked display list, list of dicts
hw    = a.hwstate()                     # CPU + ANTIC + GTIA + POKEY + PIA
pal   = a.palette()                     # 768 bytes RGB24
```

`peek()` is debug-safe — reading I/O addresses ($D000-$D7FF) does
NOT trigger ANTIC/GTIA/POKEY side effects. Use it for inspection;
use `poke()` writes through the same path.

## Writing state and injecting input

```python
a.poke(0x600, 0xab)
a.poke16(0x602, 0x1234)
a.memload(0x4000, open("loader.bin", "rb").read())
data = a.memdump(0x4000, 0x100)         # bytes

a.joy(0, "upright", fire=True)          # joystick port 0
a.joy(0, "centre")                      # release direction, keep fire as-is
a.key("A", shift=True)                  # types capital A
a.consol(start=True, select=True)       # hold both switches
a.consol()                              # release all three
```

Boot, reset, save-state, config:

```python
a.boot("/path/to/game.xex"); a.frame(120)
a.mount(0, "/path/to/disk.atr")          # D1:
a.cold_reset()                           # preserves pause state
a.warm_reset()
a.state_save("/tmp/save.altstate"); a.frame(1)
a.state_load("/tmp/save.altstate"); a.frame(1)

# Simulator configuration — query or set
cfg = a.config()                         # all keys
a.config("basic", "false")               # disable BASIC (no reset)
a.config("machine", "800")               # set Atari 800 (triggers reset)
a.config("memory", "48K")                # set 48K RAM (triggers reset)
a.config("debugbrkrun", "true")          # break at EXE run address
```

Async commands (`boot`, `mount`, `state_save`, `state_load`) are
queued via the SDL3 deferred-action system in `AltirraSDL --bridge`,
or executed inline in `AltirraBridgeServer`. Either way, issue
`frame(N)` afterwards to wait for the action to complete before
reading state.

## Rendering

Three commands. Two output formats (PNG and raw XRGB8888). Two
delivery modes (inline base64 and server-side path). All four
combinations work.

```python
# Inline PNG (works over adb forward)
png = a.screenshot()
open("frame.png", "wb").write(png)

# Server-side write
a.screenshot(path="/tmp/frame.png")

# Raw XRGB8888 — bytes on the wire are B, G, R, 0
raw = a.rawscreen()
print(raw.width, raw.height, len(raw.pixels))
# RawFrame.pixels_rgba() reorders to R, G, B, A for PIL etc.
rgba = raw.pixels_rgba()
# from PIL import Image
# img = Image.frombytes("RGBA", (raw.width, raw.height), rgba)

# Convenience alias for screenshot inline
png = a.render_frame()
```

The Atari frame is **always 336 × 240** in NTSC. The headless
`AltirraBridgeServer` build installs a null video display so
`mpLastFrame` is populated even without a real backend — the
rendering commands work identically in headless and GUI mode.

## Debugger introspection

```python
# Disassemble at the reset vector — uses Altirra's real disassembler
# (symbol resolution, 6502/65C02/65C816 mode auto-pick, illegal opcodes)
for ins in a.disasm(0xe477, count=8):
    print(ins["addr"], ins["text"])

# Last 32 instructions executed (Altirra ring-buffer history)
for h in a.history(32):
    print(f"{h['cycle']:>10}  {h['pc']}  op={h['op']}  a={h['a']}")

# Expression evaluator — same syntax as Altirra's debugger console.
# Lowercase register names. Word deref via dw(), byte deref via db().
print(a.eval_expr("pc"))
print(a.eval_expr("dw($fffc)"))           # reset vector value
print(a.eval_expr("db($80) + a"))

# JSR/RTS call stack
for f in a.callstack(8):
    print(f["pc"])

# Decoded memory layout from PIA PORTB + cartridge
for region in a.memmap():
    print(region["name"], region["lo"], "-", region["hi"], region["kind"])

print(a.bank_info())                      # XL/XE banking decode
print(a.cart_info())                      # mapper mode + bank
print(a.pmg())                            # decoded player/missile state
print(a.audio_state())                    # decoded POKEY channels
```

## Breakpoints and watchpoints

```python
bp = a.bp_set(0xe477)                     # PC breakpoint, returns id
print(a.bp_list())                        # list every active BP
a.bp_clear(bp)
a.bp_clear_all()

# Read watchpoint at RANDOM
ids = a.watch_set(0xd40b, mode="r")       # returns [id]

# Read+write watch — creates TWO breakpoints (one read, one write).
# Returns both ids; rolls back the first if the second fails.
ids_rw = a.watch_set(0xd40b, mode="rw")   # returns [id_r, id_w]
```

The conditional breakpoint syntax uses Altirra's expression
evaluator (lowercase register names). The condition value cannot
contain spaces — the bridge tokenizer is space-split.

```python
# Break only when X register is zero at $0600
a.bp_set(0x600, condition="x==0")
```

A bad condition raises `RemoteError` cleanly — the breakpoint is
not created.

## Symbols and memory search

```python
mod = a.sym_load("/path/to/game.lab")     # MADS .lab/.lbl/.lst, .sym, etc.
addr = a.sym_resolve("SIOV")              # → $e459
sym  = a.sym_lookup(0xe459)               # → {'name':'SIOV','base':'$e459','offset':0}

# Find every JSR to $e459 in cartridge ROM
hits = a.memsearch(b"\x20\x59\xe4", start=0xa000, end=0xc000)
```

`memsearch` works over the CPU view (banking-aware) and is capped
at 1024 hits. Pattern is raw bytes; the SDK hex-encodes on the wire.

## Profiler

```python
a.profile_start(mode="insns")
a.frame(300)                              # collect data
a.profile_stop()
report = a.profile_dump(top=20)
print(report["total_cycles"])
for h in report["hot"]:
    print(f"{h['addr']}  cycles={h['cycles']:>8}  insns={h['insns']:>6}")
```

Hierarchical call tree:

```python
a.profile_start(mode="callgraph")
a.frame(300); a.profile_stop()
for node in a.profile_dump_tree():
    print(f"ctx={node['ctx']:3}  parent={node['parent']:3}  "
          f"addr={node['addr']}  incl={node['incl_cycles']}")
```

**`profile_dump*` is destructive**: the second call returns empty
data. Restart the profiler to collect a fresh session.

## Verifier

```python
# Bit flags from Altirra's verifier.h:
#   0x001 UndocumentedKernelEntry, 0x002 RecursiveNMI,
#   0x004 InterruptRegs,           0x008 64KWrap,
#   0x010 AbnormalDMA,             0x020 AddressZero,
#   0x040 LoadingOverDisplayList,  0x080 CallingConventionViolations,
#   0x100 NonCanonicalHardwareAddress, 0x200 StackWrap,
#   0x400 StackInZP816
a.verifier_set(0x208)                     # StackWrap + RecursiveNMI
print(a.verifier_status())
a.verifier_set(None)                      # disable
```

Verifier violations are reported through Altirra's debugger console
output. v1 of the bridge does not expose a structured violation log.

## Error handling

Every command method raises on failure. Three exception types:

```python
from altirra_bridge import BridgeError, AuthError, RemoteError

try:
    a.bp_set(0x600, condition="bad syntax @@@")
except RemoteError as e:
    print("server rejected:", e.error)
except AuthError:
    print("token wrong")
except BridgeError as e:
    print("transport / protocol failure:", e)
```

`RemoteError.error` is the server-side error string verbatim.
`AuthError` is a subclass of `BridgeError` raised only by `hello()`.
Use `BridgeError` as a catch-all if you don't care which.

## Higher-level Python tools

```python
from altirra_bridge import parse_xex, load_xex, Project

# XEX parsing — pure local file work
img = load_xex("/path/to/game.xex")
print(img.runad, len(img.segments))
for seg in img.segments:
    print(f"  ${seg.start:04x}-${seg.end:04x}  ({seg.length} bytes)")

# Persistent RE project state
p = Project.new("my game", source_path="/path/to/game.xex")
p.label(0x6000, "main_loop")
p.comment(0x6000, "called from VBI")
p.add_note(0x6000, 0x6020, "input handling")
p.save("/path/to/project.json")
p.export_lab("/path/to/labels.lab")
# ... then in another session:
p2 = Project.load("/path/to/project.json")
```

```python
# MADS source export — combines bridge DISASM with project labels
from altirra_bridge import asm_writer
with open("game.s", "w") as f:
    asm_writer.emit_xex(f, bridge, img, project)
```

## Threading

Not thread-safe. Use one `AltirraBridge` per thread. The underlying
socket is single-client by design (the server accepts one
connection at a time and the bridge dispatcher runs single-threaded
on the SDL3 main thread).
