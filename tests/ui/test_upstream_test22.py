"""Exercise test22's virtual mount entry points and Unicode save-state paths."""
from urllib.parse import quote, unquote

from .harness import AltirraTestHarness


def test_virtual_folder_dialog(emu, tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "config"))
    with AltirraTestHarness(executable=emu.executable,
                           extra_args=["--ui-mode", "desktop"]) as fresh:
        emu = fresh
        emu.open_dialog("DiskManager")
        emu.wait_frames(4)
        emu.click("Disk drives", "+")
        emu.wait_frames(4)
        item = next(i for i in emu.list_items()
                    if i.get("label") == "Mount folder as virtual disk...")
        emu.click(item["window"], item["label"])
        emu.wait_frames(4)
        win = "Mount folder as virtual disk"
        assert emu.find_item(win, "##Path") is not None
        assert emu.find_item(win, "##Sectors")["disabled"] is True
        emu.click(win, "OK")
        emu.wait_frames(4)
        # An invalid path leaves the configuration open for correction.
        assert emu.find_item(win, "Cancel") is not None
        emu.click(win, "Cancel")
        emu.wait_frames(4)
        assert emu.find_item(win, "##Path") is None


def test_virtual_url_and_unicode_state(emu, tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "config"))
    folder = tmp_path / "mount ç 中 😀"
    folder.mkdir()
    (folder / "HELLO.TXT").write_bytes(b"Hello from the host")
    url = "atvdisk://" + quote(str(folder), safe="/") + "?fs=sdfs&ssize=256"
    state = tmp_path / "state ç 中 😀.altstate"
    with AltirraTestHarness(executable=emu.executable,
                           extra_args=["--ui-mode", "desktop"]) as fresh:
        fresh.send("attach_disk 0 " + url)
        fresh.wait_frames(10)
        current = fresh.query_state()["state"]
        assert current["toolResult"] == ""
        assert current["disks"][0]["sectorSize"] == 256
        assert str(folder) in unquote(current["disks"][0]["path"])
        fresh.save_state(str(state))
        fresh.wait_frames(10)
        assert fresh.query_state()["state"]["toolResult"] == ""
        assert state.is_file() and state.stat().st_size > 0
        fresh.load_state(str(state))
        fresh.wait_frames(10)
        assert fresh.query_state()["state"]["toolResult"] == ""
        assert fresh.ping()["ok"] is True
        restored = fresh.query_state()["state"]["disks"][0]
        assert restored["sectorSize"] == 256
        assert str(folder) in unquote(restored["path"])
