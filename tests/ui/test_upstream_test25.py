"""Test25 VBXE controls and recovery of legacy disk motor timer snapshots."""
import json
import zipfile

import pytest

from .test_explorers import click


def choose_core(emu, window, version):
    # ImGui's legacy Combo wrapper does not publish an item to the test
    # hooks. Open the first row using the live dialog bounds instead.
    bounds = next(w for w in emu.query_state()["state"]["windows"]
                  if w["name"] == window)
    emu.send(f"click_at {bounds['x'] + bounds['w'] / 3} "
             f"{bounds['y'] + bounds['h'] * 0.28}")
    emu.wait_frames(3)
    click(emu, "", version)


def test_vbxe_shared_memory_tracks_core(emu):
    emu.navigate_system_config("Devices")
    click(emu, "Configure System", "Add Device...")
    click(emu, "", "Internal devices")
    click(emu, "", "VideoBoard XE (VBXE)")
    window = "VideoBoard XE Settings###DeviceConfig"
    assert emu.find_item(window, "Enable shared memory")["disabled"]
    choose_core(emu, window, "FX 1.26")
    assert not emu.find_item(window, "Enable shared memory")["disabled"]
    click(emu, window, "Enable shared memory")
    choose_core(emu, window, "FX 1.27")
    assert emu.find_item(window, "Enable shared memory")["disabled"]
    choose_core(emu, window, "FX 1.24")
    assert not emu.find_item(window, "Enable shared memory")["disabled"]
    # Switching versions must retain the older core's checkbox choice.
    assert emu.find_item(window, "Enable shared memory")["checked"]
    click(emu, window, "Cancel")


@pytest.mark.parametrize("timer", [0, 1, 30_000_000, 0x40000000, 0xFFFFFFFF])
def test_legacy_disk_idle_timer(emu, tmp_path, timer):
    image = tmp_path / "disk.atr"
    image.write_bytes(bytes([0x96, 2, 0x80, 0x16, 0x80, 0])
                      + bytes(10 + 720 * 128))
    emu.send("attach_disk 0 " + str(image))
    emu.wait_frames(5)
    emu.pause()
    original = tmp_path / "original.altstate"
    emu.save_state(str(original))
    emu.wait_frames(5)
    assert emu.query_state()["state"]["toolResult"] == ""
    with zipfile.ZipFile(original) as archive:
        contents = {name: archive.read(name) for name in archive.namelist()}
    snapshot = json.loads(contents["savestate.json"])
    disk = next(obj for obj in snapshot["objects"]
                if obj.get("_type") == "ATSaveStateDisk")
    # Remove the fork's exact-runtime extension to exercise a Windows
    # legacy mechanical-position snapshot, including its broken timer.
    for key in list(disk):
        if (key.startswith("exact_") or key.startswith("runtime_")
                or key in ("motor_event_delay", "motor_deadline_offset")):
            del disk[key]
    disk["idle_timer"] = timer
    contents["savestate.json"] = json.dumps(snapshot).encode()
    legacy = tmp_path / "legacy.altstate"
    with zipfile.ZipFile(legacy, "w") as archive:
        for name, data in contents.items():
            archive.writestr(name, data)
    emu.load_state(str(legacy))
    emu.wait_frames(2)
    emu.pause()
    emu.wait_frames(3)
    assert emu.query_state()["state"]["toolResult"] == ""
    restored = tmp_path / "restored.altstate"
    emu.save_state(str(restored))
    emu.wait_frames(5)
    assert emu.query_state()["state"]["toolResult"] == ""
    with zipfile.ZipFile(restored) as archive:
        snapshot = json.loads(archive.read("savestate.json"))
    disk = next(obj for obj in snapshot["objects"]
                if obj.get("_type") == "ATSaveStateDisk")
    # Loading a state resumes emulation; allow the few frames before the
    # pause command takes effect, including expiry of a one-tick timer.
    remaining = int(disk["idle_timer"])
    target = min(timer, 100_000_000)
    assert max(0, target - 2_000_000) <= remaining <= target
    if remaining:
        assert remaining == int(disk["motor_deadline_offset"])
    assert emu.ping()["ok"]
