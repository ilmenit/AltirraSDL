# Using Altirra as a display device

AltirraBridge lets you drive the Atari 8-bit as an **interactive graphics
renderer** from any external process. There are three distinct patterns,
with different trade-offs and different primitive sets. Picking the right
one for your use case keeps the code short, robust, and fast.

This guide names all three, explains when to use which, and shows the
minimum machinery for each. The canonical references are the four
example programs in `sdk/c/examples/` and `sdk/python/examples/`, plus
the `04_paint` rewrite discussed below.

## TL;DR — which case are you in?

| Case | Example | You control | The Atari runs |
|------|---------|-------------|----------------|
| A — **Raw pixel canvas** | 04_paint | Every byte of screen RAM + hardware regs | A parked CPU (nothing else) |
| B — **Boot a real .xex** | BOOT + observe | Nothing. The program owns the machine. | The full program you handed it |
| C — **Live editor** | Graph2Font integration | A runtime image you constructed + its data regions | Your runtime, hot-patched as the user edits |

If you're drawing pixels with a mouse and don't care about DLIs,
per-scanline effects, or sprites, you want **Case A**. If you have an
existing `.xex` and just need to run it and screenshot the output, you
want **Case B**. If you're building a Graph2Font-class graphics editor
that needs DLI-driven per-scanline colour changes, player/missile
graphics, raster programs — everything Altirra emulates — you want
**Case C**.

---

## Case A — raw pixel canvas

**Use when:** you want the Atari to display what you write, and only
what you write. No OS, no BASIC, no VBI, no interrupts. The client has
the only voice.

**Canonical example:** [`sdk/c/examples/04_paint.c`](../sdk/c/examples/04_paint.c)

### Why a plain "cold reset + poke screen RAM" doesn't work

On a cold reset the Atari kernel is running. Whether that's the real
Atari OS or [AltirraOS](https://www.virtualdub.org/altirraos.html) (the
GPL-clean stub the bridge server boots by default) the OS is constantly
re-asserting state through its VBI handlers — including, on the real
OS, the `SDLSTL`/`SDLSTH` shadow → ANTIC `DLISTL`/`DLISTH` copy. If you
poke a custom display list and leave the kernel running, the kernel
will fight you for control of ANTIC, GTIA, PIA and screen RAM in
various subtle and kernel-dependent ways.

AltirraOS specifically does **not** perform the `SDLSTL` shadow copy in
every code path, so the "poke the shadow and trust the VBI" pattern
that works on the real Atari OS silently fails on AltirraOS — you'll
end up with a screen that reverts to BASIC / the OS text mode, even
though your shadow writes succeeded.

### The fix: park the CPU first

