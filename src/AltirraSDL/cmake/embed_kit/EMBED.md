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
| `back_url`  | URL or path | Per-link override of the page-bar *⬅ Lobby* button's destination — useful for `?ui=desktop` links from your own catalog page when you want the back-link to return to your page rather than to the canonical lobby (or to be hidden entirely).  Accepts a same-origin path (`/games/`) or an absolute `http(s)://` URL.  An empty value (`?back_url=`) **forces the button hidden**, even if the bundle ships a `config.json` with `lobbyUrl`.  Wins over `config.json`.  No effect under `?embed=1` (the entire header is hidden in embed mode). |
| `back_label` | string    | Text shown on the *⬅ Lobby* button when `back_url` is in use.  Printed verbatim — supply your own arrow / glyph if you want one.  Clamped to 32 chars; control chars stripped.  Defaults to `⬅ Lobby` when omitted. |

### First-run defaults (Experience + Add-ons)

The emulator applies sensible defaults the very first time a visitor
loads your embed.  These two URL params let you control what those
defaults look like so demos requiring modern hardware "just work" or,
conversely, so a period-accurate originals showcase boots into a
stock 800 XL with NTSC artifact colors.

| Parameter | Allowed values | Effect |
|---|---|---|
| `experience` | `convenient` *(default)* / `authentic` | **Convenient**: artifact mode = None, SIO patches on (fast disk loading), drive sounds off, accurate disk timing off, SharpBilinear filter, sharpness +1.  **Authentic**: artifact mode = AutoHi (NTSC color smearing on hi-res text — a faithful look), SIO patches off, drive sounds on, accurate disk timing on, Bilinear filter. |
| `addons` | `on` *(default)* / `off`<br>(also accepts `1`/`0`, `true`/`false`, `enabled`/`disabled`, `yes`/`no`) | **On**: enable VBXE 1.26 + Covox $D600/4 ch + Stereo POKEY, set memory mode to 1088 K.  Required for most modern Atari demos.  **Off**: actively REMOVE those three devices (memory mode left alone — use `?memsize=` to set it explicitly).  Right for period-accurate originals. |

The two axes are **orthogonal** — you can pick any combination
(Convenient + on, Authentic + on, Convenient + off, Authentic + off).

These defaults fire **only on first run** (a fresh IndexedDB / no
saved registry).  Subsequent visits use whatever the user (or your
embed) configured, persisted in the browser's IndexedDB-backed
settings.  To change a configured deploy, the visitor uses
*Hamburger > Settings* in Gaming Mode or the Atari menu in Desktop
Mode.

The hardware switches in the Hardware table below ALWAYS override
the silent defaults — `?addons=on&memsize=128K` yields VBXE + Covox
+ Stereo POKEY at 128 K (CLI wins).  Same for `--hardware`,
`--stereo`, `--adddevice`, etc.

#### Common embed recipes

| Goal | URL params |
|---|---|
| Lobby-style "modern demo just works" *(default)* | none — implicit `experience=convenient&addons=on` |
| Period-accurate original Atari title | `?experience=authentic&addons=off` |
| Realistic look on a VBXE-required demo | `?experience=authentic&addons=on` |
| Convenient loading on a stock 130 XE | `?experience=convenient&addons=off&hardware=130xe` |
| Stock 800 XL with 64 K (replay capture / speedrun) | `?addons=off&hardware=800xl&memsize=64K&randmem=0` |

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

### Display look — CRT, filter, artifacting

The gaming-mode default (a deep-link with `?lib=` enters Gaming
Mode) applies the **Balanced** performance preset: bilinear filter,
vignette on, screen effects in Basic mode, color artifacting on
Auto.  This is a sensible default for arcade-style games on a
phone but it makes pixel-art text fuzzy and adds visible color
smearing on high-resolution Atari modes (the 80-column GR.0 text
GTIA puts up at the start of many `.xex` titles).

The four params below override the gaming-mode defaults so an
embed author can pick the look that fits their game.  Each one is
optional — leaving the param off keeps whatever the user's saved
preference / gaming-mode preset chose.

