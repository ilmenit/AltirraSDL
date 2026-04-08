#!/usr/bin/env python3
"""04_paint.py — Tiny paint program targeting Atari ANTIC mode D.

Demonstrates the AltirraBridge SDK driving the emulator as a live
display device. The user paints with the mouse, the example writes
packed pixels into Atari screen RAM, and the actual Atari frame is
captured back from the running emulator via SCREENSHOT and shown
side-by-side with the drawing canvas.

What this example shows
-----------------------

1. Building a custom ANTIC display list in Python and pushing it
   into Atari RAM via ``bridge.memload``.
2. Pointing the OS display-list shadow registers ($0230/$0231) at
   our DL so the OS VBI re-asserts it every frame.
3. Setting color registers via the OS shadows ($02C4-$02C8).
4. Mode D pixel packing — 4 pixels per byte at 2 bpp, MSB-first
   inside the byte. The ``pack_pixel`` helper is the load-bearing
   piece for any client that wants to draw on Atari screen RAM.
5. Reading the live frame back via ``bridge.rawscreen()`` and
   displaying it in a Tk window using PPM (no PIL, no pygame, no
   image libraries — pure stdlib).

The example uses Tkinter (Python stdlib) instead of SDL3 because
Tkinter is universally available with no install step. The same
SDK calls work identically with any UI framework.

Usage
-----

In one terminal::

    AltirraBridgeServer --bridge

It will print something like::

    [Bridge] token-file: /tmp/altirra-bridge-12345.token

In another terminal::

    python3 04_paint.py /tmp/altirra-bridge-12345.token

Click+drag on the left canvas to paint. Pick a color from the
swatches in the middle. The right side shows the live Atari frame
the emulator is producing.
"""

import sys
import tkinter as tk

from altirra_bridge import AltirraBridge


# =====================================================================
# Atari layout
# =====================================================================

# Display list lives in low free RAM, screen memory just above it.
# Both addresses chosen so the screen (3840 bytes for mode D) does
# not cross a 4 KB boundary, which ANTIC playfield DMA cannot do.
DL_ADDR     = 0x1000
SCREEN_ADDR = 0x2000

# ANTIC mode D = "Graphics 7": 160 × 96 pixels, 4 colors, 2 bpp,
# each row is 40 bytes, each row is 2 scanlines tall.
W          = 160
H          = 96
ROW_BYTES  = 40
SCREEN_LEN = ROW_BYTES * H        # 3840 bytes

# OS shadow registers we touch.  The OS VBI copies these into the
# real ANTIC/GTIA registers every frame — using the shadows means
# our settings stick across the OS's own VBI fix-up code.
SDLSTL = 0x0230   # display list low byte  -> ANTIC DLISTL
SDLSTH = 0x0231   # display list high byte -> ANTIC DLISTH
COLOR0 = 0x02C4   # mode D pixel value 1   -> GTIA COLPF0
COLOR1 = 0x02C5   # mode D pixel value 2   -> GTIA COLPF1
COLOR2 = 0x02C6   # mode D pixel value 3   -> GTIA COLPF2
COLOR4 = 0x02C8   # mode D pixel value 0   -> GTIA COLBK (background)


def build_display_list() -> bytes:
    """Construct the 104-byte mode D display list.

    Layout:
        24 blank scanlines  (3 × $70)
        mode D + LMS load   (1 × $4D + 2 bytes screen address)
        95 more mode D rows (95 × $0D)
        JVB back to start   (1 × $41 + 2 bytes DL address)
    """
    dl = bytearray()
    # 3 × 8 = 24 blank scanlines: required at the top of every DL
    # so the first row falls inside the visible playfield area.
    dl += bytes([0x70, 0x70, 0x70])
    # Mode D ($0D) with LMS bit ($40) set: $4D, then screen lo/hi.
    dl += bytes([0x4D, SCREEN_ADDR & 0xFF, SCREEN_ADDR >> 8])
    # 95 more mode D rows; ANTIC auto-increments the playfield
    # pointer 40 bytes per row.
    dl += bytes([0x0D] * (H - 1))
    # JVB ($41): jump and wait for next vertical blank, looping
    # back to the start of the DL forever.
    dl += bytes([0x41, DL_ADDR & 0xFF, DL_ADDR >> 8])
    return bytes(dl)


# Atari color values (hue << 4 | luma).  These three plus the
# background black give a 4-colour mode D palette.
COLORS = [
    (0x0F, "white"),
    (0x46, "red"),
    (0xC8, "green"),
]
COLOR_BG = 0x00


