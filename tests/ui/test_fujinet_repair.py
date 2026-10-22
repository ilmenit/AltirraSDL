"""Recover the managed connection without changing the user's Atari or SD."""
from pathlib import Path
import errno
import os
import shutil
import socket
import sys

import pytest

from .harness import AltirraTestHarness
from .test_fujinet import fujinet_desktop, free_port, wait_status
from .test_explorers import click


@pytest.fixture
def runtime_fixture(fujinet_desktop, tmp_path):
    emu = fujinet_desktop
    suffix = '.exe' if emu.executable.lower().endswith('.exe') else ''
    fixture = Path(emu.executable).resolve().parents[2] / (
        'src/AltirraSDL/source/media/tests/altirra-fujinet-pc-test' + suffix)
    if not fixture.is_file():
        pytest.skip('Build the managed FujiNet process fixture')
    runtime = tmp_path / 'runtime'
    (runtime / 'data').mkdir(parents=True)
    executable = runtime / ('fujinet' + suffix)
    shutil.copy2(fixture, executable)
    (runtime / 'fnconfig.ini').write_text('[Host1]\nname=keep.example\ntype=TNFS\n')
    emu.send(f'fujinet listen {free_port()} 1 250')
    yield emu, executable, runtime
    if emu._proc and emu._proc.poll() is None:
        emu.send('fujinet stop')
        wait_status(emu, lambda s: not s['running'])


def test_busy_web_port_is_avoided_and_selection_survives_restart(runtime_fixture):
    emu, executable, _ = runtime_fixture
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as occupied:
        occupied.bind(('127.0.0.1', 0))
        occupied.listen()
        busy = occupied.getsockname()[1]
        emu.send(f'fujinet runtime {busy} {executable}')
        emu.send('fujinet start')
        started = wait_status(emu, lambda s: 'child started' in s['logTail'])
        assert started['running'] and not started['error']
        assert started['webPort'] != busy
        assert str(busy) in started['notice'] and str(started['webPort']) in started['notice']
        arguments = (Path(started['stateDirectory']) / 'SD/received-arguments.txt').read_text()
        assert f'http://127.0.0.1:{started["webPort"]}' in arguments
        emu.send('fujinet stop')
        wait_status(emu, lambda s: not s['running'])
        emu.stop()
        with AltirraTestHarness(executable=emu.executable,
                               extra_args=['--ui-mode', 'desktop']) as fresh:
            restored = fresh.send('fujinet status')
            assert restored['webPort'] == started['webPort']
            assert restored['executable'] == str(executable) and not restored['running']


def test_repair_button_rebuilds_connection_preserving_data_and_machine(runtime_fixture):
    emu, executable, _ = runtime_fixture
    emu.send(f'fujinet runtime 18000 {executable}')
    emu.send('fujinet start')
    started = wait_status(emu, lambda s: 'child started' in s['logTail'])
    state = Path(started['stateDirectory'])
    sentinel = state / 'SD/my-file.txt'
    sentinel.write_text('keep my SD file')
    config = state / 'fnconfig.ini'
    config.write_text('[Host1]\nname=keep.example\ntype=TNFS\n'
                      '[Mount1]\nhostslot=1\npath=/keep.atr\n'
                      '[BOIP]\nenabled=0\nhost=bad.example\nport=1\n')
    emu.send('netplay_profile disk_create')
    emu.send('netplay_profile disk_write 113')
    emu.send('debugger_console e 1000 5A')
    before_disk = emu.send('netplay_profile status')
    before = emu.query_state()['state']['sim']
    emu.send('fujinet show')
    emu.wait_frames(5)
    # Recovery remains discoverable with the setup details collapsed.
    click(emu, 'FujiNet', 'Repair automatic setup')
    repaired = wait_status(emu, lambda s: s['running'] and not s['repairPending']
                           and s['port'] != started['port'])
    assert not repaired['bootPending'] and not repaired['error']
    assert repaired['executable'] == started['executable']
    assert 9997 <= repaired['port'] < 10017
    assert 'name=keep.example' in config.read_text() and 'path=/keep.atr' in config.read_text()
    assert 'enabled=1' in config.read_text() and 'host=127.0.0.1' in config.read_text()
    assert f'port={repaired["port"]}' in config.read_text()
    assert sentinel.read_text() == 'keep my SD file'
    assert emu.mem_read(0x1000, 1) == [0x5A]
    after_disk = emu.send('netplay_profile status')
    assert after_disk['diskObject'] == before_disk['diskObject']
    assert after_disk['diskDirty'] and after_disk['diskByte'] == 113
    after = emu.query_state()['state']['sim']
    assert not after['running'] and after['emulationTick'] == before['emulationTick']
    assert repaired['localD1Enabled'] == started['localD1Enabled']


