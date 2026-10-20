"""Behavioral speed/pause regressions through the actual desktop loop."""
import time

import pytest

from .harness import AltirraTestHarness
from .test_explorers import click


@pytest.fixture
def speed_desktop(request, monkeypatch, tmp_path):
    monkeypatch.setenv("SDL_VIDEODRIVER", "dummy")
    monkeypatch.setenv("SDL_AUDIODRIVER", "dummy")
    monkeypatch.setenv("ALTIRRA_DISPLAY_BACKEND", "sdlrenderer")
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "config"))
    config = tmp_path / "config/altirra"
    config.mkdir(parents=True)
    (config / "settings.ini").write_text(
        '[User\\AltirraSDL\\Settings]\n"UI: Pause during menus" = 1\n')
    with AltirraTestHarness(executable=request.config.getoption("--emu-path"),
                           extra_args=["--pacing=realtime"]) as emu:
        emu.close_dialog("SetupWizard")
        emu.resume()
        emu.send("set_speed normal")
        emu.wait_frames(4)
        yield emu


def state(emu):
    return emu.query_state()["state"]["sim"]


def test_arbitrary_speed_controls_and_restart(speed_desktop):
    emu = speed_desktop
    emu.open_dialog("SystemConfig")
    emu.wait_frames(3)
    click(emu, "Configure System", "Speed")
    labels = emu.get_item_labels("Configure System")
    assert "Slow Motion" not in labels
    assert any(item["label"] == "Speed" and item["type"] != "button"
               for item in emu.list_items("Configure System"))
    assert "Preset" in labels
    slider = next(item for item in emu.list_items("Configure System")
                  if item["label"] == "Speed" and item["type"] != "button")
    emu.send("physical_key down Left Ctrl")
    emu.wait_frames(2)
    emu.send(f"click_at {slider['x'] + slider['w'] / 3} "
             f"{slider['y'] + slider['h'] / 2}")
    emu.wait_frames(4)
    emu.send("physical_key up Left Ctrl")
    emu.send("key ctrl+a")
    emu.wait_frames(3)
    emu.send("send_text 37")
    emu.send("key enter")
    emu.wait_frames(4)
    assert state(emu)["speedPercent"] == pytest.approx(37, abs=0.001)
    for percentage in (1, 25, 37.5, 75, 100, 125, 500, 800):
        assert emu.send(f"set_speed {percentage}")["ok"]
        assert state(emu)["speedPercent"] == pytest.approx(percentage, abs=0.001)
    for invalid in ("nan", "inf", "0", "-1", "801", "50junk"):
        assert not emu.send_raw("set_speed " + invalid)["ok"]
    # Upstream settings store speed in whole percentage points.
    emu.send("set_speed 37")
    emu.send("quit")
    assert emu._proc.wait(timeout=10) == 0
    with AltirraTestHarness(executable=emu.executable,
                           extra_args=["--pacing=realtime"]) as reopened:
        assert state(reopened)["speedPercent"] == pytest.approx(37, abs=0.001)
        reopened.send("quit")
        assert reopened._proc.wait(timeout=10) == 0


def test_slow_speed_keeps_ui_responsive_and_scales_core(speed_desktop):
    emu = speed_desktop
    def elapsed_ticks(percentage):
        emu.send(f"set_speed {percentage}")
        time.sleep(0.15)
        before = state(emu)["emulationTick"]
        time.sleep(1.1)
        return state(emu)["emulationTick"] - before
    normal = elapsed_ticks(100)
    quarter = elapsed_ticks(25)
    assert normal > 0
    assert 0.17 < quarter / normal < 0.35
    emu.send("set_speed 1")
    time.sleep(0.1)
    start = time.monotonic()
    for _ in range(5):
        assert emu.ping()["ok"]
    assert time.monotonic() - start < 0.5
    emu.send("set_speed normal")


def test_menu_pause_preserves_run_state_and_releases(speed_desktop):
    emu = speed_desktop
    click(emu, "##MainMenuBar", "File")
    before = state(emu)
    assert before["menuOpen"] and before["menuPaused"] and before["running"]
    time.sleep(0.15)
    assert state(emu)["emulationTick"] == before["emulationTick"]
    emu.send("key escape")
    emu.wait_frames(4)
    after = state(emu)
    assert not after["menuOpen"] and not after["menuPaused"]
    time.sleep(0.1)
    assert state(emu)["emulationTick"] > before["emulationTick"]
    emu.pause()
    click(emu, "##MainMenuBar", "View")
    emu.send("key escape")
    emu.wait_frames(4)
    assert state(emu)["paused"]


def test_disabled_menu_pause_keeps_running(speed_desktop):
    emu = speed_desktop
    emu.send("system_config_category 28")  # Emulator > UI
    emu.wait_frames(3)
    click(emu, "Configure System", "Pause when menus are open")
    emu.close_dialog("SystemConfig")
    emu.wait_frames(3)
    click(emu, "##MainMenuBar", "File")
    before = state(emu)
    assert before["menuOpen"] and not before["menuPaused"]
    time.sleep(0.15)
    assert state(emu)["emulationTick"] > before["emulationTick"]
    emu.send("key escape")
