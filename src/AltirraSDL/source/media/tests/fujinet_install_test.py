#!/usr/bin/env python3
"""One-click FujiNet-PC install against a local mock of GitHub's release API."""
import hashlib
import http.server
import io
import json
import os
import pathlib
import stat
import subprocess
import sys
import tarfile
import tempfile
import threading
import zipfile

# The macOS builds embed Homebrew library paths; a formula that cannot exist
# exercises the missing-library report on every platform.
PROGRAM = b'\x7fELF fixture\0/opt/homebrew/opt/openssl@99/lib/libcrypto.99.dylib\0rest'


def add(archive, name, data=b'', mode=0o644, kind=tarfile.REGTYPE, link=''):
    info = tarfile.TarInfo(name)
    info.type = kind
    info.mode = mode
    info.linkname = link
    info.size = len(data) if kind == tarfile.REGTYPE else 0
    archive.addfile(info, io.BytesIO(data) if kind == tarfile.REGTYPE else None)


def release_tar(fmt=tarfile.GNU_FORMAT):
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode='w:gz', format=fmt) as archive:
        add(archive, 'fujinet-pc-ATARI', kind=tarfile.DIRTYPE, mode=0o755)
        add(archive, 'fujinet-pc-ATARI/fujinet', PROGRAM, 0o755)
        add(archive, 'fujinet-pc-ATARI/run-fujinet', b'#!/bin/sh\n', 0o755)
        add(archive, 'fujinet-pc-ATARI/fnconfig.ini', b'[BOIP]\nenabled=1\n')
        add(archive, 'fujinet-pc-ATARI/data/' + 'long-name-' * 12 + '.bin', b'long')
        add(archive, 'fujinet-pc-ATARI/SD/--- FujiNet SD Card ---')
        # Links are never needed and must not escape the destination.
        add(archive, 'fujinet-pc-ATARI/escape', kind=tarfile.SYMTYPE, link='/etc/passwd')
    return buffer.getvalue()


def release_zip():
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, 'w', zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('fujinet-pc-ATARI/', b'')
        archive.writestr('fujinet-pc-ATARI/fujinet.exe', b'MZ' + bytes(4096))
        archive.writestr('fujinet-pc-ATARI/data/850relocator.bin', b'data')
    return buffer.getvalue()


def evil_tar(name):
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode='w:gz', format=tarfile.GNU_FORMAT) as archive:
        add(archive, name, b'owned')
    return buffer.getvalue()


FILES = {
    'fujinet-pc-ATARI_v9.9-dev_ubuntu-24.04-amd64.tar.gz': release_tar(),
    'fujinet-pc-ATARI_v9.9-dev_ubuntu-22.04-amd64.tar.gz': release_tar(tarfile.PAX_FORMAT),
    'fujinet-pc-ATARI_v9.9-dev_windows-x64.zip': release_zip(),
    'fujinet-pc-ATARI_v9.9-dev_macos-15-x64.tar.gz': release_tar(),
}
# Newer draft with a matching name is ignored; device firmware never matches.
DRAFT = 'fujinet-pc-ATARI_v10-draft_ubuntu-24.04-amd64.tar.gz'


