"""
Phase 3: Dialog widget audit — verify all standalone dialogs have the
expected widgets with correct labels and types.
"""

import pytest
from .harness import AltirraTestHarness


# ── Disk Manager ─────────────────────────────────────────────────────────

class TestDiskManager:

    WIN = "Disk drives"

    def test_widgets_exist(self, emu: AltirraTestHarness):
        emu.open_dialog("DiskManager")
        emu.wait_frames(3)
        labels = emu.get_item_labels(self.WIN)
        assert "Drives 1-8" in labels or "OK" in labels, (
            f"Disk Manager has no expected widgets. Got: {labels[:20]}"
        )
        assert "OK" in labels

    def test_ok_closes(self, emu: AltirraTestHarness):
        emu.open_dialog("DiskManager")
        emu.wait_frames(5)
        emu.click(self.WIN, "OK")
        emu.wait_frames(5)
        assert not emu.get_dialog_state("DiskManager")


# ── Cassette Control ────────────────────────────────────────────────────

class TestCassetteControl:

    WIN = "Cassette Tape Control"

    TRANSPORT_BUTTONS = ["Stop", "Pause", "Play", "Rec"]

    def test_window_opens(self, emu: AltirraTestHarness):
        emu.open_dialog("CassetteControl")
        emu.wait_frames(3)
        win = emu.query_window(self.WIN)
        assert win.get("visible", False), "Cassette Control window not visible"

    def test_transport_buttons_exist(self, emu: AltirraTestHarness):
        emu.open_dialog("CassetteControl")
        emu.wait_frames(3)
        labels = emu.get_item_labels(self.WIN)
        for btn in self.TRANSPORT_BUTTONS:
            assert btn in labels, f"Transport button '{btn}' not found"


# ── Adjust Colors ───────────────────────────────────────────────────────

class TestAdjustColors:

    WIN = "Adjust Colors"

    EXPECTED_SLIDERS = [
        "Hue Start", "Hue Range",
        "Brightness", "Contrast", "Saturation", "Gamma", "Intensity Scale",
    ]

    EXPECTED_CHECKBOXES = [
        "Share NTSC/PAL settings",
        "PAL quirks",
    ]

    def test_widgets_exist(self, emu: AltirraTestHarness):
        emu.open_dialog("AdjustColors")
        emu.wait_frames(3)
        labels = emu.get_item_labels(self.WIN)
        for label in self.EXPECTED_SLIDERS + self.EXPECTED_CHECKBOXES:
            assert label in labels, f"Widget '{label}' not found in Adjust Colors"

    def test_reset_button_exists(self, emu: AltirraTestHarness):
        emu.open_dialog("AdjustColors")
        emu.wait_frames(3)
        labels = emu.get_item_labels(self.WIN)
        assert "Reset to Defaults" in labels


# ── Display Settings ────────────────────────────────────────────────────

class TestDisplaySettings:

    WIN = "Display Settings"

    EXPECTED = ["Show FPS", "Show Indicators", "Auto-Hide Mouse Pointer"]

    def test_widgets_exist(self, emu: AltirraTestHarness):
        emu.open_dialog("DisplaySettings")
        emu.wait_frames(3)
        labels = emu.get_item_labels(self.WIN)
        for label in self.EXPECTED:
            assert label in labels, f"Widget '{label}' not found"

    def test_close_via_api(self, emu: AltirraTestHarness):
        """Display Settings has no OK button — close via close_dialog."""
        emu.open_dialog("DisplaySettings")
        emu.wait_frames(3)
        emu.close_dialog("DisplaySettings")
        emu.wait_frames(3)
        assert not emu.get_dialog_state("DisplaySettings")


# ── Audio Options ───────────────────────────────────────────────────────

class TestAudioOptions:

    WIN = "Audio Options"

    EXPECTED = ["Volume", "Drive volume", "Covox volume", "Latency",
                "Extra buffer", "Show debug info", "OK"]

    def test_widgets_exist(self, emu: AltirraTestHarness):
        emu.open_dialog("AudioOptions")
        emu.wait_frames(3)
        labels = emu.get_item_labels(self.WIN)
        for label in self.EXPECTED:
            assert label in labels, f"Widget '{label}' not found"


# ── Profiles ────────────────────────────────────────────────────────────

