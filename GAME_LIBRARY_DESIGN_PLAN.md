# Game Library & Welcome Screen — Design & Implementation Plan

## 1. Overview

The Game Library adds a welcome screen to Gaming Mode that displays a
scannable, cached collection of Atari games.  The user configures one or
more **game folders** and **game archives** (ZIPs); the library scans them
for loadable media, groups variants, matches game-art, and presents
everything in a gamepad/touch/mouse/keyboard-friendly browser.

The Atari VM does **not** boot until the user selects a game or
explicitly chooses "Boot Atari".

---

## 2. Data Model

### 2.1 Supported Extensions

The canonical extension list comes from `IsSupportedExtension()` in
`ui_mobile.cpp` — the mobile file browser's filter.  The game library
scanner must use the same set so that every file it lists is actually
loadable.  Note: `IsSupportedExtension()` is currently `static`
(file-local).  It must be extracted to a shared header (e.g.
`mobile_internal.h` or `game_library.h`) so the scanner can call it.

| Category    | Extensions                                    |
|-------------|-----------------------------------------------|
| Disk        | `.atr` `.atx` `.xfd` `.dcm`                  |
| Executable  | `.xex` `.obx` `.com` `.exe`                  |
| Cartridge   | `.car` `.rom` `.bin`                          |
| Cassette    | `.cas`                                        |
| Archive     | `.zip` `.gz` `.atz` (browsed into)            |

**Note:** The emulator's Windows build supports additional extensions
via its file-dialog filter in `main.cpp:1471`: `.pro` (protected disk),
`.a52` (5200 cartridge), `.bas` (BASIC program), `.arc` (archive),
`.wav`/`.flac`/`.ogg` (cassette audio), `.sap` (SAP music),
`.vgm`/`.vgz` (VGM music).  These should be added to
`IsSupportedExtension()` in a separate task so they become loadable in
Gaming Mode too; once added there, the scanner picks them up
automatically since it calls the same filter function.

### 2.2 Game Entry

A **GameEntry** is the atomic unit the library displays.  When multiple
files represent variants of the same game they are grouped under a single
entry (see §2.4).

```cpp
enum class GameMediaType {
    Disk,        // .atr .atx .xfd .dcm
    Executable,  // .xex .obx .com .exe
    Cartridge,   // .car .rom .bin
    Cassette,    // .cas
};

struct GameVariant {
    VDStringW     mPath;          // VFS path: filesystem path for loose files,
                                  // or ATMakeVFSPathForZipFile() result for
                                  // entries inside archives (loadable directly
                                  // via kATDeferred_BootImage)
    VDStringW     mArchivePath;   // ZIP filesystem path if inside archive;
                                  // empty for loose files (used for display and
                                  // cache invalidation, not for loading)
    GameMediaType mType;
    uint64_t      mFileSize;
    uint64_t      mModTime;       // for cache invalidation
    VDStringW     mLabel;         // variant label, e.g. L"v1, ATR" or L"v10, XEX"
};

struct GameEntry {
    VDStringW                mDisplayName;   // cleaned, disambiguated name (wide
                                             // for Unicode folder/file names;
                                             // convert to UTF-8 at ImGui boundary)
    std::vector<GameVariant> mVariants;      // ≥ 1; first is default
    VDStringW                mArtPath;       // resolved game-art image; empty if none
    uint64_t                 mLastPlayed;    // unix timestamp, 0 = never
    uint32_t                 mPlayCount;
    // Future: user rating, notes, manual override name
};
```

### 2.3 Game Source

```cpp
struct GameSource {
    VDStringW mPath;
    bool      mbIsArchive;    // folder vs ZIP/GZ/ATZ
};

struct GameLibrarySettings {
    bool mbRecursive      = true;   // scan subfolders (global, all folder sources)
    bool mbCrossFolderArt = false;  // match game-art from other source folders
    bool mbShowOnStartup  = true;   // show game browser instead of auto-boot
    int  mViewMode        = 1;      // 0=List, 1=Grid
};
```

### 2.4 Variant Grouping

Many game collections store multiple versions/formats of the same game.
Real-world Atari game packs follow two distinct archetypes:

**Archetype A — Directory-per-game** (e.g. Archiwum Gier):
```
#/Raid Over Moscow/
    Raid Over Moscow (v1).atr
    Raid Over Moscow (v1).atx
    Raid Over Moscow (v1,b).atr
    Raid Over Moscow (v10).xex
    Raid Over Moscow (v11).xex
#/0 Grad Nord/
    0 Grad Nord (s1,b,v1).atr
    0 Grad Nord (s1,b,v2).atr
    0 Grad Nord (s2,b).atr
```
Each game has its own subdirectory.  Tags are comma-separated metadata
tokens inside parentheses: version (`v1`), side (`s1`), bootable (`b`),
etc.

**Archetype B — Flat alphabetical** (e.g. Fandal):
```
Binaries/Games/B/
    Boulder Dash.png
    Boulder Dash.xex
    Boulder Dash (Chaos).png
    Boulder Dash (Chaos).xex
    Boulder Dash (FAJ Soft).png
    Boulder Dash (FAJ Soft).xex
Images/Games/0-9/
    221B Baker Street.png
    221B Baker Street - Disk 1.atr
    221B Baker Street - Disk 2.atr
```
Games are flat in letter directories (0-9, A, B, ...).  Each game file
sits next to a matching `.png` screenshot.  Parenthesized content is
part of the game name (author, region, variant), NOT a strippable tag.
Multi-disk games use a ` - Disk N` suffix.

