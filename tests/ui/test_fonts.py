"""Installed font discovery, selection, persistence, and scalable search."""
import json
import shutil
import subprocess
import sys
from pathlib import Path
from xml.sax.saxutils import escape

import pytest

from .harness import AltirraTestHarness
from .test_desktop_search_profiles import desktop, search, search_input_window
from .test_explorers import click, enter


def installed(family):
    if not shutil.which("fc-list"):
        pytest.skip("Native Linux font catalog not available")
    families = subprocess.check_output(
        ["fc-list", "-f", "%{family[0]}\n"], text=True).splitlines()
    if family not in families:
        pytest.skip(f"{family} is not installed")


def choose_family(emu, role, family):
    click(emu, "", "Family##" + role)
    enter(emu, "", "##familyFilter", family.lower())
    assert family in emu.get_item_labels()
    click(emu, "", family)


def fonts(emu):
    # The Fonts sidebar entry starts below the visible area. Use the same
    # command as Debug > Change Font to open the existing page directly.
    emu.send("run_command Debug.ChangeFontDialog")
    emu.wait_frames(5)


@pytest.mark.parametrize("family", ["Liberation Serif", "Noto Sans", "JetBrains Mono"])
def test_installed_font_family_filter_and_refresh(desktop, family):
    installed(family)
    fonts(desktop)
    choose_family(desktop, "ui", family)
    click(desktop, "", "Refresh font list")
    choose_family(desktop, "ui", family)
    desktop.close_dialog("SystemConfig")
    search(desktop, "VBXE")
    assert "VideoBoard XE (VBXE)" in desktop.get_item_labels("Search actions")


def test_font_styles_and_persistence(desktop, tmp_path):
    installed("Liberation Serif")
    installed("JetBrains Mono")
    fonts(desktop)
    choose_family(desktop, "ui", "Liberation Serif")
    click(desktop, "", "Style##ui")
    assert "Bold Italic" in desktop.get_item_labels()
    click(desktop, "", "Bold Italic")
    choose_family(desktop, "mono", "JetBrains Mono")
    desktop.send("quit")
    desktop._proc.wait(timeout=10)
    desktop.stop()
    settings = tmp_path / "config" / "altirra" / "settings.ini"
    text = settings.read_text()
    assert '"UI: Font family" = "Liberation Serif"' in text
    assert '"UI: Font weight" = "Bold Italic"' in text
    assert '"Console: Font family" = "JetBrains Mono"' in text
    with AltirraTestHarness(executable=desktop.executable) as restarted:
        fonts(restarted)
        # Force a save of the restored values, so a restart that silently
        # used defaults cannot pass by leaving the original file untouched.
        click(restarted, "", "Refresh font list")
        restarted.send("ui_screenshot " + str(tmp_path / "font-previews.png"))
        restarted.close_dialog("SystemConfig")
        search(restarted, "VBXE")
        restarted.send("ui_screenshot " + str(tmp_path / "search-serif.png"))
        assert "VideoBoard XE (VBXE)" in restarted.get_item_labels("Search actions")
        restarted.send("quit")
        restarted._proc.wait(timeout=10)
    text = settings.read_text()
    assert '"UI: Font family" = "Liberation Serif"' in text
    assert '"UI: Font weight" = "Bold Italic"' in text
    assert '"Console: Font family" = "JetBrains Mono"' in text


def test_live_font_size_changes_menus_and_search(desktop):
    fonts(desktop)
    before = desktop.find_item("", "Family##ui")["h"]
    slider = desktop.find_item("", "Size (pt)##ui")
    desktop.send(f"click_at {slider['x'] + slider['w'] * 0.7} {slider['y'] + slider['h'] / 2}")
    desktop.wait_frames(6)
    after = desktop.find_item("", "Family##ui")["h"]
    assert after > before * 1.2
    desktop.close_dialog("SystemConfig")
    search(desktop, "VBXE")
    result = desktop.find_item("Search actions", "VideoBoard XE (VBXE)")
    assert result["h"] > before * 3


def test_variable_named_styles_render_and_restart(desktop, tmp_path):
    installed("Adwaita Sans")
    if "FreeType" not in desktop.send("query_fonts")["loader"]:
        pytest.skip("Build uses the fallback rasterizer")
    fonts(desktop)
    choose_family(desktop, "ui", "Adwaita Sans")
    click(desktop, "", "Style##ui")
    click(desktop, "", "Light")
    light = desktop.send("query_fonts")
    click(desktop, "", "Style##ui")
    # Reopening scrolls to Light; Black is the first sorted style and may
    # sit above the visible rows in a catalog with many named instances.
    desktop.send("key home")
    desktop.wait_frames(3)
    click(desktop, "", "Black")
    black = desktop.send("query_fonts")
    assert light["uiFaceIndex"] >> 16 > 0
    assert black["uiFaceIndex"] >> 16 > 0
    assert light["uiFaceIndex"] != black["uiFaceIndex"]
    assert abs(light["sampleWidth"] - black["sampleWidth"]) > 1
    desktop.send("ui_screenshot " + str(tmp_path / "variable-black.png"))
    desktop.send("quit")
    desktop._proc.wait(timeout=10)
    desktop.stop()
    with AltirraTestHarness(executable=desktop.executable) as restarted:
        restarted.wait_frames(5)
        restored = restarted.send("query_fonts")
        assert restored["uiFaceIndex"] == black["uiFaceIndex"]
        assert abs(restored["sampleWidth"] - black["sampleWidth"]) < 0.01


