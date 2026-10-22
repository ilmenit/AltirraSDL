"""FujiNet activities prepare and boot through the existing managed runtime.

All downloads and mount requests use the local fixture. The process fixture
deliberately waits for an explicit UDP HELLO, so pending startup and cancellation
cannot accidentally fetch the public Lobby or connect to a BBS.
"""
import http.server
import importlib.util
from pathlib import Path
import shutil
import socket
import threading
import time

import pytest

from .test_explorers import click, enter
from .harness import AltirraTestHarness
from .test_fujinet import free_port, fujinet_desktop, wait_status
from .test_fujinet_file import managed_mount
from .test_fujinet_install import mock_github


ACTIVITIES = (
    ("lobby", "Network games / Lobby"),
    ("bbs", "BBS directory"),
)


def activity_done(emu):
    return wait_status(emu, lambda s: not s["activityPending"]
                       and not s["filePending"] and not s["installing"])


def stopped_runtime(emu):
    emu.pause()
    emu.send("fujinet stop")
    status = wait_status(emu, lambda s: not s["running"] and not s["stopping"])
    assert not status["connected"]
    emu.send("fujinet local_d1")
    return status


def hello(emu, peer):
    status = emu.send("fujinet status")
    peer.sendto(b"\xc1", ("127.0.0.1", status["port"]))
    return wait_status(emu, lambda s: s["connected"])


def test_unconfigured_panel_offers_setup_before_activities(fujinet_desktop):
    emu = fujinet_desktop
    emu.send("fujinet show")
    emu.wait_frames(5)
    labels = emu.get_item_labels("FujiNet")
    assert "Set up FujiNet automatically" in labels
    assert all(label not in labels for _, label in ACTIVITIES)
    assert "Setup and full controls" not in labels
    assert "##Executable" not in labels and "##UDP port" not in labels
    status = emu.send("fujinet status")
    assert not status["running"] and not status["installing"]
    assert not emu.query_state()["state"]["sim"]["running"]


def test_configured_runtime_shows_activities_and_collapses_setup(managed_mount):
    emu, _, _, _ = managed_mount
    stopped_runtime(emu)
    emu.send("fujinet show")
    emu.wait_frames(5)
    labels = emu.get_item_labels("FujiNet")
    assert all(label in labels for _, label in ACTIVITIES)
    assert "Setup and full controls" in labels
    assert "FujiNet-PC program" not in labels
    assert "Choose FujiNet-PC" not in labels
    assert "##Executable" not in labels and "##UDP port" not in labels
    click(emu, "FujiNet", "Setup and full controls")
    assert "FujiNet-PC program" in emu.get_item_labels("FujiNet")
    click(emu, "FujiNet", "FujiNet-PC program")
    assert "##Executable" in emu.get_item_labels("FujiNet")
    click(emu, "FujiNet", "Setup and full controls")
    assert "##Executable" not in emu.get_item_labels("FujiNet")
    assert all(label in emu.get_item_labels("FujiNet") for _, label in ACTIVITIES)
    assert not emu.send("fujinet status")["running"]


def test_external_peer_arrival_replaces_collapsed_local_setup(managed_mount):
    emu, handler, _, _ = managed_mount
    stopped_runtime(emu)
    emu.send("fujinet show")
    emu.wait_frames(5)
    assert "Setup and full controls" in emu.get_item_labels("FujiNet")
    assert "Boot Atari" not in emu.get_item_labels("FujiNet")
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as peer:
        peer.bind(("127.0.0.1", 0))
        hello(emu, peer)
        emu.wait_frames(5)
        labels = emu.get_item_labels("FujiNet")
        assert all(label not in labels for _, label in ACTIVITIES)
        assert "Setup and full controls" not in labels
        assert "Boot Atari" in labels
        assert not emu.find_item("FujiNet", "Boot Atari")["disabled"]
        status = emu.send("fujinet status")
        assert status["connected"] and not status["running"]
        assert not status["bootPending"] and not handler.posts
        assert not emu.query_state()["state"]["sim"]["running"]


