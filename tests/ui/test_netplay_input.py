"""Netplay input must reach real Atari registers only when its frame applies."""
import struct
import tempfile
import threading
from pathlib import Path

import pytest

from .harness import AltirraTestHarness
from .test_video_outputs import AltirraBridge


@pytest.fixture
def emu(request, monkeypatch, tmp_path):
    monkeypatch.setenv("SDL_VIDEODRIVER", "dummy")
    monkeypatch.setenv("SDL_AUDIODRIVER", "dummy")
    monkeypatch.setenv("ALTIRRA_DISPLAY_BACKEND", "sdlrenderer")
    (tmp_path / "config").mkdir()
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "config"))
    with AltirraTestHarness(executable=request.config.getoption("--emu-path"),
                           extra_args=["--bridge", "--ui-mode", "desktop"]) as app:
        output = (tmp_path / "emulator-stderr.log").open("wb")
        pipe = app._proc.stderr
        def drain():
            for line in pipe:
                output.write(line)
                output.flush()
        threading.Thread(target=drain, daemon=True).start()
        app.close_dialog("SetupWizard")
        app.send("pause")
        yield app
        app.send("netplay_input end")
        app.send("netplay_profile end")


def poll_apply(emu, slot=0):
    captured = emu.send("netplay_input poll")
    return captured, emu.send(f"netplay_input apply_polled {slot}")


def bridge(emu):
    token = Path(tempfile.gettempdir()) / f"altirra-bridge-{emu._proc.pid}.token"
    return AltirraBridge.from_token_file(str(token))


def test_scan_zero_repeated_events_and_encoded_modifiers(emu):
    emu.send("netplay_input begin")
    emu.send("debugger_console e D20F 03")  # POKEY scan/debounce enabled
    emu.send("netplay_input press 0 0 0")  # L is scan zero
    emu.send("netplay_input press 0 0 0")
    for _ in range(2):
        captured, _ = poll_apply(emu)
        assert captured["keyScan"] == 0 and captured["keyFlags"] == 1
        assert emu.mem_read(0xD209, 1) == [0]
    assert emu.send("netplay_input poll")["keyFlags"] == 0
    emu.send("netplay_input press 0x61 0 0")  # already encoded Shift+Space
    poll_apply(emu)
    assert emu.mem_read(0xD209, 1) == [0x61]


def test_raw_key_chord_quick_taps_and_focus_release(emu):
    emu.send("netplay_input begin")
    emu.send("netplay_input press 0 0 1")
    emu.send("netplay_input press 1 0 1")
    assert poll_apply(emu)[1]["rawMask"] == 1
    assert poll_apply(emu)[1]["rawMask"] == 3
    emu.send("netplay_input modifiers 1 1")
    _, applied = poll_apply(emu)
    assert applied["shift"] and applied["ctrl"]
    # Focus/UI capture clears local state but must not touch emulation
    # until the corresponding delayed frame is applied on both peers.
    released = emu.send("netplay_input release")
    assert released["rawMask"] == 3 and released["shift"]
    _, applied = poll_apply(emu)
    assert applied["rawMask"] == 0
    assert not applied["shift"] and not applied["ctrl"]
    for _ in range(2):
        emu.send("netplay_input press 0 0 1")
        emu.send("netplay_input up 0")
    assert [poll_apply(emu)[1]["rawMask"] for _ in range(4)] == [1, 0, 1, 0]


def test_console_capture_and_peer_port_routing(emu):
    # The fixture pauses during cold boot, when GTIA drives CONSOL low
    # and the simulator can still force OPTION to disable BASIC. Advance
    # past the 150-frame startup hold before checking gameplay input.
    with bridge(emu) as control:
        control.frame(180)
        control.ping()
    emu.send("debugger_console e D01F 08")
    emu.send("netplay_input begin")
    before = emu.mem_read(0xD01F, 1)[0] & 7
    emu.send("input console start")
    assert emu.mem_read(0xD01F, 1)[0] & 7 == before
    captured, applied = poll_apply(emu, 1)
    assert captured["buttons"] & 2
    assert applied["console"] == 1
    assert emu.mem_read(0xD01F, 1)[0] & 7 == 6
    emu.send("netplay_input release")
    assert emu.mem_read(0xD01F, 1)[0] & 7 == 6
    poll_apply(emu, 1)
    assert emu.mem_read(0xD01F, 1)[0] & 7 == 7
    # Remote trigger belongs to port two on both peers.
    emu.send("netplay_input apply 1 0 1 0 0 0 0")
    assert emu.mem_read(0xD010, 2) == [1, 0]


