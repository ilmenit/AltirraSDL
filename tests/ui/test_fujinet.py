"""Native NetSIO controls and managed runtime use the real main-thread UI paths."""
from pathlib import Path
import shutil
import socket
import time

import pytest

from .harness import AltirraTestHarness
from .test_explorers import make_disk, click


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def wait_status(emu, condition, timeout=8):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        status = emu.send('fujinet status')
        if condition(status):
            return status
        time.sleep(.03)
    raise AssertionError('FujiNet status did not reach expected state: ' + str(status))


@pytest.fixture
def fujinet_desktop(emu, monkeypatch, tmp_path):
    monkeypatch.setenv('SDL_VIDEODRIVER', 'dummy')
    monkeypatch.setenv('SDL_AUDIODRIVER', 'dummy')
    monkeypatch.setenv('ALTIRRA_DISPLAY_BACKEND', 'sdlrenderer')
    monkeypatch.setenv('XDG_CONFIG_HOME', str(tmp_path / 'config with spaces'))
    with AltirraTestHarness(executable=emu.executable,
                           extra_args=['--ui-mode', 'desktop']) as fresh:
        fresh.close_dialog('SetupWizard')
        fresh.pause()
        fresh.wait_frames(3)
        yield fresh


def test_listener_reconfiguration_rolls_back_and_profile_persists(fujinet_desktop):
    emu = fujinet_desktop
    first, second = free_port(), free_port()
    emu.send(f'fujinet listen {first} 0 100')
    status = emu.send('fujinet status')
    assert status['present'] and status['listening'] and status['port'] == first
    emu.send(f'fujinet listen {second} 1 150')
    assert emu.send('fujinet status')['port'] == second
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as occupied:
        occupied.bind(('127.0.0.1', 0))
        blocked = occupied.getsockname()[1]
        result = emu.send_raw(f'fujinet listen {blocked} 0 100')
        assert result['ok'] is False
        after = emu.send('fujinet status')
        assert after['listening'] and after['port'] == second
    emu.send('fujinet disconnect')
    assert not emu.send('fujinet status')['listening']
    emu.send('fujinet reconnect')
    assert emu.send('fujinet status')['listening']
    # Reconfiguration and connection management leave CPU state unchanged.
    assert not emu.query_state()['state']['sim']['running']


def test_boot_keeps_disk_images_and_resumes(fujinet_desktop, tmp_path):
    emu = fujinet_desktop
    first = tmp_path / 'local D1.atr'
    second = tmp_path / 'local D2.atr'
    make_disk(first)
    make_disk(second)
    emu.send(f'attach_disk 0 {first}')
    emu.send(f'attach_disk 1 {second}')
    emu.wait_frames(5)
    emu.pause()
    before = emu.query_state()['state']['disks'][:2]
    port = free_port()
    emu.send(f'fujinet listen {port} 0 100')
    # Joined is the protocol's peer HELLO byte (NetSIO spec / netsio.cpp).
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as peer:
        peer.bind(('127.0.0.1', 0))
        peer.sendto(b'\xc1', ('127.0.0.1', port))
        wait_status(emu, lambda s: s['connected'])
        emu.set_speed('200')
        emu.set_speed('warp')
        emu.send('fujinet boot')
        after = emu.send('fujinet status')
        assert not after['localD1Enabled'] and not after['fastBoot']
        assert emu.query_state()['state']['sim']['running']
        speed = emu.query_state()['state']['sim']
        assert not speed['turbo'] and speed['speedPercent'] == 100
        assert emu.query_state()['state']['disks'][:2] == before
        emu.pause()
        # Computer-only reset must not power-cycle the external peer.
        peer.settimeout(.2)
        packets = []
        deadline = time.monotonic() + .5
        try:
            while time.monotonic() < deadline:
                packets.append(peer.recvfrom(2048)[0])
        except socket.timeout:
            pass
        assert b'\xff' not in packets
    emu.send('fujinet local_d1')
    assert emu.send('fujinet status')['localD1Enabled']
    assert emu.query_state()['state']['disks'][:2] == before