**Grouping rules:**

1. **Strip extension** from filename to get the *base name*.
2. **Strip trailing variant tags** to get the *canonical name*.  A
   trailing parenthesized group `(...)` is stripped **only if every
   comma-separated token inside is a known variant token**.  Known
   tokens:
   - Version: `v\d+`
   - Side/part: `s\d+`, `p\d+`
   - Boot: `b`
   - Format: `cload`
   - Memory: `128`, `320`, `64`
   - OS: `osa`, `osb`
   - Status: `demo`, `source`, `fixed`
   - Video: `pal`, `ntsc`
   - Misc: `mj`, `g`, `doc`

   If **any** token is unrecognized (e.g. an author name like `Chaos`,
   a device name like `FujiNet`, a decimal version like `0.80`), the
   entire parenthesized group is kept as part of the canonical name.
   This prevents incorrect grouping of distinct games that happen to
   share a base name (e.g. `Boulder Dash (Chaos)` ≠ `Boulder Dash
   (FAJ Soft)`).

   Additionally strip:
   - Trailing ` - Disk \d+` suffix (Fandal multi-disk pattern, 1000+
     real files).
   - Trailing `\[.*\]` bracket groups (rare, ~70 files in Archiwum).

3. Files sharing the same canonical name **and** residing in the same
   parent directory (or same archive root) are grouped into one
   `GameEntry`.
4. Each file becomes a `GameVariant` with a label built from the
   stripped tag + extension (e.g. `"v1, ATR"`, `"s1·b·v2, ATR"`,
   `"Disk 1, ATR"`).
5. If only one variant exists, the label is just the extension
   (`"ATR"`, `"XEX"`).
6. Default variant: prefer the most recent version tag, and among equal
   tags prefer Disk > Executable > Cartridge > Cassette (matching
   typical user expectation).

**Coverage:** Against real collections (Archiwum Gier 48K entries,
Fandal 19K entries), the known-token approach correctly identifies 96%
of parenthesized variant tags while avoiding false positives on
author/region/device names that are part of the game identity.

### 2.5 Display Name Disambiguation

When the same base filename appears in different parent directories —
e.g. `Games/Robbo 2/robbo.xex` and `Games/Robbo 3/robbo.xex` — the
raw filename alone is ambiguous.

**Heuristic:**

1. After grouping, collect all entries whose canonical name is identical.
2. If duplicates exist, prepend the **immediate parent folder name**:
   `"Robbo 2 — Robbo"`, `"Robbo 3 — Robbo"`.
3. If still ambiguous (nested deeper), prepend two levels:
   `"Collection/Robbo 2 — Robbo"`.
4. Display name cleaning: replace underscores with spaces, apply
   title-case where the source is all-lowercase or all-uppercase.

### 2.6 Game-Art Matching

Game-art images (screenshots, boxart, title screens) are matched by
filename.  The most common real-world pattern is **inline art** (Fandal
collection): each game file sits next to a `.png` with the same base
name (`Boulder Dash.xex` + `Boulder Dash.png`).  This covers ~97% of
the Fandal collection's 7,000+ games with zero configuration.

- `"Air Raid.png"` matches any `GameEntry` whose canonical name is
  `"Air Raid"`, regardless of media extension.
- For multi-variant entries, the art matches the **canonical name**
  (after tag stripping), so `Raid Over Moscow.png` matches all of
  `Raid Over Moscow (v1).atr`, `Raid Over Moscow (v10).xex`, etc.
- Supported image extensions: `.png`, `.jpg`, `.jpeg`, `.bmp`, `.webp`.
- Search order:
  1. Same directory as the game file (handles inline art).
  2. A `media/` or `artwork/` or `images/` or `screenshots/` or
     `boxart/` subdirectory next to the game file.
  3. **(Optional, checkbox)** Cross-folder matching: scan all game
     source folders for a matching image name.  Off by default for
     performance; user enables via "Match game-art from other folders"
     in Game Library settings.
- First match wins (in search-order priority).
- Art path is cached in the library JSON alongside the entry.

---

## 3. Cache Format (JSON)

File: `~/.config/altirra/gamelibrary.json`

Separate from `settings.ini` to avoid bloating the main config.  JSON
chosen for human readability, easy metadata extension, and negligible
performance difference at typical library sizes (hundreds to low
thousands of entries).

The **authoritative** list of game sources lives in the registry (§8).
The JSON cache stores a snapshot of which sources were scanned and when,
so the scanner can detect added/removed sources without hitting the
registry.  If the two disagree, the registry wins and a rescan is
triggered.

```jsonc
{
  "version": 1,
  "lastScanTime": "2026-04-12T14:30:00Z",

  "scannedSources": [
    {
      "path": "/home/user/Atari/Games",
      "lastScanMtime": 1712340000
    },
    {
      "path": "/home/user/Atari/collection.zip",
      "lastScanMtime": 1712300000
    }
  ],

  "games": [
    {
      "displayName": "Rescue on Fractalus",
      "variants": [
        {
          "path": "/home/user/Atari/Games/Rescue on Fractalus.atr",
          "archivePath": "",
          "type": "disk",
          "fileSize": 92176,
          "modTime": 1712345678,
          "label": "ATR"
        },
        {
          "path": "/home/user/Atari/Games/Rescue on Fractalus.xex",
          "archivePath": "",
          "type": "executable",
          "fileSize": 34212,
          "modTime": 1712345700,
          "label": "XEX"
        }
      ],
      "artPath": "/home/user/Atari/Games/artwork/Rescue on Fractalus.png",
      "lastPlayed": 1712400000,
      "playCount": 5
    },
    {
      "displayName": "Robbo 2 — Robbo",
      "variants": [
        {
          "path": "/home/user/Atari/Games/Robbo 2/robbo.xex",
          "archivePath": "",
          "type": "executable",
          "fileSize": 16384,
          "modTime": 1712000000,
          "label": "XEX"
        }
      ],
      "artPath": "",
      "lastPlayed": 0,
      "playCount": 0
    }
  ]
}
```

