"""Document Explorer regressions: independent files, previews and byte exports.

Run with an isolated XDG_CONFIG_HOME and --emu-path pointing to the review build.
Fixtures follow ATDiskFSDOS2::InitNew/ReadFile and the XEX segment encoding.
"""
import struct
from pathlib import Path

import pytest


def windows(emu, fragment):
    return [w for w in emu.query_state()["state"]["windows"]
            if fragment in w["name"] and "###Explorer" in w["name"]
            and "/" not in w["name"].split("###Explorer")[-1]]


def click(emu, window, label):
    items = [item for item in emu.list_items(window) if item["label"] == label]
    row = next((item for item in items if "/Contents_" in item["window"]
                or "/FileList_" in item["window"]), None)
    if row:
        # A spanning table row's center can coincide with a column resize
        # handle. Click the label area, as a user selecting its name would.
        emu.send(f"click_at {row['x'] + 24} {row['y'] + row['h'] / 2}")
    else:
        emu.click(window, label)
    emu.wait_frames(6)


def enter(emu, window, label, text, submit=False):
    click(emu, window, label)
    emu.send("key ctrl+a")
    emu.wait_frames(3)
    emu.send("send_text " + str(text))
    emu.wait_frames(3)
    if submit:
        emu.send("key enter")
        emu.wait_frames(5)


def save_file(emu, path):
    enter(emu, "Save File", "##path", path.parent, True)
    enter(emu, "Save File", "File name", path.name)
    click(emu, "Save File", "Save")
    emu.wait_frames(5)


def xex_bytes():
    def segment(address, data):
        return struct.pack("<HH", address, address + len(data) - 1) + data
    return b"\xff\xff" + segment(0x2000, b"\xa9\x01\x60") + segment(0x2001, b"\xea") + segment(0x02E0, b"\x00\x20")


