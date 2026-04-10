#!/usr/bin/env python3
"""
deep_analyze.py — reproducible analysis driver for river_raid.xex.

Usage:
    PYTHONPATH=../../../sdk/python python3 deep_analyze.py <token-file>

Designed to be re-run from a clean state to regenerate the
river_raid.pyA8/ project tree. Game-specific knowledge (labels,
region classifications, comments) is encoded inline below; everything
else delegates to the altirra_bridge package and the live bridge.

Phases mirror PROMPT.md. Each phase function is idempotent and
checkpoints to disk.
"""

from __future__ import annotations

import hashlib
import json
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
XEX = HERE / "river_raid.xex"
PROJ = HERE / "river_raid.pyA8"

EXPECTED_SHA256 = "3924596cf04e1c7f97af9688a125c8a7e0b0d686dbaa40820c812cc1c406e704"


# ----------------------------------------------------------------------
# XEX walker — local because altirra_bridge.loader.parse_xex doesn't
# tolerate the trailing $1A DOS-sector padding this file carries.
# ----------------------------------------------------------------------
def walk_xex(data: bytes):
    """Yield (start, end, body_bytes) for every well-formed segment.

    Stops at the first segment whose length would overrun the file —
    that's the boundary between real segments and disk padding.
    """
    i = 0
    while i + 4 <= len(data):
        # Optional inter-segment $FFFF marker (mandatory before seg0).
        if data[i] == 0xFF and data[i + 1] == 0xFF:
            i += 2
            if i + 4 > len(data):
                break
        start = data[i] | (data[i + 1] << 8)
        end = data[i + 2] | (data[i + 3] << 8)
        i += 4
        body_len = end - start + 1
        if body_len <= 0 or i + body_len > len(data):
            break
        yield start, end, data[i:i + body_len]
        i += body_len


# ----------------------------------------------------------------------
# Phase 1 — static structure
# ----------------------------------------------------------------------
def phase1_static():
    data = XEX.read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    assert sha == EXPECTED_SHA256, f"sha mismatch: {sha}"

    segments = []
    for start, end, body in walk_xex(data):
        segments.append({
            "start": start,
            "end": end,
            "size": len(body),
        })

    project = {
        "name": "river_raid",
        "source_file": "river_raid.xex",
        "sha256": sha,
        "file_size": len(data),
        "segments": segments,
    }
    PROJ.mkdir(exist_ok=True)
    (PROJ / "project.json").write_text(json.dumps(project, indent=2) + "\n")

    # Empty companion artifacts; populated in later phases.
    for name, default in [
        ("regions.json", {}),
        ("comments.json", {}),
        ("xrefs.json", {}),
        ("call_graph.json", {}),
    ]:
        path = PROJ / name
        if not path.exists():
            path.write_text(json.dumps(default, indent=2) + "\n")
    print(f"phase1: {len(segments)} segments, sha256 ok")
    return project


# ----------------------------------------------------------------------
# Bridge connection helper
# ----------------------------------------------------------------------
def connect(token_file: str):
    from altirra_bridge import AltirraBridge
    return AltirraBridge.from_token_file(token_file)


# ----------------------------------------------------------------------
# Phase 2 — boot + first snapshot
# ----------------------------------------------------------------------
def phase2_boot(token_file: str):
    with connect(token_file) as a:
        a.boot(str(XEX.resolve()))
        a.frame(300)
        a.pause()
        print("phase2: regs after 300 frames:", a.regs())
        snaps = PROJ / "snapshots"
        snaps.mkdir(exist_ok=True)
        (snaps / "title.png").write_bytes(a.screenshot())
        (snaps / "ram_title.bin").write_bytes(a.memdump(0x0000, 0xFFFF))
        print("phase2: snapshots saved")


# ----------------------------------------------------------------------
# Phase 4 — static recursive descent with iterative gap feedback
# ----------------------------------------------------------------------
def _regions_to_touched(regions, lo, hi):
    touched = set()
    for r in regions:
        s = r["start"]; e = r["end"]
        if isinstance(s, str): s = int(s, 16)
        if isinstance(e, str): e = int(e, 16)
        if e < lo or s > hi: continue
        for a in range(max(s, lo), min(e, hi) + 1):
            touched.add(a)
    return touched


def _find_gaps(touched, lo, hi):
    gaps = []
    in_gap = False
    gs = lo
    for a in range(lo, hi + 1):
        if a in touched:
            if in_gap:
                gaps.append((gs, a - 1))
                in_gap = False
        else:
            if not in_gap:
                gs = a
                in_gap = True
    if in_gap:
        gaps.append((gs, hi))
    return gaps


