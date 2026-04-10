"""AltirraBridge Python client.

Pure stdlib: socket, json. No external dependencies.

Threading model: not thread-safe. Use one AltirraBridge per thread.
"""

from __future__ import annotations

import json
import os
import socket
from dataclasses import dataclass
from typing import Any, Optional


@dataclass
class RawFrame:
    """A raw GTIA frame capture returned by :meth:`AltirraBridge.rawscreen`.

    Pixel format is XRGB8888 little-endian: each 4-byte group is
    (B, G, R, 0) in memory order. ``stride == width * 4``.
    """

    width: int
    height: int
    stride: int
    pixels: bytes

    def pixels_rgba(self) -> bytes:
        """Return pixels reordered as R, G, B, A (A=255). Suitable
        for ``PIL.Image.frombytes("RGBA", (w, h), data)``.
        """
        out = bytearray(len(self.pixels))
        for i in range(0, len(self.pixels), 4):
            b = self.pixels[i]
            g = self.pixels[i + 1]
            r = self.pixels[i + 2]
            out[i]     = r
            out[i + 1] = g
            out[i + 2] = b
            out[i + 3] = 0xFF
        return bytes(out)


class BridgeError(Exception):
    """Base exception for all AltirraBridge client errors."""


class AuthError(BridgeError):
    """The HELLO handshake was rejected (bad or missing token)."""


class RemoteError(BridgeError):
    """The server returned ``{"ok": false, "error": "..."}``.

    The ``error`` attribute holds the server-side error string.
    """

    def __init__(self, message: str) -> None:
        super().__init__(message)
        self.error = message


