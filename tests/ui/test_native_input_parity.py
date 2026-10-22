"""Native Altirra input behavior through the SDL event and device paths."""
import tempfile
import threading
from pathlib import Path

import pytest

from .harness import AltirraTestHarness
from .test_video_outputs import AltirraBridge


@pytest.fixture
def emu(request, monkeypatch, tmp_path):
    monkeypatch.setenv("SDL_VIDEODRIVER", "dummy")
    monkeypatch.setenv("SDL_AUDIODRIVER", "dummy")
    monkeypatch.setenv("ALTIRRA_DISPLAY_BACKEND", "sdlrenderer")
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "config"))
    with AltirraTestHarness(executable=request.config.getoption("--emu-path"),
                           extra_args=["--bridge", "--autotest", "--ui-mode", "desktop"]) as app:
        output = (tmp_path / "emulator-stderr.log").open("wb")
        def drain():
            for line in app._proc.stderr:
                output.write(line)
                output.flush()
        threading.Thread(target=drain, daemon=True).start()
        app.close_dialog("SetupWizard")
        app.send("pause")
        yield app


def sim(emu):
    return emu.query_state()["state"]["sim"]


def press(emu, name):
    emu.send(f"physical_key down {name}")
    emu.wait_frames(2)
    emu.send(f"physical_key up {name}")
    emu.wait_frames(2)


def test_hold_key_and_console_capture_toggle_focus_and_reset(emu):
    emu.send("run_command Input.KeyboardLayoutDirect")
    emu.send("hold_keys")
    assert sim(emu)["holdKeys"]
    assert sim(emu)["pendingHeldKey"] == -1
    # L is scan zero in the shared Atari keymap. It must not be confused
    # with the no-key sentinel, and normal key-up must preserve the hold.
    keyboard_before = emu.mem_read(0xD209, 1)
    console_before = emu.mem_read(0xD01F, 1)
    press(emu, "L")
    assert sim(emu)["pendingHeldKey"] == 0
    assert emu.mem_read(0xD209, 1) == keyboard_before
    press(emu, "F2")
    assert sim(emu)["pendingHeldSwitches"] == 1
    assert emu.mem_read(0xD01F, 1) == console_before
    emu.send("physical_focus lost")
    emu.wait_frames(2)
    assert sim(emu)["pendingHeldKey"] == 0
    assert sim(emu)["pendingHeldSwitches"] == 1
    emu.send("physical_focus gained")
    press(emu, "L")
    assert sim(emu)["pendingHeldKey"] == -1
    press(emu, "L")
    emu.send("cold_reset")
    assert sim(emu)["pendingHeldKey"] == -1
    assert sim(emu)["pendingHeldSwitches"] == 0
    # Native OnReset exits collection mode, so normal gameplay keys work.
    assert not sim(emu)["holdKeys"]
    emu.send("hold_keys")
    press(emu, "F3")
    assert sim(emu)["pendingHeldSwitches"] == 2
    emu.send("hold_keys")
    assert not sim(emu)["holdKeys"]
    assert sim(emu)["pendingHeldSwitches"] == 0


@pytest.mark.parametrize("reset", ["cold_reset", "warm_reset"])
def test_reset_applies_pending_selection_and_leaves_collection_mode(emu, reset):
    emu.send("run_command Input.KeyboardLayoutDirect")
    emu.send("hold_keys")
    press(emu, "L")
    press(emu, "F2")
    assert sim(emu)["pendingHeldKey"] == 0
    assert sim(emu)["pendingHeldSwitches"] == 1
    emu.send(reset)
    assert not sim(emu)["holdKeys"]
    assert sim(emu)["pendingHeldKey"] == -1
    assert sim(emu)["pendingHeldSwitches"] == 0
    press(emu, "J")  # New gameplay input must not start a new pending hold.
    assert sim(emu)["pendingHeldKey"] == -1


def test_natural_text_collects_pending_reset_key(emu):
    emu.send("hold_keys")
    emu.send("send_text l")
    emu.wait_frames(3)
    assert sim(emu)["pendingHeldKey"] == 0


