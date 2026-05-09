# Self-hosting the Atari emulator on your own page

This is the **embed kit** for the AltirraSDL WebAssembly build.  It is
aimed at people who have written an Atari 8-bit (or 5200) game and want
visitors to play it directly on their site, with no detour through a
third-party lobby and no install step.

## What you get

A single zip — the same `AltirraSDL-<version>-wasm.zip` that ships in
the GitHub Releases — drops onto any plain static host (Apache, Nginx,
Caddy, GitHub Pages, S3, Netlify, Cloudflare Pages…).  When the URL
carries `?embed=1`, the emulator hides every piece of lobby chrome and
the canvas fills the iframe / browser viewport.  When the URL also
carries `?lib=` (a path to your game) the emulator boots straight into
your title.

## Quickstart — three steps

1. **Drop the bundle on your server.**  Unzip
   `AltirraSDL-<version>-wasm.zip` into any directory.  The whole
   directory becomes the emulator's "home".  Example layout (the
   directory name is up to you):

   ```
   /your-site/altirra/
     index.html              ← the WASM page
     AltirraSDL.js
     AltirraSDL.wasm
     wasm_lib_deeplink.js
     config.json
     library/                ← put your game files here
     firmware/               ← optional, for custom OS / BASIC ROMs
   ```

   On a host that supports content-encoding negotiation (Caddy, Nginx
   with `gzip_static`, Apache with `mod_deflate`'s precompressed
   variants) the bundled `.br` and `.gz` siblings are served
   automatically — the `.wasm` drops from ~11 MB to ~3 MB on the wire.

2. **Add your game.**  Copy your `.atr` / `.xex` / `.car` / `.cas`
   into `library/`:

   ```
   /your-site/altirra/library/mygame.atr
   ```

3. **Link to the embed URL.**  Send visitors to:

   ```
   https://your-site/altirra/?embed=1&lib=mygame.atr&hardware=800xl
   ```

   …or embed it in your existing page with an iframe.  The `src`
   below is **page-relative**: it works whether your bundle sits at
   `/altirra/`, `/atari/altirra/`, `/games/altirra/`, or any other
   prefix, as long as the iframe-hosting page is in the parent
   directory of the bundle:

   ```html
   <iframe
     src="altirra/?embed=1&lib=mygame.atr&hardware=800xl&pal=1"
     width="800" height="600"
     style="border:0; aspect-ratio: 4 / 3; max-width: 100%;"
     allow="autoplay; fullscreen; gamepad"
     loading="lazy"
     title="Play MyGame in your browser">
   </iframe>
   ```

   > **Watch the leading slash.**  `src="/altirra/..."` (with `/`)
   > is *root-relative* and only works when the bundle is at the
   > site root.  If the page is `https://your-site/atari/foo.htm`
   > and the bundle is at `https://your-site/atari/altirra/`, use
   > `src="altirra/..."` (no leading slash) or the full path
   > `src="/atari/altirra/..."`.  A 404 on the iframe is almost
   > always this.

That is the whole tutorial.  See **EMBED.md** for the full URL
reference, custom firmware, layout overrides for non-standard
deploy paths, and the security model.

## Common display tweaks

Add any of these to the URL to override the default look.  All four
work together — combine them to taste.

| Param      | Values                                     | What it does |
|------------|--------------------------------------------|--------------|
| `crt`      | `0` / `1`                                  | Disable / enable the CRT screen effects (scanlines, bloom, vignette).  `crt=0` gives a flat, sharp look — best for text-heavy titles or readability. |
| `filter`   | `point`, `bilinear`, `sharp`, `bicubic`, `auto` | Display upscale filter.  `point` = pixel-perfect (nearest-neighbour); `bilinear` = soft; `sharp` = sharp-bilinear; `auto` = renderer's choice. |
| `artifact` | `none`, `auto`, `ntsc`, `pal`, `ntschi`, `palhi` | NTSC/PAL color artifacting.  `none` kills color smearing on text; `auto` matches the active video standard. |
| `vkbd`     | `0` / `1`                                  | Show / hide the on-screen Atari keyboard at startup.  Useful for embeds whose game accepts text input. |
| `randmem`  | `0` / `1`                                  | Randomize uninitialised RAM on EXE load.  **On by default in embed mode** so each visit feels different (game RNG seeds vary).  Set `randmem=0` to opt out for replay-capture / speedrun pages. |
| `randdelay`| `0` / `1`                                  | Randomize the small jitter between cold-reset and program entry.  On by default; turn off (`randdelay=0`) for frame-deterministic boot (speedrun pages, replay capture). |

**Pixel-perfect text** for a PAL game with no color tricks:

```
?embed=1&lib=mygame.xex&hardware=800xl&pal=1&crt=0&filter=point&artifact=none
```

**Authentic CRT look + a virtual keyboard** for a touchscreen
embed of a typing game:

```
?embed=1&lib=type.atr&hardware=800xl&crt=1&vkbd=1
```
