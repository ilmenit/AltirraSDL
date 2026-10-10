#!/usr/bin/env python3
"""Check canonical headless defaults and frame production after initialization."""

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
    from altirra_bridge import AltirraBridge

    cases = [([], "none"), (["--settings=none"], "none")]
    # POSIX settings.ini fixtures must never touch the real user store.
    # Windows user mode uses the registry, so only test isolated mode there.
    if os.name != "nt":
        cases += [(["--settings=user"], "type1")]
    for args, expected_defect in cases:
        with tempfile.TemporaryDirectory(prefix="altirra-defaults-") as td:
            env = os.environ.copy()
            env.update(TMPDIR=td, TEMP=td, TMP=td, XDG_CONFIG_HOME=td)
            config_dir = Path(td) / "altirra"
            config_dir.mkdir()
            settings = config_dir / "settings.ini"
            fixture = ('[User\\AltirraSDL\\Profiles]\n'
                       '"Defaults inited" = 1\n'
                       '"Current profile" = 0\n'
                       '[User\\AltirraSDL\\Profiles\\00000000]\n'
                       '"GTIA: Defect mode" = "type1"\n')
            settings.write_text(fixture)
            proc = subprocess.Popen(
                [str(server), "--bridge=tcp:127.0.0.1:0", *args],
                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                text=True, env=env)
            lines = []
            pending = queue.Queue()
            reader = start_stderr_reader(proc, lines, pending)
            try:
                token = wait_for_token_file(proc, pending)
                with AltirraBridge.from_token_file(str(token)) as bridge:
                    assert bridge.ping()["ok"]
                    bridge.frame(20)
                    assert bridge.screenshot()[:8] == b"\x89PNG\r\n\x1a\n"
                    bridge.quit()
                assert proc.wait(timeout=10) == 0
                reader.join(timeout=5)
                stage = ("loaded from user settings" if expected_defect == "type1"
                         else "deterministic defaults")
                config = next(line for line in lines if stage + " config:" in line)
                assert "gtia_defect=" + expected_defect in config, config
                assert "cassette_decode=peakFilter" in config, config
                if expected_defect == "none":
                    assert "machine=800XL" in config, config
                    assert "basic=off" in config, config
                assert settings.read_text() == fixture
            except Exception:
                print("\n".join(lines), file=sys.stderr)
                raise
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait()
                reader.join(timeout=5)
                proc.stderr.close()
    print("Bridge defaults and frame production OK")


if __name__ == "__main__":
    main()
