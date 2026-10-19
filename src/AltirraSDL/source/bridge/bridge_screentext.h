// bridge_screentext.h — portable ANTIC text-mode screen reader.
// No ImGui, no SDL3, no Win32: safe to compile into AltirraBridgeServer.
#ifndef AT_BRIDGE_SCREENTEXT_H
#define AT_BRIDGE_SCREENTEXT_H

#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/vdstl.h>

class ATSimulator;

namespace ATBridge {

// Reads up to numChars ATASCII characters from ANTIC text-mode line
// yc, starting at character column startChar, into dst. Returns the
// number of characters actually written (0 if yc is not currently a
// valid text-mode line, or fewer than numChars if the line is
// shorter). intl is set true if the line uses the international
// character set (CHBASE points at the alternate font). Side-effect-
// free: reads via ANTIC's own DMA memory view (DebugAnticReadMemory),
// not the CPU's. Reads exclusively through the sim argument — no
// global state. Port of Windows' ATUIVideoDisplayWindow::ReadText
// (formerly the static ReadText() in ui_textselection.cpp), with the
// only change being that every g_sim.* reference becomes sim.*.
int ReadTextLine(ATSimulator& sim, uint8 *dst, int yc, int startChar, int numChars, bool& intl);

// Walks every valid ANTIC mode line top to bottom (scanlines 8-247)
// and decodes each text-mode line (ANTIC modes 2/3/6/7) in full,
// using ReadTextLine plus the same ASCII decode table
// ui_textselection.cpp's ATTextCopyMode::ASCII path uses. Graphics-
// mode lines are skipped (not blanked), so a mixed graphics/text
// screen returns only its text rows. Each output line has leading and
// trailing spaces trimmed, and empty lines are omitted entirely. Does
// not read or write ATTextSelectionState — this is a fresh, stateless
// walk, not a reuse of the mouse-selection code path.
void ReadScreenText(ATSimulator& sim, vdvector<VDStringA>& outLines);

} // namespace ATBridge

#endif
