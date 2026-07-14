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

void ATPrinterExportAsPDF(const wchar_t *path, ATPrinterGraphicalOutput& output, float suggestedPageWidthMM, float suggestedPageHeightMM) {
	// ZLIB header (Deflate 32K Normal)
	static constexpr uint8 kZLIBHeader[] { 0x78, 0x9C };

	VDFileStream fileOut(path, nsVDFile::kWrite | nsVDFile::kCreateAlways | nsVDFile::kDenyAll);
	VDTextOutputStream textOut(&fileOut);

	vdfastvector<uint32> objectOffsets;

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

	// Create missing char
	ttf->BeginSimpleGlyph();
	ttf->EndSimpleGlyph();

	// Create space
	ttf->MapCharacter(0x20, ttf->BeginSimpleGlyph());
	ttf->EndSimpleGlyph();

	// Create initial dot
	//
	// TrueType uses quadratic splines with clockwise orientation in a bottom-up coordinate
	// system. We use 8 on-curve points at octants and round it off with another 8 off-curve
	// points. For a unit circle, the length of an octagonal segment is:
	//
	//  hypot(1-sqrt(2)/2, sqrt(2)/2) = sqrt(2 - sqrt(2)) = 0.765367
	//
	// The law of cosines then gives the distance for the control point:
	//
	//  c^2 = a^2 + b^2 - 2ab cos theta    (theta = 22.5d, a=b)
	//  2 - sqrt(2) = 2*a^2 * (1 - cos(135d))
	//  2 - sqrt(2) = 2*a^2 * (1 + sqrt(2)/2)
	//  a^2 = [2 - sqrt(2)] / [2 + sqrt(2)]
	//  a = sqrt(2) - 1

	auto dotGlyph = ttf->BeginSimpleGlyph();
	ttf->MapCharacter(0x21, dotGlyph);

	{
		const int r = ttfDotRadius;
		const int r2 = r*2;
		const int k = (ttfDotRadius * 724) / 1024;	// sqrt(2)/2
		const int c = (ttfDotRadius * 424) / 1024;	// sqrt(2) - 1

		ttf->AddGlyphPoint(r,   0,   true );
		ttf->AddGlyphPoint(r-c, 0,   false);
		ttf->AddGlyphPoint(r-k, r-k, true );
		ttf->AddGlyphPoint(0,   r-c, false);
		ttf->AddGlyphPoint(0,   r,   true );
		ttf->AddGlyphPoint(0,   r+c, false);
		ttf->AddGlyphPoint(r-k, r+k, true );
		ttf->AddGlyphPoint(r-c, r2,  false);
		ttf->AddGlyphPoint(r,   r2,  true );
		ttf->AddGlyphPoint(r+c, r2,  false);
		ttf->AddGlyphPoint(r+k, r+k, true );
		ttf->AddGlyphPoint(r2,  r+c, false);
		ttf->AddGlyphPoint(r2,  r,   true );
		ttf->AddGlyphPoint(r2,  r-c, false);
		ttf->AddGlyphPoint(r+k, r-k, true );
		ttf->AddGlyphPoint(r+c, 0,   false);
		ttf->EndContour();
	}

	ttf->EndSimpleGlyph();

	// map 0x20-0x7F
	for(int i=2; i<96; ++i) {
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
		"/FontDescriptor 4 0 R >>");

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

	textOut.FormatLine("%u 0 obj", (uint32)objectOffsets.size());

	{
		VDMemoryBufferStream bufs;

		bufs.Write(kZLIBHeader, 2);

		uint32 adler32;

		{
			VDDeflateStream defs(bufs, VDDeflateChecksumMode::Adler32, VDDeflateCompressionLevel::Quick);
			defs.Write(fontData.data(), fontData.size());
			defs.Finalize();
			adler32 = defs.Adler32();
		}

		uint32 v = VDToBEU32(adler32);
		bufs.Write(&v, 4);

		unsigned clen = (unsigned)bufs.Length();
		textOut.FormatLine("<</Length %u/Length1 %u/Filter/FlateDecode>>", clen, (unsigned)fontData.size());

		textOut.PutLine("stream");

		textOut.Write((const char *)bufs.GetBuffer().data(), clen);

		textOut.PutLine();
		textOut.PutLine("endstream");
	}

	textOut.PutLine("endobj");

	// render pages
	uint32 basePageObj = (uint32)objectOffsets.size() + 1;

	static constexpr float mmToPoints = 72.0f / 25.4f;
	const float pageWidthMM = suggestedPageWidthMM > 0 ? suggestedPageWidthMM : spec.mPageWidthMM;
	const float pageHeightMM = suggestedPageHeightMM > 0 ? suggestedPageHeightMM : 11.0f * 25.4f;
	const float headHeightMM = spec.mDotRadiusMM * 2 + spec.mVerticalDotPitchMM * (float)(spec.mNumPins - 1);
	const int numPages = std::max(1, (int)ceilf(output.GetDocumentBounds().bottom / pageHeightMM));

	const float lineToBaselineAdjustMM = spec.mbBit0Top ? headHeightMM - spec.mDotRadiusMM : spec.mDotRadiusMM;

	for(int page = 0; page < numPages; ++page) {
		objectOffsets.push_back((uint32)textOut.Pos());
		textOut.FormatLine("%u 0 obj", (uint32)objectOffsets.size());
		textOut.PutLine("<< /Type /Page");
		textOut.PutLine("/Parent 2 0 R");
		textOut.PutLine("/Resources <<");
		textOut.PutLine(" /Font << /Print 3 0 R >>");
		textOut.PutLine(">>");
		textOut.FormatLine("/Contents %u 0 R", (uint32)objectOffsets.size() + 1);
		textOut.PutLine(">>");
		textOut.PutLine("endobj");

		objectOffsets.push_back((uint32)textOut.Pos());
		textOut.FormatLine("%u 0 obj", (uint32)objectOffsets.size());

		VDStringA s;

		s.sprintf("%.3f g", powf(ATPrinterRasterizer::kBlackLevel, 1.0f/2.2f));

		ATPrinterGraphicalOutput::CullInfo cullInfo {};
		vdrect32f pageRect(0.0f, pageHeightMM * (float)page, pageWidthMM, pageHeightMM * (float)(page + 1));

		if (output.PreCull(cullInfo, pageRect)) {
			const float mmToUnits = 10000.0f / 25.4f;

			vdfastvector<ATPrinterGraphicalOutput::RenderColumn> cols;
			float lineY = 0;
			while(output.ExtractNextLine(cols, lineY, cullInfo, pageRect)) {
				// if the head orientation is top-down, reverse the bit pattern to bottom-up
				if (spec.mbBit0Top) {
					int shift = 32 - spec.mNumPins;
					for(ATPrinterGraphicalOutput::RenderColumn& col : cols) {
						uint32 v = col.mPins;

						v = ((v & 0x55555555) << 1) + ((v >> 1) & 0x55555555);
						v = ((v & 0x33333333) << 2) + ((v >> 2) & 0x33333333);
						v = ((v & 0x0F0F0F0F) << 4) + ((v >> 4) & 0x0F0F0F0F);
						v = ((v & 0x00FF00FF) << 8) + ((v >> 8) & 0x00FF00FF);
						v = (v >> 16) + (v << 16);

						col.mPins = v >> shift;
					}
				}

				// encode 10000 units / 1" per tile
				const float unitsPerPoint = 10000.0f / 72.0f;
				const float pointsPerUnit = 1.0f / unitsPerPoint;

				// PDF by default scales the font's em size to 1 unit high, so
				// we need to scale by the desired height of the em square -- which
				// is 1.024 times the height in 10000th of an inch.
				const float fontSize = ttfAscentMM * mmToUnits * 1024.0f / 1000.0f;

				s.append_sprintf(
					" q %.10f 0 0 %.10f 0 0 cm /Print %.2f Tf"
					, pointsPerUnit
					, pointsPerUnit
					, fontSize
				);

				// The font only accommodates up to 7 pins, so if there are more than 7
				// pins in the head, we may need to do more than one band.
				const int numBands = (spec.mNumPins + 6) / 7;

				for(int band = 0; band < numBands; ++band) {
					// Find the first column that has anything in the band. We do this to detect if a band
					// is empty so we can skip the band.
					const int bandPinShift = 7 * band;
					vdspan bandColumns(cols);
					uint32 bandPins = 0x7F << bandPinShift;

					while(!bandColumns.empty()) {
						if (bandColumns.front().mPins & bandPins)
							break;

						bandColumns = bandColumns.subspan(1);
					}

					if (bandColumns.empty())
						continue;

					// Set the line origin and begin the text object. X is easy as it's just the left
					// edge, but Y needs to be adjusted. In PDF, it needs to be set to the baseline,
					// but it's the center of the first dot in the printer output.
					const int fxx0 = VDRoundToInt32((cols[0].mX - pageRect.left) * mmToUnits);
					const int fxy0 = VDRoundToInt32((pageRect.bottom - (lineY + lineToBaselineAdjustMM - spec.mVerticalDotPitchMM * bandPinShift)) * mmToUnits);

					// Sort the columns in the font by ascending X position so we have the smallest
					// delta X offsets.
					std::sort(cols.begin(), cols.end(),
						[](const auto& a, const auto& b) {
							return a.mX < b.mX;
						}
					);

					// begin text object, update text transform, and begin array for TJ command
					s.append_sprintf(" BT %d %d Td [", fxx0, fxy0);

					const float dotWidthMils = 2 * ttfDotRadius;
					const float mmToMils = 1000.0f * mmToUnits / fontSize;
					float xoff = (-pageRect.left * mmToUnits - fxx0) * 1000.0f / fontSize;
					for(const auto& col : bandColumns) {
						uint32 pins = (col.mPins >> bandPinShift) & 0x7F;

						if (pins) {
							const int dx = VDRoundToInt32(col.mX * mmToMils + xoff);

							// apply vertical offset if needed
							if (dx)
								s.append_sprintf("%d", -dx);

							// print pins using character
							s.append_sprintf("<%02X>", pins >= 0x60 ? 0x60 + pins : 0x20 + pins);

							// update X offset tracking based on advance width and applied adjustment
							xoff -= dotWidthMils + (float)dx;
						}
					}

					// print text and end text object
					s += "] TJ ET";
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

			// push graphics state, set round end cap
			s.append_sprintf(" q 1 J %.2f w", dotRadiusPts * 2.0f);

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

		VDMemoryBufferStream bufs;

		bufs.Write(kZLIBHeader, 2);

		uint32 adler32;

		{
			VDDeflateStream defs(bufs, VDDeflateChecksumMode::Adler32, VDDeflateCompressionLevel::Quick);
			defs.Write(s.data(), s.size());
			defs.Finalize();
			adler32 = defs.Adler32();
		}

		uint32 v = VDToBEU32(adler32);
		bufs.Write(&v, 4);

		textOut.FormatLine("<< /Length %u /Filter /FlateDecode >>", (unsigned)bufs.Length());
		textOut.PutLine("stream");

		const auto bufData = bufs.GetBuffer();
		textOut.Write((const char *)bufData.data(), (int)bufData.size());

		textOut.PutLine();
		textOut.PutLine("endstream");
		textOut.PutLine("endobj");
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
		while(output.ExtractNextLine(cols, rawLineY, cullInfo, docBounds)) {
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
						// Continuous line -- check if new vector is collinear:
						//	(A / |A|) * (B / |B|) = 1
						//
						// For integer math, this is reorganized to:
						//	(A*B) == |A|^2 * |B|^2
						//
						// As the rhs is non-negative, the math can switch to unsigned mid-way, allowing
						// slightly simpler unsigned 128-bit math instead of signed 128-bit.

						bool collinear = false;

						if ((lineDX == 0 && lineDY == 0) || (dx == 0 && dy == 0)) {
							collinear = true;
						} else {
							const sint64 dotProduct = (sint64)lineDX * dx + (sint64)lineDY * dy;

							if (dotProduct > 0) {
								const uint64 prevSq = (uint64)((sint64)lineDX * lineDX + (sint64)lineDY * lineDY);
								const uint64 nextSq = (uint64)((sint64)dx * dx + (sint64)dy * dy);

								const vduint128 dotSq = VDUMul64x64To128(dotProduct, dotProduct);
								const vduint128 magSq = VDUMul64x64To128(prevSq, nextSq);

								if (dotSq == magSq)
									collinear = true;
							}
						}

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
