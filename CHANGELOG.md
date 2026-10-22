# AltirraSDL changes

## Unreleased — 2026-10-22

### Desktop frontend and Android recovery

- Added **Confirm on Exit** under **Configure System → Emulator → UI**
  (also `ui.confirm_exit` in Advanced Configuration). It defaults to on;
  disabling it lets IDE launches close without a confirmation, including
  when unsaved changes exist. The preference survives restarts and applies
  across machine profiles.
- Added portable `.altfx` custom display effects on SDL_GPU and
  OpenGL/GLES, including multiple passes, scaling, reload and profile
  persistence. Invalid replacements retain the working effect. Native
  Windows `.cgp` presets require conversion; see
  [custom display effects](docs/custom-display-effects.md).
- Added emulated-screen text publication through Windows UI Automation,
  macOS accessibility and Linux AT-SPI, with provider status and Linux
  reconnect handling. See [screen-reader support](docs/emulated-screen-accessibility.md)
  for dependencies and platform limits. Fixed the Windows adapter's COM
  header prerequisites so the release build compiles.
- Restored unsaved-media, BASIC-overlap and incompatible-image decisions.
  Cancelling preserves the original profile, live settings, media objects
  and running/paused state. Native file-picker completions are processed
  on the emulator thread, and failed URL loads preserve the current session.
- Connected Hold Keys for Reset, pointer auto-hide/confinement and
  tablet/light-pen overlays to runtime behavior. CPU-history changes reach
  existing peripheral CPUs, and automation exit requests close the application.
- Improved Android background/foreground recovery and preserved suspended
  game sessions. Repaired Android's cached SDL revision checks; device
  testing is still needed for the reported Pixel 7 black-screen scenario.
  See the [background-recovery notes](docs/android-background-recovery.md).

### Online play

- Added per-hosted-game **Controller layout**: **Two joysticks — one per
  player** (default), or **Shared joystick — take turns** for digital
  joystick games using port 1 for both players. The host sends the layout
  to the guest, and rollback/resync preserve it. Protocol 9 requires both
  players to update; the web Lobby keeps its existing controls.
- Fixed joystick directions and fire being suppressed during online play.
  Installing discard redirects for other local controllers no longer
  removes joystick-one capture. Each player's local joystick one reaches
  their assigned emulated port; input mappings survive rebuilds and return
  to normal when the session ends.
- Fixed game transfer stalling when the final chunk acknowledgement is
  lost. The receiver acknowledges valid retries after download/apply
  without restarting the game or applying the same state twice.
- Added bounded rollback with two frames of input delay, plus a delay-only
  compatibility mode selected before hosting. Corrected frames replay from
  native checkpoints without duplicate audio submission.
- Fixed changing inputs under latency, retransmission during stalls,
  historical packet repair, long-session hash cursors and deferred host
  approval/upload races. Protocol 8 requires both peers to update.
- Preserve exact runtime clocks, random state, keyboard/analog input and
  versioned session disk writes when rewinding. Restore the original local
  game, media objects and running state on leaving online play. Pending
  XEX/BASIC loaders, startup key holds, CPU interrupt phases and drive/SIO
  buffers now survive native checkpoints and serialized resyncs. Fixed
  mid-scanline ANTIC DMA restoration and banked BASIC/self-test ROM latches.
- Authenticate lobby relay routes with private role tickets bound to the
  join attempt; public names and session IDs cannot claim another host's
  listing or relay slot. Browser-host relay admission also requires the
  owner's approved join intent. Recover expired registrations and separate
  authenticated offer heartbeats from public request limits.
- Use HTTPS-capable native lobby requests and advertised relay endpoints.
  Cancelled browser joins cannot resume after a delayed response.

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
- Failed image parsing now preserves mounted media before a boot attempt,
  including malformed disks inside downloaded ZIP and gzip archives. ZIP
  validation also checks the actual decoded member length before its CRC.
- Added Game Library → Sources → Add from URL and the matching Gaming
  Mode library action. Games and multi-gigabyte ZIP/ZIP64 packs stream to a
  chosen disk folder, with progress, cancellation and background importing.
  Archives remain intact and are indexed without extracting the collection;
  completed sources and the download destination survive restarts. POSIX
  builds use 64-bit file offsets, including 32-bit Android.
- Android explicitly permits user-selected HTTP game/library URLs and HTTP
  download redirects while retaining normal HTTPS certificate validation.
- Fixed the insertion index reported when a newly attached video output
  sorts before existing outputs. Expanded presentation regressions for
  XEP80, Bit 3, 1090 and AF80, including valid programmed rasters,
  computer-video pass-through, paused switching and device removal.

### Help

- Added offline setup and troubleshooting guides for FujiNet, RAM-Cart,
  SiDiCar, Mosaic, AF80/80-column video and URL media/game packs. FujiNet
  and URL dialogs link directly to their guides, including in Gaming Mode.
  Narrow windows show articles at full width with a Contents toggle.

### Regression coverage

- Added memory-bus, cartridge-image and serialized save-state tests for
  all ten writable cartridge mappings, plus physical-switch profile
  restoration.
- Added Mosaic bank/ROM/reset/ANTIC tests and AF80 firmware, memory,
  rendering, cartridge-gating and serialized save-state tests.
- Added local HTTP tests for redirects, limits, archives, cancellation,
  malformed responses and cache preservation, plus native image-loader
  integration tests. HTML error pages with a UTF-8 BOM or leading comments
  are rejected even when served under an image filename.
- Added streamed HTTP and ZIP64 regressions, including sparse offsets above
  4 GiB, malformed indexes, sink failures and 64-bit progress accounting.
- Preserve advertised download sizes when libcurl rejects an oversized
  response before body transfer begins.
