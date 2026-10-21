"""Native URL downloads must feed the real image loader on the main thread."""
import gzip
import http.server
import io
import threading
import time
import zipfile

import pytest

from .harness import AltirraTestHarness
from .test_explorers import make_disk, click


@pytest.fixture
def image_server(tmp_path):
    image = tmp_path / 'download.atr'
    make_disk(image)
    body = image.read_bytes()
    zipped = io.BytesIO()
    with zipfile.ZipFile(zipped, 'w', zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('download.atr', body)

    malformed_zip = io.BytesIO()
    with zipfile.ZipFile(malformed_zip, 'w', zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('bad.atr', b'bad disk')
    malformed = {
        '/bad.atr': b'bad disk',
        '/bad.zip': malformed_zip.getvalue(),
        '/bad.atr.gz': gzip.compress(b'bad disk'),
    }

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_GET(self):
            if self.path == '/missing':
                self.send_error(404)
                return
            content = malformed.get(self.path,
                                    zipped.getvalue() if self.path == '/disk.zip' else body)
            self.send_response(200)
            self.send_header('Content-Length', str(len(content)))
            self.end_headers()
            try:
                if self.path == '/slow.atr':
                    for offset in range(0, len(content), 1024):
                        self.wfile.write(content[offset:offset + 1024])
                        self.wfile.flush()
                        time.sleep(.03)
                else:
                    self.wfile.write(content)
            except (BrokenPipeError, ConnectionResetError):
                pass

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    yield f'http://127.0.0.1:{server.server_port}'
    server.shutdown()
    server.server_close()


def wait_download(emu):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        status = emu.send('media_url_status')
        if not status['pending']:
            emu.wait_frames(5)
            return status
        time.sleep(.03)
    raise AssertionError('download did not finish')


@pytest.mark.parametrize('entry', ['open', 'boot', 'cli'])
def test_url_loads_disk(emu, monkeypatch, tmp_path, image_server, entry):
    monkeypatch.setenv('XDG_CONFIG_HOME', str(tmp_path / 'config'))
    monkeypatch.setenv('SDL_VIDEODRIVER', 'dummy')
    monkeypatch.setenv('SDL_AUDIODRIVER', 'dummy')
    monkeypatch.setenv('ALTIRRA_DISPLAY_BACKEND', 'sdlrenderer')
    args = ['--ui-mode', 'desktop']
    url = image_server + '/disk.zip'
    if entry == 'cli':
        args.append(url)
    with AltirraTestHarness(executable=emu.executable, extra_args=args) as fresh:
        fresh.close_dialog('SetupWizard')
        if entry != 'cli':
            fresh.send(f'open_url {entry} {url}')
        status = wait_download(fresh)
        assert status['error'] == ''
        disk = fresh.query_state()['state']['disks'][0]
        assert disk['sectors'] == 720
        assert disk['sectorSize'] == 128
        assert 'downloads' in disk['path']
        assert 'download.atr' in disk['path']


def test_url_failure_and_cancel_keep_current_disk(emu, monkeypatch, tmp_path, image_server):
    monkeypatch.setenv('XDG_CONFIG_HOME', str(tmp_path / 'config'))
    monkeypatch.setenv('SDL_VIDEODRIVER', 'dummy')
    monkeypatch.setenv('SDL_AUDIODRIVER', 'dummy')
    monkeypatch.setenv('ALTIRRA_DISPLAY_BACKEND', 'sdlrenderer')
    with AltirraTestHarness(executable=emu.executable,
                           extra_args=['--ui-mode', 'desktop']) as fresh:
        fresh.close_dialog('SetupWizard')
        fresh.send(f'open_url open {image_server}/download.atr')
        assert wait_download(fresh)['error'] == ''
        # This filesystem fixture has no boot program; dismiss its
        # expected execution error before exercising real Cancel clicks.
        fresh.pause()
        fresh.wait_frames(3)
        if fresh.query_window('Program Error').get('visible'):
            click(fresh, 'Program Error', 'Pause')
        before = fresh.query_state()['state']['disks'][0]
        fresh.send(f'open_url boot {image_server}/missing')
        assert '404' in wait_download(fresh)['error']
        assert fresh.query_state()['state']['disks'][0] == before
        assert not fresh.query_state()['state']['sim']['running']
        fresh.send(f'open_url boot {image_server}/slow.atr')
        fresh.wait_frames(3)
        click(fresh, 'Open Image from URL', 'Cancel Download')
        assert wait_download(fresh)['error'] == ''
        assert fresh.query_state()['state']['disks'][0] == before
        assert not fresh.query_state()['state']['sim']['running']


def test_gaming_file_browser_opens_url(emu, monkeypatch, tmp_path, image_server):
    monkeypatch.setenv('XDG_CONFIG_HOME', str(tmp_path / 'config'))
    monkeypatch.setenv('SDL_VIDEODRIVER', 'dummy')
    monkeypatch.setenv('SDL_AUDIODRIVER', 'dummy')
    monkeypatch.setenv('ALTIRRA_DISPLAY_BACKEND', 'sdlrenderer')
    with AltirraTestHarness(executable=emu.executable,
                           extra_args=['--ui-mode', 'gaming']) as fresh:
        fresh.close_dialog('SetupWizard')
        fresh.send('set_window_size 430 800')
        fresh.send('mobile_screen files')
        fresh.wait_frames(5)
        click(fresh, '##FileBrowser', 'From URL...')
        fresh.wait_frames(3)
        assert fresh.query_window('Open Image from URL')['visible']
        fresh.send(f'open_url boot {image_server}/disk.zip')
        assert wait_download(fresh)['error'] == ''
        assert fresh.query_state()['state']['disks'][0]['sectors'] == 720
        assert not fresh.query_window('##FileBrowser').get('visible', False)


@pytest.mark.parametrize('font_size', [12, 32])
def test_url_dialog_controls_remain_reachable_after_resize(emu, monkeypatch, tmp_path, font_size):
    monkeypatch.setenv('XDG_CONFIG_HOME', str(tmp_path / 'config'))
    config = tmp_path / 'config' / 'altirra'
    config.mkdir(parents=True)
    (config / 'settings.ini').write_text(
        '[User\\AltirraSDL\\Settings]\n"UI: Font point size tenths" = '
        + str(font_size * 10) + '\n')
    monkeypatch.setenv('SDL_VIDEODRIVER', 'dummy')
    monkeypatch.setenv('SDL_AUDIODRIVER', 'dummy')
    monkeypatch.setenv('ALTIRRA_DISPLAY_BACKEND', 'sdlrenderer')
    with AltirraTestHarness(executable=emu.executable,
                           extra_args=['--ui-mode', 'desktop']) as fresh:
        fresh.close_dialog('SetupWizard')
        fresh.send('set_window_size 1280 900')
        fresh.send('open_url open invalid-url')
        fresh.wait_frames(3)
        fresh.send('set_window_size 430 800')
        fresh.wait_frames(5)
        items = fresh.list_items('Open Image from URL')
        for label in ('Open Image', 'Boot Image', 'Close'):
            control = next(item for item in items if item['label'] == label)
            assert control['x'] >= 0
            assert control['x'] + control['w'] <= 430
            assert control['y'] >= 0
            assert control['y'] + control['h'] <= 800
        click(fresh, 'Open Image from URL', 'Close')
        fresh.wait_frames(3)
        assert not fresh.query_window('Open Image from URL').get('visible', False)


@pytest.mark.parametrize('filename', ['bad.atr', 'bad.zip', 'bad.atr.gz'])
def test_malformed_download_preserves_all_mounted_disks(
        emu, monkeypatch, tmp_path, image_server, filename):
    monkeypatch.setenv('XDG_CONFIG_HOME', str(tmp_path / 'config'))
    monkeypatch.setenv('SDL_VIDEODRIVER', 'dummy')
    monkeypatch.setenv('SDL_AUDIODRIVER', 'dummy')
    monkeypatch.setenv('ALTIRRA_DISPLAY_BACKEND', 'sdlrenderer')
    with AltirraTestHarness(executable=emu.executable,
                           extra_args=['--ui-mode', 'desktop']) as fresh:
        fresh.close_dialog('SetupWizard')
        fresh.pause()
        for index in range(2):
            path = tmp_path / f'keep D{index + 1}.atr'
            make_disk(path)
            fresh.send(f'attach_disk {index} {path}')
        fresh.wait_frames(5)
        before = fresh.query_state()['state']
        fresh.send(f'open_url boot {image_server}/{filename}')
        wait_download(fresh)
        assert fresh.query_window('Tool Result').get('visible'), 'Parser error was not shown'
        after = fresh.query_state()['state']
        assert after['disks'] == before['disks']
        assert not after['sim']['running']
