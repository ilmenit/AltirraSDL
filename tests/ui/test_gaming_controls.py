"""Exercise Gaming Mode layout and real SDL navigation and touch events."""
import json
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
    first_game = games / "Test Game 00.xex"
    (profile / "gamelibrary.json").write_text(json.dumps({
        "version": 3,
        "games": [{
            "displayName": "Test Game 00",
            "variants": [{"path": str(first_game), "type": "xex",
                          "fileSize": first_game.stat().st_size}],
            "meta": {"status": "useredited", "title": "Test Game 00",
                     "description": "Bring the cargo back to the station. " * 60},
        }],
    }))
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


@pytest.mark.parametrize("width,height",
                         [(1280, 720), (640, 360), (480, 260), (480, 854)])
def test_details_split_full_safe_area(emu, width, height):
    emu.send(f"set_window_size {width} {height}")
    emu.pause()
    wait_for(emu, lambda: (browser := pane(emu, "##GameBrowser"))
             and (browser["w"], browser["h"]) == (width, height)
             and pane(emu, "##gamePreview_"))
    browser = pane(emu, "##GameBrowser")
    library = pane(emu, "##Library_")
    game_list = pane(emu, "##GameList_")
    details = pane(emu, "##gamePreview_")
    body = pane(emu, "##previewBody_")
    assert library and game_list and details and body
    assert pane(emu, "##StatusOverlay") is None
    assert body["h"] >= 60
    assert library["y"] + library["h"] - game_list["y"] >= 48
    play = emu.find_item("##gamePreview_", "Play##preview")
    assert play and play["w"] >= details["w"] * 0.40
    if width > height:
        assert library["y"] == details["y"]
        assert abs(details["w"] - browser["w"] // 2) <= 14
        assert details["h"] >= browser["h"] - 22
    else:
        assert library["x"] == details["x"]
        assert abs(details["h"] - browser["h"] // 2) <= 14
        assert details["w"] >= browser["w"] - 22


def test_short_landscape_library_overflow(emu):
    emu.send("set_window_size 480 260")
    wait_for(emu, lambda: emu.find_item("##Library_", "More##libraryActions"))
    game_list = pane(emu, "##GameList_")
    tile = emu.find_item("##GameList", "##tile_0")
    assert tile["y"] + tile["h"] <= game_list["y"] + game_list["h"]
    assert not emu.find_item("##Library_", "Boot Game")
    assert not emu.find_item("##Library_", "Settings##chrome")

    # Controller Back belongs to the popup and keeps the browser open.
    controller = emu.send("virtual_controller attach gamepad")["index"]
    emu.click("##Library_", "More##libraryActions")
    wait_for(emu, lambda: emu.find_item("##Popup_", "Boot Game"))
    emu.send(f"virtual_controller button {controller} 1 1")
    emu.wait_frames(3)
    emu.send(f"virtual_controller button {controller} 1 0")
    wait_for(emu, lambda: not emu.find_item("##Popup_", "Boot Game"))
    assert pane(emu, "##GameBrowser")
    emu.send(f"virtual_controller detach {controller}")

    # A swipe can reach display options without also activating a row.
    emu.click("##Library_", "More##libraryActions")
    wait_for(emu, lambda: emu.find_item("##Popup_", "Settings"))
    settings = emu.find_item("##Popup_", "Settings")
    x = (settings["x"] + settings["w"] / 2) / 480
    start_y = (settings["y"] + settings["h"] / 2) / 260
    for action, y in (("down", start_y), ("move", .18), ("up", .18)):
        emu.send(f"physical_touch {action} 1 {x} {y} ui")
        emu.wait_frames(4)
    assert pane(emu, "##GameBrowser")
    assert emu.find_item("##Popup_", "Hide Details")

    # A rotated layout should discard the open popup and restore navigation.
    emu.send("set_window_size 360 780")
    wait_for(emu, lambda: not emu.find_item("##Popup_", "Hide Details")
             and emu.find_item("##Library_", "Details##infopill"))
    emu.send("set_window_size 480 260")
    wait_for(emu, lambda: emu.find_item("##Library_", "More##libraryActions"))

    emu.click("##Library_", "More##libraryActions")
    wait_for(emu, lambda: emu.find_item("##Popup_", "Boot Game"))
    emu.click("##Popup_", "Boot Game")
    wait_for(emu, lambda: pane(emu, "##FileBrowser"))


def test_library_preview_scrolls_art_and_synopsis_together(emu):
    width, height = 480, 260
    emu.send(f"set_window_size {width} {height}")
    wait_for(emu, lambda: (body := pane(emu, "##previewBody_")) and body["h"] < 200)
    body = pane(emu, "##previewBody_")
    art = emu.find_item("##previewBody_", "##artnext")
    play = emu.find_item("##gamePreview_", "Play##preview")
    assert art and play

    # Start the drag in the artwork, above the switch and the synopsis.
    x = (body["x"] + body["w"] / 2) / width
    y = (art["y"] - 8) / height
    emu.send(f"physical_touch down 1 {x} {y} ui")
    emu.wait_frames(3)
    emu.send(f"physical_touch move 1 {x} {(body['y'] + 4) / height} ui")
    emu.wait_frames(3)
    moved_art = emu.find_item("##previewBody_", "##artnext")
    assert moved_art and moved_art["y"] < art["y"] - 25
    emu.send(f"physical_touch up 1 {x} {(body['y'] + 4) / height} ui")
    emu.wait_frames(3)
    assert emu.find_item("##gamePreview_", "Play##preview")["y"] == play["y"]


def test_right_stick_scrolls_full_details_with_play_focused(emu):
    emu.send("set_window_size 480 260")
    wait_for(emu, lambda: emu.find_item("##gamePreview_", "Details##preview"))
    controller = emu.send("virtual_controller attach gamepad")["index"]
    emu.click("##gamePreview_", "Details##preview")
    wait_for(emu, lambda: emu.find_item("##detailsBody_", "##artnext"))
    play = emu.find_item("##GameDetails", "Play")
    wait_for(emu, lambda: mobile(emu)["navFocusId"] == play["id"])
    art = emu.find_item("##detailsBody_", "##artnext")
    body = pane(emu, "##detailsBody_")
    assert body["h"] >= 100

    emu.send(f"virtual_controller axis {controller} 3 32767")
    emu.wait_frames(10)
    emu.send(f"virtual_controller axis {controller} 3 0")
    emu.wait_frames(3)
    moved_art = emu.find_item("##detailsBody_", "##artnext")
    assert moved_art is None or moved_art["y"] < art["y"]
    assert mobile(emu)["navFocusId"] == play["id"]
    assert emu.find_item("##GameDetails", "Play")["y"] == play["y"]

    emu.send(f"virtual_controller axis {controller} 3 32767")
    emu.wait_frames(60)
    emu.send(f"virtual_controller axis {controller} 3 0")
    emu.wait_frames(3)
    remove = emu.find_item("##detailsBody_", "Remove from library")
    assert remove and body["y"] <= remove["y"]
    assert remove["y"] + remove["h"] <= body["y"] + body["h"]
    assert mobile(emu)["navFocusId"] == play["id"]
    emu.send(f"virtual_controller detach {controller}")


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

    # A mouse click or a touch on the artwork switch must leave the list
    # ready for the next controller press.
    emu.click("##gamePreview_", "##artnext")
    emu.wait_frames(3)
    assert focus("##tile_0")
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

    # Confirm returns to the focused row before it can launch off-screen.
    input_event("axis", 3, 32767, frames=22)
    list_top = pane(emu, "##GameList_")["y"]
    assert emu.find_item("##GameList", "##game_1")["y"] + first["h"] < list_top
    input_event("button", 0, 1)
    assert not mobile(emu)["gameLoaded"]
    assert emu.find_item("##GameList", "##game_1")["y"] >= list_top
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
    touch("move", 1, .88, .80)
    assert emu.send("mem_read D010")["bytes"] == [0]  # Still in the fire zone.
    touch("move", 1, .50, .68)
    assert emu.send("mem_read D010")["bytes"] == [1]
    touch("move", 1, .90, .82)
    assert emu.send("mem_read D010")["bytes"] == [0]
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


def test_touch_id_reuse_after_rotation(emu):
    emu.send("mobile_hamburger")
    emu.send("mobile_screen none")
    emu.wait_frames(6)
    emu.send("physical_touch down 1 .90 .82")
    emu.wait_frames(3)
    assert emu.send("mem_read D010")["bytes"] == [0]

    emu.send("set_window_size 540 900")
    emu.wait_frames(6)
    assert emu.send("mem_read D010")["bytes"] == [1]
    emu.send("physical_touch down 1 .90 .82")
    emu.wait_frames(3)
    assert emu.send("mem_read D010")["bytes"] == [0]
    emu.send("physical_touch up 1 .90 .82")
    emu.wait_frames(3)
    assert emu.send("mem_read D010")["bytes"] == [1]


@pytest.mark.parametrize("emu", [False], indirect=True)
def test_touch_overlay_respects_saved_idle_preference(emu):
    emu.send("mobile_hamburger")
    emu.send("mobile_screen none")
    wait_for(emu, lambda: not mobile(emu)["touchOverlay"]["console"])
    assert mobile(emu)["touchOverlay"] == dict(gameplay=True, console=False, menu=True)
