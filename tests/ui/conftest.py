"""
Pytest fixtures for AltirraSDL UI tests.

Each module gets an emulator and private settings directory. Tests within a
module share that instance and clean up dialogs; unrelated modules never
inherit mounted media, stopped CPUs, or modified settings from earlier tests.

Override the executable path with:
    pytest --emu-path=/path/to/AltirraSDL
"""

import pytest
import tempfile
from .harness import AltirraTestHarness


def pytest_addoption(parser):
    parser.addoption(
        "--emu-path",
        default=None,
        help="Path to AltirraSDL binary (auto-detected if not set)",
    )
    parser.addoption(
        "--emu-timeout",
        default=10.0,
        type=float,
        help="Socket command timeout in seconds (default: 10)",
    )


@pytest.fixture(scope="module")
def emu(request) -> AltirraTestHarness:
    """Module-scoped emulator with settings isolated from the user's profile."""
    path = request.config.getoption("--emu-path")
    timeout = request.config.getoption("--emu-timeout")

    harness = AltirraTestHarness(executable=path, timeout=timeout)
    with tempfile.TemporaryDirectory(prefix="altirra-ui-module-") as config:
        with pytest.MonkeyPatch.context() as environment:
            environment.setenv("XDG_CONFIG_HOME", config)
            try:
                harness.start()
                # Child harnesses inherit module isolation; per-test
                # monkeypatches can still override it and restore it safely.
                # Native pickers are outside the ImGui automation surface.
                harness.send("file_dialog_builtin on")
                harness.close_dialog("SetupWizard")
                harness.wait_frames(3)
                yield harness
            finally:
                harness.stop()


@pytest.fixture(autouse=True)
def _cleanup_dialogs(emu: AltirraTestHarness):
    """Close all dialogs after each test to leave a clean slate."""
    yield
    # A file-first Explorer owns its picker before it owns any document.
    # Closing the Explorer alone cannot cancel that outstanding request.
    try:
        def cancel_pickers():
            for _ in range(8):
                cancel = next((item for item in emu.list_items()
                               if item["label"] == "Cancel"
                               and "##ATFileDialogFallback" in item["window"]), None)
                if cancel is None:
                    return
                emu.click(cancel["window"], "Cancel")
                emu.wait_frames(6)

        cancel_pickers()
        state = emu.query_state()
        dialogs = state.get("state", {}).get("dialogs", {})
        for name, is_open in dialogs.items():
            if is_open:
                emu.close_dialog(name)
        emu.wait_frames(2)
        cancel_pickers()
    except Exception:
        pass  # best-effort cleanup