def test_checkpoint_restores_input_and_preserves_queued_live_events(emu):
    emu.send("netplay_input begin")
    emu.send("netplay_input apply 0 0 2 0 0 1 3 655360 13107200")
    saved = emu.send("netplay_checkpoint save input")
    emu.send("netplay_input apply 0 0 0 0 0 0 0")
    emu.send("netplay_input press 0 0 0")
    restored = emu.send("netplay_checkpoint restore input")
    assert restored == saved
    status = emu.send("netplay_input status")
    assert status["pot0"] == 655360 and status["pot1"] == 13107200
    assert status["console"] == 1 and status["rawMask"] == 1
    assert emu.send("netplay_input poll")["keyFlags"] == 1


def test_profile_restores_unsaved_media_and_exact_paused_game(emu, tmp_path):
    # An infinite loop gives a repeatable program with no external I/O.
    image = tmp_path / "loop.xex"
    image.write_bytes(b"\xff\xff" + struct.pack("<HH", 0x2000, 0x2002)
                     + b"\x4c\x00\x20" + struct.pack("<HHH", 0x2E0, 0x2E1, 0x2000))
    emu.send(f"boot_image {image}")
    emu.wait_frames(60)
    with bridge(emu) as control:
        control.pause()
        control.frame(1)
        control.ping()
        emu.send("netplay_profile disk_create")
        emu.send("netplay_profile disk_write 113")
        emu.send("netplay_profile cart_create")
        emu.send("netplay_profile cart_write 91")
        before = emu.send("netplay_profile status")
        baseline = emu.send("netplay_checkpoint save before-profile")
        expected = []
        for _ in range(8):
            control.frame(1)
            control.ping()
            expected.append(emu.send("netplay_checkpoint hash"))
        emu.send("netplay_checkpoint restore before-profile")
        emu.send("netplay_profile begin")
        emu.send("netplay_profile disk_create")
        emu.send("netplay_profile disk_write 229")
        emu.send("netplay_profile cart_create")
        emu.send("netplay_profile cart_write 182")
        after = emu.send("netplay_profile end")
        for field in ("diskObject", "diskByte", "diskDirty", "diskPath",
                      "writeMode", "cartObject", "cartByte", "cartDirty"):
            assert after[field] == before[field], (field, before, after)
        assert after["restored"] and after["paused"]
        assert emu.send("netplay_checkpoint hash") == baseline
        actual = []
        for _ in range(8):
            control.frame(1)
            control.ping()
            actual.append(emu.send("netplay_checkpoint hash"))
        assert actual == expected


def test_profile_preserves_running_state_and_unsaved_tape(emu):
    emu.send("netplay_profile tape_create")
    before = emu.send("netplay_profile status")
    emu.send("resume")
    emu.send("netplay_profile begin")
    emu.send("netplay_profile tape_create")
    after = emu.send("netplay_profile end")
    assert after["restored"] and not after["paused"]
    assert after["tapeObject"] == before["tapeObject"]
    assert after["tapeDirty"] is True


def future_inputs(emu, control):
    """Exercise scan, modifiers, console and electrical paddle scan phases."""
    results = []
    for index, (held, modifiers) in enumerate(((1, 3), (3, 1), (2, 0), (0, 0), (1, 2), (0, 0))):
        emu.send(f"netplay_input apply 0 0 {2 if index == 1 else 0} 0 0 {held} {modifiers} 655360 13107200")
        control.poke(0xD20B, 0)  # POTGO, as decoded by ATPokeyEmulator::WriteByte
        control.frame(1)
        control.ping()
        results.append((emu.send("netplay_checkpoint hash"), emu.mem_read(0xD200, 16)))
    return results


@pytest.mark.parametrize("serialized", [False, True], ids=["native", "serialized"])
def test_replay_held_keys_paddles_and_dirty_disk(emu, serialized):
    emu.send("netplay_profile disk_create")
    emu.send("netplay_profile disk_write 113")
    emu.send("netplay_transfer wrap_disks")
    emu.send("netplay_input begin")
    emu.send("netplay_input apply 0 0 2 0 0 1 3 655360 13107200")
    with bridge(emu) as control:
        control.pause()
        control.poke(0xD20F, 3)
        control.frame(2)
        control.ping()
        command = "netplay_transfer" if serialized else "netplay_checkpoint"
        emu.send(f"{command} save replay")
        expected = future_inputs(emu, control)
        emu.send("netplay_profile disk_write 229")
        emu.send(f"{command} restore replay")
        assert emu.send("netplay_profile status")["diskByte"] == 113
        state = emu.send("netplay_input status")
        assert state["rawMask"] == 1 and state["shift"] and state["ctrl"]
        assert state["console"] == 1
        assert future_inputs(emu, control) == expected


