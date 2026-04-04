"""
Phase 3: Configure System dialog — widget audit and interaction tests.

Covers every category page in the System Configuration sidebar.
Each test navigates to a page, verifies all expected widgets exist,
and tests key interactions.
"""

import pytest
from .harness import AltirraTestHarness

WIN = "Configure System"


# ── Helpers ──────────────────────────────────────────────────────────────

def open_page(emu: AltirraTestHarness, page: str):
    """Navigate to a System Config category page."""
    emu.navigate_system_config(page)


def assert_labels_present(emu: AltirraTestHarness, expected: list[str]):
    """Assert all expected labels exist in the current window."""
    labels = emu.get_item_labels(WIN)
    for label in expected:
        assert label in labels, (
            f"Expected widget '{label}' not found.\n"
            f"Available: {[l for l in labels if not l.startswith('##')]}"
        )


def assert_checkbox_toggles(emu: AltirraTestHarness, label: str):
    """Click a checkbox and verify it toggled."""
    item_before = emu.find_item(WIN, label)
    assert item_before is not None, f"Checkbox '{label}' not found"
    assert item_before["type"] == "checkbox", f"'{label}' is {item_before['type']}, expected checkbox"
    was_checked = item_before.get("checked", False)

    emu.click(WIN, label)
    emu.wait_frames(3)

    item_after = emu.find_item(WIN, label)
    assert item_after is not None, f"Checkbox '{label}' disappeared after click"
    assert item_after.get("checked", False) != was_checked, (
        f"Checkbox '{label}' did not toggle (was {was_checked}, still {item_after.get('checked')})"
    )

    # Toggle back to original state
    emu.click(WIN, label)
    emu.wait_frames(2)


# ── Sidebar Navigation ──────────────────────────────────────────────────

class TestSidebarNavigation:
    """Verify all sidebar categories exist and are navigable."""

    SIDEBAR_ENTRIES = [
        "Overview", "Recommendations",
        # Computer subtree
        "System", "CPU", "Firmware", "Memory", "Acceleration", "Speed", "Boot",
        # Outputs subtree
        "Video", "Enhanced Text", "Audio",
        # Peripherals subtree
        "Devices", "Keyboard",
        # Media subtree
        "Defaults", "Disk", "Cassette", "Flash",
        # Emulator subtree
        "Compat DB", "Display", "Ease of Use", "Error Handling",
        "Input", "Window Caption", "Workarounds",
    ]

    # Pages immediately visible (trees auto-expanded or no tree)
    INITIALLY_VISIBLE = [
        "Overview", "Recommendations",
        "System", "CPU", "Firmware", "Memory", "Acceleration", "Speed", "Boot",
        "Video", "Enhanced Text", "Audio",
        "Devices", "Keyboard",
        "Defaults", "Disk", "Cassette", "Flash",
    ]

    # Pages near the bottom of the sidebar that may be off-screen in the
    # 480px-tall dialog. These are clipped by the scrollable child region
    # and not tracked by ImGui's test engine hooks until visible.
    EMULATOR_PAGES = [
        "Compat DB", "Display", "Ease of Use", "Error Handling",
        "Input", "Window Caption", "Workarounds",
    ]

    def test_initial_pages_visible(self, emu: AltirraTestHarness):
        emu.open_dialog("SystemConfig")
        emu.wait_frames(5)
        labels = emu.get_item_labels(WIN)
        for page in self.INITIALLY_VISIBLE:
            assert page in labels, (
                f"Page '{page}' not visible in sidebar.\n"
                f"Available: {[l for l in labels if not l.startswith('#')]}"
            )

    @pytest.mark.parametrize("page", INITIALLY_VISIBLE)
    def test_page_navigable(self, emu: AltirraTestHarness, page: str):
        open_page(emu, page)
        # If we got here without CommandError, navigation worked

    @pytest.mark.parametrize("page", EMULATOR_PAGES)
    @pytest.mark.xfail(reason="Sidebar items clipped in 480px dialog — need scroll support in test mode")
    def test_emulator_page_navigable(self, emu: AltirraTestHarness, page: str):
        open_page(emu, page)
        # If we got here without CommandError, navigation worked


# ── System (Hardware) Page ───────────────────────────────────────────────

class TestSystemPage:

    EXPECTED = ["Toggle NTSC/PAL", "CTIA mode"]

    def test_widgets_exist(self, emu: AltirraTestHarness):
        open_page(emu, "System")
        assert_labels_present(emu, self.EXPECTED)

    def test_ctia_toggle(self, emu: AltirraTestHarness):
        open_page(emu, "System")
        assert_checkbox_toggles(emu, "CTIA mode")


# ── CPU Page ─────────────────────────────────────────────────────────────