| Parameter   | Allowed values                                          | Effect |
|-------------|---------------------------------------------------------|--------|
| `crt`       | `0`, `1`                                                | `0` applies the **Efficient** preset (no scanlines/bloom/distortion/vignette, screen effects off, artifacting off, point filter).  `1` applies **Quality** (full CRT look — scanlines, bloom, distortion, vignette, sharp-bilinear filter, artifacting Auto).  Same routing as the page-bar CRT button. |
| `filter`    | `point`, `bilinear`, `sharp` (= `sharpbilinear`), `bicubic`, `auto` | Display upscale filter applied AFTER the `crt=` preset.  Lets you keep the CRT look (`crt=1`) but force `filter=point` for crisp text overlays, or vice versa. |
| `artifact`  | `none`, `ntsc`, `pal`, `ntschi`, `palhi`, `auto`, `autohi` | Color artifacting mode.  `none` is the right pick for any title that draws solid-color graphics or pixel-art text.  `auto` matches the active video standard (PAL → PAL artifacting, NTSC → NTSC).  The `*hi` variants are higher-quality / higher-cost shaders for the same look. |
| `vkbd`      | `0`, `1`                                                | Show or hide the on-screen Atari keyboard at startup.  In embed mode the page-bar **⌨ Keyboard** button is hidden along with all the other chrome, so this is the only way for an embed visitor to reach the keyboard if the game needs it.  The keyboard can still be dismissed with **F12** (PC keyboard) or by tapping the X icon (touch). |
| `stretch`   | `fill`, `preserve`, `square`, `integral`, `integral_preserve` | Display stretch mode.  See the **Pixel-perfect text** section below — `integral` is the only mode that guarantees every Atari pixel maps to the same number of destination pixels at any iframe size, so it is the right pick for titles whose hi-res GR.0 text needs to look crisp.  Default is `preserve` (gaming-mode default). |

The application order at runtime is:

1. Saved settings + gaming-mode performance preset (Efficient /
   Balanced / Quality) load from the user's profile.
2. `crt=0|1` runs (if present) — overwrites `filter` and `artifact`
   to the preset's values.
3. `filter=` runs (if present) — overrides whatever `crt=` set.
4. `artifact=` runs (if present) — overrides whatever `crt=` set.
5. `vkbd=` runs (if present) — independent of the above.

So `?crt=1&filter=point&artifact=none` is a valid combination: it
gives you scanlines + bloom + vignette but with crisp pixel text
and no color smearing.

**Common recipes:**

```
# Pixel-perfect text, no CRT effects, no color artifacting.
?embed=1&lib=mygame.xex&pal=1&crt=0&filter=point&artifact=none&stretch=integral

# Authentic CRT look (default) but artifacting disabled.
?embed=1&lib=mygame.xex&pal=1&artifact=none

# Soft scaling with a virtual keyboard for all visitors.
?embed=1&lib=adventure.atr&hardware=800xl&filter=bilinear&vkbd=1
```

#### Pixel-perfect text — `stretch=integral` and the right iframe aspect ratio

Two settings together decide whether your titlescreen text looks
crisp or slightly fuzzy:

1. **The stretch mode** decides whether the source frame is scaled
   by an integer multiplier or stretched to fill the iframe
   continuously.
2. **The iframe's CSS aspect ratio** decides how much room the
   browser hands the canvas for that scaling.

The default `stretch=preserve` (PreserveAspectRatio) multiplies the
source width by the **pixel aspect ratio** (PAR) of the active video
standard — about ×1.04 for PAL and ×0.857 for NTSC — to reproduce
the geometry of a CRT.  That ratio is irrational, so for almost any
iframe size the renderer ends up scaling each Atari pixel by a
non-integer factor: some Atari pixels become 2 destination pixels
wide, others become 3, and hi-res GR.0 text (the 80-column GR.0 font
many `.xex` titles use for menus) reads as slightly uneven even with
`filter=point`.

**Fix: pass `stretch=integral`.**  This drops the PAR multiplier and
forces an integer scale (1×, 2×, 3×, …) on both axes, so every
Atari pixel is drawn at exactly the same size.  The visual cost is
that the picture is slightly narrower than a CRT would have shown
(no PAR correction), and the iframe gets thin black bars on the
sides if its size doesn't match a clean multiple.  For a typical
800-wide embed of an `800xl` title you'll get a 672×480 (2×) canvas
with 64 px of black on each side.

