"""A managed replacement clears the old UDP peer before its first HELLO."""
from pathlib import Path
import shutil
import socket

import pytest

from .test_fujinet import fujinet_desktop, free_port, wait_status


def test_owned_restart_releases_old_peer_before_new_hello(fujinet_desktop, tmp_path):
    emu = fujinet_desktop
    suffix = '.exe' if emu.executable.lower().endswith('.exe') else ''
    fixture = Path(emu.executable).resolve().parents[2] / (
        'src/AltirraSDL/source/media/tests/altirra-fujinet-pc-test' + suffix)
    if not fixture.is_file():
        pytest.skip('Build altirra-fujinet-pc-test for managed restart coverage')
    runtime = tmp_path / 'restart fixture'
    (runtime / 'data').mkdir(parents=True)
    (runtime / 'SD').mkdir()
    executable = runtime / ('fujinet fixture' + suffix)
    shutil.copy2(fixture, executable)
    (runtime / 'fnconfig.ini').write_text(
        ';test-mode=restart-trigger\n[BOIP]\nenabled=1\nhost=127.0.0.1\nport=9997\n')
    port = free_port()
    emu.send(f'fujinet listen {port} 0 100')
    emu.send(f'fujinet runtime 18000 {executable}')
    emu.send('fujinet start')
    status = wait_status(emu, lambda s: s['running'] and 'child started' in s['logTail'])
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as old_peer:
        old_peer.bind(('127.0.0.1', 0))
        old_peer.sendto(b'\xc1', ('127.0.0.1', port))
        wait_status(emu, lambda s: s['connected'])
        # The child exits75 without sending NetSIO DISCONNECT. The process
        # controller must clear this stale endpoint before its replacement.
        (Path(status['stateDirectory']) / 'SD' / 'restart-now').write_text('restart')
        restarted = wait_status(emu, lambda s: s['restarts'] == 1 and not s['connected'])
        assert restarted['running'] and restarted['listening']
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as replacement:
            replacement.bind(('127.0.0.1', 0))
            replacement.sendto(b'\xc1', ('127.0.0.1', port))
            connected = wait_status(emu, lambda s: s['connected'], timeout=3)
            assert str(replacement.getsockname()[1]) in connected['peer']
        assert not emu.query_state()['state']['sim']['running']
    emu.send('fujinet stop')
    wait_status(emu, lambda s: not s['running'])
