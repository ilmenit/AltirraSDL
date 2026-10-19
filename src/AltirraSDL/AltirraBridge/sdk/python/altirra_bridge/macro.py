"""macro -- run a whole sequence of Bridge calls in one call.

Synchronous, matching the rest of this SDK: no asyncio dependency, so
this is directly usable from a plain command-line script --
``AltirraBridge.macro(steps)`` (or the module-level :func:`run_macro`,
if you'd rather not add a method to the class) -- with no MCP server
involved. AltirraMCP's ``macro`` tool is a thin async wrapper around
this same function (see ``AltirraBridge/mcp/altirra_mcp/tools_macro.py``):
one canonical implementation, two callers.

See ``docs/altirra-mcp-design.md`` §6 in the Altirra source tree for
the full design.

Step shape: a list of single-key dicts naming the step type, e.g.

    [
        {"mount":      {"drive": 0, "path": "/path/to/dos.atr"}},
        {"config":     {"machine": "800XL", "basic": "false"}},
        {"cold_reset": {}},
        {"wait_text":  {"text": "Ready", "timeout_frames": 600}},
        {"type":       {"text": "dir"}},
        {"key":        {"name": "RETURN"}},
    ]

Most step types are a direct passthrough to the identically-named
:class:`AltirraBridge` method (``args`` become that method's keyword
arguments). ``wait_text``/``wait_expr`` are composite steps: a small
poll loop over ``frame``/``screen_text``/``eval_expr``, not a single
Bridge command. ``config`` is also handled specially: unlike every
other step, its ``args`` dict can name more than one key, applied as
one ``CONFIG`` call per key (see :meth:`AltirraBridge.config_many`).
"""

from __future__ import annotations

from typing import TYPE_CHECKING, Any, Dict, List, Optional

from .client import BridgeError

if TYPE_CHECKING:
    from .client import AltirraBridge

# Step name -> AltirraBridge method name, for the (few) cases where
# they differ. Everything else passes the step name straight through
# to getattr(bridge, step_name).
_STEP_ALIASES: Dict[str, str] = {
    "eval": "eval_expr",
}

# Step names handled specially, not via getattr dispatch: wait_text/
# wait_expr aren't single Bridge calls, and config's multi-key/
# cold-reset-ordering semantics don't match a plain 1:1 passthrough.
_SPECIAL_STEPS = frozenset({"wait_text", "wait_expr", "config"})

# Explicit allowlist of passthrough step names (after alias
# resolution) -- default-deny, not default-allow: connection
# lifecycle methods (from_token_file/connect/hello/close), the
# checkpoint context manager, and anything private aren't meaningful
# as a macro step, and a stray typo should read as "unknown step
# type", not silently resolve to some unrelated attribute.
_ALLOWED_PASSTHROUGH_STEPS = frozenset({
    "ping", "pause", "resume", "frame", "quit",
    "regs", "peek", "peek16", "antic", "gtia", "pokey", "pia", "dlist",
    "hwstate", "palette", "load_act_palette", "reset_palette", "screen_text",
    "poke", "poke16", "hwpoke", "memload", "memdump",
    "joy", "key", "type", "consol",
    "boot", "boot_bare", "mount", "eject", "cart_eject", "fresh",
    "cold_reset", "warm_reset",
    "state_save", "state_load", "state_list", "state_drop",
    "device_list", "device_get", "device_set", "device_remove", "device_clear",
    "enable_modern_addons", "disable_modern_addons",
    "screenshot", "rawscreen", "render_frame",
    "disasm", "history", "eval_expr", "callstack", "memmap", "bank_info",
    "cart_info", "pmg", "audio_state",
    "bp_set", "bp_clear", "bp_clear_all", "bp_list", "watch_set",
    "sym_load", "sym_unload", "sym_clear_all", "sym_resolve", "sym_lookup",
    "memsearch",
    "profile_start", "profile_stop", "profile_status", "profile_dump",
    "profile_dump_tree",
    "verifier_status", "verifier_set",
})


