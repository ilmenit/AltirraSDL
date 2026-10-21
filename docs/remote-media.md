# URL media and Game Library imports

There are two workflows: open/boot a single image now, or add a game/ZIP pack
to Game Library for later play. They have different download limits and
cancellation behavior. The in-app guide is under **Help → Contents → AltirraSDL
additions → URL media and game packs**.

## Open or boot a single image

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

## Add games or large ZIP packs to Game Library

Open **Add from URL** in Game Library, paste a direct game-file or ZIP-pack
URL, and choose **Import**. Before importing, expand **Download location**
and use **Choose Folder...** to select a writable drive with enough free
space. The default is `library-downloads/` below the configuration directory;
your destination choice persists.

ZIP packs stream to disk and can be multiple gigabytes. The 64 MiB limit of
the single-image opener does not apply to the whole pack. The importer looks
for supported game members of 64 MiB or less, retains the complete archive,
adds it as a library source and starts a library scan. It does not boot a
game or change mounted media. Pick a game from the library afterward.

The progress display shows received bytes and, when supplied by the server,
a total-size progress bar. **Hide** or closing this import dialog keeps the
transfer running; reopen **Add from URL** to check progress. **Cancel Import**
explicitly cancels it and removes incomplete staging data. This differs from
the single-image URL opener, whose close action cancels its download.

Archive members are not extracted as host paths. Content-derived storage
names prevent one import from overwriting another. Removing a library source
keeps the downloaded archive/file; moving or deleting it breaks that source
until updated. Downloads are not automatically evicted.

## Troubleshooting

- Use a direct file link. HTML landing pages, repository pages and login pages
  are rejected; copying a web page's address is usually insufficient.
- Use Game Library's importer for a large ZIP collection rather than raising
  the single-image boot limit. A ZIP needs at least one supported game of
  64 MiB or less to pass the importer check.
- For disk-full or permission errors, select a writable destination with room
  for the complete pack. The importer does not expand every member onto disk.
- For HTTPS failures, check the system clock, certificate store, network and
  proxy configuration. Certificate verification stays enabled.
- Linux/macOS require the host libcurl library. Windows uses WinHTTP; Android
  uses Java HTTP connections. Browser builds retain their browser workflow.

### Large-pack verification

The native importer was exercised with a real 4,294,971,748-byte HTTP
transfer containing a ZIP64 entry beyond the 4 GiB boundary. The retained
archive was indexed and its game opened through the normal Altirra ZIP VFS.
The measured peak resident memory for the test was about 24 MiB. No full
archive extraction was performed. The separate ZIP64 regression also
covers 65,536 entries and malformed indexes.

The large transfer is opt-in so routine CI does not write several gigabytes:

```bash
mkdir -p build/_scratch
ALTIRRA_LIBRARY_TEST_TMP="$PWD/build/_scratch" PYTHONDONTWRITEBYTECODE=1 \
  python3 src/AltirraSDL/source/media/tests/library_url_http_test.py \
  build/linux-release/src/AltirraSDL/source/media/tests/altirra-library-url-test \
  --large
```

This measurement was made on Linux. Android ARMv7 and ARM64 use explicit
64-bit POSIX file offsets; their build commands and 64-bit seek symbols
were checked with the NDK, without claiming an Android runtime transfer.
