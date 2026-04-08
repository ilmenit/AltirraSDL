# AltirraBridge workflow playbooks

Task-oriented recipes. Pick the one that matches the user's goal,
adapt the script.

All examples use the Python SDK. Translate to C by replacing
`a.method(...)` with `atb_method(c, ...)` per
[`c-quickstart.md`](c-quickstart.md).

---

## 1. Boot a game and capture the title screen

```python
from altirra_bridge import AltirraBridge

with AltirraBridge.from_token_file(sys.argv[1]) as a:
    a.boot("/path/to/game.xex")
    a.frame(180)                       # let the game finish loading
    open("title.png", "wb").write(a.screenshot())
```

`frame(N)` waits server-side until N frames have been emulated, so
your screenshot is taken from a deterministic point in the game's
boot sequence. Adjust `N` per game.

For multi-stage loaders (cassette images, multi-disk software),
allow more frames or query the loader's "ready" condition with
`peek()` / `eval_expr()`.

---

## 2. Walk a game through every level and screenshot each

```python
def screenshot_level(a, level_idx):
    # Game-specific: write the level number into RAM and trigger
    # whatever the game uses to advance. Inspect with the debugger
    # first to find these addresses.
    a.poke(0x80, level_idx)
    a.poke(0x82, 1)                    # "advance" flag
    a.frame(60)                        # let the level draw
    open(f"level_{level_idx:02d}.png", "wb").write(a.screenshot())

with AltirraBridge.from_token_file(sys.argv[1]) as a:
    a.boot("/path/to/game.xex")
    a.frame(180)
    for i in range(32):
        screenshot_level(a, i)
```

Pattern: deterministic state injection (`poke`), one-frame settle
(`frame`), inline capture (`screenshot`). Repeat in a tight loop.

---

## 3. AI gameplay agent — frame-step + see + decide

```python
def frame_observation(a):
    """Return everything the agent needs to make a decision."""
    return {
        "frame":  a.rawscreen(),       # RawFrame (XRGB8888)
        "regs":   a.regs(),
        "score":  a.peek(0x88, 2),     # game-specific
        "lives":  a.peek(0x90, 1)[0],
    }

def act(a, action):
    if action == "left":  a.joy(0, "left")
    elif action == "right": a.joy(0, "right")
    elif action == "fire":  a.joy(0, "centre", fire=True)
    else:                   a.joy(0, "centre")

with AltirraBridge.from_token_file(sys.argv[1]) as a:
    a.boot("/path/to/game.xex"); a.frame(180)
    while not done(a):
        obs = frame_observation(a)
        act(a, agent.choose(obs))
        a.frame(1)                      # one frame per action
```

Bridge owns its own input slot, so injected joystick state does
NOT fight with real-gamepad input from the host. Disconnect cleanup
releases everything automatically.

---

## 4. Find where a value lives in RAM (search-and-narrow)

```python
def snapshot_ram(a):
    return a.memdump(0x0000, 0x10000)

with AltirraBridge.from_token_file(sys.argv[1]) as a:
    a.boot("/path/to/game.xex"); a.frame(180)

    # Capture the snapshot when the value is X
    snap_a = snapshot_ram(a)

    input("change the value in-game then press Enter")
    a.frame(1)
    snap_b = snapshot_ram(a)

    # Find every byte that changed
    candidates = [i for i in range(0x10000) if snap_a[i] != snap_b[i]]
    print(f"{len(candidates)} candidates")
```

This is the cheat-finder workflow. For repeated narrowing, do
multiple snapshots and intersect.

For finding a known byte/word pattern in ROM:

```python
hits = a.memsearch(b"\x4c\x00\x60", start=0xa000, end=0xc000)  # JMP $6000
```

---

## 5. Conditional breakpoint loop — "stop the next time X happens"

```python
with AltirraBridge.from_token_file(sys.argv[1]) as a:
    a.boot("/path/to/game.xex"); a.frame(180)

    bp = a.bp_set(0x6000, condition="x==0")
    a.resume()
    # The simulator runs until the breakpoint hits, then halts.
    # Poll for the halt:
    while a.regs()["PC"] != "$6000":
        a.frame(1)
    print("hit:", a.regs())
    a.bp_clear(bp)
```

