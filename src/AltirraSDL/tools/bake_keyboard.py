#!/usr/bin/env python3
# Bake keyboard BMP + key region JSON into C++ data arrays.
#
# Reads:
#   keyboard.bmp       — Atari XL/XE keyboard image (4-bit indexed, ~229 KB)
#   rescaledkeys.json  — key hit regions with labels
#
# Emits:
#   <outdir>/keyboard_data.h    — declarations
#   <outdir>/keyboard_data.cpp  — BMP byte array + key definition table
#
# Uses Python stdlib only (json, struct, os, sys, math).

import json
import math
import os
import struct
import sys


# Flag constants matching the C++ header
FLAG_NONE    = 0
FLAG_TOGGLE  = 1   # Shift / Control (sticky toggle)
FLAG_CONSOLE = 2   # START / SELECT / OPTION (GTIA console switch)
FLAG_BREAK   = 4   # BREAK key (POKEY PushBreak)
FLAG_RESET   = 8   # RESET key (WarmReset)

# Label → (scancode, flags)
# Scancodes from uionscreenkeyboard.cpp lines 39-107
LABEL_TO_SCANCODE = {
    "HELP":    (0x11, FLAG_NONE),
    "START":   (0x48, FLAG_CONSOLE),
    "SELECT":  (0x49, FLAG_CONSOLE),
    "OPTION":  (0x4A, FLAG_CONSOLE),
    "RESET":   (0x4B, FLAG_RESET),
    "ESC":     (0x1C, FLAG_NONE),
    "1":       (0x1F, FLAG_NONE),
    "2":       (0x1E, FLAG_NONE),
    "3":       (0x1A, FLAG_NONE),
    "4":       (0x18, FLAG_NONE),
    "5":       (0x1D, FLAG_NONE),
    "6":       (0x1B, FLAG_NONE),
    "7":       (0x33, FLAG_NONE),
    "8":       (0x35, FLAG_NONE),
    "9":       (0x30, FLAG_NONE),
    "0":       (0x32, FLAG_NONE),
    "CLEAR":   (0x36, FLAG_NONE),
    "INSERT":  (0x37, FLAG_NONE),
    "DELETE":  (0x34, FLAG_NONE),
    "BREAK":   (0x40, FLAG_BREAK),
    "TAB":     (0x2C, FLAG_NONE),
    "Q":       (0x2F, FLAG_NONE),
    "W":       (0x2E, FLAG_NONE),
    "E":       (0x2A, FLAG_NONE),
    "R":       (0x28, FLAG_NONE),
    "T":       (0x2D, FLAG_NONE),
    "Y":       (0x2B, FLAG_NONE),
    "U":       (0x0B, FLAG_NONE),
    "I":       (0x0D, FLAG_NONE),
    "O":       (0x08, FLAG_NONE),
    "P":       (0x0A, FLAG_NONE),
    "UP":      (0x0E, FLAG_NONE),
    "DOWN":    (0x0F, FLAG_NONE),
    "RETURN":  (0x0C, FLAG_NONE),
    "CONTROL": (0x41, FLAG_TOGGLE),
    "A":       (0x3F, FLAG_NONE),
    "S":       (0x3E, FLAG_NONE),
    "D":       (0x3A, FLAG_NONE),
    "F":       (0x38, FLAG_NONE),
    "G":       (0x3D, FLAG_NONE),
    "H":       (0x39, FLAG_NONE),
    "J":       (0x01, FLAG_NONE),
    "K":       (0x05, FLAG_NONE),
    "L":       (0x00, FLAG_NONE),
    "COLON":   (0x02, FLAG_NONE),
    "LEFT":    (0x06, FLAG_NONE),
    "RIGHT":   (0x07, FLAG_NONE),
    "CAPS":    (0x3C, FLAG_NONE),
    "LSHIFT":  (0x42, FLAG_TOGGLE),
    "Z":       (0x17, FLAG_NONE),
    "X":       (0x16, FLAG_NONE),
    "C":       (0x12, FLAG_NONE),
    "V":       (0x10, FLAG_NONE),
    "B":       (0x15, FLAG_NONE),
    "N":       (0x23, FLAG_NONE),
    "M":       (0x25, FLAG_NONE),
    "COMMA":   (0x20, FLAG_NONE),
    "PERIOD":  (0x22, FLAG_NONE),
    "SLASH":   (0x26, FLAG_NONE),
    "RSHIFT":  (0x42, FLAG_TOGGLE),
    "INV":     (0x27, FLAG_NONE),
    "SPACE":   (0x21, FLAG_NONE),
}


