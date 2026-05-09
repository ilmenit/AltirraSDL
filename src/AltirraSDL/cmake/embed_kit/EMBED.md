# AltirraSDL embed reference

Complete reference for self-hosting the AltirraSDL WebAssembly build
with a preconfigured game.  See `README.md` for the three-step
quickstart; this document is the long-form contract.

The embed kit ships **as part of the WASM build artifact** — every
`AltirraSDL-<version>-wasm.zip` includes both the runtime files
(`index.html`, `AltirraSDL.js`, `AltirraSDL.wasm`,
`wasm_lib_deeplink.js`, `config.json`) and this `embed/` directory
(this `README.md`, `EMBED.md`, `example.html`).  Pinning the embed
kit to the same zip guarantees that the JS↔C ABI of the deep-link
shim matches the wasm exports — a separately distributed kit could
silently rot when the wasm bumps a function signature.

## Test locally before deploying

You **cannot** open `index.html` from `file://` — modern browsers
refuse to instantiate `.wasm` modules from the filesystem (wrong MIME
type) and IndexedDB persistence is disabled on `file://` origins.
Run a tiny static server instead.  All commands assume you're inside
the unzipped bundle directory:

**Python (always present on Linux / macOS, also on most Windows
boxes via the Microsoft Store install):**
```sh
python3 -m http.server 8000
# then visit http://localhost:8000/?embed=1&lib=mygame.atr
```

**Node.js:**
```sh
npx serve -l 8000
# or: npx http-server -p 8000
```

**Caddy / Nginx (one-liner with `caddy file-server`):**
```sh
caddy file-server --root . --listen :8000
```

The included `embed/example.html` runs at
`http://localhost:8000/embed/example.html` once the server is up;
its iframe loads `../index.html?embed=1&lib=yourgame.atr`, so drop a
file at `library/yourgame.atr` first and the demo boots straight
into it.

A 404 in DevTools' Network tab is the most common "I started the
server but nothing happens" symptom — usually a typo in the
`library/` filename or a missing file.

## URL parameter reference

All parameters are read by `wasm_lib_deeplink.js` on page load.  Every
field is optional; defaults match the lobby's "Start Atari Emulator"
behaviour.

### Display + chrome

| Parameter   | Value      | Effect |
|-------------|------------|--------|
| `embed`     | `1`        | Hide all page chrome (header bar, log, drop overlay, file manager, wizards). Canvas fills the viewport. Suppresses the first-run firmware-bundle download — the emulator falls back to its built-in LLE kernel if no firmware is supplied.  Do **not** set this for the lobby flow. |
| `title`     | string     | Sets the browser tab name (`document.title`) for an embed page that the visitor lands on directly (rather than via an iframe).  Truncated to 64 chars and stripped of control chars.  No effect when the page is hosted in an iframe — the parent page's `<title>` wins for the actual tab. |

### Hardware

| Parameter       | Allowed values                                                                  |
|-----------------|---------------------------------------------------------------------------------|
| `hardware`      | `800`, `800xl`, `1200xl`, `130xe`, `xegs`, `1400xl`, `5200`                     |
| `memsize`       | `8K`, `16K`, `24K`, `32K`, `40K`, `48K`, `52K`, `64K`, `128K`, `256K`, `320K`, `320KCOMPY`, `576K`, `576KCOMPY`, `1088K` |
| `pal`           | `1` — force PAL video standard (50 Hz)                                           |
| `ntsc`          | `1` — force NTSC video standard (60 Hz)                                          |
| `basic`         | `1` — boot with Atari BASIC enabled                                              |

If neither `pal` nor `ntsc` is set, the emulator picks based on the
saved settings (default NTSC).  PAL is the right choice for most
European-authored games.

### Game files (`library/`)

| Parameter | Value | Effect |
|-----------|-------|--------|
| `lib` | `path1,path2,…` | Comma-separated list of game files, relative to the library base.  Each file is fetched once, cached in IndexedDB, and mounted in the emulator according to its extension. |

Extension routing:

| Extensions                              | Boot mode      |
|-----------------------------------------|----------------|
| `.atr`, `.xfd`, `.atx`, `.pro`, `.dcm`  | Disk (D1: D2: …) — multiple disks are slotted in URL order |
| `.xex`, `.com`, `.exe`                  | Run (binary loader) |
| `.car`, `.a52`, `.rom`, `.bin`          | Cartridge      |
| `.cas`, `.wav`                          | Cassette       |

Unknown extensions are skipped with a warning in the JS log.  Up to
**16** entries are processed; the rest are ignored.

The library base resolves in this order:

1. `<meta name="altirra-library-base" content="/some/path/">` in the
   embed page (wins, gives total flexibility).
2. With `?embed=1`: page-relative `library/` (so dropping the bundle
   anywhere works without configuration).
3. Otherwise: absolute `/AltirraSDL/library/` (lobby-compatible
   default).

### Firmware ROMs (`firmware/`)