The condition expression uses Altirra's debugger evaluator
(lowercase register names, `db()` / `dw()` for memory deref,
arithmetic + bitwise ops, symbol lookup). The condition value
cannot contain spaces — wrap complex conditions accordingly.

---

## 6. Watchpoint — "what code touches this address?"

```python
with AltirraBridge.from_token_file(sys.argv[1]) as a:
    a.boot("/path/to/game.xex"); a.frame(180)

    # Halt on any read of $80
    a.watch_set(0x80, mode="r")

    a.resume()
    # Wait until the simulator halts at the access...
    while not a.regs()["PC"]:
        a.frame(1)
    # The accessing instruction is at PC. CALLSTACK shows how we
    # got here.
    print("accessed by:", a.regs()["PC"])
    for f in a.callstack(8):
        print("  ", f["pc"])
```

Range watches aren't supported by the underlying API. Set one
`watch_set` per address. `mode="rw"` creates two breakpoints (one
read, one write); `bp_clear` each id from the returned list.

---

## 7. Profile a hot inner loop

```python
with AltirraBridge.from_token_file(sys.argv[1]) as a:
    a.boot("/path/to/game.xex"); a.frame(60)

    a.profile_start(mode="insns")
    a.frame(300)                                # collect 5 seconds at 60 fps
    a.profile_stop()

    report = a.profile_dump(top=20)
    print(f"total cycles: {report['total_cycles']}")
    for h in report["hot"]:
        print(f"{h['addr']}  {h['cycles']:>10}  ({h['insns']} insns)")
```

For a hierarchical call tree:

```python
    a.profile_start(mode="callgraph")
    a.frame(300); a.profile_stop()

    nodes = a.profile_dump_tree()
    by_ctx = {n["ctx"]: n for n in nodes}
    for n in sorted(nodes, key=lambda n: -n["incl_cycles"])[:10]:
        depth = 0
        c = n
        while c["parent"] != 0 and depth < 16:
            depth += 1
            c = by_ctx.get(c["parent"], {"parent": 0})
        print(f"{'  '*depth}{n['addr']}  incl={n['incl_cycles']}")
```

**Reminders:**
- Always `profile_stop()` before `profile_dump*()` — the bridge
  rejects dumps on a running profiler.
- `profile_dump*()` is destructive: the second call returns empty.
  Restart the profiler to collect new data.

---

## 8. Save / load state for deterministic test branches

```python
with AltirraBridge.from_token_file(sys.argv[1]) as a:
    a.boot("/path/to/game.xex")
    a.frame(180)
    a.state_save("/tmp/checkpoint.altstate"); a.frame(1)

    for trial in range(10):
        a.state_load("/tmp/checkpoint.altstate"); a.frame(1)
        # Try a different input sequence each time
        run_strategy(a, trial)
        open(f"trial_{trial}.png", "wb").write(a.screenshot())
```

`cold_reset()` and `warm_reset()` preserve the simulator's pause
state — a paused emulator stays paused after reset. Rely on this
invariant.

---

## 9. Drive the Atari as a custom display device

(See `examples/04_paint.py` for a fully worked-out paint program
that does this.)