@pytest.mark.skipif(sys.platform == 'win32', reason='Uses the POSIX ignore-SIGTERM fixture')
@pytest.mark.parametrize('cancel', ['explicit', 'profile'])
def test_pending_repair_cancellation_has_no_late_restart(runtime_fixture, cancel):
    emu, executable, runtime = runtime_fixture
    (runtime / 'fnconfig.ini').write_text(';test-mode=ignore\n[BOIP]\nenabled=1\n')
    emu.send(f'fujinet runtime 18000 {executable}')
    target = emu.send('profile_test create')['id'] if cancel == 'profile' else None
    emu.send('fujinet start')
    wait_status(emu, lambda s: 'child started' in s['logTail'])
    emu.send('fujinet repair')
    if cancel == 'explicit':
        emu.send('fujinet cancel_repair')
    else:
        emu.send(f'profile_test activate {target}')
    stopped = wait_status(emu, lambda s: not s['running'] and not s['repairPending'])
    assert 'cancelled' in stopped['error'].lower()
    emu.wait_frames(30)
    assert not emu.send('fujinet status')['running']


def test_child_web_start_failure_is_actionable_and_prevents_automatic_boot(runtime_fixture):
    emu, executable, runtime = runtime_fixture
    (runtime / 'fnconfig.ini').write_text(
        ';test-mode=web-error\n;test-mode=restart-trigger\n[BOIP]\nenabled=1\n')
    emu.send(f'fujinet runtime 18000 {executable}')
    emu.send('debugger_console e 1000 5A')
    emu.send('fujinet start_boot')
    failed = wait_status(emu, lambda s: s['error'] and not s['bootPending'])
    assert failed['running'] and 'web server' in failed['error']
    assert 'Repair automatic setup' in failed['error'] and str(failed['webPort']) in failed['error']
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as peer:
        peer.bind(('127.0.0.1', 0))
        peer.sendto(b'\xc1', ('127.0.0.1', failed['port']))
        wait_status(emu, lambda s: s['connected'])
        emu.wait_frames(15)
        assert emu.mem_read(0x1000, 1) == [0x5A]
        assert not emu.query_state()['state']['sim']['running']
        emu.send('fujinet show')
        emu.wait_frames(5)
        assert emu.find_item('FujiNet', 'Open Web Interface')['disabled']
        # A healthy exit-75 replacement must not retain the old web error.
        state = Path(failed['stateDirectory'])
        config = state / 'fnconfig.ini'
        config.write_text(config.read_text().replace(';test-mode=web-error\n', ''))
        (state / 'SD/restart-now').write_text('restart')
        recovered = wait_status(emu, lambda s: s['restarts'] == 1 and not s['error'])
        assert recovered['running'] and not recovered['bootPending']
        assert emu.mem_read(0x1000, 1) == [0x5A]


def test_external_peer_is_not_reconfigured_by_repair(runtime_fixture):
    emu, executable, _ = runtime_fixture
    emu.send(f'fujinet runtime 18000 {executable}')
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as peer:
        peer.bind(('127.0.0.1', 0))
        peer.sendto(b'\xc1', ('127.0.0.1', emu.send('fujinet status')['port']))
        before = wait_status(emu, lambda s: s['connected'])
        emu.send('fujinet show')
        emu.wait_frames(5)
        assert 'Repair automatic setup' not in emu.get_item_labels('FujiNet')
        rejected = emu.send_raw('fujinet repair')
        assert not rejected['ok']
        after = emu.send('fujinet status')
        assert after['connected'] and after['peer'] == before['peer']
        assert not after['running'] and not after['repairPending']


@pytest.mark.parametrize('address', ['127.0.0.2', '0.0.0.0'])
def test_repair_normalizes_saved_bind_address_and_avoids_busy_udp_port(runtime_fixture, address):
    emu, executable, _ = runtime_fixture
    emu.send(f'fujinet runtime 18000 {executable}')
    emu.stop()
    settings = Path(os.environ['XDG_CONFIG_HOME']) / 'altirra/settings.ini'
    previous = settings.read_text()
    old = r'\"bind_address\":\"0.0.0.0\"'
    assert old in previous
    settings.write_text(previous.replace(old, r'\"bind_address\":\"' + address + r'\"'))
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as occupied:
        try:
            occupied.bind(('127.0.0.1', 9997))
        except OSError as error:
            # A user's running emulator can already occupy the default.
            if error.errno not in (errno.EADDRINUSE, 10048):
                raise
        with AltirraTestHarness(executable=emu.executable,
                               extra_args=['--ui-mode', 'desktop']) as fresh:
            fresh.close_dialog('SetupWizard')
            fresh.pause()
            fresh.send('debugger_console e 1000 5A')
            tick = fresh.query_state()['state']['sim']['emulationTick']
            fresh.send('fujinet repair')
            repaired = wait_status(fresh, lambda s: s['running'] and not s['repairPending'])
            assert not repaired['error'] and not repaired['bootPending']
            assert 9998 <= repaired['port'] < 10017
            assert repaired['listening']
            assert fresh.mem_read(0x1000, 1) == [0x5A]
            assert fresh.query_state()['state']['sim']['emulationTick'] == tick
    updated = settings.read_text()
    assert r'\"bind_address\":\"127.0.0.1\"' in updated
    assert r'\"bind_address\":\"' + address + r'\"' not in updated
