#!/usr/bin/env python3
"""Native HTTP streaming, without retaining the large response in either peer."""
import http.server
import json
import pathlib
import subprocess
import sys
import threading
import time
import zlib

LARGE = 80 * 1024 * 1024 + 37
CHUNK = b'Z' * 65536
compressor = zlib.compressobj(wbits=31)
compressed = []
for offset in range(0, LARGE, len(CHUNK)):
    compressed.append(compressor.compress(CHUNK[:min(len(CHUNK), LARGE - offset)]))
compressed.append(compressor.flush())
GZIP = b''.join(compressed)


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def do_GET(self):
        try:
            self.serve()
        except (BrokenPipeError, ConnectionResetError):
            pass
        finally:
            self.close_connection = True

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get('Content-Length', '0')))
        valid = ((self.headers.get('Content-Type') == 'application/json' and
                  json.loads(body) == {'path': '/AltirraSDL/game.xex', 'host_slot': 0, 'mode': 'r'}) or
                 (self.headers.get('Content-Type') == 'application/octet-stream' and body == b'POST\0DATA'))
        if valid and self.path in ('/post-303', '/post-307'):
            self.send_response(303 if self.path == '/post-303' else 307)
            self.send_header('Location', '/small' if self.path == '/post-303' else '/post')
            self.send_header('Content-Length', '0')
            self.end_headers()
            self.close_connection = True
            return
        response = b'{"status":"success"}' if valid else b'{"error":"bad request"}'
        self.send_response(400 if not valid or self.path == '/post-error' else 200)
        self.send_header('Content-Length', str(len(response)))
        self.send_header('Content-Type', 'application/json')
        self.end_headers()
        self.wfile.write(response)
        self.close_connection = True

    def serve(self):
        size = LARGE
        if self.path == '/error-stall':
            self.send_response(503)
            self.send_header('Content-Length', str(2 * 1024**3))
            self.end_headers()
            time.sleep(8)
            return
        if self.path == '/advertised':
            self.send_response(200)
            self.send_header('Content-Length', str(5 * 1024**3 + 17))
            self.end_headers()
            self.close_connection = True
            return
        if self.path in ('/small', '/truncated'):
            size = 4096
        if self.path == '/error':
            size = 2 * 1024 * 1024
        self.send_response(302 if self.path == '/redirect' else 404 if self.path == '/error' else 200)
        if self.path == '/redirect':
            self.send_header('Location', '/large')
            size = 1024 * 1024
        if self.path == '/gzip':
            self.send_header('Content-Encoding', 'gzip')
            self.send_header('Content-Length', str(len(GZIP)))
        elif self.path == '/chunked':
            self.send_header('Transfer-Encoding', 'chunked')
        else:
            self.send_header('Content-Length', str(size))
        self.end_headers()
        if self.path == '/gzip':
            self.wfile.write(GZIP)
            return
        if self.path == '/truncated':
            self.wfile.write(CHUNK[:32])
            self.close_connection = True
            return
        for offset in range(0, size, len(CHUNK)):
            chunk = CHUNK[:min(len(CHUNK), size - offset)]
            if self.path == '/chunked':
                self.wfile.write(f'{len(chunk):x}\r\n'.encode() + chunk + b'\r\n')
            else:
                self.wfile.write(chunk)
            if self.path == '/stall':
                self.wfile.flush()
                time.sleep(8)
        if self.path == '/chunked':
            self.wfile.write(b'0\r\n\r\n')


def main():
    executable = str(pathlib.Path(sys.argv[1]).resolve())
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f'http://127.0.0.1:{server.server_port}'

    def run(path, limit=0, mode='stream'):
        result = subprocess.run([executable, base + path, str(limit), mode],
                                capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, result.stdout + result.stderr
        fields = dict(line.partition(' ')[::2] for line in result.stdout.splitlines())
        return {key: value if key == 'ERROR' else int(value) for key, value in fields.items()}

    try:
        for path in ('/large', '/redirect', '/gzip', '/chunked'):
            result = run(path)
            assert result['STATUS'] == 200 and not result['ERROR'], result
            assert result['BUFFER'] == 0 and result['SINK_BYTES'] == LARGE, result
            assert result['RECEIVED'] == LARGE and result['SUM'] == LARGE * 90, result
            assert result['TOTAL'] == (0 if path in ('/gzip', '/chunked') else LARGE), result
        for mode in ('fail', 'throw'):
            result = run('/large', mode=mode)
            assert result['ERROR'] == 'download sink failed', result
            assert result['BUFFER'] == result['SINK_BYTES'] == 0, result
        result = run('/large', limit=64 * 1024 * 1024)
        assert result['ERROR'] == 'response too large' and result['SINK_BYTES'] == 0, result
        result = run('/advertised', limit=100)
        assert result['ERROR'] == 'response too large' and result['TOTAL'] > 2**32, result
        result = run('/gzip', limit=64 * 1024 * 1024)
        assert result['ERROR'] == 'response too large' and result['SINK_BYTES'] <= 64 * 1024 * 1024, result
        result = run('/error')
        assert result['STATUS'] == 404 and result['CALLS'] == result['BUFFER'] == 0, result
        start = time.monotonic()
        result = run('/error-stall')
        assert result['STATUS'] == 503 and result['CALLS'] == 0, result
        assert time.monotonic() - start < 2, 'streaming errors must not wait for an error-page body'
        result = run('/small', mode='memory')
        assert result['STATUS'] == 200 and result['BUFFER'] == 4096 and result['SUM'] == 4096 * 90, result
        result = run('/error', mode='memory')
        assert result['STATUS'] == 404 and result['BUFFER'] == 2 * 1024 * 1024, result
        for mode in ('post', 'post-memory', 'post-perform', 'post-binary'):
            result = run('/post', mode=mode)
            assert result['STATUS'] == 200 and not result['ERROR'], result
            assert result['SUM'] == sum(b'{"status":"success"}'), result
        result = run('/small', mode='get-forced')
        assert result['STATUS'] == 200 and result['SUM'] == 4096 * 90, result
        result = run('/post-303', mode='post')
        assert result['STATUS'] == 200 and result['SUM'] == 4096 * 90, result
        result = run('/post-307', mode='post')
        assert result['STATUS'] == 200 and result['SUM'] == sum(b'{"status":"success"}'), result
        result = run('/post-error', mode='post-memory')
        assert result['STATUS'] == 400 and result['BUFFER'] > 0, result
        result = run('/post-error', mode='post')
        assert result['STATUS'] == 400 and result['CALLS'] == 0, result
        result = run('/truncated')
        assert result['ERROR'] and result['BUFFER'] == 0, result
        result = run('/large', mode='cancel-in-sink')
        assert result['ERROR'] == 'cancelled' and result['SINK_BYTES'] < LARGE, result
        start = time.monotonic()
        result = run('/stall', mode='cancel')
        assert result['ERROR'] == 'cancelled' and time.monotonic() - start < 7.5, result
    finally:
        server.shutdown()
        server.server_close()
    print('HTTP streaming tests passed')


if __name__ == '__main__':
    main()
