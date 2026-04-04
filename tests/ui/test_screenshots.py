"""
Phase 6: Screenshot regression — capture every dialog for visual comparison.

Run with:
    pytest tests/ui/test_screenshots.py --screenshot-dir=/tmp/altirra-screenshots

Screenshots are saved as BMP files. Compare against reference images to detect
layout regressions or missing widgets.
"""

import os
import pytest
from .harness import AltirraTestHarness


def pytest_addoption_screenshots(parser):
    parser.addoption(
        "--screenshot-dir",
        default="/tmp/altirra-screenshots",
        help="Directory to save dialog screenshots",
    )


ALL_DIALOGS = [
    ("SystemConfig", "system_config"),
    ("DiskManager", "disk_manager"),
    ("CassetteControl", "cassette_control"),
    ("About", "about"),
    ("AdjustColors", "adjust_colors"),
    ("DisplaySettings", "display_settings"),
    ("AudioOptions", "audio_options"),
    ("Profiles", "profiles"),
    ("CartridgeMapper", "cartridge_mapper"),
    ("CommandLineHelp", "cmdline_help"),
    ("ChangeLog", "changelog"),
    ("SetupWizard", "setup_wizard"),
    ("AdvancedConfig", "advanced_config"),
    ("DiskExplorer", "disk_explorer"),
    ("KeyboardShortcuts", "keyboard_shortcuts"),
    ("CompatDB", "compat_db"),
]


class TestScreenshots:

    @pytest.fixture(autouse=True)
    def _setup_dir(self, request):
        self.screenshot_dir = request.config.getoption(
            "--screenshot-dir", default="/tmp/altirra-screenshots"
        )
        os.makedirs(self.screenshot_dir, exist_ok=True)

    @pytest.mark.parametrize("dialog_name,filename", ALL_DIALOGS)
    def test_capture_dialog(self, emu: AltirraTestHarness,
                            dialog_name: str, filename: str):
        emu.open_dialog(dialog_name)
        emu.wait_frames(5)

        path = os.path.join(self.screenshot_dir, f"{filename}.bmp")
        emu.screenshot(path)
        emu.wait_frames(3)

        # Screenshot command is fire-and-forget; the file is written by the
        # renderer on the next frame. We just verify the command succeeded.

    def test_capture_main_window(self, emu: AltirraTestHarness):
        """Screenshot the main emulator window with no dialogs open."""
        emu.wait_frames(5)
        path = os.path.join(self.screenshot_dir, "main_window.bmp")
        emu.screenshot(path)
        emu.wait_frames(3)
