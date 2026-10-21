#!/usr/bin/env python3
"""Deterministic library imports. --large also transfers a >4GiB ZIP64 pack."""
import concurrent.futures
import http.server
import gzip
import io
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time
import zipfile
import zlib

IMAGE = b'\xff\xff\x00\x20\x01\x20\xea\x60'


def archive(files):
    result = io.BytesIO()
    with zipfile.ZipFile(result, 'w', zipfile.ZIP_DEFLATED) as pack:
        for name, value in files:
            pack.writestr(name, value)
    return result.getvalue()


PACK = archive([('one.xex', IMAGE), ('directory/two.atr', b'\x96\x02' + b'\0' * 142)])
GIANT = bytearray(archive([('oversize.dat', b'notes'), ('game.xex', IMAGE)]))
struct.pack_into('<I', GIANT, GIANT.index(b'PK\x01\x02') + 24, 0xF0000000)
BOMB = bytearray(archive([('huge.xex', IMAGE)]))
struct.pack_into('<I', BOMB, BOMB.index(b'PK\x01\x02') + 24, 65 * 1024 * 1024)
BROKEN = PACK[:-30]
FIXTURES = {'/game%2Exex': IMAGE, '/game.xex': IMAGE, '/CON.xex': IMAGE, '/pack.zip': PACK,
            '/no-games.zip': archive([('notes.txt', b'text')]),
            '/compressed-games.zip': archive([('program.xex.gz', gzip.compress(IMAGE))]),
            '/nested-only.zip': archive([('nested.zip', PACK)]),
            '/music-only.zip': archive([('song.sap', b'SAP')]),
            '/html.xex': b'\xef\xbb\xbf <!-- CDN -->\n<!DOCTYPE HTML><html>link</html>',
            '/single.atz': gzip.compress(b'\x96\x02' + b'\0' * 142), '/single.xex.gz': gzip.compress(IMAGE), '/pack-with-giant.zip': bytes(GIANT), '/bomb.zip': bytes(BOMB),
            '/broken.zip': BROKEN, '/empty': b''}


def large_parts():
    # A real stored non-game member >4GiB, followed by one small game. Only
    # its zero-filled payload is streamed repeatedly; fixtures stay bounded.
    size = (1 << 32) + 4096
    block = b'\0' * (1024 * 1024)
    crc = 0
    for offset in range(0, size, len(block)):
        crc = zlib.crc32(block[:min(len(block), size - offset)], crc)
    name = b'padding.dat'
    extra = struct.pack('<HHQQ', 1, 16, size, size)
    header = struct.pack('<IHHHHHIIIHH', 0x04034B50, 45, 0, 0, 0, 0, crc,
                         0xFFFFFFFF, 0xFFFFFFFF, len(name), len(extra)) + name + extra
    game_name = b'game.xex'
    game_crc = zlib.crc32(IMAGE)
    game_offset = len(header) + size
    game = struct.pack('<IHHHHHIIIHH', 0x04034B50, 20, 0, 0, 0, 0, game_crc,
                       len(IMAGE), len(IMAGE), len(game_name), 0) + game_name + IMAGE
    def entry(filename, checksum, length, offset, size64=False):
        values = struct.pack('<QQQ', length, length, offset) if size64 else struct.pack('<Q', offset)
        ext = struct.pack('<HH', 1, len(values)) + values
        return struct.pack('<IHHHHHHIIIHHHHHII', 0x02014B50, 45, 45, 0, 0, 0, 0,
                           checksum, 0xFFFFFFFF if size64 else length,
                           0xFFFFFFFF if size64 else length, len(filename), len(ext),
                           0, 0, 0, 0, 0xFFFFFFFF) + filename + ext
    directory = entry(name, crc, size, 0, True) + entry(game_name, game_crc, len(IMAGE), game_offset)
    directory_offset = game_offset + len(game)
    record_offset = directory_offset + len(directory)
    record = struct.pack('<IQHHIIQQQQ', 0x06064B50, 44, 45, 45, 0, 0, 2, 2,
                         len(directory), directory_offset)
    locator = struct.pack('<IIQI', 0x07064B50, 0, record_offset, 1)
    end = struct.pack('<IHHHHIIH', 0x06054B50, 0, 0, 0xFFFF, 0xFFFF,
                      0xFFFFFFFF, 0xFFFFFFFF, 0)
    return header, size, block, game + directory + record + locator + end


