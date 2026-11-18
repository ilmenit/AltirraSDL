"""Exercise Gaming Mode layout and real SDL navigation and touch events."""
import struct
import time

import pytest

from .harness import AltirraTestHarness


@pytest.fixture
def emu(request, monkeypatch, tmp_path):
    games = tmp_path / "games"
    games.mkdir()
    for index in range(30):
        # Write the game number to $4000, then loop.
        (games / f"Test Game {index:02d}.xex").write_bytes(
            struct.pack("<HHH8BHHH", 0xFFFF, 0x2000, 0x2007,
                        0xA9, index, 0x8D, 0x00, 0x40, 0x4C, 0x05, 0x20,
                        0x02E0, 0x02E1, 0x2000))
    config = tmp_path / "config"
    profile = config / "altirra"
    profile.mkdir(parents=True)
    idle_pref = ('"AutoHideTouchOverlay" = 0\n'
                 if getattr(request, "param", None) is False else "")
    (profile / "settings.ini").write_text(
        '[User\\AltirraSDL]\n"ShownSetupWizard" = 1\n'
        '[User\\AltirraSDL\\Mobile]\n"FirstRunComplete" = 1\n'
        f'"ShowTouchControls" = 1\n{idle_pref}'
        '[User\\AltirraSDL\\GameLibrary]\n'
        f'"SourceCount" = 1\n"Source0.Path" = "{games.as_posix()}"\n'
        '"ListSize" = 2\n'
        '[User\\AltirraSDL\\Metadata]\n"FirstRunNudgeShown" = 1\n'
    )
    monkeypatch.setenv("XDG_CONFIG_HOME", str(config))
    monkeypatch.setenv("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1")
    with AltirraTestHarness(executable=request.config.getoption("--emu-path"),
                            startup_timeout=25, timeout=20,
                            extra_args=["--ui-mode", "gaming"]) as harness:
        harness.close_dialog("SetupWizard")
        harness.send("set_window_size 1280 720")
        harness.send("mobile_screen browser")
        harness.send("physical_focus gained")
        yield harness


def mobile(emu):
    return emu.query_state()["state"]["mobile"]


def wait_for(emu, condition, timeout=8):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if condition():
            return
        emu.wait_frames(2)
    assert condition()


def pane(emu, name):
    return next((window for window in emu.query_state()["state"]["windows"]
                 if name in window["name"]), None)


@pytest.mark.parametrize("width,height", [(1280, 720), (640, 360), (480, 854)])
def test_details_split_full_safe_area(emu, width, height):
    emu.send(f"set_window_size {width} {height}")
    wait_for(emu, lambda: (browser := pane(emu, "##GameBrowser"))
             and (browser["w"], browser["h"]) == (width, height)
             and pane(emu, "##gamePreview_"))
    browser = pane(emu, "##GameBrowser")
    library = pane(emu, "##Library_")
    game_list = pane(emu, "##GameList_")
    details = pane(emu, "##gamePreview_")
    text = pane(emu, "##previewText_")
    assert library and game_list and details and text
    assert text["h"] >= 60
    assert library["y"] + library["h"] - game_list["y"] >= 60
    if width > height:
        assert library["y"] == details["y"]
        assert abs(details["w"] - browser["w"] // 2) <= 14
        assert details["h"] >= browser["h"] - 22
    else:
        assert library["x"] == details["x"]
        assert abs(details["h"] - browser["h"] // 2) <= 14
        assert details["w"] >= browser["w"] - 22


def test_controller_sticks_match_navigation_and_scrolling(emu):
    wait_for(emu, lambda: emu.find_item("##GameList", "##tile_1"))
    controller = emu.send("virtual_controller attach gamepad")["index"]
    emu.wait_frames(8)

    def input_event(control, code, value, frames=1):
        emu.send(f"virtual_controller {control} {controller} {code} {value}")
        emu.wait_frames(frames)
        emu.send(f"virtual_controller {control} {controller} {code} 0")
        emu.wait_frames(3)

    def focus(label):
        item = emu.find_item("##GameList", label)
        return item and mobile(emu)["navFocusId"] == item["id"]

    # SDL_gamepad.h defines down/right as 12/14 and LX/LY/RY as 0/1/3.
    emu.click("##GameBrowser", "Grid##view")
    emu.wait_frames(3)
    input_event("button", 12, 1)
    focus_id = mobile(emu)["navFocusId"]
    assert focus("##tile_0"), [item for item in emu.list_items() if item["id"] == focus_id]
    input_event("axis", 0, 32767)
    assert focus("##tile_1")
    input_event("button", 14, 1)
    assert focus("##tile_2")
    input_event("axis", 0, 5000)
    assert focus("##tile_2")

    emu.click("##GameBrowser", "List##view")
    emu.wait_frames(3)
    input_event("axis", 1, 32767)
    assert focus("##game_0")
    input_event("axis", 1, 32767)
    assert focus("##game_1")
    first = emu.find_item("##GameList", "##game_1")
    assert first["h"] >= 96
    input_event("axis", 3, 15000, frames=6)
    assert mobile(emu)["navFocusId"] == first["id"]
    assert emu.find_item("##GameList", "##game_1")["y"] < first["y"]
    input_event("axis", 3, -32768, frames=12)
    input_event("button", 0, 1)
    wait_for(emu, lambda: emu.send("mem_read 4000")["bytes"] == [1])
    emu.send(f"virtual_controller detach {controller}")


def test_touch_overlay_reveals_by_zone_and_ignores_hidden_buttons(emu):
    emu.send("mobile_hamburger")
    emu.send("mobile_screen none")
    emu.wait_frames(6)

    def visibility():
        return mobile(emu)["touchOverlay"]

    def touch(action, finger, x, y):
        emu.send(f"physical_touch {action} {finger} {x} {y}")
        emu.wait_frames(2)

    hidden = dict(gameplay=False, console=False, menu=False)
    gameplay_only = dict(gameplay=True, console=False, menu=False)
    all_visible = dict(gameplay=True, console=True, menu=True)
    wait_for(emu, lambda: visibility() == hidden)
    touch("down", 1, .90, .82)
    assert visibility() == gameplay_only
    assert emu.send("mem_read D010")["bytes"] == [0]  # Fire is active on first touch.
    touch("cancel", 1, .90, .82)
    assert emu.send("mem_read D010")["bytes"] == [1]

    # Reveal above the controls first, then open Menu with a separate tap.
    touch("down", 2, .97, .047)
    touch("up", 2, .97, .047)
    assert visibility() == all_visible
    assert mobile(emu)["currentScreen"] == 0
    touch("down", 3, .97, .047)
    touch("cancel", 3, .97, .047)
    assert mobile(emu)["currentScreen"] == 0
    touch("down", 4, .97, .047)
    touch("up", 4, .97, .047)
    assert mobile(emu)["currentScreen"] != 0


@pytest.mark.parametrize("emu", [False], indirect=True)
def test_touch_overlay_respects_saved_idle_preference(emu):
    emu.send("mobile_hamburger")
    emu.send("mobile_screen none")
    wait_for(emu, lambda: not mobile(emu)["touchOverlay"]["console"])
    assert mobile(emu)["touchOverlay"] == dict(gameplay=True, console=False, menu=True)
