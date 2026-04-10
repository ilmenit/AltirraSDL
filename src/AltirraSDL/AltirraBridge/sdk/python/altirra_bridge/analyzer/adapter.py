"""Adapter that presents an ``AltirraBridge`` client as the minimal
emulator interface expected by ``altirra_bridge.analyzer``'s runtime
helpers (``sample_pc``, ``sample_portb``, ``sample_dli_chain``,
``diff_memory``, ``profile_guided_classify``).

The adapter is intentionally tiny: the analyzer only needs read
access to memory, register getters, and a "run one instruction" or
"run N frames" method. It does not need writes.

Usage::

    from altirra_bridge import AltirraBridge
    from altirra_bridge.analyzer_adapter import BridgeEmu
    from altirra_bridge import analyzer

    with AltirraBridge.from_token_file(tok) as b:
        emu = BridgeEmu(b)
        emu.snapshot_ram()          # cache a full RAM image
        hot = analyzer.sample_pc(emu, frames=200)

The ``snapshot_ram()`` call pulls the full 64 KB address space once
and caches it, so ``emu.mem[addr]`` is a cheap local read. Call
``snapshot_ram()`` again any time you need a fresh view.
"""

from __future__ import annotations


class _MemView:
    """Dict-like view over a cached bytes buffer, addressable 0x0000-0xFFFF."""

    __slots__ = ("_buf",)

    def __init__(self, buf: bytes):
        self._buf = buf

    def __getitem__(self, addr: int) -> int:
        return self._buf[addr & 0xFFFF]

    def __len__(self) -> int:
        return len(self._buf)

    def __bytes__(self) -> bytes:
        return bytes(self._buf)


class BridgeEmu:
    """Pretend-emulator shim over an AltirraBridge client.

    Satisfies the read/step interface that analyzer runtime helpers
    expect. All state is fetched lazily from the bridge; ``mem`` is
    backed by a cached snapshot that the caller refreshes as needed.
    """

    def __init__(self, bridge):
        self._b = bridge
        self._mem = _MemView(bytes(0x10000))
        self._regs_cache = None

    # ---- memory ------------------------------------------------------

    def snapshot_ram(self) -> None:
        """Refresh the full 64 KB memory cache from the bridge."""
        self._b.pause()
        self._mem = _MemView(self._b.memdump(0x0000, 0xFFFF))

    @property
    def mem(self) -> _MemView:
        return self._mem

    def peek(self, addr: int) -> int:
        return self._b.peek(addr, 1)[0]

    # ---- registers ---------------------------------------------------

    def _refresh_regs(self) -> None:
        self._regs_cache = self._b.regs()

    def _reg_hex(self, key: str) -> int:
        if self._regs_cache is None:
            self._refresh_regs()
        v = self._regs_cache[key]
        return int(v.lstrip("$"), 16)

    @property
    def pc(self) -> int:
        self._refresh_regs()
        return self._reg_hex("PC")

    @property
    def a(self) -> int: return self._reg_hex("A")

    @property
    def x(self) -> int: return self._reg_hex("X")

    @property
    def y(self) -> int: return self._reg_hex("Y")

    @property
    def s(self) -> int: return self._reg_hex("S")

    # ---- shadow-register shortcuts the analyzer asks about ----------

    @property
    def portb(self) -> int:
        return self._b.peek(0xD301, 1)[0]

    @property
    def vdslst(self) -> int:
        lo = self._b.peek(0x0200, 1)[0]
        hi = self._b.peek(0x0201, 1)[0]
        return lo | (hi << 8)

    # ---- stepping / running -----------------------------------------

    def step(self) -> None:
        """Execute a single 6502 instruction."""
        # Not all bridge versions expose a direct `step`; fall back
        # to a very short frame window if needed.
        stepper = getattr(self._b, "step", None)
        if callable(stepper):
            stepper()
        else:
            # Best-effort: run one full frame as a step surrogate.
            self._b.frame(1)

    def run_frames(self, n: int) -> None:
        self._b.frame(n)
