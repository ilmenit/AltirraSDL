// AltirraSDL — Curated-library deep-link helpers
//
// This file is loaded by the WASM page (index.html) BEFORE
// AltirraSDL.js, then the host page calls three named entry points at
// the appropriate Emscripten lifecycle points:
//
//   1. ATLibDeepLink.parseUrl(__wasmCliArgs)
//        Synchronous URL parse; runs before window.Module is built.
//        Reads ?lib= / ?host= / ?pal= / ?hardware= / ?memsize= / ?title=
//        from location.search and stashes the request on
//        window.__altirraLib.  CLI flags that the C-side argv parser
//        accepts (--hardware / --memsize / --pal / --ntsc) get pushed
//        into __wasmCliArgs immediately.  --disk N <path> / --run /
//        --cart / --tape are pushed by step (2) once the VFS paths
//        are known.
//
//   2. ATLibDeepLink.preRunFetch(Module, __wasmCliArgs)
//        Called inside Module.preRun (after IDBFS mount + initial
//        syncfs) when window.__altirraLib.paths is non-empty.  Fetches
//        each library file into /home/web_user/games/library/, pushes
//        the corresponding boot CLI args in URL-source order so multi-
//        disk slots stay deterministic, and adds a 'library-fetch'
//        run-dependency that blocks main() until every file has
//        landed.  Stat-skips files that already exist in IDBFS from a
//        previous visit (~80 KB ATR re-download avoided per repeat
//        Play Solo click).
//
//   3. ATLibDeepLink.onRuntimeReady(Module)
//        Called once from the onRuntimeInitialized tail.  Registers
//        the library staging dir with the in-emulator Game Library so
//        deep-link plays accumulate alongside wizard-installed packs.
//        Then, if ?host=1 was set, kicks the auto-host: a single call
//        into ATWasmAutoHostNetplay() which stashes the publish for
//        the netplay tick's DriveAutoHost driver to fire once the
//        boot lands.
//
// Why split out: keeping ~250 lines of library plumbing inline in
// wasm_index.html.in pushed that file past 6,400 lines and made
// future edits to either the wizard or the deep-link risk ripping
// the wrong scrollback.  A separate .js with three named entry
// points keeps the host page's "what runs when" obvious.
//
// Why a global namespace (window.ATLibDeepLink) rather than a
// module: the host page has no <script type="module">, IDBFS doesn't
// like top-level await in the preRun timing window, and the
// surrounding wasm_index.html.in is plain ES5-style anyway.  A flat
// IIFE attached to a global is the smallest change that ships this.

