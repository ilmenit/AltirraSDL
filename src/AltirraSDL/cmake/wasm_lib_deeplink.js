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
  };

  // Path validation: same-origin /AltirraSDL/library/<path> only.
  // Reject '..' segments, leading slashes, control chars, scheme://.
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
    } catch (e) {
      log('URL parse failed —', e && e.message ? e.message : e);
    }
  }

  // ── 2. preRun fetch ──────────────────────────────────────────────
  function preRunFetch(Module, __wasmCliArgs) {
    var lib = window.__altirraLib;
    if (!lib || !lib.paths || !lib.paths.length) return;

    Module.addRunDependency('library-fetch');

    var BASE = '/AltirraSDL/library/';
    var DEST = '/home/web_user/games/library';
    var KIND = {
      atr:'disk', xfd:'disk', atx:'disk', pro:'disk', dcm:'disk',
      xex:'run',  com:'run',  exe:'run',
      car:'cart', a52:'cart', rom:'cart', bin:'cart',
      cas:'tape', wav:'tape',
    };
    try { Module.FS.mkdirTree(DEST); } catch (e) {}

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

    var fetched = 0, cached = 0;
    var promises = entries.map(function (e) {
      // Stat-skip cached files (IDBFS persistence saves the ~80 KB
      // ATR re-download on every repeat Play Solo click).
      try {
        var st = Module.FS.stat(e.vfs);
        if (st && st.size > 0) { cached++; return Promise.resolve(); }
      } catch (_) {}

      var encRel = e.rel.split('/').map(encodeURIComponent).join('/');
      return fetch(BASE + encRel, { cache: 'force-cache' })
        .then(function (r) {
          if (!r.ok) throw new Error('HTTP ' + r.status + ' for ' + e.rel);
          return r.arrayBuffer();
        })
        .then(function (buf) {
          try {
            Module.FS.writeFile(e.vfs, new Uint8Array(buf));
            fetched++;
          } catch (err) {
            log('writeFile failed for', e.vfs, '—',
                err && err.message ? err.message : err);
            throw err;
          }
        })
        .catch(function (err) {
          log('fetch failed for', e.rel, '—',
              err && err.message ? err.message : err);
        });
    });

    Promise.all(promises).then(function () {
      log('library: ' + fetched + ' fetched + ' + cached
          + ' cached / ' + lib.paths.length + ' file(s)'
          + ' — argv now has ' + __wasmCliArgs.length + ' arg(s)');
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

    // Mode-of-entry contract — set first, every time, regardless of
    // whether the URL carries a ?lib= entry:
    //   ?lib=…           (Play Solo / Play Together)  → Gaming Mode
    //   /AltirraSDL/play/ (Start Atari Emulator)      → Desktop Mode
    // This honours the user's explicit spec ("Start Atari Emulator
    // should start the Desktop UI") even when the previous visit was
    // a deep-link that persisted Gaming Mode in the registry.  Users
    // can still flip via View → Switch to Gaming/Desktop Mode at any
    // time and that choice is saved for the next bare-URL visit.
    if (Module._ATWasmSetGamingMode) {
      try { Module._ATWasmSetGamingMode(hasLib ? 1 : 0); } catch (e) {}
    }
    if (!hasLib) return;

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

    // Auto-host (Play Together): one call into the C export, which
    // stashes the request for the netplay tick's DriveAutoHost
    // driver to fire once the boot lands.  No polling here.
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
