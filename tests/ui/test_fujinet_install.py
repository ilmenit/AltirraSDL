"""One-click FujiNet setup: install from a mock GitHub, pick a free port, boot."""
import hashlib
import http.server
import io
import json
from pathlib import Path
import socket
import tarfile
import threading
import time
import zipfile

import pytest

from .harness import AltirraTestHarness
from .test_explorers import click
from .test_fujinet import free_port, wait_status

SUFFIXES = ['windows-x64.zip', 'ubuntu-24.04-amd64.tar.gz', 'ubuntu-22.04-amd64.tar.gz',
            'ubuntu-24.04-arm64.tar.gz', 'ubuntu-24.04-armhf.tar.gz',
            'macos-15-arm64.tar.gz', 'macos-14-arm64.tar.gz', 'macos-15-x64.tar.gz']


def release_archive(fixture, suffix):
    files = {
        'fujinet-pc-ATARI/fnconfig.ini': (b'[BOIP]\nenabled=1\nhost=localhost\nport=\n', 0o644),
        'fujinet-pc-ATARI/data/850relocator.bin': (b'data', 0o644),
        'fujinet-pc-ATARI/SD/--- FujiNet SD Card ---': (b'', 0o644),
        'fujinet-pc-ATARI/' + ('fujinet.exe' if suffix.endswith('.zip') else 'fujinet'):
            (fixture.read_bytes(), 0o755),
    }
    buffer = io.BytesIO()
    if suffix.endswith('.zip'):
        with zipfile.ZipFile(buffer, 'w', zipfile.ZIP_DEFLATED) as archive:
            for name, (data, _) in files.items():
                archive.writestr(name, data)
    else:
        with tarfile.open(fileobj=buffer, mode='w:gz') as archive:
            for name, (data, mode) in files.items():
                info = tarfile.TarInfo(name)
                info.size, info.mode = len(data), mode
                archive.addfile(info, io.BytesIO(data))
    return buffer.getvalue()


@pytest.fixture
def mock_github(emu):
    suffix = '.exe' if emu.executable.lower().endswith('.exe') else ''
    fixture = Path(emu.executable).resolve().parents[2] / (
        'src/AltirraSDL/source/media/tests/altirra-fujinet-pc-test' + suffix)
    if not fixture.is_file():
        pytest.skip('Build altirra-fujinet-pc-test for one-click setup coverage')
    files = {f'fujinet-pc-ATARI_v9.9-dev_{s}': release_archive(fixture, s) for s in SUFFIXES}

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_GET(self):
            if self.path == '/releases':
                base = f'http://127.0.0.1:{self.server.server_port}/files/'
                data = json.dumps([{'name': 'FujiNet-PC Nightly', 'draft': False, 'assets': [
                    {'id': 7, 'name': name, 'size': len(body),
                     'digest': 'sha256:' + hashlib.sha256(body).hexdigest(),
                     'browser_download_url': base + name}
                    for name, body in files.items()]}]).encode()
            elif self.path.startswith('/files/') and self.path[7:] in files:
                data = files[self.path[7:]]
            else:
                self.send_response(404)
                self.end_headers()
                return
            self.send_response(200)
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    yield f'http://127.0.0.1:{server.server_port}/releases'
    server.shutdown()