class TestCPUPage:

    EXPECTED_RADIOS = [
        "6502 / 6502C", "65C02",
        "65C816 (1.79MHz)", "65C816 (3.58MHz)",
        "65C816 (7.14MHz)", "65C816 (10.74MHz)",
        "65C816 (14.28MHz)", "65C816 (17.90MHz)",
    ]

    EXPECTED_CHECKBOXES = [
        "Enable illegal instructions",
        "Allow BRK/IRQ to block NMI",
        "Stop on BRK instruction",
        "Record instruction history",
        "Track code paths",
        "Shadow ROMs in fast RAM",
        "Shadow cartridges in fast RAM",
    ]

    def test_widgets_exist(self, emu: AltirraTestHarness):
        open_page(emu, "CPU")
        assert_labels_present(emu, self.EXPECTED_RADIOS + self.EXPECTED_CHECKBOXES)

    @pytest.mark.parametrize("checkbox", EXPECTED_CHECKBOXES)
    def test_checkbox_toggle(self, emu: AltirraTestHarness, checkbox: str):
        open_page(emu, "CPU")
        # Items near the bottom of the content area may be off-screen
        item = emu.find_item(WIN, checkbox)
        if item and item.get("y", 0) > 500:
            pytest.skip("Item clipped at bottom of content area")
        assert_checkbox_toggles(emu, checkbox)


# ── Memory Page ──────────────────────────────────────────────────────────

class TestMemoryPage:

    EXPECTED_CHECKBOXES = [
        "Enable MapRAM (XL/XE only)",
        "Enable Ultimate1MB",
        "Enable bank register aliasing",
        "Preserve extended memory on cold reset",
    ]

    def test_widgets_exist(self, emu: AltirraTestHarness):
        open_page(emu, "Memory")
        assert_labels_present(emu, self.EXPECTED_CHECKBOXES)


# ── Acceleration Page ────────────────────────────────────────────────────

class TestAccelerationPage:

    EXPECTED = [
        "Fast boot", "Fast floating-point math",
        "SIO Patch",
        "D: patch (Disk SIO)", "C: patch (Cassette SIO)", "PRT: patch (Other SIO)",
        "H: (Host device CIO)", "P: (Printer CIO)", "R: (RS-232 CIO)", "T: (1030 Serial CIO)",
        "D: burst I/O", "PRT: burst I/O", "CIO burst transfers",
        "SIO override detection",
    ]

    def test_widgets_exist(self, emu: AltirraTestHarness):
        open_page(emu, "Acceleration")
        assert_labels_present(emu, self.EXPECTED)

    def test_fast_boot_toggle(self, emu: AltirraTestHarness):
        open_page(emu, "Acceleration")
        assert_checkbox_toggles(emu, "Fast boot")


# ── Speed Page ───────────────────────────────────────────────────────────

class TestSpeedPage:

    EXPECTED = [
        "Run as fast as possible (warp)",
        "Slow Motion",
        "Pause when emulator window is inactive",
    ]

    def test_widgets_exist(self, emu: AltirraTestHarness):
        open_page(emu, "Speed")
        assert_labels_present(emu, self.EXPECTED)

    def test_warp_toggle(self, emu: AltirraTestHarness):
        open_page(emu, "Speed")
        assert_checkbox_toggles(emu, "Run as fast as possible (warp)")


# ── Boot Page ────────────────────────────────────────────────────────────

class TestBootPage:

    EXPECTED = [
        "Randomize Memory on EXE Load",
        "Randomize program load timing",
        "Unload cartridges when booting new image",
        "Unload disks when booting new image",
        "Unload tapes when booting new image",
        "Use Specific Seed", "Auto",
    ]

    def test_widgets_exist(self, emu: AltirraTestHarness):
        open_page(emu, "Boot")
        assert_labels_present(emu, self.EXPECTED)


# ── Video Page ───────────────────────────────────────────────────────────

class TestVideoPage:

    EXPECTED = [
        "Frame Blending", "Linear Frame Blending",
        "Mono Persistence", "Interlace", "Scanlines",
        "Extended PAL Height",
    ]

    def test_widgets_exist(self, emu: AltirraTestHarness):
        open_page(emu, "Video")
        assert_labels_present(emu, self.EXPECTED)


# ── Audio Page ───────────────────────────────────────────────────────────

class TestAudioPage:

    EXPECTED = [
        "Mute All", "Stereo", "Downmix stereo to mono",
        "Non-linear mixing", "Serial noise",
        "Simulate console speaker", "Drive Sounds",
        "Audio monitor", "Audio scope",
        "Host audio options...",
    ]

    def test_widgets_exist(self, emu: AltirraTestHarness):
        open_page(emu, "Audio")
        assert_labels_present(emu, self.EXPECTED)


# ── Keyboard Page ────────────────────────────────────────────────────────

