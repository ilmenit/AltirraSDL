//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2026 Avery Lee
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

#include <stdafx.h>
#include <vd2/system/bitmath.h>
#include <vd2/system/color.h>
#include <vd2/system/file.h>
#include <vd2/system/int128.h>
#include <vd2/system/vecmath.h>
#include <vd2/system/zip.h>
#include "printerexport.h"
#include "printerttfencoder.h"
#include "printeroutput.h"
#include "printerrasterizer.h"
#include "versioninfo.h"

void ATPDFWriteStreamObject(VDTextOutputStream& textOut, uint32 objectId,
	vdspan<const uint8> data, bool fontProgram) {
	textOut.FormatLine("%u 0 obj", objectId);

	VDMemoryBufferStream bufs;

	// ZLIB header (Deflate 32K Normal)
	static constexpr uint8 kZLIBHeader[] { 0x78, 0x9C };

	bufs.Write(kZLIBHeader, 2);

	uint32 adler32;

	{
		VDDeflateStream defs(bufs, VDDeflateChecksumMode::Adler32, VDDeflateCompressionLevel::Quick);
		defs.Write(data.data(), data.size());
		defs.Finalize();
		adler32 = defs.Adler32();
	}

	uint32 v = VDToBEU32(adler32);
	bufs.Write(&v, 4);

	unsigned clen = (unsigned)bufs.Length();

	if (clen < data.size()) {
		if (fontProgram) {
			textOut.FormatLine(
				"<</Length %u/Length1 %u/Filter/FlateDecode>>",
				clen,
				(unsigned)data.size());
		} else {
			textOut.FormatLine(
				"<</Length %u/Filter/FlateDecode>>",
				clen);
		}

		textOut.PutLine("stream");

		textOut.Write((const char *)bufs.GetBuffer().data(), clen);
	} else {
		if (fontProgram) {
			// MERGE NOTE: PDF requires Length1 for embedded TrueType
			// programs even when no stream filter is used. test15 omitted it
			// on this uncompressed path.
			textOut.FormatLine(
				"<</Length %u/Length1 %u>>",
				(unsigned)data.size(),
				(unsigned)data.size());
		} else {
			textOut.FormatLine("<</Length %u>>", (unsigned)data.size());
		}

		textOut.PutLine("stream");

		textOut.Write((const char *)data.data(), data.size());
	}

	textOut.PutLine();
	textOut.PutLine("endstream");

	textOut.PutLine("endobj");
}

