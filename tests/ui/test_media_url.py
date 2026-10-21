"""Native URL downloads must feed the real image loader on the main thread."""
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

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_GET(self):
            if self.path == '/missing':
                self.send_error(404)
                return
            content = zipped.getvalue() if self.path == '/disk.zip' else body
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
