"""Portable custom effects through actual GL and SDL_GPU display pipelines."""
import json
import os
import shutil
import threading
from pathlib import Path

import pytest
from PIL import Image, ImageChops, ImageStat, ImageOps

from .harness import AltirraTestHarness


@pytest.fixture(params=["opengl", "sdlgpu"])
def renderer(request, monkeypatch, tmp_path):
    monkeypatch.setenv("SDL_VIDEODRIVER", os.environ.get("ALTIRRA_EFFECT_TEST_VIDEO_DRIVER", "offscreen"))
    monkeypatch.setenv("SDL_AUDIODRIVER", "dummy")
    monkeypatch.setenv("ALTIRRA_DISPLAY_BACKEND", request.param)
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "config"))
    with AltirraTestHarness(executable=request.config.getoption("--emu-path"),
                           extra_args=["--ui-mode", "desktop"]) as app:
        output = (tmp_path / "stderr.log").open("wb")
        def drain():
            for line in app._proc.stderr:
                output.write(line)
                output.flush()
        threading.Thread(target=drain, daemon=True).start()
        app.close_dialog("SetupWizard")
        app.wait_frames(30)
        app.send("pause")
        expected = "OpenGL" if request.param == "opengl" else "SDL_GPU"
        actual = app.query_state()["state"]["videoOutput"]["backend"]
        if actual != expected:
            pytest.skip(f"Requested {expected} unavailable, active backend is {actual}")
        yield app


def capture(app, path):
    app.wait_frames(3)
    app.send(f"ui_screenshot {path}")
    image = Image.open(path).convert("RGB")
    w, h = image.size
    return image.crop((w // 3, h // 3, w * 2 // 3, h * 2 // 3))


def select(app, path):
    app.send(f"custom_effect {path}")
    app.wait_frames(3)
    return app.send("custom_effect")


def test_effect_render_multipass_reload_failure_retention_and_clear(renderer, tmp_path):
    source = Path(__file__).resolve().parents[2] / "dist/extras/customeffects/portable"
    assets = tmp_path / "effect package with spaces"
    shutil.copytree(source, assets)
    package = assets / "invert.altfx"
    baseline = capture(renderer, tmp_path / "base.png")
    result = select(renderer, package)
    assert result["error"] == ""
    assert result["active"] == str(package)
    inverted = capture(renderer, tmp_path / "invert.png")
    # The emulator is paused, so image differences are caused by the shader.
    assert sum(ImageStat.Stat(ImageChops.difference(baseline, inverted)).mean) > 60
    oversized = json.loads(package.read_text())
    oversized["passes"][0].update(scale_x=16, scale_y=16)
    too_large = assets / "oversized.altfx"
    too_large.write_text(json.dumps(oversized))
    result = select(renderer, too_large)
    assert result["error"] and result["active"] == str(package)
    assert ImageChops.difference(inverted, capture(renderer, tmp_path / "oversize-retained.png")).getbbox() is None
    select(renderer, package)
    # No-op reload still rereads and recompiles the same selected file.
    renderer.send("custom_effect reload")
    renderer.wait_frames(3)
    assert renderer.send("custom_effect")["error"] == ""
    assert ImageChops.difference(inverted, capture(renderer, tmp_path / "reload.png")).getbbox() is None

    content = json.loads(package.read_text())
    content["passes"] *= 2
    content["passes"][0]["scale_x"] = 2
    content["passes"][0]["scale_y"] = 2
    # Create distinct dictionaries: the source JSON has one pass, so list
    # multiplication alone would alias the same mutable object.
    content["passes"][1] = dict(content["passes"][1], scale_x=0.5, scale_y=0.5)
    double = assets / "double.altfx"
    double.write_text(json.dumps(content))
    result = select(renderer, double)
    assert result["error"] == ""
    restored = capture(renderer, tmp_path / "double.png")
    assert max(ImageStat.Stat(ImageChops.difference(baseline, restored)).mean) < 2

    bad = assets / "broken.altfx"
    bad.write_text('{"version":1,"passes":[{"glsl":"missing.glsl"}]}')
    result = select(renderer, bad)
    assert result["error"]
    assert result["active"] == str(double)
    assert ImageChops.difference(restored, capture(renderer, tmp_path / "retained.png")).getbbox() is None
    # Driver compilation and current-driver representation errors must also
    # retain the complete prior working pipeline, including both scale passes.
    if renderer.query_state()["state"]["videoOutput"]["backend"] == "OpenGL":
        (assets / "broken.glsl").write_text("this is not valid GLSL")
        shader_bad = {"version": 1, "passes": [{"glsl": "broken.glsl"}]}
    else:
        shader_bad = {"version": 1, "passes": [{"glsl": "invert.frag.glsl"}]}
    bad.write_text(json.dumps(shader_bad))
    renderer.send("custom_effect reload")
    renderer.wait_frames(3)
    result = renderer.send("custom_effect")
    assert result["error"] and result["active"] == str(double)
    assert ImageChops.difference(restored, capture(renderer, tmp_path / "compile-retained.png")).getbbox() is None
    result = select(renderer, "clear")
    assert result["error"] == "" and result["active"] == ""
    assert max(ImageStat.Stat(ImageChops.difference(baseline, capture(renderer, tmp_path / "clear.png"))).mean) < 2


def test_software_renderer_reports_effect_capability(request, monkeypatch, tmp_path):
    monkeypatch.setenv("SDL_VIDEODRIVER", "dummy")
    monkeypatch.setenv("SDL_AUDIODRIVER", "dummy")
    monkeypatch.setenv("ALTIRRA_DISPLAY_BACKEND", "sdlrenderer")
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "config"))
    with AltirraTestHarness(executable=request.config.getoption("--emu-path"),
                           extra_args=["--ui-mode", "desktop"]) as app:
        threading.Thread(target=lambda: app._proc.stderr.read(), daemon=True).start()
        app.close_dialog("SetupWizard")
        app.wait_frames(10)
        package = Path(__file__).resolve().parents[2] / "dist/extras/customeffects/portable/invert.altfx"
        result = select(app, package)
        assert "SDL_GPU or OpenGL" in result["error"]
        assert result["active"] == ""
        assert select(app, "clear")["error"] == ""


