"""Test28 command registration, real host-file closure, and stereo modem audio."""
import re
import pytest

from .test_video_outputs import video_desktop
from .test_explorers import click


def command(emu, name):
    result = emu.send("query_command " + name)
    assert result["exists"], name
    return result


def type_line(bridge, text):
    bridge.type(text)
    bridge.frame(300)
    bridge.key("RETURN")
    bridge.frame(40)
    bridge.ping()


@pytest.mark.parametrize("tag,prefix,action", [
    ("hostfs", "Host device", "Close Open Files"),
    ("pclink", "PCLink", "Close Open Files"),
    ("1030", "1030", "Close Network Connections"),
])
def test_device_close_context_actions(video_desktop, tag, prefix, action):
    emu, bridge = video_desktop
    bridge.device_add(tag)
    emu.navigate_system_config("Devices")
    row = next(item for item in emu.list_items("Configure System")
               if item["label"].startswith(prefix))
    emu.send(f"right_click_at {row['x'] + 24} {row['y'] + row['h'] / 2}")
    emu.wait_frames(4)
    click(emu, "", action)
    assert bridge.ping()["ok"]


def test_close_commands(video_desktop, tmp_path):
    emu, bridge = video_desktop
    bridge.device_clear()
    for name in ("Console.CloseFiles", "Console.CloseNetworkConnections"):
        assert not command(emu, name)["enabled"]
    (tmp_path / "TEST.TXT").write_bytes(b"Z123456789")
    bridge.device_add("hostfs", path1=str(tmp_path), lowercase=False)
    assert command(emu, "Console.CloseFiles")["enabled"]
    bridge.config_many(machine="800XL", basic="true")
    bridge.cold_reset()
    bridge.frame(160)
    type_line(bridge, 'OPEN #1,4,0,"H1:TEST.TXT"')
    type_line(bridge, "GET #1,A:PRINT A")
    assert "90" in bridge.screen_text(), bridge.screen_text()
    emu.send("run_command Console.CloseFiles")
    emu.wait_frames(3)
    type_line(bridge, "GET #1,A")
    # kATCIOStat_NotOpen ($85) from atcore/cio.h; the IOCB remains
    # assigned, but the host device's underlying channel is now closed.
    assert re.search(r"ERROR-\s*133", " ".join(bridge.screen_text()).upper())
    bridge.device_remove("hostfs")
    assert not command(emu, "Console.CloseFiles")["enabled"]
    bridge.device_add("pclink", path=str(tmp_path))
    assert command(emu, "Console.CloseFiles")["enabled"]
    emu.send("run_command Console.CloseFiles")
    bridge.device_clear()
    bridge.device_add("850")
    bridge.device_set("modem", parent="850")
    assert command(emu, "Console.CloseNetworkConnections")["enabled"]
    emu.send("run_command Console.CloseNetworkConnections")
    bridge.device_clear()
    for tag in ("1030", "sx212", "pocketmodem", "mpp1000e"):
        bridge.device_add(tag)
        assert command(emu, "Console.CloseNetworkConnections")["enabled"], tag
        emu.send("run_command Console.CloseNetworkConnections")
        bridge.device_remove(tag)
        assert not command(emu, "Console.CloseNetworkConnections")["enabled"], tag
    bridge.config("machine", "1400XL")
    assert command(emu, "Console.CloseNetworkConnections")["enabled"]
    emu.send("run_command Console.CloseNetworkConnections")
    bridge.config("machine", "800XL")


def test_1400xl_stereo_audio_toggle(video_desktop):
    emu, bridge = video_desktop
    bridge.config_many(machine="1400XL", basic="false", stereo="true")
    bridge.boot_bare()
    assert bridge.config("machine")["machine"] == "1400XL"
    assert bridge.config("stereo")["stereo"]
    # The modem control latch is $D108-$D10B. Bit 2 blocks internal
    # audio unless bits 3 and 5 provide the phone path (1400xl.cpp).
    for value in (0x04, 0x00, 0x2C, 0x04, 0x00):
        bridge.hwpoke(0xD108, value)
        bridge.frame(10)
        assert bridge.ping()["ok"]
    bridge.config("machine", "800XL")