@pytest.mark.parametrize("width", [640, 1280])
def test_large_font_menus_wrap_and_help_search(request, monkeypatch, tmp_path, width):
    if sys.platform == "darwin":
        pytest.skip("macOS uses its native menu bar")
    directory = tmp_path / "config" / "altirra"
    directory.mkdir(parents=True)
    (directory / "settings.ini").write_text(
        '[User\\AltirraSDL\\Settings]\n"UI: Font point size tenths" = 320\n')
    monkeypatch.setenv("XDG_CONFIG_HOME", str(directory.parent))
    with AltirraTestHarness(executable=request.config.getoption("--emu-path")) as emu:
        emu.close_dialog("SetupWizard")
        emu.send(f"set_window_size {width} 900")
        emu.wait_frames(6)
        menus = [i for i in emu.list_items("##MainMenuBar") if i["type"] == "treenode"]
        assert len(menus) == 11
        assert len({i["y"] for i in menus}) > 1
        assert all(i["x"] >= 0 and i["x"] + i["w"] <= width for i in menus)
        click(emu, "##MainMenuBar", "Help")
        click(emu, "", "Search actions and settings...")
        assert emu.get_dialog_state("GlobalSearch")
        emu.send("ui_screenshot " + str(tmp_path / f"wrapped-menu-{width}.png"))


@pytest.mark.parametrize("size", [8, 18, 32])
@pytest.mark.parametrize("family,style", [("Roboto", "Medium"),
                                         ("Liberation Serif", "Bold Italic"),
                                         ("JetBrains Mono", "Regular")])
def test_search_at_font_sizes(request, monkeypatch, tmp_path, size, family, style):
    if family != "Roboto":
        installed(family)
    config_dir = tmp_path / "config" / "altirra"
    config_dir.mkdir(parents=True)
    (config_dir / "settings.ini").write_text(
        '[User\\AltirraSDL\\Settings]\n'
        f'"UI: Font family" = "{family}"\n'
        f'"UI: Font weight" = "{style}"\n'
        f'"UI: Font point size tenths" = {size * 10}\n')
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "config"))
    with AltirraTestHarness(executable=request.config.getoption("--emu-path")) as emu:
        emu.close_dialog("SetupWizard")
        emu.send("set_window_size 1280 900")
        emu.wait_frames(5)
        search(emu, "VBXE")
        result = next(i for i in emu.list_items("Search actions")
                      if i["label"] == "VideoBoard XE (VBXE)")
        assert result["x"] >= 0 and result["y"] >= 0
        assert result["x"] + result["w"] <= 1280
        assert result["y"] + result["h"] <= 900
        emu.send("ui_screenshot " + str(tmp_path / f"search-{size}pt.png"))
        click(emu, search_input_window(), "##search")
        emu.send("key enter")
        emu.wait_frames(5)
        assert emu.get_dialog_state("SystemConfig")


