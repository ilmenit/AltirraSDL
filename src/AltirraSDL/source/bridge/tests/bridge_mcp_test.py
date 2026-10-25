#!/usr/bin/env python3
"""MCP protocol boundaries and shared bridge/MCP timing barriers."""
from __future__ import annotations
import concurrent.futures
import http.client
import json
import os
from pathlib import Path
import queue
import socket
import subprocess
import sys
import tempfile
import time
from bridge_custom_device_test import start_stderr_reader, wait_for_token_file


class MCP:
    def __init__(self, port, token="test-token"):
        self.port, self.token = port, token
        self.session = None

    def request(self, method="POST", body=None, headers=None):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=20)
        base = {"Content-Type": "application/json", "Authorization": "Bearer " + self.token}
        base.update(headers or {})
        conn.request(method, "/mcp", body=body, headers=base)
        response = conn.getresponse()
        data = response.read()
        result = response.status, dict(response.getheaders()), data
        conn.close()
        return result

    def rpc(self, method, params=None, legacy=False):
        params = dict(params or {})
        headers = {}
        if method != "initialize" and not legacy:
            params["_meta"] = {"io.modelcontextprotocol/protocolVersion": "2026-07-28"}
            headers = {"MCP-Protocol-Version": "2026-07-28", "Mcp-Method": method}
            if method in ("tools/call", "resources/read"):
                headers["Mcp-Name"] = params.get("name", params.get("uri", ""))
        if legacy and self.session:
            headers["Mcp-Session-Id"] = self.session
        status, response_headers, data = self.request(body=json.dumps({
            "jsonrpc": "2.0", "id": 1, "method": method, "params": params}), headers=headers)
        assert status == 200, (status, data)
        if method == "initialize":
            self.session = response_headers["Mcp-Session-Id"]
        parsed = json.loads(data)
        assert "error" not in parsed, parsed
        return parsed["result"]

    def tool(self, name, args=None):
        result = self.rpc("tools/call", {"name": name, "arguments": args or {}})
        assert not result.get("isError"), result
        return result.get("structuredContent", result)


def main():
    server = Path(sys.argv[1]).resolve()
    sys.path.insert(0, str(Path(sys.argv[2]).resolve()))
    from altirra_bridge import AltirraBridge
    with tempfile.TemporaryDirectory(prefix="altirra-mcp-test-") as td:
        env = os.environ.copy()
        env.update(TMPDIR=td, TEMP=td, TMP=td, XDG_CONFIG_HOME=str(Path(td) / "config"))
        (Path(td) / "config" / "altirra").mkdir(parents=True)
        with socket.socket() as reserve:
            reserve.bind(("127.0.0.1", 0))
            port = reserve.getsockname()[1]
        proc = subprocess.Popen([str(server), "--bridge=tcp:127.0.0.1:0",
            f"--mcp={port}", "--mcp-token=test-token"],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
            stdin=subprocess.DEVNULL, text=True, env=env)
        lines, pending = [], queue.Queue()
        reader = start_stderr_reader(proc, lines, pending)
        try:
            token = wait_for_token_file(proc, pending)
            mcp = MCP(port)
            deadline = time.monotonic() + 10
            while True:
                try:
                    assert mcp.request("GET")[0] == 405
                    break
                except ConnectionRefusedError:
                    assert time.monotonic() < deadline
                    time.sleep(0.01)
            # Host/Origin and bearer authorization are enforced before RPC.
            assert mcp.request(body="{}", headers={"Host": "attacker.invalid"})[0] == 403
            assert mcp.request(body="{}", headers={"Origin": "http://attacker.invalid"})[0] == 403
            assert MCP(port, "wrong-token").request(body="{}")[0] == 401
            # Recursive parser input must be rejected before stack exhaustion.
            assert mcp.request(body='{"x":' + '[' * 10000 + '0' + ']' * 10000 + '}')[0] == 400
            discovery = mcp.rpc("server/discover")
            assert discovery["resultType"] == "complete"
            assert "2026-07-28" in discovery["supportedVersions"]
            legacy = mcp.rpc("initialize", {"protocolVersion": "2025-11-25",
                "clientInfo": {"name": "regression", "version": "1"}, "capabilities": {}})
            assert legacy["protocolVersion"] == "2025-11-25"
            assert mcp.rpc("ping", legacy=True) == {}
            tools = {t["name"]: t for t in mcp.rpc("tools/list")["tools"]}
            assert "advance_frames" in tools
            assert tools["pokey_trace"]["inputSchema"]["properties"]["max_events"]["maximum"] == 4096
            assert tools["raw_screen"]["annotations"]["readOnlyHint"] is False
            assert len(mcp.rpc("resources/read", {"uri": "altirra://memory/0/16"})["contents"]) == 1
            # Bounds failures are normal tool errors, followed by a working RPC.
            bad = mcp.rpc("tools/call", {"name": "read_memory", "arguments": {"address": 0xFFFFFF, "length": 2}})
            assert bad["isError"]
            assert mcp.rpc("ping", {"ignored": '\\\"' + '[' * 10000})["resultType"] == "complete"
            with AltirraBridge.from_token_file(str(token)) as bridge:
                bridge.pause()
                bridge.config("video", "ntsc")
                bridge.boot_bare()
                before = bridge.clock()["tick"]
                bridge.frame(300)
                result = mcp.tool("advance_frames", {"count": 2})
                assert result["paused"] and result["frames_run"] == 2, result
                after = bridge.clock()["tick"]
                assert after - before >= 301 * 262 * 114, (before, after)
                # In the opposite order, the socket must wait for the MCP gate
                # before arming its own run, preserving both requested counts.
                before = after
                with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
                    future = pool.submit(mcp.tool, "advance_frames", {"count": 300})
                    deadline = time.monotonic() + 5
                    while not future.done():
                        status = mcp.rpc("resources/read", {"uri": "altirra://status"})
                        if json.loads(status["contents"][0]["text"])["running"]:
                            break
                        assert time.monotonic() < deadline
                    bridge.frame(2)
                    bridge.ping()  # Barrier after the socket's own two frames.
                    result = future.result()
                    assert result["paused"] and result["frames_run"] == 300, result
                after = bridge.clock()["tick"]
                assert after - before >= 301 * 262 * 114, (before, after)
                bridge.quit()
            assert proc.wait(timeout=10) == 0
        except Exception:
            print("\n".join(lines), file=sys.stderr)
            raise
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
            reader.join(timeout=5)
            proc.stderr.close()
    print("MCP protocol, authorization, limits and shared timing barriers passed")


if __name__ == "__main__":
    main()