- Follow HTTP redirects without draining large or stalled intermediate
  bodies. Windows uses the same bounded redirect and cancellation behavior,
  and supports long signed download URLs.
- Included the new production-code regression targets in Linux normal
  and sanitizer CI and native Windows/macOS regression jobs.

### FujiNet and NetSIO

- Automatically select and remember a free local web port when the preferred
  port is occupied. Report web-server startup failures even if NetSIO still
  connects, and include the actual URL in mount API errors instead of
  treating every HTTP 404 as an outdated runtime.
- Added **Repair automatic setup** to reconnect the managed runtime using
  local defaults without deleting host slots, disk settings or virtual SD
  files, reinstalling FujiNet-PC, or rebooting the current Atari game.
  Pending repairs can be cancelled and cannot cross profile changes/netplay.
- Added **Network games / Lobby** and **BBS directory** to Tools → FujiNet.
  They launch the official Atari clients through the managed FujiNet-PC
  runtime, starting it when needed, downloading and mounting the client
  read-only, then booting through real SIO. Local media, unsaved disk
  changes and BASIC are preserved. FujiNet multiplayer is separate from
  AltirraSDL's Online Play.
- Added automatic download, published-checksum verification and private
  installation of official FujiNet-PC packages, with dependency checks,
  retry and manual-download recovery. Optional startup launches the
  configured runtime; Gaming Mode leaves the running Atari unchanged.
- Made setup the first-use action. Once configured, Lobby and BBS lead
  the window and **Setup and full controls** collapses the setup options.
  Launches show progress and cancellation. Fixed manual setup failing to
  boot after an unsuccessful activity launch; existing external-device
  and custom-bridge workflows remain available.
- Added activity-flow regressions to Linux CI and live smoke tests with
  the official runtime. The live checks reach the Lobby's player-name
  prompt and populated BBS directory with BASIC on and off; they do not
  join public games or connect to boards. See [FujiNet verification](docs/fujinet.md#verification-and-references).
- Added a native UDP NetSIO device for FujiNet-PC and external FujiNet,
  with command/data synchronization, serial pacing, baud changes,
  PROCEED/INTERRUPT signals, packet credits, reset and reconnect handling.
  Local drives retain their own SIO acceleration. Network callbacks queue
  data for the emulation thread; bounded queues and transmit waits handle
  backpressure during fast emulation.
- Added Tools → FujiNet on the ImGui and native macOS menus. The panel
  configures a local or LAN listener, shows the peer and diagnostics, and
  optionally manages an extracted official FujiNet-PC runtime. Start and
  Boot waits for the connection before resetting the computer. Private
  configuration, SD data and bounded logs preserve the installed runtime
  and its distribution files. Expected runtime restarts are automatic.
- FujiNet boot disables the built-in D1 drive and Fast boot and selects
  normal speed while retaining mounted images and unsaved changes.
  Full-drive conflicts are reported. An exclusive runtime-data lock
  prevents concurrent managed processes from sharing the same SD/config.
  The web interface and data folder can be opened directly. Native Android
  can use an external FujiNet; browser UDP and local mobile process
  management limitations are explained in the panel.
- Fixed socket address comparison and UDP send-queue reporting on the
  portable and Windows backends, preventing silent loss when a queue fills.
- Fixed UDP retries after a temporary OS send failure and receive-queue
  capacity checks on Windows and portable builds. Portable sockets now
  rebuild readiness after socket-table updates, sleep while idle, and
  report TCP half-close EOF correctly.
- Serialize socket reference release with worker collection to prevent
  an asynchronous close from freeing a socket during its final release.
- Fixed rapid NetSIO disconnect/reconnect racing an asynchronous socket
  close. Managed runtimes can reconnect directly without rebooting the Atari.
- Improved FujiNet and URL dialogs when resizing the emulator window.
  FujiNet fields and actions fit narrow windows and large fonts, connection
  errors are prominent, and unavailable actions reflect the current state.
  The web-interface button uses the managed runtime's local port while it
  is running and restores the saved external URL when it stops.
- Reorganized FujiNet into **This computer** and **Other FujiNet** workflows,
  with a compact state-dependent action area and expandable runtime,
  connection, drive and diagnostic controls. Existing custom-device/Python-hub setups retain their
  own manual view. Native/legacy route conflicts and attempts to replace an
  existing external peer with a managed process are rejected before mutation.
  Mode preferences also persist when the configuration directory is new.
  Added a live legacy-bridge test option and descriptor-detection regressions.
  Detection follows the loaded custom-device VM, preserving manual ownership
  when its source is edited or deleted with hot reload disabled.
- Fixed peer restarts during pending NetSIO synchronization and reset the
  reported baud when the connection changes. Expanded real-time expiry,
  signal-release and stale-packet regressions.
- Fixed relative private-runtime paths and recursive SD seeding when private
  storage is below the release SD directory. Release-directory aliases are
  rejected before writing configuration or lock files.
- Added optional Guided setup and Run a file through FujiNet for the managed
  runtime. Local files or direct URLs are copied to its private SD, mounted
  read-only through the official API, verified, then booted through real SIO.
  A fresh SD ownership probe prevents mounting through another web service.
  Cancellation and profile changes suppress automatic boot.
- Clear the previous managed UDP peer before an automatic runtime restart,
  including when its final disconnect packet was lost.
- Added real UDP protocol, process ownership and UI regression tests,
  plus an opt-in end-to-end test using actual FujiNet-PC and Atari OS SIO
  calls for configuration boot, network/JSON, SD/TNFS and local-drive
  coexistence. See `docs/fujinet.md` for setup and verification.

- Fixed custom-device hot reload on portable builds: callback timers now
  run from the host loop while paused, and precise metadata detects rapid
  edits, replacement and deletion without newer sibling files hiding changes.