The server ships a tiny 30-byte bare-metal stub as an embedded XEX.
Calling [`boot_bare()`](#the-boot_bare-primitive) loads it; once it has
run, the machine is in this state:

- CPU parked in an infinite `JMP *` loop (so no OS code runs)
- IRQs disabled (`SEI`) — no keyboard / serial / POKEY interrupts
- NMIs disabled (`STA $D40E #0`) — no VBI, no DLI, no reset NMI
- ANTIC DMA off (`STA $D400 #0`) — screen is black until you enable it
- BASIC cart unmapped (`STA $D301 #$FF`) — the full `$A000-$BFFF` RAM
  is available
- Decimal flag cleared (`CLD`)

From that point the client owns the machine. It can:

1. `memload` a display list anywhere in RAM
2. `memload` pixel / font / PMG data anywhere in RAM
3. `hwpoke` ANTIC's `DLISTL`/`DLISTH` (`$D402`/`$D403`) to point at the
   DL
4. `hwpoke` GTIA colour registers (`$D016`..`$D01A`) for playfield and
   background colours
5. `hwpoke` `DMACTL` (`$D400`) to wake ANTIC up (`$22` = playfield on,
   normal width, single-line resolution)
6. Paint in RAM as the user edits — **every subsequent `memload` to
   screen RAM shows up in the next Atari frame** with no kernel
   interference

> **Important:** ANTIC and GTIA register writes must use **`hwpoke`**
> (C: `atb_hwpoke`, Python: `bridge.hwpoke`), not plain `poke`. The
> regular `poke` writes the debug-safe RAM latch and has no effect on
> the actual chip registers in the `$D000-$D7FF` range. `hwpoke`
> routes the write through the CPU bus the same way a `STA $Dxxx`
> instruction would.

### The `boot_bare` primitive

Both SDKs expose `boot_bare()` as a one-call helper:

```python
# Python
bridge.boot_bare()          # default settle_frames=180
```

```c
/* C */
atb_boot_bare(client, /*settle_frames=*/180);
```

`settle_frames` is the number of frames advanced after the BOOT_BARE
command so the OS loader has time to hand off to the stub and the stub
has time to actually execute its `SEI`/`CLD`/`STA`/`JMP *`. 180 is a
safe default covering both NTSC and PAL OS boot times.

### Full skeleton (C)

```c
atb_boot_bare(c, 180);

unsigned char dl[128];     /* build your display list */
atb_memload(c, 0x1000, dl, dl_len);

unsigned char screen[3840] = {0};
atb_memload(c, 0x2000, screen, sizeof screen);

atb_hwpoke(c, 0xD402, 0x00);   /* DLISTL */
atb_hwpoke(c, 0xD403, 0x10);   /* DLISTH → DL at $1000 */
atb_hwpoke(c, 0xD016, 0x0F);   /* COLPF0 */
atb_hwpoke(c, 0xD017, 0x46);   /* COLPF1 */
atb_hwpoke(c, 0xD018, 0xC8);   /* COLPF2 */
atb_hwpoke(c, 0xD01A, 0x00);   /* COLBK  */
atb_hwpoke(c, 0xD400, 0x22);   /* DMACTL: enable playfield DMA */
```

Same thing in Python:

```python
bridge.boot_bare()
bridge.memload(0x1000, build_display_list())
bridge.memload(0x2000, bytes(3840))
bridge.hwpoke(0xD402, 0x00)
bridge.hwpoke(0xD403, 0x10)
bridge.hwpoke(0xD016, 0x0F)
bridge.hwpoke(0xD017, 0x46)
bridge.hwpoke(0xD018, 0xC8)
bridge.hwpoke(0xD01A, 0x00)
bridge.hwpoke(0xD400, 0x22)
```

After this, your paint loop is:

```python
while user_paints:
    # local changes into screen[] bytearray
    ...
    bridge.memload(0x2000, bytes(screen))
    bridge.frame(1)
    frame = bridge.rawscreen()
    blit_to_window(frame)
```

That's the whole thing. No VBI, no kernel, no fighting.

---

## Case B — boot a real .xex and observe it

**Use when:** you have a fully-formed Atari executable (a game, a demo,
a homebrew, Graph2Font's `export/picture.xex`) and you want to run it
and capture or inspect its output. The bridge is a lens on a running
program, not a graphics pipeline.

**Canonical example:** existing `atb_boot()` + `atb_frame()` +
`atb_screenshot_*` usage.

```python
bridge.boot("picture.xex")
bridge.frame(120)                      # let it load and settle
png = bridge.screenshot()
open("picture.png", "wb").write(png)
```

The program's own display list, DLIs, PMG setup, colour tables — all of
it — runs inside the real Altirra emulator, so every per-scanline
effect, every priority quirk, every cycle-accurate wrinkle is honoured
correctly. You can't get closer to running the software on real
hardware without actually owning a working 800XL.

You can also **live-edit the running program** from the bridge side:

```python
bridge.pause()
bridge.memload(0x2800, new_font_bytes)   # overwrite the font at $2800
bridge.resume()
bridge.frame(1)
```

The running program's next frame picks up the new font, no reboot
required. This is the fastest iteration loop for any kind of "tweak
the data next to a running program" workflow.

---

## Case C — live Atari graphics editor (Graph2Font class)

**Use when:** you're building a tool like Graph2Font, RastaConverter,
or any interactive editor where the user manipulates per-scanline
effects — DLI-driven colour-register changes, player/missile graphics,
raster programs — and needs to see the result refresh within one
monitor frame. The editor generates both the **code** (DLI handlers,
raster programs, main loop) and the **data** (display list, screen,
font, colour tables), and the Atari actually runs both.

**Canonical production integration:** Graph2Font's live preview,
replacing its internal homemade Atari renderer with calls to
AltirraBridge.

### Why neither Case A nor Case B is enough

- **Case A is too bare.** The CPU is parked; you can't run DLI
  handlers, you can't react to VCOUNT, you can't run a raster program.
  All per-scanline effects would have to be simulated client-side.
- **Case B is too coarse.** Rebooting the .xex on every brush stroke is
  slow (cold reset + OS boot + program load ≈ one second wall clock).
  The editor becomes laggy and loses state.

Case C is **Case B with hot-patching as its steady state**.

### The pattern

1. **Construct a runtime template once.** The editor generates a small
   .xex containing:
   - A main loop that infinite-JMPs or waits for VCOUNT
   - The DLI handlers the editor's model requires
   - A display list with DLI bits set on the rows that need per-scan
     changes
   - Initial font / screen / PMG / colour-table data

   This is the same .xex the editor would export today — so if the
   editor already has an exporter, the runtime template is free.
2. **Boot it once** via `bridge.boot(runtime_xex_path)`. Takes
   ≈100 ms including cold reset; happens once per editing session.
3. **On every edit, hot-patch just the changed bytes**. The user
   changes a pixel → `memload` the one affected byte. The user changes
   COLPF1 on scanline 40 → `memload` the one byte in the DLI colour
   table. The user drags a sprite → `memload` HPOSP0 in the PMG data
   table. Every hot-patch is typically 1-20 bytes.
4. **Step one frame, read back, display.** `frame(1)` + `rawscreen()`
   + blit into the editor's preview pane. With the bridge's current
   transport (sub-millisecond round-trips on loopback), this takes
   roughly one NTSC frame period (≈17 ms) end to end. One monitor
   refresh of perceived lag.
5. **Loop forever.** The Atari never reboots during the session; the
   program template keeps running, the data keeps mutating.

### The `.act` palette question

Graph2Font lets the user pick from Atari palette files (`.act`) and
expects the live preview to honour them. The bridge supports this
directly:

```python
with open("g2f.act", "rb") as f:
    err = bridge.load_act_palette(f.read())    # ~50-200 ms, once per palette change
print(f"solver matched the .act with RMS error {err:.2f}")
```

This uses the **same palette-fitting solver** Windows Altirra runs when
you load a .act in its Color Image Reference dialog. The server tunes
GTIA's analog-decoder parameters so every subsequent `rawscreen()`,
`screenshot()`, and `render_frame()` composites through a palette that
approximates the target .act as closely as the NTSC/PAL circuit model
allows. **Visual parity between Windows Altirra (with the same .act
loaded) and the bridge preview is guaranteed by construction** — both
paths invoke the same solver against the same initial parameters.

To undo and return to the factory palette:

```python
bridge.reset_palette()
```

`reset_palette` also fires automatically on `cold_reset`.

The 04_paint example ships with `g2f.act` (Graph2Font's default
palette) in `sdk/assets/g2f.act` and loads it at startup so you can
see the pattern in action.

### Skeleton (pseudo-code)

```python
from altirra_bridge import AltirraBridge

bridge = AltirraBridge.from_token_file(token_file)
bridge.boot("editor_runtime.xex")
bridge.frame(60)

# One-shot palette upload
with open("g2f.act", "rb") as f:
    bridge.load_act_palette(f.read())

# Edit loop
while True:
    event = wait_for_user_edit()
    dirty = editor_model.apply(event)       # returns list of (addr, bytes)
    for addr, data in dirty:
        bridge.memload(addr, data)          # hot-patch
    bridge.frame(1)
    frame = bridge.rawscreen()
    editor_preview.show(frame)
```

### Why this is fast enough

Per edit, the wire traffic is roughly:
- N small `memload`s (N typically 1-5, each a few to a few-tens of
  bytes plus base64 framing)
- One `frame(1)` — returns immediately; the subsequent read blocks on
  the frame gate (one NTSC frame = ~17 ms wall clock)
- One `rawscreen()` — one ~430 KB base64 payload in, one frame out
  (~5 ms at current client latency)

Total end-to-end: ~20-25 ms from click to preview refresh, dominated
by the hard floor of one emulator frame. That's 45-50 Hz refresh for
the editor UI, which is indistinguishable from "instant" in an
interactive editor context.

---

## Cross-reference

| You want to… | Case | Commands |
|---|---|---|
| Draw arbitrary pixels from an external tool | A | `boot_bare`, `memload`, `hwpoke`, `rawscreen` |
| Load a .act palette | A / B / C | `load_act_palette` |
| Run a .xex and screenshot it | B | `boot`, `frame`, `screenshot` / `rawscreen` |
| Hot-edit font / screen RAM of a running program | B / C | `pause`, `memload`, `resume`, `frame` |
| Build a live graphics editor with per-scanline effects | C | `boot`, `load_act_palette`, `memload`, `frame`, `rawscreen` |
| Reset GTIA's palette to factory | any | `reset_palette` |

See also:
- [`COMMANDS.md`](COMMANDS.md) — one-line summary of every verb
- [`PROTOCOL.md`](PROTOCOL.md) — full wire format
- [`GETTING_STARTED.md`](GETTING_STARTED.md) — five-minute walkthrough
- [`sdk/c/examples/04_paint.c`](../sdk/c/examples/04_paint.c) — Case A
  reference implementation
- [`sdk/python/examples/04_paint.py`](../sdk/python/examples/04_paint.py)
  — Case A in Python
- [`sdk/assets/bare_metal.s`](../sdk/assets/bare_metal.s) — annotated
  source of the 6502 stub `boot_bare` loads
- [`sdk/assets/g2f.act`](../sdk/assets/g2f.act) — Graph2Font's default
  palette, used by 04_paint to demonstrate `load_act_palette`