def test_boot_without_peer_changes_nothing(fujinet_desktop):
    emu = fujinet_desktop
    emu.send(f'fujinet listen {free_port()} 0 100')
    emu.set_speed('200')
    emu.set_speed('warp')
    before = emu.send('fujinet status')
    result = emu.send_raw('fujinet boot')
    assert not result['ok'] and 'peer' in result['error'].lower()
    after = emu.send('fujinet status')
    assert after['localD1Enabled'] == before['localD1Enabled']
    assert after['fastBoot'] == before['fastBoot']
    assert not emu.query_state()['state']['sim']['running']
    speed = emu.query_state()['state']['sim']
    assert speed['turbo'] and speed['speedPercent'] == 200


def test_managed_runtime_start_stop_and_preferences(fujinet_desktop, tmp_path):
    emu = fujinet_desktop
    suffix = '.exe' if emu.executable.lower().endswith('.exe') else ''
    fixture = Path(emu.executable).resolve().parents[2] / (
        'src/AltirraSDL/source/media/tests/altirra-fujinet-pc-test' + suffix)
    if not fixture.is_file():
        pytest.skip('Build altirra-fujinet-pc-test to enable managed runtime UI coverage')
    runtime = tmp_path / 'extracted runtime with spaces'
    (runtime / 'data').mkdir(parents=True)
    (runtime / 'SD').mkdir()
    executable = runtime / ('fujinet fixture' + suffix)
    shutil.copy2(fixture, executable)
    template = '[Host1]\ntype=TNFS\nname=original.example\n[BOIP]\nenabled=0\nhost=localhost\nport=9997\n'
    (runtime / 'fnconfig.ini').write_text(template)
    port = free_port()
    emu.send(f'fujinet listen {port} 0 100')
    emu.send(f'fujinet runtime 18000 {executable}')
    emu.send('fujinet show')
    emu.wait_frames(3)
    click(emu, 'FujiNet', 'Start FujiNet-PC')
    status = wait_status(emu, lambda s: s['running'] and 'child started' in s['logTail'])
    state = Path(status['stateDirectory'])
    assert (state / 'fnconfig.ini').is_file()
    assert f'port={port}' in (state / 'fnconfig.ini').read_text()
    assert (runtime / 'fnconfig.ini').read_text() == template
    assert (state / 'SD' / 'received-arguments.txt').is_file()
    # UI text input and reconfiguration API both honour active process ownership.
    assert emu.find_item('FujiNet', '##Executable')['disabled']
    assert not emu.send_raw(f'fujinet listen {free_port()} 0 100')['ok']
    # Disconnecting a managed runtime leaves an explicit recovery action,
    # without requiring a boot/reset or stopping its process.
    click(emu, 'FujiNet', 'Disconnect')
    assert not emu.send('fujinet status')['listening']
    click(emu, 'FujiNet', 'Reconnect NetSIO')
    restored = wait_status(emu, lambda s: s['listening'])
    assert restored['running'] and not restored['bootPending']
    assert not emu.query_state()['state']['sim']['running']
    click(emu, 'FujiNet', 'Stop FujiNet-PC')
    status = wait_status(emu, lambda s: not s['running'])
    assert not status['stopping']
    assert (state / 'fujinet-pc.log').is_file()

    emu.send('fujinet local_d1')
    emu.send('fujinet start_boot')
    waiting = wait_status(emu, lambda s: s['running'] and s['bootPending'])
    assert waiting['localD1Enabled']
    assert not emu.query_state()['state']['sim']['running']
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as peer:
        peer.bind(('127.0.0.1', 0))
        peer.sendto(b'\xc1', ('127.0.0.1', port))
        booted = wait_status(emu, lambda s: not s['bootPending'])
        assert booted['connected'] and not booted['localD1Enabled']
        assert not booted['fastBoot']
        assert emu.query_state()['state']['sim']['running']
    emu.pause()
    emu.send('fujinet stop')
    wait_status(emu, lambda s: not s['running'])

    # Stop cancels an outstanding boot intent; no delayed reset can fire
    # after the owner has stopped the external process.
    emu.send('fujinet local_d1')
    emu.send('fujinet start_boot')
    wait_status(emu, lambda s: s['bootPending'])
    emu.send('fujinet stop')
    stopped = wait_status(emu, lambda s: not s['running'])
    assert not stopped['bootPending'] and stopped['localD1Enabled']
    assert not emu.query_state()['state']['sim']['running']

    # Clean process exit persists machine-global selections. Restarting the
    # emulator does not automatically start another external process.
    emu.stop()
    with AltirraTestHarness(executable=emu.executable,
                           extra_args=['--ui-mode', 'desktop']) as fresh:
        fresh.close_dialog('SetupWizard')
        status = fresh.send('fujinet status')
        assert status['executable'] == str(executable)
        assert status['webPort'] == 18000 and not status['running']
        assert status['present'] and status['port'] == port


