# Emulated-screen accessibility

Configure System → Emulator → Accessibility → Enable screen reader support
publishes the Atari text display to the operating system's accessibility API.
The existing `Accessibility: Enable` preference remains the saved switch.

Windows uses Altirra's native UI Automation document and text-range provider
attached to the SDL window. macOS exposes a native accessibility text area to
VoiceOver. Linux exports an AT-SPI application, window, and text document on the
desktop accessibility bus. Linux loads the system's `libgio-2.0.so.0` when the
feature is enabled; the AT-SPI bus/registry services must be installed and
available. There is no GTK build dependency. Disabled support creates no bus
connection or accessibility worker.
The Accessibility page reports whether the provider is active, connecting, or
unavailable. Missing GIO and unavailable bus/registry services have separate
messages; temporary service failures retry automatically. After installing a
missing GIO runtime, disable and enable the setting to retry loading it.

Text comes from the same ANTIC display-list and ATASCII decoding used by text
selection, including international characters and reverse video. Active device
video outputs publish their own text rows. As in native Altirra, this is text
extraction from emulated text modes; it does not apply OCR to graphics. Reading
the screen does not alter the user's selection, resume paused emulation, or
inject Atari keys. Accessibility text selection and navigation maintain their
own reader position.

The text document supports text ranges and line geometry, change notifications,
and focus reporting. The debugger's Display pane supplies its own image bounds.
Disabling support detaches the native provider and releases its resources.
Linux reconnects if the accessibility bus is lost and re-registers if the
registry restarts. Native callbacks consume immutable text/geometry snapshots
and never read or mutate the emulator core.

This integration covers the emulated screen. Dear ImGui controls do not acquire
native accessibility semantics from this switch. Android and WebAssembly do not
implement these desktop providers; the desktop-only setting is disabled there.

`tests/ui/test_accessibility.py` checks actual emulated text, paused operation,
reverse video, and toggling. Its Linux integration test starts an isolated real
AT-SPI bus and registry, then verifies discovery, text/ranges/geometry/clipping,
change events, selection, and teardown over D-Bus. That test requires the system
AT-SPI tools and Python GIO introspection binding. Automated publication tests
do not replace interactive Narrator, NVDA, VoiceOver, or Orca validation.