**Cache lifecycle:**

| Event | Action |
|-------|--------|
| Startup | Load JSON from disk → display instantly |
| After load | Spawn background scan thread |
| Scan complete | Diff with cached list, merge play history, save JSON |
| Game launched | Update `lastPlayed`/`playCount`, save JSON |
| Source added/removed | Rescan immediately, save JSON |
| Settings change (art matching) | Re-resolve art paths, save JSON |

---

## 4. Background Scanner

### 4.1 Thread Architecture

```
Main Thread                         Scanner Thread
    │                                    │
    ├── Load gamelibrary.json            │
    ├── Display cached entries           │
    ├── Create scanner, pass sources ──> Start
    │                                    ├── For each folder source:
    │                                    │   ├── Stat directory mod-time
    │                                    │   ├── If unchanged, skip (use cache)
    │                                    │   ├── If changed, walk recursively
    │                                    │   │   ├── Filter by supported extensions
    │                                    │   │   ├── Collect image files for art
    │                                    │   │   └── Build raw file list
    │                                    │   └── Group into GameEntries
    │                                    ├── For each archive source:
    │                                    │   ├── Stat archive mod-time
    │                                    │   ├── If changed, list ZIP contents
    │                                    │   └── Filter & group
    │                                    ├── Disambiguate display names
    │                                    ├── Match game-art
    │                                    │
    │  <── mutex-protected result ──────  Done
    │                                    │
    ├── Lock mutex, swap new entries     │
    ├── Merge play history from old      │
    ├── Save gamelibrary.json            │
    └── Refresh UI                       │
```

### 4.2 Thread Safety

