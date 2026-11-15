"""Exercise actual startup markers and next-launch renderer preferences."""
import os
import subprocess
import pytest
from .harness import AltirraTestHarness


def recovery(emu):
    return emu.query_state()['state']['graphicsRecovery']


def choose(emu, label):
    emu.wait_frames(6)
    item = next(item for item in emu.list_items()
                if item['window'].startswith('Graphics recovery')
                and item['label'] == label)
    emu.click(item['window'], label)
    emu.wait_frames(4)


@pytest.fixture
def isolated(request, tmp_path, monkeypatch):
    monkeypatch.setenv('XDG_CONFIG_HOME', str(tmp_path))
    # Several test windows coexist; focus must not suppress virtual pads.
    monkeypatch.setenv('SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS', '1')
    monkeypatch.delenv('ALTIRRA_DISPLAY_BACKEND', raising=False)
    monkeypatch.delenv('ALTIRRA_TEST_GRAPHICS_STARTUP', raising=False)
    monkeypatch.delenv('ALTIRRA_TEST_GRAPHICS_WINDOW', raising=False)
    return request.config.getoption('--emu-path') or AltirraTestHarness._find_executable()


@pytest.mark.parametrize('mode', ['desktop', 'gaming'])
def test_window_interruptions_are_not_renderer_failures(isolated, monkeypatch, mode):
    monkeypatch.setenv('ALTIRRA_TEST_GRAPHICS_WINDOW', 'interrupt')
    for _ in range(3):
        run = subprocess.run([isolated, '--test-mode', '--ui-mode', mode],
                             capture_output=True, timeout=20)
        assert run.returncode == 87, run.stderr.decode(errors='replace')
    monkeypatch.delenv('ALTIRRA_TEST_GRAPHICS_WINDOW')
    with AltirraTestHarness(executable=isolated,
                           extra_args=['--ui-mode', mode]) as emu:
        emu.close_dialog('SetupWizard')
        assert not recovery(emu)['active']
        assert recovery(emu)['preferred'] == 'sdlgpu'
        assert recovery(emu)['actual'] == 'SDL_GPU'


@pytest.mark.parametrize('mode', ['desktop', 'gaming'])
def test_window_wait_is_not_slow_renderer_startup(isolated, monkeypatch, mode):
    monkeypatch.setenv('ALTIRRA_TEST_GRAPHICS_WINDOW', 'slow')
    with AltirraTestHarness(executable=isolated, startup_timeout=30,
                           extra_args=['--ui-mode', mode]) as emu:
        emu.close_dialog('SetupWizard')
        assert not recovery(emu)['active']
        assert recovery(emu)['preferred'] == 'sdlgpu'
        assert emu.ping()['ok']


@pytest.mark.parametrize('mode', ['desktop', 'gaming'])
def test_slow_startup_keep_and_switch(isolated, monkeypatch, mode):
    monkeypatch.setenv('ALTIRRA_TEST_GRAPHICS_STARTUP', 'slow')
    with AltirraTestHarness(executable=isolated, startup_timeout=30,
                           extra_args=['--ui-mode', mode]) as emu:
        emu.close_dialog('SetupWizard')
        assert recovery(emu)['active']
        assert recovery(emu)['preferred'] == 'sdlgpu'
        choose(emu, 'Keep current renderer')
        assert not recovery(emu)['active']
        assert recovery(emu)['preferred'] == 'sdlgpu'
        emu.send('graphics_recovery slow')
        emu.send('screen_effects basic')
        effects = emu.query_state()['state']['display']
        choose(emu, 'Use OpenGL next launch')
        assert recovery(emu)['preferred'] == 'opengl'
        assert emu.query_state()['state']['display'] == effects
        emu.send('quit')
        assert emu._proc.wait(timeout=10) == 0
    monkeypatch.delenv('ALTIRRA_TEST_GRAPHICS_STARTUP')
    with AltirraTestHarness(executable=isolated) as fresh:
        assert recovery(fresh)['preferred'] == 'opengl'
        assert recovery(fresh)['actual'] == 'OpenGL'
        assert fresh.query_state()['state']['display'] == effects
        fresh.send('graphics_recovery slow')
        choose(fresh, 'Use compatibility graphics next launch')
        fresh.send('quit')
        assert fresh._proc.wait(timeout=10) == 0
    with AltirraTestHarness(executable=isolated) as fresh:
        assert recovery(fresh)['preferred'] == 'sdlrenderer'
        assert recovery(fresh)['actual'] == 'SDL_Renderer'
        assert fresh.query_state()['state']['display'] == effects
        assert fresh.ping()['ok']