The frame the emulator renders depends on the **overscan** setting
(default: Normal):

| Region | Source frame | Stock aspect (no PAR) |
|--------|--------------|-----------------------|
| NTSC   | 336 × 224    | 3 / 2                 |
| PAL    | 336 × 240    | 7 / 5                 |

If you want the iframe to wrap the canvas without any letterboxing
in `stretch=integral` mode, give the iframe one of the matching
aspect ratios:

```html
<!-- PAL game, pixel-perfect, fills the iframe at any 2× / 3× size. -->
<iframe
  src="altirra/?embed=1&lib=mygame.xex&pal=1&crt=0&filter=point&artifact=none&stretch=integral"
  style="border:0; aspect-ratio: 7 / 5; max-width: 100%; width: 672px;"
  allow="autoplay; fullscreen; gamepad"
  loading="lazy"
  title="Play MyGame">
</iframe>
```

```html
<!-- NTSC game, same idea with the 3:2 aspect that matches 336 × 224. -->
<iframe
  src="altirra/?embed=1&lib=mygame.xex&ntsc=1&crt=0&filter=point&artifact=none&stretch=integral"
  style="border:0; aspect-ratio: 3 / 2; max-width: 100%; width: 672px;"
  allow="autoplay; fullscreen; gamepad"
  loading="lazy"
  title="Play MyGame">
</iframe>
```

The pixel width 672 = 2× source width is intentional: at smaller
widths the Integral stretch falls back to 1× and the picture looks
tiny; at 1008 px it would jump to 3×.  Pick a width that's a clean
multiple of 336 and the canvas always exactly fills its iframe.

The often-seen `aspect-ratio: 4 / 3` is the right choice **only**
for `stretch=preserve` (the default) and **only** if you accept the
non-integer per-pixel scaling that comes with it.  For
pixel-perfect text use `stretch=integral` and one of the source-
matching aspects above.

#### Showing the virtual keyboard only on mobile

`vkbd=1` is unconditional by design — it is the embed author's
explicit choice, not a device auto-detect.  If you want the
virtual keyboard to appear only on touchscreen visitors, that
decision belongs in the page that hosts the iframe, where you
have the audience and layout context.  A few lines of vanilla JS
on the parent page do it:

```html
<iframe id="emu"
  src="altirra/?embed=1&lib=mygame.xex&hardware=800xl&pal=1"
  width="800" height="600"
  style="border:0; aspect-ratio: 4 / 3; max-width: 100%;"
  allow="autoplay; fullscreen; gamepad"
  loading="lazy"
  title="Play MyGame">
</iframe>
<script>
  // Show the on-screen Atari keyboard only when the visitor is on
  // a touch device.  Heuristic mirrors the WASM page's own touch-
  // overlay detection: pointer:coarse first, maxTouchPoints fallback.
  // Keep this <script> AFTER the <iframe> element so getElementById
  // finds it; the iframe then fetches the rewritten URL.
  (function () {
    var f = document.getElementById('emu');
    if (!f) return;
    var isTouch =
      (window.matchMedia &&
       window.matchMedia('(pointer: coarse)').matches) ||
      (navigator.maxTouchPoints || 0) > 0;
    if (isTouch) f.src = f.src + '&vkbd=1';
  })();
</script>
```

The script appends `&vkbd=1` to the iframe's `src` on touch
devices, so phone / tablet visitors land with the keyboard up
while desktop visitors do not.

### Click-to-play — `autoplay=0`

By default the emulator boots and starts running the moment the page
loads — same behaviour as every video player without a poster
image.  For embed authors who want a YouTube-style facade where the
~5 MB wasm bundle isn't downloaded until the visitor explicitly
engages, pass `?autoplay=0`.

What changes when `autoplay=0` is set:

1. The page paints a black canvas with a centred ▶ play button.
2. The `AltirraSDL.js` script tag is **not** injected.  No wasm
   bytes, no firmware fetch, no game-file fetch — none of the embed
   payload hits the wire.
3. On the first click / tap on the overlay, the script tag is
   injected, the wasm boots, and the deep-link applier runs the
   normal startup sequence (firmware probe, lib fetch, cold reset).
