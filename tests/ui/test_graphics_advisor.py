"""Reversible graphics trials use the real UI and existing settings lifecycle."""
import time
import pytest
from .harness import AltirraTestHarness


def state(emu):
    return emu.query_state()['state']


def choose(emu, prefix, label):
    emu.wait_frames(5)
    item = next(i for i in emu.list_items()
                if i['window'].startswith(prefix) and i['label'] == label)
    emu.click(item['window'], label)
    emu.wait_frames(4)


@pytest.fixture
def launch(request, tmp_path, monkeypatch):
    monkeypatch.setenv('XDG_CONFIG_HOME', str(tmp_path))
    # Several test windows coexist; focus must not suppress virtual pads.
    monkeypatch.setenv('SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS', '1')
    monkeypatch.delenv('ALTIRRA_DISPLAY_BACKEND', raising=False)
    executable = request.config.getoption('--emu-path') or AltirraTestHarness._find_executable()
    children = []

    def start(mode='desktop'):
        emu = AltirraTestHarness(executable=executable,
                               extra_args=['--ui-mode', mode], timeout=20)
        emu.start()
        children.append(emu)
        emu.close_dialog('SetupWizard')
        return emu

    yield start
    for emu in reversed(children):
        emu.stop()


@pytest.mark.parametrize('mode', ['desktop', 'gaming'])
def test_negative_signals_and_effects_restore(launch, mode):
    emu = launch(mode)
    emu.send('screen_effects basic')
    initial = state(emu)['display']
    for signal in ['vsync', 'cpu', 'paused']:
        emu.send('graphics_advisor ' + signal)
        emu.wait_frames(4)
        assert not state(emu)['graphicsAdvisor']['active']
    emu.send('graphics_advisor slow')
    choose(emu, 'Smoother graphics', 'Try screen effects off')
    assert state(emu)['graphicsAdvisor']['trial']
    assert not state(emu)['graphicsAdvisor']['effectiveEffects']
    assert state(emu)['display'] == initial
    choose(emu, 'Graphics trial', 'Restore previous graphics')
    assert not state(emu)['graphicsAdvisor']['trial']
    assert state(emu)['display'] == initial
    assert state(emu)['graphicsAdvisor']['effectiveEffects']
    # No repeat nag during this launch.
    emu.send('graphics_advisor slow')
    emu.wait_frames(4)
    assert not state(emu)['graphicsAdvisor']['active']


@pytest.mark.parametrize('mode', ['desktop', 'gaming'])
def test_accepted_effects_persist(launch, mode):
    emu = launch(mode)
    emu.send('screen_effects basic')
    emu.send('graphics_advisor slow')
    choose(emu, 'Smoother graphics', 'Try screen effects off')
    choose(emu, 'Graphics trial', 'Keep simpler graphics')
    assert not state(emu)['graphicsAdvisor']['effectiveEffects']
    emu.send('quit')
    assert emu._proc.wait(timeout=10) == 0
    fresh = launch(mode)
    assert not state(fresh)['graphicsAdvisor']['effectiveEffects']


def test_filter_trial_profile_and_suspend(launch):
    emu = launch()
    emu.send('run_command View.FilterModeBicubic')
    original = state(emu)['graphicsAdvisor']['storedFilter']
    emu.send('graphics_advisor slow')
    choose(emu, 'Smoother graphics', 'Try bilinear filtering')
    assert state(emu)['graphicsAdvisor']['storedFilter'] == original
    assert state(emu)['graphicsAdvisor']['effectiveFilter'] != original
    emu.send('app_lifecycle background')
    emu.send('app_lifecycle foreground')
    emu.wait_frames(4)
    assert not state(emu)['graphicsAdvisor']['trial']
    assert state(emu)['graphicsAdvisor']['effectiveFilter'] == original


def test_profile_switch_reverts_trial(launch):
    emu = launch()
    other = emu.send('profile_test create')['id']
    emu.send('graphics_advisor slow')
    choose(emu, 'Smoother graphics', 'Try screen effects off')
    emu.send(f'profile_test activate {other}')
    emu.wait_frames(4)
    assert not state(emu)['graphicsAdvisor']['trial']