@pytest.mark.parametrize('mode', ['desktop', 'gaming'])
@pytest.mark.parametrize('attempts', [2, 3])
def test_repeated_interrupted_launches(isolated, monkeypatch, mode, attempts):
    monkeypatch.setenv('ALTIRRA_TEST_GRAPHICS_STARTUP', 'interrupt')
    for _ in range(attempts):
        run = subprocess.run([isolated, '--test-mode', '--ui-mode', mode], env=os.environ,
                             capture_output=True, timeout=20)
        assert run.returncode == 86, run.stderr.decode(errors='replace')
    monkeypatch.delenv('ALTIRRA_TEST_GRAPHICS_STARTUP')
    with AltirraTestHarness(executable=isolated, extra_args=['--ui-mode', mode]) as emu:
        emu.close_dialog('SetupWizard')
        assert recovery(emu)['active']
        assert recovery(emu)['preferred'] == 'sdlgpu'
        if mode == 'gaming':
            assert recovery(emu)['actual'] == ('OpenGL' if attempts == 2 else 'SDL_Renderer')
        choose(emu, 'Try preferred renderer next launch' if mode == 'gaming'
               else 'Keep current renderer')
    with AltirraTestHarness(executable=isolated) as emu:
        assert not recovery(emu)['active']


def test_known_errors_and_override(isolated):
    with AltirraTestHarness(executable=isolated,
                           extra_args=['--renderer', 'opengl']) as emu:
        emu.close_dialog('SetupWizard')
        emu.send('graphics_recovery failed')
        choose(emu, 'Use compatibility graphics next launch')
        assert recovery(emu)['preferred'] == 'sdlrenderer'
        assert recovery(emu)['actual'] == 'OpenGL'
        # The running renderer remains usable; switching only saves preference.
        assert emu.ping()['ok']
    with AltirraTestHarness(executable=isolated) as emu:
        assert recovery(emu)['preferred'] == 'sdlrenderer'
        assert not recovery(emu)['active']


def test_repeated_presentation_errors(isolated):
    with AltirraTestHarness(executable=isolated) as emu:
        emu.close_dialog('SetupWizard')
        emu.send('graphics_recovery failed')
        assert recovery(emu)['active']
        # Simulate process termination while the error prompt is pending.
    with AltirraTestHarness(executable=isolated) as emu:
        emu.close_dialog('SetupWizard')
        assert recovery(emu)['active']
        choose(emu, 'Use compatibility graphics next launch')
    with AltirraTestHarness(executable=isolated) as emu:
        assert recovery(emu)['preferred'] == 'sdlrenderer'
        assert recovery(emu)['actual'] == 'SDL_Renderer'
        assert not recovery(emu)['active']


@pytest.mark.parametrize('mode', ['desktop', 'gaming'])
def test_escape_default_and_small_window(isolated, mode):
    with AltirraTestHarness(executable=isolated,
                           extra_args=['--ui-mode', mode]) as emu:
        emu.close_dialog('SetupWizard')
        emu.send('set_window_size 360 640')
        emu.wait_frames(6)
        emu.send('graphics_recovery slow')
        emu.wait_frames(6)
        items = [i for i in emu.list_items()
                 if i['window'].startswith('Graphics recovery')]
        assert any(i['label'] == 'Keep current renderer' for i in items)
        for item in items:
            if item['type'] == 'button':
                assert item['x'] >= 0 and item['y'] >= 0
                assert item['x'] + item['w'] <= 360
                assert item['y'] + item['h'] <= 640
        emu.send('key escape')
        emu.wait_frames(4)
        assert not recovery(emu)['active']
        assert recovery(emu)['preferred'] == 'sdlgpu'
        emu.send('graphics_recovery slow')
        emu.wait_frames(6)
        emu.send('key enter')
        emu.wait_frames(4)
        assert not recovery(emu)['active']
        assert recovery(emu)['preferred'] == 'sdlgpu'


def test_gaming_controller_navigation(isolated):
    with AltirraTestHarness(executable=isolated,
                           extra_args=['--ui-mode', 'gaming']) as emu:
        emu.close_dialog('SetupWizard')
        index = emu.send('virtual_controller attach gamepad')['index']
        emu.wait_frames(10)
        emu.send('graphics_recovery slow')
        emu.wait_frames(6)

        def button(code):
            emu.send('physical_focus gained')
            emu.wait_frames(2)
            emu.send(f'virtual_controller button {index} {code} 1')
            emu.wait_frames(4)
            emu.send(f'virtual_controller button {index} {code} 0')
            emu.wait_frames(4)

        # SDL_gamepad.h: SOUTH = 0, DPAD_UP = 11.
        button(0)
        assert not recovery(emu)['active']
        assert recovery(emu)['preferred'] == 'sdlgpu'
        emu.send('graphics_recovery slow')
        emu.wait_frames(6)
        button(11)  # Keep -> compatibility in the stacked action order.
        button(0)
        assert not recovery(emu)['active']
        assert recovery(emu)['preferred'] == 'sdlrenderer'
        emu.send(f'virtual_controller detach {index}')


@pytest.mark.parametrize('mode', ['desktop', 'gaming'])
def test_background_resume_is_not_a_graphics_failure(isolated, mode):
    with AltirraTestHarness(executable=isolated,
                           extra_args=['--ui-mode', mode]) as emu:
        emu.close_dialog('SetupWizard')
        emu.pause()
        emu.send('app_lifecycle background')
        assert emu.query_state()['state']['appSuspended']
        emu.send('app_lifecycle foreground')
        emu.wait_frames(6)
        assert not emu.query_state()['state']['appSuspended']
        assert not recovery(emu)['active']
        assert recovery(emu)['preferred'] == 'sdlgpu'
