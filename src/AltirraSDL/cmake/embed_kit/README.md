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

   …or embed it in your existing page with an iframe:

   ```html
   <iframe
     src="/altirra/?embed=1&lib=mygame.atr&hardware=800xl&pal=1"
     width="800" height="600"
     style="border:0; aspect-ratio: 4 / 3; max-width: 100%;"
     allow="autoplay; fullscreen; gamepad"
     loading="lazy"
     title="Play MyGame in your browser">
   </iframe>
   ```

That is the whole tutorial.  See **EMBED.md** for the full URL
reference, custom firmware, layout overrides for non-standard
deploy paths, and the security model.
