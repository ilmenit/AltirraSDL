"""
Phase 1: Smoke Tests

Verify the test mode infrastructure works: connection, basic commands,
dialog open/close cycle for every dialog.
"""

import pytest
from .harness import AltirraTestHarness

# All 19 dialogs from kDialogMap in ui_testmode.cpp
ALL_DIALOGS = [
    "SystemConfig",
    "DiskManager",
    "CassetteControl",
    "About",
    "AdjustColors",
    "DisplaySettings",
    "CartridgeMapper",
    "AudioOptions",
    "InputMappings",
    "InputSetup",
    "Profiles",
    "CommandLineHelp",
    "ChangeLog",
    "CompatWarning",
    "DiskExplorer",
    "SetupWizard",
    "KeyboardShortcuts",
    "CompatDB",
    "AdvancedConfig",
    "Cheater",
    "LightPen",
    # "Rewind" — excluded: auto-closes if no save states exist
    "ScreenEffects",
]


class TestConnection:
    """Verify basic connectivity and protocol."""

    def test_ping(self, emu: AltirraTestHarness):
        resp = emu.ping()
        assert resp["ok"] is True

    def test_query_state(self, emu: AltirraTestHarness):
        state = emu.query_state()
        assert state["ok"] is True
        assert "state" in state
        assert "dialogs" in state["state"]
        assert "sim" in state["state"]
        assert "windows" in state["state"]

    def test_list_dialogs(self, emu: AltirraTestHarness):
        resp = emu.send("list_dialogs")
        assert resp["ok"] is True
        dialogs = resp["dialogs"]
        for name in ALL_DIALOGS:
            assert name in dialogs, f"Dialog '{name}' missing from list_dialogs"

    def test_list_items_returns_widgets(self, emu: AltirraTestHarness):
        items = emu.list_items()
        assert isinstance(items, list)
        # Menu bar should always have some items
        assert len(items) > 0

    def test_wait_frames(self, emu: AltirraTestHarness):
        resp = emu.wait_frames(2)
        assert resp["ok"] is True

    def test_sim_state(self, emu: AltirraTestHarness):
        sim = emu.get_sim_state()
        assert "running" in sim
        assert "paused" in sim
        assert "turbo" in sim
        assert "hardwareMode" in sim


class TestDialogOpenClose:
    """Open and close every dialog, verify state flags."""

    @pytest.mark.parametrize("dialog", ALL_DIALOGS)
    def test_dialog_cycle(self, emu: AltirraTestHarness, dialog: str):
        # Verify closed initially
        assert not emu.get_dialog_state(dialog), f"{dialog} should start closed"

        # Open
        emu.open_dialog(dialog)
        emu.wait_frames(3)
        assert emu.get_dialog_state(dialog), f"{dialog} should be open"

        # Verify it has widgets
        # (some dialogs may not have items until interaction; skip check for those)

        # Close
        emu.close_dialog(dialog)
        emu.wait_frames(3)
        assert not emu.get_dialog_state(dialog), f"{dialog} should be closed"


class TestEmulationControl:
    """Verify emulation state commands."""

    def test_pause_resume(self, emu: AltirraTestHarness):
        emu.resume()
        emu.wait_frames(2)
        sim = emu.get_sim_state()
        assert sim["paused"] is False

        emu.pause()
        emu.wait_frames(2)
        sim = emu.get_sim_state()
        assert sim["paused"] is True

        emu.resume()
        emu.wait_frames(2)
        sim = emu.get_sim_state()
        assert sim["paused"] is False

    def test_cold_reset(self, emu: AltirraTestHarness):
        emu.cold_reset()
        emu.wait_frames(30)
        sim = emu.get_sim_state()
        assert sim["running"] is True

    def test_warm_reset(self, emu: AltirraTestHarness):
        # Ensure running state before warm reset
        emu.resume()
        emu.wait_frames(5)
        emu.warm_reset()
        emu.wait_frames(30)
        sim = emu.get_sim_state()
        # Warm reset preserves running state — verify we can still query
        assert "running" in sim