def phase4_recursive_descent(
    memory_image_path: Path,
    entry_points,
    code_range=(0xA000, 0xBFFF),
    max_iterations: int = 6,
):
    """Iteratively walk code reachable from entry_points, feed back
    gap_code / addr_table targets until byte coverage stabilises.

    Args:
        memory_image_path: path to a 64 KB RAM snapshot.
        entry_points: iterable of (addr, label) tuples.
        code_range: (lo, hi) inclusive code window for gap analysis.

    Returns:
        dict with regions, xrefs, addr_tables, procedures, stats.
    """
    from altirra_bridge.analyzer import (
        recursive_descent,
        classify_gap,
        scan_address_tables,
        scan_indirect_jumps,
        scan_vector_pointers,
        build_procedures,
    )

    ram = bytearray(memory_image_path.read_bytes())
    lo, hi = code_range
    roots = list(entry_points)

    regions = []
    xrefs = []
    iter_stats = []
    seen_roots = {addr for addr, _ in roots}

    for it in range(max_iterations):
        regions, xrefs = recursive_descent(ram, roots)
        touched = _regions_to_touched(regions, lo, hi)
        cov = len(touched)
        gaps = _find_gaps(touched, lo, hi)
        # classify each gap — add any gap_code entries as new roots
        added = 0
        gap_classified = []
        for gs, ge in gaps:
            c = classify_gap(ram, gs, ge, code_range=[(lo, hi)])
            gap_classified.append((gs, ge, c))
            if c.get("type") == "code" and "entry_addr" in c:
                ea = c["entry_addr"]
                if ea not in seen_roots:
                    roots.append((ea, f"gap_code_iter{it}"))
                    seen_roots.add(ea)
                    added += 1
        iter_stats.append({
            "iter": it, "code_bytes": cov, "coverage_pct": cov / (hi - lo + 1),
            "gaps": len(gaps), "new_roots": added,
        })
        print(f"  iter {it}: code={cov} ({cov/(hi-lo+1):.1%})  gaps={len(gaps)}  +{added} roots")
        if added == 0:
            break

    # Final scans
    addr_tables = scan_address_tables(ram, regions, [(lo, hi)])
    indirect_jumps = scan_indirect_jumps(ram, regions)
    vector_pointers = scan_vector_pointers(ram, [(lo, hi)])

    # Feed address-table targets back one more time
    for t in addr_tables:
        for tgt in t.get("targets", []):
            if lo <= tgt <= hi and tgt not in seen_roots:
                roots.append((tgt, "addr_table_target"))
                seen_roots.add(tgt)
    regions, xrefs = recursive_descent(ram, roots)
    touched = _regions_to_touched(regions, lo, hi)
    final_cov = len(touched)
    final_gaps = _find_gaps(touched, lo, hi)
    print(f"  final: code={final_cov} ({final_cov/(hi-lo+1):.1%})  gaps={len(final_gaps)}")

    procedures = build_procedures(ram, xrefs, regions)

    return {
        "regions": regions,
        "xrefs": xrefs,
        "addr_tables": addr_tables,
        "indirect_jumps": indirect_jumps,
        "vector_pointers": vector_pointers,
        "procedures": procedures,
        "iter_stats": iter_stats,
        "final_coverage_bytes": final_cov,
        "final_gaps": final_gaps,
        "roots_used": sorted(seen_roots),
    }


def phase4_run_and_save(memory_image_path: Path, entry_points):
    out = phase4_recursive_descent(memory_image_path, entry_points)

    # Serialise to project files
    regions_json = [
        {
            "start": f"${(r['start'] if isinstance(r['start'], int) else int(r['start'],16)):04X}",
            "end":   f"${(r['end']   if isinstance(r['end'],   int) else int(r['end'],  16)):04X}",
            "type":  r.get("type", "code"),
        }
        for r in out["regions"]
    ]
    (PROJ / "regions.json").write_text(json.dumps(regions_json, indent=1) + "\n")
    (PROJ / "xrefs.json").write_text(json.dumps(out["xrefs"][:5000], indent=1) + "\n")
    (PROJ / "snapshots" / "addr_tables.json").write_text(
        json.dumps(out["addr_tables"], indent=1) + "\n")
    (PROJ / "snapshots" / "indirect_jumps.json").write_text(
        json.dumps(out["indirect_jumps"], indent=1) + "\n")
    (PROJ / "snapshots" / "iter_stats.json").write_text(
        json.dumps(out["iter_stats"], indent=1) + "\n")

    # procedures may contain set/tuple types — normalise
    def norm(obj):
        if isinstance(obj, dict):
            return {k: norm(v) for k, v in obj.items()}
        if isinstance(obj, (list, tuple, set)):
            return [norm(v) for v in obj]
        return obj
    (PROJ / "snapshots" / "procedures.json").write_text(
        json.dumps(norm(out["procedures"]), indent=1, default=str) + "\n")
    print(f"phase4: wrote regions.json ({len(regions_json)} regions), "
          f"xrefs.json ({len(out['xrefs'])}), "
          f"procedures.json ({len(out['procedures']) if out['procedures'] else 0})")
    return out


# ----------------------------------------------------------------------
def main():
    phase1_static()
    if len(sys.argv) > 1 and sys.argv[1] not in ("--phase4",):
        phase2_boot(sys.argv[1])
    snap = PROJ / "snapshots" / "ram_title.bin"
    if snap.exists():
        # River raid's NMI/cold entry, NMI service branch, and IRQ stub.
        entry_points = [
            (0xA000, "cold_and_nmi"),
            (0xA04D, "nmi_service"),
            (0xBFF0, "irq_stub"),
        ]
        phase4_run_and_save(snap, entry_points)


if __name__ == "__main__":
    main()