def test_5200_keypad_and_selected_controller_restore(emu):
    with bridge(emu) as control:
        control.config("machine", "5200")
        control.cold_reset()
        control.pause()
        emu.send("netplay_input begin")
        # The keypad-0 matrix follows AT5200ControllerController::SetKeyState:
        # key index 2 sets doubled slots 4/5 and their +0x20 counterparts.
        keypad0 = (1 << 4) | (1 << 5) | (1 << 36) | (1 << 37)
        emu.send(f"netplay_input apply 1 0 0 0 0 {keypad0} 4 1966080 9830400")
        emu.send("netplay_input select5200 1 1")
        status = emu.send("netplay_input status")
        assert status["selected5200"] == 1 and status["pots5200Enabled"]
        assert status["rawMask"] == keypad0
        assert status["shift"] and status["ctrl"]  # 5200 top fire button
        saved = emu.send("netplay_checkpoint save keypad")
        emu.send("netplay_input select5200 0 0")
        emu.send("netplay_input apply 1 0 0 0 0 0 0")
        assert emu.send("netplay_checkpoint restore keypad") == saved
        assert emu.send("netplay_input status")["selected5200"] == 1
        emu.send("netplay_input end")
        # Preserve a game-selected controller across a canonical XL session.
        emu.send("netplay_input select5200 2 1")
        emu.send("netplay_profile begin xl")
        assert control.config("machine")["machine"] == "800XL"
        assert emu.send("netplay_profile end")["restored"]
        assert control.config("machine")["machine"] == "5200"
        assert emu.send("netplay_input status")["selected5200"] == 2


