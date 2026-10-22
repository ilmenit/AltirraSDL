"""Real worker dialog completions must be delivered by the main-thread poll."""
import pytest

from .harness import AltirraTestHarness


@pytest.fixture
def picker_app(request, monkeypatch, tmp_path):
    monkeypatch.setenv("SDL_VIDEODRIVER", "dummy")
    monkeypatch.setenv("SDL_AUDIODRIVER", "dummy")
    monkeypatch.setenv("ALTIRRA_DISPLAY_BACKEND", "sdlrenderer")
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "config"))
    with AltirraTestHarness(executable=request.config.getoption("--emu-path"),
                           extra_args=["--ui-mode", "desktop"]) as app:
        app.close_dialog("SetupWizard")
        app.wait_frames(3)
        yield app


def settled(app):
    app.wait_frames(8)
    result = app.send("file_dialog_worker status")
    assert not result["pickerPaused"]
    assert not app.query_state()["state"]["sim"]["fileDialogPaused"]
    return result


def test_temporary_worker_paths_and_filter_survive_until_main_callback(picker_app):
    app = picker_app
    app.send("resume")
    app.wait_frames(3)
    queued = app.send("file_dialog_worker success")
    assert queued["callbacks"] == 0 and queued["paths"] == []
    assert queued["pickerPaused"]
    result = settled(app)
    assert result["callbacks"] == 1
    assert result["mainThread"] and result["leaseHeldInCallback"]
    assert result["filter"] == 7
    assert result["paths"] == ["/tmp/dialog selection one.atr", "/tmp/dialog selection two.atr"]
    assert app.query_state()["state"]["sim"]["running"]


@pytest.mark.parametrize("notify,callbacks", [(False, 0), (True, 1)])
def test_worker_cancellation_preserves_callback_opt_in(picker_app, notify, callbacks):
    app = picker_app
    app.send("pause")
    queued = app.send("file_dialog_worker cancel" + (" notify" if notify else ""))
    assert queued["callbacks"] == 0 and queued["pickerPaused"]
    result = settled(app)
    assert result["callbacks"] == callbacks and result["paths"] == []
    if notify:
        assert result["mainThread"] and result["leaseHeldInCallback"]
    assert app.query_state()["state"]["sim"]["paused"]


def test_throwing_main_callback_releases_pause_lease(picker_app):
    app = picker_app
    app.send("pause")
    assert app.send("file_dialog_worker throw")["callbacks"] == 0
    result = settled(app)
    assert result["callbacks"] == 1
    assert result["mainThread"] and result["leaseHeldInCallback"]
    assert app.query_state()["state"]["sim"]["paused"]
    assert app.ping()["ok"]


def test_late_native_completion_after_shutdown_cannot_call_old_userdata(picker_app):
    app = picker_app
    app.send("pause")
    result = app.send("file_dialog_worker late notify")
    assert result["callbacks"] == 0 and not result["pickerPaused"]
    assert settled(app)["callbacks"] == 0
    # A fresh lifecycle must still accept a new picker normally.
    app.send("file_dialog_worker success")
    result = settled(app)
    assert result["callbacks"] == 1 and result["mainThread"]


def test_worker_native_error_falls_back_without_releasing_context_or_lease(picker_app):
    app = picker_app
    app.send("pause")
    queued = app.send("file_dialog_worker error notify")
    assert queued["callbacks"] == 0 and queued["pickerPaused"]
    app.wait_frames(5)
    assert any(item["label"] == "Cancel" for item in app.list_items("Open File"))
    pending = app.send("file_dialog_worker status")
    assert pending["callbacks"] == 0 and pending["pickerPaused"]
    app.click("Open File", "Cancel")
    result = settled(app)
    assert result["callbacks"] == 1 and result["paths"] == []
    assert result["mainThread"] and result["leaseHeldInCallback"]
    assert result["filter"] == 0  # built-in picker chooses its own filter
    assert app.query_state()["state"]["sim"]["paused"]