def test_open_panel_tracks_profile_switch_before_start(fujinet_desktop, tmp_path):
    from .test_desktop_search_profiles import create_profile, switch_profile

    emu = fujinet_desktop
    default_port, other_port = free_port(), free_port()
    emu.send(f'fujinet listen {default_port} 0 100')
    create_profile(emu, 'FujiNet other profile')
    switch_profile(emu, 'FujiNet other profile')
    emu.close_dialog('Profiles')
    emu.send(f'fujinet listen {other_port} 1 150')
    emu.send('fujinet show')
    emu.wait_frames(3)
    # Leave the manager open while changing the active device profile.
    switch_profile(emu, 'Default')
    emu.close_dialog('Profiles')
    wait_status(emu, lambda s: s['port'] == default_port and s['listening'])
    emu.send(f'fujinet runtime 18000 {tmp_path}/missing runtime')
    assert not emu.send_raw('fujinet start')['ok']
    # Even a failed runtime start must not write the old panel's LAN/port
    # values into the newly selected profile.
    current = emu.send('fujinet status')
    assert current['port'] == default_port and current['listening']
    switch_profile(emu, 'FujiNet other profile')
    restored = wait_status(emu, lambda s: s['port'] == other_port and s['listening'])
    assert restored['present']


def test_panel_resize_fields_and_recovery_buttons_fit(fujinet_desktop, tmp_path):
    emu = fujinet_desktop
    emu.send('set_window_size 1280 900')
    emu.send('fujinet show')
    emu.wait_frames(5)
    assert emu.find_item('FujiNet', 'Disconnect')['disabled']
    assert emu.find_item('FujiNet', 'Boot FujiNet (disable local D1)')['disabled']
    for width, height in [(430, 800), (640, 480), (1280, 900)]:
        emu.send(f'set_window_size {width} {height}')
        emu.wait_frames(8)
        window = emu.send('query_window_label FujiNet')
        assert window['visible']
        assert 0 <= window['x'] < window['x'] + window['w'] <= width
        assert 0 <= window['y'] < window['y'] + window['h'] <= height
        for item in emu.list_items('FujiNet'):
            if item['type'] in ('input', 'checkbox', 'button') and item['label'] != 'FujiNet':
                assert item['x'] >= window['x'], item
                assert item['x'] + item['w'] <= window['x'] + window['w'], item
        # Primary action stays at the top, accessible before scrolling.
        primary = emu.find_item('FujiNet', 'Start and Boot FujiNet')
        assert primary['y'] + primary['h'] <= height
    emu.send('set_window_size 430 800')
    emu.wait_frames(5)
    emu.send('ui_screenshot ' + str(tmp_path / 'fujinet-narrow.png'))


def test_panel_large_font_keeps_primary_action_visible(emu, monkeypatch, tmp_path):
    directory = tmp_path / 'large font config' / 'altirra'
    directory.mkdir(parents=True)
    (directory / 'settings.ini').write_text(
        '[User\\AltirraSDL\\Settings]\n"UI: Font point size tenths" = 320\n')
    monkeypatch.setenv('XDG_CONFIG_HOME', str(directory.parent))
    with AltirraTestHarness(executable=emu.executable,
                           extra_args=['--ui-mode', 'desktop']) as fresh:
        fresh.close_dialog('SetupWizard')
        fresh.send('set_window_size 430 800')
        fresh.send('fujinet show')
        fresh.wait_frames(8)
        window = fresh.send('query_window_label FujiNet')
        primary = next(item for item in fresh.list_items('FujiNet')
                       if '###Start and Boot FujiNet' in item['label'])
        assert '\n' in primary['label']
        assert primary['x'] + primary['w'] <= window['x'] + window['w']
        assert primary['y'] + primary['h'] < 800
        assert fresh.find_item('FujiNet', '##Executable')['w'] > 100
        fresh.send('ui_screenshot ' + str(tmp_path / 'fujinet-large-font.png'))
