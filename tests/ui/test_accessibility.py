"""Emulated text snapshots and the real Linux AT-SPI D-Bus provider."""
from contextlib import contextmanager
from pathlib import Path
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "src/AltirraSDL/AltirraBridge/sdk/python"))
from tests.ui.harness import AltirraTestHarness
from altirra_bridge import AltirraBridge


def until(get, predicate, timeout=8):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = get()
        if predicate(value):
            return value
        time.sleep(0.04)
    raise AssertionError(f"Timed out waiting for accessibility update: {value!r}")


@contextmanager
def sample_display(executable, directory):
    with AltirraTestHarness(executable=executable, extra_args=["--bridge"]) as emu:
        emu.close_dialog("SetupWizard")
        emu.wait_frames(3)
        image = directory / "text.xex"
        image.write_bytes(b"\xff\xff" + struct.pack("<HH", 0x2000, 0x2002)
                          + b"\x4c\x00\x20"
                          + struct.pack("<HHH", 0x2E0, 0x2E1, 0x2000))
        emu.send(f"boot_image {image}")
        emu.wait_frames(120)
        token = Path(tempfile.gettempdir()) / f"altirra-bridge-{emu._proc.pid}.token"
        with AltirraBridge.from_token_file(str(token)) as control:
            control.pause()
            # SAVMSC is the canonical OS screen-memory pointer (kerneldb.h).
            address = control.peek16(0x58)
            first = "HELLO SCREEN READER"
            second = "SECOND LINE"
            internal = bytes(ord(c) - 32 for c in first)
            internal += bytes(40 - len(first))
            internal += bytes(ord(c) - 32 for c in second)
            control.memload(address, internal)
            control.frame(2)
            control.ping()
            emu.wait_frames(6)
            yield emu, control, address


@pytest.fixture
def accessibility_display(request, monkeypatch, tmp_path):
    monkeypatch.setenv("SDL_VIDEODRIVER", "dummy")
    monkeypatch.setenv("SDL_AUDIODRIVER", "dummy")
    monkeypatch.setenv("ALTIRRA_DISPLAY_BACKEND", "sdlrenderer")
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "config"))
    if sys.platform.startswith("linux"):
        monkeypatch.setenv("DBUS_SESSION_BUS_ADDRESS", f"unix:path={tmp_path / 'missing-session-bus'}")
    with sample_display(request.config.getoption("--emu-path"), tmp_path) as display:
        yield display


def test_screen_reader_tracks_antic_text_without_resuming(accessibility_display):
    emu, control, address = accessibility_display
    emu.send("accessibility enabled")
    state = until(lambda: emu.send("accessibility status"),
                  lambda s: s["text"].startswith("HELLO SCREEN READER\nSECOND LINE"))
    if sys.platform.startswith("linux"):
        status = until(lambda: emu.send("accessibility status"),
                       lambda s: s.get("providerState") == "connecting" and "bus" in s.get("providerMessage", ""))
        assert "bus" in status["providerMessage"]
    assert emu.query_state()["state"]["sim"]["paused"]
    # Reverse-video characters have the same spoken text.
    control.memload(address, bytes((ord(c) - 32) | 0x80 for c in "UPDATED"))
    control.frame(2)
    control.ping()
    until(lambda: emu.send("accessibility status"),
          lambda s: s["text"].startswith("UPDATEDCREEN READER"))
    assert emu.query_state()["state"]["sim"]["paused"]
    emu.send("accessibility disabled")
    until(lambda: emu.send("accessibility status"), lambda s: not s["text"])
    emu.send("accessibility enabled")
    until(lambda: emu.send("accessibility status"), lambda s: s["text"].startswith("UPDATED"))