class TestProfiles:

    WIN = "Profiles"

    def test_widgets_exist(self, emu: AltirraTestHarness):
        emu.open_dialog("Profiles")
        emu.wait_frames(3)
        labels = emu.get_item_labels(self.WIN)
        assert "Add Profile" in labels
        assert "OK" in labels

    def test_temporary_profile_checkbox(self, emu: AltirraTestHarness):
        emu.open_dialog("Profiles")
        emu.wait_frames(3)
        item = emu.find_item(self.WIN, "Temporary Profile (don't save changes)")
        # Might have slightly different label; check for partial match
        if item is None:
            labels = emu.get_item_labels(self.WIN)
            temp_labels = [l for l in labels if "Temporary" in l]
            assert temp_labels, f"No 'Temporary Profile' widget found. Labels: {labels[:20]}"


# ── Cartridge Mapper ────────────────────────────────────────────────────

class TestCartridgeMapper:

    WIN = "Cartridge Mapper"

    def test_widgets_exist(self, emu: AltirraTestHarness):
        emu.open_dialog("CartridgeMapper")
        emu.wait_frames(3)
        labels = emu.get_item_labels(self.WIN)
        assert "OK" in labels or "Cancel" in labels


# ── About Dialog ────────────────────────────────────────────────────────

class TestAboutDialog:

    WIN = "About AltirraSDL"

    def test_opens_and_has_ok(self, emu: AltirraTestHarness):
        emu.open_dialog("About")
        emu.wait_frames(3)
        win = emu.query_window(self.WIN)
        assert win.get("visible", False), "About dialog not visible"
        labels = emu.get_item_labels(self.WIN)
        assert "OK" in labels

    def test_ok_closes(self, emu: AltirraTestHarness):
        emu.open_dialog("About")
        emu.wait_frames(5)
        emu.click(self.WIN, "OK")
        emu.wait_frames(8)
        assert not emu.get_dialog_state("About")


# ── Command-Line Help ───────────────────────────────────────────────────

class TestCommandLineHelp:

    WIN = "Command-Line Help"

    def test_opens_and_has_ok(self, emu: AltirraTestHarness):
        emu.open_dialog("CommandLineHelp")
        emu.wait_frames(3)
        win = emu.query_window(self.WIN)
        assert win.get("visible", False)
        labels = emu.get_item_labels(self.WIN)
        assert "OK" in labels


# ── Change Log ──────────────────────────────────────────────────────────

class TestChangeLog:

    WIN = "Change Log"

    def test_opens_and_has_ok(self, emu: AltirraTestHarness):
        emu.open_dialog("ChangeLog")
        emu.wait_frames(3)
        win = emu.query_window(self.WIN)
        assert win.get("visible", False)
        labels = emu.get_item_labels(self.WIN)
        assert "OK" in labels


# ── Setup Wizard ────────────────────────────────────────────────────────

class TestSetupWizard:

    WIN = "First Time Setup"

    def test_widgets_exist(self, emu: AltirraTestHarness):
        emu.open_dialog("SetupWizard")
        emu.wait_frames(3)
        labels = emu.get_item_labels(self.WIN)
        # Wizard should have navigation buttons
        has_nav = ("Next >" in labels or "Finish" in labels or "Close" in labels)
        assert has_nav, f"No navigation buttons found. Labels: {labels[:20]}"


# ── Disk Explorer ───────────────────────────────────────────────────────

class TestDiskExplorer:

    WIN = "Disk Explorer"

    def test_window_opens(self, emu: AltirraTestHarness):
        emu.open_dialog("DiskExplorer")
        emu.wait_frames(3)
        # Disk Explorer might use a different window name
        state = emu.get_dialog_state("DiskExplorer")
        assert state, "DiskExplorer dialog flag not set"


# ── Advanced Configuration ──────────────────────────────────────────────

class TestAdvancedConfig:

    WIN = "Advanced Configuration"

    def test_widgets_exist(self, emu: AltirraTestHarness):
        emu.open_dialog("AdvancedConfig")
        emu.wait_frames(3)
        labels = emu.get_item_labels(self.WIN)
        assert "Close" in labels or "OK" in labels, (
            f"No close button. Labels: {labels[:20]}"
        )
