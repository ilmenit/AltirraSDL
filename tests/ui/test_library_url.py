"""Real streamed downloads, library scanning/persistence and contextual help."""
import gzip
import http.server
import io
from pathlib import Path
import threading
import time
import zipfile

import pytest

from .harness import AltirraTestHarness
from .test_explorers import make_disk, xex_bytes, click, enter

TITLE = 'Add to Game Library from URL'


@pytest.fixture
def library_server(tmp_path):
    disk = tmp_path / 'disk.atr'
    make_disk(disk)
    archive = io.BytesIO()
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as pack:
        pack.writestr('folder/Space Quest.atr', disk.read_bytes())
        pack.writestr('Puzzle.xex', xex_bytes())
        pack.writestr('notes.txt', b'A collection')
    body = archive.getvalue()

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_GET(self):
            if self.path == '/error.zip':
                self.send_error(404)
                return
            if self.path == '/slow.zip':
                self.send_response(200)
                self.send_header('Content-Length', str(1024 * 1024))
                self.end_headers()
                try:
                    for _ in range(1024):
                        self.wfile.write(b'PK' + b'\0' * 1022)
                        self.wfile.flush()
                        time.sleep(.01)
                except (BrokenPipeError, ConnectionResetError):
                    pass
                return
            content = (b'<html>not a pack</html>' if self.path == '/html.zip'
                       else gzip.compress(disk.read_bytes()) if self.path == '/single.atz'
                       else gzip.compress(xex_bytes()) if self.path == '/single.xex.gz'
                       else body)
            self.send_response(200)
            self.send_header('Content-Length', str(len(content)))
            self.end_headers()
            self.wfile.write(content)

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    yield f'http://127.0.0.1:{server.server_port}'
    server.shutdown()
    server.server_close()


@pytest.fixture
def library_desktop(emu, monkeypatch, tmp_path):
    monkeypatch.setenv('XDG_CONFIG_HOME', str(tmp_path / 'config'))
    monkeypatch.setenv('SDL_VIDEODRIVER', 'dummy')
    monkeypatch.setenv('SDL_AUDIODRIVER', 'dummy')
    monkeypatch.setenv('ALTIRRA_DISPLAY_BACKEND', 'sdlrenderer')
    with AltirraTestHarness(executable=emu.executable,
                           extra_args=['--ui-mode', 'desktop']) as fresh:
        fresh.close_dialog('SetupWizard')
        fresh.send('set_window_size 1280 900')
        fresh.pause()
        yield fresh


def wait_import(emu):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        status = emu.send('library_url status')
        if not status['pending'] and not status['scanning']:
            emu.wait_frames(4)
            return emu.send('library_url status')
        time.sleep(.03)
    raise AssertionError('library import did not complete')


def test_pack_import_scans_once_and_survives_restart(library_desktop, library_server, tmp_path):
    emu = library_desktop
    keep = tmp_path / 'mounted.atr'
    make_disk(keep)
    emu.send(f'attach_disk 0 {keep}')
    before = emu.query_state()['state']['disks']
    destination = tmp_path / 'custom download drive' / 'packs'
    emu.send(f'library_url destination {destination}')
    emu.open_dialog('GameLibrary')
    emu.wait_frames(5)
    click(emu, 'Game Library', 'Sources')
    click(emu, 'Game Library', 'Add from URL...')
    enter(emu, TITLE, '##Library URL', library_server + '/pack.zip')
    click(emu, TITLE, 'Import')
    result = wait_import(emu)
    assert not result['error'] and result['files'] == 2
    assert result['sources'] == 1 and result['games'] == 2
    assert result['added'] and Path(result['path']).is_file()
    assert destination in Path(result['path']).parents
    assert emu.query_state()['state']['disks'] == before
    assert not emu.query_state()['state']['sim']['running']
    emu.send(f'library_url import {library_server}/pack.zip')
    duplicate = wait_import(emu)
    assert not duplicate['added'] and duplicate['sources'] == 1
    assert duplicate['path'] == result['path'] and duplicate['games'] == 2
    emu.send('quit')
    emu._proc.wait(timeout=10)
    emu.stop()
    with AltirraTestHarness(executable=emu.executable,
                           extra_args=['--ui-mode', 'desktop']) as fresh:
        fresh.close_dialog('SetupWizard')
        fresh.open_dialog('GameLibrary')
        fresh.wait_frames(6)
        restored = fresh.send('library_url status')
        assert restored['sources'] == 1 and restored['games'] == 2
        assert restored['destination'] == str(destination)
        assert Path(result['path']).is_file()


