"""Exit preferences use the real menu and persist independently of profiles."""
import pytest

from .harness import AltirraTestHarness


@pytest.fixture
def desktop(emu, tmp_path, monkeypatch):
    monkeypatch.setenv('SDL_VIDEODRIVER', 'dummy')
    monkeypatch.setenv('SDL_AUDIODRIVER', 'dummy')
    monkeypatch.setenv('ALTIRRA_DISPLAY_BACKEND', 'sdlrenderer')
    monkeypatch.setenv('XDG_CONFIG_HOME', str(tmp_path))
    with AltirraTestHarness(executable=emu.executable,
                           extra_args=['--ui-mode', 'desktop']) as instance:
        instance.close_dialog('SetupWizard')
        instance.send('set_window_size 1280 900')
        instance.pause()
        instance.wait_frames(6)
        yield instance


def choose_exit(emu):
    emu.click('##MainMenuBar', 'File')
    emu.wait_frames(6)
    item = next(i for i in emu.list_items() if i['label'] == 'Exit')
    # Do not wait for more UI frames: disabling confirmation exits here.
    emu.click(item['window'], 'Exit')


def test_default_confirmation_can_cancel_with_dirty_media(desktop):
    emu = desktop
    emu.send('netplay_profile disk_create')
    emu.send('netplay_profile disk_write 113')
    choose_exit(emu)
    emu.wait_frames(6)
    cancel = next(i for i in emu.list_items()
                  if i['label'] == 'Cancel' and i['window'].startswith('Confirm Exit'))
    emu.click(cancel['window'], 'Cancel')
    emu.wait_frames(4)
    assert emu.ping()['ok']
    assert emu.send('netplay_profile status')['diskDirty']


@pytest.mark.parametrize('route', ['menu', 'gaming'])
def test_disabled_confirmation_survives_restart_and_exits_with_dirty_media(desktop, route):
    emu = desktop
    emu.navigate_system_config('UI')
    emu.assert_checkbox_state('Configure System', 'Confirm on Exit', True)
    emu.click('Configure System', 'Confirm on Exit')
    emu.wait_frames(6)
    emu.assert_checkbox_state('Configure System', 'Confirm on Exit', False)
    # This is a global preference, unaffected by switching machine profiles.
    profile = emu.send('profile_test create')['id']
    emu.send(f'profile_test activate {profile}')
    emu.wait_frames(4)
    emu.assert_checkbox_state('Configure System', 'Confirm on Exit', False)
    emu.stop()
    with AltirraTestHarness(executable=emu.executable,
                           extra_args=['--ui-mode', 'desktop']) as fresh:
        fresh.close_dialog('SetupWizard')
        # The real sidebar was exercised above. Select its existing page
        # directly here to keep restart checks independent of scroll focus.
        fresh.open_dialog('SystemConfig')
        fresh.send('system_config_category 28')
        fresh.wait_frames(6)
        fresh.assert_checkbox_state('Configure System', 'Confirm on Exit', False)
        fresh.close_dialog('SystemConfig')
        fresh.send('netplay_profile disk_create')
        fresh.send('netplay_profile disk_write 113')
        if route == 'menu':
            choose_exit(fresh)
        else:
            fresh.send('mobile_exit_confirm')
        assert fresh._proc.wait(timeout=5) == 0


def test_confirmation_can_be_reenabled(desktop):
    emu = desktop
    emu.navigate_system_config('UI')
    for _ in range(2):
        emu.click('Configure System', 'Confirm on Exit')
        emu.wait_frames(6)
    emu.close_dialog('SystemConfig')
    choose_exit(emu)
    emu.wait_frames(6)
    cancel = next(i for i in emu.list_items()
                  if i['label'] == 'Cancel' and i['window'].startswith('Confirm Exit'))
    emu.click(cancel['window'], 'Cancel')
    emu.wait_frames(3)
    assert emu.ping()['ok']