class TestKeyboardPage:

    EXPECTED = [
        "Allow SHIFT key to be detected on cold reset",
        "Enable F1-F4 as 1200XL function keys",
        "Share modifier host keys between keyboard and input maps",
        "Share non-modifier host keys between keyboard and input maps",
    ]

    def test_widgets_exist(self, emu: AltirraTestHarness):
        open_page(emu, "Keyboard")
        assert_labels_present(emu, self.EXPECTED)


# ── Disk Page ────────────────────────────────────────────────────────────

class TestDiskPage:

    EXPECTED = [
        "Accurate sector timing",
        "Play drive sounds",
        "Show sector counter",
    ]

    def test_widgets_exist(self, emu: AltirraTestHarness):
        open_page(emu, "Disk")
        assert_labels_present(emu, self.EXPECTED)


# ── Cassette Page ────────────────────────────────────────────────────────

class TestCassettePage:

    EXPECTED = [
        "Auto-boot on startup",
        "Auto-boot BASIC on startup",
        "Auto-rewind on startup",
        "Load data as audio",
        "Randomize starting position",
        "Invert turbo data",
        "Avoid OS C: random VBI-related errors",
        "Enable FSK speed compensation",
        "Enable crosstalk reduction",
    ]

    def test_widgets_exist(self, emu: AltirraTestHarness):
        open_page(emu, "Cassette")
        assert_labels_present(emu, self.EXPECTED)


# ── Display (Emulator) Page ──────────────────────────────────────────────

# ── Pages under "Emulator" section (off-screen in 480px dialog) ─────────
# These pages can't be navigated via sidebar click because ImGui doesn't
# render (and thus doesn't track) clipped items. Marked xfail until we add
# a scroll_to or set_category command to the test protocol.

XFAIL_SCROLL = pytest.mark.xfail(
    reason="Sidebar item clipped in 480px dialog — needs scroll support in test mode"
)


@XFAIL_SCROLL
class TestDisplayEmulatorPage:

    EXPECTED = [
        "Auto-hide mouse pointer after short delay",
        "Constrain mouse pointer in full-screen mode",
        "Show indicators",
        "Pad bottom margin to reserve space for indicators",
    ]

    def test_widgets_exist(self, emu: AltirraTestHarness):
        open_page(emu, "Display")
        assert_labels_present(emu, self.EXPECTED)


@XFAIL_SCROLL
class TestInputEmulatorPage:

    EXPECTED = [
        "Enable paddle potentiometer noise",
        "Use immediate analog updates",
        "Use immediate light pen updates",
    ]

    def test_widgets_exist(self, emu: AltirraTestHarness):
        open_page(emu, "Input")
        assert_labels_present(emu, self.EXPECTED)


@XFAIL_SCROLL
class TestEaseOfUsePage:

    EXPECTED = [
        "Reset when changing cartridges",
        "Reset when changing video standard",
        "Reset when toggling internal BASIC",
    ]

    def test_widgets_exist(self, emu: AltirraTestHarness):
        open_page(emu, "Ease of Use")
        assert_labels_present(emu, self.EXPECTED)


@XFAIL_SCROLL
class TestWorkaroundsPage:

    EXPECTED = [
        "Poll directories for changes (H: device)",
    ]

    def test_widgets_exist(self, emu: AltirraTestHarness):
        open_page(emu, "Workarounds")
        assert_labels_present(emu, self.EXPECTED)


@XFAIL_SCROLL
class TestCompatDBPage:

    EXPECTED = [
        "Show compatibility warnings",
        "Use internal database",
        "Use external database",
        "Browse...",
        "Unmute all warnings",
    ]

    def test_widgets_exist(self, emu: AltirraTestHarness):
        open_page(emu, "Compat DB")
        assert_labels_present(emu, self.EXPECTED)


@XFAIL_SCROLL
class TestMediaDefaultsPage:

    def test_widgets_exist(self, emu: AltirraTestHarness):
        open_page(emu, "Defaults")
        assert_labels_present(emu, ["Write mode"])


# ── Window Caption Page ─────────────────────────────────────────────────

@XFAIL_SCROLL
class TestCaptionPage:

    def test_widgets_exist(self, emu: AltirraTestHarness):
        open_page(emu, "Window Caption")
        assert_labels_present(emu, ["Template"])


# ── Devices Page ─────────────────────────────────────────────────────────

class TestDevicesPage:

    def test_add_device_button_exists(self, emu: AltirraTestHarness):
        open_page(emu, "Devices")
        assert_labels_present(emu, ["Add Device..."])


# ── OK Button ────────────────────────────────────────────────────────────

class TestDialogOK:

    def test_ok_closes_dialog(self, emu: AltirraTestHarness):
        emu.open_dialog("SystemConfig")
        emu.wait_frames(3)
        assert emu.get_dialog_state("SystemConfig")

        emu.click(WIN, "OK")
        emu.wait_frames(3)
        assert not emu.get_dialog_state("SystemConfig")
