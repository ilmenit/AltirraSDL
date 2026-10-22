"""Media rollback preserves run intent across a completed native picker lease."""
import json
import zipfile

import pytest

from .test_explorers import enter
from .test_native_media_parity import media, choose


@pytest.mark.parametrize("running", [False, True])
def test_real_load_state_picker_cancel_restores_logical_run_state(media, tmp_path, running):
    if running:
        media.resume()
        media.wait_frames(5)
    state = tmp_path / "picker-mismatch.atstate2"
    media.send("save_state " + str(state))
    media.wait_frames(3)
    with zipfile.ZipFile(state) as archive:
        contents = {name: archive.read(name) for name in archive.namelist()}
    document = json.loads(contents["savestate.json"])
    rom = document["objects"][document["objects"][0]["os_rom"] - 1]
    rom.update(ref_string="missing-picker-rollback-rom", crc32="0")
    contents["savestate.json"] = json.dumps(document).encode()
    with zipfile.ZipFile(state, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, data in contents.items():
            archive.writestr(name, data)

    media.send("file_dialog_builtin on")
    media.send("run_command File.LoadState")
    media.wait_frames(4)
    assert media.get_sim_state()["fileDialogPaused"]
    enter(media, "Open File", "##path", tmp_path, True)
    enter(media, "Open File", "##filename", state.name, True)
    media.wait_frames(5)
    # The file picker has finished before the independent ROM decision.
    # Capturing its temporary suspend as the original state previously left
    # running sessions suspended indefinitely when this warning was cancelled.
    assert not media.get_sim_state()["fileDialogPaused"]
    choose(media, "Kernel ROM Mismatch", "Cancel")
    after = media.get_sim_state()
    assert after["running"] is running
    assert after["paused"] is not running
    assert not after["fileDialogPaused"]
    tick = after["emulationTick"]
    media.wait_frames(10)
    assert (media.get_sim_state()["emulationTick"] > tick) is running


def test_file_attach_disk_picker_cancel_preserves_dirty_drive(media, tmp_path):
    from .test_explorers import click, make_disk
    from .test_native_media_parity import dirty_disk
    before = dirty_disk(media)
    path = tmp_path / "replacement.atr"
    make_disk(path)
    media.send("file_dialog_builtin on")
    click(media, "##MainMenuBar", "File")
    click(media, "", "Attach Disk")
    drive = next(item for item in media.list_items()
                 if item["label"].startswith("Drive 1 ["))
    click(media, drive["window"], drive["label"])
    enter(media, "Open File", "##path", tmp_path, True)
    enter(media, "Open File", "##filename", path.name, True)
    pending = media.send("netplay_profile status")
    assert pending["diskObject"] == before["diskObject"] and pending["diskDirty"]
    choose(media, "Unsaved Items", "Cancel")
    after = media.send("netplay_profile status")
    assert after["diskObject"] == before["diskObject"]
    assert after["diskDirty"] and after["diskByte"] == 113
