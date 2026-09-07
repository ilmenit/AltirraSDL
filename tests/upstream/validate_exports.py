"""Validate encoder output independently of Altirra's decoder.

PNG checks use only Python's standard library. PDF checks run when Poppler
is installed and compare the selectable character with raw-dot output.
"""
from pathlib import Path
import argparse
import shutil
import struct
import subprocess
import zlib

parser = argparse.ArgumentParser()
parser.add_argument("--require-pdf", action="store_true")
parser.add_argument("--pdftoppm", default="pdftoppm")
parser.add_argument("--pdftotext", default="pdftotext")
args = parser.parse_args()


def check_png(path):
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos, compressed = 8, b""
    while pos < len(data):
        length = struct.unpack_from(">I", data, pos)[0]
        kind = data[pos + 4:pos + 8]
        payload = data[pos + 8:pos + 8 + length]
        crc = struct.unpack_from(">I", data, pos + 8 + length)[0]
        assert zlib.crc32(kind + payload) == crc
        if kind == b"IDAT":
            compressed += payload
        pos += length + 12
    raw = zlib.decompress(compressed)
    assert len(raw) == 9 * 52
    previous = [0] * 51
    for y in range(9):
        filter_type = raw[y * 52]
        row = list(raw[y * 52 + 1:(y + 1) * 52])
        for x in range(51):
            a = row[x - 3] if x >= 3 else 0
            b = previous[x]
            c = previous[x - 3] if x >= 3 else 0
            p = a + b - c
            distances = [abs(p - v) for v in (a, b, c)]
            paeth = (a, b, c)[distances.index(min(distances))]
            row[x] = (row[x] + (0, a, b, (a + b) // 2, paeth)[filter_type]) & 255
            source_x = (x // 3) * 3 + 2 - x % 3
            assert row[x] == (source_x * 7 + y * 13) & 255
        previous = row
    print(f"{path}: chunk CRCs, zlib stream, and every decoded pixel passed")


def pdf_bounds(path):
    # Crop includes the test character near the top-left printable margin.
    raw = subprocess.check_output([
        args.pdftoppm, "-f", "1", "-singlefile", "-r", "600", "-gray",
        "-x", "100", "-y", "200", "-W", "200", "-H", "180", str(path)])
    magic, width, height, maximum, pixels = raw.split(maxsplit=4)
    assert magic == b"P5" and maximum == b"255"
    width, height = int(width), int(height)
    assert len(pixels) == width * height
    ink = [(i % width, i // width) for i, v in enumerate(pixels) if v < 128]
    assert ink, f"Empty PDF render: {path}"
    return (min(x for x, y in ink), min(y for x, y in ink),
            max(x for x, y in ink), max(y for x, y in ink))


for name in ("filtered.png", "quick.png"):
    check_png(Path(name))
if shutil.which(args.pdftoppm) and shutil.which(args.pdftotext):
    tracked, raw = pdf_bounds(Path("tracked.pdf")), pdf_bounds(Path("raw.pdf"))
    assert all(abs(a - b) <= 1 for a, b in zip(tracked, raw)), (tracked, raw)
    text = subprocess.check_output([args.pdftotext, "tracked.pdf", "-"]).decode().strip()
    assert text == "A", text
    print("PDF dot/character alignment and selectable text passed")
else:
    if args.require_pdf:
        raise SystemExit("Required PDF validation unavailable: install Poppler")
    print("PDF rendering check skipped: install Poppler to enable it")