@pytest.mark.skipif(not sys.platform.startswith("linux"), reason="Linux optional desktop GIO runtime")
def test_missing_desktop_runtime_reports_unavailable(request, monkeypatch, tmp_path):
    compiler = shutil.which("cc")
    if not compiler:
        pytest.skip("A C compiler is needed for runtime loader failure injection")
    source = tmp_path / "missing_gio.c"
    source.write_text('''#define _GNU_SOURCE
#include <dlfcn.h>
#include <string.h>
void *dlopen(const char *name, int flags) {
    static void *(*real_dlopen)(const char *, int);
    if (!real_dlopen) real_dlopen = dlsym(RTLD_NEXT, "dlopen");
    if (name && !strcmp(name, "libgio-2.0.so.0")) return 0;
    return real_dlopen(name, flags);
}
''')
    library = tmp_path / "missing_gio.so"
    subprocess.run([compiler, "-shared", "-fPIC", str(source), "-ldl", "-o", str(library)], check=True)
    previous_preload = os.environ.get("LD_PRELOAD", "")
    monkeypatch.setenv("LD_PRELOAD", f"{previous_preload}:{library}" if previous_preload else str(library))
    monkeypatch.setenv("SDL_VIDEODRIVER", "dummy")
    monkeypatch.setenv("SDL_AUDIODRIVER", "dummy")
    monkeypatch.setenv("ALTIRRA_DISPLAY_BACKEND", "sdlrenderer")
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "config"))
    with sample_display(request.config.getoption("--emu-path"), tmp_path) as (emu, control, address):
        emu.send("accessibility enabled")
        status = until(lambda: emu.send("accessibility status"),
                       lambda s: s.get("providerState") == "unavailable")
        assert "libgio-2.0.so.0" in status["providerMessage"]
        assert "Install" in status["providerMessage"]
        assert status["text"].startswith("HELLO SCREEN READER")
        emu.send("accessibility disabled")
        until(lambda: emu.send("accessibility status"), lambda s: s.get("providerState") == "disabled")


def daemon_path(name):
    return shutil.which(name) or next((str(p / name) for p in
        (Path("/usr/libexec"), Path("/usr/lib/at-spi2-core")) if (p / name).is_file()), None)


@pytest.mark.skipif(not sys.platform.startswith("linux"), reason="Linux native AT-SPI integration")
def test_native_atspi_text_ranges_events_and_lifecycle(request, tmp_path):
    pytest.importorskip("gi", reason="Native AT-SPI probe uses the desktop GIO introspection binding")
    if not shutil.which("dbus-run-session") or not daemon_path("at-spi-bus-launcher") or not daemon_path("at-spi2-registryd"):
        pytest.skip("AT-SPI daemons and dbus-run-session are required")
    executable = request.config.getoption("--emu-path") or AltirraTestHarness._find_executable()
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
               ALTIRRA_DISPLAY_BACKEND="sdlrenderer", XDG_CONFIG_HOME=str(tmp_path / "config"))
    # Isolate registry publication and all test events from the user's desktop.
    subprocess.run(["dbus-run-session", "--", sys.executable, __file__,
                    "--native-probe", executable, str(tmp_path)],
                   env=env, timeout=50, check=True)