def test_one_click_setup_installs_moves_off_busy_port_and_boots(
        emu, mock_github, monkeypatch, tmp_path):
    monkeypatch.setenv('SDL_VIDEODRIVER', 'dummy')
    monkeypatch.setenv('SDL_AUDIODRIVER', 'dummy')
    monkeypatch.setenv('ALTIRRA_DISPLAY_BACKEND', 'sdlrenderer')
    monkeypatch.setenv('XDG_CONFIG_HOME', str(tmp_path / 'config with spaces'))
    monkeypatch.setenv('ALTIRRA_FUJINET_RELEASES_URL', mock_github)
    with AltirraTestHarness(executable=emu.executable,
                            extra_args=['--ui-mode', 'desktop']) as fresh:
        fresh.close_dialog('SetupWizard')
        fresh.send('set_window_size 1280 900')
        fresh.pause()
        # Another program (e.g. a second AltirraSDL) holds the profile's port.
        busy = free_port()
        fresh.send(f'fujinet listen {busy} 0 100')
        fresh.send('fujinet disconnect')
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as holder, \
                socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as peer:
            holder.bind(('127.0.0.1', busy))
            fresh.send('fujinet show')
            fresh.wait_frames(5)
            # The listener above already configured the profile, so the
            # panel opens on full controls; the guide offers the one click.
            click(fresh, 'FujiNet', 'Step-by-step guide')
            click(fresh, 'FujiNet', 'Set up FujiNet automatically')
            status = wait_status(fresh, lambda s: s['running'] and s['bootPending'], timeout=20)
            assert status['port'] != busy and status['listening'], status
            assert status['runtimeVersion'].startswith('fujinet-pc-ATARI_v9.9-dev_')
            installed = Path(status['executable'])
            assert installed.parent.name == 'fujinet-pc-ATARI'
            assert 'FujiNet-PC' in installed.parts
            assert not status['autoSetupPending'] and status['missingLibraries'] == 0
            fresh.wait_frames(3)
            fresh.send('ui_screenshot ' + str(tmp_path / 'fujinet-setting-up.png'))
            # The installed runtime's NetSIO HELLO completes the boot.
            peer.bind(('127.0.0.1', 0))
            peer.sendto(b'\xc1', ('127.0.0.1', status['port']))
            booted = wait_status(fresh, lambda s: s['connected'] and not s['bootPending'])
            assert not booted['localD1Enabled']
            assert fresh.query_state()['state']['sim']['running']
            deadline = time.monotonic() + 5
            while 'Read the CONFIG and XEX guide' not in fresh.get_item_labels('FujiNet'):
                assert time.monotonic() < deadline, fresh.get_item_labels('FujiNet')
                fresh.wait_frames(2)
            fresh.send('ui_screenshot ' + str(tmp_path / 'fujinet-ready.png'))
            fresh.send('fujinet stop')
            wait_status(fresh, lambda s: not s['running'])
        # Running it again reuses the install and goes straight to starting.
        fresh.send('fujinet auto')
        again = wait_status(fresh, lambda s: s['running'])
        assert again['executable'] == status['executable'] and not again['installing']
        fresh.send('fujinet stop')
        wait_status(fresh, lambda s: not s['running'])


def test_install_failure_explains_and_offers_manual_route(emu, monkeypatch, tmp_path):
    monkeypatch.setenv('SDL_VIDEODRIVER', 'dummy')
    monkeypatch.setenv('SDL_AUDIODRIVER', 'dummy')
    monkeypatch.setenv('ALTIRRA_DISPLAY_BACKEND', 'sdlrenderer')
    monkeypatch.setenv('XDG_CONFIG_HOME', str(tmp_path / 'config'))
    # Nothing listens here: the download site is unreachable.
    monkeypatch.setenv('ALTIRRA_FUJINET_RELEASES_URL', f'http://127.0.0.1:{free_port()}/releases')
    with AltirraTestHarness(executable=emu.executable,
                            extra_args=['--ui-mode', 'desktop']) as fresh:
        fresh.close_dialog('SetupWizard')
        fresh.send('set_window_size 1280 900')
        fresh.pause()
        fresh.send('fujinet show')
        fresh.wait_frames(5)
        click(fresh, 'FujiNet', 'Set up FujiNet automatically')
        status = wait_status(fresh, lambda s: not s['installing'] and s['error'], timeout=20)
        assert 'download site' in status['error'] and not status['running']
        assert not fresh.query_state()['state']['sim']['running']
        fresh.wait_frames(3)
        labels = fresh.get_item_labels('FujiNet')
        assert 'Try again' in labels and 'Download FujiNet-PC yourself instead' in labels


