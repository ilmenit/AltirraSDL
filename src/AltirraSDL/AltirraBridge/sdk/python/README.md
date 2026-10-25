# altirra_bridge

Python client for the AltirraBridge scripting protocol — drive
[Altirra](https://www.virtualdub.org/altirra.html) (Atari 8-bit
emulator) programmatically over a local socket. Use it for automated
testing, AI-driven gameplay, headless screenshot capture, reverse
engineering of Atari binaries, and anything else where you'd want to
script an emulator.

Pure stdlib — no external dependencies.

## Install

From a release package:

```sh
cd AltirraBridge/sdk/python
pip install .
```

This also installs `altirra-macro TOKEN_FILE MACRO_FILE`, which runs a JSON
list of macro steps and prints the results as JSON. Without installing,
run `python3 sdk/python/altirra-macro TOKEN_FILE MACRO_FILE` from the bridge
distribution directory.

Binary results (memory reads, screenshots, rendered pixels) are represented
as `{"encoding": "base64", "data": "..."}`. Raw screen results retain their
`width`, `height`, `stride`, and `pixels` fields, with `pixels` encoded in
the same form. The Python `bridge.macro()` API keeps its native bytes and
`RawFrame` values.

## Quick start

In one terminal, launch AltirraSDL with the bridge enabled:

```sh
./AltirraSDL --bridge
```

It will log two lines like:

```
[bridge] listening on tcp:127.0.0.1:54321
[bridge] token-file: /tmp/altirra-bridge-12345.token
```

In another terminal:

```python
from altirra_bridge import AltirraBridge

with AltirraBridge.from_token_file("/tmp/altirra-bridge-12345.token") as a:
    a.ping()
    a.frame(60)         # advance 60 frames then re-pause
    a.pause()
    a.resume()
```

Enable modern demo/debug hardware before booting a program:

```python
with AltirraBridge.from_token_file("/tmp/altirra-bridge-12345.token") as a:
    a.enable_modern_addons()          # 1088K, Stereo POKEY, VBXE, Covox, SoundBoard, Rapidus
    a.device_set("vbxe", True, version=126, base="d600")
    a.boot("demo.xex")
```

Add nested peripherals with an explicit parent bus:

```python
with AltirraBridge.from_token_file("/tmp/altirra-bridge-12345.token") as a:
    a.device_add("printer", parent="/")
    a.device_add("fx80", parent="/printer/parport",
                 auto_lf=True, intl_mode=1)
```

See `examples/` for runnable scripts and `docs/PROTOCOL.md` (in the
parent AltirraBridge directory) for the full wire contract.


VBXE development uses `a.vbxe()` for structured state and
`a.vbxe_read(address, length)` / `a.vbxe_write(address, bytes)` for local
512K RAM without MEMAC remapping. Transfers are 1..16384 bytes and cannot
cross the memory boundary. The default core is FX 1.27, which has no shared
memory mode; request FX 1.26 explicitly for shared-memory experiments.
See the [VBXE workflow](../../docs/COMMANDS.md#vbxe-development).
