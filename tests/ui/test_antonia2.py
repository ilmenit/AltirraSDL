"""Antonia 2 device placement, profile persistence, and CPU-setting migration."""
import json
import os

import pytest

from .harness import AltirraTestHarness
from .test_explorers import click


LABEL = "Antonia 2 MULDIV block"
TAG = "antonia2-muldiv"
LEGACY = "CPU: Antonia2 MULDIV Block"


def devices(emu):
    return json.loads(emu.get_sim_state()["devices"])


def assert_enabled(emu, enabled):
    assert sum(d["tag"] == TAG for d in devices(emu)) == int(enabled)


def device_page(emu):
    emu.close_dialog("SetupWizard")
    emu.navigate_system_config("Devices")


def remove_block(emu):
    device_page(emu)
    click(emu, "Configure System", LABEL)
    assert emu.find_item("Configure System", "Settings...")["disabled"]
    # Device rows also have a Remove button. Use the bottom action for the
    # selected device rather than the first row's identically named button.
    button = max((item for item in emu.list_items("Configure System")
                  if item["label"] == "Remove"), key=lambda item: item["y"])
    emu.send(f"click_at {button['x'] + button['w'] / 2} "
             f"{button['y'] + button['h'] / 2}")
    emu.wait_frames(6)
    assert_enabled(emu, False)
    assert LABEL not in emu.get_item_labels("Configure System")


def quit_and_restart(emu):
    emu.close_dialog("SystemConfig")
    emu.send("quit")
    assert emu._proc.wait(timeout=10) == 0
    emu.stop()
    emu.start()
    emu.wait_frames(5)
    device_page(emu)


def test_antonia2_device_profile_restart(emu):
    emu.send("set_window_size 1280 900")
    emu.navigate_system_config("CPU")
    labels = emu.get_item_labels("Configure System")
    assert LABEL not in labels
    assert "Antonia2 MULDIV Block" not in labels
    click(emu, "Configure System", "65C816 (1.79MHz)")

    device_page(emu)
    assert_enabled(emu, False)
    click(emu, "Configure System", "Add Device...")
    click(emu, "", "Internal devices")
    click(emu, "", LABEL)
    assert_enabled(emu, True)
    assert LABEL in emu.get_item_labels("Configure System")

    original = emu.query_state()["state"]["profiles"]["current"]
    emu.close_dialog("SystemConfig")
    profile = emu.send("profile_test create")["id"]
    emu.send(f"profile_test activate {profile}")
    assert_enabled(emu, True)
    remove_block(emu)
    emu.close_dialog("SystemConfig")

    emu.send(f"profile_test activate {original}")
    assert_enabled(emu, True)
    emu.send(f"profile_test activate {profile}")
    assert_enabled(emu, False)
    emu.send(f"profile_test activate {original}")
    assert_enabled(emu, True)

    quit_and_restart(emu)
    assert_enabled(emu, True)
    remove_block(emu)
    quit_and_restart(emu)
    assert_enabled(emu, False)


def write_profile_ini(path, profiles):
    lines = ['[User\\AltirraSDL\\Profiles]',
             '"Defaults inited" = 1', '"Current profile" = 0']
    for profile_id, values in profiles.items():
        lines.append(f'[User\\AltirraSDL\\Profiles\\{profile_id:08X}]')
        for key, value in values.items():
            lines.append(f'{json.dumps(key)} = {json.dumps(value)}')
    path.write_text('\n'.join(lines) + '\n')


@pytest.mark.skipif(os.name == "nt", reason="Private INI fixture is POSIX-only")
@pytest.mark.parametrize("root_enabled", [False, True])
def test_antonia2_legacy_profile_migration(request, monkeypatch, tmp_path,
                                           root_enabled):
    config = tmp_path / "altirra"
    config.mkdir()
    ini = config / "settings.ini"
    # Hardware and Devices may be inherited from different ancestors. Include
    # unsaved categories, grandchildren, and cycles handled by the loader.
    write_profile_ini(ini, {
        0: {LEGACY: int(root_enabled),
            "Devices": json.dumps([{"tag": "rtime8"}])},
        1: {LEGACY: int(not root_enabled), "_Category Mask": "hardware",
            "_Saved Category Mask": "hardware"},
        2: {LEGACY: int(root_enabled), "_Category Mask": "hardware",
            "_Saved Category Mask": "hardware"},
        3: {"_Category Mask": "devices", "_Saved Category Mask": "devices",
            "Devices": "[]"},
        4: {LEGACY: int(not root_enabled), "_Category Mask": "hardware",
            "_Saved Category Mask": ""},
        5: {"_Parent": 1, "_Category Mask": "devices",
            "_Saved Category Mask": "devices", "Devices": "[]"},
        6: {"_Parent": 1, LEGACY: int(root_enabled),
            "_Category Mask": "hardware", "_Saved Category Mask": "hardware"},
        7: {"_Parent": 1},
        8: {"_Parent": 9, LEGACY: int(not root_enabled),
            "_Category Mask": "hardware", "_Saved Category Mask": "hardware"},
        9: {"_Parent": 8, LEGACY: int(not root_enabled),
            "_Category Mask": "hardware", "_Saved Category Mask": "hardware"},
    })
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path))
    with AltirraTestHarness(executable=request.config.getoption("--emu-path")) as emu:
        emu.close_dialog("SetupWizard")
        emu.send("set_window_size 1280 900")
        for profile, enabled, has_clock in [
                (0, root_enabled, True), (1, not root_enabled, True),
                (2, root_enabled, True), (3, root_enabled, False),
                (4, root_enabled, True), (5, not root_enabled, False),
                (6, root_enabled, True), (7, not root_enabled, True),
                (8, not root_enabled, True), (9, not root_enabled, True)]:
            emu.send(f"profile_test activate {profile}")
            assert_enabled(emu, enabled)
            assert any(d["tag"] == "rtime8" for d in devices(emu)) == has_clock
        emu.send("profile_test activate 0")
        quit_and_restart(emu)
        assert_enabled(emu, root_enabled)
        assert LEGACY not in ini.read_text()
        if root_enabled:
            remove_block(emu)
            quit_and_restart(emu)
            assert_enabled(emu, False)


@pytest.mark.skipif(os.name == "nt", reason="Private INI fixture is POSIX-only")
@pytest.mark.parametrize("legacy_enabled", [0, 1])
def test_antonia2_migration_does_not_duplicate_device(request, monkeypatch,
                                                     tmp_path, legacy_enabled):
    config = tmp_path / "altirra"
    config.mkdir()
    ini = config / "settings.ini"
    write_profile_ini(ini, {
        0: {LEGACY: legacy_enabled, "Devices": json.dumps([{"tag": TAG}])},
    })
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path))
    with AltirraTestHarness(executable=request.config.getoption("--emu-path")) as emu:
        emu.close_dialog("SetupWizard")
        assert_enabled(emu, True)
        remove_block(emu)
        quit_and_restart(emu)
        assert_enabled(emu, False)
        assert LEGACY not in ini.read_text()
