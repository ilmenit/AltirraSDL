"""Managed mount-and-boot UI and cancellation use real worker/controller paths."""
import importlib.util
import http.server
from pathlib import Path
import shutil
import socket
import threading
import time

import pytest

from .test_fujinet import fujinet_desktop, free_port, wait_status
from .test_explorers import make_disk, click, enter

TITLE = 'Run a file through FujiNet'


class ManagedAPI:
    """Serve the child fixture's API only after it has selected its port."""
    def __init__(self, handler):
        self.handler = handler
        self.server = None

    def start(self, port):
        if self.server and self.server.server_port == port:
            return
        self.stop()
        self.server = http.server.ThreadingHTTPServer(('127.0.0.1', port), self.handler)
        self.server.daemon_threads = True
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def stop(self):
        if self.server:
            self.server.shutdown()
            self.server.server_close()
            self.server = None


@pytest.fixture
def managed_mount(fujinet_desktop, tmp_path):
    emu = fujinet_desktop
    suffix = '.exe' if emu.executable.lower().endswith('.exe') else ''
    fixture = Path(emu.executable).resolve().parents[2] / (
        'src/AltirraSDL/source/media/tests/altirra-fujinet-pc-test' + suffix)
    if not fixture.is_file():
        pytest.skip('Build the self-hosted FujiNet process fixture')
    # Reuse the standalone fixture's actual official REST response shapes.
    source = Path(__file__).resolve().parents[2] / (
        'src/AltirraSDL/source/media/tests/fujinet_mount_test.py')
    spec = importlib.util.spec_from_file_location('fujinet_mount_fixture', source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    handler = module.Handler
    handler.mode, handler.posts, handler.mounted = '', [], None
    api = ManagedAPI(handler)
    emu.fujinet_api = api
    runtime = tmp_path / 'owned test runtime'
    (runtime / 'data').mkdir(parents=True)
    (runtime / 'SD').mkdir()
    executable = runtime / 'fujinet fixture'
    shutil.copy2(fixture, executable)
    (runtime / 'fnconfig.ini').write_text('[BOIP]\nenabled=1\nhost=127.0.0.1\nport=9997\n')
    port = free_port()
    emu.send(f'fujinet listen {port} 0 100')
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as available:
        available.bind(('127.0.0.1', 0))
        web_port = available.getsockname()[1]
    emu.send(f'fujinet runtime {web_port} {executable}')
    emu.send('fujinet start')
    status = wait_status(emu, lambda s: s['running'] and 'child started' in s['logTail'])
    handler.sd_root = Path(status['stateDirectory']) / 'SD'
    api.start(status['webPort'])
    game = tmp_path / 'local game.xex'
    game.write_bytes(module.XEX)
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as peer:
            peer.bind(('127.0.0.1', 0))
            peer.sendto(b'\xc1', ('127.0.0.1', port))
            wait_status(emu, lambda s: s['connected'])
            yield emu, handler, game, f'http://127.0.0.1:{api.server.server_port}'
    finally:
        emu.pause()
        emu.send('fujinet stop')
        wait_status(emu, lambda s: not s['running'])
        api.stop()


def test_url_button_mounts_read_only_and_boots_without_ejecting_local_disk(managed_mount, tmp_path):
    emu, handler, game, url = managed_mount
    keep = tmp_path / 'keep local D1.atr'
    make_disk(keep)
    emu.send(f'attach_disk 0 {keep}')
    before = emu.query_state()['state']['disks']
    emu.send('fujinet show')
    emu.wait_frames(5)
    click(emu, 'FujiNet', 'Setup and full controls')
    click(emu, 'FujiNet', 'Run a file through FujiNet...')
    enter(emu, TITLE, '##FujiNet game URL', url + '/game.xex')
    click(emu, TITLE, 'Download, mount and boot')
    result = wait_status(emu, lambda s: not s['filePending'])
    assert not result['error'] and result['running'] and result['connected']
    assert not result['open'] and not emu.query_window(TITLE).get('visible', False)
    assert emu.query_state()['state']['disks'] == before
    assert emu.query_state()['state']['sim']['running']
    assert not result['localD1Enabled'] and not result['fastBoot']
    assert len(handler.posts) == 1 and handler.posts[0][1]['mode'] == 'r'
    assert handler.posts[0][1]['host_slot'] == 2
    mounted = handler.sd_root / handler.mounted['path'].lstrip('/')
    assert mounted.read_bytes() == game.read_bytes()
    assert not list(handler.sd_root.rglob('.owner-*'))


@pytest.mark.parametrize('action', ['cancel_file', 'stop'])
def test_cancelling_during_mount_never_boots(managed_mount, action):
    emu, handler, game, _ = managed_mount
    handler.mode = 'cancel-post'
    emu.send(f'fujinet run {game}')
    deadline = time.monotonic() + 5
    while not handler.posts and time.monotonic() < deadline:
        time.sleep(.01)
    assert handler.posts, 'Mount request was not reached'
    assert emu.send('fujinet status')['filePending']
    emu.send('fujinet ' + action)
    status = wait_status(emu, lambda s: not s['filePending'])
    assert not emu.query_state()['state']['sim']['running']
    assert not list(handler.sd_root.rglob('.owner-*'))
    if action == 'cancel_file':
        assert 'cancel' in status['error'].lower() and status['running']
    else:
        wait_status(emu, lambda s: not s['running'])
