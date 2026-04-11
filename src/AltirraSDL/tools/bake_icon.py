#!/usr/bin/env python3
# Bake window/taskbar icon data into a C header + Windows .ico + macOS .icns.
#
# Reads the Android launcher PNGs (the only icon art in the tree) and
# emits:
#
#   <outdir>/altirra_icon_data.h   — decoded RGBA8 byte arrays + sizes,
#                                    consumed by main_sdl3.cpp to build
#                                    SDL_Surface icons at runtime.
#   <outdir>/altirra.ico           — PNG-embedded .ico (Vista+), wired
#                                    into AltirraSDL.rc on Windows for
#                                    the exe/taskbar icon.
#   <outdir>/altirra.icns          — Apple IconSuite container, copied
#                                    into AltirraSDL.app/Contents/Resources
#                                    on macOS so Finder, Launchpad and the
#                                    Dock-before-launch all show the
#                                    Altirra icon instead of the generic
#                                    app placeholder.
#
# Uses Python stdlib only (zlib, struct, binascii) so it runs on every
# GitHub-hosted runner (Linux/macOS/Windows) without pip installs.
# Supports exactly the PNG flavour the Android assets use: 8-bit RGBA,
# non-interlaced, color type 6. That's all we need — no generality.

import binascii
import math
import os
import struct
import sys
import zlib


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def decode_png_rgba8(path):
    """Return (width, height, rgba_bytes) for an 8-bit RGBA non-interlaced PNG."""
    with open(path, "rb") as f:
        data = f.read()

    if data[:8] != PNG_SIGNATURE:
        raise ValueError(f"{path}: not a PNG file")

    pos = 8
    width = height = 0
    bit_depth = color_type = interlace = -1
    idat = bytearray()

    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        ctype = data[pos + 4:pos + 8]
        cdata = data[pos + 8:pos + 8 + length]
        pos += 8 + length + 4  # skip CRC

        if ctype == b"IHDR":
            (width, height, bit_depth, color_type,
             _comp, _filt, interlace) = struct.unpack(">IIBBBBB", cdata)
        elif ctype == b"IDAT":
            idat += cdata
        elif ctype == b"IEND":
            break

    if bit_depth != 8 or color_type != 6 or interlace != 0:
        raise ValueError(
            f"{path}: expected 8-bit RGBA non-interlaced PNG "
            f"(got depth={bit_depth}, color_type={color_type}, "
            f"interlace={interlace})")

    raw = zlib.decompress(bytes(idat))
    stride = width * 4
    out = bytearray(stride * height)
    prev = bytes(stride)
    ri = 0  # read index in `raw`
    wi = 0  # write index in `out`

    for _y in range(height):
        filt = raw[ri]
        ri += 1
        row = bytearray(raw[ri:ri + stride])
        ri += stride

        if filt == 0:  # None
            pass
        elif filt == 1:  # Sub
            for x in range(4, stride):
                row[x] = (row[x] + row[x - 4]) & 0xFF
        elif filt == 2:  # Up
            for x in range(stride):
                row[x] = (row[x] + prev[x]) & 0xFF
        elif filt == 3:  # Average
            for x in range(stride):
                left = row[x - 4] if x >= 4 else 0
                row[x] = (row[x] + ((left + prev[x]) >> 1)) & 0xFF
        elif filt == 4:  # Paeth
            for x in range(stride):
                a = row[x - 4] if x >= 4 else 0
                b = prev[x]
                c = prev[x - 4] if x >= 4 else 0
                p = a + b - c
                pa = abs(p - a)
                pb = abs(p - b)
                pc = abs(p - c)
                if pa <= pb and pa <= pc:
                    pr = a
                elif pb <= pc:
                    pr = b
                else:
                    pr = c
                row[x] = (row[x] + pr) & 0xFF
        else:
            raise ValueError(f"{path}: unknown PNG filter {filt}")

        out[wi:wi + stride] = row
        wi += stride
        prev = bytes(row)

    return width, height, bytes(out)