@pytest.mark.parametrize("use_fontconfig", [True, False])
def test_collection_refresh_and_unicode_restart(request, monkeypatch, tmp_path, use_fontconfig):
    ttlib = pytest.importorskip("fontTools.ttLib")
    installed("Liberation Serif")
    directory = tmp_path / "data" / "fonts"
    directory.mkdir(parents=True)
    config_dir = tmp_path / "config" / "altirra"
    config_dir.mkdir(parents=True)
    fontconfig = tmp_path / "fonts.conf"
    fontconfig.write_text(
        '<?xml version="1.0"?><!DOCTYPE fontconfig SYSTEM "fonts.dtd">'
        f'<fontconfig><dir>{escape(str(directory))}</dir>'
        f'<cachedir>{escape(str(tmp_path / "cache"))}</cachedir></fontconfig>')
    monkeypatch.setenv("FONTCONFIG_FILE", str(fontconfig))
    monkeypatch.setenv("XDG_DATA_HOME", str(directory.parent))
    if not use_fontconfig:
        if not sys.platform.startswith("linux") or not shutil.which("cc"):
            pytest.skip("Directory fallback injection requires Linux and a C compiler")
        block_font_library(monkeypatch, tmp_path, "libfontconfig.so")
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "config"))
    family = "Žluťoučký Review Serif"
    executable = request.config.getoption("--emu-path")
    with AltirraTestHarness(executable=executable) as emu:
        emu.close_dialog("SetupWizard")
        fonts(emu)
        click(emu, "", "Family##ui")
        enter(emu, "", "##familyFilter", "Review Serif")
        assert family not in emu.get_item_labels()
        emu.send("key escape")
        emu.wait_frames(3)
        fonts(emu)
        collection = ttlib.TTCollection()
        collection.fonts = []
        for style in ("Regular", "Bold Italic"):
            source = subprocess.check_output(
                ["fc-match", "-f", "%{file}", f"Liberation Serif:style={style}"],
                # Resolve the source in the system catalog, not our empty catalog.
                env={"PATH": str(Path(shutil.which("fc-match")).parent)}, text=True)
            font = ttlib.TTFont(source)
            font["name"].names = [n for n in font["name"].names if n.platformID == 3]
            for name_id, value in ((1, family), (16, family), (2, style),
                                   (17, style), (4, family + " " + style)):
                font["name"].setName(value, name_id, 3, 1, 0x409)
            collection.fonts.append(font)
        collection.save(directory / "review.ttc")
        click(emu, "", "Refresh font list")
        choose_family(emu, "ui", family)
        click(emu, "", "Style##ui")
        assert "Bold Italic" in emu.get_item_labels()
        click(emu, "", "Bold Italic")  # second collection face, FontNo=1
        assert emu.send("query_fonts")["uiFaceIndex"] == 1
        emu.send("ui_screenshot " + str(tmp_path / "collection-font.png"))
        emu.send("quit")
        emu._proc.wait(timeout=10)
    with AltirraTestHarness(executable=executable) as emu:
        emu.close_dialog("SetupWizard")
        fonts(emu)
        click(emu, "", "Refresh font list")
        emu.close_dialog("SystemConfig")
        search(emu, "VBXE")
        assert "VideoBoard XE (VBXE)" in emu.get_item_labels("Search actions")
        emu.send("ui_screenshot " + str(tmp_path / "collection-restarted.png"))
        emu.send("quit")
        emu._proc.wait(timeout=10)
    text = (config_dir / "settings.ini").read_text()
    saved_family = next(line.split(" = ", 1)[1] for line in text.splitlines()
                        if line.startswith('"UI: Font family" = '))
    # The portable INI writer serializes non-ASCII characters as \uXXXX.
    assert json.loads(saved_family) == family
    assert '"UI: Font weight" = "Bold Italic"' in text



def block_font_library(monkeypatch, tmp_path, blocked):
    source = tmp_path / "no-fontconfig.c"
    source.write_text('''#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
void *dlopen(const char *path, int flags) {
    if (path && strstr(path, "{blocked}")) {
        fprintf(stderr, "Font review: blocked optional font load\\n");
        return NULL;
    }
    void *(*real_open)(const char *, int) = dlsym(RTLD_NEXT, "dlopen");
    return real_open(path, flags);
}
'''.replace('{blocked}', blocked))
    library = tmp_path / "no-fontconfig.so"
    subprocess.run(["cc", "-shared", "-fPIC", str(source), "-ldl", "-o", str(library)], check=True)
    monkeypatch.setenv("LD_PRELOAD", str(library))

@pytest.mark.parametrize("blocked", ["libfontconfig.so", "libfreetype.so"])
def test_without_optional_font_runtime(request, monkeypatch, tmp_path, blocked):
    if not sys.platform.startswith("linux") or not shutil.which("cc"):
        pytest.skip("Linux dynamic-loader failure injection requires a C compiler")
    block_font_library(monkeypatch, tmp_path, blocked)
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "config"))
    with AltirraTestHarness(executable=request.config.getoption("--emu-path")) as emu:
        emu.close_dialog("SetupWizard")
        if blocked == "libfreetype.so":
            assert "stb" in emu.send("query_fonts")["loader"].lower()
        fonts(emu)
        choose_family(emu, "ui", "Roboto")
        choose_family(emu, "mono", "Fira Mono")
        # Native metadata and italic names also work through directory scans.
        if Path("/usr/share/fonts/liberation-serif-fonts").is_dir():
            choose_family(emu, "ui", "Liberation Serif")
            click(emu, "", "Style##ui")
            click(emu, "", "Bold Italic")
        click(emu, "", "Refresh font list")
        emu.close_dialog("SystemConfig")
        search(emu, "VBXE")
        assert "VideoBoard XE (VBXE)" in emu.get_item_labels("Search actions")
        emu.send("ui_screenshot " + str(tmp_path / "without-fontconfig.png"))
        emu.send("quit")
        emu._proc.wait(timeout=10)
        assert "blocked optional font load" in emu._proc.stderr.read().decode()