- Scanner produces a `std::vector<GameEntry>` with zero play-history
  data (it doesn't read `lastPlayed`/`playCount`).
- Main thread owns all play-history state.
- On completion: main thread locks a mutex, takes the new entry list,
  and merges play history by matching on canonical name + variant paths.
- Scanner never touches the simulator, ImGui, or any shared UI state.
- Communication: `std::mutex` + `std::atomic<bool> mScanComplete`.
  Main thread checks the atomic each frame (zero-cost when false).

### 4.3 Incremental Scan

For large libraries, avoid full rescans where possible:

- **ZIP archives:** Stat the archive file itself.  If the archive's
  mtime is unchanged since last scan, reuse cached entries — this is
  reliable because a ZIP is a single file.
- **Folders:** Directory mtime on Linux only updates when files are
  added/removed in *that specific directory*, **not** in subdirectories.
  A file added to `Games/NewFolder/game.xex` does NOT change
  `Games/`'s mtime.  Therefore, for recursive folder sources, mtime
  alone is unreliable.  Strategy:
  1. **Fast path:** Walk the directory tree collecting file paths and
     mtimes (cheap — no file I/O beyond `stat()`).
  2. **Diff against cache:** Compare the collected path+mtime set with
     cached entries for that source.  If identical → no changes, skip
     grouping/art-matching.  If different → re-group only the affected
     entries.
  3. For very large folders (10k+ files), cache the path+mtime set
     itself alongside the game entries, so the diff can short-circuit
     without re-walking on subsequent scans when nothing changed.
- New files → add.  Missing files → remove.  Changed mtime → update.

---

## 5. UI Design

### 5.1 Screen Flow

```
App Launch (Gaming Mode)
    │
    ├── Load game library cache
    │
    ├── Game sources configured?
    │   ├── Yes → Game Browser (cached entries + background scan)
    │   └── No  → Game Browser (empty state with setup prompt)
    │
    ├── Game Browser actions:
    │   ├── Select game (single variant) ──→ Load → ColdReset → Resume
    │   ├── Select game (multi-variant)  ──→ Variant picker → Load
    │   ├── "Boot Atari" ──────────────────→ ColdReset → Resume (no media)
    │   ├── Settings gear ─────────────────→ Settings (Game Library page)
    │   ├── View toggle ───────────────────→ Switch List ↔ Grid
    │   └── Hamburger ─────────────────────→ Existing hamburger menu
    │
    └── After game loads → Normal emulation (None screen)
        └── Hamburger → "Game Library" returns to browser
```

### 5.2 Top Bar

```
┌──────────────────────────────────────────────────────────┐
│  ALTIRRA              [🔍 Search]  [≡ List|⊞ Grid]  [⚙] │
└──────────────────────────────────────────────────────────┘
```

- **Title:** "ALTIRRA" left-aligned.
- **Search:** Text filter — filters entries by display name substring.
  On gamepad: pressing a letter-key or dedicated search button opens an
  on-screen keyboard or jump-to-letter overlay.
- **View toggle:** Switches between List and Grid.  Persisted in
  settings.  Keyboard shortcut: Tab.
- **Settings gear:** Opens Game Library settings page.

### 5.3 Grid View (Default)

```
┌──────────────────────────────────────────────────────────────┐
│  ALTIRRA                              [🔍]  [≡] [⊞]  [⚙]   │
│──────────────────────────────────────────────────────────────│
│                                                              │
│  LAST PLAYED                                                 │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐       │
│  │          │ │          │ │          │ │          │  ···   │
│  │  [art    │ │  [type   │ │  [art    │ │  [type   │       │
│  │   or     │ │  color   │ │   or     │ │  color   │       │
│  │  color]  │ │  block]  │ │  color]  │ │  block]  │       │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘       │
│   Rescue on    Star Raid…   River Raid   Montezum…          │
│                                                              │
│  ALL GAMES                                                   │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐       │
│  │          │ │          │ │          │ │          │       │
│  │  [art    │ │  [type   │ │  [art    │ │  [type   │  ···  │
│  │   or     │ │  color   │ │   or     │ │  color   │       │
│  │  color]  │ │  block]  │ │  color]  │ │  block]  │       │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘       │
│   Archon       Ballblaz…   Boulder D…   Bruce Lee           │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐       │
│  │          │ │          │ │          │ │          │       │
│  ···                                                         │
│                                                              │
│                                     [Boot Atari without game]│
└──────────────────────────────────────────────────────────────┘
```

**Tile rendering:**

- Tile aspect ratio: 4:3 (matching Atari screen ratio) — good for
  screenshots; adequate for boxart.
- Columns: calculated to fit 4–6 tiles across the window width with
  comfortable padding.
- If game-art exists: render the image scaled to fill the tile.
- If no game-art: render a colored rectangle with a centered type label.

| GameMediaType | Color  | Label |
|---------------|--------|-------|
| Disk          | #4A90D9 (blue)   | ATR / ATX / etc. |
| Executable    | #7BC67E (green)  | XEX / OBX / etc. |
| Cartridge     | #E8A838 (orange) | CAR / ROM / etc. |
| Cassette      | #C084D8 (purple) | CAS |

- Game name below tile: centered, single line, truncated with ellipsis.
- Multi-variant indicator: small badge or overlay count (e.g. "×6")
  in the tile corner when a game has multiple variants.
- Selected tile: highlighted border (accent color) + subtle scale-up
  (1.05×).  On touch/mouse: highlight on hover.

**"Last Played" section:**

- Shows up to 8–12 most recently played games, sorted by `lastPlayed`
  descending.
- Single row, scrollable horizontally if needed.
- Hidden entirely if no games have been played yet.

**"All Games" section:**

- Full wrapped grid, alphabetically sorted by display name.
- Continuous vertical scroll.

### 5.4 List View

```
┌──────────────────────────────────────────────────────────────┐
│  ALTIRRA                              [🔍]  [≡] [⊞]  [⚙]   │
│──────────────────────────────────────────────────────────────│
│                                                              │
│  LAST PLAYED                                                 │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  ■ ATR  Rescue on Fractalus               ×2  2m ago│   │
│  │  ■ CAR  Star Raiders                      ×6  1h ago│   │
│  │  ■ XEX  River Raid                        ×1  yday  │   │
│  ├──────────────────────────────────────────────────────┤   │
│  │                                                      │   │
│  │  ALL GAMES (347)                                     │   │
│  │  ■ ATR  Archon                                   ×3  │   │
│  │  ■ XEX  Ballblazer                               ×1  │   │
│  │  ■ ATR  Boulder Dash                              ×2  │   │
│  │  ■ CAR  Bruce Lee                                 ×1  │   │
│  │  ■ ATR  Caverns of Mars                           ×1  │   │
│  │  ■ XEX  Choplifter                                ×4  │   │
│  │  ...                                                  │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
│                                     [Boot Atari without game]│
└──────────────────────────────────────────────────────────────┘
```

**Row layout:**

- Height: `dp(56)` — touch-friendly.
- Left: color-coded square indicator (media type color).
- Type badge: short extension label (ATR, XEX, CAR, etc.).
- Center: game display name.
- Right: variant count badge (×N, if >1), relative time (Last Played
  section only).
- Selected row: highlighted background.
- Divider between Last Played and All Games sections.

**"Last Played" section:**

- Top 3–5 rows, sorted by `lastPlayed` descending.
- Shows relative time: "2m ago", "1h ago", "yesterday", "3 days ago",
  "Apr 8", etc.
- Hidden if no games have been played.

### 5.5 Variant Picker

When a game with multiple variants is selected, a bottom sheet or
centered popup appears:

```
┌────────────────────────────────────┐
│  Raid Over Moscow                  │
│                                    │
│  ■ v1, ATR      92 KB             │
│  ■ v1, ATX     133 KB             │
│  ■ v1, CAS      48 KB             │
│  ■ v1, XEX      34 KB             │
│  ■ v10, XEX     35 KB             │
│  ■ v11, XEX     35 KB             │
│                                    │
│           [Cancel]                 │
└────────────────────────────────────┘
```

- Same gamepad/touch/keyboard navigation as the main list.
- Selecting a variant launches that specific file.
- If only one variant exists, skip this picker entirely — launch
  directly.

### 5.6 Empty State

When no game sources are configured:

```
┌──────────────────────────────────────────────────────────────┐
│  ALTIRRA                                             [⚙]    │
│──────────────────────────────────────────────────────────────│
│                                                              │
│                                                              │
│              No games in your library yet.                   │
│                                                              │
│         Add a folder or archive containing Atari             │
│         games to get started.                                │
│                                                              │
│              [+ Add Game Folder]                             │
│              [+ Add Game Archive]                            │
│                                                              │
│              [Boot Atari without game]                       │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### 5.7 Scan-in-Progress Indicator

While the background scanner runs, show a subtle indicator in the top
bar — a small spinner or pulsing dot next to the game count.  When the
scan completes and new games are found, briefly flash the updated count.
No modal, no blocking — the user interacts with cached data while the
scan runs.

---

## 6. Input & Navigation

### 6.1 Focus Model

One item is always "focused" (highlighted).  Focus is a single integer
index into the flattened, visible entry list.  The UI translates this
index to screen position and ensures it's visible.

### 6.2 Input Mapping

| Input | Action |
|-------|--------|
| **Gamepad D-pad Up/Down** | Move focus up (list) or up one row (grid) |
| **Gamepad D-pad Left/Right** | No-op in list; move focus left/right in grid |
| **Gamepad A (South) / Start** | Launch focused game / open variant picker |
| **Gamepad B (East)** | Back — close variant picker, or open hamburger |
| **Gamepad Y (North)** | Toggle List ↔ Grid view |
| **Gamepad X (West)** | Open search / jump-to-letter |
| **Gamepad L1/R1 (shoulders)** | Page up / page down (jump ~1 screenful) |
| **Gamepad L2/R2 (triggers)** | Jump to previous/next letter group |
| **Gamepad right stick** | Analog scroll (speed proportional to deflection) |
| **Touch tap** | Launch tapped game (or open variant picker) |
| **Touch drag** | Scroll with momentum (fling gesture) |
| **Touch long-press** | Future: context menu (game info, remove, etc.) |
| **Mouse click** | Launch clicked game (or open variant picker) |
| **Mouse hover** | Highlight hovered item |
| **Mouse wheel** | Scroll vertically |
| **Mouse middle-button drag** | Scroll drag (like touch drag) |
| **Keyboard Up/Down** | Same as D-pad up/down |
| **Keyboard Left/Right** | Same as D-pad left/right (grid only) |
| **Keyboard Enter** | Launch / select |
| **Keyboard Escape / Backspace** | Back |
| **Keyboard Page Up/Down** | Page scroll |
| **Keyboard Home/End** | Jump to first/last entry |
| **Keyboard letter** | Jump to first entry starting with that letter |
| **Keyboard Tab** | Toggle List ↔ Grid view |
| **Keyboard Ctrl+F** | Open search filter |

### 6.3 Scrolling

```cpp
struct ScrollState {
    float mOffset       = 0.0f;  // current scroll offset (pixels)
    float mVelocity     = 0.0f;  // pixels/second (for momentum)
    float mMaxOffset    = 0.0f;  // computed from content height
    bool  mbDragging    = false;
    float mDragStartY   = 0.0f;
    float mDragStartOff = 0.0f;

    void Update(float dt);          // apply momentum, friction, clamping
    void BeginDrag(float y);
    void UpdateDrag(float y);
    void EndDrag(float velocityY);  // start fling with measured velocity
    void ScrollTo(float y, bool animate);  // auto-scroll to keep focus visible
    void PageUp(float pageHeight);
    void PageDown(float pageHeight);
};
```

**Momentum physics:**
- On drag end, sample velocity from last few touch positions.
- Each frame: `mOffset += mVelocity * dt;`
  `mVelocity *= pow(friction, dt * 60.0f);` where `friction ≈ 0.95`
  (frame-rate independent exponential decay — at 60 fps each frame
  multiplies by 0.95; at 30 fps each frame multiplies by 0.95²).
- Stop when `|mVelocity| < 1.0`.
- Clamp `mOffset` to `[0, mMaxOffset]` with elastic overscroll bounce
  (optional, nice-to-have).

**Mouse wheel:** Each notch scrolls by `3 × rowHeight` (list) or
`tileHeight + padding` (grid).

**Auto-scroll on focus change:** When gamepad/keyboard moves focus, if
the focused item is outside the visible area, `ScrollTo()` animates
smoothly to bring it into view with one row/item of context.

---

## 7. Game-Art Display

### 7.1 Image Loading

Game-art images are loaded as GPU textures on demand:

- **Texture cache:** LRU cache of `N` loaded textures (e.g. 64–128).
  Tiles outside the visible scroll area have their textures evicted.
- **Background loading:** Image decode via SDL3_image (`IMG_Load()` /
  `IMG_Load_IO()`, already integrated in `oshelper_sdl3.cpp`) happens
  on a loader thread.  The tile shows the placeholder color block until
  the texture is ready.
- **Thumbnail generation:** On first load, generate and cache a
  thumbnail at tile resolution (e.g. 160×120) to avoid loading full
  screenshots each time.  Thumbnails stored in
  `~/.config/altirra/thumbnails/<hash>.png`.

### 7.2 Matching Rules (Detailed)

Given a game entry with canonical name `"Air Raid"`:

1. **Same directory:** Look for `Air Raid.{png,jpg,jpeg,bmp,webp}` in
   the same directory as the game file.
2. **Art subdirectory:** Look in `artwork/`, `screenshots/`, `images/`,
   `media/`, `boxart/` subdirectories relative to the game file's
   parent directory.
3. **Case-insensitive match** on filesystems that support it (Linux: do
   explicit lowercased comparison).
4. **Cross-folder matching** (optional, off by default): If enabled,
   search all configured game source folders for a matching image name.
   This is O(sources × games) on first scan but results are cached.

---

## 8. Settings — Game Library Page

Added as a new page in the Gaming Mode settings panel
(`mobile_settings.cpp`), accessible from the settings gear icon on the
Game Browser top bar and from the existing Settings screen.

```
┌──────────────────────────────────────────────────────────┐
│  ← Settings                                              │
│                                                          │
│  GAME LIBRARY                                            │
│                                                          │
│  Game Folders                                            │
│  ┌──────────────────────────────────────────────────┐   │
│  │  /home/user/Atari/Games                     [✕]  │   │
│  │  /media/roms/Atari800                       [✕]  │   │
│  └──────────────────────────────────────────────────┘   │
│  [+ Add Folder]                                          │
│                                                          │
│  Game Archives                                           │
│  ┌──────────────────────────────────────────────────┐   │
│  │  /home/user/Atari/collection.zip            [✕]  │   │
│  └──────────────────────────────────────────────────┘   │
│  [+ Add Archive]                                         │
│                                                          │
│  ☑ Scan subfolders recursively                           │
│  ☐ Match game-art from other folders                     │
│  ☑ Show Game Browser on startup                          │
│                                                          │
│  Library: 347 games found                                │
│  Last scan: 2 minutes ago                                │
│  [Rescan Now]                                            │
│                                                          │
│  [Clear Play History]                                    │
│                                                          │
└──────────────────────────────────────────────────────────┘
```

**Settings stored in registry** (not in the game library JSON — these
are config, not cache):

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `GameLibrary/SourceCount` | int | 0 | Number of configured sources |
| `GameLibrary/Source<N>.Path` | string | — | Source path |
| `GameLibrary/Source<N>.IsArchive` | bool | — | Folder vs archive |
| `GameLibrary/Recursive` | bool | true | Scan subfolders (global, applies to all folder sources) |
| `GameLibrary/CrossFolderArt` | bool | false | Match art across folders |
| `GameLibrary/ShowOnStartup` | bool | true | Show browser on boot |
| `GameLibrary/ViewMode` | int | 1 | 0=List, 1=Grid |

The settings page is added by extending `ATMobileSettingsPage` enum in
`mobile_internal.h` (currently: Home, Machine, Display, Performance,
Controls, SaveState, Firmware) and adding a `GameLibrary` value.  The
page renders inside the existing `RenderSettings()` dispatch in
`mobile_settings.cpp`.

"Add Folder" and "Add Archive" open the existing file browser in a
folder-picker or archive-picker mode (similar to the existing
`s_romFolderMode` for firmware scanning), returning a selected path
that is added to the source list and triggers an immediate scan.

---

## 9. Persistence & Play History

**On game launch:**
1. Set `lastPlayed = now()` and `++playCount` on the `GameEntry`.
2. Write updated `gamelibrary.json` to disk.  All JSON writes happen
   on the **main thread only** — the scanner thread never writes the
   cache.  Writes are infrequent (game launch, scan complete) so
   blocking is acceptable.  If a background scan is in progress when
   a game launches, the play-history update applies to the in-memory
   entries and will be merged into the scan result when it completes.
3. The game browser's "Last Played" section updates next time it's
   shown.

**Play history survives rescans:** The scanner produces entries without
play history.  Main thread merges by matching on **any overlapping
variant path** between old and new entries (not just the primary
variant).  This handles cases where variants are added or reordered
between scans.  If all variant paths change (folder renamed), history is
lost for that entry (future: content hash matching).

**On "Clear Play History":** Zero all `lastPlayed`/`playCount` fields,
save JSON.

---

## 10. Startup Integration

### 10.1 Modified Boot Sequence (Gaming Mode Only)

Current `main_sdl3.cpp` flow (simplified):

```cpp
// Current:
g_sim.ColdReset();
g_sim.Resume();
// → emulation starts immediately
```

New flow:

```cpp
if (ATUIIsGamingMode() &&
    gameLibrarySettings.showOnStartup && !hasCommandLineImage) {
    // Don't boot — show Game Browser (or empty-state setup prompt
    // if no sources configured yet)
    mobileState.currentScreen = ATMobileUIScreen::GameBrowser;
    // VM is initialized but not running
    // Main loop renders ImGui but skips g_sim.Advance()
} else {
    // Existing behavior
    g_sim.ColdReset();
    g_sim.Resume();
}
```

### 10.2 Returning to Game Browser

From emulation, the hamburger menu gains a "Game Library" option that:
1. Pauses the simulator (`g_sim.Pause()`).
2. Switches to `ATMobileUIScreen::GameBrowser`.
3. When a new game is selected, the old media is unloaded and the new
   game boots.

Desktop mode: the game browser is not shown (Desktop mode uses the
existing menu bar and file dialogs).  However, the game library data
model and scanning are shared — a future Desktop mode browser can reuse
the same `GameLibrary` class.

---

## 11. File Structure

### 11.1 New Files

```
src/AltirraSDL/source/ui/gamelibrary/
    game_library.h              — GameEntry, GameSource, GameLibrary class
    game_library.cpp            — Data model, JSON load/save, play history
    game_library_scanner.h      — Scanner thread interface
    game_library_scanner.cpp    — Background folder/archive walking & grouping
    game_library_art.h          — Game-art matching and thumbnail cache
    game_library_art.cpp        — Image matching, thumbnail generation

src/AltirraSDL/source/ui/mobile/
    mobile_game_browser.cpp     — Game Browser screen (grid + list views)
    mobile_game_settings.cpp    — Game Library settings page
    mobile_scroll.h             — Momentum scrolling helper (reusable)
    mobile_scroll.cpp           — Scroll physics (fling, auto-scroll)
```

### 11.2 Modified Files

| File | Change |
|------|--------|
| `ui_mobile.h` | Add `GameBrowser` to `ATMobileUIScreen` enum |
| `ui_mobile.cpp` | Dispatch to game browser renderer |
| `mobile_internal.h` | Add `GameLibrary*` pointer to mobile state |
| `main_sdl3.cpp` | Initialize game library, conditional startup |
| `mobile_settings.cpp` | Add Game Library section to settings pages |
| `mobile_hamburger.cpp` | Add "Game Library" item to hamburger menu |
| `mobile_file_browser.cpp` | Add folder-picker mode for source selection |
| `CMakeLists.txt` | Add new source files |

---

## 12. Implementation Phases

### Phase 1 — Data Model & JSON Cache

**Goal:** `GameLibrary` class that can load/save a JSON cache file and
store game entries with variants and play history.

**Files:** `game_library.h`, `game_library.cpp`

**Work:**
- Define `GameMediaType`, `GameVariant`, `GameEntry`, `GameSource` structs.
- Implement JSON serialization using `VDJSONWriter` and deserialization
  using `VDJSONReader` (both in vdjson, already in the project).
- Implement play history updates (`RecordPlay()`, `ClearHistory()`).
- Implement display name cleaning (strip extension, underscore→space,
  title-case).
- Unit-testable in isolation — no ImGui or simulator dependency.

**Acceptance:** Can round-trip a `gamelibrary.json` file with sample
entries.

### Phase 2 — Scanner

**Goal:** Background thread that walks folders and ZIP archives,
discovers game files, groups variants, disambiguates names.

**Files:** `game_library_scanner.h`, `game_library_scanner.cpp`

**Work:**
- Implement folder walking (recursive, filtered by supported
  extensions) using the system library's filesystem APIs.
- Implement ZIP content listing using `VDZipArchive` from
  `vd2/system/zip.h`.
- Implement variant grouping: canonical name extraction, grouping by
  parent dir + canonical name, label construction.
- Implement display name disambiguation (parent folder prefix for
  collisions).
- Implement thread management: start, cancel, completion notification.
- Implement incremental scanning (mod-time check, diff against cache).

**Acceptance:** Scanner finds and correctly groups test collections
with variants, duplicate names across folders, nested dirs.

### Phase 3 — Momentum Scrolling

**Goal:** Reusable scroll widget with touch fling, mouse wheel, and
keyboard/gamepad page navigation.

**Files:** `mobile_scroll.h`, `mobile_scroll.cpp`

**Work:**
- Implement `ScrollState` with momentum physics.
- Integrate touch drag tracking (extend `ATTouchDragScroll()`).
- Support mouse wheel via ImGui IO.
- Support animated `ScrollTo()` for focus-follows-selection.
- Handle overscroll clamping.

**Acceptance:** Smooth scrolling in a test ImGui child window with all
three input methods.

### Phase 4 — List View

**Goal:** Functional game browser screen with list view, navigation,
and game launching.

**Files:** `mobile_game_browser.cpp`, modified `ui_mobile.h/cpp`,
`mobile_internal.h`

**Work:**
- Add `GameBrowser` to `ATMobileUIScreen` enum.
- Implement top bar (title, view toggle, settings gear).
- Implement list view rendering: Last Played section, divider, All
  Games section.
- Implement focus tracking and highlight rendering.
- Implement all input navigation (gamepad, keyboard, mouse, touch).
- Implement game launch: load media via deferred action queue, cold
  reset, resume, switch to None screen.
- Implement variant picker popup for multi-variant entries.
- Implement empty state screen.

**Acceptance:** Can browse a scanned library in list view with all
input methods and launch games.

### Phase 5 — Grid View

**Goal:** Grid view with colored placeholder tiles and view toggle.

**Files:** `mobile_game_browser.cpp` (continued)

**Work:**
- Implement grid tile rendering (colored rectangles with type labels).
- Implement grid layout: column count calculation, row wrapping.
- Implement 2D grid focus navigation (up/down/left/right).
- Implement view toggle (Tab key, Y button, UI button).
- Implement "Last Played" horizontal row in grid view.
- Persist view mode in settings.

**Acceptance:** Can switch between list and grid, navigate grid with
gamepad, launch from grid.

### Phase 6 — Game Library Settings

**Goal:** Settings page for managing sources and library options.

**Files:** `mobile_game_settings.cpp`, modified `mobile_settings.cpp`,
`mobile_file_browser.cpp`

**Work:**
- Add Game Library page to mobile settings.
- Implement source list rendering (folders + archives with remove
  buttons).
- Implement "Add Folder" and "Add Archive" via file browser in
  folder/archive picker mode.
- Implement option toggles (recursive, cross-folder art, show on
  startup).
- Implement "Rescan Now" button.
- Implement library stats display (game count, last scan time).
- Implement "Clear Play History" button.
- Store settings in registry.

**Acceptance:** Can add/remove sources, trigger rescan, toggle options.

### Phase 7 — Startup Integration & Hamburger

**Goal:** Game browser appears on startup in Gaming Mode; returnable
from emulation.

**Files:** modified `main_sdl3.cpp`, `mobile_hamburger.cpp`

**Work:**
- Modify startup flow to show game browser instead of auto-booting VM
  when conditions are met (gaming mode, setting enabled, no command-line
  image) — sources do not need to be configured; the empty-state setup
  prompt handles that case (see §10.1).
- Skip `g_sim.Advance()` in main loop when game browser is active and
  VM hasn't booted.
- Add "Game Library" item to hamburger menu.
- On hamburger → Game Library: pause simulator, show browser.
- On game selection from browser: unload current media, load new, boot.
- "Boot Atari" button: cold reset + resume with no media.

**Acceptance:** Full startup-to-game-to-library-to-new-game flow works.

### Phase 8 — Game-Art Matching

**Goal:** Discover and display game-art images as tile backgrounds in
grid view.

**Files:** `game_library_art.h`, `game_library_art.cpp`, modified
scanner and browser.

**Work:**
- Implement art matching during scan (same dir, art subdirs, cross-
  folder optional).
- Implement image loading on background thread via SDL3_image
  (`IMG_Load()` — already a project dependency).
- Implement GPU texture upload on main thread (reference:
  `ui_rewind.cpp` already uploads SDL_Surface → ImTextureID for the
  rewind preview feature — reuse the same pattern).
- Implement LRU texture cache (64–128 entries).
- Implement thumbnail generation and disk cache
  (`~/.config/altirra/thumbnails/`).
- Render art in grid tiles, falling back to color placeholder if no
  art or not yet loaded.
- Show loading shimmer/fade-in when texture arrives.

**Acceptance:** Games with matching art files show images in grid view;
scrolling is smooth with texture streaming.

### Phase 9 — Search & Jump-to-Letter

**Goal:** Keyboard search filter and letter-jump navigation.

**Files:** `mobile_game_browser.cpp` (continued)

**Work:**
- Implement search bar: text input filters visible entries by substring.
- On gamepad: X button opens on-screen letter picker (A–Z grid +
  digits + clear), selecting a letter jumps to first match.
- On keyboard: typing a letter key (when search bar is not focused)
  jumps to first entry starting with that letter.
- Ctrl+F or gamepad X opens/focuses the search bar.
- Escape or gamepad B clears search and closes search bar.
- Trigger/shoulder buttons jump between letter groups.

**Acceptance:** Can quickly find games in a large library via search
and letter-jump on all input methods.

### Phase 10 — Polish & Edge Cases

**Work:**
- Handle source removal gracefully (remove entries from that source,
  preserve play history for remaining entries).
- Handle corrupt/unreadable JSON cache (discard and rescan).
- Handle filesystem permission errors during scan (skip and warn).
- Scan progress indicator (entries found so far, shown in top bar).
- Animations: tile hover scale, focus transitions, scroll snap.
- Performance profiling with large libraries (5000+ entries).
- Android-specific: SAF/scoped storage path handling for sources.
- Accessibility: ensure all interactive elements have proper ImGui IDs
  for test mode.

---

## 13. Dependencies & Constraints

- **vdjson** — Used for JSON parsing **and writing** (already in
  project, zero Win32 dependency).  `VDJSONWriter` in
  `vd2/vdjson/jsonwriter.h` provides `OpenObject()`, `OpenArray()`,
  `WriteMemberName()`, `WriteString()`, `WriteInt()`, `WriteBool()`,
  `Close()`, etc.  `WriteString()` takes `wchar_t*` — pass `VDStringW`
  content directly.  The stream output adapter (`VDJSONStreamOutput` in
  `vd2/vdjson/jsonoutput.h`) handles wchar_t → UTF-8 conversion
  internally via `VDCodePointToU8()`.  Similarly, `VDJSONReader::Parse()`
  auto-detects encoding and produces wchar_t values in the
  `VDJSONDocument` tree.  No manual `VDTextWToU8()`/`VDTextU8ToW()` is
  needed at the JSON API boundary.  For file output, use
  `VDJSONStreamOutput` wrapping an `IVDStream` (e.g. `VDFileStream`);
  for reading, load the file into a buffer and call
  `VDJSONReader::Parse(buf, len, doc)`, then walk the document via
  `VDJSONValueRef`.
- **ZIP reading** — `VDZipArchive` in `vd2/system/zip.h` (not ATIO).
  API: `GetFileCount()`, `GetFileInfo(idx)` returns `FileInfo` with
  `mDecodedFileName`, `mUncompressedSize`, etc.
- **Image loading** — SDL3_image (already integrated as a project
  dependency; used in `oshelper_sdl3.cpp`).  `IMG_Load()` /
  `IMG_Load_IO()` handle PNG, JPG, BMP, WebP, etc.
- **Thread primitives** — `std::thread`, `std::mutex`, `std::atomic`
  from C++17 standard library (already used elsewhere in codebase).
- **No Windows `.sln` changes** — All new files are SDL3-only; they
  compile only under CMake.  The Windows build is unaffected.
- **Desktop mode unaffected** — Game Library UI only activates in Gaming
  Mode.  The `GameLibrary` data model is mode-agnostic and reusable if
  Desktop mode wants a browser later.

---

## 14. Future Enhancements (Out of Scope)

Documented here so the design accommodates them without requiring
rework:

- **Boxart/screenshot scraper** — downloads art from online databases;
  stores in game-art directories; updates `artPath` in library.
- **Per-game settings** — hardware mode, BASIC toggle, input map
  override, stored as additional JSON fields on `GameEntry`.
- **Collections/favorites** — user-defined groupings beyond "Last
  Played" and "All Games".
- **Game info panel** — long-press or info button shows metadata,
  file details, compatibility notes.
- **Content hash matching** — use file content hash instead of path
  for play history, so renamed/moved files keep their history.
- **Desktop mode browser** — reuse `GameLibrary` with an ImGui window
  that fits the desktop menu-bar UI.
- **Network game sources** — scan SMB/NFS shares or HTTP indices.
- **Auto-detect hardware mode** — from file headers (CAR mapper,
  5200 detection) to auto-configure the emulator.