4. The same click satisfies the browser's audio-autoplay gesture
   requirement, so the emulator's first frames are audible.

This is independent of the iframe's
`allow="autoplay; fullscreen; gamepad"` attribute — that attribute
controls the *browser's* audio-autoplay permission policy and has
no effect on whether the simulator runs.  Removing
`allow="autoplay"` does not stop the emulator from booting; it just
mutes the audio until the user clicks.  `?autoplay=0` is the URL
param that actually defers the boot.

```html
<!-- Lazy-loaded embed: nothing is fetched until the visitor clicks. -->
<iframe
  src="altirra/?embed=1&lib=mygame.xex&pal=1&autoplay=0"
  width="800" height="600"
  style="border:0; aspect-ratio: 4 / 3; max-width: 100%;"
  allow="autoplay; fullscreen; gamepad"
  loading="lazy"
  title="Play MyGame">
</iframe>
```

`autoplay=0` is independent of every other display knob — it
combines freely with `crt`, `filter`, `artifact`, `stretch`, `vkbd`,
and the determinism flags.

### UI mode — `ui=desktop` to land in the full emulator front end

By default any URL that carries `?lib=…` (or a netplay-join /
broker context) enters **Gaming Mode** — the full-canvas, no-menu-
bar layout designed for kiosks and phones.  This is the right
choice for a self-hosted embed that exists only to play a single
title.

For "Try this game in the full emulator" links — where you want
the visitor to land on the same page they would see at
`/AltirraSDL/play/`, with the menu bar, the file manager, the
debugger, the dialogs — but with a game pre-loaded, pass
`?ui=desktop`:

```
# Open the full emulator front end, with TheLady.xex preloaded.
https://your-site/altirra/?lib=TheLady.xex&hardware=800xl&pal=1&ui=desktop
```

Notice that `?embed=1` is **omitted** in this recipe.  `embed=1`
hides the page chrome via CSS (`body.embed > header { display:none }`),
which would defeat the point of switching to Desktop UI.  Use
`ui=desktop` for "I want the full app" and `embed=1` for "I want
just the canvas".  The two are independent:

| `embed=1` | `ui=desktop` | Result |
|-----------|--------------|--------|
| no        | no           | Full chrome page; Gaming Mode if `?lib=` is set, else Desktop Mode |
| no        | yes          | Full chrome page; Desktop Mode even with `?lib=` |
| yes       | no           | Chrome hidden; Gaming Mode (typical embed) |
| yes       | yes          | Chrome hidden; Desktop Mode — rarely useful (no menu bar to drive Desktop UI) |

`ui=gaming` exists as the inverse override, for the corner case of
a "Start Atari Emulator" entry point that should land in Gaming
Mode without auto-loading any game (`?ui=gaming` with no `?lib=`).

The Join (`?s=…`) deep-link path always stays in Gaming Mode even
with `ui=desktop`, because the netplay handshake's prep flow lives
only in the Gaming-Mode dispatcher — overriding there would trap
the user in Desktop UI without the controls they need to complete
the join.

### Determinism — RAM randomization and launch jitter

Two independent knobs control how reproducible each play is.  Both
are passed to the simulator as CLI switches before the program
loads, so they take effect on the very first cold-reset.

| Parameter   | Default   | Effect |
|-------------|-----------|--------|
| `randmem`   | `1` (on)  | Bundled toggle: cold-reset memory clear mode + EXE-load random fill.  When on, RAM at cold-reset time is filled with a wall-clock-seeded pseudo-random pattern, and any RAM region the loaded program does not overwrite is re-randomized at HLE load time.  Either path produces different bytes each visit, so any address the game samples as an "RNG seed" returns different content per session.  **On by default for every surface** (lobby Solo / Play Together / bare emulator / self-hosted embed) — real Atari hardware never gave you the same uninitialised RAM twice.  Set `randmem=0` to opt out for replay-capture / speedrun pages where every visitor must see a bit-identical play sequence.  Bundles **Memory: Randomize on EXE load** *and* **Memory clear mode = Random** from the Windows Altirra Configure System dialog. |
| `randdelay` | `1` (on)  | The HLE program loader inserts a small randomized jitter between the cold-reset settle frame and program entry.  Set `randdelay=0` for a frame-deterministic boot — needed when capturing reproducible replays alongside `randmem=0`. |