LARGE = None


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass
    def do_GET(self):
        if self.path == '/redirect':
            self.send_response(302)
            self.send_header('Location', '/pack.zip')
            self.end_headers()
            self.wfile.write(b'non-image redirect body')
            return
        if self.path == '/large.zip':
            header, size, block, tail = LARGE
            self.send_response(200)
            self.send_header('Content-Length', str(len(header) + size + len(tail)))
            self.end_headers()
            self.wfile.write(header)
            for offset in range(0, size, len(block)):
                self.wfile.write(block[:min(len(block), size - offset)])
            self.wfile.write(tail)
            return
        if self.path == '/slow.zip':
            self.send_response(200)
            self.send_header('Content-Length', str(1024 * 1024))
            self.end_headers()
            try:
                for _ in range(1024):
                    self.wfile.write(b'PK' + b'\0' * 1022)
                    self.wfile.flush()
                    time.sleep(.005)
            except (BrokenPipeError, ConnectionResetError):
                pass
            return
        if self.path == '/error.zip':
            self.send_response(404)
            self.send_header('Content-Length', '4')
            self.end_headers()
            self.wfile.write(b'nope')
            return
        body = FIXTURES.get(self.path)
        self.send_response(200 if body is not None else 404)
        self.send_header('Content-Length', str(len(body or b'')))
        self.end_headers()
        self.wfile.write(body or b'')


def run(binary, root, url, okay=True, cancel=None, timeout=30, check_staging=True):
    args = [binary, url, str(root)]
    if cancel is not None:
        args.append(str(cancel))
    response = subprocess.run(args, capture_output=True, text=True, timeout=timeout)
    assert (response.returncode == 0) is okay, response.stdout + response.stderr
    if check_staging:
        assert not list(root.glob('.library-download-*')), 'staging leaked'
    fields = {}
    for line in response.stdout.splitlines():
        key, _, value = line.partition(' ')
        fields[key] = value
    if okay:
        assert Path(fields['PATH']).is_file()
    return fields


def main():
    global LARGE
    binary = sys.argv[1]
    large = '--large' in sys.argv
    if large:
        LARGE = large_parts()
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    server.daemon_threads = True
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    base = f'http://127.0.0.1:{server.server_port}'
    # Keep the optional large transfer on the checkout's disk, not /tmp tmpfs.
    root = Path(tempfile.mkdtemp(prefix='altirra-library-url-', dir=os.environ.get('ALTIRRA_LIBRARY_TEST_TMP')))
    try:
        cache = root / 'cache with spaces'
        cache.mkdir()
        game = run(binary, cache, base + '/game%2Exex')
        assert game['ARCHIVE'] == '0' and Path(game['PATH']).read_bytes() == IMAGE
        reserved = run(binary, cache, base + '/CON.xex')
        assert Path(reserved['PATH']).name == 'download-CON.xex'
        assert Path(reserved['PATH']).read_bytes() == IMAGE
        pack = run(binary, cache, base + '/pack.zip')
        assert pack['ARCHIVE'] == '1' and pack['COUNT'] == '2' and pack['GAME_BYTES'] == '8'
        assert Path(pack['PATH']).read_bytes() == PACK
        # Identical imports reuse the durable path and preserve mounted edits.
        again = run(binary, cache, base + '/pack.zip')
        assert again['PATH'] == pack['PATH']
        with concurrent.futures.ThreadPoolExecutor(2) as pool:
            parallel = list(pool.map(lambda _: run(binary, cache, base + '/pack.zip',
                                                  check_staging=False), range(2)))
        # A completed import can overlap its peer's legitimate staging file.
        # Check shared cleanup only after both processes have exited.
        assert not list(cache.glob('.library-download-*')), 'staging leaked'
        assert all(item['PATH'] == pack['PATH'] for item in parallel)
        assert run(binary, cache, base + '/redirect')['COUNT'] == '2'
        assert run(binary, cache, base + '/single.atz')['ARCHIVE'] == '0'
        assert run(binary, cache, base + '/single.xex.gz')['ARCHIVE'] == '0'
        assert run(binary, cache, base + '/compressed-games.zip')['COUNT'] == '1'
        assert run(binary, cache, base + '/pack-with-giant.zip')['COUNT'] == '1'
        for endpoint in ('/no-games.zip', '/nested-only.zip', '/music-only.zip', '/html.xex', '/bomb.zip', '/broken.zip', '/empty', '/error.zip'):
            run(binary, cache, base + endpoint, False)
        run(binary, cache, base + '/slow.zip', False, cancel=100)
        if large:
            before = time.monotonic()
            imported = run(binary, cache, base + '/large.zip', timeout=600)
            assert int(imported['BYTES']) > 1 << 32 and imported['COUNT'] == '1' and imported['GAME_BYTES'] == '8'
            assert Path(imported['PATH']).stat().st_size == int(imported['BYTES'])
            print(f"LARGE_BYTES {imported['BYTES']} elapsed={time.monotonic() - before:.2f}s", flush=True)
        print('Library streamed URL import tests passed', flush=True)
    finally:
        server.shutdown()
        server.server_close()
        shutil.rmtree(root)


if __name__ == '__main__':
    main()
