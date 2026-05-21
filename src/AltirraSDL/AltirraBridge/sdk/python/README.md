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

See `examples/` for runnable scripts and `docs/PROTOCOL.md` (in the
parent AltirraBridge directory) for the full wire contract.
