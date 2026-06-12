"""
Automation verbs: mem_read, input joy/console, set_speed/get_speed,
and the non-interactive quit path.

These verbs exist for scripted sessions (dynamic analysis, gameplay
harnesses, capture pipelines).  The joystick/console tests assert
against the emulated hardware registers via mem_read, so they verify
the full chain: socket verb -> input manager / GTIA -> PIA/GTIA
register state the running program would actually see.

Hardware registers used (all read side-effect-free via DebugReadByte):
  PORTA  $D300  joystick port 1 in low nibble, active low
                (bit0 up, bit1 down, bit2 left, bit3 right)
  TRIG0  $D010  joystick 1 trigger, 0 = pressed
  CONSOL $D01F  console switches, active low
                (bit0 Start, bit1 Select, bit2 Option)
"""

import signal
import subprocess
import time

import pytest
from .harness import AltirraTestHarness, CommandError

PORTA = 0xD300
TRIG0 = 0xD010
CONSOL = 0xD01F

PORTA_IDLE = 0xFF
PORTA_LEFT = 0xFF & ~0x04
PORTA_RIGHT = 0xFF & ~0x08
PORTA_UP = 0xFF & ~0x01
PORTA_DOWN = 0xFF & ~0x02

CONSOL_IDLE = 0x07


def wait_reg(emu: AltirraTestHarness, addr: int, expected: int,
             tries: int = 30) -> int:
    """Poll a register until it reads `expected` (or give up).

    Input edges propagate to PIA/GTIA state within a frame or two, but
    not synchronously with the socket reply (e.g. console switches can
    be deferred a frame by the netplay input glue) — so assertions poll
    instead of assuming a fixed frame delay.  Returns the last value
    read, so `assert wait_reg(...) == expected` reports it on failure.
    """
    val = emu.mem_read(addr, 1)[0]
    for _ in range(tries):
        if val == expected:
            break
        emu.wait_frames(2)
        val = emu.mem_read(addr, 1)[0]
    return val


@pytest.fixture(scope="session")
def booted(emu: AltirraTestHarness):
    """Wait until the OS has finished booting.

    Until OS init sets PACTL bit 2, $D300 reads back the PIA *direction*
    register (0 after reset), not the port — so joystick state is only
    observable once boot has settled and PORTA reads idle ($FF).
    """
    emu.input_joy(0, "release_all")
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        if emu.mem_read(PORTA, 1)[0] == PORTA_IDLE:
            return
        emu.wait_frames(10)
    pytest.fail("PORTA never reached idle state — OS boot did not settle")


@pytest.fixture(autouse=True)
def _release_inputs(emu: AltirraTestHarness):
    """Leave no stuck input or speed override behind for later tests."""
    yield
    try:
        emu.input_joy(0, "release_all")
        emu.set_speed("normal")
        emu.wait_frames(2)
    except Exception:
        pass  # best-effort cleanup


class TestMemRead:
    """mem_read returns ground-truth bytes and rejects bad input loudly."""

    def test_single_byte_default_count(self, emu: AltirraTestHarness):
        resp = emu.send("mem_read D300")
        assert resp["ok"] is True
        assert resp["addr"] == "$D300"
        assert len(resp["bytes"]) == 1

    def test_count_and_values_are_bytes(self, emu: AltirraTestHarness):
        data = emu.mem_read(0x0600, 16)
        assert len(data) == 16
        assert all(0 <= b <= 255 for b in data)

    def test_count_clamped_to_4096(self, emu: AltirraTestHarness):
        resp = emu.send("mem_read 0000 99999")
        assert len(resp["bytes"]) == 4096

    def test_address_wraps_16bit(self, emu: AltirraTestHarness):
        # Reading 4 bytes at $FFFF wraps to $0000 without error
        resp = emu.send("mem_read FFFF 4")
        assert len(resp["bytes"]) == 4

    def test_bad_address_is_an_error(self, emu: AltirraTestHarness):
        with pytest.raises(CommandError, match="bad hex address"):
            emu.send("mem_read zzz")

    def test_bad_count_is_an_error(self, emu: AltirraTestHarness):
        with pytest.raises(CommandError, match="bad count"):
            emu.send("mem_read 0600 bogus")

    def test_missing_address_is_usage_error(self, emu: AltirraTestHarness):
        with pytest.raises(CommandError, match="usage"):
            emu.send("mem_read")


