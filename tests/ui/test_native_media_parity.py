"""Native media warnings retain the old session until an explicit decision."""
import json
import struct
import zipfile

import pytest

from .harness import AltirraTestHarness
from .test_explorers import click


@pytest.fixture
def media(request, monkeypatch, tmp_path):
    monkeypatch.setenv("SDL_VIDEODRIVER", "dummy")
    monkeypatch.setenv("SDL_AUDIODRIVER", "dummy")
    monkeypatch.setenv("ALTIRRA_DISPLAY_BACKEND", "sdlrenderer")
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "config"))
    with AltirraTestHarness(executable=request.config.getoption("--emu-path"),
                           extra_args=["--ui-mode", "desktop"]) as emu:
        emu.close_dialog("SetupWizard")
        emu.pause()
        yield emu


def program(path, address=0x2000):
    path.write_bytes(b"\xff\xff" + struct.pack("<HH", address, address + 2)
                     + b"\x4c" + struct.pack("<H", address)
                     + struct.pack("<HHH", 0x2E0, 0x2E1, address))
    return path


def choose(emu, title, label):
    emu.wait_frames(3)
    items = [item for item in emu.list_items()
             if item["window"].startswith(title) and item["label"] == label]
    assert items, (title, label, emu.list_items())
    click(emu, items[0]["window"], label)


def dirty_disk(emu):
    emu.send("netplay_profile disk_create")
    emu.send("netplay_profile disk_write 113")
    status = emu.send("netplay_profile status")
    assert status["diskDirty"] and status["diskByte"] == 113
    return status


@pytest.mark.parametrize("accept", [False, True])
def test_boot_requires_explicit_discard(media, tmp_path, accept):
    before = dirty_disk(media)
    image = program(tmp_path / "loop.xex")
    media.send("boot_image " + str(image))
    media.wait_frames(4)
    pending = media.send("netplay_profile status")
    assert pending["diskObject"] == before["diskObject"]
    assert pending["diskDirty"]
    choose(media, "Unsaved Items", "OK" if accept else "Cancel")
    after = media.send("netplay_profile status")
    if accept:
        assert after["diskObject"] == 0
        assert media.get_sim_state()["running"]
    else:
        assert after["diskObject"] == before["diskObject"]
        assert after["diskDirty"] and after["diskByte"] == 113
        assert media.get_sim_state()["paused"]


def test_running_session_freezes_for_discard_without_changing_pause(media, tmp_path):
    image = program(tmp_path / "loop.xex")
    media.send("boot_image " + str(image))
    media.wait_frames(6)
    dirty_disk(media)
    media.send("boot_image " + str(image))
    media.wait_frames(4)
    tick = media.get_sim_state()["emulationTick"]
    media.wait_frames(12)
    assert media.get_sim_state()["emulationTick"] == tick
    assert not media.get_sim_state()["paused"]
    choose(media, "Unsaved Items", "Cancel")
    media.wait_frames(4)
    assert media.get_sim_state()["emulationTick"] > tick


@pytest.mark.parametrize("decision", ["Yes", "No", "Cancel"])
def test_basic_overlap_is_a_real_three_way_decision(media, tmp_path, decision):
    if media.send("query_command System.ToggleBASIC")["state"] != "checked":
        media.send("run_command System.ToggleBASIC")
    before = dirty_disk(media)
    image = program(tmp_path / "overlap.xex", 0xA000)
    media.send("boot_image " + str(image))
    choose(media, "Unsaved Items", "OK")
    # The later BASIC decision must also precede storage destruction.
    assert media.send("netplay_profile status")["diskObject"] == before["diskObject"]
    assert media.send("query_command System.ToggleBASIC")["state"] == "checked"
    choose(media, "Memory Conflict", decision)
    basic = media.send("query_command System.ToggleBASIC")["state"] == "checked"
    assert basic is (decision != "Yes")
    if decision == "Cancel":
        assert media.send("netplay_profile status")["diskObject"] == before["diskObject"]
        assert media.get_sim_state()["paused"]
    else:
        assert media.send("netplay_profile status")["diskObject"] == 0


