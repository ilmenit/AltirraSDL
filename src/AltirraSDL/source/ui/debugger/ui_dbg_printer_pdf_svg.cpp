//	AltirraSDL - Printer output PDF + SVG writers (split from ui_dbg_printer.cpp Phase 3h)

#include <stdafx.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <vd2/system/binary.h>
#include <vd2/system/color.h>
#include <vd2/system/vdtypes.h>
#include "printeroutput.h"
#include "printer_internal.h"

bool SaveFramebufferAsPDF(const uint32 *framebuffer, int w, int h,
		float docWidthMM, float docHeightMM, const char *path) {
	FILE *f = fopen(path, "wb");
	if (!f)
		return false;

	// Build raw RGB data
	std::vector<uint8> rgb(w * h * 3);
	for (int i = 0; i < w * h; i++) {
		uint32 px = framebuffer[i];
		rgb[i * 3 + 0] = (uint8)(px >> 16);
		rgb[i * 3 + 1] = (uint8)(px >> 8);
		rgb[i * 3 + 2] = (uint8)(px);
	}

	// PDF page size in points (72 per inch)
	static constexpr float mmToPoints = 72.0f / 25.4f;
	float pageW = docWidthMM * mmToPoints;
	float pageH = docHeightMM * mmToPoints;

	std::vector<long> objOffsets;
	auto recordObj = [&]() {
		objOffsets.push_back(ftell(f));
	};

	fprintf(f, "%%PDF-1.4\n%%\x80\x80\x80\x80\n");

	// Object 1: Catalog
	recordObj();
	fprintf(f, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");

	// Object 2: Pages
	recordObj();
	fprintf(f, "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n");

	// Object 3: Page
	recordObj();
	fprintf(f, "3 0 obj\n<< /Type /Page /Parent 2 0 R "
		"/MediaBox [0 0 %.2f %.2f] "
		"/Contents 4 0 R "
		"/Resources << /XObject << /Im0 5 0 R >> >> >>\nendobj\n",
		pageW, pageH);

	// Object 4: Page content stream — draw image scaled to page size
	char contentStr[256];
	snprintf(contentStr, sizeof(contentStr),
		"q %.2f 0 0 %.2f 0 0 cm /Im0 Do Q", pageW, pageH);
	int contentLen = (int)strlen(contentStr);
	recordObj();
	fprintf(f, "4 0 obj\n<< /Length %d >>\nstream\n%s\nendstream\nendobj\n",
		contentLen, contentStr);

	// Object 5: Image XObject
	recordObj();
	fprintf(f, "5 0 obj\n<< /Type /XObject /Subtype /Image "
		"/Width %d /Height %d /ColorSpace /DeviceRGB "
		"/BitsPerComponent 8 /Length %d >>\nstream\n", w, h, (int)rgb.size());
	fwrite(rgb.data(), 1, rgb.size(), f);
	fprintf(f, "\nendstream\nendobj\n");

	// Cross-reference table
	long xrefOffset = ftell(f);
	fprintf(f, "xref\n0 %d\n", (int)objOffsets.size() + 1);
	fprintf(f, "0000000000 65535 f \n");
	for (size_t i = 0; i < objOffsets.size(); i++)
		fprintf(f, "%010ld 00000 n \n", objOffsets[i]);

	fprintf(f, "trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%ld\n%%%%EOF\n",
		(int)objOffsets.size() + 1, xrefOffset);

	fclose(f);
	return true;
}

// =========================================================================
// SVG writer — vector format using dot patterns, matching Windows output
// =========================================================================

bool SavePrinterOutputAsSVG(ATPrinterGraphicalOutput& output, const char *path) {
	const ATPrinterGraphicsSpec& spec = output.GetGraphicsSpec();
	vdrect32f docBounds = output.GetDocumentBounds();

	static constexpr float kUnitsPerMM = 100.0f;

	// Round off and ensure non-zero size
	docBounds.left = roundf(docBounds.left * kUnitsPerMM) / kUnitsPerMM;
	docBounds.top = roundf(docBounds.top * kUnitsPerMM) / kUnitsPerMM;
	docBounds.right = roundf(docBounds.right * kUnitsPerMM) / kUnitsPerMM;
	docBounds.bottom = roundf(docBounds.bottom * kUnitsPerMM) / kUnitsPerMM;
	docBounds.right = std::max<float>(docBounds.right, docBounds.left + 10.0f);
	docBounds.bottom = std::max<float>(docBounds.bottom, docBounds.top + 10.0f);

	const float width = docBounds.width();
	const float height = docBounds.height();

	FILE *f = fopen(path, "w");
	if (!f)
		return false;

	fprintf(f, "<?xml version=\"1.0\" standalone=\"yes\"?>\n");
	fprintf(f, "<!DOCTYPE svg PUBLIC \"-//W3C/DTD/SVG 1.1//EN\" "
		"\"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">\n");
	fprintf(f, "<svg width=\"%gmm\" height=\"%gmm\" viewBox=\"0 0 %d %d\" version=\"1.1\""
		" xmlns=\"http://www.w3.org/2000/svg\" xmlns:l=\"http://www.w3.org/1999/xlink\">\n",
		roundf(width * kUnitsPerMM) / kUnitsPerMM,
		roundf(height * kUnitsPerMM) / kUnitsPerMM,
		(int)roundf(width * kUnitsPerMM),
		(int)roundf(height * kUnitsPerMM));

	ATPrinterGraphicalOutput::CullInfo cullInfo {};
	const float dotDY = spec.mVerticalDotPitchMM * kUnitsPerMM * (spec.mbBit0Top ? 1.0f : -1.0f);
	const int dotRadius = (int)roundf(spec.mDotRadiusMM * 100);

	uint32 dotMasksUsed[8] {};

	if (output.PreCull(cullInfo, docBounds)) {
		vdfastvector<ATPrinterGraphicalOutput::RenderColumn> cols;
		float rawLineY = 0;
		while (output.ExtractNextLine(cols, rawLineY, cullInfo, docBounds)) {
			uint32 allDotMask = 0;
			for (auto& col : cols)
				allDotMask |= col.mPins;

			const float lineY = (rawLineY - docBounds.top) * kUnitsPerMM;

			for (int i = 0; i < 4; ++i) {
				const int subMaskShift = i * 8;
				if (!((allDotMask >> subMaskShift) & 0xFF))
					continue;

				fprintf(f, "<g transform=\"translate(0,%d)\">\n",
					(int)roundf(lineY + dotDY * 8 * i));

				for (auto& col : cols) {
					const int dotX = (int)roundf((col.mX - docBounds.left) * kUnitsPerMM);
					const uint8 subMask = (uint8)(col.mPins >> subMaskShift);
					if (subMask) {
						dotMasksUsed[subMask >> 5] |= UINT32_C(1) << (subMask & 31);
						fprintf(f, "<use x=\"%d\" l:href=\"#m%02X\"/>\n", dotX, subMask);
					}
				}

				fprintf(f, "</g>\n");
			}
		}
	}

	// Vectors — group by color, convert linear RGB to sRGB (matching Windows)
	vdfastvector<ATPrinterGraphicalOutput::RenderVector> rvectors;
	output.ExtractVectors(rvectors, docBounds);

	if (!rvectors.empty()) {
		// Collect unique colors
		// Collect unique colors.  ToBGR8() recovers the correct 0x00RRGGBB
		// value from the native-order linear color (see LinearColorToSRGB8
		// comment for the full explanation).
		struct ColorGroup {
			uint32 linearColor;
			uint32 srgbColor;	// 0x00RRGGBB — correct for CSS #RRGGBB
		};
		std::vector<ColorGroup> colors;
		std::vector<uint32> colorsSeen;

		for (const auto& rv : rvectors) {
			bool found = false;
			for (uint32 c : colorsSeen) {
				if (c == rv.mLinearColor) { found = true; break; }
			}
			if (!found) {
				uint32 srgb = VDColorRGB(vdfloat32x4::unpacku8(rv.mLinearColor) * (1.0f / 64.0f)).LinearToSRGB().ToBGR8();
				colors.push_back({rv.mLinearColor, srgb});
				colorsSeen.push_back(rv.mLinearColor);
			}
		}

		// Sort by decreasing luminance (matching Windows)
		std::sort(colors.begin(), colors.end(),
			[](const ColorGroup& a, const ColorGroup& b) {
				uint32 la = ((a.srgbColor >> 16) & 0xFF) + ((a.srgbColor >> 8) & 0xFF) + (a.srgbColor & 0xFF);
				uint32 lb = ((b.srgbColor >> 16) & 0xFF) + ((b.srgbColor >> 8) & 0xFF) + (b.srgbColor & 0xFF);
				return la > lb;
			});

		for (const auto& cg : colors) {
			fprintf(f, "<g style=\"stroke:#%06X; stroke-width:%d; stroke-linecap:round; fill:none\">\n",
				cg.srgbColor, dotRadius * 2);

			for (const auto& rv : rvectors) {
				if (rv.mLinearColor != cg.linearColor)
					continue;

				fprintf(f, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\"/>\n",
					(int)roundf((rv.mX1 - docBounds.left) * kUnitsPerMM),
					(int)roundf((rv.mY1 - docBounds.top) * kUnitsPerMM),
					(int)roundf((rv.mX2 - docBounds.left) * kUnitsPerMM),
					(int)roundf((rv.mY2 - docBounds.top) * kUnitsPerMM));
			}

			fprintf(f, "</g>\n");
		}
	}

	// Defs for dot patterns
	if (std::any_of(std::begin(dotMasksUsed), std::end(dotMasksUsed),
			[](uint32 v) { return v != 0; })) {
		fprintf(f, "<defs>\n");
		for (int i = 1; i < 256; ++i) {
			if (dotMasksUsed[i >> 5] & (UINT32_C(1) << (i & 31))) {
				fprintf(f, "<path id=\"m%02X\" fill=\"black\" stroke=\"none\" d=\"", i);
				uint32 mask = (uint32)i;
				bool first = true;
				while (mask) {
					int dotIndex = 0;
					uint32 tmp = mask;
					while (!(tmp & 1)) { dotIndex++; tmp >>= 1; }
					mask &= mask - 1;

					if (!first)
						fprintf(f, " ");
					first = false;
					fprintf(f, "M0,%d a%d,%d 0 0 0 0,%d a%d,%d 0 0 0 0,%d",
						(int)roundf(dotDY * (float)dotIndex),
						dotRadius, dotRadius, 2 * dotRadius,
						dotRadius, dotRadius, -2 * dotRadius);
				}
				fprintf(f, "\"/>\n");
			}
		}
		fprintf(f, "</defs>\n");
	}

	fprintf(f, "</svg>\n");
	fclose(f);
	return true;
}

// File dialog callback for async save dialogs
