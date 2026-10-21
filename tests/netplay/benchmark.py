#!/usr/bin/env python3
"""Reproduce seeded network simulations against a historical lockstep core.

Run from any directory: python3 tests/netplay/benchmark.py
Requires Git and a C++17 compiler (CXX, g++, or clang++). The simulation uses
small synthetic state; it measures scheduling/input latency, not emulator
checkpoint or rendering cost. No sockets or public services are contacted.
"""
from __future__ import annotations

import argparse
import csv
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "src/AltirraSDL/source/netplay"
HEADER = ["mode", "fps", "rtt_ms", "host_frames", "joiner_frames",
          "stall_peer_ms", "replayed_frames", "divergence_frame_plus_one",
          "local_input_to_first_apply_ms", "fixture_cpu_us"]


def run(command: list[str]) -> str:
    return subprocess.check_output(command, cwd=ROOT, text=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", default="0a011be1")
    parser.add_argument("--output", type=Path,
                        default=ROOT / "build/_scratch/netplay-benchmark")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    compiler = shlex.split(os.environ.get("CXX", ""))
    if not compiler:
        executable = shutil.which("g++") or shutil.which("clang++")
        if not executable:
            parser.error("Set CXX to a C++17 compiler.")
        compiler = [executable]
    baseline = output / "baseline"
    baseline.mkdir(exist_ok=True)
    for filename in ("lockstep.cpp", "lockstep.h", "packets.h"):
        text = run(["git", "show", f"{args.baseline}:"
                    f"src/AltirraSDL/source/netplay/{filename}"])
        (baseline / filename).write_text(text)
    shim = output / "shim"
    shim.mkdir(exist_ok=True)
    (shim / "stdafx.h").write_text(
        "#pragma once\n#include <cstdint>\n#include <cstring>\n")
    rows = []
    for label, directory, definitions in (
        ("baseline", baseline, ["-DOLD"]),
        ("current", SOURCE, []),
    ):
        executable = output / label
        # The baseline sources live in a separate directory from the executable.
        if label == "baseline":
            executable = output / "baseline-run"
        subprocess.run(compiler + ["-std=c++17", "-O2", *definitions,
            f"-I{directory}", f"-I{shim}",
            str(SOURCE / "lockstep_benchmark.cpp"),
            str(directory / "lockstep.cpp"), "-o", str(executable)],
            cwd=ROOT, check=True)
        result = run([str(executable)])
        parsed = list(csv.reader(result.splitlines()))
        rows.extend(parsed)
        with (output / f"{label}.csv").open("w", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(HEADER)
            writer.writerows(parsed)
    writer = csv.writer(sys.stdout)
    writer.writerow(HEADER)
    writer.writerows(rows)
    # Historical failures are expected and remain visible in the report.
    return int(any(row[0] != "old-delay4" and int(row[7]) for row in rows))


if __name__ == "__main__":
    raise SystemExit(main())