def test_trial_timeout_and_no_accidental_enter(launch):
    emu = launch()
    emu.send('graphics_advisor slow')
    choose(emu, 'Smoother graphics', 'Try screen effects off')
    emu.send('key enter')  # Default is Restore, never Keep.
    emu.wait_frames(4)
    assert not state(emu)['graphicsAdvisor']['trial']
    emu.send('quit')
    assert emu._proc.wait(timeout=10) == 0
    emu = launch()
    emu.send('graphics_advisor slow')
    choose(emu, 'Smoother graphics', 'Try screen effects off')
    time.sleep(15.2)
    assert not state(emu)['graphicsAdvisor']['trial']
    assert state(emu)['graphicsAdvisor']['effectiveEffects']


@pytest.mark.parametrize('mode', ['desktop', 'gaming'])
def test_filter_keep_survives_restart(launch, mode):
    emu = launch(mode)
    emu.send('run_command View.FilterModeBicubic')
    original = state(emu)['graphicsAdvisor']['storedFilter']
    effects = state(emu)['display']
    emu.send('graphics_advisor slow')
    choose(emu, 'Smoother graphics', 'Try bilinear filtering')
    changed = state(emu)['graphicsAdvisor']['effectiveFilter']
    assert changed != original
    choose(emu, 'Graphics trial', 'Keep simpler graphics')
    assert state(emu)['display'] == effects
    emu.send('quit')
    assert emu._proc.wait(timeout=10) == 0
    fresh = launch(mode)
    assert state(fresh)['graphicsAdvisor']['storedFilter'] == changed


def test_exit_during_trial_preserves_saved_settings(launch):
    emu = launch()
    initial = state(emu)['display']
    emu.send('graphics_advisor slow')
    choose(emu, 'Smoother graphics', 'Try screen effects off')
    emu.send('quit')
    assert emu._proc.wait(timeout=10) == 0
    fresh = launch()
    assert state(fresh)['display'] == initial
    assert state(fresh)['graphicsAdvisor']['effectiveEffects']


def test_temporary_profile_does_not_persist_choice(launch):
    emu = launch('gaming')
    emu.send('profile_test temporary 1')
    emu.send('graphics_advisor slow')
    choose(emu, 'Smoother graphics', 'Try screen effects off')
    choose(emu, 'Graphics trial', 'Keep simpler graphics')
    assert not state(emu)['graphicsAdvisor']['effectiveEffects']
    emu.send('quit')
    assert emu._proc.wait(timeout=10) == 0
    fresh = launch('gaming')
    assert state(fresh)['graphicsAdvisor']['effectiveEffects']


def test_gaming_live_escape_restores(launch):
    emu = launch('gaming')
    emu.send('mobile_hamburger')
    emu.send('mobile_screen none')
    emu.send('graphics_advisor slow')
    choose(emu, 'Smoother graphics', 'Try screen effects off')
    emu.send('key escape')
    emu.wait_frames(4)
    assert not state(emu)['graphicsAdvisor']['trial']
    assert state(emu)['graphicsAdvisor']['effectiveEffects']


def test_gaming_controller_trial_keep(launch):
    emu = launch('gaming')
    index = emu.send('virtual_controller attach gamepad')['index']
    emu.wait_frames(10)
    emu.send('graphics_advisor slow')
    choose(emu, 'Smoother graphics', 'Try screen effects off')
    emu.send('physical_focus gained')
    emu.wait_frames(2)
    # SDL_gamepad.h: DPAD_UP=11, SOUTH=0. Restore is selected initially.
    for code in [11, 0]:
        emu.send(f'virtual_controller button {index} {code} 1')
        emu.wait_frames(4)
        emu.send(f'virtual_controller button {index} {code} 0')
        emu.wait_frames(4)
    assert not state(emu)['graphicsAdvisor']['trial']
    assert not state(emu)['graphicsAdvisor']['effectiveEffects']
    emu.send(f'virtual_controller detach {index}')


@pytest.mark.parametrize('mode', ['desktop', 'gaming'])
def test_trial_controls_fit_after_rotation(launch, mode):
    emu = launch(mode)
    emu.send('set_window_size 360 640')
    emu.send('graphics_advisor slow')
    choose(emu, 'Smoother graphics', 'Try screen effects off')
    for width, height in [(360, 640), (640, 360)]:
        emu.send(f'set_window_size {width} {height}')
        emu.wait_frames(6)
        buttons = [i for i in emu.list_items()
                   if i['window'].startswith('Graphics trial') and i['label']
                   in ('Keep simpler graphics', 'Restore previous graphics')]
        assert len(buttons) == 2
        for button in buttons:
            assert 0 <= button['x'] <= width - button['w']
            assert 0 <= button['y'] <= height - button['h']
    choose(emu, 'Graphics trial', 'Restore previous graphics')