@pytest.mark.parametrize("accept", [False, True])
def test_kernel_mismatch_cancel_and_continue_use_the_validated_bytes(media, tmp_path, accept):
    media.send("debugger_console e 4000 5A")
    path = tmp_path / "mismatch.atstate2"
    media.send("save_state " + str(path))
    media.wait_frames(3)
    with zipfile.ZipFile(path) as archive:
        files = {name: archive.read(name) for name in archive.namelist()}
    root = json.loads(files["savestate.json"])
    firmware = root["objects"][root["objects"][0]["os_rom"] - 1]
    firmware.update(ref_string="parity-test-missing-rom", crc32="0")
    files["savestate.json"] = json.dumps(root).encode()
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, contents in files.items():
            archive.writestr(name, contents)
    media.send("debugger_console e 4000 77")
    media.send("load_state " + str(path))
    media.wait_frames(4)
    assert media.mem_read(0x4000, 1) == [0x77]
    # Retrying must not reopen a file that changed during the prompt.
    path.write_bytes(b"file replaced during confirmation")
    choose(media, "Kernel ROM Mismatch", "OK" if accept else "Cancel")
    assert media.mem_read(0x4000, 1) == [0x5A if accept else 0x77]
    assert media.get_sim_state()["running"] is accept


def profiles(emu):
    return emu.query_state()["state"]["profiles"]


def profile_ids(emu):
    return {item["id"] for item in profiles(emu)["items"]}


def enable_basic(emu):
    if emu.send("query_command System.ToggleBASIC")["state"] != "checked":
        emu.send("run_command System.ToggleBASIC")


def auto_hide_pointer(emu):
    emu.send("system_config_category 19")  # Emulator > Display
    emu.wait_frames(3)
    checkbox = next(item for item in emu.list_items("Configure System")
                    if item["label"] == "Auto-hide mouse pointer after short delay")
    checked = checkbox["checked"]
    emu.close_dialog("SystemConfig")
    emu.wait_frames(2)
    return checked


def test_profile_cancel_restores_configuration_runtime_and_dirty_media(media, tmp_path):
    enable_basic(media)
    media.send("set_speed 100")
    target = media.send("profile_test create")["id"]
    media.send("set_speed 139")
    assert auto_hide_pointer(media)
    media.send("run_command View.ToggleAutoHidePointer")
    original = profiles(media)
    original_ids = profile_ids(media)
    media.send("debugger_console e 4000 77")
    disk = dirty_disk(media)
    image = program(tmp_path / "profile-overlap.xex", 0xA000)
    media.send(f"boot_profile {target} {image}")
    choose(media, "Unsaved Items", "OK")
    media.wait_frames(3)
    assert profiles(media)["current"] == target
    assert profiles(media)["temporary"]
    assert media.send("get_speed")["percent"] == pytest.approx(100)
    choose(media, "Memory Conflict", "Cancel")
    media.wait_frames(3)
    assert profiles(media)["current"] == original["current"]
    assert profiles(media)["temporary"] == original["temporary"]
    assert media.send("get_speed")["percent"] == pytest.approx(139)
    assert not auto_hide_pointer(media)
    assert media.mem_read(0x4000, 1) == [0x77]
    restored = media.send("netplay_profile status")
    assert restored["diskObject"] == disk["diskObject"]
    assert restored["diskDirty"] and restored["diskByte"] == 113
    assert media.get_sim_state()["paused"]
    assert profile_ids(media) == original_ids
    # The abandoned target must retain its original settings rather than
    # receiving a save of the transaction's temporary configuration.
    media.send(f"profile_test activate {target}")
    assert media.send("get_speed")["percent"] == pytest.approx(100)