void ATPrinterExportAsPDF(const wchar_t *path, ATPrinterGraphicalOutput& output, float suggestedPageWidthMM, float suggestedPageHeightMM) {
	VDFileStream fileOut(path, nsVDFile::kWrite | nsVDFile::kCreateAlways | nsVDFile::kDenyAll);
	VDTextOutputStream textOut(&fileOut);

	vdfastvector<uint32> objectOffsets;

	const auto beginObject = [&](uint32 objectId) {
		objectOffsets[objectId - 1] = (uint32)textOut.Pos();
	};

	textOut.PutLine("%PDF-1.4");
	textOut.PutLine("%\x80\x80\x80\x80");

	objectOffsets.push_back((uint32)textOut.Pos());
	textOut.PutLine("1 0 obj");
	textOut.PutLine("<< /Type /Catalog");
	textOut.PutLine("/Pages 2 0 R");
	textOut.PutLine(">>");
	textOut.PutLine("endobj");

	// reserve entry for pages object
	objectOffsets.push_back(0);

	// build TrueType font
	const auto& spec = output.GetGraphicsSpec();
	const float dotRadiusMM = spec.mDotRadiusMM;

	const int ttfDotsPerLine = std::min<int>(spec.mNumPins, 7);
	const float ttfAscentMM = spec.mDotRadiusMM*2 + spec.mVerticalDotPitchMM * (ttfDotsPerLine - 1);
	const float ttfUnitsPerMM = 1000.0f / ttfAscentMM;
	const int ttfDotRadius = (int)(0.5f + dotRadiusMM * ttfUnitsPerMM);
	const int ttfDotVRange = 1000 - 2*ttfDotRadius;

	vdautoptr<ATTrueTypeEncoder> ttf(new ATTrueTypeEncoder);

	ttf->SetDefaultAdvanceWidth(ttfDotRadius * 2);

	// Create initial dot / missing char
	auto dotGlyph = ttf->BeginSimpleGlyph();
	ttf->AddCircleContour(ttfDotRadius, ttfDotRadius, ttfDotRadius);
	ttf->EndSimpleGlyph();

	// Create space
	ttf->MapCharacter(0x20, ttf->BeginSimpleGlyph());
	ttf->EndSimpleGlyph();

	// MERGE NOTE: test15 called EndSimpleGlyph() a second time here after
	// moving dot-glyph construction above the space glyph. The dot glyph is
	// already finalized, so a second call only redundantly finalizes the
	// space glyph twice.

	// map 0x21-0x7F
	for(int i=1; i<96; ++i) {
		ttf->MapCharacter(0x20+i, ttf->BeginCompositeGlyph());

		for(int j=0; j<7; ++j) {
			if (i & (1<<j))
				ttf->AddGlyphReference(dotGlyph, 0, (ttfDotVRange*j) / 6);
		}
		ttf->EndCompositeGlyph();
	}

	// Map 0xC0-0xDF. We avoid 0x80-0xBF because of character mapping problems in 0x80-0x9F and the altspace
	// at 0xA0. PDF is supposed to allow symbolic fonts to be mapped 1:1, but this doesn't seem to work in Acrobat
	// despite working in the browser viewers, and it's difficult to satisfy both PDF's symbol font character
	// mappings and FontValidator's requirements. Therefore we avoid the issue and just map to Latin-1 characters.
	for(int i=96; i<128; ++i) {
		ttf->MapCharacter(0x60+i, ttf->BeginCompositeGlyph());

		for(int j=0; j<7; ++j) {
			if (i & (1<<j))
				ttf->AddGlyphReference(dotGlyph, 0, (ttfDotVRange*j) / 6);
		}
		ttf->EndCompositeGlyph();
	}

	ttf->SetName(ATTrueTypeName::Copyright, "None - autogenerated");
	ttf->SetName(ATTrueTypeName::FontFamily, "Altirra Print");
	ttf->SetName(ATTrueTypeName::FontSubfamily, "Normal");
	ttf->SetName(ATTrueTypeName::FullFontName, "Altirra Print Normal");
	ttf->SetName(ATTrueTypeName::UniqueFontIdentifier, "Altirra Print Normal");
	ttf->SetName(ATTrueTypeName::Version, "Version 1.0");
	ttf->SetName(ATTrueTypeName::PostScriptName, "Print");

	const vdspan<const uint8> fontData = ttf->Finalize();

	// add entry for embedded font (3)
	objectOffsets.push_back((uint32)textOut.Pos());
	textOut.FormatLine("%u 0 obj", (uint32)objectOffsets.size());

	textOut.PutLine(
		"<< /Type /Font "
		"/Subtype /TrueType "
		"/BaseFont /AAAAAA+Print "
		"/FirstChar 32 "
		"/LastChar 32 "
		"/Widths ["
	);

	textOut.FormatLine(" %d", 2*ttfDotRadius);

	textOut.PutLine(
		"] "
		"/FontDescriptor 4 0 R "
		">>");

	textOut.PutLine("endobj");

	// add entry for embedded font descriptor (4)
	objectOffsets.push_back((uint32)textOut.Pos());
	textOut.FormatLine("%u 0 obj", (uint32)objectOffsets.size());

	textOut.FormatLine(
		"<< /Type /FontDescriptor "
		"/FontName /AAAAAA+Print "
		"/Flags 5 "
		"/FontBBox [0 -24 %d 1000] "
		"/ItalicAngle 0 "
		"/Ascent 1000 "
		"/Descent -24 "
		"/CapHeight 1000 "
		"/StemV 80 "
		"/MissingWidth %d "
		"/FontFile2 5 0 R >>",
		2 * ttfDotRadius,
		2 * ttfDotRadius
	);

	textOut.PutLine("endobj");

	// add entry for embedded font (5)
	objectOffsets.push_back((uint32)textOut.Pos());

	ATPDFWriteStreamObject(
		textOut,
		(uint32)objectOffsets.size(),
		fontData,
		true);


	// reserve object space for character print font object (7)
	objectOffsets.push_back((uint32)textOut.Pos());
	const uint32 print2Obj = (uint32)objectOffsets.size();

	// add blend state object
	objectOffsets.push_back((uint32)textOut.Pos());
	const uint32 blendStateObj = (uint32)objectOffsets.size();

	textOut.FormatLine("%u 0 obj", blendStateObj);
	textOut.PutLine("<< /Type /ExtGState /BM /Darken >>");
	textOut.PutLine("endobj");

	vdfastvector<uint32> assignedChars;
	vdfastvector<sint32> assignedCharLookup;

	// render pages
	uint32 basePageObj = (uint32)objectOffsets.size() + 1;

	static constexpr float mmToPoints = 72.0f / 25.4f;
	const float pageWidthMM = suggestedPageWidthMM > 0 ? suggestedPageWidthMM : spec.mPageWidthMM;
	const float pageHeightMM = suggestedPageHeightMM > 0 ? suggestedPageHeightMM : 11.0f * 25.4f;
	const float headHeightMM = spec.mDotRadiusMM * 2 + spec.mVerticalDotPitchMM * (float)(spec.mNumPins - 1);
	const int numPages = std::max(1, (int)ceilf(output.GetDocumentBounds().bottom / pageHeightMM));

	const float lineToBaselineAdjustMM = spec.mbBit0Top ? headHeightMM - spec.mDotRadiusMM : spec.mDotRadiusMM;

	// PDF by default scales the font's em size to 1 unit high, so
	// we need to scale by the desired height of the em square -- which
	// is 1.024 times the height in 10000th of an inch.
	const float mmToUnits = 10000.0f / 25.4f;
	const float dotFontSize = ttfAscentMM * mmToUnits * 1024.0f / 1000.0f;

	// compute basic ttf metrics for the char font
	const vdrect32f& charFontBox = output.GetMaxCharBounds();
	const float charEmSize = std::max<float>(-charFontBox.top, charFontBox.right);
	const float charFontSize = charEmSize * mmToUnits * 1024.0f / 1000.0f;

	const float charMMToUnits = 1000.0f / charEmSize;
	const sint32 charFontAscentUnits = VDRoundToInt32(-charFontBox.top * charMMToUnits);
	const sint32 charFontDescentUnits = VDRoundToInt32(-charFontBox.bottom * charMMToUnits);
	const sint32 charFontMinXUnits = VDRoundToInt32(charFontBox.left * charMMToUnits);
	const sint32 charFontMaxAdvanceUnits = VDRoundToInt32(charFontBox.right * charMMToUnits);

	bool print2Used = false;

	for(int page = 0; page < numPages; ++page) {
		// preallocate object IDs for page and page content objects
		objectOffsets.push_back((uint32)textOut.Pos());
		const uint32 pageObj = (uint32)objectOffsets.size();

		objectOffsets.push_back((uint32)textOut.Pos());
		const uint32 pageContentsObj = (uint32)objectOffsets.size();

		// render page contents
		VDStringA s;

		s.sprintf("%.3f g", powf(ATPrinterRasterizer::kBlackLevel, 1.0f/2.2f));

		ATPrinterGraphicalOutput::CullInfo cullInfo {};
		vdrect32f pageRect(0.0f, pageHeightMM * (float)page, pageWidthMM, pageHeightMM * (float)(page + 1));

		bool print2UsedOnPage = false;
		if (output.PreCull(cullInfo, pageRect)) {
			vdfastvector<ATPrinterGraphicalOutput::RenderColumn> cols;
			float lineY = 0;

			while(output.ExtractNextLineAsDotsOrChars(cols, lineY, cullInfo, pageRect)) {
				// encode 10000 units / 1" per tile
				const float unitsPerPoint = 10000.0f / 72.0f;
				const float pointsPerUnit = 1.0f / unitsPerPoint;

				s.append_sprintf(
					" q %.10f 0 0 %.10f 0 0 cm"
					, pointsPerUnit
					, pointsPerUnit
				);

				// Partition column graphics and characters
				const auto itCharSplit = std::partition(
					cols.begin(),
					cols.end(),
					[](const ATPrinterGraphicalOutput::RenderColumn& col) {
						return (col.mPins & col.kCharBit) != 0;
					}
				);

				const float dotFontAdvanceWidth = 2 * ttfDotRadius;

				const float dotFontMmToMils = 1000.0f * mmToUnits / dotFontSize;
				const float charFontMmToMils = 1000.0f * mmToUnits / charFontSize;

				vdspan<ATPrinterGraphicalOutput::RenderColumn> charColumns(cols.begin(), itCharSplit);

				if (!charColumns.empty()) {
					std::sort(charColumns.begin(), charColumns.end(),
						[](const ATPrinterGraphicalOutput::RenderColumn& x, const ATPrinterGraphicalOutput::RenderColumn& y) {
							return x.mX < y.mX;
						}
					);

					// Set the line origin and begin the text object. X is easy as it's just the left
					// edge, but Y needs to be adjusted. In PDF, it needs to be set to the baseline,
					// but it's the center of the first dot in the printer output.
					const int fxx0 = VDRoundToInt32((cols[0].mX - pageRect.left) * mmToUnits);
					const int fxy0 = VDRoundToInt32((pageRect.bottom - (lineY + lineToBaselineAdjustMM)) * mmToUnits);

					// begin text object, update text transform, and begin array for TJ command
					print2UsedOnPage = true;

					s.append_sprintf(
						" /Print2 %.2f Tf"
						, charFontSize
					);

					s.append_sprintf(" BT %d %d Td [", fxx0, fxy0);

					float xoff = (-pageRect.left * mmToUnits - fxx0) * 1000.0f / charFontSize;
					for(const ATPrinterGraphicalOutput::RenderColumn& col : charColumns) {
						const int dx = VDRoundToInt32(col.mX * charFontMmToMils + xoff);

						// apply horizontal offset if needed
						if (dx)
							s.append_sprintf("%d", -dx);

						// print pins using character

						const uint32 ch = col.mPins - col.kCharBit;
						if (assignedCharLookup.size() <= ch)
							assignedCharLookup.resize(ch + 1, -1);

						sint32& chIndex = assignedCharLookup[ch];
						if (chIndex < 0) {
							chIndex = (sint32)assignedChars.size() + 2;
							assignedChars.push_back(ch);
						}

						s.append_sprintf("<%04X>", (unsigned)chIndex);

						// Update X offset tracking based on advance width and applied adjustment.
						xoff -= output.GetCharAdvance(ch) * charFontMmToMils + (float)dx;
					}

					// print text and end text object
					s += "] TJ ET";
				}

				vdspan<ATPrinterGraphicalOutput::RenderColumn> dotColumns(itCharSplit, cols.end());

				if (!dotColumns.empty()) {
					// if the head orientation is top-down, reverse the bit pattern to bottom-up
					if (spec.mbBit0Top) {
						int shift = 32 - spec.mNumPins;
						for(ATPrinterGraphicalOutput::RenderColumn& col : dotColumns) {
							uint32 v = col.mPins;

							v = ((v & 0x55555555) << 1) + ((v >> 1) & 0x55555555);
							v = ((v & 0x33333333) << 2) + ((v >> 2) & 0x33333333);
							v = ((v & 0x0F0F0F0F) << 4) + ((v >> 4) & 0x0F0F0F0F);
							v = ((v & 0x00FF00FF) << 8) + ((v >> 8) & 0x00FF00FF);
							v = (v >> 16) + (v << 16);

							col.mPins = v >> shift;
						}
					}

					s.append_sprintf(
						" /Print %.2f Tf"
						, dotFontSize
					);

					// The font only accommodates up to 7 pins, so if there are more than 7
					// pins in the head, we may need to do more than one band.
					const int numBands = (spec.mNumPins + 6) / 7;

					for(int band = 0; band < numBands; ++band) {
						// Find the first column that has anything in the band. We do this to detect if a band
						// is empty so we can skip the band.
						const int bandPinShift = 7 * band;
						vdspan bandColumns(dotColumns);
						uint32 bandPins = 0x7F << bandPinShift;

						while(!bandColumns.empty()) {
							const uint32 pins = bandColumns.front().mPins;

							if (pins & bandPins)
								break;

							bandColumns = bandColumns.subspan(1);
						}

						if (bandColumns.empty())
							continue;

						// Set the line origin and begin the text object. X is easy as it's just the left
						// edge, but Y needs to be adjusted. In PDF, it needs to be set to the baseline,
						// but it's the center of the first dot in the printer output.
						//
						// The font also has its left edge at the left side of the dot while the printer
						// output tracks the centerline of the left column of dots, so there is also a
						// half dot horizontal offset to account for.
						const int fxx0 = VDRoundToInt32((bandColumns[0].mX - pageRect.left - dotRadiusMM) * mmToUnits);
						const int fxy0 = VDRoundToInt32((pageRect.bottom - (lineY + lineToBaselineAdjustMM - spec.mVerticalDotPitchMM * bandPinShift)) * mmToUnits);

						// Sort the columns in the band by ascending X position so we have the smallest
						// delta X offsets.
						std::sort(bandColumns.begin(), bandColumns.end(),
							[](const auto& a, const auto& b) {
								return a.mX < b.mX;
							}
						);

						// begin text object, update text transform, and begin array for TJ command
						s.append_sprintf(" BT %d %d Td [", fxx0, fxy0);

						// MERGE NOTE: the TJ adjustment must retain the dot-radius
						// offset too; otherwise it cancels the corrected line origin.
						// Test19 shifts character glyphs to match that raw-dot error.
						float xoff = (-(pageRect.left + dotRadiusMM) * mmToUnits - fxx0) * 1000.0f / dotFontSize;
						for(const auto& col : bandColumns) {
							uint32 pins = (col.mPins >> bandPinShift) & 0x7F;

							if (pins) {
								const int dx = VDRoundToInt32(col.mX * dotFontMmToMils + xoff);

								// apply horizontal offset if needed
								if (dx)
									s.append_sprintf("%d", -dx);

								// print pins using character
								s.append_sprintf("<%02X>", pins >= 0x60 ? 0x60 + pins : 0x20 + pins);

								// update X offset tracking based on advance width and applied adjustment
								xoff -= dotFontAdvanceWidth + (float)dx;
							}
						}

						// print text and end text object
						s += "] TJ ET";
					}
				}

				// end of tile - pop transform
				s += " Q";
			}
		}

		vdfastvector<ATPrinterGraphicalOutput::RenderVector> rvectors;
		output.ExtractVectors(rvectors, pageRect);

		if (!rvectors.empty()) {
			const float dotRadiusPts = spec.mDotRadiusMM * mmToPoints;
			uint32 lastLinearColor = ~UINT32_C(0);

			// push graphics state, set round end cap, and shift to Darken blend mode
			s.append_sprintf(" q 1 J %.2f w /BlendState gs", dotRadiusPts * 2.0f);

			for(const auto& rv : rvectors) {
				if (lastLinearColor != rv.mLinearColor) {
					lastLinearColor = rv.mLinearColor;

					const uint32 rgb = output.ConvertLinearColorToSrgb(rv.mLinearColor);

					s.append_sprintf(
						" %.2f %.2f %.2f RG"
						, (float)((rgb >> 16) & 0xFF) / 255.0f
						, (float)((rgb >>  8) & 0xFF) / 255.0f
						, (float)((rgb >>  0) & 0xFF) / 255.0f
					);
				}

				const vdfloat2 v1 = vdfloat2 { rv.mX1 - pageRect.left, pageRect.bottom - rv.mY1 } * mmToPoints;
				const vdfloat2 v2 = vdfloat2 { rv.mX2 - pageRect.left, pageRect.bottom - rv.mY2 } * mmToPoints;

				s.append_sprintf(
					" %.2f %.2f m %.2f %.2f l S"
					, v1.x, v1.y
					, v2.x, v2.y
				);
			}

			s += " Q";
		}

		// write page -- this is delayed so we know if the Print2 font was used
		beginObject(pageObj);
		textOut.FormatLine("%u 0 obj", pageObj);
		textOut.PutLine("<< /Type /Page");
		textOut.PutLine("/Parent 2 0 R");
		textOut.PutLine("/Resources <<");
		textOut.PutLine(" /Font <<");
		textOut.PutLine(" /Print 3 0 R");

		if (print2UsedOnPage) {
			print2Used = true;

			textOut.PutLine(" /Print2 6 0 R");
		}

		textOut.PutLine(" >>");
		textOut.PutLine(" /ExtGState <<");
		textOut.FormatLine("  /BlendState %u 0 R", blendStateObj);
		textOut.PutLine(" >>");
		textOut.PutLine(">>");
		textOut.FormatLine("/Contents %u 0 R", pageContentsObj);
		textOut.PutLine(">>");
		textOut.PutLine("endobj");

		// write page contents stream
		beginObject(pageContentsObj);
		ATPDFWriteStreamObject(
			textOut,
			pageContentsObj,
			vdspan<const uint8>((const uint8 *)s.data(), s.size()),
			false);
	}

	// write pages table
	objectOffsets[1] = (uint32)textOut.Pos();
	textOut.PutLine("2 0 obj");
	textOut.PutLine("<< /Type /Pages");
	textOut.PutLine("/Kids [");

	for(int i=0; i<numPages; ++i)
		textOut.FormatLine("%u 0 R", basePageObj + i*2);

	textOut.PutLine("]");
	textOut.FormatLine("/Count %u", numPages);
	textOut.FormatLine("/MediaBox [0 0 %f %f]", pageWidthMM * mmToPoints, pageHeightMM * mmToPoints);
	textOut.PutLine(">>");
	textOut.PutLine("endobj");

	// define character font, if needed (6)
	if (!print2Used) {
		// no character font needed -- just define it as a dummy object
		beginObject(print2Obj);
		textOut.FormatLine("%u 0 obj 0 endobj", print2Obj);
	} else {
		//////////////////////////////////////
		// build char font

		vdautoptr<ATTrueTypeEncoder> ttf(new ATTrueTypeEncoder);
		const sint32 charFontDotRadius = VDRoundToInt32(charMMToUnits * spec.mDotRadiusMM);

		const sint32 charFontDefaultAdvanceWidth = charFontDotRadius * 2;
		ttf->SetDefaultAdvanceWidth(charFontDefaultAdvanceWidth);

		// create initial dot / break char
		const auto dotGlyph = ttf->BeginSimpleGlyph();
		ttf->AddCircleContour(charFontDotRadius, charFontDotRadius, charFontDotRadius);
		ttf->EndSimpleGlyph();

		// create space char
		const auto spaceGlyph = ttf->BeginSimpleGlyph();
		ttf->EndSimpleGlyph();

		ttf->SetBreakChar(0x20);
		ttf->MapCharacter(0x20, spaceGlyph);

		// print characters
		vdfastvector<sint32> cidAdvanceWidths;
		uint32 chIndex = 0x21;
		for(uint32 ch : assignedChars) {
			const float charAdvance = output.GetCharAdvance(ch);
			const float dotdy = spec.mbBit0Top ? -spec.mVerticalDotPitchMM : spec.mVerticalDotPitchMM;
			const float doty0 = spec.mbBit0Top ? -dotdy * (float)(spec.mNumPins - 1) : 0;

			ttf->MapCharacter(chIndex++, ttf->BeginCompositeGlyph());

			for(const auto& cc : output.GetCharColumns(ch)) {
				// MERGE NOTE: test19 removes this subtraction to align with
				// the raw-dot TJ offset error corrected above. Keep both paths
				// centered on the actual printer dot: this glyph's center is
				// +radius, so cancel that displacement here.
				const sint32 cx = VDRoundToInt32((cc.mXOffset - spec.mDotRadiusMM) * charMMToUnits);

				for(uint32 dots = cc.mDots; dots; dots &= dots - 1) {
					int index = VDFindLowestSetBitFast(dots);

					ttf->AddGlyphReference(dotGlyph, cx, VDRoundToInt32((doty0 + dotdy * (float)index) * charMMToUnits));
				}
			}

			const sint32 advanceWidthUnits = VDRoundToInt32(charAdvance * charMMToUnits);
			cidAdvanceWidths.push_back(advanceWidthUnits);
			ttf->SetGlyphAdvanceWidth(advanceWidthUnits);
			ttf->EndCompositeGlyph();
		}

		ttf->SetName(ATTrueTypeName::Copyright, "None - autogenerated");
		ttf->SetName(ATTrueTypeName::FontFamily, "Altirra Print 2");
		ttf->SetName(ATTrueTypeName::FontSubfamily, "Normal");
		ttf->SetName(ATTrueTypeName::FullFontName, "Altirra Print 2 Normal");
		ttf->SetName(ATTrueTypeName::UniqueFontIdentifier, "Altirra Print 2 Normal");
		ttf->SetName(ATTrueTypeName::Version, "Version 1.0");
		ttf->SetName(ATTrueTypeName::PostScriptName, "Print2");

		const vdspan<const uint8> fontData = ttf->Finalize();

		// build PDF objects
		objectOffsets.push_back((uint32)textOut.Pos());
		const uint32 charFontToUnicodeObj = (uint32)objectOffsets.size();

		objectOffsets.push_back((uint32)textOut.Pos());
		const uint32 charFontObj = (uint32)objectOffsets.size();

		objectOffsets.push_back((uint32)textOut.Pos());
		const uint32 charFontDescriptorObj = (uint32)objectOffsets.size();

		objectOffsets.push_back((uint32)textOut.Pos());
		const uint32 charTrueTypeFontObj = (uint32)objectOffsets.size();

		// define character font
		beginObject(print2Obj);
		textOut.FormatLine("%u 0 obj", print2Obj);
		textOut.PutLine("<<");
		textOut.PutLine("/Type /Font");
		textOut.PutLine("/Subtype /Type0");
		// MERGE NOTE: For a Type 2 CIDFont descendant, PDF requires the
		// Type 0 and CIDFont BaseFont names to match. test15 appended a
		// second "-Print2" here.
		textOut.PutLine("/BaseFont /AAAAAA+Print2 ");
		textOut.PutLine("/Encoding /Identity-H ");
		textOut.FormatLine("/DescendantFonts [%u 0 R]", charFontObj);
		textOut.FormatLine("/ToUnicode %u 0 R", charFontToUnicodeObj);
		textOut.PutLine(">>");
		textOut.PutLine("endobj");

		beginObject(charFontObj);
		textOut.FormatLine("%u 0 obj", charFontObj);
		textOut.PutLine("<<");
		textOut.PutLine("/Type /Font");
		textOut.PutLine("/Subtype /CIDFontType2");
		textOut.PutLine("/BaseFont /AAAAAA+Print2");
		textOut.PutLine("/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >>");
		textOut.FormatLine("/FontDescriptor %u 0 R", charFontDescriptorObj);
		textOut.FormatLine("/DW %u", (charFontDefaultAdvanceWidth * 1000 + 512) / 1024);
		textOut.PutLine("/W [2 [");

		for(sint32 advanceWidth : cidAdvanceWidths) {
			textOut.FormatLine("%d", (advanceWidth * 1000 + 512) / 1024);
		}

		textOut.PutLine("]]");
		textOut.PutLine("/CIDToGIDMap /Identity");
		textOut.PutLine(">>");
		textOut.PutLine("endobj");

		// add font descriptor
		beginObject(charFontDescriptorObj);
		textOut.FormatLine("%u 0 obj", charFontDescriptorObj);
		textOut.FormatLine(
			"<< /Type /FontDescriptor "
			// MERGE NOTE: test15 accidentally reused the dot font's name
			// here. A subset's FontName and BaseFont must identify the same
			// font.
			"/FontName /AAAAAA+Print2 "
			"/Flags 4 "
			"/FontBBox [%d %d %d %d] ",
			charFontMinXUnits,
			-charFontDescentUnits,
			charFontMaxAdvanceUnits,
			charFontAscentUnits);

		textOut.FormatLine(
			"/ItalicAngle 0 "
			"/Ascent %d "
			"/Descent %d "
			"/CapHeight %d "
			"/StemV 80 "
			"/MissingWidth %d "
			"/FontFile2 %u 0 R >>",
			charFontAscentUnits,
			-charFontDescentUnits,
			charFontAscentUnits,
			charFontMaxAdvanceUnits,
			charTrueTypeFontObj
		);
		textOut.PutLine("endobj");

		// add ToUnicode table

		VDStringA uniTbl;

		uniTbl += "/CIDInit /ProcSet findresource begin ";

			uniTbl += "12 dict begin ";
				uniTbl += "begincmap ";
					uniTbl += "/CIDSystemInfo ";
					uniTbl += "<< /Registry (Adobe) ";
					uniTbl += "/Ordering (UCS) ";
					uniTbl += "/Supplement 0 ";
					uniTbl += ">> def ";
					uniTbl += "/CMapName /Print2 def ";
					uniTbl += "/CMapType 2 def ";

					uniTbl += "1 begincodespacerange ";
						uniTbl += "<0000> <FFFF>  ";
					uniTbl += "endcodespacerange ";

					// emit character to Unicode mappings in batches of 100 max (spec limit)
					const size_t numCharMappings = assignedChars.size();
					for(size_t i = 0; i < numCharMappings; i += 100) {
						const size_t m = std::min<size_t>(100, numCharMappings - i);

						uniTbl.append_sprintf("%u beginbfchar ", m);
							for(size_t j = 0; j < m; ++j) {
								// CIDs 0 and 1 are the space and dot characters, so the first
								// assigned character starts at CID 2
								const uint32 cid = i+j+2;

								// break surrogates if needed
								const uint32 ch = assignedChars[i+j];
								const uint32 uch = output.GetCharUnicodeChar(ch);

								if (uch >= 0x10000 && uch <= 0x10FFFF) {
									// MERGE NOTE: test15 omitted the required
									// subtraction before splitting a Unicode
									// scalar into UTF-16 surrogates, producing
									// invalid ToUnicode mappings above the BMP.
									const uint32 uch2 = uch - 0x10000;

									uniTbl.append_sprintf("<%04X> <%04X%04X> ",
										cid,
										0xD800 + (uch2 >> 10),
										0xDC00 + (uch2 & 0x03FF));
								} else if (uch
									&& (uch < 0xD800 || uch > 0xDFFF))
									uniTbl.append_sprintf("<%04X> <%04X> ", cid, uch);
								else {
									// Ideally, we'd be able to suppress copying for characters that don't have an
									// equivalent. But it seems that there isn't such a capability; attempting to
									// use an empty mapping <> results in Acrobat Reader copying out broken
									// characters. Thus, we use spaces for now.
									uniTbl.append_sprintf("<%04X> <0020> ", cid);
								}
							}
						uniTbl += "endbfchar ";
					}

				uniTbl += "endcmap ";
				uniTbl += "CMapName currentdict /CMap defineresource pop ";
			uniTbl += "end ";

		uniTbl += "end ";

		beginObject(charFontToUnicodeObj);
		textOut.FormatLine("%u 0 obj", charFontToUnicodeObj);
		textOut.FormatLine("<</Length %u>>", uniTbl.size());
		textOut.PutLine("stream");
		textOut.Write(uniTbl.data(), uniTbl.size());
		textOut.PutLine();
		textOut.PutLine("endstream");
		textOut.PutLine("endobj");

		beginObject(charTrueTypeFontObj);
		ATPDFWriteStreamObject(
			textOut,
			charTrueTypeFontObj,
			fontData,
			true);
	}

	// write info table
	objectOffsets.push_back((uint32)textOut.Pos());
	const uint32 infoTableId = (uint32)objectOffsets.size();
	textOut.FormatLine("%u 0 obj", infoTableId);
	textOut.FormatLine("<< /Producer (%ls)", AT_PROGRAM_NAME_STR  L" " AT_VERSION_STR);

	VDExpandedDate utcTime = VDGetUtcDate(VDGetCurrentDate());

	// There is a disagreement between ISO 32000 and Adobe's PDF 1.7 reference. ISO 32000 says
	// that dates have only one apos between the time zone hour and minute offsets; Adobe says
	// there is an apos at the end as well. We follow Adobe since that seems to be prevailing
	// practice.
	textOut.FormatLine("/CreationDate (D:%04u%02u%02u%02u%02u%02uZ00'00')"
		, utcTime.mYear
		, utcTime.mMonth
		, utcTime.mDay
		, utcTime.mHour
		, utcTime.mMinute
		, utcTime.mSecond
	);
	textOut.PutLine(">>");
	textOut.PutLine("endobj");

	// write cross-reference table
	uint32 xrefPos = (uint32)textOut.Pos();

	textOut.PutLine("xref");
	textOut.FormatLine("0 %u", (unsigned)objectOffsets.size() + 1);
	textOut.PutLine("0000000000 65535 f");

	for(uint32 offset : objectOffsets)
		textOut.FormatLine("%010u 00000 n", offset);

	textOut.PutLine("trailer");
	textOut.PutLine("<< /Root 1 0 R");
	textOut.FormatLine("/Info %u 0 R", infoTableId);
	textOut.FormatLine("/Size %u", (unsigned)(objectOffsets.size() + 1));
	textOut.PutLine(">> startxref");
	textOut.FormatLine("%u", xrefPos);
	textOut.PutLine("%%EOF");
}

