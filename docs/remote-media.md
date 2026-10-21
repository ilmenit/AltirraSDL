# Open emulator media from a URL

Native AltirraSDL builds can download Atari media over HTTP or HTTPS:

- Desktop: **File → Open Image from URL...**
- Gaming/mobile mode: **Load Game → From URL...**
- Command line: `AltirraSDL 'https://example.org/game.atr'`

Enter a direct download link, then choose **Open Image** to use the normal
Open Image workflow, or **Boot Image** to load, reset and run through the
normal Boot Image workflow. ZIP and gzip media use Altirra's existing image
loader. ZIP files remain archives: no member paths are extracted onto the
host filesystem, and companion files remain available through the VFS.
As with opening a local ZIP, the loader chooses the first supported image.

The download runs on a worker thread; emulator state changes only on the
main thread after it finishes. The dialog shows received bytes, and a
progress bar when the platform supplies a total length. **Cancel Download**
or closing the dialog cancels the pending operation. Download failures and
cancellation leave mounted media and running/paused state unchanged.

Files are limited to 64 MiB per download and per expanded archive member.
Archive preflight also limits cumulative expansion to 128 MiB, ZIP entries
to 4,096, and nested compression to four levels. HTTP errors, incomplete
responses, HTML pages, corrupt ZIP media and oversized archives are
reported in the dialog. The normal loader reports format/compatibility
errors and requests cartridge mapper selection as it does for local media.

Completed files are retained below the configuration directory's
`downloads/` folder, keyed by content SHA-256 and a filename hash. This
keeps recently opened media, profiles and disk working-copy paths usable
across restarts. A fresh download contacts the server again; files are
not reused solely because their URL matches. Existing cached files are
never replaced by a concurrent download. Cache files can be deleted when
they are no longer mounted or needed by recent-image entries; downloads
are not evicted automatically.

Native HTTP uses the same platform backend as the metadata scraper:
WinHTTP on Windows, Java HTTP connections on Android, and the host's
libcurl on Linux/macOS. System certificate verification remains enabled.
Linux/macOS installations without libcurl report an unavailable backend.
The browser build retains its existing JavaScript **From URL** workflow;
the new native picker is omitted there.

Regression coverage: `media-url-http` uses a loopback HTTP fixture and
`tests/ui/test_media_url.py` verifies the real deferred media loader via
desktop, command-line and gaming entry points, including cancellation and
failure preserving the mounted disk and paused state.
