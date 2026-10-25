"""Regression checks for macro frame budgets and lossless TYPE framing."""
import pathlib
import base64
from contextlib import redirect_stdout
import io
import json
import socket
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] /
    "src/AltirraSDL/AltirraBridge/sdk/python"))
from altirra_bridge import AltirraBridge, BridgeError
from altirra_bridge.macro import run_macro
from altirra_bridge import RawFrame
from altirra_bridge.cli_macro import main as macro_main


class FakeBridge:
    def __init__(self, ready_after=100):
        self.elapsed = 0
        self.ready_after = ready_after
        self.batches = []

    def frame(self, count):
        self.batches.append(count)
        self.elapsed += count

    def screen_text(self):
        return ["READY"] if self.elapsed >= self.ready_after else []

    def eval_expr(self, expr):
        return self.elapsed >= self.ready_after


class MacroTests(unittest.TestCase):
    def test_cli_prints_lossless_memory_and_frame_results(self):
        class CaptureBridge:
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def macro(self, steps, stop_on_error=True):
                return run_macro(self, steps, stop_on_error)
            def peek(self, addr): return b"\x00\xff"
            def rawscreen(self): return RawFrame(1, 1, 4, b"\x01\x02\x03\x00")

        with tempfile.TemporaryDirectory() as directory:
            macro = pathlib.Path(directory) / "capture.json"
            macro.write_text(json.dumps([
                {"peek": {"addr": 0}}, {"rawscreen": {}}]))
            output = io.StringIO()
            with patch.object(sys, "argv", ["altirra-macro", "token", str(macro)]), \
                    patch.object(AltirraBridge, "from_token_file", return_value=CaptureBridge()), \
                    redirect_stdout(output):
                self.assertEqual(macro_main(), 0)
            result = json.loads(output.getvalue())
            self.assertTrue(result["ok"])
            memory = result["results"][0]["result"]
            self.assertEqual(memory["encoding"], "base64")
            self.assertEqual(base64.b64decode(memory["data"]), b"\x00\xff")
            frame = result["results"][1]["result"]
            self.assertEqual((frame["width"], frame["height"], frame["stride"]), (1, 1, 4))
            self.assertEqual(base64.b64decode(frame["pixels"]["data"]), b"\x01\x02\x03\x00")

    def test_wait_preserves_exact_frame_budget(self):
        for kind, condition in (("wait_text", {"text": "READY"}),
                                ("wait_expr", {"expr": "$600"})):
            with self.subTest(kind=kind):
                bridge = FakeBridge()
                result = run_macro(bridge, [{kind: dict(condition,
                    timeout_frames=13, poll_frames=10)}])
                self.assertFalse(result["ok"])
                self.assertEqual(bridge.batches, [10, 3])

    def test_already_matching_condition_does_not_advance(self):
        bridge = FakeBridge(ready_after=0)
        result = run_macro(bridge, [{"wait_text": {
            "text": "READY", "timeout_frames": 0}}])
        self.assertTrue(result["ok"])
        self.assertEqual(result["results"][0]["frames_waited"], 0)
        self.assertEqual(bridge.batches, [])

    def test_elapsed_frames_include_successful_batch(self):
        bridge = FakeBridge(ready_after=10)
        result = run_macro(bridge, [{"wait_expr": {
            "expr": "$600", "timeout_frames": 20, "poll_frames": 10}}])
        self.assertTrue(result["ok"])
        self.assertEqual(result["results"][0]["frames_waited"], 10)

    def test_invalid_wait_arguments_report_errors_without_running(self):
        invalid = [{"poll_frames": 0}, {"poll_frames": -1},
                   {"poll_frames": 1.5}, {"timeout_frames": -1},
                   {"timeout_frames": "20"}, {"unexpected": True}]
        for kind, condition in (("wait_text", {"text": "READY"}),
                                ("wait_expr", {"expr": "$600"})):
            for args in invalid:
                with self.subTest(kind=kind, args=args):
                    bridge = FakeBridge()
                    result = run_macro(bridge, [{kind: dict(
                        dict(condition, timeout_frames=10), **args)}])
                    self.assertFalse(result["ok"])
                    self.assertEqual(bridge.batches, [])

    def test_type_sends_one_lossless_quoted_token(self):
        client, server = socket.socketpair()
        bridge = AltirraBridge()
        bridge._sock = client
        try:
            server.sendall(b'{"ok":true,"typed":20,"skipped":[]}\n')
            bridge.type('  PRINT "A  B" \\ C  ')
            command = server.recv(1024)
            self.assertEqual(command, b'TYPE "  PRINT \\"A  B\\" \\\\ C  "\n')
        finally:
            bridge.close()
            server.close()

    def test_type_rejects_protocol_delimiters(self):
        bridge = AltirraBridge()
        for text in ("one\ntwo", "one\rtwo"):
            with self.assertRaises(BridgeError):
                bridge.type(text)


if __name__ == "__main__":
    unittest.main()