def setup_screen(bridge: AltirraBridge) -> None:
    """One-time setup: install DL, clear screen, point OS at DL,
    program color registers."""
    # Pause so the OS doesn't VBI in the middle of our staging.
    bridge.pause()
    bridge.memload(DL_ADDR, build_display_list())
    bridge.memload(SCREEN_ADDR, bytes(SCREEN_LEN))   # all background
    bridge.poke(SDLSTL, DL_ADDR & 0xFF)
    bridge.poke(SDLSTH, DL_ADDR >> 8)
    bridge.poke(COLOR4, COLOR_BG)
    bridge.poke(COLOR0, COLORS[0][0])
    bridge.poke(COLOR1, COLORS[1][0])
    bridge.poke(COLOR2, COLORS[2][0])
    bridge.resume()


# =====================================================================
# Mode D pixel packing
# =====================================================================
#
# Each row is 40 bytes. Each byte holds 4 pixels at 2 bits per pixel,
# MSB-first:
#
#       byte = | px0 (b7-6) | px1 (b5-4) | px2 (b3-2) | px3 (b1-0) |
#
# pixel value:  0 = COLBK, 1 = COLPF0, 2 = COLPF1, 3 = COLPF2.
# (Yes, the playfield numbering is one off from the color register
# numbering — historical Atari oddity.)

def pack_pixel(buf: bytearray, x: int, y: int, color: int) -> None:
    """Set the pixel at (x, y) in ``buf`` to ``color`` (0..3)."""
    if not (0 <= x < W and 0 <= y < H):
        return
    if not (0 <= color <= 3):
        return
    offset = y * ROW_BYTES + (x >> 2)
    shift  = (3 - (x & 3)) * 2
    mask   = 0x03 << shift
    buf[offset] = (buf[offset] & ~mask) | (color << shift)


# =====================================================================
# Atari color → RGB (for the on-screen swatches)
# =====================================================================
#
# Crude approximation of NTSC Atari colour decoding. Good enough to
# label the buttons; the live Atari frame on the right uses the real
# Altirra palette.

def atari_to_rgb(value: int) -> tuple[int, int, int]:
    import math
    hue  = (value >> 4) & 0x0F
    luma = value & 0x0F
    if hue == 0:
        v = luma * 17
        return v, v, v
    angle = math.radians((hue - 1) * (360.0 / 15.0))
    base  = luma * 17
    r = int(base + 60 * math.cos(angle))
    g = int(base + 60 * math.cos(angle - 2.0944))
    b = int(base + 60 * math.cos(angle + 2.0944))
    clamp = lambda v: max(0, min(255, v))
    return clamp(r), clamp(g), clamp(b)


# =====================================================================
# RawFrame (XRGB8888 LE) → PPM P6 bytes for Tk PhotoImage
# =====================================================================
#
# AltirraBridge ``rawscreen()`` returns a ``RawFrame`` whose
# ``pixels`` are XRGB8888 little-endian — i.e. the byte sequence per
# pixel is (B, G, R, X). PPM P6 wants raw R, G, B triples after a
# small ASCII header. We do the channel reorder by slicing every 4th
# byte (CPython's bytes/bytearray slicing is fast) instead of a
# Python-level pixel loop.

def rawframe_to_ppm(raw) -> bytes:
    px = raw.pixels
    n  = raw.width * raw.height
    rgb = bytearray(n * 3)
    rgb[0::3] = px[2::4]   # R
    rgb[1::3] = px[1::4]   # G
    rgb[2::3] = px[0::4]   # B
    header = f"P6\n{raw.width} {raw.height}\n255\n".encode("ascii")
    return header + bytes(rgb)


# =====================================================================
# Tkinter UI
# =====================================================================