class AltirraBridge:
    """Connection to a running AltirraSDL --bridge instance.

    Construct with :meth:`from_token_file` (the common case) or
    :meth:`connect` (when you already know the address and token).

    All command methods raise :class:`BridgeError` (or a subclass) on
    failure. The transport is line-delimited UTF-8: commands sent as
    ``VERB arg1 arg2\\n``, responses read back as a single-line JSON
    object terminated by ``\\n``.

    Use as a context manager to ensure the socket is closed::

        with AltirraBridge.from_token_file("/tmp/altirra-bridge-1234.token") as a:
            a.frame(60)
    """

    # ------------------------------------------------------------------
    # Construction
    # ------------------------------------------------------------------

    def __init__(self) -> None:
        self._sock: Optional[socket.socket] = None
        self._recv_buf = b""
        self._authenticated = False

    @classmethod
    def from_token_file(cls, path: str) -> "AltirraBridge":
        """Read a token file written by AltirraSDL on startup, connect
        to the address it names, perform the HELLO handshake, and
        return a ready-to-use bridge instance.

        The token file is logged to stderr by AltirraSDL when launched
        with ``--bridge``::

            [bridge] listening on tcp:127.0.0.1:54321
            [bridge] token-file: /tmp/altirra-bridge-12345.token

        It contains the bound address on the first line and the
        128-bit hex session token on the second.
        """
        if not os.path.exists(path):
            raise BridgeError(f"token file not found: {path}")
        with open(path, "r", encoding="utf-8") as f:
            lines = [ln.rstrip("\r\n") for ln in f.readlines()]
        if len(lines) < 2:
            raise BridgeError(f"token file is malformed: {path}")
        addr, token = lines[0], lines[1]
        b = cls()
        b.connect(addr)
        b.hello(token)
        return b

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------

    def connect(self, addr_spec: str) -> None:
        """Open the socket. Does not perform HELLO; call :meth:`hello`
        next or use :meth:`from_token_file` for the combined call.

        ``addr_spec`` accepts the same forms as the server's
        ``--bridge`` argument:

        * ``tcp:127.0.0.1:54321`` — loopback TCP (all platforms)
        * ``unix:/path/to/sock`` — POSIX filesystem UDS (POSIX only)
        """
        if addr_spec.startswith("tcp:"):
            rest = addr_spec[4:]
            host, _, port_str = rest.rpartition(":")
            if not host or not port_str:
                raise BridgeError(f"bad tcp spec: {addr_spec}")
            port = int(port_str)
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((host, port))
            # Disable Nagle on the client side. The server already
            # sets TCP_NODELAY on accept, but the option is
            # per-direction — without this, small client writes get
            # batched by the kernel and interact with delayed-ACK
            # to add up to ~40 ms stalls on every request/response
            # round-trip on localhost.
            try:
                s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            except OSError:
                pass  # non-fatal; connection still works, just slower
        elif addr_spec.startswith("unix:"):
            path = addr_spec[5:]
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(path)
        else:
            raise BridgeError(f"unsupported addr spec: {addr_spec}")
        self._sock = s
        self._recv_buf = b""
        self._authenticated = False

    def hello(self, token: str) -> dict:
        """Authenticate. Returns the server's HELLO response payload
        (a dict with at least ``protocol``, ``server``, ``paused``).

        Raises :class:`AuthError` if the token is wrong.
        """
        resp = self._send_command(f"HELLO {token}")
        if not resp.get("ok"):
            raise AuthError(resp.get("error", "HELLO rejected"))
        self._authenticated = True
        return resp

    def close(self) -> None:
        """Close the socket. Idempotent."""
        if self._sock is not None:
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None
        self._authenticated = False

    def __enter__(self) -> "AltirraBridge":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    # ------------------------------------------------------------------
    # Phase 1 commands
    # ------------------------------------------------------------------

    def ping(self) -> dict:
        """Round-trip liveness check. Returns the parsed response."""
        return self._cmd_ok("PING")

    def pause(self) -> dict:
        """Pause the simulator. Cancels any active FRAME gate."""
        return self._cmd_ok("PAUSE")

    def resume(self) -> dict:
        """Resume free-running emulation. Cancels any active FRAME gate."""
        return self._cmd_ok("RESUME")

    def frame(self, n: int = 1) -> dict:
        """Run ``n`` frames then re-pause. Returns immediately; the
        next command waits server-side until the gate releases.

        ``n`` must be ``>= 1`` and reasonable (the server caps it at
        one million to catch obvious bugs).
        """
        if not isinstance(n, int) or n < 1:
            raise BridgeError("frame count must be an integer >= 1")
        return self._cmd_ok(f"FRAME {n}")

    def quit(self) -> dict:
        """Ask the AltirraSDL process to exit cleanly. The socket
        will close shortly afterwards.
        """
        return self._cmd_ok("QUIT")

    # ------------------------------------------------------------------
    # Phase 2 commands — state read
    # ------------------------------------------------------------------

    def regs(self) -> dict:
        """CPU registers + status flags + cycle counter + CPU mode.

        Fields: ``PC, A, X, Y, S, P`` (all hex strings like ``"$3c"``),
        ``flags`` (8-char string like ``"N--B-IZC"``), ``cycles``
        (integer cycle counter since reset), ``mode``
        (``"6502"`` / ``"65C02"`` / ``"65C816"``).
        """
        return self._cmd_ok("REGS")

    def peek(self, addr: int, length: int = 1) -> bytes:
        """Read ``length`` bytes from memory starting at ``addr``.

        Returns a ``bytes`` object. Uses the debug-safe read path —
        reading I/O register addresses ($D000-$D7FF) does NOT trigger
        ANTIC/GTIA/POKEY side effects.

        ``length`` is capped server-side at 16384.
        """
        resp = self._cmd_ok(f"PEEK ${addr:x} {length}")
        return bytes.fromhex(resp["data"])

    def peek16(self, addr: int) -> int:
        """Read a little-endian 16-bit word from ``addr``.

        Convenience wrapper around two PEEK bytes; saves one round
        trip. Returns an int.
        """
        resp = self._cmd_ok(f"PEEK16 ${addr:x}")
        return int(resp["value"].lstrip("$"), 16)

    def antic(self) -> dict:
        """ANTIC register state plus beam position and current DLIST."""
        return self._cmd_ok("ANTIC")

    def gtia(self) -> dict:
        """GTIA register state — all 32 registers with descriptive
        field names (HPOSP0..3, COLPF0..3, PRIOR, GRACTL, etc.) plus
        the current console-switch input state.
        """
        return self._cmd_ok("GTIA")

    def pokey(self) -> dict:
        """POKEY register state — AUDF/AUDC for all four channels,
        AUDCTL, IRQEN, SKCTL, and the other write-side registers.
        """
        return self._cmd_ok("POKEY")

    def pia(self) -> dict:
        """PIA register state — PORTA/B, DDRA/B, CRA/B, plus the
        currently driven port outputs (with DDR applied).
        """
        return self._cmd_ok("PIA")

    def dlist(self) -> list:
        """Walk the current ANTIC display list and return a list of
        decoded entries. Each entry is a dict with: ``addr``, ``ctl``,
        ``mode``, ``kind`` (``graphics``/``blank``/``jmp``/``jvb``),
        ``lms``, ``dli``, ``hscrol``, ``vscrol``, ``pf``, ``dmactl``,
        ``chbase``.
        """
        resp = self._cmd_ok("DLIST")
        return resp.get("entries", [])

    def hwstate(self) -> dict:
        """Combined snapshot: CPU + ANTIC + GTIA + POKEY + PIA in one
        round trip. Returns a dict with sub-objects for each of the
        five sections.
        """
        return self._cmd_ok("HWSTATE")

    def palette(self) -> bytes:
        """GTIA analysis palette — 256 RGB24 entries (768 bytes) as
        a flat ``bytes`` object. Use ``palette[i*3:i*3+3]`` for the
        RGB triple of color index ``i``.
        """
        resp = self._cmd_ok("PALETTE")
        return bytes.fromhex(resp["data"])

    def load_act_palette(self, act_bytes: bytes) -> float:
        """Upload a 768-byte Adobe Color Table (``.act``) and run
        the palette-fitting solver that Windows Altirra uses in its
        Color Image Reference dialog. Two passes: matching=None,
        then matching=sRGB. The server updates the active profile's
        NTSC (or PAL, if currently in PAL mode) analog-decoder
        parameters so GTIA composites subsequent frames with a
        palette that approximates the supplied .act.

        Returns the achieved per-channel standard-error of the fit
        (roughly "how close the analog model got to the target",
        same metric the Windows dialog reports).

        Takes roughly 50-200 ms wall-clock depending on CPU and
        complexity of the target palette. Safe to call at any
        time; persists until the next :meth:`reset_palette` or
        :meth:`cold_reset`.
        """
        if len(act_bytes) != 768:
            raise BridgeError(
                f"load_act_palette: expected 768 bytes, got {len(act_bytes)}")
        import base64
        b64 = base64.b64encode(act_bytes).decode("ascii")
        resp = self._cmd_ok(f"PALETTE_LOAD_ACT {b64}")
        return float(resp.get("rms_error", 0.0))

    def reset_palette(self) -> dict:
        """Restore GTIA's factory-default NTSC and PAL color
        parameters, undoing any prior :meth:`load_act_palette`.
        """
        return self._cmd_ok("PALETTE_RESET")

    # ------------------------------------------------------------------
    # Phase 3 commands — state write & input injection
    # ------------------------------------------------------------------

    def poke(self, addr: int, value: int) -> dict:
        """Write a byte at ``addr`` (0..$FFFF). Uses the debug-safe
        write path: I/O register addresses ($D000-$D7FF) write the
        underlying latch without triggering ANTIC/GTIA/POKEY side
        effects.
        """
        return self._cmd_ok(f"POKE ${addr:x} ${value & 0xFF:02x}")

    def poke16(self, addr: int, value: int) -> dict:
        """Write a little-endian 16-bit word at ``addr``."""
        return self._cmd_ok(f"POKE16 ${addr:x} ${value & 0xFFFF:04x}")

    def hwpoke(self, addr: int, value: int) -> dict:
        """Hardware-register poke. Unlike :meth:`poke`, which
        writes the debug-safe RAM latch with no side effects,
        ``hwpoke`` routes the write through the real CPU bus —
        for addresses in the ``$D000-$D7FF`` I/O range it
        triggers the same ANTIC / GTIA / POKEY / PIA handlers a
        ``STA $Dxxx`` 6502 instruction would.

        Use this to drive ANTIC's ``DLISTL``/``DLISTH``/``DMACTL``,
        GTIA's colour registers, POKEY audio registers, etc. from
        a bare-metal client that has parked the CPU via
        :meth:`boot_bare`.
        """
        return self._cmd_ok(f"HWPOKE ${addr:x} ${value & 0xFF:02x}")

    def memload(self, addr: int, data: bytes) -> dict:
        """Load arbitrary bytes into RAM starting at ``addr``. Sent
        inline as base64; works over `adb forward` on Android with
        no shared filesystem.
        """
        import base64
        b64 = base64.b64encode(data).decode("ascii")
        return self._cmd_ok(f"MEMLOAD ${addr:x} {b64}")

    def memdump(self, addr: int, length: int) -> bytes:
        """Read ``length`` bytes (max 65536) from ``addr`` and return
        them. Same debug-safe read path as :meth:`peek`, just with
        a larger cap and a base64-encoded wire payload.
        """
        import base64
        resp = self._cmd_ok(f"MEMDUMP ${addr:x} {length}")
        return base64.b64decode(resp["data"])

    def joy(self, port: int, direction: str = "centre",
            fire: bool = False) -> dict:
        """Inject joystick state for ``port`` (0..3).

        ``direction`` is one of: ``centre``, ``up``, ``down``,
        ``left``, ``right``, ``upleft``, ``upright``, ``downleft``,
        ``downright`` (compass forms ``n``, ``ne``, ``e``, ``se``,
        ``s``, ``sw``, ``w``, ``nw``, ``c`` are also accepted).
        """
        cmd = f"JOY {port} {direction}"
        if fire:
            cmd += " fire"
        return self._cmd_ok(cmd)

    def key(self, name: str, shift: bool = False, ctrl: bool = False) -> dict:
        """Push one keystroke into POKEY's queue.

        ``name`` is a key identifier like ``"A"``, ``"SPACE"``,
        ``"RETURN"``, ``"ESC"``, ``"1"`` etc. The full table lives
        in the server's ``bridge_commands_write.cpp``. Letters are
        case-insensitive (the lowercase letter is the on-key).

        ``shift``/``ctrl`` OR the corresponding KBCODE bits.
        """
        cmd = f"KEY {name}"
        if shift: cmd += " shift"
        if ctrl:  cmd += " ctrl"
        return self._cmd_ok(cmd)

    def consol(self, start: bool = False, select: bool = False,
               option: bool = False) -> dict:
        """Hold the named console switches down (active-low). Any
        switches not passed True are released. Pass no arguments to
        release all three switches.
        """
        names = []
        if start:  names.append("start")
        if select: names.append("select")
        if option: names.append("option")
        return self._cmd_ok("CONSOL " + " ".join(names) if names else "CONSOL")

    def boot(self, path: str) -> dict:
        """Load and boot a media file (XEX/ATR/CAS/CAR). The action
        is queued via the SDL3 deferred-action queue and runs at
        the start of the next frame.

        After ``boot()``, issue ``frame(N)`` to wait for the load
        to complete before reading state.
        """
        return self._cmd_ok(f"BOOT {path}")

    def boot_bare(self) -> dict:
        """Boot a tiny embedded stub that parks the CPU and leaves
        the machine as a blank raw-display canvas.

        The server ships a 30-byte stub XEX. When loaded, it:

        - disables IRQs and NMIs (no VBI, no DLI, no reset NMI)
        - disables ANTIC DMA (screen blank)
        - unmaps the BASIC cartridge
        - parks the CPU in an infinite ``JMP *`` loop

        After ``boot_bare()`` returns, the stub is **guaranteed**
        to have fully loaded and be executing its park loop: the
        server polls for the stub signature at ``$0600`` and a CPU
        PC inside the park region before it sends the response.
        No frame-settle handshake is required on the client side.

        After this call the client owns the machine. It can POKE
        ANTIC's ``DLISTL``/``DLISTH`` (``$D402``/``$D403``) via
        :meth:`hwpoke`, install a display list anywhere in RAM,
        enable DMACTL, write pixel data, and never have the OS
        modify any of it. This is the recommended pattern for any
        client that wants to use the emulator as a raw display
        device — see the 04_paint example.

        Typical wall-clock latency: 1-3 seconds (bounded by OS
        cold-boot time on NTSC, which the server advances through
        internally).

        See also: :meth:`load_act_palette`, :meth:`reset_palette`.
        """
        return self._cmd_ok("BOOT_BARE")

    def mount(self, drive: int, path: str) -> dict:
        """Mount a disk image into ``drive`` (0..14, where 0=D1)."""
        return self._cmd_ok(f"MOUNT {drive} {path}")

    def cold_reset(self) -> dict:
        """Cold-reset the simulator. Preserves pause state."""
        return self._cmd_ok("COLD_RESET")

    def warm_reset(self) -> dict:
        """Warm-reset the simulator (Reset key). Preserves pause state."""
        return self._cmd_ok("WARM_RESET")

    def state_save(self, path: str) -> dict:
        """Save the simulator snapshot to ``path``. Async via the
        deferred-action queue; issue ``frame(1)`` to wait for the
        write to complete.
        """
        return self._cmd_ok(f"STATE_SAVE {path}")

    def state_load(self, path: str) -> dict:
        """Load a previously-saved snapshot from ``path``. Async."""
        return self._cmd_ok(f"STATE_LOAD {path}")

    def config(self, key: Optional[str] = None,
               value: Optional[str] = None) -> dict:
        """Query or set simulator configuration.

        * ``config()`` — return all config keys.
        * ``config("basic")`` — query one key.
        * ``config("basic", "false")`` — set a key, return full config.

        Supported keys:

        * ``basic`` — ``"true"``/``"false"``: built-in BASIC enabled.
        * ``machine`` — hardware mode: ``"800"``, ``"800XL"``,
          ``"130XE"``, ``"XEGS"``, ``"1200XL"``, ``"1400XL"``,
          ``"5200"``.
        * ``memory`` — RAM size: ``"8K"`` through ``"1088K"``.
        * ``debugbrkrun`` — ``"true"``/``"false"``: break at EXE run
          address.

        Setting ``machine`` or ``memory`` triggers a cold reset
        (preserving pause state).  Setting ``basic`` or
        ``debugbrkrun`` does not.
        """
        if key is None:
            return self._cmd_ok("CONFIG")
        if value is None:
            return self._cmd_ok(f"CONFIG {key}")
        return self._cmd_ok(f"CONFIG {key} {value}")

    # ------------------------------------------------------------------
    # Phase 4 commands — rendering (SCREENSHOT / RAWSCREEN / RENDER_FRAME)
    # ------------------------------------------------------------------

    def screenshot(self, path: Optional[str] = None) -> bytes:
        """Capture the last GTIA frame as a PNG.

        * With ``path=None`` (default): returns PNG bytes inline.
        * With ``path="/tmp/foo.png"``: writes the PNG to the
          server-side filesystem and returns its bytes from disk on
          the client side is the caller's job. The returned value is
          an empty ``bytes`` object in that case (the PNG lives on
          the server).

        On headless ``AltirraBridgeServer`` a null display backend
        keeps ``mpLastFrame`` populated, so this works with or without
        a GUI window attached.
        """
        import base64
        if path is None:
            resp = self._cmd_ok("SCREENSHOT inline=true")
            return base64.b64decode(resp["data"])
        else:
            self._cmd_ok(f"SCREENSHOT path={path}")
            return b""

    def rawscreen(self, path: Optional[str] = None) -> "RawFrame":
        """Capture the last GTIA frame as a raw XRGB8888 little-endian
        buffer. Bytes on the wire are B, G, R, 0 per pixel (native
        little-endian order).

        Returns a :class:`RawFrame` with ``width``, ``height``,
        ``stride`` and ``pixels`` (the raw bytes). Use :meth:`pixels_rgba`
        to convert to R, G, B, A order if your downstream consumer
        (e.g. PIL ``Image.frombytes``) expects that.
        """
        import base64
        if path is None:
            resp = self._cmd_ok("RAWSCREEN inline=true")
            return RawFrame(
                width=int(resp["width"]),
                height=int(resp["height"]),
                stride=int(resp["stride"]),
                pixels=base64.b64decode(resp["data"]),
            )
        else:
            resp = self._cmd_ok(f"RAWSCREEN path={path}")
            return RawFrame(
                width=int(resp["width"]),
                height=int(resp["height"]),
                stride=int(resp["stride"]),
                pixels=b"",
            )

    def render_frame(self) -> bytes:
        """Alias for :meth:`screenshot()` (inline PNG). Reserved for a
        future extension that accepts a state-override block.
        """
        import base64
        resp = self._cmd_ok("RENDER_FRAME")
        return base64.b64decode(resp["data"])

    # ------------------------------------------------------------------
    # Phase 5a commands — debugger introspection
    # ------------------------------------------------------------------

    def disasm(self, addr: int, count: int = 1) -> list:
        """Disassemble ``count`` instructions starting at ``addr``.

        Returns a list of dicts with keys ``addr``, ``length``,
        ``bytes`` (list of hex strings), ``text``, ``next``. The
        ``text`` field uses Altirra's disassembler — symbol
        resolution (COLDSV, SIOV, etc.) and auto-selection of
        6502/65C02/65C816 mode come for free.
        """
        resp = self._cmd_ok(f"DISASM ${addr:x} {count}")
        return resp.get("insns", [])

    def history(self, count: int = 64) -> list:
        """Return the last ``count`` CPU history entries, oldest
        first. Each entry has ``cycle``, ``pc``, ``op``,
        ``a``/``x``/``y``/``s``/``p``, ``ea``, ``irq``, ``nmi``.

        Altirra keeps a 131072-entry ring buffer; ``count`` is capped
        server-side at 4096. This is the single biggest RE primitive
        AltirraBridge exposes over the pyA8 reference.
        """
        resp = self._cmd_ok(f"HISTORY {count}")
        return resp.get("entries", [])

    def eval_expr(self, expr: str) -> int:
        """Evaluate a debugger expression against the current CPU
        state. The expression syntax is the one Altirra's ``.printf``
        and conditional breakpoints use — ``pc``, ``a``, ``x``, ``y``,
        ``dw($fffc)`` (word deref), ``db($80)`` (byte deref), symbol
        names, arithmetic and bitwise operators.

        Returns the integer result (may be negative).
        """
        resp = self._cmd_ok(f"EVAL {expr}")
        v = resp["value"]
        if isinstance(v, int) and v >= 0x80000000:
            v -= 0x100000000
        return v

    def callstack(self, count: int = 64) -> list:
        """JSR/RTS call-stack walk. Returns a list of frames, each
        with ``pc`` (return address), ``sp``, ``p``.
        """
        resp = self._cmd_ok(f"CALLSTACK {count}")
        return resp.get("frames", [])

    def memmap(self) -> list:
        """Decoded memory layout: list of regions with ``name``,
        ``lo``, ``hi``, ``kind`` (``ram``/``rom``/``io``), ``note``.
        Derived from PIA PORTB + cartridge state.
        """
        resp = self._cmd_ok("MEMMAP")
        return resp.get("regions", [])

    def bank_info(self) -> dict:
        """PORTB-decoded banking state: ``os_rom``, ``basic_enabled``,
        ``selftest_rom``, ``cpu_bank_at_window``,
        ``antic_bank_at_window``, ``xe_bank`` (0..3).
        """
        return self._cmd_ok("BANK_INFO")

    def cart_info(self) -> dict:
        """Cartridge info: ``present``, ``mode`` (numeric
        ``ATCartridgeMode``), ``size``, ``bank``.
        """
        return self._cmd_ok("CART_INFO")

    def pmg(self) -> dict:
        """Decoded player/missile state: ``hposp``/``hposm`` (4-entry
        lists), ``sizep``/``sizem``, ``grafp``/``grafm``, ``colpm``,
        ``colpf``, ``colbk``, ``prior``, ``vdelay``, ``gractl``, plus
        the collision read-side: ``coll_mpf``, ``coll_ppf``,
        ``coll_mpl``, ``coll_ppl``.
        """
        return self._cmd_ok("PMG")

    def audio_state(self) -> dict:
        """Decoded POKEY per-channel state: ``audctl``, flags
        (``nine_bit_poly``, ``join_1_2``, etc.), and a ``channels``
        list of 4 dicts with ``audf``, ``audc``, ``volume``,
        ``distortion``, ``clock`` (``"64kHz"``/``"15kHz"``/``"1.79MHz"``).
        """
        return self._cmd_ok("AUDIO_STATE")

    # ------------------------------------------------------------------
    # Phase 5b commands — breakpoints, symbols, memsearch, profiler,
    # verifier
    # ------------------------------------------------------------------

    def bp_set(self, addr: int, condition: Optional[str] = None) -> int:
        """Set a PC breakpoint at ``addr``. With ``condition``, the
        debugger only halts when the expression is non-zero at the
        PC. Returns the assigned breakpoint id.

        The condition runs through Altirra's expression evaluator
        (same syntax as :meth:`eval_expr`). The condition token must
        not contain spaces — wrap complex conditions in parentheses
        or use a helper variable.
        """
        cmd = f"BP_SET ${addr:x}"
        if condition is not None:
            cmd += f" condition={condition}"
        return int(self._cmd_ok(cmd)["id"])

    def bp_clear(self, bp_id: int) -> dict:
        """Clear a single breakpoint by id."""
        return self._cmd_ok(f"BP_CLEAR {bp_id}")

    def bp_clear_all(self) -> dict:
        """Clear every user breakpoint."""
        return self._cmd_ok("BP_CLEAR_ALL")

    def bp_list(self) -> list:
        """List every active breakpoint. Each entry has ``id``,
        ``addr``, ``length``, ``pc``, ``read``, ``write``,
        ``oneshot``, ``continue_on_hit``, ``conditional``.
        """
        resp = self._cmd_ok("BP_LIST")
        return resp.get("breakpoints", [])

    def watch_set(self, addr: int, mode: str = "r") -> list:
        """Set an access (read/write) breakpoint at ``addr``.

        ``mode`` is one of ``"r"``, ``"w"``, ``"rw"``. Returns a list
        of breakpoint ids (``rw`` creates two — one read, one write).

        Range watches aren't supported by the underlying Altirra API;
        set one watch per address.
        """
        resp = self._cmd_ok(f"WATCH_SET ${addr:x} mode={mode}")
        return list(resp.get("ids", []))

    def sym_load(self, path: str) -> int:
        """Load a MADS ``.lab``/``.lbl``/``.lst``, DEBUG.COM, or
        Altirra ``.sym`` symbol file. Returns the assigned module id.
        """
        resp = self._cmd_ok(f"SYM_LOAD {path}")
        return int(resp["module_id"])

    def sym_resolve(self, name: str) -> int:
        """Resolve a symbol ``name`` to its address."""
        resp = self._cmd_ok(f"SYM_RESOLVE {name}")
        return int(resp["value"])

    def sym_lookup(self, addr: int, flags: str = "rwx") -> dict:
        """Reverse symbol lookup: nearest symbol at or before ``addr``.
        Returns ``{name, base, offset}`` or raises :class:`RemoteError`
        if no symbol matches.

        ``flags`` restricts by symbol kind: ``r``/``w``/``x`` or any
        combination.
        """
        return self._cmd_ok(f"SYM_LOOKUP ${addr:x} flags={flags}")

    def memsearch(self, pattern: bytes, start: int = 0,
                  end: int = 0x10000) -> list:
        """Linear scan ``[start, end)`` for byte pattern ``pattern``.
        Returns a list of hit addresses (hex strings).

        The server caps the hit count at 1024. Works over the CPU
        view (banking-aware).
        """
        hex_pat = pattern.hex()
        resp = self._cmd_ok(
            f"MEMSEARCH {hex_pat} start=${start:x} end=${end:x}")
        return list(resp.get("hits", []))

    # --- Profiler ---

    def profile_start(self, mode: str = "insns") -> dict:
        """Start the CPU profiler.

        ``mode`` is ``"insns"`` (default, flat per-address hot list),
        ``"functions"``, ``"callgraph"`` (enables
        :meth:`profile_dump_tree`), or ``"basicblock"``.
        """
        return self._cmd_ok(f"PROFILE_START mode={mode}")

    def profile_stop(self) -> dict:
        """Stop the profiler. Must be called before ``profile_dump*``
        because the underlying session-take is destructive and would
        corrupt the running builder.
        """
        return self._cmd_ok("PROFILE_STOP")

    def profile_status(self) -> dict:
        """Return ``{"enabled": bool, "running": bool}``."""
        return self._cmd_ok("PROFILE_STATUS")

    def profile_dump(self, top: int = 32) -> dict:
        """Dump the top ``top`` hot addresses. Returns a dict with
        ``total_cycles``, ``total_insns``, ``count``, and ``hot``
        (list of ``{addr, cycles, insns, calls}``).

        Requires :meth:`profile_stop` first. Calling twice returns an
        empty session the second time (the data is moved out of the
        profiler on first read).
        """
        return self._cmd_ok(f"PROFILE_DUMP top={top}")

    def profile_dump_tree(self) -> list:
        """Dump the hierarchical call tree. Only valid in ``callgraph``
        mode. Returns a list of nodes with ``ctx``, ``parent``,
        ``addr``, ``calls``, ``excl_cycles``, ``excl_insns``,
        ``incl_cycles``, ``incl_insns``. Walk the parent chain to
        render the tree.
        """
        resp = self._cmd_ok("PROFILE_DUMP_TREE")
        return resp.get("nodes", [])

    # --- Verifier ---

    def verifier_status(self) -> dict:
        """Return ``{"enabled": bool, "flags": "$xxx"}``."""
        return self._cmd_ok("VERIFIER_STATUS")

    def verifier_set(self, flags: Optional[int] = None) -> dict:
        """Enable the CPU verifier with the given flag bitmask, or
        disable it if ``flags`` is ``None`` or ``0``.

        Flag bits (from Altirra's ``verifier.h``):
        ``UndocumentedKernelEntry = 0x01``, ``RecursiveNMI = 0x02``,
        ``InterruptRegs = 0x04``, ``64KWrap = 0x08``,
        ``AbnormalDMA = 0x10``, ``AddressZero = 0x20``,
        ``LoadingOverDisplayList = 0x40``,
        ``CallingConventionViolations = 0x80``,
        ``NonCanonicalHardwareAddress = 0x100``, ``StackWrap = 0x200``,
        ``StackInZP816 = 0x400``.
        """
        if not flags:
            return self._cmd_ok("VERIFIER_SET flags=off")
        return self._cmd_ok(f"VERIFIER_SET flags=0x{flags:x}")

    # ------------------------------------------------------------------
    # Internal: send/recv
    # ------------------------------------------------------------------

    def _cmd_ok(self, command: str) -> dict:
        resp = self._send_command(command)
        if not resp.get("ok"):
            raise RemoteError(resp.get("error", "remote error"))
        return resp

    def _send_command(self, command: str) -> dict:
        if self._sock is None:
            raise BridgeError("not connected")
        if not command.endswith("\n"):
            command += "\n"
        try:
            self._sock.sendall(command.encode("utf-8"))
        except OSError as e:
            raise BridgeError(f"send failed: {e}") from e

        line = self._recv_line()
        try:
            return json.loads(line)
        except json.JSONDecodeError as e:
            raise BridgeError(f"bad JSON from server: {line!r}") from e

    def _recv_line(self) -> str:
        assert self._sock is not None
        while b"\n" not in self._recv_buf:
            try:
                chunk = self._sock.recv(65536)
            except OSError as e:
                raise BridgeError(f"recv failed: {e}") from e
            if not chunk:
                raise BridgeError("peer closed connection")
            self._recv_buf += chunk
        line, _, rest = self._recv_buf.partition(b"\n")
        self._recv_buf = rest
        return line.decode("utf-8", errors="replace").rstrip("\r")