def encode_png_rgba8(width, height, rgba):
    """Return bytes of a minimal 8-bit RGBA non-interlaced PNG.

    One IDAT chunk, filter None for every scanline. zlib at default
    level is plenty for these icon images — the file lives inside an
    .icns that is not further compressed by packagers, so spending a
    few extra kB per slot is irrelevant.
    """
    if len(rgba) != width * height * 4:
        raise ValueError(
            f"encode_png_rgba8: expected {width*height*4} bytes, got "
            f"{len(rgba)}")

    def chunk(tag, data):
        crc = binascii.crc32(tag + data) & 0xFFFFFFFF
        return (struct.pack(">I", len(data))
                + tag + data + struct.pack(">I", crc))

    # IHDR: depth=8, color=6 (RGBA), comp=0, filt=0, interlace=0
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)

    stride = width * 4
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter None
        raw.extend(rgba[y * stride:(y + 1) * stride])
    idat = zlib.compress(bytes(raw), 9)

    return (PNG_SIGNATURE
            + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", idat)
            + chunk(b"IEND", b""))


def _downscale_area(src_w, src_h, src, dst_w, dst_h):
    """Area-weighted (box-filter) downscale of 8-bit RGBA pixels.

    Each destination pixel is the coverage-weighted average of every
    source pixel that intersects its footprint in source coordinates.
    This is equivalent to OpenCV's INTER_AREA and produces noticeably
    cleaner results than bilinear for large reduction factors (the
    192 → 16 case is 12:1).
    """
    dst = bytearray(dst_w * dst_h * 4)
    x_ratio = src_w / dst_w
    y_ratio = src_h / dst_h

    for dy in range(dst_h):
        y0 = dy * y_ratio
        y1 = y0 + y_ratio
        iy0 = int(math.floor(y0))
        iy1 = int(math.ceil(y1))
        for dx in range(dst_w):
            x0 = dx * x_ratio
            x1 = x0 + x_ratio
            ix0 = int(math.floor(x0))
            ix1 = int(math.ceil(x1))

            r = g = b = a = 0.0
            wsum = 0.0
            for sy in range(iy0, iy1):
                fy_lo = sy if sy > y0 else y0
                fy_hi = (sy + 1) if (sy + 1) < y1 else y1
                hy = fy_hi - fy_lo
                if hy <= 0.0:
                    continue
                row = sy * src_w
                for sx in range(ix0, ix1):
                    fx_lo = sx if sx > x0 else x0
                    fx_hi = (sx + 1) if (sx + 1) < x1 else x1
                    hx = fx_hi - fx_lo
                    if hx <= 0.0:
                        continue
                    w = hx * hy
                    o = (row + sx) * 4
                    r += src[o] * w
                    g += src[o + 1] * w
                    b += src[o + 2] * w
                    a += src[o + 3] * w
                    wsum += w

            do = (dy * dst_w + dx) * 4
            inv = 1.0 / wsum
            dst[do]     = int(r * inv + 0.5)
            dst[do + 1] = int(g * inv + 0.5)
            dst[do + 2] = int(b * inv + 0.5)
            dst[do + 3] = int(a * inv + 0.5)
    return bytes(dst)


def _upscale_bilinear(src_w, src_h, src, dst_w, dst_h):
    """Bilinear upscale of 8-bit RGBA pixels.

    Center-aligned sampling: output pixel (dx, dy) samples input at
    ((dx + 0.5) * src_w / dst_w - 0.5, ...) so corners stay anchored
    and the image does not shift by half a pixel.
    """
    dst = bytearray(dst_w * dst_h * 4)
    sx_scale = src_w / dst_w
    sy_scale = src_h / dst_h
    max_x = src_w - 1
    max_y = src_h - 1

    for dy in range(dst_h):
        sy = (dy + 0.5) * sy_scale - 0.5
        y0 = int(math.floor(sy))
        fy = sy - y0
        if y0 < 0:
            y0 = 0; fy = 0.0
        elif y0 >= max_y:
            y0 = max_y; fy = 0.0
        y1 = y0 + 1 if y0 < max_y else y0

        for dx in range(dst_w):
            sx = (dx + 0.5) * sx_scale - 0.5
            x0 = int(math.floor(sx))
            fx = sx - x0
            if x0 < 0:
                x0 = 0; fx = 0.0
            elif x0 >= max_x:
                x0 = max_x; fx = 0.0
            x1 = x0 + 1 if x0 < max_x else x0

            w00 = (1.0 - fx) * (1.0 - fy)
            w10 = fx * (1.0 - fy)
            w01 = (1.0 - fx) * fy
            w11 = fx * fy

            o00 = (y0 * src_w + x0) * 4
            o10 = (y0 * src_w + x1) * 4
            o01 = (y1 * src_w + x0) * 4
            o11 = (y1 * src_w + x1) * 4
            do = (dy * dst_w + dx) * 4
            for c in range(4):
                v = (src[o00 + c] * w00 + src[o10 + c] * w10
                     + src[o01 + c] * w01 + src[o11 + c] * w11)
                iv = int(v + 0.5)
                if iv < 0:
                    iv = 0
                elif iv > 255:
                    iv = 255
                dst[do + c] = iv
    return bytes(dst)


