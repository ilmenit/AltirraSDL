#!/usr/bin/env python3
"""Exercise the native media importer against deterministic HTTP fixtures."""
import gzip
import concurrent.futures
import http.server
import io
import pathlib
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import zipfile

IMAGE = b'\xff\xff\x00\x20\x01\x20\xea\x60'


def archive(name, data):
    result = io.BytesIO()
    with zipfile.ZipFile(result, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr(name, data)
    return result.getvalue()


ZIP = archive('../game.xex', IMAGE)
BOMB = bytearray(archive('game.xex', IMAGE))
# Central directory advertises an oversized member; reject before allocation.
struct.pack_into('<I', BOMB, BOMB.index(b'PK\x01\x02') + 24, 65 * 1024 * 1024)
CORRUPT = bytearray(archive('game.xex', IMAGE))
struct.pack_into('<I', CORRUPT, CORRUPT.index(b'PK\x01\x02') + 16, 0)
CORRUPT_GZIP = bytearray(gzip.compress(IMAGE))
CORRUPT_GZIP[-8] ^= 0x80


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path == '/stalled.xex':
            self.send_response(200)
            self.send_header('Content-Length', str(len(IMAGE)))
            self.end_headers()
            time.sleep(8)
            try:
                self.wfile.write(IMAGE)
            except (BrokenPipeError, ConnectionResetError):
                pass
            return
        if self.path == '/redirect':
            self.send_response(302)
            self.send_header('Location', '/game.xex')
            self.end_headers()
            return
        if self.path == '/file-redirect':
            self.send_response(302)
            self.send_header('Location', 'file:///etc/passwd')
            self.end_headers()
            return
        if self.path == '/loop':
            self.send_response(302)
            self.send_header('Location', '/loop')
            self.end_headers()
            return
        fixtures = {'/game.xex': IMAGE, '/race.xex': IMAGE, '/collection.zip': ZIP,
                    '/no-media.zip': archive('readme.txt', b'No Atari image'),
                    '/bomb.zip': bytes(BOMB), '/corrupt.zip': bytes(CORRUPT),
                    '/game.xex.gz': gzip.compress(IMAGE),
                    '/corrupt.xex.gz': bytes(CORRUPT_GZIP),
                    '/bad.gz': b'\x1f\x8b' + b'broken gzip',
                    '/short.gz': gzip.compress(IMAGE)[:10],
                    '/empty': b'', '/html': b'<html>download page</html>'}
        if self.path == '/slow.xex':
            self.send_response(200)
            self.send_header('Content-Length', '65536')
            self.end_headers()
            try:
                for _ in range(64):
                    self.wfile.write(b'x' * 1024)
                    self.wfile.flush()
                    time.sleep(.05)
            except (BrokenPipeError, ConnectionResetError):
                pass
            return
        if self.path == '/oversize':
            self.send_response(200)
            self.send_header('Content-Length', str(65 * 1024 * 1024))
            self.end_headers()
            try:
                for _ in range(65):
                    self.wfile.write(b'x' * (1024 * 1024))
            except (BrokenPipeError, ConnectionResetError):
                pass
            return
        if self.path not in fixtures:
            self.send_error(404)
            return
        body = fixtures[self.path]
        self.send_response(200)
        self.send_header('Content-Type', 'text/html' if self.path == '/html'
                         else 'application/octet-stream')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main():
    executable = str(pathlib.Path(sys.argv[1]).resolve())
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = 'http://127.0.0.1:' + str(server.server_port)
    with tempfile.TemporaryDirectory(prefix='altirra media URL ') as td:
        cache = pathlib.Path(td) / 'downloads'

        def run(path, error=None, delay=None):
            url = path if '://' in path else base + path
            args = [executable, url, str(cache)]
            if delay is not None:
                args.append(str(delay))
            process = subprocess.run(args, capture_output=True, text=True, timeout=15)
            if error is not None:
                assert process.returncode == 1, process.stdout + process.stderr
                assert error.lower() in process.stdout.lower(), process.stdout
                return
            assert process.returncode == 0, process.stdout + process.stderr
            result = pathlib.Path(process.stdout.splitlines()[0][5:])
            assert result.is_file(), process.stdout
            return result

        direct = run('/game.xex')
        assert direct.read_bytes() == IMAGE
        assert run('/game.xex') == direct

        # Concurrent application instances publish complete cache files;
        # subsequent downloads also preserve a file mounted for writing.
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            raced = list(pool.map(lambda _: run('/race.xex'), range(16)))
            assert all(path == raced[0] for path in raced)
            assert raced[0].read_bytes() == IMAGE
        direct.write_bytes(b'user-modified cache image')
        assert run('/game.xex').read_bytes() == b'user-modified cache image'
        assert run('/redirect').read_bytes() == IMAGE
        assert run('/collection.zip').read_bytes() == ZIP
        assert not (pathlib.Path(td) / 'game.xex').exists()  # no extraction
        assert gzip.decompress(run('/game.xex.gz').read_bytes()) == IMAGE
        run('/missing', 'HTTP 404')
        run('/empty', 'empty')
        run('/html', 'web page')
        run('/bomb.zip', 'exceeds')
        run('/no-media.zip', 'no recognized')
        run('/corrupt.zip', 'CRC')
        run('/bad.gz', 'ERROR')
        run('/short.gz', 'truncated')
        run('/corrupt.xex.gz', 'CRC')
        run('/oversize', 'too large')
        run('/file-redirect', 'ERROR')
        run('/loop', 'ERROR')
        run('file:///etc/passwd', 'HTTP or HTTPS')
        run('/slow.xex', 'cancelled', 120)
        start = time.monotonic()
        run('/stalled.xex', 'cancelled', 120)
        assert time.monotonic() - start < 7.5, 'cancelling stalled I/O must be bounded'
        assert not list(cache.rglob('*.part'))
        assert not list(cache.glob('.download-*'))
    server.shutdown()
    server.server_close()
    print('Media URL HTTP integration tests passed')


if __name__ == '__main__':
    main()
