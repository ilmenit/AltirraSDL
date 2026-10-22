"""Per-host-game controller layout uses real editors and registry storage."""
from contextlib import contextmanager
import re
import threading

import pytest

from .harness import AltirraTestHarness
from .test_explorers import enter

SEPARATE = "Two joysticks - one per player"
SHARED = "Shared joystick - take turns"


def click(emu, label, window=""):
    item = emu.find_item(window, label)
    assert item is not None, (label, emu.get_item_labels())
    emu.click(item["window"], label)
    emu.wait_frames(5)


def saved_layout(path, index=0):
    text = path.read_text()
    match = re.search(rf'^"Game{index}_controllerLayout"\s*=\s*(\d+)', text, re.M)
    assert match, text
    return int(match[1])


def choose_file(emu, image):
    emu.send("file_dialog_builtin on")
    click(emu, "File…##addoffer")
    enter(emu, "Open File", "##path", image.parent, True)
    enter(emu, "Open File", "##filename", image.name, True)


@pytest.fixture
def configured(request, monkeypatch, tmp_path):
    directory = tmp_path / "config" / "altirra"
    directory.mkdir(parents=True)
    settings = directory / "settings.ini"
    # An explicitly disabled local endpoint prevents public lobby activity.
    (directory / "lobby.ini").write_text(
        "[private-test]\nname=Private test\nurl=http://127.0.0.1:1\nenabled=false\n")
    monkeypatch.setenv("XDG_CONFIG_HOME", str(directory.parent))
    monkeypatch.setenv("SDL_VIDEODRIVER", "dummy")
    monkeypatch.setenv("SDL_AUDIODRIVER", "dummy")
    monkeypatch.setenv("ALTIRRA_DISPLAY_BACKEND", "sdlrenderer")

    def seed(layout=0, hardware=1, index=0, second_layout=None):
        # Hardware values are from constants.h: 800XL=1, 5200=2.
        settings.write_text(
            '[User\\AltirraSDL]\n"ShownSetupWizard" = 1\n'
            '[User\\AltirraSDL\\Netplay]\n"Handle" = "Controls test"\n'
            f'"GameCount" = {index + 1}\n"Game{index}_id" = "saved-game"\n'
            f'"Game{index}_path" = "{tmp_path / "game.xex"}"\n'
            f'"Game{index}_name" = "Saved game"\n'
            f'"Game{index}_hwMode" = {hardware}\n'
            f'"Game{index}_controllerLayout" = {layout}\n')
        if second_layout is not None:
            text = settings.read_text().replace('"GameCount" = 1', '"GameCount" = 2')
            settings.write_text(text +
                '"Game1_id" = "second-game"\n"Game1_name" = "Second saved game"\n'
                f'"Game1_path" = "{tmp_path / "game.xex"}"\n'
                f'"Game1_hwMode" = {hardware}\n'
                f'"Game1_controllerLayout" = {second_layout}\n')

    @contextmanager
    def start(mode="desktop"):
        with AltirraTestHarness(executable=request.config.getoption("--emu-path"),
                               extra_args=["--ui-mode", mode]) as emu:
            log = (tmp_path / "emulator.log").open("ab")
            def drain():
                for line in emu._proc.stderr:
                    log.write(line)
                    log.flush()
            threading.Thread(target=drain, daemon=True).start()
            emu.close_dialog("SetupWizard")
            emu.send("set_window_size 1280 1000")
            emu.send("pause")
            # The real Leave path opens Host Games and saves retained offers.
            emu.send("netplay_disconnect")
            emu.wait_frames(6)
            yield emu, settings

    return seed, start


def test_existing_host_game_layout_saves_and_survives_restart(configured):
    seed, start = configured
    seed()
    with start() as (emu, settings):
        click(emu, "Controls...")
        click(emu, "Controller layout")
        click(emu, SHARED)
        assert saved_layout(settings) == 1
    with start() as (emu, settings):
        # Leave/save serializes the loaded choice, proving restart restored it.
        assert saved_layout(settings) == 1
        click(emu, "Controls...")
        click(emu, "Controller layout")
        click(emu, SEPARATE)
        assert saved_layout(settings) == 0


