"""
Pytest fixtures for AltirraSDL UI tests.

The emulator is launched once per test session (session scope) to avoid
the overhead of starting/stopping it for every test. Individual tests
should clean up after themselves (close dialogs, reset state).

Override the executable path with:
    pytest --emu-path=/path/to/AltirraSDL
"""

import pytest
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


@pytest.fixture(scope="session")
def emu(request) -> AltirraTestHarness:
    """Session-scoped fixture: one emulator instance for all tests."""
    path = request.config.getoption("--emu-path")
    timeout = request.config.getoption("--emu-timeout")

    harness = AltirraTestHarness(executable=path, timeout=timeout)
    harness.start()
    yield harness
    harness.stop()


@pytest.fixture(autouse=True)
def _cleanup_dialogs(emu: AltirraTestHarness):
    """Close all dialogs after each test to leave a clean slate."""
    yield
    # After test: close any open dialogs
    try:
        state = emu.query_state()
        dialogs = state.get("state", {}).get("dialogs", {})
        for name, is_open in dialogs.items():
            if is_open:
                emu.close_dialog(name)
        emu.wait_frames(2)
    except Exception:
        pass  # best-effort cleanup
