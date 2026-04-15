"""Path-to-module classification rules for the AltirraSDL sync tooling.

Given a relative path such as ``src/Altirra/source/gtia.cpp``, return a
``Classification`` describing:

* ``module``  — which Altirra module the file belongs to.
* ``role``    — its role in the SDL fork's sync policy.
* ``priority``— integer sort key (lower = resolve earlier).

The rules here mirror the module table in
``syncing-with-upstream/GUIDE.md``.  Whenever upstream introduces a new
top-level folder or a distinctive filename convention, extend the
patterns below.  The classifier is intentionally *not* heuristic: every
path must match one rule, and unknown paths are flagged as ``unknown``
so the operator can extend the map.

This file is imported from ``classify_changes.py`` and ``apply_trivial.py``.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Iterable, Sequence


@dataclass(frozen=True)
class Classification:
    module: str
    role: str
    priority: int
    note: str = ""


# Order matters — the first matching rule wins.  Patterns are glob-style
# relative to the snapshot root (e.g. ``src/Altirra/source/cmd*.cpp``).
# Use ``**`` to match any depth.
_RULES: Sequence[tuple[str, Classification]] = (
    # --- Fork-only paths (should never appear in upstream diffs) ---------
    ("src/AltirraSDL/**",             Classification("AltirraSDL",       "fork-only",     99, "Fork-only frontend")),
    ("syncing-with-upstream/**",      Classification("sync-tooling",     "fork-only",     99)),
    ("cmake/**",                      Classification("build",            "fork-only",     99)),
    ("vendor/**",                     Classification("vendored-deps",    "fork-only",     99)),
    ("tests/**",                      Classification("fork-tests",       "fork-only",     99)),
    ("android/**",                    Classification("android",          "fork-only",     99)),

    # --- Pure upstream docs / autogen (always copy verbatim) -------------
    ("src/Altirra/autogen/**",        Classification("autogen",          "copy-verbatim", 50)),
    ("src/Altirra/res/changes.txt",   Classification("docs",             "copy-verbatim", 51)),
    ("src/Altirra/res/romset.html",   Classification("docs",             "copy-verbatim", 51)),

    # --- Kernel ROM (always port; will need rebuild) ---------------------
    ("src/Kernel/**",                 Classification("kernel-rom",       "port",          20)),

    # --- Cross-platform core emulation -----------------------------------
    ("src/ATCPU/**",                  Classification("core-cpu",         "port",          10)),
    ("src/ATEmulation/**",            Classification("core-emulation",   "port",          10)),
    ("src/ATDevices/**",              Classification("core-devices",     "port",          10)),
    ("src/ATIO/**",                   Classification("core-io",          "port",          10)),
    ("src/ATNetwork/**",              Classification("core-network",     "port",          10)),
    ("src/ATDebugger/**",             Classification("core-debugger",    "port",          10)),
    ("src/ATCompiler/**",             Classification("core-compiler",    "port",          10)),
    ("src/ATVM/**",                   Classification("core-vm",          "port",          10)),
    ("src/ATCore/**",                 Classification("core-utils",       "port",          10)),
    ("src/Kasumi/**",                 Classification("kasumi",           "port",          10)),
    ("src/vdjson/**",                 Classification("vdjson",           "port",          10)),

    # --- Cross-platform audio (split: cross-platform + win-only backends)
    ("src/ATAudio/source/audioout*_win32.cpp", Classification("ATAudio-win", "skip-win-only", 80)),
    ("src/ATAudio/source/audioout*_wasapi*",   Classification("ATAudio-win", "skip-win-only", 80)),
    ("src/ATAudio/source/audioout*_xa2*",      Classification("ATAudio-win", "skip-win-only", 80)),
    ("src/ATAudio/source/audioout*_dsound*",   Classification("ATAudio-win", "skip-win-only", 80)),
    ("src/ATAudio/source/audioout*_waveout*",  Classification("ATAudio-win", "skip-win-only", 80)),
    ("src/ATAudio/**",                Classification("core-audio",       "port",          10)),

    # --- System / VD2 headers --------------------------------------------
    ("src/h/at/**",                   Classification("headers-at",       "port",          11)),
    ("src/h/vd2/system/**",           Classification("headers-vd2-system", "port",        11)),
    ("src/h/vd2/VDDisplay/**",        Classification("headers-vd2-vddisplay", "review-ui", 30)),
    ("src/h/vd2/Tessa/**",            Classification("headers-vd2-tessa", "skip-win-only",80)),
    ("src/h/vd2/Dita/**",             Classification("headers-vd2-dita", "skip-win-only", 80)),
    ("src/h/vd2/Riza/**",             Classification("headers-vd2-riza", "skip-win-only", 80)),
    ("src/h/vd2/Kasumi/**",           Classification("headers-vd2-kasumi", "port",        11)),
    ("src/h/vd2/**",                  Classification("headers-vd2",      "port",          11)),
    ("src/h/**",                      Classification("headers",          "port",          11)),

    # --- VD2 system / Kasumi source --------------------------------------
    ("src/system/**",                 Classification("system",           "port",          12)),

    # --- Windows-only UI / platform frameworks (do not port verbatim) ----
    ("src/ATNativeUI/**",             Classification("ATNativeUI",       "skip-win-only", 81, "Win32 HWND UI; mirror user-visible changes in ImGui")),
    ("src/ATUI/**",                   Classification("ATUI",             "skip-win-only", 81)),
    ("src/ATUIControls/**",           Classification("ATUIControls",     "skip-win-only", 81)),
    ("src/VDDisplay/**",              Classification("VDDisplay",        "skip-win-only", 81, "GDI/D3D display; fork uses SDL3 backends")),
    ("src/Riza/**",                   Classification("Riza",             "skip-win-only", 81)),
    ("src/Dita/**",                   Classification("Dita",             "skip-win-only", 81)),
    ("src/Tessa/**",                  Classification("Tessa",            "skip-win-only", 81)),
    ("src/ATAppBase/**",              Classification("ATAppBase",        "skip-win-only", 81)),
    ("src/AltirraShell/**",           Classification("AltirraShell",     "skip-win-only", 99)),
    ("src/Asuka/**",                  Classification("Asuka",            "copy-verbatim", 50, "Build tool; used by both Windows and SDL builds")),

    # --- Altirra main module (Win32 app; per-file triage) ---------------
    ("src/Altirra/source/cmd*.cpp",           Classification("cmd",          "reflect-in-ui", 30, "UI command handler; reflect user-visible changes in ImGui")),
    ("src/Altirra/source/ui*_win32.cpp",      Classification("ui-win32",     "skip-win-only", 81)),
    ("src/Altirra/source/devx*_win32.cpp",    Classification("ui-win32",     "skip-win-only", 81)),
    ("src/Altirra/source/ui*.cpp",            Classification("ui",           "reflect-in-ui", 30)),
    ("src/Altirra/source/*_win32.cpp",        Classification("altirra-win",  "skip-win-only", 81)),
    ("src/Altirra/source/**",                 Classification("altirra-core", "port",          15, "Platform-agnostic Altirra sources")),
    ("src/Altirra/h/**",                      Classification("altirra-headers", "port",       15)),
    ("src/Altirra/res/**",                    Classification("altirra-res",  "skip-win-only", 85, "Win32 .rc / resource headers")),
    ("src/Altirra/*.vcxproj*",                Classification("build-meta",   "inform-only",   90)),
    ("src/Altirra/*.sln",                     Classification("build-meta",   "inform-only",   90)),

    # --- Tests ------------------------------------------------------------
    ("src/ATTest/**",                 Classification("tests",            "port-if-wired", 40)),
    ("src/AltirraTest/**",            Classification("tests",            "port-if-wired", 40)),

    # --- Everything else --------------------------------------------------
    ("src/**",                        Classification("unknown-src",      "unknown",       70)),
)


def classify(path: str) -> Classification:
    """Classify a snapshot-relative path. Returns an ``unknown`` entry
    if no rule matched."""
    normalised = path.replace("\\", "/")
    for pattern, cls in _RULES:
        if _glob_match(pattern, normalised):
            return cls
    return Classification("unknown", "unknown", 100, note="unclassified path")


# --- internals -----------------------------------------------------------

def _glob_match(pattern: str, path: str) -> bool:
    """Glob match with path-aware wildcards:

    * ``*``  matches any run of characters **within a single path segment**
             (i.e. does not cross ``/``) — same as git/POSIX pathspecs.
    * ``**`` matches any number of path segments, including zero.
    * ``?``  matches a single character within a segment.

    We deliberately do **not** use ``fnmatch`` here because ``fnmatch``'s
    ``*`` treats ``/`` as an ordinary character, which would cause e.g.
    ``src/Altirra/source/ui*.cpp`` to greedily match
    ``src/Altirra/source/ui_sub/foo.cpp``.
    """
    escaped = re.escape(pattern)
    escaped = escaped.replace(r"\*\*", "§§DOUBLESTAR§§")
    escaped = escaped.replace(r"\*", "[^/]*")
    escaped = escaped.replace(r"\?", "[^/]")
    escaped = escaped.replace("§§DOUBLESTAR§§", ".*")
    return re.fullmatch(escaped, path) is not None


def filter_roles(
    files: Iterable[str],
    roles: Iterable[str],
) -> list[tuple[str, Classification]]:
    """Return (path, classification) tuples whose role is in ``roles``."""
    role_set = set(roles)
    out: list[tuple[str, Classification]] = []
    for f in files:
        cls = classify(f)
        if cls.role in role_set:
            out.append((f, cls))
    return out