@pytest.mark.parametrize("activity,label", ACTIVITIES)
@pytest.mark.parametrize("basic", (False, True))
def test_connected_activity_preserves_dirty_media_and_basic(
        managed_mount, activity, label, basic):
    emu, handler, game, url = managed_mount
    current = emu.send("query_command System.ToggleBASIC")["state"] == "checked"
    if current != basic:
        emu.send("run_command System.ToggleBASIC")
    emu.send("netplay_profile disk_create")
    emu.send("netplay_profile disk_write 113")
    before = emu.send("netplay_profile status")
    assert before["diskDirty"] and before["diskByte"] == 113
    disk_paths = emu.query_state()["state"]["disks"]
    emu.send("fujinet show")
    emu.wait_frames(4)
    assert label in emu.get_item_labels("FujiNet")

    # Both local and URL activity sources use the real worker/controller;
    # the override replaces only the public activity asset.
    source = str(game) if activity == "lobby" else url + "/game.xex"
    emu.send(f"fujinet activity {activity} {source}")
    result = activity_done(emu)
    assert not result["error"] and result["running"] and result["connected"]
    assert not result["bootPending"] and not result["open"]
    assert emu.query_state()["state"]["sim"]["running"]
    after = emu.send("netplay_profile status")
    assert after["diskObject"] == before["diskObject"]
    assert after["diskDirty"] and after["diskByte"] == 113
    assert emu.query_state()["state"]["disks"] == disk_paths
    assert (emu.send("query_command System.ToggleBASIC")["state"]
            == "checked") is basic
    assert not result["localD1Enabled"] and not result["fastBoot"]
    assert len(handler.posts) == 1
    assert handler.posts[0][1]["mode"] == "r"
    assert handler.posts[0][1]["host_slot"] == 2
    mounted = handler.sd_root / handler.mounted["path"].lstrip("/")
    assert mounted.read_bytes() == game.read_bytes()
    assert not list(handler.sd_root.rglob(".owner-*"))


def test_stopped_activity_starts_and_waits_without_booting_config(managed_mount):
    emu, handler, game, _ = managed_mount
    stopped_runtime(emu)
    before = emu.query_state()["state"]["sim"]
    emu.send(f"fujinet activity lobby {game}")
    waiting = wait_status(emu, lambda s: s["running"] and s["activityPending"])
    assert waiting["activity"] == "lobby" and waiting["activityMessage"]
    assert not waiting["bootPending"] and not waiting["filePending"]
    assert waiting["localD1Enabled"] and not handler.posts
    emu.wait_frames(12)
    assert emu.query_state()["state"]["sim"]["emulationTick"] == before["emulationTick"]
    assert not emu.query_state()["state"]["sim"]["running"]
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as peer:
        peer.bind(("127.0.0.1", 0))
        hello(emu, peer)
        result = activity_done(emu)
        assert not result["error"]
        assert len(handler.posts) == 1
        assert emu.query_state()["state"]["sim"]["running"]


@pytest.mark.parametrize("activity,label", ACTIVITIES)
def test_activity_button_cancel_while_connecting_has_no_late_boot(
        managed_mount, activity, label):
    emu, handler, _, _ = managed_mount
    stopped_runtime(emu)
    emu.send("fujinet show")
    emu.wait_frames(4)
    click(emu, "FujiNet", label)
    waiting = wait_status(emu, lambda s: s["running"] and s["activityPending"])
    assert waiting["activity"] == activity
    assert not waiting["filePending"] and not waiting["bootPending"]
    emu.wait_frames(3)
    click(emu, "FujiNet", "Cancel activity")
    activity_done(emu)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as peer:
        peer.bind(("127.0.0.1", 0))
        hello(emu, peer)
        emu.wait_frames(30)
        result = emu.send("fujinet status")
        assert not result["activityPending"] and not result["filePending"]
        assert not result["bootPending"] and result["localD1Enabled"]
        assert not handler.posts
        assert not emu.query_state()["state"]["sim"]["running"]


@pytest.mark.parametrize("activity,label", ACTIVITIES)
def test_cancel_activity_during_mount_never_boots(managed_mount, activity, label):
    emu, handler, game, _ = managed_mount
    handler.mode = "cancel-post"
    emu.send(f"fujinet activity {activity} {game}")
    deadline = time.monotonic() + 5
    while not handler.posts and time.monotonic() < deadline:
        time.sleep(.01)
    assert handler.posts, "Activity did not reach its mount request"
    assert emu.send("fujinet status")["filePending"]
    emu.send("fujinet cancel_activity")
    activity_done(emu)
    emu.wait_frames(45)
    result = emu.send("fujinet status")
    assert not result["activityPending"] and not result["filePending"]
    assert not emu.query_state()["state"]["sim"]["running"]
    assert not list(handler.sd_root.rglob(".owner-*"))


def test_activity_image_failure_is_recoverable_without_boot(managed_mount, tmp_path):
    emu, handler, game, _ = managed_mount
    invalid = tmp_path / "broken activity.xex"
    invalid.write_bytes(b"not an Atari executable")
    emu.send(f"fujinet activity bbs {invalid}")
    failed = activity_done(emu)
    assert failed["error"] and not handler.posts
    assert not failed["bootPending"]
    assert not emu.query_state()["state"]["sim"]["running"]
    emu.send(f"fujinet activity bbs {game}")
    recovered = activity_done(emu)
    assert not recovered["error"] and len(handler.posts) == 1
    assert emu.query_state()["state"]["sim"]["running"]


