"""The real Leave action must release session state before restoring a game."""

import pytest

from .test_netplay_input import emu  # shared isolated, paused emulator fixture


@pytest.mark.parametrize("running", [False, True])
def test_leave_restores_original_held_inputs_and_pause_state(emu, running):
    # Leave is invoked from an already open online-play overlay. Settle
    # its normal input-capture edge before preserving the original game.
    emu.send("netplay_profile prefs")
    emu.wait_frames(4)
    original = emu.send("netplay_input physical 0 0 0 0")
    assert original["rawMask"] == 0
    assert not original["shift"] and not original["ctrl"]
    assert original["console"] == 0
    baseline = emu.send("netplay_checkpoint save before-leave")
    if running:
        emu.send("resume")
    emu.send("netplay_profile begin xl")
    emu.send("netplay_input begin")
    session = emu.send("netplay_input apply 0 0 2 0 0 3 3")
    assert session["active"] and session["rawMask"] == 3
    # This calls the UI's EndSession entry point, unlike the profile-only
    # test helper, so it catches cleanup that runs after checkpoint restore.
    emu.send("netplay_disconnect")
    profile = emu.send("netplay_profile status")
    assert not profile["active"] and profile["restored"]
    assert profile["paused"] is not running
    restored = emu.send("netplay_input status")
    assert not restored["active"]
    for field in ("rawMask", "shift", "ctrl", "console"):
        assert restored[field] == original[field], (field, original, restored)
    if not running:
        assert emu.send("netplay_checkpoint hash") == baseline
    # Deferred coordinator/input cleanup must not clear the restored state
    # on the next ordinary UI/main-loop polls.
    emu.wait_frames(8)
    restored = emu.send("netplay_input status")
    for field in ("rawMask", "shift", "ctrl", "console"):
        assert restored[field] == original[field], (field, original, restored)
    assert emu.send("netplay_profile status")["paused"] is not running
    if not running:
        assert emu.send("netplay_checkpoint hash") == baseline


def test_actual_focus_events_preserve_netplay_capture_and_delayed_release(emu):
    emu.send("netplay_input begin")
    applied = emu.send("netplay_input apply 0 0 2 0 0 3 3")
    assert applied["rawMask"] == 3 and applied["restricted"]
    emu.send("physical_focus lost")
    emu.wait_frames(2)
    lost = emu.send("netplay_input status")
    assert lost["restricted"]
    for field in ("rawMask", "shift", "ctrl", "console"):
        assert lost[field] == applied[field], (field, applied, lost)
    emu.send("physical_focus gained")
    emu.wait_frames(2)
    gained = emu.send("netplay_input status")
    assert gained["restricted"]
    # Loss of focus releases capture-side state. Emulated state changes only
    # when that release is applied through the shared delayed frame input.
    emu.send("netplay_input poll")
    released = emu.send("netplay_input apply_polled 0")
    assert released["rawMask"] == 0 and released["console"] == 0
    assert not released["shift"] and not released["ctrl"]
    ended = emu.send("netplay_input end")
    assert not ended["active"] and not ended["restricted"]
