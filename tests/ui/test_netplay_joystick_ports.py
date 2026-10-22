"""Capture port one and discard the other real mapped local controllers."""

from .test_netplay_input import emu, bridge, poll_apply


def click_item(emu, label, window=""):
    item = emu.find_item(window, label)
    assert item is not None, label + "\n" + "\n".join(i["window"] + ": " + i["label"] for i in emu.list_items())
    emu.click(item["window"], label)
    emu.wait_frames(4)


def make_joystick_map(emu, port):
    before = {m["name"] for m in emu.send("input_maps")["maps"]}
    click_item(emu, "Presets...", "Input Mappings")
    click_item(emu, "Create From Template...")
    emu.send("key tab")
    emu.wait_frames(3)
    emu.send("key tab")
    emu.wait_frames(3)
    emu.send("key enter")
    emu.wait_frames(3)
    click_item(emu, f"Port {port}")
    click_item(emu, "OK", "Create Input Map")
    maps = emu.send("input_maps")["maps"]
    assert len(maps) == len(before) + 1
    created, = [m for m in maps if m["name"] not in before]
    assert created["enabled"] and created["bindings"]
    assert all(binding["port"] == port - 1 for binding in created["bindings"])
    return created["name"]


def physical(emu, keys, down):
    for key in keys:
        emu.send(f"physical_key {'down' if down else 'up'} {key}")
    emu.wait_frames(3)


def assert_neutral(emu):
    assert emu.mem_read(0xD300, 2) == [0xFF, 0xFF]
    assert emu.mem_read(0xD010, 4) == [1, 1, 1, 1]


def test_all_four_mapped_ports_remain_isolated_and_restore_after_netplay(emu):
    # The original 800 exposes all four joystick ports through PIA/GTIA,
    # allowing cleanup to prove that ports three/four really were mapped.
    with bridge(emu) as control:
        control.config("machine", "800")
        control.frame(180)
        control.ping()
    emu.send("set_window_size 1280 1000")
    maps = emu.send("input_maps")["maps"]
    primary_index, primary = next((i, m) for i, m in enumerate(maps)
                                  if m["enabled"] and m["name"]
                                  == "Arrow Keys -> Joystick (port 1)")
    # Derive actual trigger codes from the enabled native default map.
    targets = {binding["input"]: binding["target"] for binding in primary["bindings"]}
    right, down, left, up, fire = (targets[code]
                                  for code in (0x27, 0x28, 0x25, 0x26, 0xA2))
    emu.open_dialog("InputMappings")
    emu.wait_frames(4)
    other_names = [make_joystick_map(emu, port) for port in (2, 3, 4)]
    map_indices = {m["name"]: i for i, m in enumerate(emu.send("input_maps")["maps"])}
    other_indices = [map_indices[name] for name in other_names]
    primary_index = map_indices[primary["name"]]
    emu.close_dialog("InputMappings")
    emu.wait_frames(4)
    # Distinct physical keys avoid sending the same input to every port.
    # ATInputCode letter keys use their corresponding uppercase key code.
    emu.send(f"input_map_add_binding {primary_index} 0 {ord('C')} {right}")
    emu.send(f"input_map_add_binding {primary_index} 0 {ord('F')} {fire}")
    other_keys = (("X", "V", down), ("Y", "B", left), ("Z", "N", up))
    for index, (direction, button, target) in zip(other_indices, other_keys):
        emu.send(f"input_map_add_binding {index} 0 {ord(direction)} {target}")
        emu.send(f"input_map_add_binding {index} 0 {ord(button)} {fire}")
    assert_neutral(emu)
    emu.send("netplay_input begin")
    try:
        for rebuild in (False, True):
            if rebuild:
                # This destroys/recreates the active controllers. The
                # port-one capture and all three discard targets must stay.
                emu.send(f"input_map_add_binding {other_indices[0]} 0 {ord('P')} {fire}")
                assert_neutral(emu)
            discarded = [key for direction, button, _ in other_keys
                         for key in (direction, button)]
            if rebuild:
                discarded.append("P")
            physical(emu, discarded, True)
            captured = emu.send("netplay_input poll")
            assert captured["stick"] == 0 and captured["buttons"] & 1 == 0
            assert_neutral(emu)
            physical(emu, ("C", "F"), True)
            captured = emu.send("netplay_input poll")
            assert captured["stick"] == 8 and captured["buttons"] & 1 == 1
            assert_neutral(emu)
            # The network role, not the local mapping's port, selects P2.
            emu.send("netplay_input apply_polled 1")
            assert emu.mem_read(0xD300, 2) == [0x7F, 0xFF]
            assert emu.mem_read(0xD010, 4) == [1, 0, 1, 1]
            physical(emu, discarded + ["C", "F"], False)
            released, _ = poll_apply(emu, 1)
            assert released["stick"] == 0 and released["buttons"] & 1 == 0
            assert_neutral(emu)
    finally:
        emu.send("netplay_input end")
    # Ordinary local input returns to each of the four real hardware ports.
    local_keys = ["C", "F", "X", "V", "Y", "B", "Z", "N"]
    physical(emu, local_keys, True)
    assert emu.mem_read(0xD300, 2) == [0xD7, 0xEB]
    assert emu.mem_read(0xD010, 4) == [0, 0, 0, 0]
    physical(emu, local_keys, False)
    assert_neutral(emu)