@pytest.mark.parametrize("layout,hardware", [(77, 1), (-1, 1), (257, 1), (1, 2)])
def test_invalid_or_5200_layout_normalizes_to_separate(configured, layout, hardware):
    seed, start = configured
    seed(layout, hardware)
    with start() as (emu, settings):
        assert saved_layout(settings) == 0
        if hardware == 2:
            click(emu, "Controls...")
            assert emu.find_item("", "Controller layout")["disabled"]


def test_editing_controls_cannot_duplicate_an_existing_offer(configured):
    seed, start = configured
    seed(0, second_layout=1)
    with start() as (emu, settings):
        click(emu, "Controls...")
        click(emu, "Controller layout")
        click(emu, SHARED)
        # Rejection keeps the editor open and retains its original choice.
        assert emu.find_item("", "Controller layout")
        emu.send("key escape")
        emu.wait_frames(3)
        # Force serialization of live state, rather than trusting old disk data.
        emu.send("netplay_disconnect")
        assert saved_layout(settings, 0) == 0
        assert saved_layout(settings, 1) == 1


def test_highest_saved_game_index_retains_full_layout_key(configured):
    seed, start = configured
    seed(1, index=255)
    with start() as (_, settings):
        # Invalid empty slots are compacted on load; the surviving game moves
        # from 255 to 0; its untruncated controllerLayout key must survive.
        assert saved_layout(settings) == 1


def test_gaming_hosted_game_controls_use_the_shared_editor(configured):
    seed, start = configured
    seed()
    with start("gaming") as (emu, settings):
        click(emu, "Controls...")
        click(emu, "Controller layout:  " + SEPARATE)
        click(emu, SHARED)
        assert saved_layout(settings) == 1


def test_gaming_hardware_change_normalizes_shared_layout_immediately(configured):
    seed, start = configured
    seed(1)
    with start("gaming") as (emu, _):
        click(emu, "+ Add Game")
        assert emu.find_item("", "Controller layout:  " + SHARED)
        click(emu, "Hardware:  Atari 800XL")
        click(emu, "Atari 5200")
        separate = emu.find_item("", "Controller layout:  " + SEPARATE)
        assert separate is not None and separate["disabled"], separate


def test_copy_current_machine_preserves_add_game_controller_choice(configured, tmp_path):
    seed, start = configured
    seed()
    with start() as (emu, settings):
        current_basic = emu.send("query_command System.ToggleBASIC")["state"] == "checked"
        click(emu, "Add Game...")
        # Stage the opposite BASIC choice so Copy must visibly change it;
        # retaining the layout alone must not pass if Copy was never clicked.
        if emu.find_item("", "BASIC enabled")["checked"] == current_basic:
            click(emu, "BASIC enabled")
        click(emu, "Controller layout")
        click(emu, SHARED)
        body = next(w for w in emu.query_state()["state"]["windows"]
                    if "##addGameBody" in w["name"])
        emu.send(f"click_at {body['x'] + body['w'] - 24} {body['y'] + 8}")
        emu.send("key end")
        emu.wait_frames(5)
        click(emu, "Copy from current emulator")
        assert emu.find_item("", "BASIC enabled")["checked"] == current_basic
        emu.send(f"click_at {body['x'] + body['w'] - 24} {body['y'] + 8}")
        emu.send("key home")
        emu.wait_frames(5)
        image = tmp_path / "new hosted game.xex"
        image.write_bytes(b"\xff\xff\x00\x20\x00\x20\x60")
        choose_file(emu, image)
        click(emu, "Add Game")
        emu.send("netplay_disconnect")
        assert saved_layout(settings, 1) == 1


def test_long_game_path_allows_distinct_separate_and_shared_offers(configured, tmp_path):
    seed, start = configured
    seed()
    folder = tmp_path
    for index in range(7):
        folder /= f"long but valid game collection folder {index}"
    folder.mkdir(parents=True)
    image = folder / "same game with different controller layouts.xex"
    image.write_bytes(b"\xff\xff\x00\x20\x00\x20\x60")
    assert len(str(image).encode()) > 384
    with start() as (emu, settings):
        for index, layout in ((1, SEPARATE), (2, SHARED)):
            click(emu, "Add Game...")
            choose_file(emu, image)
            click(emu, "Controller layout")
            click(emu, layout)
            click(emu, "Add Game")
            # This must return to the host list, not duplicate-game Error.
            assert emu.find_item("", "Add Game..."), emu.get_item_labels()
            emu.send("netplay_disconnect")
            assert saved_layout(settings, index) == index - 1
