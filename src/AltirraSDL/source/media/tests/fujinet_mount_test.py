#!/usr/bin/env python3
"""Managed private-SD publication and official FujiNet REST mount requests."""
import http.server
import json
import pathlib
import struct
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse

XEX = b'\xff\xff\x00\x20\x01\x20\xea\x60'
ATR = struct.pack('<HHHH8s', 0x0296, 720 * 8, 128, 0, bytes(8)) + bytes(720 * 128)


class Handler(http.server.BaseHTTPRequestHandler):
    mode = ''
    posts = []
    mounted = None
    sd_root = None

    def log_message(self, *args):
        pass

    def reply(self, status, value):
        data = json.dumps(value).encode()
        self.send_response(status)
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        try:
            self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == '/download':
            query = urllib.parse.parse_qs(parsed.query)
            if Handler.mode == 'foreign-web':
                data = b'another runtime has another SD card'
            elif Handler.mode == 'no-download':
                self.reply(404, {'error': 'unsupported'})
                return
            else:
                assert query['hostslot'] == ['2']
                data = (Handler.sd_root / query['filename'][0].lstrip('/')).read_bytes()
            self.send_response(200)
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        elif self.path == '/game.xex':
            self.send_response(200)
            self.send_header('Content-Length', str(len(XEX)))
            self.end_headers()
            self.wfile.write(XEX)
        elif self.path == '/api/v1/hosts':
            mode = Handler.mode
            if mode == 'no-api':
                self.reply(404, {'detail': 'Not Found'})
                return
            if mode == 'cancel-host':
                time.sleep(.4)
            self.reply(200, [{'slot': 1, 'type': 'TNFS', 'configured': True},
                             {'slot': 3, 'type': 'TNFS' if mode == 'no-sd' else 'SD', 'configured': True}])
        elif self.path == '/api/v1/drives/1':
            body = dict(Handler.mounted)
            body['mounted'] = True
            if Handler.mode == 'mismatch':
                body['path'] = '/another image.xex'
            self.reply(200, body)
        else:
            self.reply(404, {'error': 'missing'})

    def do_POST(self):
        mode = Handler.mode
        size = int(self.headers.get('Content-Length', '0'))
        body = json.loads(self.rfile.read(size))
        Handler.posts.append((self.path, body, self.headers.get('Content-Type')))
        Handler.mounted = body
        if mode == 'cancel-post':
            time.sleep(.4)
        self.reply(400 if mode == 'post-error' else 200, {'status': 'success'})


def main():
    executable = str(pathlib.Path(sys.argv[1]).resolve())
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        with tempfile.TemporaryDirectory(prefix='altirra managed mount ') as td:
            root = pathlib.Path(td)
            state = root / 'private state'
            (state / 'SD').mkdir(parents=True)
            Handler.sd_root = state / 'SD'
            sentinel = state / 'SD' / 'keep-user-file.txt'
            sentinel.write_text('unchanged')
            # Literal quotes are valid POSIX names but forbidden by Windows.
            # Keep spaces on both platforms and quote coverage where valid.
            xex = root / ('selected game.xex' if sys.platform == 'win32'
                          else 'selected "game".xex')
            xex.write_bytes(XEX)
            atr = root / 'game.atr'
            atr.write_bytes(ATR)

            def run(source=xex, mode=''):
                Handler.mode, Handler.posts = mode, []
                result = subprocess.run([executable, str(source), str(state),
                                         str(server.server_port), mode],
                                        capture_output=True, text=True, timeout=15)
                assert result.returncode == 0, result.stdout + result.stderr
                fields = dict(line.partition(' ')[::2] for line in result.stdout.splitlines())
                assert sentinel.read_text() == 'unchanged'
                assert not list((state / 'SD').rglob('.preparing-*'))
                assert not list((state / 'SD').rglob('.owner-*'))
                return fields

            first = run()
            assert first['MOUNTED'] == first['ATTEMPTED'] == '1' and not first['ERROR'], first
            assert first['HOST'] == '2', first  # SD is slot 3, never assume host 0.
            missing = run(mode='no-api')
            assert missing['ATTEMPTED'] == '0' and not Handler.posts, missing
            assert 'HTTP 404' in missing['ERROR'] and '/api/v1/hosts' in missing['ERROR'], missing
            assert 'Repair automatic setup' in missing['ERROR'] and 'another program' in missing['ERROR'], missing
            first = run()
            path, body, content_type = Handler.posts[0]
            assert path == '/api/v1/drives/1/mount' and content_type == 'application/json'
            assert body == {'path': first['PATH'], 'host_slot': 2, 'mode': 'r'}
            cached = state / 'SD' / first['PATH'].lstrip('/')
            assert cached.read_bytes() == XEX and xex.read_bytes() == XEX
            assert run()['PATH'] == first['PATH']
            cached.write_bytes(b'user edited mounted file')
            fresh = run()
            assert fresh['MOUNTED'] == '1' and fresh['PATH'] != first['PATH'], fresh
            assert cached.read_bytes() == b'user edited mounted file'
            assert (state / 'SD' / fresh['PATH'].lstrip('/')).read_bytes() == XEX
            assert run(atr)['MOUNTED'] == '1'
            assert run(f'http://127.0.0.1:{server.server_port}/game.xex')['MOUNTED'] == '1'
            invalid = root / 'broken.atr'
            invalid.write_bytes(b'\x96\x02broken')
            result = run(invalid)
            assert result['MOUNTED'] == result['ATTEMPTED'] == '0' and result['ERROR'], result
            assert not Handler.posts
            oversized = root / 'too large.xex'
            with oversized.open('wb') as stream:
                stream.seek(65 * 1024 * 1024)
                stream.write(b'!')
            result = run(oversized)
            assert '64 MiB' in result['ERROR'] and result['ATTEMPTED'] == '0', result
            for mode in ('no-sd', 'pre-cancel', 'cancel-host', 'foreign-web', 'no-download'):
                result = run(mode=mode)
                assert result['MOUNTED'] == result['ATTEMPTED'] == '0' and result['ERROR'], result
                assert not Handler.posts
            for mode in ('post-error', 'mismatch', 'cancel-post'):
                result = run(mode=mode)
                assert result['MOUNTED'] == '0' and result['ATTEMPTED'] == '1', result
                assert 'may already have changed D1' in result['ERROR'], result
    finally:
        server.shutdown()
        server.server_close()
    print('FujiNet mount helper tests passed')


if __name__ == '__main__':
    main()