def resize_rgba(src_w, src_h, src, dst_w, dst_h):
    """Resize 8-bit RGBA pixels to (dst_w, dst_h).

    Picks area-average for downscale and bilinear for upscale.  Returns
    the original buffer unchanged when no resize is needed.
    """
    if dst_w == src_w and dst_h == src_h:
        return src
    if dst_w <= src_w and dst_h <= src_h:
        return _downscale_area(src_w, src_h, src, dst_w, dst_h)
    return _upscale_bilinear(src_w, src_h, src, dst_w, dst_h)


def write_icns(out_path, src_w, src_h, src_rgba):
    """Write an Apple IconSuite (.icns) built from a single RGBA source.

    The .icns container is: 'icns' magic, 4-byte big-endian total file
    size, then a sequence of (4-byte OSType, 4-byte big-endian chunk
    size-including-header, payload) entries.  For OS X 10.7+ every
    slot's payload is just a PNG file whose pixel dimensions exactly
    match the slot size — macOS IconServices validates this.
    """
    # (OSType, size) — slot table.  We emit only slots whose target
    # size is <= our highest-quality source (192x192) so we never
    # blur-upscale past a modest 1.33x (for the 256-wide slots).
    # Covers every Finder/Dock/Launchpad display path up to and
    # including 128@2x Retina.  @2x Retina variants (ic11/ic12/ic13)
    # are the same bytes as their non-Retina counterparts of the same
    # pixel size, which is the standard .icns convention.
    slots = [
        (b"icp4",  16),  # 16x16
        (b"icp5",  32),  # 32x32
        (b"ic11",  32),  # 16x16 @2x
        (b"icp6",  64),  # 64x64
        (b"ic12",  64),  # 32x32 @2x
        (b"ic07", 128),  # 128x128
        (b"ic08", 256),  # 256x256
        (b"ic13", 256),  # 128x128 @2x
    ]

    # Cache PNG bytes per unique pixel size — ic11 reuses icp5, ic12
    # reuses icp6, ic13 reuses ic08.
    png_cache = {}
    entries = []
    for tag, size in slots:
        if size not in png_cache:
            rgba = resize_rgba(src_w, src_h, src_rgba, size, size)
            png_cache[size] = encode_png_rgba8(size, size, rgba)
        entries.append((tag, png_cache[size]))

    body = bytearray()
    for tag, png in entries:
        body += tag + struct.pack(">I", 8 + len(png)) + png

    total = 8 + len(body)
    header = b"icns" + struct.pack(">I", total)

    tmp = out_path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(header + bytes(body))
    os.replace(tmp, out_path)


