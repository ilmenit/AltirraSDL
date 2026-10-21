"""Offline Android SDL pin, Java linkage and native-cache regression tests."""

from pathlib import Path
import shutil
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[1]
TAG = "release-3.4.14"


def git(directory, *args):
    return subprocess.check_output(
        ["git", "-C", str(directory), *args], text=True,
        stderr=subprocess.PIPE).strip()


@pytest.fixture
def setup_tree(tmp_path):
    upstream = tmp_path / "upstream"
    upstream.mkdir()
    git(upstream, "init", "-q")
    git(upstream, "config", "user.name", "Setup test")
    git(upstream, "config", "user.email", "setup@example.invalid")
    java = upstream / "android-project/app/src/main/java/org/libsdl"
    java.mkdir(parents=True)
    (java / "SDLActivity.java").write_text("old SDL\n")
    (upstream / "CMakeLists.txt").write_text("# old SDL\n")
    git(upstream, "add", ".")
    git(upstream, "commit", "-qm", "Old release")
    git(upstream, "tag", "release-3.4.4")
    old = git(upstream, "rev-parse", "HEAD")
    (java / "SDLActivity.java").write_text("new SDL\n")
    git(upstream, "add", ".")
    git(upstream, "commit", "-qm", "Pinned release")
    git(upstream, "tag", TAG)

    android = tmp_path / "project/android"
    android.mkdir(parents=True)
    script = (ROOT / "android/setup_sdl3.sh").read_text()
    # Exercise real git clone/fetch/checkout without network access.
    script = script.replace("https://github.com/libsdl-org/SDL.git",
                            upstream.as_uri())
    (android / "setup_sdl3.sh").write_text(script)
    (android / "gradlew").touch()
    return android, upstream, old


def run_setup(android, success=True):
    result = subprocess.run(["bash", str(android / "setup_sdl3.sh")],
                            text=True, capture_output=True)
    assert (result.returncode == 0) == success, result.stdout + result.stderr
    return result


def clone_old(android, upstream):
    git(android, "clone", "--quiet", "--depth", "1", "--branch",
        "release-3.4.4", upstream.as_uri(), "SDL3")


def cache_sentinel(android):
    sentinel = android / "app/.cxx/old-build.ninja"
    sentinel.parent.mkdir(parents=True, exist_ok=True)
    sentinel.write_text("old native build")
    return sentinel


def test_new_clone_and_repeated_setup_keep_current_cache(setup_tree):
    android, upstream, _ = setup_tree
    run_setup(android)
    expected = git(upstream, "rev-parse", TAG)
    assert git(android / "SDL3", "rev-parse", "HEAD") == expected
    link = android / "app/src/main/java/org/libsdl"
    assert link.is_symlink()
    assert (link / "SDLActivity.java").read_text() == "new SDL\n"
    sentinel = cache_sentinel(android)
    run_setup(android)
    assert sentinel.exists()
    assert (android / "app/.cxx/.altirra-sdl3-revision").read_text().strip() == expected


def test_cached_old_clone_is_updated_and_native_cache_invalidated(setup_tree):
    android, upstream, _ = setup_tree
    clone_old(android, upstream)
    sentinel = cache_sentinel(android)
    (sentinel.parent / ".altirra-sdl3-revision").write_text("old revision\n")
    run_setup(android)
    assert git(android / "SDL3", "rev-parse", "HEAD") == git(upstream, "rev-parse", TAG)
    assert not sentinel.exists()


def test_unstamped_native_cache_is_invalidated(setup_tree):
    android, _, _ = setup_tree
    sentinel = cache_sentinel(android)
    run_setup(android)
    assert not sentinel.exists()


@pytest.mark.parametrize("dirty", ["tracked", "untracked", "staged"])
def test_dirty_clone_is_preserved_without_touching_cache(setup_tree, dirty):
    android, upstream, old = setup_tree
    clone_old(android, upstream)
    path = android / "SDL3" / ("notes.txt" if dirty == "untracked" else "CMakeLists.txt")
    path.write_text("developer work\n")
    if dirty == "staged":
        git(android / "SDL3", "add", "CMakeLists.txt")
    sentinel = cache_sentinel(android)
    result = run_setup(android, success=False)
    assert "local changes" in result.stderr
    assert path.read_text() == "developer work\n"
    assert git(android / "SDL3", "rev-parse", "HEAD") == old
    assert sentinel.exists()


def test_dirty_pinned_clone_also_fails(setup_tree):
    android, _, _ = setup_tree
    run_setup(android)
    (android / "SDL3/CMakeLists.txt").write_text("developer work\n")
    assert "local changes" in run_setup(android, success=False).stderr


@pytest.mark.parametrize("broken", [False, True])
def test_wrong_or_broken_java_symlink_is_repaired(setup_tree, broken):
    android, _, _ = setup_tree
    link = android / "app/src/main/java/org/libsdl"
    link.parent.mkdir(parents=True)
    target = android / "other-java"
    if not broken:
        target.mkdir()
        (target / "keep.java").write_text("keep\n")
    link.symlink_to(target)
    run_setup(android)
    assert (link / "SDLActivity.java").read_text() == "new SDL\n"
    if not broken:
        assert (target / "keep.java").read_text() == "keep\n"


def test_real_java_directory_is_preserved(setup_tree):
    android, _, _ = setup_tree
    java = android / "app/src/main/java/org/libsdl"
    java.mkdir(parents=True)
    (java / "custom.java").write_text("developer work\n")
    sentinel = cache_sentinel(android)
    assert "not a symlink" in run_setup(android, success=False).stderr
    assert (java / "custom.java").read_text() == "developer work\n"
    assert sentinel.exists()


def test_fetch_failure_preserves_old_clone_and_cache(setup_tree):
    android, upstream, old = setup_tree
    clone_old(android, upstream)
    shutil.rmtree(upstream)
    sentinel = cache_sentinel(android)
    run_setup(android, success=False)
    assert git(android / "SDL3", "rev-parse", "HEAD") == old
    assert sentinel.exists()


def test_non_git_directory_is_preserved(setup_tree):
    android, _, _ = setup_tree
    (android / "SDL3").mkdir()
    marker = android / "SDL3/keep"
    marker.touch()
    assert "Git clone" in run_setup(android, success=False).stderr
    assert marker.exists()


def test_build_always_runs_setup():
    script = (ROOT / "scripts/build/android.sh").read_text()
    section = script.split('info "Checking SDL3 Android setup..."', 1)[1]
    section = section.split('ok "SDL3 ready"', 1)[0]
    assert '(cd "$ANDROID_DIR" && bash setup_sdl3.sh)' in section
    assert "if " not in section
