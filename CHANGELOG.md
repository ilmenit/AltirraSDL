# AltirraSDL changes

## Unreleased — 2026-10-21

### Hardware and media

- Added RAM-Cart 64/128 KiB, Double RAM-Cart 256 KiB, RAM-Cart
  1–32 MiB and SiDiCar 32 KiB cartridge mappings (CART IDs 94–103).
  Writable cartridge bytes survive save states and can be saved to CART
  images. Physical switches are available beside the existing cartridge
  switches and are stored per profile. Empty images can be created from
  File → Attach Special Cartridge.
- Added the Mosaic 64K Select bank-select memory expansion for the
  Atari 400/800, with 4–36 banks, configuration in Configure System →
  Devices, CPU/ANTIC access and save states. This models the bank-select
  configuration, rather than the separate 1200-style OS-under-RAM wiring.
- Added the Austin Franklin AF80 video board, including firmware banking,
  character and attribute RAM, split-screen addressing, text extraction,
  RGBI video, blinking, cursor and save states. Import its separate
  4 KiB firmware and 4 KiB character ROM in Firmware Manager; these ROMs
  are not included.
- Added File → Open Image from URL and HTTP(S) image arguments on the
  native command line. Downloads run in the background with progress,
  cancellation and errors, and use the normal image loader for local
  files, ZIP and gzip media. Downloaded media is retained in the
  configuration directory's `downloads` cache. Browser builds retain
  their existing From URL workflow. Android follows relative and
  HTTP/HTTPS redirects with a shared deadline and five-hop limit.
- Fixed the insertion index reported when a newly attached video output
  sorts before existing outputs. Expanded presentation regressions for
  XEP80, Bit 3, 1090 and AF80, including valid programmed rasters,
  computer-video pass-through, paused switching and device removal.

### Regression coverage

- Added memory-bus, cartridge-image and serialized save-state tests for
  all ten writable cartridge mappings, plus physical-switch profile
  restoration.
- Added Mosaic bank/ROM/reset/ANTIC tests and AF80 firmware, memory,
  rendering, cartridge-gating and serialized save-state tests.
- Added local HTTP tests for redirects, limits, archives, cancellation,
  malformed responses and cache preservation, plus native image-loader
  integration tests.
- Included the new production-code regression targets in Linux normal
  and sanitizer CI and native Windows/macOS regression jobs.