def test_activity_rejects_overlapping_launch_without_replacing_pending_job(managed_mount):
    emu, handler, game, _ = managed_mount
    stopped_runtime(emu)
    emu.send(f"fujinet activity lobby {game}")
    wait_status(emu, lambda s: s["running"] and s["activityPending"])
    rejected = emu.send_raw(f"fujinet activity bbs {game}")
    assert not rejected["ok"] and rejected["error"]
    pending = emu.send("fujinet status")
    assert pending["activityPending"] and pending["activity"] == "lobby"
    assert not handler.posts
    emu.send("fujinet cancel_activity")
    activity_done(emu)


def test_profile_switch_cancels_waiting_activity(managed_mount):
    emu, handler, game, _ = managed_mount
    target = emu.send("profile_test create")["id"]
    stopped_runtime(emu)
    emu.send(f"fujinet activity bbs {game}")
    wait_status(emu, lambda s: s["running"] and s["activityPending"])
    emu.send(f"profile_test activate {target}")
    cancelled = activity_done(emu)
    assert cancelled["error"] and not handler.posts
    emu.wait_frames(15)
    assert not emu.send("fujinet status")["activityPending"]
    assert not handler.posts


def test_disconnect_cancels_activity_before_reconnect(managed_mount):
    emu, handler, game, _ = managed_mount
    stopped_runtime(emu)
    emu.send(f"fujinet activity lobby {game}")
    wait_status(emu, lambda s: s["running"] and s["activityPending"])
    disconnected = emu.send("fujinet disconnect")
    assert not disconnected["activityPending"] and not disconnected["listening"]
    emu.send("fujinet reconnect")
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as peer:
        peer.bind(("127.0.0.1", 0))
        hello(emu, peer)
        emu.wait_frames(20)
        assert not emu.send("fujinet status")["activityPending"]
        assert not handler.posts
        assert not emu.query_state()["state"]["sim"]["running"]


def test_cartridge_inserted_during_mount_prevents_automatic_boot(managed_mount):
    emu, handler, game, _ = managed_mount
    emu.send("debugger_console e 1000 5A")
    handler.mode = "cancel-post"
    emu.send(f"fujinet activity lobby {game}")
    deadline = time.monotonic() + 5
    while not handler.posts and time.monotonic() < deadline:
        time.sleep(.01)
    assert handler.posts, "Activity did not reach the blocked mount request"
    assert emu.send("fujinet status")["filePending"]
    emu.send("netplay_profile cart_create")
    emu.send("netplay_profile cart_write 113")
    inserted = emu.send("netplay_profile status")
    assert inserted["cartObject"] and inserted["cartDirty"]
    result = activity_done(emu)
    assert result["error"] and "cartridge" in result["error"].lower()
    emu.wait_frames(30)
    assert not emu.query_state()["state"]["sim"]["running"]
    assert emu.mem_read(0x1000, 1) == [0x5A]
    retained = emu.send("netplay_profile status")
    assert retained["cartObject"] == inserted["cartObject"]
    assert retained["cartDirty"] and retained["cartByte"] == 113
    assert not list(handler.sd_root.rglob(".owner-*"))


def test_fresh_activity_install_waits_for_connection_without_config_boot(
        emu, mock_github, monkeypatch, tmp_path):
    # The official-shaped mock release contains only the process fixture;
    # a separate local server supplies the existing mount API fixture.
    source = Path(__file__).resolve().parents[2] / (
        "src/AltirraSDL/source/media/tests/fujinet_mount_test.py")
    spec = importlib.util.spec_from_file_location("activity_install_mount_fixture", source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    handler = module.Handler
    handler.mode, handler.posts, handler.mounted = "", [], None
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    game = tmp_path / "installed activity.xex"
    game.write_bytes(module.XEX)
    monkeypatch.setenv("SDL_VIDEODRIVER", "dummy")
    monkeypatch.setenv("SDL_AUDIODRIVER", "dummy")
    monkeypatch.setenv("ALTIRRA_DISPLAY_BACKEND", "sdlrenderer")
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "fresh install config"))
    monkeypatch.setenv("ALTIRRA_FUJINET_RELEASES_URL", mock_github)
    try:
        with AltirraTestHarness(executable=emu.executable,
                               extra_args=["--ui-mode", "desktop"]) as fresh:
            fresh.close_dialog("SetupWizard")
            fresh.pause()
            # Set the local fixture's web port without supplying an installed
            # program. Automatic setup must replace this missing selection.
            missing = tmp_path / "not installed yet" / "fujinet"
            fresh.send(f"fujinet runtime {server.server_port} {missing}")
            fresh.send(f"fujinet listen {free_port()} 0 100")
            fresh.send("fujinet local_d1")
            before = fresh.query_state()["state"]["sim"]
            fresh.send(f"fujinet activity bbs {game}")
            waiting = wait_status(fresh, lambda s: s["running"]
                                  and s["activityPending"] and not s["installing"],
                                  timeout=20)
            assert waiting["runtimeVersion"].startswith("fujinet-pc-ATARI_v9.9-dev_")
            installed = Path(waiting["executable"])
            assert installed.is_file() and installed != missing
            assert "FujiNet-PC" in installed.parts
            assert not waiting["bootPending"] and not waiting["filePending"]
            assert not waiting["autoSetupPending"] and waiting["localD1Enabled"]
            assert not handler.posts
            fresh.wait_frames(15)
            assert fresh.query_state()["state"]["sim"]["emulationTick"] == before["emulationTick"]
            assert not fresh.query_state()["state"]["sim"]["running"]
            handler.sd_root = Path(waiting["stateDirectory"]) / "SD"
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as peer:
                peer.bind(("127.0.0.1", 0))
                hello(fresh, peer)
                result = activity_done(fresh)
                assert not result["error"] and not result["bootPending"]
                assert len(handler.posts) == 1
                assert handler.posts[0][1]["mode"] == "r"
                mounted = handler.sd_root / handler.mounted["path"].lstrip("/")
                assert mounted.read_bytes() == game.read_bytes()
                assert fresh.query_state()["state"]["sim"]["running"]
            fresh.send("fujinet stop")
            wait_status(fresh, lambda s: not s["running"])
    finally:
        server.shutdown()
        server.server_close()


