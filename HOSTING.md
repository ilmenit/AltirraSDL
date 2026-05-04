# Self-hosting AltirraSDL on the web

The AltirraSDL build's WASM target ships as four static files plus an
optional `config.json`.  Any web host that can serve static files over
HTTPS — GitHub Pages, Cloudflare Pages, Netlify, Nginx, Caddy, plain
S3 + CloudFront — can host AltirraSDL.  No server-side code is
required for the emulator itself; only the netplay lobby needs a
backend, and that's already deployed publicly (you can point at it
from your own page without running any server).

## 1. Build artifacts

After running the WASM build (see `BUILD.md` for prerequisites + build
commands), the four files needed to host AltirraSDL are produced in
`build-wasm/src/AltirraSDL/`:

| File             | Notes                                          |
|------------------|------------------------------------------------|
| `AltirraSDL.html`| Page shell (Files… modal, drag-drop, wizard).  |
| `AltirraSDL.js`  | Emscripten loader, ~80 KB.                     |
| `AltirraSDL.wasm`| Compiled emulator + UI, ~7 MB.                 |
| `AltirraSDL.data`| Bundled assets (Atari font, vendored ImGui ini).|

Copy all four to the same directory on your web host.  Browsers must
serve the page over HTTPS — IDBFS persistence relies on `Storage`
quota that's only granted in secure contexts.

## 2. Optional: `config.json`

Drop a `config.json` next to `AltirraSDL.html` to customise the
first-run experience.  Every field is optional.

```json
{
  "firmwareUrl": "https://example.com/firmware/xf25.zip",
  "lobbyHost":   "altirra-lobby.duckdns.org",
  "gamePacks": [
    {
      "name":        "Games",
      "url":         "https://a8.fandal.cz/files/a8_fandal_cz_february_2026.zip",
      "description": "Fandal A8 archive — February 2026 snapshot"
    },
    {
      "name":        "Demos",
      "url":         "https://example.com/altirra-demos.zip",
      "description": "Selected demoscene productions"
    },
    {
      "name":        "Utilities",
      "url":         "https://example.com/altirra-utils.zip",
      "description": "Disk utilities, monitors, language disks"
    }
  ]
}
```

### Schema

- **`firmwareUrl`** *(string, optional)* — surfaced to the user in the
  wizard.  The emulator's mirror list (xf25.zip on atariarea + ibiblio
  + a couple of FreeDOS archives) is still the canonical source for
  the standard ROM bundle, and works for almost everyone.  Override
  this only if you're running on an air-gapped network where the
  default mirrors aren't reachable; in that case, host your own copy
  of `xf25.zip` (or build a minimal zip containing only `.rom`
  entries) and point at it.

- **`lobbyHost`** *(string, optional, default `altirra-lobby.duckdns.org`)*
  — the netplay lobby host.  Used by the *🩺 Diagnose lobby* button to
  probe `/healthz` and `/v1/stats`, and by the WASM netplay transport
  for its WSS connection.  Leave at the default to use the public
  Altirra netplay lobby; override only if you've stood up your own
  lobby (see `server/lobby/README.md`).

- **`gamePacks`** *(array of objects, optional)* — pre-populated
  starter library.  Each pack:
  - **`name`** *(string)* — short label shown in the wizard.  Also
    used to compute the install directory: characters outside
    `[A-Za-z0-9_-]` are stripped and the pack lands in
    `/home/web_user/games/<sanitised-name>/`.
  - **`url`** *(string)* — direct URL to a `.zip` of game files (any
    mix of `.xex`, `.com`, `.exe`, `.atr`, `.atx`, `.car`, `.a52`,
    `.cas`, `.rom`, etc.).  See "CORS" below.
  - **`description`** *(string, optional)* — secondary text on the
    pack's wizard row.  Falls back to `url` if omitted.