def make_boot_disk(path):
    # Boot layout is taken from Kernel/source/BootSector/bootsecdos2.s:
    # flags,count,load address,init address,then boot code at load+6.
    # A three-sector boot requires real POKEY/SIO transfers with patches off.
    disk = bytearray(720 * 128)
    disk[:9] = struct.pack("<BBHH", 0, 3, 0x0700, 0x0706) + b"\x4c\x06\x07"
    header = bytearray(16)
    header[:6] = struct.pack("<HHH", 0x0296, len(disk) // 16, 128)
    path.write_bytes(header + disk)


@pytest.mark.parametrize("serialized", [False, True], ids=["native", "serialized"])
@pytest.mark.parametrize("video", ["ntsc", "pal"])
def test_atr_boot_replays_real_sio(emu, tmp_path, serialized, video):
    image = tmp_path / "sio-loop.atr"
    make_boot_disk(image)
    with bridge(emu) as control:
        control.config("video", video)
        control.config("siopatch", "false")
        control.config("burstio", "false")
        emu.send(f"boot_image {image}")
        control.pause()
        control.frame(1)
        control.ping()  # Start from a completed frame, like rollback captures.
        emu.send("netplay_transfer wrap_disks")
        command = "netplay_transfer" if serialized else "netplay_checkpoint"
        # Checkpoints cover the initial boot and all three sector transfers,
        # rather than proving only a quiet infinite loop after loading.
        serial_writes = 0
        for chunk in range(18):
            emu.send(f"{command} save sio")
            control.pokey_trace_start()
            expected = []
            for _ in range(8):
                control.frame(1)
                control.ping()
                expected.append(emu.send("netplay_checkpoint hash"))
            trace = control.pokey_trace_read()
            serial_writes += sum(event["reg"] == 13 for event in trace["events"])
            emu.send(f"{command} restore sio")
            actual = []
            for _ in range(8):
                control.frame(1)
                control.ping()
                actual.append(emu.send("netplay_checkpoint hash"))
            assert actual == expected, (chunk, serialized, actual, expected)
        assert serial_writes > 0, "Boot must exercise real SEROUT writes"
        assert control.peek(0x0700, 9) == image.read_bytes()[16:25]
        # REGS exposes the in-flight fetch PC. A JMP $0706 can be
        # observed before, during, or just after fetching its three bytes.
        assert 0x0706 <= int(control.regs()["PC"].lstrip("$"), 16) <= 0x0709


@pytest.mark.parametrize("memory", ["64K", "320K"])
def test_cold_boot_checkpoint_restores_basic_memory_mapping(emu, memory):
    # Early OS RAM testing depends on BASIC/self-test visibility. Extended
    # memory also carries ROM latch state that PORTB registers alone omit.
    with bridge(emu) as control:
        control.config("memory", memory)
        control.cold_reset()
        control.pause()
        control.frame(1)
        control.ping()
        emu.send("netplay_checkpoint save cold")
        def sequence():
            result = []
            for _ in range(12):
                control.frame(1)
                control.ping()
                result.append((emu.send("netplay_checkpoint hash"), control.peek(0xA000, 16)))
            return result
        expected = sequence()
        emu.send("netplay_checkpoint restore cold")
        assert sequence() == expected


@pytest.mark.parametrize("restore", ["native", "serialized", "profile"])
@pytest.mark.parametrize("program", ["xex", "basic", "xex-diskboot"])
def test_pending_program_loader_replays_and_survives_session(emu, tmp_path, restore, program):
    def xex(address):
        return (b"\xff\xff" + struct.pack("<HH", address, address + 2)
                + bytes((0x4C, address & 255, address >> 8))
                + struct.pack("<HHH", 0x2E0, 0x2E1, address))
    original = tmp_path / ("original-pending.xex" if program.startswith("xex") else "original-pending.bas")
    original.write_bytes(xex(0x2000) if program.startswith("xex") else
                         (Path(__file__).resolve().parents[2]
                          / "src/ATBasic/tests/logical.bas").read_bytes())
    replacement = tmp_path / "session.xex"
    replacement.write_bytes(xex(0x3000))
    with bridge(emu) as control:
        if program == "xex-diskboot":
            control.config("exeloadmode", "diskboot")
        emu.send(f"boot_image {original}")
        control.pause()
        control.frame(1)
        control.ping()
        command = "netplay_transfer" if restore == "serialized" else "netplay_checkpoint"
        emu.send(f"{command} save pending")
        def sequence():
            result = []
            for _ in range(20):
                control.frame(8)
                control.ping()
                result.append(emu.send("netplay_checkpoint hash"))
            return result
        expected = sequence()
        emu.send(f"{command} restore pending")
        if restore == "profile":
            emu.send("netplay_profile begin")
            emu.send(f"boot_image {replacement}")
            assert emu.send("netplay_profile end")["restored"]
        assert sequence() == expected
        if program.startswith("xex"):
            assert 0x2000 <= int(control.regs()["PC"].lstrip("$"), 16) <= 0x2003


def test_malformed_basic_loader_snapshot_is_rejected(emu, tmp_path):
    import io
    import json
    import zipfile
    from altirra_bridge import RemoteError
    program = Path(__file__).resolve().parents[2] / "src/ATBasic/tests/logical.bas"
    with bridge(emu) as control:
        emu.send(f"boot_image {program}")
        control.pause()
        blob = control.state_save(inline=True)["data"]
        with zipfile.ZipFile(io.BytesIO(blob)) as archive:
            contents = {name: archive.read(name) for name in archive.namelist()}
        snapshot = json.loads(contents["savestate.json"])
        loader = next(obj for obj in snapshot["objects"] if obj["_type"] == "ATSaveStateHLEBasic")
        # An owned IOCB with no image used to reach GetChars' null-image
        # dereference. Reject it during parsing before replacing core state.
        loader.update(image=0, index=0, state=0, iocb=0x10, launch_hook=True)
        contents["savestate.json"] = json.dumps(snapshot).encode()
        invalid = tmp_path / "invalid-basic.atstate2"
        with zipfile.ZipFile(invalid, "w", zipfile.ZIP_DEFLATED) as archive:
            for name, data in contents.items():
                archive.writestr(name, data)
        with pytest.raises(RemoteError, match="STATE_LOAD"):
            control.state_load(str(invalid))
        assert control.ping()["ok"]


def test_serialized_xex_resumes_between_init_segments(emu, tmp_path):
    def segment(address, data):
        return struct.pack("<HH", address, address + len(data) - 1) + data
    image = tmp_path / "init-pending.xex"
    image.write_bytes(b"\xff\xff" + segment(0x4000, b"\x00")
                      + segment(0x2100, b"\xad\x00\x40\xf0\xfb\x60")
                      + segment(0x2E2, struct.pack("<H", 0x2100))
                      + segment(0x2000, b"\x4c\x00\x20")
                      + segment(0x2E0, struct.pack("<H", 0x2000)))
    with bridge(emu) as control:
        emu.send(f"boot_image {image}")
        control.pause()
        for _ in range(160):
            control.frame(1)
            control.ping()
            if 0x2100 <= int(control.regs()["PC"].lstrip("$"), 16) <= 0x2106:
                break
        else:
            pytest.fail("The XEX loader did not reach its waiting INIT segment")
        # The loader has consumed bytes and must resume at its saved cursor,
        # rather than restarting the file or losing the continuation hook.
        emu.send("netplay_transfer save init")
        def finish():
            control.poke(0x4000, 1)
            result = []
            for _ in range(12):
                control.frame(1)
                control.ping()
                result.append(emu.send("netplay_checkpoint hash"))
            return result
        expected = finish()
        emu.send("netplay_transfer restore init")
        assert control.peek(0x4000, 1) == b"\x00"
        assert finish() == expected
        assert 0x2000 <= int(control.regs()["PC"].lstrip("$"), 16) <= 0x2003


def test_completed_basic_loader_serialized_roundtrip(emu):
    program = Path(__file__).resolve().parents[2] / "src/ATBasic/tests/logical.bas"
    with bridge(emu) as control:
        emu.send(f"boot_image {program}")
        control.pause()
        control.frame(120)
        control.ping()
        emu.send("netplay_transfer save basic-completed")
        control.frame(8)
        control.ping()
        expected = emu.send("netplay_checkpoint hash")
        emu.send("netplay_transfer restore basic-completed")
        # Repeated resyncs must keep bulk image bytes in a binary entry,
        # rather than expanding them into the JSON object graph.
        import io
        import zipfile
        with zipfile.ZipFile(io.BytesIO(control.state_save(inline=True)["data"])) as archive:
            assert "hle-basic.bin" in archive.namelist()
        control.frame(8)
        control.ping()
        assert emu.send("netplay_checkpoint hash") == expected


@pytest.mark.parametrize("memory", ["576K", "1088K"])
@pytest.mark.parametrize("serialized", [False, True])
def test_banked_rom_latches_restore_mapping_and_future(emu, memory, serialized):
    # PORTB PB1/PB7 become bank-address lines in these modes. Their ROM
    # latches remember the last unbanked setting; PORTB alone is insufficient.
    with bridge(emu) as control:
        control.config("machine", "800XL")
        control.config("memory", memory)
        control.config("basic", "true")
        control.cold_reset()
        control.pause()
        # PIA: PBCTL bit 2 selects DDR/data (pia.cpp); set all outputs.
        control.poke(0xD303, 0x38)
        control.poke(0xD301, 0xFF)
        control.poke(0xD303, 0x3C)
        control.poke(0xD301, 0xFF)
        control.poke(0xA000, 0x42)
        # Read the actual BASIC and self-test mappings into extended RAM.
        program = bytes.fromhex("AD00A08D0040AD00508D01404C0020")
        for offset, value in enumerate(program):
            control.poke(0x2000 + offset, value)
        emu.send("debugger_console r pc 2000")

        def mapping():
            return (control.peek(0xA000, 16), control.peek(0x5000, 16),
                    control.peek(0xD301))

        def future():
            result = []
            for _ in range(4):
                control.frame(1)
                control.ping()
                result.append((emu.send("netplay_checkpoint hash"),
                               control.peek(0x4000, 2), mapping()))
            return result

        for roms_on in (True, False):
            # mmu.cpp SetBankRegister: bit4=1 updates ROM latches; bit4=0
            # holds them while PB1/PB7 select an extended memory bank.
            control.poke(0xD301, 0xFF)
            control.poke(0xD301, 0x7D if roms_on else 0xFF)
            control.poke(0xD301, 0x6D)
            # Match the emulator's frame-boundary checkpoint contract.
            control.frame(1)
            control.ping()
            original = mapping()
            emu.send("netplay_transfer save mmu" if serialized
                     else "netplay_checkpoint save mmu")
            expected = future()
            control.poke(0xD301, 0xFF if roms_on else 0x7D)
            control.poke(0xD301, 0xEF)
            assert mapping() != original
            emu.send("netplay_transfer restore mmu" if serialized
                     else "netplay_checkpoint restore mmu")
            assert mapping() == original
            assert future() == expected


@pytest.mark.parametrize("serialized", [False, True], ids=["native", "serialized"])
@pytest.mark.parametrize("beam_x", [42, 66])
def test_midscanline_character_dma_checkpoint_replays_collisions(emu, tmp_path, serialized, beam_x):
    # A real guest continuously reads P0PF into RAM. The player sits over
    # blank characters, so shifting ANTIC's partially filled character row
    # on restore produces an observable collision on the following scanline.
    code = b"\x78\xad\x04\xd0\x8d\x00\x40\x4c\x01\x20"
    image = tmp_path / "character-dma.xex"
    image.write_bytes(b"\xff\xff" + struct.pack("<HH", 0x2000, 0x2000 + len(code) - 1)
                      + code + struct.pack("<HHH", 0x2E0, 0x2E1, 0x2000))
    with bridge(emu) as control:
        emu.send(f"boot_image {image}")
        control.pause()
        control.frame(100)
        control.ping()
        control.poke(0xD40E, 0)  # NMIEN: keep the OS from changing our display.
        # ANTIC IR mode2 is 40x8 text (antic.cpp's mode table). A LMS at
        # $5003 addresses screen $6000, followed by23 more text rows +JVB.
        control.memload(0x5000, bytes([0x70] * 3 + [0x42, 0, 0x60]
                                     + [0x02] * 23 + [0x41, 0, 0x50]))
        control.memload(0x6000, bytes([0] * 10 + [1] * 10 + [2] * 10 + [3] * 10) * 24)
        control.memload(0x7000, bytes([0] * 8 + [255] * 8 + [170] * 8 + [85] * 8) + bytes(992))
        # Register decodes follow ANTIC::WriteByte and GTIA::WriteByte:
        # normal-width DL DMA, custom font, player0 atHPOS64, direct GRAFP0.
        for address, value in ((0xD402, 0), (0xD403, 0x50), (0xD400, 0x22),
                               (0xD401, 0), (0xD409, 0x70), (0xD000, 0x40),
                               (0xD008, 0), (0xD00D, 255), (0xD01D, 2)):
            control.poke(address, value)
        control.frame(2)
        control.ping()
        antic = control.antic()
        # NTSC fixture: 114 machine cycles/scanline,262 scanlines/frame.
        cycles = ((64 - antic["beam_y"]) % 262) * 114 + beam_x - antic["beam_x"]
        control.cycles(cycles if cycles > 0 else cycles + 262 * 114)
        control.ping()
        antic = control.antic()
        assert (antic["beam_x"], antic["beam_y"]) == (beam_x, 64)
        assert antic["DMACTL"] == "$22" and antic["CHBASE"] == "$70"
        control.poke(0xD01E, 0)  # HITCLR, after rendering the preceding pixels.
        command = "netplay_transfer" if serialized else "netplay_checkpoint"
        emu.send(f"{command} save mid-dma")
        def sequence():
            result = []
            for count in (1, 3, 7, 13, 23, 32, 64, 128):
                control.cycles(count)
                control.ping()
                regs = control.regs()
                result.append((emu.send("netplay_checkpoint hash"),
                               {key: regs[key] for key in ("PC", "A", "X", "Y", "S", "P")},
                               control.peek(0x4000, 1)))
            return result
        expected = sequence()
        assert expected[-1][2] == b"\x00", "The guest player must remain over a blank character"
        emu.send(f"{command} restore mid-dma")
        assert sequence() == expected


@pytest.mark.parametrize("serialized", [False, True], ids=["native", "serialized"])
def test_delayed_antic_register_queue_checkpoint_replays_all_writes(emu, serialized):
    import io
    import json
    import zipfile

    with bridge(emu) as control:
        control.frame(100)
        control.ping()
        control.poke(0xD40E, 0)
        # ANTIC queues CHBASE writes for two cycles. Bridge writes at the
        # same paused tick create two pending entries, exercising restoration
        # of the second triple independently of the first.
        control.poke(0xD409, 0x70)
        control.poke(0xD409, 0x74)
        with zipfile.ZipFile(io.BytesIO(control.state_save(inline=True)["data"])) as archive:
            snapshot = json.loads(archive.read("savestate.json"))
        antic = next(obj for obj in snapshot["objects"]
                     if obj["_type"] == "ATSaveStateAnticInternal")
        assert len(antic["register_updates"]) == 6
        command = "netplay_transfer" if serialized else "netplay_checkpoint"
        emu.send(f"{command} save queued-antic")

        def sequence():
            result = []
            for count in (1, 1, 1, 3, 8):
                control.cycles(count)
                control.ping()
                result.append((emu.send("netplay_checkpoint hash"),
                               control.antic()["CHBASE"]))
            return result

        expected = sequence()
        assert expected[-1][1] == "$74"
        emu.send(f"{command} restore queued-antic")
        assert sequence() == expected