@pytest.mark.usefixtures("booted")
class TestJoystickInput:
    """input joy drives PIA/GTIA state exactly as a physical stick could."""

    def test_idle_state(self, emu: AltirraTestHarness):
        emu.input_joy(0, "release_all")
        assert wait_reg(emu, PORTA, PORTA_IDLE) == PORTA_IDLE
        assert wait_reg(emu, TRIG0, 1) == 1

    @pytest.mark.parametrize("action,expected", [
        ("left", PORTA_LEFT),
        ("right", PORTA_RIGHT),
        ("up", PORTA_UP),
        ("down", PORTA_DOWN),
    ])
    def test_direction_press(self, emu: AltirraTestHarness, action, expected):
        emu.input_joy(0, "release_all")
        emu.input_joy(0, action)
        assert wait_reg(emu, PORTA, expected) == expected, \
            f"{action} should pull its PORTA bit low"

    def test_opposite_directions_are_exclusive(self, emu: AltirraTestHarness):
        # A physical stick cannot hold left+right: pressing right must
        # release left (and vice versa), not combine into an impossible
        # state some games glitch on.
        emu.input_joy(0, "release_all")
        emu.input_joy(0, "left")
        assert wait_reg(emu, PORTA, PORTA_LEFT) == PORTA_LEFT
        emu.input_joy(0, "right")
        assert wait_reg(emu, PORTA, PORTA_RIGHT) == PORTA_RIGHT, \
            "pressing right must release left, not hold both"

    def test_diagonal_is_allowed(self, emu: AltirraTestHarness):
        # Distinct axes combine fine — diagonals are real stick states.
        emu.input_joy(0, "release_all")
        emu.input_joy(0, "up")
        emu.input_joy(0, "left")
        expected = PORTA_UP & PORTA_LEFT
        assert wait_reg(emu, PORTA, expected) == expected

    def test_fire_and_release_all(self, emu: AltirraTestHarness):
        emu.input_joy(0, "fire")
        emu.input_joy(0, "down")
        assert wait_reg(emu, TRIG0, 0) == 0, "TRIG0 reads 0 while fire is held"
        emu.input_joy(0, "release_all")
        assert wait_reg(emu, TRIG0, 1) == 1
        assert wait_reg(emu, PORTA, PORTA_IDLE) == PORTA_IDLE

    def test_bad_unit_is_an_error(self, emu: AltirraTestHarness):
        with pytest.raises(CommandError, match="unit must be 0..3"):
            emu.input_joy(7, "left")

    def test_bad_action_is_an_error(self, emu: AltirraTestHarness):
        with pytest.raises(CommandError, match="unknown joy action"):
            emu.input_joy(0, "wiggle")


@pytest.mark.usefixtures("booted")
class TestConsoleSwitches:
    """input console drives GTIA CONSOL state (active low)."""

    @pytest.mark.parametrize("button,bit", [
        ("start", 0x01),
        ("select", 0x02),
        ("option", 0x04),
    ])
    def test_press_release(self, emu: AltirraTestHarness, button, bit):
        emu.input_console(button)
        assert wait_reg(emu, CONSOL, CONSOL_IDLE & ~bit) == (CONSOL_IDLE & ~bit), \
            f"{button} should pull CONSOL bit {bit:#04x} low"
        emu.input_console(button, up=True)
        assert wait_reg(emu, CONSOL, CONSOL_IDLE) == CONSOL_IDLE

    def test_bad_button_is_an_error(self, emu: AltirraTestHarness):
        with pytest.raises(CommandError, match="unknown console button"):
            emu.input_console("reset")


class TestSpeedControl:
    """set_speed/get_speed toggle sticky turbo, consistent with query_state."""

    def test_default_is_realtime(self, emu: AltirraTestHarness):
        emu.set_speed("normal")
        assert emu.get_speed() is False

    @pytest.mark.parametrize("on,off", [("turbo", "normal"), ("warp", "real")])
    def test_aliases_round_trip(self, emu: AltirraTestHarness, on, off):
        emu.set_speed(on)
        assert emu.get_speed() is True
        emu.set_speed(off)
        assert emu.get_speed() is False

    def test_matches_query_state(self, emu: AltirraTestHarness):
        emu.set_speed("turbo")
        assert emu.get_sim_state()["turbo"] is True
        emu.set_speed("normal")
        assert emu.get_sim_state()["turbo"] is False

    def test_bad_mode_is_an_error(self, emu: AltirraTestHarness):
        with pytest.raises(CommandError, match="unknown speed mode"):
            emu.set_speed("ludicrous")


class TestNonInteractiveQuit:
    """Test-mode close events must not hang on the Confirm Exit modal.

    Uses a private emulator instance — these tests end the process.
    """

    def test_quit_verb_exits(self):
        with AltirraTestHarness() as solo:
            solo.ping()
            solo.send_raw("quit")
            assert solo._proc.wait(timeout=10) == 0

    def test_sigterm_exits_cleanly(self):
        # Pre-fix, SIGTERM raised the Confirm Exit modal and the process
        # hung until killed; in test mode there is no human to dismiss it.
        with AltirraTestHarness() as solo:
            solo.ping()
            solo._proc.send_signal(signal.SIGTERM)
            try:
                code = solo._proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                pytest.fail("process still running 10s after SIGTERM "
                            "(Confirm Exit modal bypass not working)")
            assert code == 0