def compute_navigation(keys):
    """Compute D-pad navigation indices for each key.

    For each key, find the nearest neighbor in each cardinal direction
    based on the center of the key rectangles.  Left/right prefer keys
    on the same or adjacent row.  Up/down prefer keys in the same
    column range.
    """
    n = len(keys)
    centers = []
    for k in keys:
        cx = k["u0"] + (k["u1"] - k["u0"]) * 0.5
        cy = k["v0"] + (k["v1"] - k["v0"]) * 0.5
        centers.append((cx, cy))

    nav = [[-1, -1, -1, -1] for _ in range(n)]  # left, right, up, down

    for i in range(n):
        cx, cy = centers[i]
        kw = keys[i]["u1"] - keys[i]["u0"]
        kh = keys[i]["v1"] - keys[i]["v0"]

        best = [None, None, None, None]  # (dist, index) for L, R, U, D
        for j in range(n):
            if i == j:
                continue
            jx, jy = centers[j]
            dx = jx - cx
            dy = jy - cy

            # Left: target must be to the left, prefer same row
            if dx < 0:
                row_penalty = abs(dy) / max(kh, 0.001)
                dist = abs(dx) + row_penalty * 0.5
                if best[0] is None or dist < best[0][0]:
                    best[0] = (dist, j)

            # Right: target must be to the right
            if dx > 0:
                row_penalty = abs(dy) / max(kh, 0.001)
                dist = abs(dx) + row_penalty * 0.5
                if best[1] is None or dist < best[1][0]:
                    best[1] = (dist, j)

            # Up: target must be above
            if dy < 0:
                col_penalty = abs(dx) / max(kw, 0.001)
                dist = abs(dy) + col_penalty * 0.3
                if best[2] is None or dist < best[2][0]:
                    best[2] = (dist, j)

            # Down: target must be below
            if dy > 0:
                col_penalty = abs(dx) / max(kw, 0.001)
                dist = abs(dy) + col_penalty * 0.3
                if best[3] is None or dist < best[3][0]:
                    best[3] = (dist, j)

        for d in range(4):
            if best[d] is not None:
                nav[i][d] = best[d][1]

    return nav


