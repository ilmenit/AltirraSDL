#!/usr/bin/env python3
"""Compare production GPU images to a reference with Mesa's disk cache disabled.

Usage: compare_shader_cold_cache.py SAMPLING_TEST REFERENCE_SPV
Set VK_DRIVER_FILES to select hardware Vulkan or Mesa lavapipe explicitly.
Each child has a fresh process; no user's shader cache is read or removed.
"""
import argparse
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("reference", type=Path)
    parser.add_argument("--timeout", type=float, default=120)
    args = parser.parse_args()
    header = Path(__file__).resolve().parents[2] / (
        "src/AltirraSDL/source/display/shaders/generated/sdlgpu_screenfx_frag_spv.h")
    shader = bytes(int(h, 16) for h in re.findall(r"0x([0-9a-fA-F]{2})", header.read_text()))
    env = dict(os.environ, MESA_SHADER_CACHE_DISABLE="true")
    with tempfile.TemporaryDirectory(prefix="altirra-shader-") as directory:
        root = Path(directory)
        current = root / "current.spv"
        current.write_bytes(shader)
        outputs = []
        for name, binary in (("reference", args.reference.resolve()), ("current", current)):
            output = root / (name + ".rgba")
            start = time.monotonic()
            subprocess.run([str(args.executable.resolve()), str(binary), str(output)],
                           env=env, check=True, timeout=args.timeout)
            print(f"{name}: cold process + GPU readback {time.monotonic() - start:.3f}s", flush=True)
            raw = output.read_bytes()
            outputs.append(struct.unpack(f"={len(raw) // 4}f", raw))
        old, new = outputs
        if len(old) != len(new) or not old:
            raise RuntimeError("Image dump sizes differ or are empty")
        differences = [abs(a - b) for a, b in zip(old, new)]
        if any(not math.isfinite(x) for x in old + new) or max(differences) > 0.0001:
            raise RuntimeError(f"Shader output changed: max absolute difference {max(differences)}")
        print(f"Compared {len(old) // 4} RGBA pixels; max difference {max(differences):.9g}")


if __name__ == "__main__":
    main()