> **Online-play note.**  Play Together sessions also start with
> per-session random RAM, but each session is bit-identical between
> the two peers.  Mechanism:
>
> 1. The host's pre-snapshot ColdReset uses `kATMemoryClearMode_Random`
>    (set by the netplay canonical profile in `netplay_profile.cpp`),
>    seeded from the host's wall-clock-derived `mRandomSeed`.  RAM is
>    filled with a fresh random pattern that varies per host-page-load.
> 2. The host captures a snapshot of the post-boot state and ships it
>    to the joiner over the netplay channel.  `ApplySnapshot` overwrites
>    the joiner's RAM byte-for-byte with the host's, so both peers have
>    bit-identical content at frame 0.
> 3. `ATSimulator::ReseedNetplayRandomState` runs on both peers at
>    `BeginSession`, locking every RNG subsystem (`mRandomSeed`, derived
>    per-subsystem seeds, PIA floating-input LFSR, float-timer schedule)
>    to the constant `kLockedRandomSeed = 0xA7C0BEEFu`.  All in-session
>    RAM scrambles, RNG draws, and PIA floating-bit reads use this
>    constant on both peers, so lockstep proceeds with deterministic
>    divergence from frame 0.
>
> Net effect: each session feels alive (host's wall-clock seed varies
> across page loads), both peers see exactly the same content (the
> snapshot does the heavy lifting), and lockstep determinism is
> preserved (the locked seed handles every post-snapshot RNG draw).

The simulator's master RNG is seeded from wall-clock time at startup
(one `srand(SDL_GetPerformanceCounter ^ SDL_GetTicksNS)` in
`main_sdl3.cpp`), so POKEY noise + PIA floating-input + cold-reset
RAM scrambling are non-deterministic across visits.  `randmem=1`
ensures both the cold-reset clear and the EXE-load fill participate
in that randomness.

For *typical* embeds the defaults already do the right thing:

```
# Each play different — default behaviour; no flag needed.
?embed=1&lib=mygame.xex

# Bit-identical playback every time — recommended for replay captures.
?embed=1&lib=mygame.xex&randmem=0&randdelay=0
```

These params are independent of `crt` / `filter` / `artifact` /
`vkbd`; combine freely.

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
`ntsc`, `basic`, `kernel`, `title`, `crt`, `filter`, `artifact`,
`vkbd`, `stretch`, `ui`, `autoplay`, `randmem`, and `randdelay`.

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
  src="altirra/?embed=1&lib=mygame.atr&hardware=800xl"
  width="800" height="600"
  style="border:0; aspect-ratio: 4 / 3; max-width: 100%;"
  allow="autoplay; fullscreen; gamepad"
  loading="lazy"
  title="Play MyGame">
</iframe>
```

The `src` above is intentionally **page-relative** (no leading
`/`).  The browser resolves it against the URL of the document
that contains the iframe, so the example works whether your page
is at `/index.html`, `/atari/thelady.htm`, or
`/games/2026/foo/bar.html`, as long as the bundle is at
`<that-directory>/altirra/`.  A *root-relative* `src="/altirra/..."`
(with the leading slash) only works when the bundle sits at the
**site root**; a page at `https://your-site/atari/foo.htm` with an
`src="/altirra/..."` iframe will load
`https://your-site/altirra/...`, which is almost certainly not
where you put the bundle.  When in doubt, write the full path
explicitly: `src="/atari/altirra/?embed=1&..."`.

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

**The iframe shows "Not Found" / 404 instead of the emulator.**
The iframe `src` is pointing at a path that doesn't exist on your
server.  By far the most common cause is a leading `/` on the
`src` when the bundle is *not* at the site root.  If the page is
at `https://your-site/atari/thelady.htm` and the bundle is at
`https://your-site/atari/altirra/`, then `src="/altirra/..."`
resolves to `https://your-site/altirra/` (no `/atari/`), which is
the wrong directory.  Use either the page-relative form
`src="altirra/..."` (no leading slash) or the full root-relative
path `src="/atari/altirra/..."`.  Open DevTools' Network tab,
look at the failing iframe request URL, and adjust the `src`
until the URL Apache reports matches the directory you uploaded.

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