class Handler(http.server.BaseHTTPRequestHandler):
    downloads = []
    bad_digest = False
    # Number of upcoming file requests that drop the connection mid-body.
    drops = 0

    def log_message(self, *args):
        pass

    def send(self, status, data, kind='application/octet-stream'):
        self.send_response(status)
        self.send_header('Content-Type', kind)
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        base = f'http://127.0.0.1:{self.server.server_port}'
        if self.path == '/releases':
            def asset(index, name, data):
                digest = hashlib.sha256(data).hexdigest()
                if Handler.bad_digest:
                    digest = '0' * 64
                # GitHub redirects downloads to its file host; ATHttp follows it.
                return {'id': 1000 + index, 'name': name, 'size': len(data),
                        'digest': 'sha256:' + digest,
                        'browser_download_url': f'{base}/download/{name}'}
            releases = [
                {'name': 'Draft', 'draft': True, 'assets': [asset(0, DRAFT, b'')]},
                {'name': 'FujiNet-PC Nightly', 'draft': False, 'prerelease': True,
                 'assets': [asset(i + 1, n, d) for i, (n, d) in enumerate(FILES.items())]},
                {'name': 'v1.6.1', 'draft': False,
                 'assets': [asset(9, 'fujinet-ATARI-v1.6.1.zip', b'firmware')]},
            ]
            self.send(200, json.dumps(releases).encode(), 'application/json')
        elif self.path.startswith('/download/'):
            self.send_response(302)
            self.send_header('Location', '/files/' + self.path[len('/download/'):])
            self.send_header('Content-Length', '0')
            self.end_headers()
        elif self.path.startswith('/files/'):
            name = self.path[len('/files/'):]
            Handler.downloads.append(name)
            if Handler.drops:
                Handler.drops -= 1
                data = FILES[name]
                self.send_response(200)
                self.send_header('Content-Length', str(len(data)))
                self.end_headers()
                self.wfile.write(data[:len(data) // 3])
                self.close_connection = True
                return
            self.send(200, FILES[name])
        else:
            self.send(404, b'missing')


def run(binary, *args):
    output = subprocess.run([binary, *args], check=True, capture_output=True,
                            text=True, timeout=30).stdout
    result = {'MISSING': []}
    for line in output.splitlines():
        key, _, value = line.partition(' ')
        if key == 'MISSING':
            result['MISSING'].append(value)
        else:
            result[key] = value
    return result


def main():
    binary = sys.argv[1]
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f'http://127.0.0.1:{server.server_port}/releases'
    with tempfile.TemporaryDirectory(prefix='altirra fujinet install ') as temp:
        root = pathlib.Path(temp) / 'install root'
        # Newest non-draft release, first usable suffix, redirect followed.
        result = run(binary, 'install', url, str(root), 'ubuntu-24.04-amd64.tar.gz,ubuntu-22.04-amd64.tar.gz')
        assert not result['ERROR'], result
        program = pathlib.Path(result['EXECUTABLE'])
        assert program.name == 'fujinet' and program.read_bytes() == PROGRAM
        assert result['VERSION'] == 'fujinet-pc-ATARI_v9.9-dev_ubuntu-24.04-amd64'
        assert program.parent.parent.parent == root
        assert (program.parent / 'data').is_dir()
        assert len(list((program.parent / 'data').iterdir())[0].name) > 100
        assert not (program.parent / 'escape').exists()
        if os.name != 'nt':
            assert program.stat().st_mode & stat.S_IXUSR
            assert not (program.parent / 'fnconfig.ini').stat().st_mode & stat.S_IXUSR
        assert result['MISSING'] == [
            'openssl@99 /opt/homebrew/opt/openssl@99/lib/libcrypto.99.dylib'], result
        assert 'openssl@99' in result['COMMAND']
        assert not [p for p in root.iterdir() if p.name.startswith('.')]
        # A complete install of the same asset is reused without downloading.
        Handler.downloads.clear()
        again = run(binary, 'install', url, str(root), 'ubuntu-24.04-amd64.tar.gz')
        assert again['EXECUTABLE'] == result['EXECUTABLE'] and not Handler.downloads
        # Preference order applies within the newest release; pax headers parse.
        pax = run(binary, 'install', url, str(root), 'ubuntu-22.04-amd64.tar.gz')
        assert not pax['ERROR'] and pax['VERSION'].endswith('ubuntu-22.04-amd64'), pax
        # Updating replaces the previous private install.
        assert not program.exists() and pathlib.Path(pax['EXECUTABLE']).is_file()
        # A dropped transfer is fetched again; three failures give up cleanly.
        Handler.drops = 1
        Handler.downloads.clear()
        retried = run(binary, 'install', url, str(root / 'retry'), 'macos-15-x64.tar.gz')
        assert not retried['ERROR'] and len(Handler.downloads) == 2, (retried, Handler.downloads)
        Handler.drops = 3
        failed = run(binary, 'install', url, str(root / 'dropped'), 'macos-15-x64.tar.gz')
        Handler.drops = 0
        assert 'try again' in failed['ERROR'] and not failed['EXECUTABLE'], failed
        assert not [p for p in (root / 'dropped').iterdir()]
        # Firmware and other machines never match.
        none = run(binary, 'install', url, str(root / 'none'), 'macos-14-arm64.tar.gz')
        assert 'no FujiNet-PC build' in none['ERROR'] and not none['EXECUTABLE'], none
        # A checksum mismatch publishes nothing.
        Handler.bad_digest = True
        bad = run(binary, 'install', url, str(root / 'bad'), 'macos-15-x64.tar.gz')
        Handler.bad_digest = False
        assert 'checksum' in bad['ERROR'] and not bad['EXECUTABLE'], bad
        assert not [p for p in (root / 'bad').iterdir()]
        # ZIP (Windows) extraction.
        archive = pathlib.Path(temp) / 'release.zip'
        archive.write_bytes(FILES['fujinet-pc-ATARI_v9.9-dev_windows-x64.zip'])
        target = pathlib.Path(temp) / 'zip out'
        target.mkdir()
        assert not run(binary, 'extract', str(archive), str(target))['ERROR']
        assert (target / 'fujinet-pc-ATARI' / 'fujinet.exe').stat().st_size == 4098
        assert (target / 'fujinet-pc-ATARI' / 'data' / '850relocator.bin').read_bytes() == b'data'
        # Hostile member names are rejected before anything escapes.
        for index, name in enumerate(['../escaped.txt', '/tmp/altirra-escaped.txt',
                                      'ok/../../escaped.txt', 'ok/D:escaped.txt',
                                      'ok/.. /escaped.txt']):
            archive = pathlib.Path(temp) / f'evil{index}.tar.gz'
            archive.write_bytes(evil_tar(name))
            target = pathlib.Path(temp) / f'evil out {index}'
            target.mkdir()
            error = run(binary, 'extract', str(archive), str(target))['ERROR']
            assert 'path' in error, (name, error)
            assert not (pathlib.Path(temp) / 'escaped.txt').exists()
        # A damaged archive is reported, not half-installed silently.
        archive = pathlib.Path(temp) / 'damaged.tar.gz'
        data = bytearray(FILES['fujinet-pc-ATARI_v9.9-dev_ubuntu-24.04-amd64.tar.gz'])
        data[len(data) // 2] ^= 0xFF
        archive.write_bytes(bytes(data))
        target = pathlib.Path(temp) / 'damaged out'
        target.mkdir()
        assert run(binary, 'extract', str(archive), str(target))['ERROR']
    server.shutdown()
    print('fujinet install: ok')


if __name__ == '__main__':
    main()