def test_failed_activity_then_manual_setup_still_boots_config(
        emu, monkeypatch, tmp_path):
    suffix = ".exe" if emu.executable.lower().endswith(".exe") else ""
    fixture = Path(emu.executable).resolve().parents[2] / (
        "src/AltirraSDL/source/media/tests/altirra-fujinet-pc-test" + suffix)
    if not fixture.is_file():
        pytest.skip("Build the self-hosted FujiNet process fixture")
    runtime = tmp_path / "manual runtime"
    (runtime / "data").mkdir(parents=True)
    (runtime / "SD").mkdir()
    executable = runtime / ("fujinet" + suffix)
    shutil.copy2(fixture, executable)
    (runtime / "fnconfig.ini").write_text(
        "[BOIP]\nenabled=1\nhost=localhost\nport=9997\n")
    monkeypatch.setenv("SDL_VIDEODRIVER", "dummy")
    monkeypatch.setenv("SDL_AUDIODRIVER", "dummy")
    monkeypatch.setenv("ALTIRRA_DISPLAY_BACKEND", "sdlrenderer")
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "manual recovery config"))
    monkeypatch.setenv("ALTIRRA_FUJINET_RELEASES_URL",
                       f"http://127.0.0.1:{free_port()}/releases")
    with AltirraTestHarness(executable=emu.executable,
                           extra_args=["--ui-mode", "desktop"]) as fresh:
        fresh.close_dialog("SetupWizard")
        fresh.send("set_window_size 1280 1000")
        fresh.pause()
        fresh.send("file_dialog_builtin on")
        fresh.send("fujinet show")
        fresh.wait_frames(4)
        fresh.send(f"fujinet listen {free_port()} 0 100")
        fresh.send("fujinet local_d1")
        click(fresh, "FujiNet", "Set up FujiNet automatically")
        first = wait_status(fresh, lambda s: not s["installing"] and s["error"])
        assert not first["running"] and not first["autoSetupPending"]
        # An activity owns a setup-only continuation. Its failure must not
        # leak that intent into the guide's normal manual-pick fallback.
        # Activities stay hidden until setup succeeds. The controller path
        # still accepts an explicit automation request while unconfigured.
        fresh.send("fujinet activity lobby")
        failed = activity_done(fresh)
        assert failed["error"] and not failed["running"]
        click(fresh, "FujiNet", "Download FujiNet-PC yourself instead")
        click(fresh, "FujiNet", "Choose FujiNet-PC")
        enter(fresh, "Open File", "##path", runtime, True)
        enter(fresh, "Open File", "##filename", executable.name, True)
        started = wait_status(fresh, lambda s: s["running"])
        try:
            assert started["bootPending"], (
                "The guide's manual setup lost its CONFIG boot continuation")
            assert not fresh.query_state()["state"]["sim"]["running"]
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as peer:
                peer.bind(("127.0.0.1", 0))
                hello(fresh, peer)
                booted = wait_status(fresh, lambda s: not s["bootPending"])
                assert not booted["activityPending"] and not booted["filePending"]
                assert not booted["localD1Enabled"] and not booted["fastBoot"]
                assert fresh.query_state()["state"]["sim"]["running"]
        finally:
            fresh.pause()
            fresh.send("fujinet stop")
            wait_status(fresh, lambda s: not s["running"])