def test_identity_pipeline_preserves_orientation_and_rgb(renderer, tmp_path):
    renderer.send("screen_effects none")
    renderer.wait_frames(10)
    source = Path(__file__).resolve().parents[2] / "dist/extras/customeffects/portable/invert.altfx"
    baseline = capture(renderer, tmp_path / "none-base.png")
    assert select(renderer, source)["error"] == ""
    inverted = capture(renderer, tmp_path / "none-invert.png")
    # With optional CRT effects disabled, a single-pass inversion must invert
    # the same screen pixel; two accidentally flipped passes cannot mask this.
    assert max(ImageStat.Stat(ImageChops.difference(ImageOps.invert(baseline), inverted)).mean) < 2


def test_viewport_failure_status_recovers_after_resize(renderer, tmp_path):
    source = Path(__file__).resolve().parents[2] / "dist/extras/customeffects/portable"
    assets = tmp_path / "viewport-effect"
    shutil.copytree(source, assets)
    package = assets / "viewport.altfx"
    content = json.loads((assets / "invert.altfx").read_text())
    content["passes"][0].update(scale="viewport", scale_x=4, scale_y=4)
    package.write_text(json.dumps(content))
    renderer.send("set_window_size 320 240")
    renderer.wait_frames(5)
    result = select(renderer, package)
    assert result["error"] == "" and result["active"] == str(package)
    renderer.send("set_window_size 2560 1440")
    renderer.wait_frames(5)
    result = renderer.send("custom_effect")
    assert result["error"] and result["active"] == str(package)
    renderer.send("set_window_size 320 240")
    renderer.wait_frames(5)
    result = renderer.send("custom_effect")
    assert result["error"] == "" and result["active"] == str(package)
