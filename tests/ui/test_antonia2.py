"""CPU-option activation, per-profile isolation, and restart persistence."""
from .test_explorers import click


LABEL = "Antonia2 MULDIV Block"


def cpu_page(emu):
    emu.close_dialog("SetupWizard")
    emu.navigate_system_config("CPU")


def quit_and_restart(emu):
    emu.close_dialog("SystemConfig")
    emu.send("quit")
    assert emu._proc.wait(timeout=10) == 0
    emu.stop()
    emu.start()
    emu.wait_frames(5)
    cpu_page(emu)


def test_antonia2_cpu_checkbox_profile_restart(emu):
    emu.send("set_window_size 1280 900")
    cpu_page(emu)
    emu.assert_checkbox_state("Configure System", LABEL, False)
    click(emu, "Configure System", "65C816 (1.79MHz)")
    click(emu, "Configure System", LABEL)
    emu.assert_checkbox_state("Configure System", LABEL, True)

    original = emu.query_state()["state"]["profiles"]["current"]
    emu.close_dialog("SystemConfig")
    profile = emu.send("profile_test create")["id"]
    emu.send(f"profile_test activate {profile}")
    cpu_page(emu)
    emu.assert_checkbox_state("Configure System", LABEL, True)
    click(emu, "Configure System", LABEL)
    emu.assert_checkbox_state("Configure System", LABEL, False)
    emu.close_dialog("SystemConfig")

    emu.send(f"profile_test activate {original}")
    cpu_page(emu)
    emu.assert_checkbox_state("Configure System", LABEL, True)
    emu.close_dialog("SystemConfig")
    emu.send(f"profile_test activate {profile}")
    cpu_page(emu)
    emu.assert_checkbox_state("Configure System", LABEL, False)
    emu.close_dialog("SystemConfig")
    emu.send(f"profile_test activate {original}")
    cpu_page(emu)
    emu.assert_checkbox_state("Configure System", LABEL, True)

    quit_and_restart(emu)
    emu.assert_checkbox_state("Configure System", LABEL, True)
    click(emu, "Configure System", LABEL)
    emu.assert_checkbox_state("Configure System", LABEL, False)
    quit_and_restart(emu)
    emu.assert_checkbox_state("Configure System", LABEL, False)