def test_temporary_game_profile_cancel_preserves_live_settings_and_cleans_scratch(media, tmp_path):
    enable_basic(media)
    media.send("set_speed 100")
    original = media.send("profile_test create")["id"]
    target = media.send("profile_test create")["id"]
    loop = program(tmp_path / "original-game.xex")
    media.send(f"boot_profile {original} {loop}")
    media.wait_frames(5)
    media.pause()
    assert profiles(media)["current"] == original and profiles(media)["temporary"]
    # This is a live game-session preference, intentionally different from
    # the saved preset. A reload of that preset cannot restore this value.
    media.send("set_speed 175")
    assert auto_hide_pointer(media)
    media.send("run_command View.ToggleAutoHidePointer")
    media.send("debugger_console e 4000 6C")
    disk = dirty_disk(media)
    baseline_ids = profile_ids(media)
    image = program(tmp_path / "new-game-overlap.xex", 0xA000)
    media.send(f"boot_profile {target} {image}")
    choose(media, "Unsaved Items", "OK")
    media.wait_frames(3)
    assert profiles(media)["current"] == target
    assert media.send("get_speed")["percent"] == pytest.approx(100)
    scratch = [item for item in profiles(media)["items"]
               if item["id"] not in baseline_ids]
    assert len(scratch) == 1
    assert scratch[0]["name"] == "Temporary media-load rollback"
    choose(media, "Memory Conflict", "Cancel")
    media.wait_frames(3)
    assert profiles(media)["current"] == original and profiles(media)["temporary"]
    assert media.send("get_speed")["percent"] == pytest.approx(175)
    assert not auto_hide_pointer(media)
    assert media.mem_read(0x4000, 1) == [0x6C]
    restored = media.send("netplay_profile status")
    assert restored["diskObject"] == disk["diskObject"]
    assert restored["diskDirty"] and restored["diskByte"] == 113
    assert media.get_sim_state()["paused"]
    assert profile_ids(media) == baseline_ids
    # Leaving the game session reloads the unchanged preset, proving the
    # original preset was never overwritten with live session settings.
    media.send(f"profile_test activate {original}")
    assert not profiles(media)["temporary"]
    assert media.send("get_speed")["percent"] == pytest.approx(100)
    assert auto_hide_pointer(media)


def test_hardware_switch_cancel_restores_5200_session(media, tmp_path):
    enable_basic(media)
    # Preserve the stock XL setup, then make the current independent profile
    # a 5200. The incoming computer image must switch back to the stock XL.
    original = media.send("profile_test create")["id"]
    media.send(f"profile_test activate {original}")
    media.send("run_command System.HardwareMode5200")
    media.pause()
    media.send("set_speed 155")
    media.send("hold_keys")
    media.send("physical_key down F2")
    media.wait_frames(2)
    media.send("physical_key up F2")
    media.wait_frames(2)
    before = media.get_sim_state()
    assert before["holdKeys"] and before["pendingHeldSwitches"] == 1
    media.send("debugger_console e 1000 5D")
    disk = dirty_disk(media)
    image = program(tmp_path / "computer-overlap.xex", 0xA000)
    media.send("boot_image " + str(image))
    choose(media, "Unsaved Items", "OK")
    media.wait_frames(3)
    assert media.get_sim_state()["hardwareMode"] != before["hardwareMode"]
    choose(media, "Memory Conflict", "Cancel")
    media.wait_frames(3)
    assert media.get_sim_state()["hardwareMode"] == before["hardwareMode"]
    assert media.get_sim_state()["holdKeys"]
    assert media.get_sim_state()["pendingHeldSwitches"] == 1
    assert profiles(media)["current"] == original and not profiles(media)["temporary"]
    assert media.get_sim_state()["paused"]
    assert media.send("get_speed")["percent"] == pytest.approx(155)
    assert media.mem_read(0x1000, 1) == [0x5D]
    restored = media.send("netplay_profile status")
    assert restored["diskObject"] == disk["diskObject"]
    assert restored["diskDirty"] and restored["diskByte"] == 113


def test_background_cancels_profile_prompt_and_releases_modal_input(media, tmp_path):
    import time

    enable_basic(media)
    target = media.send("profile_test create")["id"]
    original = profiles(media)["current"]
    media.send("debugger_console e 4000 49")
    image = program(tmp_path / "background-profile-overlap.xex", 0xA000)
    media.send(f"boot_profile {target} {image}")
    media.wait_frames(5)
    assert profiles(media)["current"] == target

    def wait_suspended(expected):
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            if media.query_state()["state"]["appSuspended"] is expected:
                return
            time.sleep(0.03)
        raise AssertionError(f"appSuspended did not become {expected}")

    media.send("app_lifecycle background")
    wait_suspended(True)
    assert profiles(media)["current"] == original
    assert media.mem_read(0x4000, 1) == [0x49]
    media.send("app_lifecycle foreground")
    wait_suspended(False)
    media.wait_frames(5)
    assert not any(item["window"].startswith("Memory Conflict")
                   for item in media.list_items())
    # A removed media modal must also release ImGui's popup stack. Verify a
    # genuine mouse click reaches a configuration control after foreground.
    media.send("system_config_category 19")
    media.wait_frames(3)
    label = "Auto-hide mouse pointer after short delay"
    before = next(item["checked"] for item in media.list_items("Configure System")
                  if item["label"] == label)
    click(media, "Configure System", label)
    after = next(item["checked"] for item in media.list_items("Configure System")
                 if item["label"] == label)
    assert after is not before


