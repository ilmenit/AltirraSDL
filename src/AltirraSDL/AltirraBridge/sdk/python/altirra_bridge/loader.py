"""Atari XEX binary loader.

Pure-Python parser for the Atari 8-bit executable file format. No
bridge dependency — this is local-only file parsing. The bridge has
its own XEX boot path (``BOOT path``); this module is for *inspecting*
an XEX without booting it (e.g. to find the entry point before
deciding what to do with it, or to populate a project's segment list
for an RE workflow).

XEX format
----------

An Atari executable is a sequence of *segments*. Each segment has a
2-byte little-endian load address range followed by ``end - start +
1`` bytes of data. The first segment is preceded by an optional
``$ff $ff`` magic header — present in 99% of files but not strictly
required by DOS.

Two address ranges have special meaning:

* ``$02e0 / $02e1`` (``RUNAD``) — the run-address vector. After all
  segments are loaded, DOS jumps to the address stored at this
  location. A file may set this multiple times; the last value wins.
* ``$02e2 / $02e3`` (``INITAD``) — the init-address vector. After
  *each* segment that writes to this location, DOS jumps to the
  address stored there before continuing to load the next segment.
  This lets a file run setup code mid-load.

This loader extracts both. It also returns the literal segment list
so a caller can replay the load order, mount segments into the bridge
via :meth:`AltirraBridge.memload`, or compute symbol candidates by
identifying the .text and .data ranges.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List, Optional


RUNAD  = 0x02e0
INITAD = 0x02e2


@dataclass
class XexSegment:
    """One segment of an XEX file.

    ``start`` and ``end`` are inclusive load addresses. ``data`` has
    length ``end - start + 1``. The segment is exactly the bytes that
    DOS would copy into memory at ``[start, end]``.
    """

    start: int
    end:   int
    data:  bytes

    def __len__(self) -> int:
        return len(self.data)

    @property
    def length(self) -> int:
        return self.end - self.start + 1

    def covers(self, addr: int) -> bool:
        return self.start <= addr <= self.end


@dataclass
class XexImage:
    """A parsed XEX file.

    ``segments`` is the literal segment list in load order. ``runad``
    is the value of the last write to RUNAD ($02e0/$02e1), or ``None``
    if no segment touched it. ``initads`` is the *list* of values
    written to INITAD ($02e2/$02e3), in load order — every entry was
    a separate jump-during-load.
    """

    segments: List[XexSegment] = field(default_factory=list)
    runad:    Optional[int]    = None
    initads:  List[int]        = field(default_factory=list)

    @property
    def entry(self) -> Optional[int]:
        """Convenience: the run address (``runad``)."""
        return self.runad

    def segment_at(self, addr: int) -> Optional[XexSegment]:
        """Return the segment that covers ``addr``, or ``None``."""
        for s in self.segments:
            if s.covers(addr):
                return s
        return None

    def total_bytes(self) -> int:
        """Total payload byte count across all segments."""
        return sum(len(s) for s in self.segments)


def parse_xex(data: bytes) -> XexImage:
    """Parse an XEX byte stream and return a :class:`XexImage`.

    Raises :class:`ValueError` if the file is malformed.
    """
    if not isinstance(data, (bytes, bytearray)):
        raise TypeError("parse_xex expects bytes")

    img = XexImage()
    pos = 0
    n = len(data)
    if n < 4:
        raise ValueError("XEX too short")

    # Optional $ff $ff magic — strip it once at the start.
    if data[pos] == 0xff and data[pos + 1] == 0xff:
        pos += 2

    while pos < n:
        if n - pos < 4:
            raise ValueError(f"XEX truncated at offset {pos}")

        start = data[pos] | (data[pos + 1] << 8)
        end   = data[pos + 2] | (data[pos + 3] << 8)
        pos += 4

        # An $ff $ff sentinel between segments is allowed and skipped.
        if start == 0xffff and end == 0xffff:
            continue

        if end < start:
            raise ValueError(
                f"XEX segment with end < start at offset {pos - 4}: "
                f"${start:04x}-${end:04x}")

        length = end - start + 1
        if pos + length > n:
            raise ValueError(
                f"XEX segment ${start:04x}-${end:04x} extends past EOF "
                f"({length} bytes needed, {n - pos} available)")

        seg_data = bytes(data[pos:pos + length])
        pos += length

        img.segments.append(XexSegment(start=start, end=end, data=seg_data))

        # Check for INITAD writes inside this segment.
        if start <= INITAD and end >= INITAD + 1:
            off = INITAD - start
            img.initads.append(seg_data[off] | (seg_data[off + 1] << 8))

        # Check for RUNAD writes; last one wins.
        if start <= RUNAD and end >= RUNAD + 1:
            off = RUNAD - start
            img.runad = seg_data[off] | (seg_data[off + 1] << 8)

    return img


def load_xex(path: str) -> XexImage:
    """Convenience: read an XEX file from disk and parse it."""
    with open(path, "rb") as f:
        return parse_xex(f.read())