def _run_wait_text(bridge: "AltirraBridge", text: str, timeout_frames: int,
                    poll_frames: int = 10) -> dict:
    """Poll SCREEN_TEXT every `poll_frames` frames until `text`
    appears as a substring of some line, or `timeout_frames` elapses.
    """
    elapsed = 0
    lines: List[str] = []
    while elapsed < timeout_frames:
        try:
            bridge.frame(poll_frames)
            lines = bridge.screen_text()
        except BridgeError as e:
            # A real Bridge error (bad frame count, disconnected, ...)
            # is not "not yet visible" -- surface it immediately rather
            # than retrying it until timeout_frames is exhausted.
            return {"ok": False, "error": str(e)}
        if any(text in line for line in lines):
            return {"ok": True, "frames_waited": elapsed}
        elapsed += poll_frames
    return {"ok": False, "error": f"timeout waiting for {text!r}", "last_screen": lines}


def _run_wait_expr(bridge: "AltirraBridge", expr: str, timeout_frames: int,
                    poll_frames: int = 1) -> dict:
    """Poll EVAL every `poll_frames` frames until `expr` is non-zero,
    or `timeout_frames` elapses. `poll_frames` defaults lower than
    `wait_text`'s (1 vs 10): an EVAL round trip is cheaper than a
    SCREEN_TEXT one, and memory-flag conditions can flip within a
    single frame.
    """
    elapsed = 0
    while elapsed < timeout_frames:
        try:
            bridge.frame(poll_frames)
            if bridge.eval_expr(expr):
                return {"ok": True, "frames_waited": elapsed}
        except BridgeError as e:
            return {"ok": False, "error": str(e)}
        elapsed += poll_frames
    return {"ok": False, "error": f"timeout waiting for {expr!r}"}


def _run_step(bridge: "AltirraBridge", step: Any) -> dict:
    if not isinstance(step, dict) or len(step) != 1:
        raise BridgeError(f"macro: each step must be an object with exactly one key (got {step!r})")
    (kind, args), = step.items()
    if not isinstance(args, dict):
        raise BridgeError(f"macro: step {kind!r} arguments must be an object")

    if kind == "wait_text":
        return _run_wait_text(bridge, **args)
    if kind == "wait_expr":
        return _run_wait_expr(bridge, **args)
    if kind == "config":
        return bridge.config_many(**args)

    method_name = _STEP_ALIASES.get(kind, kind)
    if method_name not in _ALLOWED_PASSTHROUGH_STEPS:
        raise BridgeError(f"macro: unknown step type {kind!r}")

    method = getattr(bridge, method_name)
    try:
        result = method(**args)
    except TypeError as e:
        raise BridgeError(f"macro: bad arguments for step {kind!r}: {e}") from e

    # Most passthrough methods return the raw Bridge response dict
    # (already has "ok"); a few return a bare list/int/bytes/dataclass
    # (e.g. rawscreen -> RawFrame). Wrap those uniformly so every
    # macro result entry has "ok".
    if isinstance(result, dict) and "ok" in result:
        return result
    return {"ok": True, "result": result}


def run_macro(bridge: "AltirraBridge", steps: List[Dict[str, Any]],
               stop_on_error: bool = True) -> dict:
    """Run `steps` in order against `bridge`. See module docstring for
    step shape.

    Returns ``{"ok", "steps_completed", "results"}`` -- one result per
    step actually run, so a failed 8-step macro doesn't read as an
    opaque single failure. Default ``stop_on_error=True`` halts at the
    first failing step and leaves the emulator in whatever state it
    reached (not rolled back) so the failure is inspectable.
    ``stop_on_error=False`` runs every step regardless and reports all
    results -- for a macro that's deliberately best-effort.
    """
    results: List[dict] = []
    for step in steps:
        try:
            result = _run_step(bridge, step)
        except BridgeError as e:
            result = {"ok": False, "error": str(e)}
        kind = next(iter(step), "?") if isinstance(step, dict) else "?"
        results.append({"step": kind, **result})
        if not result.get("ok", False) and stop_on_error:
            break

    ok = len(results) == len(steps) and all(r.get("ok", False) for r in results)
    return {"ok": ok, "steps_completed": len(results), "results": results}
