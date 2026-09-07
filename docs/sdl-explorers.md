# SDL Explorer documents

Tools → Explorers opens a file picker for disks, cartridges, or Atari
executables. Cancelling does not create a window. Each opened file owns a
separate persistent ImGui tool window. Opening the same source again focuses
its existing window; different files of the same type remain independent.
File → Open another opens another document. The path appears beneath the menu
and in a tooltip, distinguishing files with identical basenames.

This is an explicitly requested SDL frontend extension of Windows Altirra's
Disk Explorer. It does not change the Windows frontend or emulation code.

## Working with contents

All three explorers use a contents table above a resizable preview. Click a
row or navigate with the arrow keys to inspect it. Ctrl/Cmd-click toggles
individual selections and Shift-click selects a range. Select all is available
on the toolbar. Right-click retains an existing multiple selection, or selects
the clicked row first. Selection actions are also accessible through a menu or
visible toolbar control.

- **Disk:** directories, sortable file metadata, all existing filesystem
  import/export operations (including text conversion and multiple-file
  export), rename, delete, directory creation, and file information. Disk
  tools retain sectors, boot records, image properties, allocation maps,
  filesystem validation, mounted drives, and block-device partitions.
  Unsupported filesystems still permit sector inspection. The preview retains
  ATASCII wrapping modes, ASCII, hex, executable structure, MAC/65, Atari BASIC,
  Syn assembler, and 6502 disassembly. Open in XEX Explorer copies a selected
  executable into an independent inspection document without a temporary file.
  A structurally valid executable also exposes **Run** in the toolbar and
  context menu and runs on double-click. It is copied into the main-thread boot
  queue and loaded directly from memory through the normal program-image boot
  pipeline. This creates no temporary host file and no recent-file or game-
  library entry, while retaining the normal unload policy, mode switching,
  cold reset, compatibility check, and resume behavior.
- **XEX:** an ordered segment table with address ranges, lengths, vector writes,
  and overlaps with earlier loads. Preview hex, ATASCII, ASCII, or cached 6502
  disassembly. Export selected bytes writes one raw file per segment; exporting
  selected segments as XEX preserves their original order and address headers.
  A subset is not necessarily a runnable executable.
- **Cartridge:** selectable storage regions, image offsets, and sizes. These
  are regions of the interpreted cartridge image, not claimed hardware mapper
  bank numbers or CPU addresses. Header/checksum information and mapper
  interpretation remain under Image properties and mapper. Selected regions
  export as raw bytes; File retains whole-image CAR and raw saving through the
  canonical cartridge saver, including small-ROM size handling.

Multiple raw segment/region exports select a destination folder. Their names
include the segment/region number and address/offset. Existing files in batch
exports are not overwritten; the status reports how many files completed if
an operation fails. Copy details copies selected metadata as tab-separated text.

## Disk modifications

Standalone disk images open read-only. Write enables filesystem modifications
when validation and filesystem capabilities permit them. Save disk image as
uses the canonical disk saver, which rejects formats that cannot represent the
image's sector characteristics. Closing a modified standalone image offers
Save as, Discard changes, or Cancel. Application exit also identifies unsaved
Explorer disk changes in its existing confirmation.

Mounted-drive editing retains its existing write/auto-flush behavior. An open
Explorer retains its image if the emulator ejects or replaces that drive, but
stops notifying the replacement drive and labels the document as detached.

## Ownership and drop routing

Document states and filesystem objects are independently owned. Legacy disk
helpers run under a scoped main-thread document context. Native dialog callbacks
only queue plain results into a weakly referenced mailbox; the owning document
drains them on the main thread. Closing a document expires outstanding results.
The initiating document is disabled while an export/import dialog is pending,
so filesystem keys, mapper interpretation, and selection snapshots stay valid.

Explorer requests opt into cancellation notification in the shared file-dialog
wrapper. Other callers retain their existing success-only callback behavior.
This releases pending state on native and built-in picker cancellation, errors
that fall back to the built-in picker, and replaced picker requests.

Drop hit-testing uses ImGui's actual front-to-back window stack and the drop
coordinates. Dropping an image into an Explorer opens another document;
dropping ordinary files into a writable disk retains import behavior. A file
dropped outside Explorer is not intercepted merely because its extension is
recognized. Highlighting uses the same destination test.

## Verification

`tests/ui/test_explorers.py` exercises cancellation, independent documents,
exact binary exports, batch boundaries, disk editing/save/reopen and close
prompts, embedded executables, raw sector tools, keyboard selection, and drop
routing through the live UI.

Run against a built SDL application, with disposable settings:

```sh
PYTHONDONTWRITEBYTECODE=1 \
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
XDG_CONFIG_HOME=/tmp/altirra-explorer-test-config \
python -m pytest tests/ui/test_explorers.py tests/ui/test_smoke.py \
    --emu-path=build/explorer-review/src/AltirraSDL/AltirraSDL
```

Test-mode commands supplement ordinary widget interactions:

- `explore_file disk|xex|cartridge <path>` uses the normal document-opening path.
- `file_dialog_builtin on|off` selects deterministic built-in pickers.
- `explorer_drop <x> <y> <path>` checks normal Explorer drop routing.
- `ui_screenshot <path>` captures the frontend framebuffer. The existing
  `screenshot` command continues to capture the emulator image.

Native macOS/Windows pickers and real desktop drag events still merit manual
platform checks; offscreen UI tests do not exercise OS picker implementations.