def test_failed_or_cancelled_import_keeps_library(library_desktop, library_server, tmp_path):
    emu = library_desktop
    emu.send(f'library_url import {library_server}/pack.zip')
    before = wait_import(emu)
    for endpoint in ('error.zip', 'html.zip'):
        emu.send(f'library_url import {library_server}/{endpoint}')
        failed = wait_import(emu)
        assert failed['error']
        assert failed['sources'] == before['sources']
        assert failed['games'] == before['games']
    emu.send(f'library_url import {library_server}/slow.zip')
    deadline = time.monotonic() + 5
    while emu.send('library_url status')['received'] == 0 and time.monotonic() < deadline:
        time.sleep(.02)
    assert not emu.send_raw('library_url destination /cannot-change-active-import')['ok']
    click(emu, TITLE, 'Hide')
    assert not emu.send('library_url status')['open']
    assert emu.send('library_url status')['pending']
    emu.send('library_url show')
    emu.wait_frames(5)
    click(emu, TITLE, 'Cancel Import')
    cancelled = wait_import(emu)
    assert 'cancelled' in cancelled['error']
    assert cancelled['sources'] == before['sources']
    assert cancelled['games'] == before['games']
    assert not list(Path(cancelled['destination']).glob('.library-download-*'))
    assert not emu.query_state()['state']['sim']['running']


@pytest.mark.parametrize('mode', ['desktop', 'gaming'])
def test_url_import_and_help_fit_narrow_window(emu, monkeypatch, tmp_path, mode):
    monkeypatch.setenv('XDG_CONFIG_HOME', str(tmp_path / 'config'))
    monkeypatch.setenv('SDL_VIDEODRIVER', 'dummy')
    monkeypatch.setenv('SDL_AUDIODRIVER', 'dummy')
    monkeypatch.setenv('ALTIRRA_DISPLAY_BACKEND', 'sdlrenderer')
    with AltirraTestHarness(executable=emu.executable,
                           extra_args=['--ui-mode', mode]) as fresh:
        fresh.close_dialog('SetupWizard')
        fresh.send('set_window_size 430 800')
        fresh.send('library_url show')
        fresh.wait_frames(6)
        for label in ('Import', 'Close', 'Import help'):
            item = fresh.find_item(TITLE, label)
            assert item and 0 <= item['x'] < item['x'] + item['w'] <= 430
            assert 0 <= item['y'] < item['y'] + item['h'] <= 800
        click(fresh, TITLE, 'Import help')
        fresh.wait_frames(5)
        assert fresh.query_window('Help — Contents')['visible']
        assert fresh.send('help_status')['topic'] == 'sdl-remote-media.xml'
        help_title = 'Help — Contents'

        def help_child(name):
            windows = fresh.query_state()['state']['windows']
            return next((window for window in windows
                         if window['name'].startswith(help_title + '/')
                         and name in window['name']), None)

        # Reading a contextual topic must not squeeze it beside a fixed TOC.
        article = help_child('##content')
        help_window = fresh.query_window(help_title)
        assert article and article['w'] >= help_window['w'] - 32
        assert help_child('##toc') is None
        for label in ('Contents', '##find', 'Next'):
            item = fresh.find_item(help_title, label)
            assert item and 0 <= item['x'] < item['x'] + item['w'] <= 430
        click(fresh, help_title, 'Contents')
        toc = help_child('##toc')
        assert toc and toc['w'] >= help_window['w'] - 32
        assert help_child('##content') is None
        # Choosing a topic returns immediately to the full-width article.
        click(fresh, help_title, 'FAQ')
        fresh.wait_frames(5)
        assert fresh.send('help_status')['topic'] == 'faq.xml'
        assert help_child('##content') and help_child('##toc') is None
        fresh.send('ui_screenshot ' + str(tmp_path / f'library-url-{mode}.png'))


def test_fujinet_context_help_opens_offline_guide(library_desktop):
    emu = library_desktop
    emu.send('fujinet show')
    emu.wait_frames(5)
    click(emu, 'FujiNet', 'Help')
    emu.wait_frames(5)
    assert emu.query_window('Help — Contents')['visible']
    assert emu.send('help_status')['topic'] == 'sdl-fujinet.xml'
    emu.send('help_topic sdl-expansions.html')
    emu.wait_frames(5)
    assert emu.send('help_status')['topic'] == 'sdl-expansions.xml'


def test_compressed_game_files_are_scanned(library_desktop, library_server):
    emu = library_desktop
    for index, name in enumerate(['single.atz', 'single.xex.gz'], 1):
        emu.send(f'library_url import {library_server}/{name}')
        status = wait_import(emu)
        assert not status['error']
        assert status['files'] == 1 and status['sources'] == index
        assert status['games'] == index
