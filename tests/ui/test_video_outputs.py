"""Real device outputs must remain selectable while emulation is paused."""
import sys
import tempfile
from pathlib import Path

import pytest

from .harness import AltirraTestHarness
from .test_explorers import click

SDK = Path(__file__).resolve().parents[2] / "src/AltirraSDL/AltirraBridge/sdk/python"
sys.path.insert(0, str(SDK))
from altirra_bridge import AltirraBridge


@pytest.fixture
def video_desktop(request, monkeypatch, tmp_path):
    monkeypatch.setenv("SDL_VIDEODRIVER", "dummy")
    monkeypatch.setenv("SDL_AUDIODRIVER", "dummy")
    monkeypatch.setenv("ALTIRRA_DISPLAY_BACKEND", "sdlrenderer")
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "config"))
    with AltirraTestHarness(executable=request.config.getoption("--emu-path"),
                           extra_args=["--bridge"]) as emu:
        emu.close_dialog("SetupWizard")
        emu.wait_frames(3)
        token_file = Path(tempfile.gettempdir()) / f"altirra-bridge-{emu._proc.pid}.token"
        with AltirraBridge.from_token_file(str(token_file)) as bridge:
            bridge.pause()
            emu.send("system_config_category 28")  # Emulator > UI
            emu.wait_frames(3)
            click(emu, "Configure System", "Pause when menus are open")
            emu.close_dialog("SystemConfig")
            emu.wait_frames(3)
            yield emu, bridge


@pytest.mark.skipif(sys.platform == "darwin", reason="Exercises the ImGui menu pause path")
@pytest.mark.parametrize("device", ["xep80", "1090", "bit3", "af80"])
def test_real_video_device_switches_while_paused(video_desktop, device):
    emu, bridge = video_desktop
    bridge.device_add(device)
    if device == "af80":
        # AF80 powers up in 40-column pass-through. D5F6 bit 7 selects
        # the board's 80-column signal, as used by its original ROM.
        bridge.poke(0xD5F6, 0x80)
    # A firmware warning does not prevent enumeration: an unprogrammed
    # output must still replace computer output with a blank signal.
    emu.wait_frames(3)
    state = emu.query_state()["state"]["videoOutput"]
    assert [entry["name"] for entry in state["available"]] == [device]
    assert state["index"] == -1

    click(emu, "##MainMenuBar", "View")
    assert emu.query_state()["state"]["sim"]["menuPaused"]
    emu.send("run_command View.VideoOutputNext")
    emu.wait_frames(3)
    assert emu.query_state()["state"]["sim"]["menuPaused"]
    state = emu.query_state()["state"]["videoOutput"]
    assert state["name"] == device
    assert state["index"] == 0
    assert state["active"]
    assert state["frameWidth"] > 0 and state["frameHeight"] > 0
    assert state["textureWidth"] == state["frameWidth"]
    assert state["textureHeight"] == state["frameHeight"]

    # Cycling includes computer output, even though its core is paused.
    emu.send("run_command View.VideoOutputNext")
    emu.wait_frames(3)
    state = emu.query_state()["state"]["videoOutput"]
    assert state["name"] == ""
    assert not state["active"]
    emu.send("run_command View.VideoOutputPrev")
    emu.wait_frames(3)
    assert emu.query_state()["state"]["videoOutput"]["name"] == device

    # Removing the displayed device releases selection and pixels before
    # the next UI frame, rather than retaining a dangling device pointer.
    bridge.device_remove(device)
    emu.wait_frames(3)
    state = emu.query_state()["state"]["videoOutput"]
    assert state["available"] == []
    assert state["name"] == ""
    assert not state["active"]


@pytest.mark.skipif(sys.platform == "darwin", reason="Exercises the ImGui display path")
@pytest.mark.parametrize("device,address", [("1090", 0xD1FA), ("bit3", 0xD580)])
def test_programmed_video_raster_and_passthrough(video_desktop, device, address):
    emu, bridge = video_desktop
    bridge.device_add(device)
    emu.send("run_command View.ToggleVideoOutputAutoswitching")
    # MC6845/SY6545 register layout: synthetic 80x24 raster, eight
    # scanlines/cell, 114 character clocks/line and 262 lines/frame.
    # This gives a valid NTSC-rate signal at the device's character clock.
    registers = [113, 80, 96, 8, 31, 6, 24, 28, 0, 7, 0x20, 7, 0, 0, 0, 0]
    for index, value in enumerate(registers):
        bridge.poke(address, index)
        bridge.poke(address + 1, value)
    if device == "bit3":
        # bit3.cpp control port: unblank and select the adapter signal.
        bridge.poke(0xD508, 0x18)
    emu.send("run_command View.VideoOutputNext")
    bridge.frame(2)
    bridge.ping()  # FRAME completion barrier, including device Tick().
    emu.wait_frames(3)
    state = emu.query_state()["state"]["videoOutput"]
    assert state["name"] == device and state["active"]
    assert (state["frameWidth"], state["frameHeight"]) == (640, 192)
    assert (state["textureWidth"], state["textureHeight"]) == (640, 192)

    if device == "bit3":
        bridge.poke(0xD508, 0x08)  # retain the raster, pass computer video through
        emu.wait_frames(3)
        state = emu.query_state()["state"]["videoOutput"]
        assert state["name"] == "bit3" and not state["active"]
        assert (state["frameWidth"], state["frameHeight"]) != (640, 192)