def write_header_and_source(header_path, source_path, entries):
    """Emit a declarations header + a definitions .c file.

    Split is deliberate: the RGBA arrays are ~1 MB total. Keeping them
    in a dedicated .c translation unit means only that one TU recompiles
    on icon changes — main_sdl3.cpp just sees tiny extern declarations.

    entries = [(size, rgba_bytes), ...], sorted largest-first.
    """
    # --- header ---
    hlines = []
    hlines.append("// Auto-generated by tools/bake_icon.py — do not edit.")
    hlines.append("#pragma once")
    hlines.append("#include <stdint.h>")
    hlines.append("#include <stddef.h>")
    hlines.append("")
    hlines.append("#ifdef __cplusplus")
    hlines.append('extern "C" {')
    hlines.append("#endif")
    hlines.append("")
    hlines.append("struct ATBakedIcon {")
    hlines.append("    int size;              // width == height in pixels")
    hlines.append("    const uint8_t* rgba;   // size*size*4 bytes, row-major")
    hlines.append("};")
    hlines.append("")
    hlines.append("extern const struct ATBakedIcon kAltirraIcons[];")
    hlines.append("extern const size_t kAltirraIconCount;")
    hlines.append("")
    hlines.append("#ifdef __cplusplus")
    hlines.append("}")
    hlines.append("#endif")
    hlines.append("")

    tmp = header_path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write("\n".join(hlines))
    os.replace(tmp, header_path)

    # --- source ---
    slines = []
    slines.append("// Auto-generated by tools/bake_icon.py — do not edit.")
    slines.append('#include "altirra_icon_data.h"')
    slines.append("")

    for size, rgba in entries:
        sym = f"kAltirraIconRGBA_{size}"
        slines.append(f"static const uint8_t {sym}[{len(rgba)}] = {{")
        chunk = []
        for i, b in enumerate(rgba):
            chunk.append(f"0x{b:02x},")
            if (i + 1) % 16 == 0:
                slines.append("    " + " ".join(chunk))
                chunk = []
        if chunk:
            slines.append("    " + " ".join(chunk))
        slines.append("};")
        slines.append("")

    slines.append("const struct ATBakedIcon kAltirraIcons[] = {")
    for size, _ in entries:
        slines.append(f"    {{ {size}, kAltirraIconRGBA_{size} }},")
    slines.append("};")
    slines.append(f"const size_t kAltirraIconCount = {len(entries)};")
    slines.append("")

    tmp = source_path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write("\n".join(slines))
    os.replace(tmp, source_path)


def write_ico(out_path, png_paths_by_size):
    """Build a PNG-embedded .ico (Vista+).

    Entries whose pixel dimension >= 256 must encode their size byte as 0.
    We embed the original PNG bytes directly — no re-encoding needed.
    """
    sizes = sorted(png_paths_by_size.keys())
    count = len(sizes)

    header = struct.pack("<HHH", 0, 1, count)  # reserved, type=icon, count
    directory = b""
    images = b""
    offset = 6 + count * 16  # ICONDIR + N * ICONDIRENTRY

    for size in sizes:
        with open(png_paths_by_size[size], "rb") as f:
            png_bytes = f.read()
        size_byte = 0 if size >= 256 else size
        directory += struct.pack(
            "<BBBBHHII",
            size_byte,         # width
            size_byte,         # height
            0,                 # palette count (0 for truecolor)
            0,                 # reserved
            1,                 # color planes
            32,                # bits per pixel
            len(png_bytes),    # bytes in resource
            offset,            # offset from start of file
        )
        images += png_bytes
        offset += len(png_bytes)

    tmp = out_path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(header + directory + images)
    os.replace(tmp, out_path)


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(
            "usage: bake_icon.py <out_dir> <png> [<png> ...]\n")
        return 2

    out_dir = argv[1]
    png_paths = argv[2:]

    os.makedirs(out_dir, exist_ok=True)

    decoded = []       # (size, rgba_bytes)
    by_size_path = {}  # size -> source path (for .ico)
    for path in png_paths:
        w, h, rgba = decode_png_rgba8(path)
        if w != h:
            raise ValueError(f"{path}: icon must be square ({w}x{h})")
        decoded.append((w, rgba))
        by_size_path[w] = path

    decoded.sort(key=lambda e: -e[0])  # largest first — primary icon

    write_header_and_source(
        os.path.join(out_dir, "altirra_icon_data.h"),
        os.path.join(out_dir, "altirra_icon_data.cpp"),
        decoded,
    )
    write_ico(os.path.join(out_dir, "altirra.ico"), by_size_path)

    # .icns is resampled from the highest-resolution source we have.
    # Always written, even on non-Apple builds, so that (a) the
    # add_custom_command OUTPUT list is consistent across platforms
    # and (b) a developer on Linux can still ship a macOS .app bundle
    # via cross-compilation without a second build step.
    top_w, top_rgba = decoded[0]
    write_icns(os.path.join(out_dir, "altirra.icns"),
               top_w, top_w, top_rgba)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