```python
DL_ADDR     = 0x1000
SCREEN_ADDR = 0x2000
ROW_BYTES   = 40   # ANTIC mode D = 160x96, 4 colors, 2 bpp

def build_dlist():
    dl  = bytes([0x70, 0x70, 0x70])                  # 24 blank scanlines
    dl += bytes([0x4D, SCREEN_ADDR & 0xff, SCREEN_ADDR >> 8])  # mode D + LMS
    dl += bytes([0x0D] * 95)                          # 95 more rows
    dl += bytes([0x41, DL_ADDR & 0xff, DL_ADDR >> 8]) # JVB
    return dl

def setup(a):
    a.pause()
    a.memload(DL_ADDR, build_dlist())
    a.memload(SCREEN_ADDR, bytes(40 * 96))            # blank
    a.poke(0x0230, DL_ADDR & 0xff)                    # SDLSTL — OS shadow
    a.poke(0x0231, DL_ADDR >> 8)                      # SDLSTH
    a.poke(0x02C4, 0x0F)                              # COLOR0 = white
    a.poke(0x02C5, 0x46)                              # COLOR1 = red
    a.poke(0x02C6, 0xC8)                              # COLOR2 = green
    a.poke(0x02C8, 0x00)                              # COLBK  = black
    a.resume()

def pack_pixel(buf, x, y, color):
    """Mode D pixel pack: 4 px / byte, 2 bpp MSB-first."""
    off = y * ROW_BYTES + (x >> 2)
    sh  = (3 - (x & 3)) * 2
    buf[off] = (buf[off] & ~(0x03 << sh)) | ((color & 3) << sh)

with AltirraBridge.from_token_file(sys.argv[1]) as a:
    setup(a)
    shadow = bytearray(40 * 96)
    pack_pixel(shadow, 80, 48, 1)                     # one pixel
    a.memload(SCREEN_ADDR, bytes(shadow))
    a.frame(2)
    open("custom.png", "wb").write(a.screenshot())
```

Trick: write the OS display-list shadow registers `$0230/$0231`
instead of ANTIC's `$D402/$D403` directly. The OS VBI re-asserts
shadow→hardware every frame, so this sticks. Ditto for color
register shadows `$02C4-$02C8`.

---

## 10. Headless CI test — boot, screenshot, exit cleanly

```python
import sys, subprocess, time, re
from altirra_bridge import AltirraBridge

# Spawn the headless server (no GUI window)
proc = subprocess.Popen(
    ["AltirraBridgeServer", "--bridge=tcp:127.0.0.1:0"],
    stderr=subprocess.PIPE, text=True)

# Read the listen address + token-file path from stderr
addr = None
tokf = None
for line in proc.stderr:
    if (m := re.search(r"listening on (\S+)", line)): addr = m.group(1)
    if (m := re.search(r"token-file: (\S+)", line)): tokf = m.group(1)
    if addr and tokf: break

a = AltirraBridge.from_token_file(tokf)
try:
    a.boot("/path/to/game.xex"); a.frame(180)
    open("ci-out.png", "wb").write(a.screenshot())
finally:
    a.quit()
    proc.wait(timeout=5)
```

Use `AltirraBridgeServer` (not `AltirraSDL --bridge`) for CI: no
window, no fonts, no librashader, ~30% smaller binary. Same
protocol, same SDK. Linux + macOS + Windows desktop, plus Android
for the bridge built into the AltirraSDL APK.

---

## Common pitfalls

- **Don't disassemble in Python.** `a.disasm()` already wraps
  Altirra's `ATDisassembleInsn` (symbol resolution, mode auto-pick,
  illegal opcodes). Anything you'd build client-side will be worse.
- **Don't assume `frame(N)` blocks the client.** It returns
  immediately after queuing the gate. The *next* command after it
  blocks server-side until the gate releases. So `frame(60); regs()`
  reads the registers as of the end of the run, not the start.
- **Don't `poke()` directly into ANTIC/POKEY/GTIA hardware
  registers expecting side effects.** `poke()` is debug-safe — it
  writes the latch without invoking the chip's write-handler.
  Address the OS shadow ($02xx range) instead, or use a future
  `POKE_HW` if you really need the hardware-trigger semantics.
- **Don't `peek()` `$D40A` (WSYNC) expecting a wait.** Same
  reason — debug reads don't trigger chip side effects.
- **Don't share an `AltirraBridge` across threads.** Use one per
  thread. The server is single-client anyway; multiplex through
  one connection from one thread.
- **Don't leave state stuck on disconnect.** It won't — the bridge
  cleanup releases joystick directions, keys, console switches and
  the input slot when the client disconnects. But knowing this is
  important when debugging "why is my emulator behaving weirdly
  after the test crashed".