(function () {
  "use strict";

  // ── State ────────────────────────────────────────────────────────
  // Single source of truth shared between the URL parser, the
  // preRun fetcher, and the onRuntimeReady auto-host kick.  Mounted
  // on `window` so the wasm_index.html.in's inline <script> can also
  // see it (e.g. for diagnostic logging).
  window.__altirraLib = {
    paths:       [],
    autoHost:    false,
    hostTitle:   '',
    vfsPaths:    [],   // populated by preRunFetch in source order
    // Broker mode (M3): set by parseUrl when the URL carries
    // ?broker=1&session=...&token=...&intent=...&handle=...&role=...
    // In broker mode, the WASM emulator adopts an existing lobby
    // session published by the page broker rather than calling
    // ATWasmAutoHostNetplay (which would create a new session).
    // null when not in broker mode; an object when set.
    brokerMode:  null,
    // Embed mode (?embed=1): host page is a third-party site that just
    // wants the canvas with their game pre-loaded.  parseUrl flips this
    // true; wasm_index.html.in reads it to add a body.embed CSS class
    // (chrome suppression) and to short-circuit the curated-content
    // first-run firmware/game-pack wizard via gameDeepLinkActive().
    embed:       false,
    // Firmware ROMs to fetch alongside the lib= files.  Populated from
    // ?firmware=path1,path2,...; landed under /home/web_user/firmware
    // by preRunFetch so ATWasmRescanFirmware (already called in
    // index.html's onRuntimeInitialized) hashes them and assigns the
    // right type-defaults.  No CLI arg is pushed for these — the
    // simulator picks up the type override automatically.
    firmwarePaths: [],
    // Embed-kit display overrides parsed from the URL.  Each is left
    // null when the corresponding param is absent so the deep-link
    // applier can distinguish "user didn't specify" (keep saved /
    // gaming-mode default) from "user wants this exact value".
    //   crt:        true|false|null  (?crt=0|1)
    //   filter:     0..4|null         (?filter=point|bilinear|sharp|bicubic|auto)
    //   artifact:   0..6|null         (?artifact=none|ntsc|pal|ntschi|palhi|auto|autohi)
    //   vkbd:       true|false|null   (?vkbd=0|1)
    // Applied in onRuntimeReady AFTER the gaming-mode preset has run,
    // so an explicit ?crt=0 wins over Balanced's vignette default.
    crt:         null,
    filter:      null,
    artifact:    null,
    vkbd:        null,
  };

  // Resolve the same-origin base URL for fetched assets.  The lobby
  // hosts the WASM at /AltirraSDL/play/ and the curated content at
  // /AltirraSDL/library/ + /AltirraSDL/firmware/, so the historical
  // default is an absolute /AltirraSDL/<kind>/ path.  Self-hosted
  // embeds (?embed=1) typically drop the whole bundle next to a
  // sibling library/ + firmware/ directory and want page-relative
  // paths so the deploy works at any URL prefix.  A host page can
  // override either knob with <meta name="altirra-library-base">
  // / "altirra-firmware-base"; the meta wins over both defaults so
  // an author with a non-standard tree can place files anywhere.
  function metaBase(name) {
    try {
      var m = document.querySelector('meta[name="' + name + '"]');
      if (!m) return null;
      var v = (m.getAttribute('content') || '').trim();
      if (!v) return null;
      // Normalise: ensure trailing slash so concatenation is clean.
      if (v.charAt(v.length - 1) !== '/') v += '/';
      return v;
    } catch (_) { return null; }
  }
  function resolveBase(kind /* 'library' | 'firmware' */, embed) {
    var meta = metaBase('altirra-' + kind + '-base');
    if (meta) return meta;
    if (embed) return kind + '/';
    return '/AltirraSDL/' + kind + '/';
  }

  // Path validation: same-origin <base>/<path> only.  Reject '..'
  // segments, leading slashes, control chars, scheme://.  Same shape
  // as the lobby's curated-content sanitiser — embed mode does not
  // relax these checks because the input still flows through fetch()
  // against the host's origin.
  function pathOk(s) {
    return s.length > 0
        && s.length <= 256
        && !/^[\/\\]/.test(s)
        && !/(^|[\/\\])\.\.([\/\\]|$)/.test(s)
        && !/[\x00-\x1f<>:"|?*]/.test(s)
        && !/^[a-zA-Z][a-zA-Z0-9+.\-]*:/.test(s);
  }

  // Minimal logger — falls through to console.log if the host page's
  // jlog isn't available yet (we're loaded before AltirraSDL.js, so
  // the wasm_index.html.in's appendLog/jlog haven't been wired up
  // when parseUrl runs).
  function log() {
    var args = Array.prototype.slice.call(arguments);
    if (typeof window.jlog === 'function') {
      window.jlog.apply(null, args);
    } else if (window.console && console.log) {
      console.log.apply(console, ['[lib-deeplink]'].concat(args));
    }
  }

  // ── 1. URL parse ─────────────────────────────────────────────────
  function parseUrl(__wasmCliArgs) {
    try {
      var p = new URLSearchParams(window.location.search || '');

      // ?embed=1 — chrome-free single-page embed mode.  Read first so
      // logging downstream can mention it and so resolveBase() picks
      // the page-relative default for ?lib= / ?firmware= when no
      // <meta> override is present.  We attach the body.embed class
      // here (rather than waiting for the lobby IIFE) so the chrome
      // is suppressed before the first paint — this script tag is
      // synchronous and lives inside <body>, so document.body is
      // already constructed by the time parseUrl runs.
      if ((p.get('embed') || '') === '1') {
        window.__altirraLib.embed = true;
        try {
          if (document && document.body)
            document.body.classList.add('embed');
        } catch (_) {}
        // ?title= sets the browser tab name in embed mode so an
        // iframe-less direct page lands with a friendlier label
        // than "AltirraSDL @VERSION@ — WebAssembly".  We only do
        // this under embed=1 to avoid stomping on the lobby's
        // baked-in <title> for non-embed visits.  The host=1 auto-
        // host path further down also reads ?title=, but for the
        // session display name (lib.hostTitle), not document.title
        // — the two uses of the same param don't conflict.
        var embedTitle = (p.get('title') || '')
          .replace(/[\x00-\x1f\x7f]/g, '').slice(0, 64);
        if (embedTitle) {
          try { document.title = embedTitle; } catch (_) {}
        }
        log('embed mode requested'
          + (embedTitle ? ', title="' + embedTitle + '"' : ''));
      }

      var libRaw = (p.get('lib') || '').trim();
      if (libRaw) {
        var parts = libRaw.split(',').map(function (x) {
          return x.trim();
        }).filter(Boolean);
        var good = [];
        for (var i = 0; i < parts.length && good.length < 16; ++i) {
          if (pathOk(parts[i])) {
            good.push(parts[i]);
          } else {
            log('rejected malformed lib path:', parts[i]);
          }
        }
        window.__altirraLib.paths = good;
        if (good.length) log('lib request — ' + good.length + ' file(s)');
      }

      // ?firmware=rom1,rom2,...  — fetch ROM(s) into the firmware
      // directory.  ATUIDoFirmwareScan (already called once at startup
      // by wasm_index.html.in) hashes each file and registers it with
      // the firmware manager, calling SetSpecificFirmware for any
      // recognised type.  Authors who need a custom OS/BASIC drop it
      // here; the simulator's hardware-mode kernel pick then resolves
      // to the dropped ROM automatically.  No CLI arg is pushed —
      // the type-default override is enough.  Cap at 8 to bound the
      // worst-case fetch latency on a fresh visit.
      var fwRaw = (p.get('firmware') || '').trim();
      if (fwRaw) {
        var fwParts = fwRaw.split(',').map(function (x) {
          return x.trim();
        }).filter(Boolean);
        var fwGood = [];
        for (var fi = 0; fi < fwParts.length && fwGood.length < 8; ++fi) {
          if (pathOk(fwParts[fi])) {
            fwGood.push(fwParts[fi]);
          } else {
            log('rejected malformed firmware path:', fwParts[fi]);
          }
        }
        window.__altirraLib.firmwarePaths = fwGood;
        if (fwGood.length)
          log('firmware request — ' + fwGood.length + ' ROM(s)');
      }

      var hwAllow = ['800','800xl','1200xl','130xe','xegs','1400xl','5200'];
      var hw = (p.get('hardware') || '').trim().toLowerCase();
      if (hw) {
        if (hwAllow.indexOf(hw) >= 0) {
          __wasmCliArgs.push('--hardware', hw);
          log('--hardware ' + hw);
        } else { log('ignored unknown hardware:', hw); }
      }

      var memAllow = ['8K','16K','24K','32K','40K','48K','52K','64K',
                      '128K','256K','320K','320KCOMPY','576K','576KCOMPY','1088K'];
      var mem = (p.get('memsize') || '').trim().toUpperCase();
      if (mem) {
        if (memAllow.indexOf(mem) >= 0) {
          __wasmCliArgs.push('--memsize', mem);
          log('--memsize ' + mem);
        } else { log('ignored unknown memsize:', mem); }
      }

      if ((p.get('pal')  || '') === '1') {
        __wasmCliArgs.push('--pal');  log('--pal');
      } else if ((p.get('ntsc') || '') === '1') {
        __wasmCliArgs.push('--ntsc');
      }

      // ?basic=1 boots with Atari BASIC enabled.  Useful even without
      // a ?lib= entry: the lobby's "Start Atari Emulator" button can
      // pass it directly.  --basic / --nobasic are the cmdline-side
      // toggles already wired into ATProcessCommandLineSDL3.
      if ((p.get('basic') || '') === '1') {
        __wasmCliArgs.push('--basic'); log('--basic');
      }

      // ?kernel=… pins a specific kernel ROM type, bypassing the
      // hardware-mode-driven auto pick.  Maps directly to the
      // existing --kernel CLI option in commandline_sdl3.cpp.  The
      // built-in `lle` / `llexl` / `5200lle` values are useful when
      // the author hasn't shipped any firmware ROM and wants the
      // built-in clean-room kernel rather than whatever the
      // simulator default would be — for example a self-hosted
      // embed of a 130XE title that should always run against
      // LLE-XL regardless of what the visitor previously installed.
      var kernelAllow = ['default','osa','osb','xl','xegs',
                         '1200xl','5200','lle','llexl','hle','5200lle'];
      var kern = (p.get('kernel') || '').trim().toLowerCase();
      if (kern) {
        if (kernelAllow.indexOf(kern) >= 0) {
          __wasmCliArgs.push('--kernel', kern);
          log('--kernel ' + kern);
        } else {
          log('ignored unknown kernel:', kern);
        }
      }

      // ?crt=0|1 — master CRT-effect toggle.  Applied post-runtime via
      // ATWasmSetCRTEnabled, which routes through the same Gaming-Mode
      // performance preset (Efficient ↔ Quality) the page bar's CRT
      // button drives.  Useful for embed authors whose game's
      // titlescreen text needs pixel-sharp rendering rather than the
      // gaming-mode default (Balanced: bilinear filter + vignette).
      var crtRaw = (p.get('crt') || '').trim();
      if (crtRaw === '0' || crtRaw === '1') {
        window.__altirraLib.crt = (crtRaw === '1');
        log('crt=' + (window.__altirraLib.crt ? '1' : '0'));
      } else if (crtRaw) {
        log('ignored unknown crt value:', crtRaw);
      }

      // ?filter=point|bilinear|sharp|bicubic|auto — display upscale
      // filter.  Maps to the public ATDisplayFilterMode enum (see
      // uitypes.h).  Applied via ATWasmSetDisplayFilter once the
      // runtime is ready.  `auto` = AnySuitable (the renderer picks).
      var filterMap = { point: 0, bilinear: 1, bicubic: 2, auto: 3,
                        sharp: 4, sharpbilinear: 4 };
      var filterRaw = (p.get('filter') || '').trim().toLowerCase();
      if (filterRaw) {
        if (filterRaw in filterMap) {
          window.__altirraLib.filter = filterMap[filterRaw];
          log('filter=' + filterRaw + ' (mode='
              + window.__altirraLib.filter + ')');
        } else {
          log('ignored unknown filter:', filterRaw);
        }
      }

      // ?artifact=none|ntsc|pal|ntschi|palhi|auto|autohi — color
      // artifacting mode.  Maps to ATArtifactMode (see gtia.h).  The
      // most common embed use is `none` to kill PAL/NTSC color
      // smearing on titlescreen text.  `auto` lets the simulator pick
      // based on the active video standard.
      var artifactMap = { none: 0, ntsc: 1, pal: 2, ntschi: 3,
                          palhi: 4, auto: 5, autohi: 6 };
      var artRaw = (p.get('artifact') || '').trim().toLowerCase();
      if (artRaw) {
        if (artRaw in artifactMap) {
          window.__altirraLib.artifact = artifactMap[artRaw];
          log('artifact=' + artRaw + ' (mode='
              + window.__altirraLib.artifact + ')');
        } else {
          log('ignored unknown artifact:', artRaw);
        }
      }

      // ?vkbd=0|1 — show the on-screen Atari keyboard at startup.
      // Useful for embeds whose game accepts text input or letter-
      // selection menus, since embed mode hides the page-bar
      // ⌨ Keyboard button (chrome is suppressed).  Applied via
      // ATWasmSetVirtualKeyboard once the runtime is ready.
      var vkbdRaw = (p.get('vkbd') || '').trim();
      if (vkbdRaw === '0' || vkbdRaw === '1') {
        window.__altirraLib.vkbd = (vkbdRaw === '1');
        log('vkbd=' + (window.__altirraLib.vkbd ? '1' : '0'));
      } else if (vkbdRaw) {
        log('ignored unknown vkbd value:', vkbdRaw);
      }

      // ?randmem=0|1 — RAM randomization on EXE load.  Pushed as a CLI
      // switch (NOT a post-runtime setter) because it must be set
      // before --run is processed in main(), otherwise the EXE has
      // already loaded against a deterministic memory floor.  Off by
      // default in Altirra — turn on for "the game feels different
      // each play" titles whose RNG samples low RAM.
      var rmRaw = (p.get('randmem') || '').trim();
      if (rmRaw === '1') {
        __wasmCliArgs.push('--randmem'); log('--randmem');
      } else if (rmRaw === '0') {
        __wasmCliArgs.push('--norandmem'); log('--norandmem');
      } else if (rmRaw) {
        log('ignored unknown randmem value:', rmRaw);
      }

      // ?randdelay=0|1 — randomize program launch delay (the small
      // jitter between cold-reset settle and EXE entry).  On by
      // default in Altirra; off makes XEX boot frame-deterministic
      // for replay / speedrun pages.  Same CLI-arg ordering reason
      // as randmem.
      var rdRaw = (p.get('randdelay') || '').trim();
      if (rdRaw === '1') {
        __wasmCliArgs.push('--randdelay'); log('--randdelay');
      } else if (rdRaw === '0') {
        __wasmCliArgs.push('--norandelay'); log('--norandelay');
      } else if (rdRaw) {
        log('ignored unknown randdelay value:', rdRaw);
      }

      if ((p.get('host') || '') === '1') {
        var t = (p.get('title') || '')
          .replace(/[\x00-\x1f\x7f]/g, '').slice(0, 64);
        if (!t && window.__altirraLib.paths.length) {
          var first = window.__altirraLib.paths[0].split('/').pop();
          t = first.replace(/\.[^.]+$/, '')
                   .replace(/\s*[-–]\s*Disk\s*\d+$/i, '');
        }
        window.__altirraLib.autoHost  = true;
        window.__altirraLib.hostTitle = t;
        log('auto-host requested, title="' + t + '"');
      }

      // ── Broker context (?broker=1&session=&token=&intent=&handle=
      //                    &role=&code=) ────────────────────────────
      // Parsed last because it OVERRIDES the auto-host flag: when in
      // broker mode the WASM does not call ATWasmAutoHostNetplay
      // (which creates a new session), it adopts the supplied
      // sessionId+token via ATWasmAdoptBrokerSession.  We still
      // honour the lib/hardware/memsize/pal CLI args parsed above —
      // the broker page passes them so the emulator boots the
      // right machine + ROM regardless of broker context.
      if ((p.get('broker') || '') === '1') {
        // Sanitise every field to the same character class the lobby
        // already enforces (UUID-shaped session/intent ids, 32-char
        // hex handles, hex code-hash, role ∈ {host,joiner}).  Bad
        // input falls back to legacy auto-host so a malformed URL
        // doesn't trap the user in a half-initialised broker state.
        var sid    = (p.get('session') || '').trim();
        var token  = (p.get('token')   || '').trim();
        var intent = (p.get('intent')  || '').trim();
        var role   = (p.get('role')    || '').trim().toLowerCase();
        var hndl   = (p.get('handle')  || '')
          .replace(/[\x00-\x1f\x7f]/g, '').slice(0, 32);
        // ?join_handle is host-only: the broker-approved joiner's
        // handle, used by the host's auto-accept gate to skip the
        // prompt modal.  On the joiner side this param is ignored.
        var jhndl  = (p.get('join_handle') || '')
          .replace(/[\x00-\x1f\x7f]/g, '').slice(0, 32);
        var code   = (p.get('code')    || '').trim();

        var idOk    = /^[0-9a-f-]{32,40}$/i.test(sid);
        var tokOk   = /^[0-9a-f]{16,64}$/i.test(token);
        var intOk   = !intent || /^[0-9a-f-]{32,40}$/i.test(intent);
        var roleOk  = (role === 'host' || role === 'joiner');
        var codeOk  = !code || /^[0-9a-fA-F]+$/.test(code);

        if (idOk && roleOk && intOk && codeOk &&
            (role === 'joiner' || tokOk) /* host requires token */) {
          window.__altirraLib.brokerMode = {
            sessionId:   sid,
            token:       token,
            intentId:    intent,
            handle:      hndl,
            joinHandle:  jhndl,
            codeHash:    code,
            role:        role,
          };
          // M3 first ship intentionally KEEPS the existing AutoHost
          // path active for role=host (publishes a fresh lobby
          // session); the broker session orphans until TTL or the
          // lobby's dedup-on-Create evicts it.  See the chrome-
          // suppression block in onRuntimeReady() for the rationale.
          log('broker mode active — role=' + role
              + ' session=' + sid.slice(0, 8) + '…');
        } else {
          log('broker URL rejected — malformed param(s); '
              + 'falling back to legacy flow');
        }
      }
    } catch (e) {
      log('URL parse failed —', e && e.message ? e.message : e);
    }
  }

  // ── 2. preRun fetch ──────────────────────────────────────────────
  function preRunFetch(Module, __wasmCliArgs) {
    var lib = window.__altirraLib;
    if (!lib) return;
    var hasLibPaths = !!(lib.paths && lib.paths.length);
    var hasFwPaths  = !!(lib.firmwarePaths && lib.firmwarePaths.length);
    if (!hasLibPaths && !hasFwPaths) return;

    Module.addRunDependency('library-fetch');

    var BASE = resolveBase('library', lib.embed);
    var FW_BASE = resolveBase('firmware', lib.embed);
    var DEST = '/home/web_user/games/library';
    var FW_DEST = '/home/web_user/firmware';
    var KIND = {
      atr:'disk', xfd:'disk', atx:'disk', pro:'disk', dcm:'disk',
      xex:'run',  com:'run',  exe:'run',
      car:'cart', a52:'cart', rom:'cart', bin:'cart',
      cas:'tape', wav:'tape',
    };
    try { Module.FS.mkdirTree(DEST); } catch (e) {}
    try { Module.FS.mkdirTree(FW_DEST); } catch (e) {}

    function basenameOf(p) {
      var i = p.lastIndexOf('/');
      return i < 0 ? p : p.substring(i + 1);
    }

    // Pre-allocate names in URL-source order so out-of-order Promise
    // resolutions can't scramble D1: vs D4: vs D2:.  The C-side argv
    // parser auto-increments the disk slot for each `--disk <path>`
    // it consumes (commandline_sdl3.cpp), so we just push the path —
    // pushing an explicit slot number would make the parser read "1"
    // as the path and fail with "Cannot open file '1'".
    var basenames = {};
    var entries = lib.paths.map(function (rel) {
      var b = basenameOf(rel);
      if (b in basenames) {
        var dot = b.lastIndexOf('.');
        var stem = dot >= 0 ? b.substring(0, dot) : b;
        var ext  = dot >= 0 ? b.substring(dot)    : '';
        b = stem + '_' + (basenames[b]++) + ext;
      }
      basenames[b] = (basenames[b] || 0) + 1;
      var ext  = (b.split('.').pop() || '').toLowerCase();
      var kind = KIND[ext] || '';
      return { rel: rel, vfs: DEST + '/' + b, kind: kind };
    });

    // Firmware ROMs land in /home/web_user/firmware with their original
    // basename (preserving the .rom extension is important — the firmware
    // scanner inspects extension AND content hash, and some recognised
    // ROMs only match when the file ends in .rom or .bin).  No CLI args
    // are pushed: ATUIDoFirmwareScan registers each detected type as the
    // type-default, and the simulator's hardware-mode boot resolves the
    // kernel/BASIC slot to the dropped ROM automatically.
    var fwBasenames = {};
    var fwEntries = (lib.firmwarePaths || []).map(function (rel) {
      var b = basenameOf(rel);
      if (b in fwBasenames) {
        var dot = b.lastIndexOf('.');
        var stem = dot >= 0 ? b.substring(0, dot) : b;
        var ext2 = dot >= 0 ? b.substring(dot)    : '';
        b = stem + '_' + (fwBasenames[b]++) + ext2;
      }
      fwBasenames[b] = (fwBasenames[b] || 0) + 1;
      return { rel: rel, vfs: FW_DEST + '/' + b };
    });

    // Push CLI args in source order NOW.  By the time main() reads
    // Module.arguments, the run-dependency below has been resolved →
    // every fetch has completed → every vfs path is on disk.
    for (var i = 0; i < entries.length; ++i) {
      var e = entries[i];
      if (e.kind === 'disk') {
        __wasmCliArgs.push('--disk', e.vfs);
      } else if (e.kind === 'run') {
        __wasmCliArgs.push('--run', e.vfs);
      } else if (e.kind === 'cart') {
        __wasmCliArgs.push('--cart', e.vfs);
      } else if (e.kind === 'tape') {
        __wasmCliArgs.push('--tape', e.vfs);
      } else {
        log('unrecognised extension, not booting:', basenameOf(e.rel));
      }
      lib.vfsPaths.push(e.vfs);
    }

    var fetched = 0, cached = 0, refetched = 0;
    function makeFetchPromise(e, baseUrl) {
      // Cache-validation flow:
      //   1. If no file is on disk yet → straight fetch.
      //   2. Otherwise HEAD the origin to learn the current ETag /
      //      Content-Length, compare against our on-disk state, and
      //      either skip (still current) or re-GET (file moved on).
      //
      // We persist the ETag returned by the most recent successful GET
      // alongside the file (`<vfs>.etag` sidecar in IDBFS) so subsequent
      // visits can do the cheap ETag equality check.  Falling back to
      // Content-Length covers (a) profiles that pre-date the sidecar
      // and (b) servers / proxies that strip ETag from HEAD responses.
      //
      // HEAD uses `cache: 'no-cache'` so we always revalidate with the
      // origin — without it the browser would happily return the same
      // stale headers it cached the day the user first played the
      // game, defeating the whole point.  HEAD on a static asset is a
      // few hundred bytes; the bandwidth saving on a >10 KB game file
      // when nothing changed (the common case) more than pays for it.
      var encRel = e.rel.split('/').map(encodeURIComponent).join('/');
      var url = baseUrl + encRel;
      var etagPath = e.vfs + '.etag';

      var onDiskSize = 0;
      try {
        var st = Module.FS.stat(e.vfs);
        if (st) onDiskSize = st.size;
      } catch (_) {}

      var savedETag = null;
      try {
        var raw = Module.FS.readFile(etagPath, { encoding: 'utf8' });
        if (raw) savedETag = String(raw).trim() || null;
      } catch (_) {}

      function writeFile(buf, etag) {
        try {
          Module.FS.writeFile(e.vfs, new Uint8Array(buf));
        } catch (err) {
          log('writeFile failed for', e.vfs, '—',
              err && err.message ? err.message : err);
          throw err;
        }
        if (etag) {
          try { Module.FS.writeFile(etagPath, etag); } catch (_) {}
        }
      }

      function doFetch(reasonLabel) {
        return fetch(url, { cache: 'no-cache' })
          .then(function (r) {
            if (!r.ok) throw new Error('HTTP ' + r.status + ' for ' + e.rel);
            var freshETag = r.headers.get('etag');
            return r.arrayBuffer().then(function (buf) {
              writeFile(buf, freshETag);
              if (reasonLabel === 'refetch') refetched++; else fetched++;
            });
          })
          .catch(function (err) {
            log('fetch failed for', e.rel, '—',
                err && err.message ? err.message : err);
          });
      }

      if (onDiskSize <= 0) {
        return doFetch('initial');
      }

      // On-disk file exists — HEAD the origin to confirm it's still
      // current.  Any HEAD failure (CORS, 405, offline, ...) falls
      // back to "trust the cache" so an offline reload still boots.
      return fetch(url, { method: 'HEAD', cache: 'no-cache' })
        .then(function (r) {
          if (!r.ok) { cached++; return; }
          var serverETag = r.headers.get('etag');
          var clHeader   = r.headers.get('content-length');
          var serverSize = clHeader != null ? parseInt(clHeader, 10) : NaN;

          var etagMatch = !!(savedETag && serverETag
                             && savedETag === serverETag);
          var sizeMatch = (!isNaN(serverSize)
                           && serverSize === onDiskSize);

          // Best signal: matching ETags on both sides.
          if (etagMatch) {
            cached++;
            return;
          }

          // ETag check inconclusive (server omits the header, or this
          // profile has no sidecar yet) — fall back to size.  Same
          // size + missing ETag side = same file.  When we land here
          // because the profile lacked a sidecar, opportunistically
          // store the server's ETag so the next visit can use the
          // ETag-only fast path.
          if (sizeMatch && (!serverETag || !savedETag)) {
            cached++;
            if (serverETag && !savedETag) {
              try { Module.FS.writeFile(etagPath, serverETag); } catch (_) {}
            }
            return;
          }

          var reason = (savedETag && serverETag)
            ? 'ETag ' + savedETag + ' → ' + serverETag
            : 'size ' + onDiskSize + ' → ' + serverSize;
          log('refetching', e.rel, '—', reason);
          return doFetch('refetch');
        })
        .catch(function (err) {
          log('HEAD check failed for', e.rel,
              '— keeping cached file:',
              err && err.message ? err.message : err);
          cached++;
        });
    }

    var promises = entries.map(function (e) {
      return makeFetchPromise(e, BASE);
    }).concat(fwEntries.map(function (e) {
      return makeFetchPromise(e, FW_BASE);
    }));

    var totalCount = entries.length + fwEntries.length;
    Promise.all(promises).then(function () {
      log('lib+firmware: ' + fetched + ' fetched + ' + refetched
          + ' refetched + ' + cached + ' cached / ' + totalCount
          + ' file(s)'
          + ' — argv now has ' + __wasmCliArgs.length + ' arg(s)');
      // No explicit ATWasmRescanFirmware call here: the wasm runtime
      // hasn't initialised yet (we're still inside a preRun promise
      // chain), so Module._* exports may be undefined.  The rescan
      // that wasm_index.html.in runs unconditionally in
      // onRuntimeInitialized executes AFTER our run-dependency has
      // been released — which means our fetched ROMs are on disk by
      // then and get hashed + type-default-registered before main()
      // boots the simulator.
      //
      // Best-effort flush so the fetched files persist in IDBFS.
      try {
        if (Module.FS && Module.FS.syncfs)
          Module.FS.syncfs(false, function () {});
      } catch (e) {}
      Module.removeRunDependency('library-fetch');
    });
  }

  // ── 3. onRuntimeReady — register source + (optional) auto-host ──
  function onRuntimeReady(Module) {
    var lib = window.__altirraLib;
    var hasLib = !!(lib && lib.paths && lib.paths.length);

    // Mode-of-entry contract — set first, every time:
    //   ?lib=…           (Play Solo / Play Together)  → Gaming Mode
    //   ?s=…             (Join via lobby invite)      → Gaming Mode
    //   /AltirraSDL/play/ (Start Atari Emulator)      → Desktop Mode
    //
    // The Join path is critical: the C-side netplay deep-link handler
    // already calls ATUISetMode(Gaming) when it sees --join-session,
    // but that runs from a netplay tick AFTER main() returned.  This
    // JS hook fires later still, on the same browser turn, so without
    // recognising ?s= here we'd reset Gaming → Desktop and the
    // DeepLinkPrep renderer (which lives only in the Gaming-Mode
    // dispatcher) would never run — the user would land in Desktop UI
    // and have to manually open Browse Hosted Games to complete the
    // join.  Detecting ?s= directly here keeps the C and JS sides
    // pointing in the same direction.
    var p;
    try { p = new URLSearchParams(window.location.search || ''); }
    catch (e) { p = null; }
    var hasJoin   = !!(p && (p.get('s') || '').trim());
    var hasBroker = !!(lib && lib.brokerMode);
    var wantsGaming = hasLib || hasJoin || hasBroker;
    if (Module._ATWasmSetGamingMode) {
      try { Module._ATWasmSetGamingMode(wantsGaming ? 1 : 0); } catch (e) {}
    }

    // Embed-kit display overrides — applied AFTER the gaming-mode
    // setter so an explicit ?crt=0 wins over the gaming-mode default
    // preset (Balanced: bilinear filter + vignette).  Apply order
    // matters: CRT first because flipping it rewrites filter +
    // artifact mode (off=Efficient → Point + None, on=Quality →
    // SharpBilinear + Auto), then filter/artifact let an embed
    // author override one knob without disabling the rest of the
    // CRT look (e.g. ?crt=1&filter=point keeps scanlines + bloom but
    // pins the upscale to nearest-neighbour for crisp text).
    if (lib.crt !== null && Module._ATWasmSetCRTEnabled) {
      try { Module._ATWasmSetCRTEnabled(lib.crt ? 1 : 0); } catch (e) {}
    }
    if (lib.filter !== null && Module._ATWasmSetDisplayFilter) {
      try { Module._ATWasmSetDisplayFilter(lib.filter); } catch (e) {}
    }
    if (lib.artifact !== null && Module._ATWasmSetArtifactMode) {
      try { Module._ATWasmSetArtifactMode(lib.artifact); } catch (e) {}
    }
    if (lib.vkbd !== null && Module._ATWasmSetVirtualKeyboard) {
      try { Module._ATWasmSetVirtualKeyboard(lib.vkbd ? 1 : 0); } catch (e) {}
    }

    // Broker mode shows the "Starting…" overlay until lockstep so
    // the user sees something happening while the netplay handshake
    // runs.  Set BEFORE the broker session adoption so the first
    // frame after spawn already has the overlay visible.
    if (hasBroker && Module._ATWasmSetStartingOverlay) {
      try { Module._ATWasmSetStartingOverlay(1); } catch (e) {}
    }
    if (!hasLib && !hasBroker) return;

    // Register the staging dir with the in-emulator Game Library so
    // titles fetched via deep-link show up alongside wizard-installed
    // packs.  Idempotent on the C side.
    if (Module._ATWasmRegisterGamePackSource) {
      var dir = '/home/web_user/games/library';
      try { Module.FS.mkdirTree(dir); } catch (e) {}
      var len = Module.lengthBytesUTF8(dir) + 1;
      var ptr = Module._malloc(len);
      try {
        Module.stringToUTF8(dir, ptr, len);
        Module._ATWasmRegisterGamePackSource(ptr);
        log('registered ' + dir + ' with Game Library');
      } finally { Module._free(ptr); }
    }

    // Broker-mode adoption (M3.4): the broker page pre-created a
    // /v1/sessions entry and supplied (sessionId, token, intent,
    // handle, code) on the URL.  When the new ATWasmAdoptBrokerSession
    // export is available, stash all five strings into the C side so
    // StartCoordForHostedGame can adopt the broker session in place
    // — no Create call, no orphan, no joiner-side polling.
    //
    // For backward compat with a transition build that lacks the new
    // export but has the old chrome-only ATWasmSetBrokerActive: fall
    // back to the chrome-suppression-only call so the user at least
    // gets gaming-mode + no-wizard.  Adoption requires the new export.
    var brokerAdopted = false;
    if (hasBroker && Module._ATWasmAdoptBrokerSession) {
      var bm = lib.brokerMode;
      var roleNum = (bm.role === 'host') ? 1 : 0;
      var alloc = function(s) {
        s = s || '';
        var n = Module.lengthBytesUTF8(s) + 1;
        var p = Module._malloc(n);
        Module.stringToUTF8(s, p, n);
        return { ptr: p, bytes: n };
      };
      var sidA = alloc(bm.sessionId);
      var tokA = alloc(bm.token);
      var iidA = alloc(bm.intentId);
      // joinerHandle slot semantics:
      //   role=host   → bm.joinHandle (the broker-approved joiner's
      //                 handle, for the host's auto-accept gate)
      //   role=joiner → empty (joiner side doesn't read this)
      // ownNickname slot:
      //   both roles → bm.handle (the user's typed name, applied to
      //                ResolvedNickname for NetHello / hostHandle).
      // Without this asymmetry the host's auto-accept gate compared
      // its own handle against the joiner's NetHello and never
      // matched, leaving the prompt modal up until the 20-s
      // auto-decline fired (joiner saw reason code 8).
      var joinerHandleStr = (bm.role === 'host') ? bm.joinHandle : '';
      var hndA  = alloc(joinerHandleStr);
      var codA  = alloc(bm.codeHash);
      var nickA = alloc(bm.handle);
      try {
        Module._ATWasmAdoptBrokerSession(
          sidA.ptr, tokA.ptr, iidA.ptr, hndA.ptr, codA.ptr,
          nickA.ptr,
          roleNum, 1);
        brokerAdopted = true;
        log('broker-mode adopted (role=' + bm.role
            + ' session=' + (bm.sessionId || '').slice(0, 8) + '…'
            + ' nick=' + (bm.handle || '<none>')
            + ' joinHandle=' + (joinerHandleStr || '<n/a>') + ')');
      } catch (e) {
        log('broker-mode adopt failed:',
            e && e.message ? e.message : e);
      } finally {
        Module._free(sidA.ptr);  Module._free(tokA.ptr);
        Module._free(iidA.ptr);  Module._free(hndA.ptr);
        Module._free(codA.ptr);  Module._free(nickA.ptr);
      }
    } else if (hasBroker && Module._ATWasmSetBrokerActive) {
      var bm = lib.brokerMode;
      var roleNum = (bm.role === 'host') ? 1 : 0;
      try {
        Module._ATWasmSetBrokerActive(roleNum, 1);
        log('broker-mode active — chrome suppression only '
            + '(adopt export missing; falling back to legacy AutoHost)');
      } catch (e) {
        log('broker-mode set failed:', e && e.message ? e.message : e);
      }
    } else if (hasBroker) {
      log('broker-mode: no broker exports present — '
          + 'wizard suppression unavailable');
    }

    // Auto-host (Play Together): one call into the C export, which
    // stashes the request for the netplay tick's DriveAutoHost
    // driver to fire once the boot lands.  No polling here.
    //
    // Always call this when ?host=1 is present — including under
    // broker adoption.  ATWasmAutoHostNetplay does NOT publish any
    // session itself (see wasm_bridge.cpp:219-227); it only
    // RequestAutoHost-stashes the title/path so DriveAutoHost can
    // call StartHostingAction once the boot lands, which in turn
    // creates the HostedGame entry the reconcile loop walks.  The
    // adoption short-circuit lives downstream in
    // StartCoordForHostedGame, which checks ATWasmBrokerIsActive()
    // and skips PostLobbyCreate, calling OnLobbyCreateSucceeded
    // directly with the broker-supplied (sessionId, token).
    // Skipping AutoHost here was a regression: no AutoHost ⇒ no
    // HostedGame ⇒ no StartCoordForHostedGame ⇒ no StartHostWss ⇒
    // joiner times out at 25 s with "no host responded".
    if (lib.autoHost && Module._ATWasmAutoHostNetplay) {
      var title = lib.hostTitle || '';
      if (!title && lib.vfsPaths.length) {
        var i = lib.vfsPaths[0].lastIndexOf('/');
        title = i < 0 ? lib.vfsPaths[0]
                      : lib.vfsPaths[0].substring(i + 1);
      }
      var primary = lib.vfsPaths.length ? lib.vfsPaths[0] : '';
      var tBytes = Module.lengthBytesUTF8(title)   + 1;
      var pBytes = Module.lengthBytesUTF8(primary) + 1;
      var tPtr = Module._malloc(tBytes);
      var pPtr = Module._malloc(pBytes);
      try {
        Module.stringToUTF8(title,   tPtr, tBytes);
        Module.stringToUTF8(primary, pPtr, pBytes);
        var rc = Module._ATWasmAutoHostNetplay(tPtr, pPtr);
        log('auto-host stashed (rc=' + rc + ', title="' + title
            + '", primary="' + primary + '")');
      } finally {
        Module._free(tPtr);
        Module._free(pPtr);
      }
    } else if (lib.autoHost) {
      log('auto-host: export missing — skipping (cmake EXPORTED_FUNCTIONS '
          + 'should include _ATWasmAutoHostNetplay)');
    }
  }

  window.ATLibDeepLink = {
    parseUrl:        parseUrl,
    preRunFetch:     preRunFetch,
    onRuntimeReady:  onRuntimeReady,
  };
})();