def native_probe(executable, directory):
    import gi
    gi.require_version("Gio", "2.0")
    from gi.repository import Gio, GLib

    launcher = subprocess.Popen([daemon_path("at-spi-bus-launcher"), "--launch-immediately"])
    registry = None
    bus = None
    try:
        time.sleep(0.3)
        session = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        address = session.call_sync("org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus",
                                    "GetAddress", None, None, 0, 3000, None).unpack()[0]
        bus = Gio.DBusConnection.new_for_address_sync(address,
            Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION, None, None)
        registry = subprocess.Popen([daemon_path("at-spi2-registryd")])
        time.sleep(0.3)

        def call(name, path, interface, method, signature=None, values=()):
            args = GLib.Variant(signature, values) if signature else None
            return bus.call_sync(name, path, interface, method, args, None, 0, 3000, None).unpack()

        accessible = "org.a11y.atspi.Accessible"
        text_interface = "org.a11y.atspi.Text"
        root = "/org/a11y/atspi/accessible/root"

        def apps():
            return call("org.a11y.atspi.Registry", root, accessible, "GetChildren")[0]

        def find_app():
            for name, path in apps():
                try:
                    title = call(name, path, "org.freedesktop.DBus.Properties", "Get", "(ss)",
                                 (accessible, "Name"))[0]
                except GLib.Error:
                    # Registry enumeration and a disconnected application's
                    # NameOwnerChanged cleanup are separate bus messages.
                    continue
                if title == "AltirraSDL":
                    return name, path
            return None

        with sample_display(executable, directory) as (emu, control, screen_address):
            emu.send("accessibility enabled")
            name, path = until(find_app, bool)
            until(lambda: emu.send("accessibility status"), lambda s: s.get("providerState") == "active")
            frame = call(name, path, accessible, "GetChildAtIndex", "(i)", (0,))[0][1]
            screen = call(name, frame, accessible, "GetChildAtIndex", "(i)", (0,))[0][1]

            def text(method, signature=None, values=()):
                return call(name, screen, text_interface, method, signature, values)

            document = until(lambda: text("GetText", "(ii)", (0, -1))[0],
                             lambda s: s.startswith("HELLO SCREEN READER\nSECOND LINE"))
            count = call(name, screen, "org.freedesktop.DBus.Properties", "Get", "(ss)",
                         (text_interface, "CharacterCount"))[0]
            assert count == len(document)
            assert text("GetStringAtOffset", "(iu)", (7, 1)) == ("SCREEN", 6, 12)
            assert text("GetTextAtOffset", "(iu)", (7, 1)) == ("SCREEN ", 6, 13)
            assert text("GetTextAtOffset", "(iu)", (7, 2)) == (" SCREEN", 5, 12)
            assert text("GetTextBeforeOffset", "(iu)", (7, 1)) == ("HELLO ", 0, 6)
            assert text("GetTextAfterOffset", "(iu)", (7, 1)) == ("READER\n", 13, 20)
            assert text("GetCharacterAtOffset", "(i)", (0,)) == (ord("H"),)
            assert text("GetCharacterExtents", "(iu)", (2147483647, 1)) == (0, 0, 0, 0)
            assert text("GetRangeExtents", "(iiu)", (-2147483648, 2147483647, 1)) == (0, 0, 0, 0)

            first = text("GetCharacterExtents", "(iu)", (0, 1))
            last = text("GetCharacterExtents", "(iu)", (5, 1))
            # A half-open range ending at the next line's start must not
            # acquire any of that next line's geometry. A caret stays one line.
            first_line = text("GetRangeExtents", "(iiu)", (0, 20, 1))
            assert first_line[1] == first[1] and first_line[3] == first[3]
            caret = text("GetRangeExtents", "(iiu)", (20, 20, 1))
            second = text("GetCharacterExtents", "(iu)", (20, 1))
            assert caret[0] == second[0] and caret[1] == second[1]
            assert caret[2] == 0 and caret[3] == second[3]
            # Clip the first and sixth glyph by one pixel on each side.
            x, y = first[0] + 1, first[1]
            width = last[0] + last[2] - 1 - x
            for clip, expected in [(0, (0, 6)), (1, (1, 6)), (2, (0, 5)), (3, (1, 5))]:
                ranges = text("GetBoundedRanges", "(iiiiuuu)", (x, y, width, first[3], 1, clip, 3))[0]
                assert len(ranges) == 1 and ranges[0][:2] == expected, (clip, ranges)

            cache = call(name, "/org/a11y/atspi/cache", "org.a11y.atspi.Cache", "GetItems")[0]
            assert len(cache) == 3 and cache[-1][6] == "Screen"
            assert text_interface in cache[-1][5]
            events = []
            subscription = bus.signal_subscribe(name, "org.a11y.atspi.Event.Object", None,
                screen, None, 0, lambda *args: events.append((args[4], args[5].unpack())))
            assert text("SetCaretOffset", "(i)", (6,)) == (True,)
            assert text("AddSelection", "(ii)", (0, 5)) == (True,)
            assert text("GetSelection", "(i)", (0,)) == (0, 5)
            assert text("RemoveSelection", "(i)", (0,)) == (True,)
            control.memload(screen_address, bytes(ord(c) - 32 for c in "UPDATED"))
            control.frame(2)
            control.ping()
            until(lambda: text("GetText", "(ii)", (0, 7))[0], lambda s: s == "UPDATED")
            context = GLib.MainContext.default()
            until(lambda: (context.iteration(False), events)[1],
                  lambda values: any(event[0] == "TextChanged" for event in values))
            assert any(event[0] == "TextCaretMoved" for event in events)
            assert any(event[0] == "TextSelectionChanged" for event in events)
            bus.signal_unsubscribe(subscription)

            emu.send("accessibility disabled")
            until(find_app, lambda value: value is None)
            emu.send("accessibility enabled")
            until(find_app, bool)
            # The bus connection survives registry replacement; its owner
            # notification must cause a fresh Embed handshake and cache tree.
            registry.terminate()
            registry.wait(timeout=5)
            registry = subprocess.Popen([daemon_path("at-spi2-registryd")])
            time.sleep(0.3)
            until(find_app, bool)
        until(find_app, lambda value: value is None)
    finally:
        if bus is not None:
            bus.close_sync(None)
        for process in (registry, launcher):
            if process is not None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == "__main__" and sys.argv[1] == "--native-probe":
    native_probe(sys.argv[2], Path(sys.argv[3]))