class PaintApp:
    SCALE = 4   # drawing canvas scale factor (160 × 4 = 640 px wide)

    def __init__(self, root: tk.Tk, bridge: AltirraBridge) -> None:
        self.bridge = bridge
        self.shadow = bytearray(SCREEN_LEN)
        self.color  = 1   # currently selected paint colour (1..3)
        self.dirty  = True

        # --- Drawing canvas (left) -----------------------------------
        self.canvas = tk.Canvas(
            root,
            width=W * self.SCALE,
            height=H * self.SCALE,
            bg="black",
            highlightthickness=0,
        )
        self.canvas.grid(row=0, column=0, padx=8, pady=8)
        self.canvas.bind("<Button-1>",  self.on_paint)
        self.canvas.bind("<B1-Motion>", self.on_paint)

        # --- Color palette (middle) ----------------------------------
        pal = tk.Frame(root)
        pal.grid(row=0, column=1, padx=8, sticky="n")
        tk.Label(pal, text="paint:").pack(pady=(0, 4))
        for idx, (val, name) in enumerate(COLORS, start=1):
            r, g, b = atari_to_rgb(val)
            fg = "white" if (r + g + b) < 384 else "black"
            btn = tk.Button(
                pal,
                text=f"{idx}\n{name}",
                width=6, height=2,
                bg=f"#{r:02x}{g:02x}{b:02x}",
                fg=fg,
                relief="raised",
                command=lambda c=idx: self.set_color(c),
            )
            btn.pack(pady=2)
        tk.Button(pal, text="clear",
                  command=self.clear).pack(pady=(12, 0))

        # --- Live Atari frame preview (right) ------------------------
        right = tk.Frame(root)
        right.grid(row=0, column=2, padx=8, pady=8, sticky="n")
        tk.Label(right, text="Live Atari frame").pack()
        self.preview = tk.Label(right, bd=1, relief="sunken")
        self.preview.pack()

        # --- Status bar ---------------------------------------------
        self.status = tk.Label(root, text="", anchor="w")
        self.status.grid(row=1, column=0, columnspan=3,
                         sticky="ew", padx=8)

        # Draw a small test pattern so the user immediately sees
        # both the local canvas and the Atari frame agree.
        for i in range(W):
            pack_pixel(self.shadow, i, 0,         (i // 40) + 1)
            pack_pixel(self.shadow, i, H - 1,     ((i // 40) + 1))
        for j in range(H):
            pack_pixel(self.shadow, 0,     j, ((j // 24) % 3) + 1)
            pack_pixel(self.shadow, W - 1, j, ((j // 24) % 3) + 1)
        self._redraw_canvas()

        self._schedule_refresh()

    # ---- canvas helpers ----------------------------------------------

    def _color_hex(self, color: int) -> str:
        if color == 0:
            return "#000000"
        r, g, b = atari_to_rgb(COLORS[color - 1][0])
        return f"#{r:02x}{g:02x}{b:02x}"

    def _redraw_canvas(self) -> None:
        """Re-render every shadow pixel onto the Tk canvas. Used
        once at startup and after Clear; not in the hot path."""
        self.canvas.delete("all")
        s = self.SCALE
        for y in range(H):
            for x in range(W):
                offset = y * ROW_BYTES + (x >> 2)
                shift  = (3 - (x & 3)) * 2
                c = (self.shadow[offset] >> shift) & 0x03
                if c == 0:
                    continue
                self.canvas.create_rectangle(
                    x * s, y * s, x * s + s, y * s + s,
                    fill=self._color_hex(c), outline="")

    # ---- event handlers ----------------------------------------------

    def set_color(self, color: int) -> None:
        self.color = color
        self.status.config(text=f"selected color {color}")

    def clear(self) -> None:
        for i in range(SCREEN_LEN):
            self.shadow[i] = 0
        self.canvas.delete("all")
        self.dirty = True

    def on_paint(self, event: tk.Event) -> None:
        x = event.x // self.SCALE
        y = event.y // self.SCALE
        if not (0 <= x < W and 0 <= y < H):
            return
        pack_pixel(self.shadow, x, y, self.color)
        s = self.SCALE
        self.canvas.create_rectangle(
            x * s, y * s, x * s + s, y * s + s,
            fill=self._color_hex(self.color), outline="")
        self.dirty = True

    # ---- bridge refresh loop -----------------------------------------

    def _schedule_refresh(self) -> None:
        self.canvas.after(80, self.refresh)

    def refresh(self) -> None:
        try:
            if self.dirty:
                # Push the entire shadow buffer in one round trip.
                # Pure-Python per-pixel POKE would be ~3000× slower.
                self.bridge.memload(SCREEN_ADDR, bytes(self.shadow))
                self.dirty = False

            # Advance one frame so ANTIC has fresh data to render.
            self.bridge.frame(1)

            # Pull the rendered frame back as raw XRGB8888 and feed
            # it through Tk via PPM (binary, no PIL needed).
            raw = self.bridge.rawscreen()
            ppm = rawframe_to_ppm(raw)
            self.tkimg = tk.PhotoImage(data=ppm, format="PPM")
            self.preview.configure(image=self.tkimg)

            self.status.config(
                text=f"color {self.color}   "
                     f"shadow {SCREEN_LEN}B   "
                     f"Atari frame {raw.width}×{raw.height}")
        except Exception as e:
            self.status.config(text=f"refresh error: {e}")
        self._schedule_refresh()


# =====================================================================
# Entry point
# =====================================================================

def main() -> None:
    if len(sys.argv) != 2:
        print("usage: 04_paint.py <token-file>", file=sys.stderr)
        print("       (token file path is printed by AltirraBridgeServer "
              "or AltirraSDL --bridge on startup)", file=sys.stderr)
        sys.exit(2)

    bridge = AltirraBridge.from_token_file(sys.argv[1])
    setup_screen(bridge)

    root = tk.Tk()
    root.title("AltirraBridge paint demo — ANTIC mode D")
    root.resizable(False, False)
    PaintApp(root, bridge)
    try:
        root.mainloop()
    finally:
        bridge.close()


if __name__ == "__main__":
    main()
