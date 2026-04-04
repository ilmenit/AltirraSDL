"""
AltirraSDL UI Test Harness

Manages the lifecycle of an AltirraSDL --test-mode process and provides
a clean API for sending commands and receiving JSON responses over the
Unix domain socket.

Usage:
    with AltirraTestHarness(executable="./build/linux-release/src/AltirraSDL/AltirraSDL") as emu:
        emu.ping()
        state = emu.query_state()
        items = emu.list_items("Configure System")
        emu.open_dialog("SystemConfig")
        emu.click("Configure System", "CPU")
        emu.wait_frames(3)
        emu.close_dialog("SystemConfig")
"""

import json
import os
import signal
import socket
import subprocess
import time
from pathlib import Path
from typing import Optional


class AltirraTestHarness:
    """Launches AltirraSDL in test mode and communicates via Unix socket."""

    def __init__(self, executable: Optional[str] = None, timeout: float = 10.0,
                 startup_timeout: float = 15.0, extra_args: Optional[list] = None):
        self.executable = executable or self._find_executable()
        self.timeout = timeout
        self.startup_timeout = startup_timeout
        self.extra_args = extra_args or []
        self._proc: Optional[subprocess.Popen] = None
        self._sock: Optional[socket.socket] = None
        self._sock_path: Optional[str] = None
        self._recv_buf = ""

    @staticmethod
    def _find_executable() -> str:
        """Search common build locations for the AltirraSDL binary."""
        candidates = [
            "build/linux-release/src/AltirraSDL/AltirraSDL",
            "build/linux-debug/src/AltirraSDL/AltirraSDL",
            "build/src/AltirraSDL/AltirraSDL",
        ]
        root = Path(__file__).resolve().parent.parent.parent
        for c in candidates:
            p = root / c
            if p.is_file() and os.access(p, os.X_OK):
                return str(p)
        raise FileNotFoundError(
            "AltirraSDL binary not found. Pass executable= or build the project first.\n"
            f"Searched: {[str(root / c) for c in candidates]}"
        )

    def start(self):
        """Launch the emulator process and connect to its test socket."""
        cmd = [self.executable, "--test-mode"] + self.extra_args
        self._proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        # Wait for the socket file to appear
        self._sock_path = f"/tmp/altirra-test-{self._proc.pid}.sock"
        deadline = time.monotonic() + self.startup_timeout
        while time.monotonic() < deadline:
            if self._proc.poll() is not None:
                stderr = self._proc.stderr.read().decode(errors="replace")
                raise RuntimeError(
                    f"AltirraSDL exited immediately with code {self._proc.returncode}.\n"
                    f"stderr: {stderr[:2000]}"
                )
            if os.path.exists(self._sock_path):
                break
            time.sleep(0.1)
        else:
            self.stop()
            raise TimeoutError(
                f"Socket {self._sock_path} did not appear within {self.startup_timeout}s"
            )

        # Connect
        self._sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self._sock.settimeout(self.timeout)
        self._sock.connect(self._sock_path)
        self._recv_buf = ""

    def stop(self):
        """Shut down the emulator process and clean up."""
        if self._sock:
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None

        if self._proc:
            # Try graceful shutdown first
            try:
                self._proc.terminate()
                self._proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self._proc.kill()
                self._proc.wait(timeout=5)
            self._proc = None

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.stop()
        return False

    # ── Low-level protocol ───────────────────────────────────────────────

    def send_raw(self, command: str) -> dict:
        """Send a newline-delimited command and return the parsed JSON response."""
        if not self._sock:
            raise RuntimeError("Not connected")

        self._sock.sendall((command.rstrip("\n") + "\n").encode())

        # Read until we get a complete line
        while "\n" not in self._recv_buf:
            try:
                data = self._sock.recv(65536)
            except socket.timeout:
                raise TimeoutError(f"No response within {self.timeout}s for: {command}")
            if not data:
                raise ConnectionError("Socket closed by emulator")
            self._recv_buf += data.decode()

        line, self._recv_buf = self._recv_buf.split("\n", 1)
        return json.loads(line)

    def send(self, command: str) -> dict:
        """Send a command and return the response. Raises on error responses."""
        resp = self.send_raw(command)
        if not resp.get("ok", False) and "error" in resp:
            raise CommandError(command, resp["error"])
        return resp

    # ── High-level API ───────────────────────────────────────────────────

    def ping(self) -> dict:
        return self.send("ping")

    def query_state(self) -> dict:
        return self.send("query_state")

    def list_items(self, window_filter: str = "") -> list[dict]:
        """Return the list of visible widgets, optionally filtered by window name."""
        if window_filter:
            resp = self.send(f'list_items "{window_filter}"')
        else:
            resp = self.send("list_items")
        return resp.get("items", [])

    def list_dialogs(self) -> list[str]:
        resp = self.send("list_dialogs")
        return resp.get("dialogs", [])

    def open_dialog(self, name: str) -> dict:
        return self.send(f"open_dialog {name}")

    def close_dialog(self, name: str) -> dict:
        return self.send(f"close_dialog {name}")

    def click(self, window: str, label: str) -> dict:
        return self.send(f'click "{window}" "{label}"')

    def wait_frames(self, n: int = 1) -> dict:
        return self.send(f"wait_frames {n}")

    def query_window(self, title: str) -> dict:
        return self.send(f'query_window "{title}"')

    def screenshot(self, path: str) -> dict:
        return self.send(f"screenshot {path}")

    def cold_reset(self) -> dict:
        return self.send("cold_reset")

    def warm_reset(self) -> dict:
        return self.send("warm_reset")

    def pause(self) -> dict:
        return self.send("pause")

    def resume(self) -> dict:
        return self.send("resume")

    def boot_image(self, path: str) -> dict:
        return self.send(f"boot_image {path}")

    def attach_disk(self, drive: int, path: str) -> dict:
        return self.send(f"attach_disk {drive} {path}")

    def load_state(self, path: str) -> dict:
        return self.send(f"load_state {path}")

    def save_state(self, path: str) -> dict:
        return self.send(f"save_state {path}")

    # ── Convenience helpers ──────────────────────────────────────────────

    def get_dialog_state(self, name: str) -> bool:
        """Check whether a dialog is currently open."""
        state = self.query_state()
        return state.get("state", {}).get("dialogs", {}).get(name, False)

    def get_sim_state(self) -> dict:
        """Return the simulator state sub-dict."""
        state = self.query_state()
        return state.get("state", {}).get("sim", {})

    def find_item(self, window: str, label: str) -> Optional[dict]:
        """Find a specific widget by window and label, or None."""
        items = self.list_items(window)
        for item in items:
            if item.get("label") == label:
                return item
        return None

    def get_item_labels(self, window: str = "") -> list[str]:
        """Return just the labels of all visible widgets in a window."""
        return [i["label"] for i in self.list_items(window) if i.get("label")]

    def assert_item_exists(self, window: str, label: str, expected_type: str = None):
        """Assert that a widget with the given label exists."""
        item = self.find_item(window, label)
        if item is None:
            available = self.get_item_labels(window)
            raise AssertionError(
                f"Widget '{label}' not found in '{window}'.\n"
                f"Available ({len(available)}): {available[:20]}..."
            )
        if expected_type and item.get("type") != expected_type:
            raise AssertionError(
                f"Widget '{label}' has type '{item.get('type')}', expected '{expected_type}'"
            )
        return item

    def assert_checkbox_state(self, window: str, label: str, expected: bool):
        """Assert a checkbox exists and has the expected checked state."""
        item = self.assert_item_exists(window, label, "checkbox")
        actual = item.get("checked", False)
        if actual != expected:
            raise AssertionError(
                f"Checkbox '{label}': expected checked={expected}, got {actual}"
            )

    # Pages near the bottom of the sidebar that may need scrolling to be visible
    _SYSCONFIG_BOTTOM_PAGES = {
        "Compat DB", "Display", "Ease of Use", "Error Handling",
        "Input", "Window Caption", "Workarounds",
    }

    def navigate_system_config(self, page: str):
        """Open SystemConfig and navigate to a specific page by clicking sidebar entries.

        The sidebar is a flat list of Selectables (no tree nodes). Items near
        the bottom ("Emulator" section) may be off-screen in the scrollable
        child window. We scroll down by clicking a nearby visible item first,
        then click the target.
        """
        if not self.get_dialog_state("SystemConfig"):
            self.open_dialog("SystemConfig")
            self.wait_frames(5)

        win = "Configure System"

        # If the target page might be off-screen, scroll down by clicking
        # a visible item near the bottom of the visible area first
        if page in self._SYSCONFIG_BOTTOM_PAGES:
            item = self.find_item(win, page)
            if item is None:
                # "Flash" is the last item in the "Media" section and should
                # be near the bottom of the visible area; clicking it scrolls
                # the sidebar down, making "Emulator" section items visible
                try:
                    self.click(win, "Flash")
                    self.wait_frames(5)
                except CommandError:
                    pass

        self.click(win, page)
        self.wait_frames(8)


class CommandError(Exception):
    """Raised when the emulator returns an error response."""
    def __init__(self, command: str, error: str):
        self.command = command
        self.error = error
        super().__init__(f"Command '{command}' failed: {error}")