def main(argv):
    if len(argv) < 4:
        sys.stderr.write(
            "usage: bake_keyboard.py <out_dir> <bmp_path> <json_path>\n")
        return 2

    out_dir = argv[1]
    bmp_path = argv[2]
    json_path = argv[3]

    os.makedirs(out_dir, exist_ok=True)

    # --- Read BMP as raw bytes ---
    with open(bmp_path, "rb") as f:
        bmp_data = f.read()

    if bmp_data[:2] != b"BM":
        raise ValueError(f"{bmp_path}: not a BMP file")

    # Extract dimensions from BMP header for reference
    bmp_w = struct.unpack_from("<i", bmp_data, 18)[0]
    bmp_h = abs(struct.unpack_from("<i", bmp_data, 22)[0])
    print(f"BMP: {bmp_w}x{bmp_h}, {len(bmp_data)} bytes")

    # --- Read JSON key regions ---
    with open(json_path, "r") as f:
        jdata = json.load(f)

    ref_w = float(jdata["image_metadata"]["width"])
    ref_h = float(jdata["image_metadata"]["height"])

    keys = []
    for k in jdata["keys"]:
        label = k["label"]
        if label not in LABEL_TO_SCANCODE:
            raise ValueError(f"Unknown key label: {label}")
        scancode, flags = LABEL_TO_SCANCODE[label]

        # Normalize to [0,1] UV using the JSON reference dimensions
        u0 = k["x"] / ref_w
        v0 = k["y"] / ref_h
        u1 = (k["x"] + k["w"]) / ref_w
        v1 = (k["y"] + k["h"]) / ref_h

        # Clamp to [0,1]
        u0 = max(0.0, min(1.0, u0))
        v0 = max(0.0, min(1.0, v0))
        u1 = max(0.0, min(1.0, u1))
        v1 = max(0.0, min(1.0, v1))

        keys.append({
            "label": label,
            "scanCode": scancode,
            "flags": flags,
            "u0": u0, "v0": v0, "u1": u1, "v1": v1,
        })

    print(f"Keys: {len(keys)}")

    # Compute navigation
    nav = compute_navigation(keys)

    # --- Write header ---
    header_path = os.path.join(out_dir, "keyboard_data.h")
    hlines = [
        "// Auto-generated by tools/bake_keyboard.py — do not edit.",
        "#pragma once",
        "#include <stdint.h>",
        "#include <stddef.h>",
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
        "enum {",
        "    kOSKFlag_None    = 0,",
        "    kOSKFlag_Toggle  = 1,",
        "    kOSKFlag_Console = 2,",
        "    kOSKFlag_Break   = 4,",
        "    kOSKFlag_Reset   = 8,",
        "};",
        "",
        "struct ATOSKKeyDef {",
        "    float u0, v0, u1, v1;",
        "    uint8_t scanCode;",
        "    uint8_t flags;",
        "    int8_t nav[4];  // left, right, up, down",
        "    const char *label;",
        "};",
        "",
        "extern const uint8_t kKeyboardBMPData[];",
        "extern const size_t kKeyboardBMPSize;",
        "extern const struct ATOSKKeyDef kOSKKeys[];",
        "extern const int kOSKKeyCount;",
        "",
        "#ifdef __cplusplus",
        "}",
        "#endif",
        "",
    ]

    tmp = header_path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write("\n".join(hlines))
    os.replace(tmp, header_path)

    # --- Write source ---
    source_path = os.path.join(out_dir, "keyboard_data.cpp")
    slines = [
        "// Auto-generated by tools/bake_keyboard.py — do not edit.",
        '#include "keyboard_data.h"',
        "",
    ]

    # BMP data array
    slines.append(f"const uint8_t kKeyboardBMPData[{len(bmp_data)}] = {{")
    chunk = []
    for i, b in enumerate(bmp_data):
        chunk.append(f"0x{b:02x},")
        if (i + 1) % 16 == 0:
            slines.append("    " + " ".join(chunk))
            chunk = []
    if chunk:
        slines.append("    " + " ".join(chunk))
    slines.append("};")
    slines.append(f"const size_t kKeyboardBMPSize = {len(bmp_data)};")
    slines.append("")

    # Key definitions
    slines.append(f"const struct ATOSKKeyDef kOSKKeys[{len(keys)}] = {{")
    for i, k in enumerate(keys):
        n = nav[i]
        slines.append(
            f"    {{ {k['u0']:.6f}f, {k['v0']:.6f}f, "
            f"{k['u1']:.6f}f, {k['v1']:.6f}f, "
            f"0x{k['scanCode']:02x}, {k['flags']}, "
            f"{{ {n[0]}, {n[1]}, {n[2]}, {n[3]} }}, "
            f"\"{k['label']}\" }},"
        )
    slines.append("};")
    slines.append(f"const int kOSKKeyCount = {len(keys)};")
    slines.append("")

    tmp = source_path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write("\n".join(slines))
    os.replace(tmp, source_path)

    print(f"Wrote {header_path}")
    print(f"Wrote {source_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
