#!/usr/bin/env python3
"""altirra-macro — run a macro file against a running AltirraSDL /
AltirraBridgeServer instance via AltirraBridge.macro().

A macro file is JSON: either a bare list of step objects,

    [
        {"mount":      {"drive": 0, "path": "/path/to/dos.atr"}},
        {"config":     {"machine": "800XL", "basic": "false"}},
        {"cold_reset": {}},
        {"wait_text":  {"text": "Ready", "timeout_frames": 600}},
        {"type":       {"text": "dir"}},
        {"key":        {"name": "RETURN"}}
    ]

or an object with a required "steps" key and an optional
"stop_on_error" key (defaults to true either way):

    {"steps": [...], "stop_on_error": true}

See altirra_bridge/macro.py for the full step-type reference.

Run:
    1. start a bridge server (either GUI or headless):
         ./AltirraSDL --bridge=tcp:127.0.0.1:0
       or
         ./AltirraBridgeServer --bridge=tcp:127.0.0.1:0
       It logs a token-file path on stderr, e.g.
         [bridge] token-file: /tmp/altirra-bridge-12345.token

    2. altirra-macro /tmp/altirra-bridge-12345.token my-macro.json
"""

import base64
from dataclasses import asdict, is_dataclass
import json
import sys

from altirra_bridge import AltirraBridge, BridgeError


def json_result(value):
    """Represent binary SDK values and frame metadata without losing data."""
    if isinstance(value, bytes):
        return {"encoding": "base64", "data": base64.b64encode(value).decode("ascii")}
    if is_dataclass(value) and not isinstance(value, type):
        return asdict(value)
    raise TypeError(f"Object of type {type(value).__name__} is not JSON serializable")


def load_macro(path):
    """Return (steps, stop_on_error) from a macro file."""
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)

    if isinstance(data, list):
        return data, True

    if isinstance(data, dict):
        if "steps" not in data:
            raise BridgeError(f'{path}: macro object must have a "steps" key')
        steps = data["steps"]
        if not isinstance(steps, list):
            raise BridgeError(f'{path}: "steps" must be a list')
        return steps, bool(data.get("stop_on_error", True))

    raise BridgeError(
        f"{path}: macro file must be a JSON list of steps, or an "
        'object with a "steps" key'
    )


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <token-file> <macro-file>", file=sys.stderr)
        print(
            "  The token file path is logged on stderr by AltirraSDL/\n"
            "  AltirraBridgeServer on startup, e.g.\n"
            "  /tmp/altirra-bridge-<pid>.token",
            file=sys.stderr,
        )
        return 2

    token_file, macro_file = sys.argv[1], sys.argv[2]

    try:
        steps, stop_on_error = load_macro(macro_file)
    except (OSError, json.JSONDecodeError, BridgeError) as e:
        print(f"altirra-macro: {e}", file=sys.stderr)
        return 2

    try:
        with AltirraBridge.from_token_file(token_file) as bridge:
            result = bridge.macro(steps, stop_on_error=stop_on_error)
    except BridgeError as e:
        print(f"altirra-macro: {e}", file=sys.stderr)
        return 1

    print(json.dumps(result, indent=2, default=json_result))
    return 0 if result.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
