#!/usr/bin/env python3
"""Exact cycle stepping and lossless intra-frame POKEY bus writes."""
from __future__ import annotations
import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
from bridge_custom_device_test import start_stderr_reader, wait_for_token_file


def main():
    server = Path(sys.argv[1]).resolve()
    sys.path.insert(0, str(Path(sys.argv[2]).resolve()))
    from altirra_bridge import AltirraBridge, RemoteError
    with tempfile.TemporaryDirectory(prefix="altirra timing test ") as td:
        env = os.environ.copy()
        env.update(TMPDIR=td, TEMP=td, TMP=td, XDG_CONFIG_HOME=str(Path(td) / "config"))
        (Path(td) / "config" / "altirra").mkdir(parents=True)
        def boot_fixture(a):
            a.pause()
            a.boot_bare()
            assert not a.clock()["running"]  # restore the caller's pause state
            assert 0x60f <= int(a.regs()["PC"][1:], 16) <= 0x612, a.regs()
        proc = subprocess.Popen([str(server), "--bridge=tcp:127.0.0.1:0", *sys.argv[3:]],
                                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                                stdin=subprocess.DEVNULL, text=True, env=env)
        lines, pending = [], queue.Queue()
        reader = start_stderr_reader(proc, lines, pending)
        try:
            token = wait_for_token_file(proc, pending)
            with AltirraBridge.from_token_file(str(token)) as a:
                boot_fixture(a)
                for video in ("pal", "ntsc"):
                    a.config("video", video)
                    boot_fixture(a)
                    for n in (1, 2, 113, 114, 115, 40000):
                        before = a.clock()["tick"]
                        step = a.cycles(n)
                        after = a.clock()
                        assert step["start_tick"] == before
                        assert after["tick"] == before + n, (n, before, after)
                        assert after["step_elapsed"] == n and after["step_completed"]
                        assert not after["running"]
                    beam = a.antic()["beam_x"]
                    before = a.clock()["tick"]
                    a.scanlines(79)  # quarter PAL frame, crosses scanline boundaries
                    after = a.clock()
                    assert after["tick"] - before == 79 * 114
                    assert a.antic()["beam_x"] == beam
                    # Completion history must not grow when subsequent frames run.
                    a.frame(1)
                    assert a.clock()["step_elapsed"] == 79 * 114

                for cmd in ("CYCLES 0", "CYCLES -1", "CYCLES 100000001",
                            "SCANLINES 877193", "CYCLES 1 extra", "CLOCK extra",
                            "POKEY_TRACE start 0", "POKEY_TRACE read 4097"):
                    try:
                        a._cmd_ok(cmd)
                        raise AssertionError(cmd)
                    except RemoteError:
                        pass

                a.config("stereo", "true")
                boot_fixture(a)
                start = a.pokey_trace_start(16)
                assert len(start["initial"]) == 2
                for addr, value in ((0xd200, 17), (0xd200, 17), (0xd210, 23)):
                    a.hwpoke(addr, value)
                events = a.pokey_trace_read()["events"]
                assert [(e["chip"], e["reg"], e["value"]) for e in events] == [
                    (1, 0, 17), (1, 0, 17), (2, 0, 23)]
                assert all(e["tick"] == start["start_tick"] for e in events)

                # Redirect the parked JMP to a real 6502 writer loop. The loop
                # repeats identical values too: LDA #$55; STA $D200; STA $D210;
                # JMP $0700. There are many writes within a single scanline.
                # Stop before the parked JMP fetch: changing its operands
                # halfway through an instruction could jump to a mixed address.
                parked_bp = a.bp_set(0x60f)
                a.cycles(1000)
                assert not a.clock()["step_completed"]
                a.bp_clear(parked_bp)
                a.memload(0x700, bytes.fromhex("a9558d00d28d10d24c0007"))
                a.memload(0x60f, bytes.fromhex("4c0007"))
                a.pokey_trace_start(4096)
                before = a.clock()["tick"]
                a.scanlines(1)
                assert a.clock()["tick"] == before + 114
                batch = a.pokey_trace_read(3)
                events = batch["events"]
                assert len(events) == 3 and batch["queued"] > 0
                rest = a.pokey_trace_read()
                events += rest["events"]
                assert rest["dropped"] == 0 and len(events) >= 12, events
                assert all(e["value"] == 0x55 for e in events)
                assert [e["chip"] for e in events] == [1 + i % 2 for i in range(len(events))]
                assert all(before < e["tick"] <= before + 114 for e in events)
                assert all(x["tick"] < y["tick"] for x, y in zip(events, events[1:]))
                a.pokey_trace_start(2)
                a.scanlines(1)
                stopped = a.pokey_trace_stop()
                assert stopped["queued"] == 2 and stopped["dropped"] > 0
                assert not stopped["valid"]
                a.scanlines(1)
                assert a.pokey_trace_status() == stopped
                assert len(a.pokey_trace_read()["events"]) == 2

                # A debugger stop releases the barrier and cancels the deadline.
                bp = a.bp_set(0x700)
                before = a.clock()["tick"]
                a.cycles(10000)
                stopped = a.clock()
                assert not stopped["step_completed"] and not stopped["running"]
                assert stopped["step_elapsed"] == stopped["tick"] - before < 10000
                a.bp_clear(bp)
                a.cycles(11)
                assert a.clock()["step_completed"]
                # Frame runs must also release their gate on a breakpoint.
                bp = a.bp_set(0x700)
                a.frame(2)
                assert not a.clock()["running"]
                a.bp_clear(bp)

                # Closing during a long run cancels the deadline and trace.
                a.pokey_trace_start(2)
                a.cycles(100000000)
            with AltirraBridge.from_token_file(str(token)) as a:
                a.pause()
                assert a.clock()["step_requested"] == 0
                assert a.clock()["step_elapsed"] == 0
                status = a.pokey_trace_status()
                assert not status["recording"] and status["queued"] == 0
                assert not status["valid"]
                a.cycles(7)
                assert a.clock()["step_elapsed"] == 7
                # Reset/state replacement cannot be mistaken for an exact
                # register stream. Restart capture to establish a new baseline.
                a.pokey_trace_start()
                assert a.pokey_trace_status()["valid"]
                a.warm_reset()
                assert a.pokey_trace_status()["invalidated"]
                assert not a.pokey_trace_status()["valid"]
                a.pokey_trace_start()
                a.state_save(slot="trace-invalidation")
                a.state_load(slot="trace-invalidation")
                assert a.pokey_trace_status()["invalidated"]
                a.pokey_trace_start()
                a.cold_reset()
                assert a.pokey_trace_status()["invalidated"]
                a.pokey_trace_start()
                a.pokey_trace_stop()
                a.cold_reset()
                # A finished trace is unaffected by later resets.
                assert a.pokey_trace_status()["valid"]
                # FRESH clears capture even when the reset itself is skipped.
                a.pokey_trace_start()
                a._cmd_ok("FRESH keep=media,reset")
                assert not a.pokey_trace_status()["valid"]
                assert not a.pokey_trace_status()["recording"]

                # Boot interruption must return an error and release its gate.
                bp = a.bp_set(0x600)
                try:
                    a.boot_bare()
                    raise AssertionError("boot breakpoint must stop the boot")
                except RemoteError as e:
                    assert "stopped" in str(e), e
                assert not a.clock()["running"]
                a.bp_clear(bp)
                boot_fixture(a)
                a.quit()
            assert proc.wait(timeout=10) == 0
        except Exception:
            print("\n".join(lines), file=sys.stderr)
            raise
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
            reader.join(timeout=5)
            if proc.stderr:
                proc.stderr.close()
    print("Bridge timing and POKEY write capture passed")


if __name__ == "__main__":
    main()