def test_boot_failure_keeps_the_missing_library_explanation(emu, monkeypatch, tmp_path):
    suffix = '.exe' if emu.executable.lower().endswith('.exe') else ''
    fixture = Path(emu.executable).resolve().parents[2] / (
        'src/AltirraSDL/source/media/tests/altirra-fujinet-pc-test' + suffix)
    if not fixture.is_file():
        pytest.skip('Build altirra-fujinet-pc-test for start-failure coverage')
    runtime = tmp_path / 'fujinet-pc-ATARI'
    (runtime / 'data').mkdir(parents=True)
    executable = runtime / ('fujinet' + suffix)
    executable.write_bytes(fixture.read_bytes())
    executable.chmod(0o755)
    # The fixture prints the glibc loader's message for a missing OpenSSL 3.
    (runtime / 'fnconfig.ini').write_text(';test-mode=missing-library\n[BOIP]\nenabled=1\n')
    monkeypatch.setenv('SDL_VIDEODRIVER', 'dummy')
    monkeypatch.setenv('SDL_AUDIODRIVER', 'dummy')
    monkeypatch.setenv('ALTIRRA_DISPLAY_BACKEND', 'sdlrenderer')
    monkeypatch.setenv('XDG_CONFIG_HOME', str(tmp_path / 'config'))
    with AltirraTestHarness(executable=emu.executable,
                            extra_args=['--ui-mode', 'desktop']) as fresh:
        fresh.close_dialog('SetupWizard')
        fresh.pause()
        fresh.send(f'fujinet listen {free_port()} 0 100')
        fresh.send(f'fujinet runtime 18000 {executable}')
        fresh.send('fujinet auto')
        status = wait_status(fresh, lambda s: not s['running'] and not s['bootPending'])
        assert 'OpenSSL 3 (libcrypto.so.3)' in status['error'], status['error']
        assert not fresh.query_state()['state']['sim']['running']


def test_start_with_altirra_option_boots_unless_command_line_names_a_program(
        emu, monkeypatch, tmp_path):
    from .test_explorers import make_disk
    suffix = '.exe' if emu.executable.lower().endswith('.exe') else ''
    fixture = Path(emu.executable).resolve().parents[2] / (
        'src/AltirraSDL/source/media/tests/altirra-fujinet-pc-test' + suffix)
    if not fixture.is_file():
        pytest.skip('Build altirra-fujinet-pc-test for automatic start coverage')
    runtime = tmp_path / 'fujinet-pc-ATARI'
    (runtime / 'data').mkdir(parents=True)
    executable = runtime / ('fujinet' + suffix)
    executable.write_bytes(fixture.read_bytes())
    executable.chmod(0o755)
    (runtime / 'fnconfig.ini').write_text('[BOIP]\nenabled=1\n')
    for name, value in (('SDL_VIDEODRIVER', 'dummy'), ('SDL_AUDIODRIVER', 'dummy'),
                        ('ALTIRRA_DISPLAY_BACKEND', 'sdlrenderer'),
                        ('XDG_CONFIG_HOME', str(tmp_path / 'config'))):
        monkeypatch.setenv(name, value)
    port = free_port()
    with AltirraTestHarness(executable=emu.executable,
                            extra_args=['--ui-mode', 'desktop']) as fresh:
        fresh.close_dialog('SetupWizard')
        fresh.send(f'fujinet listen {port} 0 100')
        fresh.send(f'fujinet runtime 18000 {executable}')
        assert not fresh.send('fujinet status')['autoStart']
        # The option is offered on the panel once a runtime is selected.
        fresh.send('fujinet show')
        fresh.wait_frames(5)
        click(fresh, 'FujiNet', 'Start FujiNet when AltirraSDL starts')
        assert fresh.send('fujinet status')['autoStart']
        assert not fresh.send('fujinet status')['running']
        fresh.send('quit')
        fresh._proc.wait(timeout=10)
    with AltirraTestHarness(executable=emu.executable,
                            extra_args=['--ui-mode', 'desktop']) as fresh:
        status = wait_status(fresh, lambda s: s['running'] and s['bootPending'])
        assert status['autoStart'] and status['port'] == port
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as peer:
            peer.bind(('127.0.0.1', 0))
            peer.sendto(b'\xc1', ('127.0.0.1', port))
            booted = wait_status(fresh, lambda s: s['connected'] and not s['bootPending'])
            assert not booted['localD1Enabled']
        fresh.send('quit')
        fresh._proc.wait(timeout=10)
    # A program named on the command line wins over the preference.
    disk = tmp_path / 'command line.atr'
    make_disk(disk)
    with AltirraTestHarness(executable=emu.executable,
                            extra_args=['--ui-mode', 'desktop', str(disk)]) as fresh:
        fresh.wait_frames(30)
        status = fresh.send('fujinet status')
        assert status['autoStart'] and not status['running'] and not status['bootPending']