| Parameter   | Value           | Effect |
|-------------|-----------------|--------|
| `firmware`  | `path1,path2,…` | Comma-separated list of ROM files (`.rom`/`.bin`) relative to the firmware base.  Each is fetched into the emulator's firmware directory and registered automatically — recognised ROMs (OS-A, OS-B, XL/XE, XEGS, 1200XL, 5200, BASIC, …) are matched by **content hash** and assigned as the type-default kernel/BASIC.  Up to 8 entries. |

When **not** to use this:

- For stock Atari behaviour with no special ROM, ship nothing.  The
  emulator's built-in **LLE kernel** boots without any installed
  firmware — it is a clean-room reverse-engineered replacement that is
  GPL-compatible and free to redistribute.
- If you need a specific stock kernel (OS-A vs OS-B, XL/XE), drop the
  ROM into your `firmware/` directory and pass `?firmware=` so the
  emulator picks it up by hash.  The standard Atari OS ROMs are
  copyrighted; only redistribute ROMs you have the legal right to
  ship.

The firmware base resolves the same way as the library base, with
`<meta name="altirra-firmware-base">` and the `?embed=1` page-relative
default.

### Pinning a kernel by name

If you need the emulator to choose a specific kernel **type** (rather
than letting the firmware scanner auto-pick by hash), add a `kernel=`
URL parameter.  This is a convenience pass-through to the existing
`--kernel` CLI option:

| `kernel=` | Maps to                                  |
|-----------|------------------------------------------|
| `default` | auto (whatever the hardware mode wants)  |
| `osa`     | OS-A kernel                              |
| `osb`     | OS-B kernel                              |
| `xl`      | XL/XE kernel                             |
| `xegs`    | XEGS kernel                              |
| `1200xl`  | 1200XL kernel                            |
| `5200`    | 5200 kernel                              |
| `lle`     | Built-in LLE kernel (no firmware needed) |
| `llexl`   | Built-in LLE-XL kernel                   |
| `5200lle` | Built-in 5200 LLE kernel                 |

Examples:
```
?embed=1&lib=demo.atr&kernel=lle
?embed=1&lib=mygame.xex&firmware=osb.rom&kernel=osb
```

### Lobby-only parameters (do not use in embeds)

These exist for the netplay lobby and are not relevant for a
self-hosted single-player embed.  They are documented here only so
you know what to avoid:

- `host=1` — auto-host a netplay session
- `s=<id>`, `code=<…>` — join an existing netplay session by invite
- `broker=1`, `session=`, `token=`, `intent=`, `handle=`,
  `join_handle=`, `role=` — broker-mode handshake for the lobby

If you pass any of these alongside `?embed=1` the emulator will try
to publish to or join a netplay session, which on a self-hosted page
will fail because there is no netplay broker on your origin.  Pass
**only** `embed`, `lib`, `firmware`, `hardware`, `memsize`, `pal` /
`ntsc`, `basic`, `kernel`, and `title`.

## Custom deploy paths

By default the embed expects this layout next to `index.html`:

```
your-deploy-root/
  index.html
  AltirraSDL.js
  AltirraSDL.wasm
  wasm_lib_deeplink.js
  config.json
  library/
    yourgame.atr
  firmware/             (optional)
    custom-os.rom
```

If your CMS or framework forces a different tree (for example you
serve all static media from `/assets/atari/`), add `<meta>` tags to
the document the iframe loads:

```html
<meta name="altirra-library-base"  content="/assets/atari/games/">
<meta name="altirra-firmware-base" content="/assets/atari/roms/">
```

The deep-link script reads these on `parseUrl` and uses them as the
fetch base for `?lib=` and `?firmware=` paths.  Both can have a
trailing slash or not — the script normalises.

This works equally well with absolute URLs **on the same origin**
(`/some/path/`).  Cross-origin paths (`https://other.example/...`) are
intentionally rejected: the path validator in `wasm_lib_deeplink.js`
strips any input that contains `://`.  Authoring an embed that pulled
ROMs from third parties would expose every visitor's IndexedDB to
whatever that third party wanted to drop in.

## Iframe vs. direct page

Both work.  Pick based on whether your page already has its own
visual identity:

**Direct page** — the visitor lands on the AltirraSDL page itself.
Smallest footprint, best for "click to play" links from another
site.

```
https://your-site/altirra/?embed=1&lib=mygame.atr
```

**Iframe** — the embed sits inside your existing page.  Best when
the surrounding page has the title, controls, screenshots, copy.
Add the `allow="autoplay; fullscreen; gamepad"` attribute so
keyboard input, fullscreen toggle, and USB controllers all work
inside the frame.  The `aspect-ratio: 4 / 3` style keeps the
frame's height proportional to its width when the page is resized.

```html
<iframe
  src="/altirra/?embed=1&lib=mygame.atr&hardware=800xl"
  width="800" height="600"
  style="border:0; aspect-ratio: 4 / 3; max-width: 100%;"
  allow="autoplay; fullscreen; gamepad"
  loading="lazy"
  title="Play MyGame">
</iframe>
```

