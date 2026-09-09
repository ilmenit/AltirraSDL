//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2014 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#ifndef f_AT_ATCORE_DEVICEPRINTER_H
#define f_AT_ATCORE_DEVICEPRINTER_H

#include <vd2/system/refcount.h>
#include <vd2/system/unknown.h>

class vdfloat2;
class vddouble2;

// Interface for output sinks connecting to printer ports and receiving text
// output. This can either be the default output or a child device with a
// parallel port interface.
//
class IATPrinterOutput : public virtual IVDRefUnknown {
public:
	static constexpr uint32 kTypeID = "IATPrinterOutput"_vdtypeid;

	// True if the printer output can accept and wants Unicode output, false
	// if it wants the raw output. Printers may still produce raw output
	// even if the output wants Unicode, but printers must not produce
	// Unicode output if the output doesn't want it.
	virtual bool WantUnicode() const = 0;
	virtual void WriteRaw(const uint8 *buf, size_t len) {}
	virtual void WriteUnicode(const wchar_t *buf, size_t len) {}
};

struct ATPrinterGraphicsSpec {
	// Page width edge-to-edge, in millimeters.
	float mPageWidthMM = 0;

	// Page height edge-to-edge, in millimeters. Zero means unspecified or
	// no page height (roll paper).
	float mPageHeightMM = 0;

	// Default vertical border between top of the first page and the top of
	// the print head. This is typically the same as the left/right borders
	// and is used to nicely position the paper at start.
	float mPageVBorderMM = 0;

	// Left margin horizontal position during normal printing.
	float mLeftMarginMM = 0;

	// Radius of a printed dot, in millimeters.
	float mDotRadiusMM = 0;

	// Distance from center to center of adjacent dots printed together in
	// the same column.
	float mVerticalDotPitchMM = 0;

	// True if bit 0 in the pin pattern is the topmost bit in the print head;
	// false if it is the bottom-most.
	bool mbBit0Top = false;

	// Number of pins in the print head.
	uint32 mNumPins = 0;

	// Pin bit number corresponding to the bottom-most dot above the text
	// baseline. All pins below this pin are for descenders or spacing. For
	// instance, bit0top=true, numPins=9, baselinePin=6 means 7 pins above
	// the baseline and 2 pins below the baseline.
	uint32 mBaselinePin = 0;
};

// Interface for output sinks connecting to printer ports and receiving
// graphical output. This is currently only the default output.
//
class IATPrinterGraphicalOutput : public virtual IVDRefUnknown {
public:
	static constexpr uint32 kTypeID = "IATPrinterGraphicalOutput"_vdtypeid;

	// Set callback after paper is cleared.
	virtual void SetOnClear(vdfunction<void()> fn) = 0;

	// Advance the paper upward by the specified number of millimeters. May be
	// negative for reverse feed.
	virtual void FeedPaper(double distanceMM) = 0;

	// Print dots in a column at the specified X position in millimeters from
	// the left edge of the page.
	virtual void Print(double x, uint32 dots) = 0;

	struct CharColumn {
		// Dot pattern for print column.
		uint32 mDots;

		// X offset from left edge of character, in millimeters. By convention,
		// the left-most position is 0, which places dots tangent to the left
		// edge of the character cell.
		float mXOffset;
	};

	// Define a new character. Characters are used to associate print patterns
	// with characters so that the text can be preserved for copy/paste on
	// export.
	//
	// Advance is the baseline advance distance in millimeters.
	// Columns contains the print columns for the character. It may be sparse
	// or empty.
	//
	// UniChar is the equivalent Unicode character. 0 means no equivalent.
	//
	// CharSet partitions characters into different sets. The ID has no meaning
	// otherwise. It's recommended that characters be partitioned into different
	// sets based on the advance width; this improves text selection in some
	// PDF readers.
	//
	// The character ID for the new character is returned.
	virtual uint32 DefineChar(double advance, vdspan<const CharColumn> columns, uint32 uniChar, uint32 charSet) = 0;

	// Return the advance distance previously defined for a character, by ID.
	virtual float GetCharAdvance(uint32 ch) const = 0;

	// Return the equivalent unicode character for a previously defined character,
	// by ID. 0 means no equivalent.
	virtual uint32 GetCharUnicodeChar(uint32 ch) const = 0;

	// Return the print columns associated with a previously defined character,
	// by ID.
	virtual vdspan<const CharColumn> GetCharColumns(uint32 ch) const = 0;

	// Print a character at the given X position, by character ID. The print head
	// is moved to the right by the advance distance from the specified X position.
	virtual void PrintChar(double x, uint32 ch) = 0;

	// Move the current pen position to the given absolute X and relative Y
	// in mm, feeding paper to match.
	virtual void MoveVector(const vddouble2& pt) = 0;

	// Add a vector (line segment) from pt1 to pt2 with the specified dot size.
	// Both pt1 and pt2 are relative to the current vertical position, and
	// afterward the vertical position is updated to pt2.
	virtual void AddVector(const vddouble2& pt1, const vddouble2& pt2, uint32 color) = 0;

	// Change the current pen color to the given internal pen color. This is
	// used to show which pen is currently active. The default shown color for
	// a new output is black.
	virtual void ChangePenColor(uint32 color) = 0;

	// Convert a standard sRGB color to internal print color.
	virtual uint32 ConvertColor(uint32 srgb) const = 0;
};

// Service that provides for creation of default printer outputs.
//
// NOTE: Most devices that support a printer output can use either the default
// printer output here or connect to a parallel port device. Devices should not
// create a default printer port until the parallel port is used, to prevent
// creating spurious printer outputs while the port is being set up.
//
class IATPrinterOutputManager : public IVDRefCount {
public:
	static constexpr auto kTypeID = "IATPrinterOutputManager"_vdtypeid;

	// Create a printer output for text output. The output is automatically
	// closed when the last reference is deleted.
	virtual vdrefptr<IATPrinterOutput> CreatePrinterOutput(const wchar_t *name) = 0;

	// Create a printer output for graphical output. The output is automatically
	// closed when the last reference is deleted.
	virtual vdrefptr<IATPrinterGraphicalOutput> CreatePrinterGraphicalOutput(const wchar_t *name, const ATPrinterGraphicsSpec& spec) = 0;

protected:
	~IATPrinterOutputManager() = default;
};

#endif