@pytest.mark.parametrize('accept', [False, True])
def test_attach_disk_requires_discard_for_exact_drive(media, tmp_path, accept):
    from .test_explorers import make_disk

    before = dirty_disk(media)
    replacement = tmp_path / 'replacement.atr'
    make_disk(replacement)
    media.send(f'attach_disk 0 {replacement}')
    media.wait_frames(4)
    assert media.send('netplay_profile status')['diskObject'] == before['diskObject']
    choose(media, 'Unsaved Items', 'OK' if accept else 'Cancel')
    restored = media.send('netplay_profile status')
    if accept:
        assert restored['diskObject'] != before['diskObject']
        assert not restored['diskDirty']
        assert media.query_state()['state']['disks'][0]['sectors'] == 720
    else:
        assert restored['diskObject'] == before['diskObject']
        assert restored['diskDirty'] and restored['diskByte'] == 113


@pytest.mark.parametrize("accept", [False, True])
def test_unknown_mapper_keeps_profile_transaction_and_captured_rom(media, tmp_path, accept):
    media.send("set_speed 100")
    target = media.send("profile_test create")["id"]
    media.send("set_speed 167")
    original = profiles(media)
    baseline_ids = profile_ids(media)
    media.send("debugger_console e 4000 4D")
    disk = dirty_disk(media)
    media.send("netplay_profile cart_create")
    cart = media.send("netplay_profile cart_write 113")
    assert cart["cartDirty"] and cart["cartByte"] == 113
    # Headerless cartridge images require a mapper decision even at this
    # supported size. The loader must retain these bytes across that choice.
    path = tmp_path / "ambiguous.rom"
    path.write_bytes(b"\x5A" + b"\xEA" * (8192 - 1))
    media.send(f"boot_profile {target} {path}")
    choose(media, "Unsaved Items", "OK")
    media.wait_frames(4)
    rows = [item for item in media.list_items()
            if item["window"].startswith("Select Cartridge Mapper")]
    assert rows, media.list_items()
    assert profiles(media)["current"] == target and profiles(media)["temporary"]
    assert media.send("get_speed")["percent"] == pytest.approx(100)
    # Replacing the source while the dialog is open must not change the
    # image installed by the retained mapper continuation.
    path.write_bytes(b"\x35" * 8192)
    if accept:
        standard = next(item for item in rows
                        if item["label"].split("##")[0].lstrip("*")
                        in ("8K", "8K (recommended)"))
        click(media, "Select Cartridge Mapper", standard["label"])
    choose(media, "Select Cartridge Mapper", "OK" if accept else "Cancel")
    media.wait_frames(3)
    status = media.send("netplay_profile status")
    assert profile_ids(media) == baseline_ids
    if accept:
        assert profiles(media)["current"] == target and profiles(media)["temporary"]
        assert status["cartObject"] != 0 and status["cartByte"] == 0x5A
        assert status["diskObject"] == 0
    else:
        assert profiles(media)["current"] == original["current"]
        assert profiles(media)["temporary"] == original["temporary"]
        assert media.send("get_speed")["percent"] == pytest.approx(167)
        assert media.mem_read(0x4000, 1) == [0x4D]
        assert media.get_sim_state()["paused"]
        assert status["diskObject"] == disk["diskObject"]
        assert status["diskDirty"] and status["diskByte"] == 113
        assert status["cartObject"] == cart["cartObject"]
        assert status["cartDirty"] and status["cartByte"] == 113