void ATPrinterExportAsSVG(const wchar_t *path, ATPrinterGraphicalOutput& output, bool useBlending) {
	const ATPrinterGraphicsSpec& spec = output.GetGraphicsSpec();
	vdrect32f docBounds = output.GetDocumentBounds();

	static constexpr float kUnitsPerMM = 100.0f;

	// Round off the document bounds to a multiple of 0.1mm and ensure that the
	// document isn't zero sized
	docBounds.left = roundf(docBounds.left * kUnitsPerMM) / kUnitsPerMM;
	docBounds.top = roundf(docBounds.top * kUnitsPerMM) / kUnitsPerMM;
	docBounds.right = roundf(docBounds.right * kUnitsPerMM) / kUnitsPerMM;
	docBounds.bottom = roundf(docBounds.bottom * kUnitsPerMM) / kUnitsPerMM;
	docBounds.right = std::max<float>(docBounds.right, docBounds.left + 10.0f);
	docBounds.bottom = std::max<float>(docBounds.bottom, docBounds.top + 10.0f);

	const float width = docBounds.width();
	const float height = docBounds.height();

	VDFileStream fileOut(path, nsVDFile::kWrite | nsVDFile::kCreateAlways | nsVDFile::kDenyAll);
	VDTextOutputStream textOut(&fileOut);

	textOut.PutLine("<?xml version=\"1.0\" standalone=\"yes\"?>");
	textOut.PutLine("<!DOCTYPE svg PUBLIC \"-//W3C/DTD/SVG 1.1//EN\" \"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">");
	textOut.FormatLine(
		"<svg width=\"%gmm\" height=\"%gmm\" viewBox=\"0 0 %d %d\" version=\"1.1\" "
			" xmlns=\"http://www.w3.org/2000/svg\" xmlns:l=\"http://www.w3.org/1999/xlink\">"
		, roundf(width * kUnitsPerMM) / kUnitsPerMM
		, roundf(height * kUnitsPerMM) / kUnitsPerMM
		, VDRoundToInt32(width * kUnitsPerMM)
		, VDRoundToInt32(height * kUnitsPerMM)
	);

	ATPrinterGraphicalOutput::CullInfo cullInfo {};
	const float dotDY = spec.mVerticalDotPitchMM * kUnitsPerMM * (spec.mbBit0Top ? 1.0f : -1.0f);
	const int dotRadius = VDRoundToInt32(spec.mDotRadiusMM * 100);

	uint32 dotMasksUsed[8] {};

	if (output.PreCull(cullInfo, docBounds)) {
		vdfastvector<ATPrinterGraphicalOutput::RenderColumn> cols;
		float rawLineY = 0;
		while(output.ExtractNextLineAsDots(cols, rawLineY, cullInfo, docBounds)) {
			uint32 allDotMask = 0;

			for(auto& renderColumn : cols)
				allDotMask |= renderColumn.mPins;

			const float lineY = (rawLineY - docBounds.top) * kUnitsPerMM;

			for(int i=0; i<4; ++i) {
				const int subMaskShift = i * 8;

				if (!((allDotMask >> subMaskShift) & 0xFF))
					continue;

				textOut.FormatLine("<g transform=\"translate(0,%d)\">", VDRoundToInt32(lineY + dotDY * 8 * i));

				for(auto& renderColumn : cols) {
					const sint32 dotX = VDRoundToInt32((renderColumn.mX - docBounds.left) * kUnitsPerMM);
					const uint32 mask = renderColumn.mPins;

					const uint8 subMask = (uint8)(mask >> subMaskShift);
					if (subMask) {
						dotMasksUsed[subMask >> 5] |= UINT32_C(1) << (subMask & 31);

						textOut.FormatLine("<use x=\"%d\" l:href=\"#m%02X\"/>", dotX, subMask);
					}
				}

				textOut.PutLine("</g>");
			}
		}
	}

	vdfastvector<ATPrinterGraphicalOutput::RenderVector> rvectors;
	output.ExtractVectors(rvectors, docBounds);

	vdhashmap<uint32, uint32> vectorColorMap;
	vdfastvector<uint32> vectorColorList;

	if (!rvectors.empty()) {
		for(const ATPrinterGraphicalOutput::RenderVector& rv : rvectors) {
			auto r = vectorColorMap.insert(rv.mLinearColor);
			if (r.second) {
				r.first->second = (uint32)vectorColorList.size();
				vectorColorList.push_back(rv.mLinearColor);
			}
		}

		// sort colors by decreasing luminance
		std::sort(
			vectorColorList.begin(),
			vectorColorList.end(),
			[](uint32 lca, uint32 lcb) {
				return VDColorRGB::FromRGB8(lca).Luma() > VDColorRGB::FromRGB8(lcb).Luma();
			}
		);

		// draw vectors for each pen
		for(const uint32 linearColor : vectorColorList) {
			const uint32 srgbColor = VDColorRGB(vdfloat32x4::unpacku8(linearColor) * (1.0f / 64.0f)).LinearToSRGB().ToBGR8();

			textOut.FormatLine("<g style=\"stroke:#%06X; stroke-width:%d; stroke-linecap:round; stroke-linejoin:round; fill:none%s\">"
				, srgbColor
				, dotRadius*2
				, useBlending ? "; mix-blend-mode:darken" : ""
			);

			using RenderVector = ATPrinterGraphicalOutput::RenderVector;

			auto it = std::partition(
				rvectors.begin(),
				rvectors.end(),
				[=](const RenderVector& rv) {
					return rv.mLinearColor != linearColor;
				}
			);

			// isolate vectors for the current color group
			vdspan<RenderVector> rvsThisColor(it, rvectors.end());

			// sort vectors for determinism
			std::sort(
				rvsThisColor.begin(),
				rvsThisColor.end(),
				[](const RenderVector& a, const RenderVector& b) {
					if (a.mY1 != b.mY1) return a.mY1 < b.mY1;
					if (a.mX1 != b.mX1) return a.mX1 < b.mX1;
					if (a.mY2 != b.mY2) return a.mY2 < b.mY2;
					return a.mX2 < b.mX2;
				}
			);

			// Connectivity data structures.
			//
			// The vectors are preprocessed to link the endpoints of all vectors
			// into doubly linked lists for each shared point. Each vectors has
			// two endpoint indices, 2*k+0 and 2*k+1 for k being the vector
			// index. This allows quickly traversing the undirected graph to
			// pull out connected lines or line loops.

			struct LinkedVector {
				uint32 mPrevEndpointIndex[2];
				uint32 mNextEndpointIndex[2];
				uint32 mPointIndex[2];
			};

			struct LinkedPoint {
				sint32 mX;
				sint32 mY;

				bool operator==(const LinkedPoint& pt) const = default;
			};

			struct LinkedPointHash {
				// Keep this as a non-static call operator when syncing from
				// upstream. test14 used C++23 static operator(), but our CI
				// compiler floor includes GCC 12, which rejects that form; the
				// vdhashmap hasher object is instance-callable anyway.
				size_t operator()(const LinkedPoint& pt) const {
					return pt.mX + (pt.mY << 16);
				}
			};

			vdfastvector<LinkedPoint> linkedPoints;
			vdfastvector<LinkedVector> linkedVectors;

			struct PointLookupInfo {
				uint32 mPointIndex;			// index in linkedPoints[]
				uint32 mLastLineEndpoint;	// vector endpoint
			};

			vdhashmap<LinkedPoint, PointLookupInfo, LinkedPointHash> pointLookup;

			for(const RenderVector& rv : rvsThisColor) {
				const LinkedPoint pt[2] {
					LinkedPoint {
						VDRoundToInt32((rv.mX1 - docBounds.left) * kUnitsPerMM),
						VDRoundToInt32((rv.mY1 - docBounds.top ) * kUnitsPerMM)
					},
					LinkedPoint {
						VDRoundToInt32((rv.mX2 - docBounds.left) * kUnitsPerMM),
						VDRoundToInt32((rv.mY2 - docBounds.top ) * kUnitsPerMM)
					}
				};

				const uint32 endpointIndex0 = (uint32)linkedVectors.size() * 2;
				auto& lv = linkedVectors.emplace_back();

				for(int endpoint = 0; endpoint < 2; ++endpoint) {
					const auto result = pointLookup.insert(pt[endpoint]);
					PointLookupInfo& lp = result.first->second;
					if (result.second) {
						lp.mPointIndex = (uint32)linkedPoints.size();

						linkedPoints.push_back(pt[endpoint]);

						// establish new point loop around this endpoint
						lv.mPrevEndpointIndex[endpoint] = endpointIndex0 + endpoint;
						lv.mNextEndpointIndex[endpoint] = endpointIndex0 + endpoint;
					} else {
						// link this endpoint into the point loop
						const uint32 prevEndpointIndex = lp.mLastLineEndpoint;
						LinkedVector& lvPrev = linkedVectors[prevEndpointIndex >> 1];

						const uint32 nextEndpointIndex = lvPrev.mNextEndpointIndex[prevEndpointIndex & 1];
						LinkedVector& lvNext = linkedVectors[nextEndpointIndex >> 1];

						lvPrev.mNextEndpointIndex[prevEndpointIndex & 1] = endpointIndex0 + endpoint;
						lv.mPrevEndpointIndex[endpoint] = prevEndpointIndex;
						lv.mNextEndpointIndex[endpoint] = nextEndpointIndex;
						lvNext.mPrevEndpointIndex[nextEndpointIndex & 1] = endpointIndex0 + endpoint;
					}

					lp.mLastLineEndpoint = endpointIndex0 + endpoint;
					lv.mPointIndex[endpoint] = lp.mPointIndex;
				}
			}

			static constexpr uint32 kAlreadyProcessedVector = UINT32_MAX;

			VDStringA pathStr;
			sint32 lastX = INT32_MIN;
			sint32 lastY = INT32_MIN;

			bool lineStarted = false;
			sint32 lineDX = 0;
			sint32 lineDY = 0;

			const auto flushLine = [&] {
				if (!lineStarted)
					return;

				if (lineDY == 0) {
					if (lineDX)
						pathStr.append_sprintf("h%d", lineDX);
				} else if (lineDX == 0)
					pathStr.append_sprintf("v%d", lineDY);
				else
					pathStr.append_sprintf("l%d%c%d", lineDX, lineDY < 0 ? '-' : ' ', abs(lineDY));

				lineDX = 0;
				lineDY = 0;
			};

			const auto isCollinear = [](sint32 dx1, sint32 dy1, sint32 dx2, sint32 dy2) {
				// Check if two vectors is collinear and in the same direction:
				//	(A / |A|) * (B / |B|) = 1
				//
				// For integer math, this is reorganized to:
				//	(A*B) == |A|^2 * |B|^2
				//
				// As the rhs is non-negative, the math can switch to unsigned mid-way, allowing
				// slightly simpler unsigned 128-bit math instead of signed 128-bit.

				if ((dx1 | dy1) == 0)
					return true;

				if ((dx2 | dy2) == 0)
					return true;

				const sint64 dotProduct = (sint64)dx1 * dx2 + (sint64)dy1 * dy2;

				// since both vectors are non-zero by this point, the dot product must be
				// positive to pass
				if (dotProduct <= 0)
					return false;

				const uint64 prevSq = (uint64)((sint64)dx1 * dx1 + (sint64)dy1 * dy1);
				const uint64 nextSq = (uint64)((sint64)dx2 * dx2 + (sint64)dy2 * dy2);

				const vduint128 dotSq = VDUMul64x64To128(dotProduct, dotProduct);
				const vduint128 magSq = VDUMul64x64To128(prevSq, nextSq);

				return dotSq == magSq;
			};

			// initialize dead end stack with list of vectors that have dead ends
			vdfastvector<uint32> deadEndStack;

			for(uint32 endpointIndexCounter = 0, endpointIndexLimit = (uint32)linkedVectors.size() * 2;
				endpointIndexCounter < endpointIndexLimit;
				endpointIndexCounter += 2)
			{
				LinkedVector& lv = linkedVectors[endpointIndexCounter >> 1];

				if (lv.mNextEndpointIndex[0] == endpointIndexCounter)
					deadEndStack.push_back(endpointIndexCounter);
				else if (lv.mNextEndpointIndex[1] == endpointIndexCounter + 1)
					deadEndStack.push_back(endpointIndexCounter + 1);
			}

			for(uint32 endpointIndexCounter = 0, endpointIndexLimit = (uint32)linkedVectors.size() * 2; ;) {
				uint32 currentEndpointIndex;

				if (deadEndStack.empty()) {
					if (endpointIndexCounter >= endpointIndexLimit)
						break;

					currentEndpointIndex = endpointIndexCounter;
					endpointIndexCounter += 2;
				} else {
					currentEndpointIndex = deadEndStack.back();
					deadEndStack.pop_back();
				}

				for(;;) {
					LinkedVector& lv = linkedVectors[currentEndpointIndex >> 1];

					if (lv.mPrevEndpointIndex[0] == kAlreadyProcessedVector)
						break;

					// extract and write points
					const ptrdiff_t epEntry = currentEndpointIndex & 1;
					const ptrdiff_t epExit = 1 - epEntry;
					const auto [x1, y1] = linkedPoints[lv.mPointIndex[epEntry]];
					const auto [x2, y2] = linkedPoints[lv.mPointIndex[epExit]];

					const sint32 dx = x2 - x1;
					const sint32 dy = y2 - y1;

					if (lastX != x1 || lastY != y1) {
						// Discontinuity -- flush line and issue move-to
						flushLine();

						if (lineStarted)
							pathStr.append_sprintf("m%d%c%d", x1 - lastX, y1 < lastY ? '-' : ' ', abs(y1 - lastY));
						else
							pathStr.append_sprintf("M%d%c%d", x1, y1 < 0 ? '-' : ' ', abs(y1));

						lineDX = dx;
						lineDY = dy;
						lineStarted = true;
					} else {
						// Continuous line -- check if new vector is collinear
						if (!isCollinear(lineDX, lineDY, dx, dy)) {
							flushLine();
							lineDX = 0;
							lineDY = 0;
						}

						lineDX += dx;
						lineDY += dy;
					}

					lastX = x2;
					lastY = y2;

					if (pathStr.size() >= 1000) {
						flushLine();

						textOut.FormatLine("<path d=\"%s\"/>", pathStr.c_str());
						pathStr.clear();

						lineStarted = false;
						lastX = INT32_MIN;
						lastY = INT32_MIN;
					}

					// unlink starting endpoint
					const uint32 entryPrevIndex = lv.mPrevEndpointIndex[epEntry];
					const uint32 entryNextIndex = lv.mNextEndpointIndex[epEntry];
					const uint32 exitPrevIndex = lv.mPrevEndpointIndex[epExit];
					const uint32 exitNextIndex = lv.mNextEndpointIndex[epExit];

					// unlink entry endpoint from point list
					{
						LinkedVector& lvEntryPrev = linkedVectors[entryPrevIndex >> 1];
						LinkedVector& lvEntryNext = linkedVectors[entryNextIndex >> 1];

						uint32& entryPrevNext = lvEntryPrev.mNextEndpointIndex[entryPrevIndex & 1];
						uint32& entryNextPrev = lvEntryNext.mPrevEndpointIndex[entryNextIndex & 1];
						VDASSERT(entryPrevNext == currentEndpointIndex);
						VDASSERT(entryNextPrev == currentEndpointIndex);

						entryPrevNext = entryNextIndex;
						entryNextPrev = entryPrevIndex;

						// if we're leaving a dead end behind on another vector, add that onto the
						// stack for consideration
						if (entryNextIndex == entryPrevIndex && entryNextIndex != currentEndpointIndex)
							deadEndStack.push_back(entryNextIndex);
					}

					// unlink exit endpoint from point list
					{
						LinkedVector& lvExitPrev = linkedVectors[exitPrevIndex >> 1];
						LinkedVector& lvExitNext = linkedVectors[exitNextIndex >> 1];

						uint32& exitPrevNext = lvExitPrev.mNextEndpointIndex[exitPrevIndex & 1];
						uint32& exitNextPrev = lvExitNext.mPrevEndpointIndex[exitNextIndex & 1];
						VDASSERT(exitPrevNext == (currentEndpointIndex ^ 1));
						VDASSERT(exitNextPrev == (currentEndpointIndex ^ 1));

						exitPrevNext = exitNextIndex;
						exitNextPrev = exitPrevIndex;

						// we don't push dead ends onto the dead end stack, because we're just going
						// to continue with it
					}

					// mark this vector as already processed in case we see it again (such as
					// because it's on the dead end stack)
					lv.mPrevEndpointIndex[0] = kAlreadyProcessedVector;

					// Choose the next vector from the vectors linked to the exit end point. If there
					// is a collinear vector, make sure to choose that.

					currentEndpointIndex = exitNextIndex;

					do {
						LinkedVector& lvCandidateNext = linkedVectors[currentEndpointIndex >> 1];
						const uint32 nextEndpointIndex = lvCandidateNext.mNextEndpointIndex[currentEndpointIndex & 1];

						if (nextEndpointIndex == currentEndpointIndex) {
							// There is only one candidate point, so therefore there's no need to test it.
							break;
						}

						const LinkedPoint& candPt1 = linkedPoints[lvCandidateNext.mPointIndex[currentEndpointIndex & 1]];
						const LinkedPoint& candPt2 = linkedPoints[lvCandidateNext.mPointIndex[1 - (currentEndpointIndex & 1)]];
						const sint32 cdx = candPt2.mX - candPt1.mX;
						const sint32 cdy = candPt2.mY - candPt1.mY;

						if (isCollinear(lineDX, lineDY, cdx, cdy))
							break;

						currentEndpointIndex = nextEndpointIndex;
					} while(currentEndpointIndex != exitNextIndex);
				}
			}

			if (!pathStr.empty()) {
				flushLine();

				textOut.FormatLine("<path d=\"%s\"/>", pathStr.c_str());
				pathStr.clear();
			}

			rvectors.erase(it, rvectors.end());

			textOut.FormatLine("</g>");
		}
	}

	if (std::any_of(std::begin(dotMasksUsed), std::end(dotMasksUsed), [](uint32 v) { return v != 0; })) {
		textOut.PutLine("<defs>");

		// Export out dot pattern templates.
		//
		// It turns out that using lots of elements, even referenced, is very slow in most SVG
		// parsers due to inherited property handling. To work around this, we combine entire sets
		// of print head dot patterns into single paths that are reused. This is an order of
		// magnitude faster than reusing circle elements.

		for(int i=1; i<256; ++i) {
			if (dotMasksUsed[i >> 5] & (UINT32_C(1) << (i & 31))) {
				textOut.Format("<path id=\"m%02X\" fill=\"black\" stroke=\"none\" d=\"", i);

				uint32 mask = i;

				while(mask) {
					const int dotIndex = VDFindLowestSetBitFast(mask);
					mask &= mask - 1;

					textOut.Format("M0,%d a%d,%d 0 0 0 0,%d a%d,%d 0 0 0 0,%d"
						, VDRoundToInt32(dotDY * (float)dotIndex)
						, dotRadius
						, dotRadius
						, 2*dotRadius
						, dotRadius
						, dotRadius
						, -2*dotRadius
					);

					if (mask)
						textOut.Write(" ");
				}

				textOut.PutLine("\"/>");
			}
		}

		textOut.PutLine("</defs>");
	}

	textOut.PutLine("</svg>");
}