def test_hold_collection_does_not_mutate_netplay_simulator(emu):
    emu.send("netplay_input begin")
    try:
        emu.send("hold_keys")
        assert not sim(emu)["holdKeys"]
        press(emu, "L")
        assert sim(emu)["pendingHeldKey"] == -1
    finally:
        emu.send("netplay_input end")


def test_existing_device_history_tracks_main_setting(emu):
    token = Path(tempfile.gettempdir()) / f"altirra-bridge-{emu._proc.pid}.token"
    with AltirraBridge.from_token_file(str(token)) as bridge:
        bridge.device_add("diskdrive1050")
        emu.wait_frames(2)
        for enabled in (True, False, True):
            emu.send(f"history_enabled {int(enabled)}")
            histories = sim(emu)["deviceHistory"]
            assert histories, "Full disk drive must expose a CPU history target"
            assert all(value == enabled for value in histories)


def test_autotest_exit_uses_normal_shutdown(emu):
    # The command only exists when --autotest is supplied. Its exit request
    # must get out of the debugger dispatch before normal cleanup begins.
    emu.send("debugger_console .autotest_exit")
    assert emu._proc.wait(timeout=8) == 0


def test_real_tablet_bounds_and_pointer_overlays_render(emu, tmp_path):
    from PIL import Image

    # This installs a real PowerPad controller through ATInputManager and
    # drives its normal mouse-pad mappings. No synthetic overlay is drawn.
    state = emu.send("pointer_overlay setup")
    assert state["nonBeam"] and state["pointers"] == 1
    emu.send("resume")
    emu.wait_frames(5)
    emu.send("pause")
    assert emu.send("pointer_overlay query")["displayRect"][2] > 0

    def count_overlay(name):
        emu.wait_frames(3)
        path = tmp_path / f"{name}.png"
        emu.send(f"ui_screenshot {path}")
        image = Image.open(path).convert("RGB")
        # The native tablet indicator color, absent from the black idle
        # Atari background. Compare deltas to allow unrelated UI pixels.
        pixels = (image.get_flattened_data() if hasattr(image, "get_flattened_data")
                  else image.getdata())
        return sum(pixel == (224, 228, 255) for pixel in pixels)

    baseline = count_overlay("none")
    emu.send("pointer_overlay bounds 1")
    bounds = count_overlay("bounds")
    assert bounds > baseline + 300
    emu.send("pointer_overlay bounds 0")
    emu.send("pointer_overlay pointers 1")
    pointers = count_overlay("pointers")
    assert pointers > baseline + 5
    emu.send("pointer_overlay pointers 0")
    assert count_overlay("none-again") == baseline


def test_debugger_tablet_overlay_uses_embedded_image_bounds(emu, tmp_path):
    from PIL import Image

    emu.send("pointer_overlay setup")
    emu.send("resume")
    emu.wait_frames(5)
    emu.send("pause")
    full = emu.send("pointer_overlay query")["displayRect"]
    emu.send("run_command Debug.ToggleDebugger")
    emu.wait_frames(8)
    rect = emu.send("pointer_overlay query")["displayRect"]
    x, y, width, height = rect
    assert width > 0 and height > 0
    assert width < full[2], (full, rect)
    # Focus the actual docked image instead of a tool pane. The overlay must
    # remain drawable even though ImGui owns the Display window's mouse.
    emu.send(f"click_at {x + width / 2} {y + height / 2}")
    emu.wait_frames(5)

    def frame(name):
        path = tmp_path / f"{name}.png"
        emu.send(f"ui_screenshot {path}")
        return Image.open(path).convert("RGB")

    before = frame("debugger-no-bounds")
    emu.send("pointer_overlay bounds 1")
    emu.wait_frames(3)
    after = frame("debugger-bounds")
    before_pixels = before.load()
    after_pixels = after.load()
    added = [(px, py) for py in range(after.height) for px in range(after.width)
             if after_pixels[px, py] == (224, 228, 255)
             and before_pixels[px, py] != (224, 228, 255)]
    assert len(added) > 100
    assert all(x - 1 <= px < x + width + 1 and y - 1 <= py < y + height + 1
               for px, py in added), (rect, min(added), max(added))
