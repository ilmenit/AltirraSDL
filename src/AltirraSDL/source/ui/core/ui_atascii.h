//	AltirraSDL - Atari character-set rendering helpers

#pragma once

#include <stddef.h>
#include <vd2/system/vdtypes.h>

constexpr int kATUITextColumnsMin = 8;
constexpr int kATUITextColumnsMax = 160;

int ATUIGetTextColumns();
void ATUISetTextColumns(int columns);
void ATUIRenderTextColumnControls(const char *id);

// Returns the stock Atari 8-bit character set in ATASCII order.  Each
// character is eight bytes, one bit per pixel, MSB first.  The data comes
// from the same kernel ROM resource used by the Windows enhanced text view.
const uint8 *ATUIGetATASCIICharset();

// Draws an ATASCII byte stream into the current ImGui window.  A zero column
// count preserves source line lengths; a positive count wraps at that many
// columns.  Inverse-video bytes and Atari EOL ($9B) are rendered as they
// appear on an Atari display rather than being approximated with Unicode.
void ATUIRenderATASCII(const uint8 *data, size_t size, int columns, float scale = 2.0f);

// Renders a developer-friendly 7-bit text interpretation.  Atari EOL ($9B)
// becomes a line break and inverse-video bits are stripped; other controls
// are shown as dots so the column layout stays stable.
void ATUIRenderASCII(const uint8 *data, size_t size, int columns = 0);

// Renders a fixed-width hexadecimal dump using the configured monospace UI
// font.  addressBase is added to the left-hand offset (useful for XEX code).
void ATUIRenderHexDump(const uint8 *data, size_t size, uint32 addressBase = 0);
