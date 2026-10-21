"""Suspend checkpoints survive process loss and failed replacement."""
import struct
import sys
import time
import zipfile
from pathlib import Path

import pytest

from .harness import AltirraTestHarness


@pytest.fixture
def suspend_environment(request, monkeypatch, tmp_path):
    if sys.platform != "darwin":
        monkeypatch.setenv("SDL_VIDEODRIVER", "dummy")
        monkeypatch.setenv("SDL_AUDIODRIVER", "dummy")
        monkeypatch.setenv("ALTIRRA_DISPLAY_BACKEND", "sdlrenderer")
    config = tmp_path / "config"
    config.mkdir()
    application_config = config / "altirra"
    application_config.mkdir()
    (application_config / "settings.ini").write_text(
        '[User\\AltirraSDL]\n"ShownSetupWizard" = 1\n'
        '[User\\AltirraSDL\\Mobile]\n"FirstRunComplete" = 1\n'
    )
    monkeypatch.setenv("XDG_CONFIG_HOME", str(config))
    # Write a recognizable RAM byte, then loop without touching it again.
    program = b"\xa9\x5a\x8d\x00\x40\x4c\x05\x20"
    image = tmp_path / "resume-game.xex"
    image.write_bytes(b"\xff\xff" + struct.pack("<HH", 0x2000, 0x2007)
                      + program + struct.pack("<HH", 0x02E0, 0x02E1)
                      + b"\x00\x20")
    return request.config.getoption("--emu-path"), image


def launch(executable):
    emu = AltirraTestHarness(executable=executable)
    emu.start()
    emu.close_dialog("SetupWizard")
    return emu


def boot_game(emu, image):
    emu.send("mobile_hamburger")  # marks a loaded Gaming Mode session
    emu.send("mobile_screen none")
    emu.send("boot_image " + str(image))
    emu.resume()
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if emu.send("mem_read 4000")["bytes"] == [0x5A]:
            return
        emu.wait_frames(5)
    pytest.fail("XEX did not reach its checkpoint marker")


@pytest.mark.parametrize("paused", [False, True])
def test_executable_suspend_survives_process_loss(suspend_environment, paused):
    executable, image = suspend_environment
    first = launch(executable)
    try:
        boot_game(first, image)
        if paused:
            first.pause()
        first.send("suspend_state save")
        slot = Path(first.send("suspend_state path")["path"])
        assert slot.parent.name == "saves"
        assert slot.name.startswith("resume-game-")
        with zipfile.ZipFile(slot) as archive:
            assert archive.testzip() is None
        # Bypass every exit handler: only suspend-time persistence counts.
        first._proc.kill()
        first._proc.wait(timeout=5)
    finally:
        first.stop()

    second = launch(executable)
    try:
        # A new process has no XEX mounted and no in-memory boot identity.
        second.send("mobile_screen browser")
        assert second.send("suspend_state restore")["restored"]
        assert second.send("mem_read 4000")["bytes"] == [0x5A]
        assert second.send("suspend_state path")["path"] == str(slot)
        state = second.query_state()["state"]
        assert state["mobile"]["gameLoaded"]
        assert state["mobile"]["currentScreen"] == 0
        assert state["sim"]["paused"] is paused
        assert state["sim"]["running"] is (not paused)
    finally:
        second.stop()


def test_failed_suspend_save_keeps_previous_checkpoint(suspend_environment):
    executable, image = suspend_environment
    emu = launch(executable)
    try:
        boot_game(emu, image)
        emu.pause()
        emu.send("suspend_state save")
        slot = Path(emu.send("suspend_state path")["path"])
        previous = slot.read_bytes()
        temporary = Path(str(slot) + ".tmp")
        temporary.mkdir()  # serialization cannot open this as a file
        emu.send("suspend_state save")
        assert slot.read_bytes() == previous
        temporary.rmdir()
        # Replacement of an existing checkpoint also succeeds.
        emu.send("suspend_state save")
        assert not temporary.exists()
        with zipfile.ZipFile(slot) as archive:
            assert archive.testzip() is None
        assert emu.send("suspend_state restore")["restored"]
        assert emu.get_sim_state()["paused"]
    finally:
        emu.stop()


@pytest.mark.parametrize("paused", [False, True])
def test_background_foreground_preserves_run_state(suspend_environment, paused):
    executable, image = suspend_environment
    emu = launch(executable)
    try:
        boot_game(emu, image)
        if paused:
            emu.pause()
        emu.send("app_lifecycle background")
        deadline = time.monotonic() + 3
        while True:
            state = emu.query_state()["state"]
            if state["appSuspended"] or time.monotonic() >= deadline:
                break
            time.sleep(0.01)
        assert state["appSuspended"]
        tick = state["sim"]["emulationTick"]
        # Rendering intentionally stops, so wait_frames cannot be used here.
        time.sleep(0.3)
        assert emu.get_sim_state()["emulationTick"] == tick
        emu.send("app_lifecycle foreground")
        emu.wait_frames(5)
        state = emu.query_state()["state"]
        assert not state["appSuspended"]
        assert state["sim"]["paused"] is paused
        if paused:
            assert state["sim"]["emulationTick"] == tick
        else:
            assert state["sim"]["emulationTick"] > tick
    finally:
        emu.stop()


@pytest.mark.parametrize("paused", [False, True])
def test_suspend_drops_temporary_menu_pause(suspend_environment, paused):
    executable, image = suspend_environment
    emu = launch(executable)
    try:
        boot_game(emu, image)
        if paused:
            emu.pause()
        emu.send("physical_key down Escape")
        emu.send("physical_key up Escape")
        emu.wait_frames(2)
        assert emu.query_state()["state"]["mobile"]["currentScreen"] == 1
        assert emu.get_sim_state()["paused"]
        emu.send("suspend_state save")
        assert emu.send("suspend_state restore")["restored"]
        assert emu.query_state()["state"]["mobile"]["currentScreen"] == 0
        assert emu.get_sim_state()["paused"] is paused
    finally:
        emu.stop()