The wizard shows every pack as an opt-in checkbox.  By default a
pack is checked when its install directory is empty, and unchecked
when it already has files (the user opened the wizard for some
other reason and probably doesn't want to re-install).  The user can
override either way before clicking *Download*.

### Pack lifecycle

When a user installs a pack:

1. The browser fetches the URL (streaming, with progress).
2. The zip is staged into `/tmp/altirra-pack-<name>.zip` (MEMFS).
3. `ATWasmUnpackArchive` unpacks the entries into
   `/home/web_user/games/<name>/` (IDBFS, persistent across reloads).
4. `ATWasmRegisterGamePackSource` adds that directory to the Game
   Library's source list and triggers a rescan.
5. The staged zip is deleted, IDBFS is flushed, the wizard closes.

After install, every game in the pack is just a normal file in the
user's VFS.  Users can:

- Browse the pack from *📁 Files… → Games → \<pack name\>* — same
  tree-mode UI as their own uploads.
- Boot games directly from the file manager or Gaming Mode.
- Delete individual games or wipe the whole pack via the file
  manager's selection mode.
- Add or remove the source from the desktop UI's *Library →
  Sources* page (if/when re-exposed in the mobile UI).

## 3. CORS

Browser security forbids `fetch()` from reading a response unless the
remote host sets `Access-Control-Allow-Origin` for your origin.  Most
public Atari archives (fandal.cz, atariarea, ibiblio) do **not** ship
permissive CORS headers, so a direct cross-origin fetch will fail with
a `TypeError: Failed to fetch` — the request leaves the browser, the
remote responds, and the browser refuses to expose the bytes.

Three workarounds that all work without code changes:

1. **Mirror the zip on your own host** (recommended).  Copy the file
   to your origin alongside `AltirraSDL.html`.  Same-origin fetches
   are exempt from CORS, and you control both ends.

2. **Reverse-proxy through your own server** (Nginx, Caddy, etc.):

   ```caddy
   # Caddyfile snippet
   handle_path /game-mirror/* {
       reverse_proxy https://a8.fandal.cz {
           header_up Host {upstream_hostport}
       }
       header Access-Control-Allow-Origin "*"
   }
   ```

   Then in `config.json`:
   ```json
   "url": "https://your-host.example/game-mirror/files/a8_fandal_cz_february_2026.zip"
   ```

3. **Find a CORS-permissive mirror.**  Some archives (ifarchive.org,
   GitHub Releases, jsDelivr-cached GitHub repos) do return
   `Access-Control-Allow-Origin: *`.  Test with
   `curl -I -H "Origin: https://your-host.example" <url>` and look for
   `access-control-allow-origin` in the response headers.

Browser DevTools → Network panel → click the failed request → look at
the *Response Headers* / *Console* tab for the exact CORS error.  If
the request says `(failed) net::ERR_FAILED` with no response body,
that's typically a CORS preflight failure.

## 4. Netplay lobby

The WASM build of AltirraSDL talks to the public netplay lobby at
`altirra-lobby.duckdns.org` over HTTPS for session listing
(`/v1/sessions`) and over WSS for the relay (`/netplay`).  No
configuration is needed for the standard hosting case — the public
lobby accepts anyone.

If your users see *"could not reach lobby (DNS / TLS / CORS / mixed-
content)"* errors, the *🩺 Diagnose lobby* button in the top bar
issues the same probes a debugger would and reports the result in
both a toast and the log panel.  See `server/lobby/README.md` for
self-hosting your own lobby.

## 5. Caching + cache-busting

The page shell loads `AltirraSDL.js?v=<timestamp>` so the loader is
always fresh, but the `.wasm` and `.data` are fetched by the
Emscripten loader without a busting query.  When you ship a new
build, either:

- Set `Cache-Control: no-cache` on `AltirraSDL.{js,wasm,data}` so
  browsers revalidate via ETag, or
- Bump the loader's cache-bust comment and force users to
  hard-reload, or
- Append your own version to the filenames (e.g. `AltirraSDL-v42.wasm`)
  and edit the loader script to match.

## 6. Minimum hosting checklist

1. Serve the four build artifacts over HTTPS (Storage quota / IDBFS
   require a secure context).
2. Set MIME types correctly: `application/wasm` for `.wasm` and
   `application/octet-stream` for `.data` are the most important.
   GitHub Pages / Cloudflare Pages / Netlify do this automatically.
3. (Optional) drop `config.json` next to `AltirraSDL.html` if you
   want to customise firmware URL, lobby host, or starter packs.
4. (Optional) if you ship game packs from a different origin, ensure
   the remote sends `Access-Control-Allow-Origin` for your origin —
   otherwise mirror the zip locally.
5. Test by loading the page in an Incognito window: the IDBFS state
   is per-origin, so an Incognito session simulates a fresh install
   exactly.