For mobile, drop `width`/`height` and let CSS sizing do the work
(`max-width: 100%; aspect-ratio: 4 / 3;` is enough).

## Security model — what to expect

The embed inherits the **same-origin** model the lobby already uses:

- All `?lib=` / `?firmware=` paths are fetched **from the embed's
  origin**.  Cross-origin paths and `..` traversals are rejected.
  An attacker can't craft a malicious URL that points the embedded
  emulator at a third-party server.
- IndexedDB persistence is **per-origin**.  An embed at
  `https://site-a/` and another at `https://site-b/` see independent
  saves, save-states, and uploads.  An iframe-embed shares its
  parent's storage only if both pages live on the same origin.
- The emulator does not call out to any third party in embed mode.
  The lobby's "fetch the standard ROM bundle" first-run path is
  explicitly suppressed when `?embed=1` is set, so authors don't
  accidentally take a runtime dependency on `lobby.atari.org.pl`.
- Audio autoplay still requires a user gesture per browser policy.
  The first click / key press unmutes; this is a browser
  constraint, not a quirk of the emulator.

## Troubleshooting

**The canvas appears but the game never boots.**  Open DevTools'
console.  `[lib-deeplink]` lines log every parsed parameter and
every fetch.  Common causes:

- Wrong path: `library/mygame.atr` doesn't exist on the server.
  Look for a 404 in the Network tab.
- File extension routes to the wrong loader.  A plain `.bin` ends
  up as a cartridge — rename to `.xex` if it's a binary executable,
  or `.rom` if it's a kernel ROM.
- Hardware/memsize mismatch — a 130XE-only game with `?hardware=800`
  + `?memsize=48K` will hang at the title screen.

**The emulator boots but the screen stays black.**  Check the
`hardware` mode matches the title.  Most modern Atari demos
expect `800xl` + `64K`.

**Fullscreen toggle does nothing.**  An iframe needs
`allow="fullscreen"` on the `<iframe>` tag.  Without it, the
emulator's fullscreen button silently fails because the parent
document refused the request.

**Keyboard input lands on my page, not the emulator.**  Click the
canvas first to focus it.  In an iframe, the parent page also
needs to relinquish focus when the user clicks inside the frame
(most browsers do this automatically; if yours does not, add
`onclick="this.contentWindow.focus()"` to the `<iframe>`).

**Custom firmware ROM has no effect.**  The firmware scanner
identifies ROMs by content **hash**, not by filename.  If the file
isn't recognised you can still pin the kernel manually with
`?kernel=osb` (or whichever applies); otherwise check that the
ROM is a known stock dump and not a patched / rehashed copy.

## Updating game files on a live deploy

`wasm_lib_deeplink.js` validates each cached file against the
origin on every visit and refetches automatically when it has
changed:

1. **First visit** — the browser GETs the file and the loader
   stores both the bytes and the response's `ETag` header
   (sidecar file `<vfs path>.etag` in IDBFS).
2. **Return visit** — the loader issues a HEAD with
   `cache: 'no-cache'` so headers come fresh from the origin, then
   compares the server's `ETag` (preferred) or `Content-Length`
   against the on-disk state.  Match → keep the cached file.
   Mismatch → re-GET and overwrite.

In practice this means **just replacing the file on your server is
enough**: the next time a returning visitor loads the page their
copy gets refreshed.  No filename change, no manual cache flush,
no `?v=…` query trick required.

A few things worth knowing:

- HEAD failures (CORS, 405 Method Not Allowed, offline reload, …)
  fall back to "trust the cache" so an offline-capable PWA-style
  setup keeps booting from IDBFS.  If your server *blocks* HEAD on
  static files you'll never see updates — re-enable HEAD or set
  `Allow: GET, HEAD` on those paths.
- The HEAD adds one small request per file (a few hundred bytes of
  headers) on every page load.  For Play Solo / Play Together URLs
  with one or two files this is invisible; if you ever embed a
  page with dozens of `lib=` entries it adds up linearly.
- Profiles that pre-date the ETag sidecar (older AltirraSDL builds)
  fall back to a `Content-Length` comparison and opportunistically
  store the server's ETag on first match, so they upgrade to the
  fast path on the very next visit.
- Same-size content changes that *also* change the ETag still get
  detected.  Same-size content changes against a server that
  doesn't emit ETag *and* the visitor has no sidecar yet are the
  only case where an update is missed — extremely rare for binary
  game files.

**Filename-bumping (`mygame.atr` → `mygame-v2.atr`) still works**
and is the right choice when you want to roll out incrementally
without disrupting visitors who are mid-session — old bookmarked
links keep serving the old name.  But it is no longer required for
ordinary updates.

## Versioning

The embed contract (URL parameters, base resolution rules, body
class names) is stable across patch releases of AltirraSDL.  Each
release zip carries its own copy of `wasm_lib_deeplink.js`, so
upgrading the bundle on your server is a drop-in replacement: stop
serving the old zip, drop in the new one, and clear the IndexedDB
under your origin if a major version changed the persistence
schema.