def make_disk(path):
    # 720 x 128-byte DOS 2 sectors. Sector numbering is one-based in VTOC
    # and file chains; ATR stores the sectors consecutively after 16 bytes.
    disk = bytearray(720 * 128)
    content = xex_bytes()
    vtoc = memoryview(disk)[359 * 128:360 * 128]
    vtoc[0] = 2
    vtoc[1:3] = struct.pack("<H", 707)
    free = set(range(4, 720)) - set(range(360, 369)) - {4}
    vtoc[3:5] = struct.pack("<H", len(free))
    for sector in free:
        vtoc[10 + sector // 8] |= 0x80 >> (sector % 8)
    entry = memoryview(disk)[360 * 128:360 * 128 + 16]
    entry[0] = 0x42  # In-use DOS 2 file, as defined in diskfsdos2.cpp.
    entry[1:5] = struct.pack("<HH", 1, 4)
    entry[5:16] = b"DEMO    XEX"
    disk[3 * 128:3 * 128 + len(content)] = content
    disk[3 * 128 + 127] = len(content)
    header = bytearray(16)
    header[:6] = struct.pack("<HHH", 0x0296, len(disk) // 16, 128)
    path.write_bytes(header + disk)


@pytest.fixture
def explorer(emu):
    emu.send("file_dialog_builtin on")
    emu.send("set_window_size 1280 900")
    emu.wait_frames(5)
    yield emu
    for name in ("XEXExplorer", "CartridgeExplorer", "DiskExplorer"):
        emu.close_dialog(name)
    emu.wait_frames(5)


def test_file_first_cancel_has_no_empty_document(explorer):
    emu = explorer
    for name, fragment in (("DiskExplorer", "Disk Explorer"),
                           ("XEXExplorer", "XEX Explorer"),
                           ("CartridgeExplorer", "Cartridge Explorer")):
        emu.open_dialog(name)
        emu.wait_frames(5)
        assert not windows(emu, fragment)
        click(emu, "Open File", "Cancel")
        assert not emu.get_dialog_state(name)
        assert not windows(emu, fragment)


def test_xex_independent_documents_and_exact_exports(explorer, tmp_path):
    emu = explorer
    a, b = tmp_path / "first.xex", tmp_path / "second.xex"
    a.write_bytes(xex_bytes())
    b.write_bytes(b"\xff\xff" + struct.pack("<HH", 0x3000, 0x3000) + b"\x60")
    emu.send(f"explore_file xex {a}")
    emu.send(f"explore_file xex {b}")
    emu.wait_frames(6)
    assert len(windows(emu, "XEX Explorer")) == 2
    emu.send(f"explore_file xex {a}")  # focuses the existing document
    emu.wait_frames(6)
    assert len(windows(emu, "XEX Explorer")) == 2
    click(emu, "first.xex", "Segment 2")
    click(emu, "first.xex", "Export selected...")
    click(emu, "", "Export selected bytes...")
    raw = tmp_path / "segment.bin"
    save_file(emu, raw)
    assert raw.read_bytes() == b"\xea"
    click(emu, "first.xex", "Select all")
    click(emu, "first.xex", "Export selected...")
    click(emu, "", "Export selected as XEX...")
    exported = tmp_path / "copy.xex"
    save_file(emu, exported)
    assert exported.read_bytes() == a.read_bytes()
    # Closing one document cannot close the other document of the same type.
    click(emu, "first.xex", "File")
    click(emu, "", "Close")
    assert len(windows(emu, "XEX Explorer")) == 1


def test_cartridge_regions_and_raw_roundtrip(explorer, tmp_path):
    emu = explorer
    path = tmp_path / "banks.car"
    payload = bytes(range(256)) * 128
    path.write_bytes(b"CART" + struct.pack(">III", 12, sum(payload), 0) + payload)
    emu.send(f"explore_file cartridge {path}")
    emu.wait_frames(6)
    click(emu, "banks.car", "Region 1")
    click(emu, "banks.car", "Export selected...")
    region = tmp_path / "region.bin"
    save_file(emu, region)
    assert region.read_bytes() == payload[8192:16384]
    click(emu, "banks.car", "File")
    click(emu, "", "Save raw image...")
    raw = tmp_path / "all.bin"
    save_file(emu, raw)
    assert raw.read_bytes() == payload


def test_disk_nested_executable_and_sector_tools(explorer, tmp_path):
    emu = explorer
    disk = tmp_path / "files.atr"
    make_disk(disk)
    emu.send(f"explore_file disk {disk}")
    emu.wait_frames(6)
    click(emu, "files.atr", "DEMO.XEX")
    # Run is a memory-backed boot; it must leave the Explorer document open.
    click(emu, "files.atr", "Run")
    assert windows(emu, "files.atr")
    click(emu, "files.atr", "Open in XEX Explorer")
    assert windows(emu, "XEX Explorer")
    emu.close_dialog("XEXExplorer")
    emu.wait_frames(5)
    click(emu, "files.atr", "Disk")
    click(emu, "", "View Sectors...")
    assert any("Sector Viewer" in w["name"] for w in emu.query_state()["state"]["windows"])


def test_batch_export_keeps_segment_boundaries(explorer, tmp_path):
    emu = explorer
    path = tmp_path / "batch.xex"
    path.write_bytes(xex_bytes())
    folder = tmp_path / "exported"
    folder.mkdir()
    emu.send(f"explore_file xex {path}")
    emu.wait_frames(5)
    click(emu, "batch.xex", "Select all")
    click(emu, "batch.xex", "Export selected...")
    click(emu, "", "Export selected bytes...")
    enter(emu, "Select Folder", "##path", tmp_path, True)
    click(emu, "Select Folder", "[DIR] exported")
    click(emu, "Select Folder", "Select Folder")
    assert (folder / "segment-001-2000-2002.bin").read_bytes() == b"\xa9\x01\x60"
    assert (folder / "segment-002-2001-2001.bin").read_bytes() == b"\xea"
    assert (folder / "segment-003-02E0-02E1.bin").read_bytes() == b"\x00\x20"


def test_disk_edit_save_and_close_decision(explorer, tmp_path):
    emu = explorer
    disk = tmp_path / "editable.atr"
    make_disk(disk)
    emu.send(f"explore_file disk {disk}")
    emu.wait_frames(6)
    click(emu, "editable.atr", "DEMO.XEX")
    click(emu, "editable.atr", "Write")
    # Remount refreshes the listing and clears selection.
    click(emu, "editable.atr", "DEMO.XEX")
    click(emu, "editable.atr", "Selection actions...")
    click(emu, "", "Rename...")
    enter(emu, "", "##rename", "RENAMED.XEX")
    click(emu, "", "OK")
    assert any(i["label"] == "RENAMED.XEX" for i in emu.list_items("editable.atr"))
    click(emu, "editable.atr", "File")
    click(emu, "", "Close")
    assert any("Save disk changes?" in w["name"] for w in emu.query_state()["state"]["windows"])
    click(emu, "Save disk changes?", "Cancel")
    assert windows(emu, "editable.atr")
    click(emu, "editable.atr", "File")
    click(emu, "", "Save disk image as...")
    saved = tmp_path / "saved.atr"
    save_file(emu, saved)
    assert saved.exists()
    emu.close_dialog("DiskExplorer")
    emu.wait_frames(6)
    assert not windows(emu, "Disk Explorer")
    emu.send(f"explore_file disk {saved}")
    emu.wait_frames(6)
    assert any(i["label"] == "RENAMED.XEX" for i in emu.list_items("saved.atr"))
    # The original file has not been overwritten by editing or Save As.
    assert b"DEMO    XEX" in disk.read_bytes()
    assert b"RENAMED XEX" in saved.read_bytes()


def test_drop_targets_frontmost_document_only(explorer, tmp_path):
    emu = explorer
    first, second, third = (tmp_path / n for n in ("one.xex", "two.xex", "three.xex"))
    for path in (first, second, third):
        path.write_bytes(xex_bytes())
    emu.send(f"explore_file xex {first}")
    emu.send(f"explore_file xex {second}")
    emu.wait_frames(6)
    assert not emu.send(f"explorer_drop 10 80 {third}")["handled"]
    window = windows(emu, "two.xex")[0]
    assert emu.send(f"explorer_drop {window['x'] + 40} {window['y'] + 100} {third}")["handled"]
    emu.wait_frames(6)
    assert len(windows(emu, "XEX Explorer")) == 3


def test_unrecognized_filesystem_keeps_sector_inspection(explorer, tmp_path):
    emu = explorer
    disk = tmp_path / "sectors.atr"
    make_disk(disk)
    data = bytearray(disk.read_bytes())
    data[16 + 359 * 128] = 0  # remove DOS VTOC signature
    disk.write_bytes(data)
    emu.send(f"explore_file disk {disk}")
    emu.wait_frames(6)
    click(emu, "sectors.atr", "Disk")
    click(emu, "", "View Sectors...")
    assert any("Sector Viewer" in w["name"] for w in emu.query_state()["state"]["windows"])


def test_cancel_export_releases_document(explorer, tmp_path):
    emu = explorer
    path = tmp_path / "cancel.xex"
    path.write_bytes(xex_bytes())
    emu.send(f"explore_file xex {path}")
    emu.wait_frames(5)
    click(emu, "cancel.xex", "Export selected...")
    click(emu, "", "Export selected bytes...")
    click(emu, "Save File", "Cancel")
    click(emu, "cancel.xex", "Segment 2")
    click(emu, "cancel.xex", "Export selected...")
    click(emu, "", "Export selected bytes...")
    result = tmp_path / "after-cancel.bin"
    save_file(emu, result)
    assert result.read_bytes() == b"\xea"


def test_keyboard_selection_updates_export(explorer, tmp_path):
    emu = explorer
    path = tmp_path / "keyboard.xex"
    path.write_bytes(xex_bytes())
    emu.send(f"explore_file xex {path}")
    emu.wait_frames(5)
    click(emu, "keyboard.xex", "Segment 1")
    emu.send("key down")
    emu.wait_frames(5)
    click(emu, "keyboard.xex", "Export selected...")
    click(emu, "", "Export selected bytes...")
    result = tmp_path / "keyboard.bin"
    save_file(emu, result)
    assert result.read_bytes() == b"\xea"


def test_closed_document_ignores_outstanding_export(explorer, tmp_path):
    emu = explorer
    path = tmp_path / "closed.xex"
    path.write_bytes(xex_bytes())
    emu.send(f"explore_file xex {path}")
    emu.wait_frames(5)
    click(emu, "closed.xex", "Export selected...")
    click(emu, "", "Export selected bytes...")
    emu.close_dialog("XEXExplorer")
    emu.wait_frames(5)
    result = tmp_path / "must-not-exist.bin"
    save_file(emu, result)
    assert not result.exists()
    assert not windows(emu, "XEX Explorer")
    assert emu.ping()["ok"]


def test_failed_open_preserves_other_documents(explorer, tmp_path):
    emu = explorer
    valid, invalid = tmp_path / "valid.xex", tmp_path / "truncated.xex"
    valid.write_bytes(xex_bytes())
    invalid.write_bytes(b"\xff\xff\x00\x20\xff\x20")
    emu.send(f"explore_file xex {valid}")
    emu.send(f"explore_file xex {invalid}")
    emu.wait_frames(6)
    assert len(windows(emu, "XEX Explorer")) == 2
    assert any(i["label"] == "Segment 3" for i in emu.list_items("valid.xex"))
    assert any(i["label"] == "Open another..." for i in emu.list_items("truncated.xex"))


def test_drop_respects_focus_across_explorer_types(explorer, tmp_path):
    emu = explorer
    xex, cart, dropped = tmp_path / "behind.xex", tmp_path / "front.car", tmp_path / "new.car"
    xex.write_bytes(xex_bytes())
    data = bytes(range(256)) * 128
    image = b"CART" + struct.pack(">III", 12, sum(data), 0) + data
    cart.write_bytes(image)
    dropped.write_bytes(image)
    emu.send(f"explore_file xex {xex}")
    emu.send(f"explore_file cartridge {cart}")
    emu.wait_frames(6)
    # XEX renders later in the frame; explicit focus must still put CAR first.
    emu.send(f"explore_file cartridge {cart}")
    emu.wait_frames(6)
    front = windows(emu, "front.car")[0]
    assert emu.send(f"explorer_drop {front['x'] + 80} {front['y'] + 140} {dropped}")["handled"]
    emu.wait_frames(6)
    assert len(windows(emu, "Cartridge Explorer")) == 2
    assert len(windows(emu, "XEX Explorer")) == 1
