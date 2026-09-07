#include <stdafx.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <vd2/Kasumi/pixmaputils.h>
#include <at/atcore/propertyset.h>
#include "printerfx80.h"
#include "printeroutput.h"
#include "encode_png.h"
#include "printerexport.h"

static void Check(bool ok, const char *message) {
	if (!ok) {
		fprintf(stderr, "%s\n", message);
		std::exit(1);
	}
}

struct Services final : IATDeviceManager {
	ATPrinterOutputManager output;
	void *GetService(uint32 iid) override {
		return iid == IATPrinterOutputManager::kTypeID
			? static_cast<IATPrinterOutputManager *>(&output) : nullptr;
	}
	void NotifyDeviceStatusChanged(IATDevice&) override {}
};

static void Send(ATDevicePrinterFX80& printer, std::initializer_list<uint8> bytes) {
	printer.WriteRaw(bytes.begin(), bytes.size());
}

// Compare selectable ROM glyphs against the same ROM copied into user RAM,
// which deliberately follows the independent raw-dot rendering path.
static std::vector<ATPrinterGraphicalOutput::RenderDot> Print(
	uint8 style, bool user, uint8 ch, bool italic = false, int repeat = 1,
	uint8 international = 0, bool underline = false, int script = -1)
{
	Services services;
	ATDevicePrinterFX80 printer;
	printer.SetManager(&services);
	printer.Init();
	printer.ColdReset();
	if (user) {
		Send(printer, {27, ':', 0, 0, 0});
		Send(printer, {27, '%', 1, 0});
	}
	Send(printer, {27, '!', style});
	if (style & 2)
		Send(printer, {27, 'p', 1});
	if (italic)
		Send(printer, {27, '4'});
	Send(printer, {27, 'R', international});
	if (underline)
		Send(printer, {27, '-', 1});
	if (script >= 0)
		Send(printer, {27, 'S', (uint8)script});
	for (int i = 0; i < repeat; ++i)
		Send(printer, {ch});
	Send(printer, {13});
	auto& output = services.output.GetGraphicalOutput(0);
	if (style == 0 && ch == 'A' && !italic && repeat == 1
		&& !international && !underline && script < 0)
		ATPrinterExportAsPDF(user ? L"raw.pdf" : L"tracked.pdf", output, 215.9f, 279.4f);
	ATPrinterGraphicalOutput::CullInfo cull;
	const vdrect32f rect(-100, -100, 300, 300);
	vdfastvector<ATPrinterGraphicalOutput::RenderColumn> columns;
	float lineY;
	if (output.PreCull(cull, rect))
		output.ExtractNextLineAsDotsOrChars(columns, lineY, cull, rect);
	const bool selectable = !user && !(style & 16) && script < 0;
	for (const auto& column : columns)
		Check(bool(column.mPins & column.kCharBit) == selectable,
			"Glyph comparison did not exercise independent tracked/raw paths");
	if (selectable) {
		Check(columns.size() == (size_t)repeat, "Missing selectable glyphs");
		if (repeat == 2)
			Check(std::abs(output.GetCharAdvance(columns[0].mPins
				& ~columns[0].kCharBit) - (columns[1].mX - columns[0].mX)) < 0.001f,
				"Selectable glyph advance disagrees with printer spacing");
	}
	vdfastvector<ATPrinterGraphicalOutput::RenderDot> dots;
	std::vector<ATPrinterGraphicalOutput::RenderDot> result;
	// Despite its name, this API extracts all pre-culled lines without
	// advancing cull. That includes double-strike and underline passes.
	if (output.PreCull(cull, rect))
		output.ExtractNextLineDots(dots, cull, rect);
	result.assign(dots.begin(), dots.end());
	printer.Shutdown();
	return result;
}

int main() {
	// Empty containers/ranges legitimately have null iterators. These
	// operations must not call the C memory functions with null arguments.
	vdfastvector<int> empty;
	vdfastvector<int> emptyCopy(empty);
	empty.assign(nullptr, nullptr);
	empty.insert(empty.end(), (const int *)nullptr, (const int *)nullptr);
	empty.insert(empty.end(), 0, 42);
	empty.erase(empty.begin(), empty.end());
	empty.reserve(8);
	empty.push_back(42);
	Check(empty.size() == 1 && empty[0] == 42 && emptyCopy.empty(),
		"Empty-vector operations changed contents");
	Print(0, false, 'A');
	Print(0, true, 'A');
	for (uint8 style : {0, 1, 4, 5, 8, 2, 32, 33, 36, 37, 40, 34}) {
		for (int variant = 0; variant < 190; ++variant) {
			const uint8 ch = 32 + variant % 95;
			const bool italic = variant >= 95;
			auto tracked = Print(style, false, ch, italic, 2);
			auto raw = Print(style, true, ch, italic, 2);
			if (tracked.size() != raw.size())
				fprintf(stderr, "style=%u ch=%u italic=%d tracked=%zu raw=%zu\n",
					style, ch, italic, tracked.size(), raw.size());
			Check(tracked.size() == raw.size(), "FX-80 glyph dot count differs from ROM raw dots");
			for (size_t i = 0; i < raw.size(); ++i)
				Check(std::abs(tracked[i].mX - raw[i].mX) < 0.001f
					&& std::abs(tracked[i].mY - raw[i].mY) < 0.001f,
					"FX-80 glyph position differs from ROM raw dots");
		}
	}
	// International substitutions and all multi-pass styles. Unlike the
	// single-line glyph check, Print() collects every pass (including underline).
	for (uint8 international = 0; international < 9; ++international) {
		for (uint8 ch : {'#', '$', '@', '[', '\\', ']', '^', '`', '{', '|', '}', '~'}) {
			for (int mode = 0; mode < 5; ++mode) {
				const uint8 style = mode == 1 ? 16 : 0;
				const bool underline = mode == 2;
				const int script = mode >= 3 ? mode - 3 : -1;
				auto rom = Print(style, false, ch, false, 2, international, underline, script);
				auto ram = Print(style, true, ch, false, 2, international, underline, script);
				Check(!rom.empty() && rom.size() == ram.size(), "International/multi-pass dot count mismatch");
				for (size_t i = 0; i < rom.size(); ++i)
					Check(std::abs(rom[i].mX - ram[i].mX) < 0.001f
						&& std::abs(rom[i].mY - ram[i].mY) < 0.001f,
						"International/multi-pass dot position mismatch");
			}
		}
	}
	Services services;
	ATDevicePrinterFX80 printer;
	printer.SetManager(&services);
	printer.Init();
	printer.ColdReset();
	Send(printer, {'A', 14, 'B', 20, 'C', 'D', 13});
	auto& output = services.output.GetGraphicalOutput(0);
	ATPrinterGraphicalOutput::CullInfo cull;
	const vdrect32f rect(-100, -100, 300, 300);
	vdfastvector<ATPrinterGraphicalOutput::RenderColumn> cols;
	float y;
	Check(output.PreCull(cull, rect), "Missing FX-80 output");
	output.ExtractNextLineAsDotsOrChars(cols, y, cull, rect);
	Check(cols.size() == 4, "Expected four tracked characters");
	Check(std::abs(cols[1].mX - cols[0].mX - 2.54f) < 0.001f,
		"SO changed preceding character width");
	Check(std::abs(cols[2].mX - cols[1].mX - 5.08f) < 0.001f,
		"SO did not expand the buffered character");
	Check(std::abs(cols[3].mX - cols[2].mX - 2.54f) < 0.001f,
		"DC4 did not restore normal character width");
	const double before = output.GetVerticalPos();
	Send(printer, {27, 'J', 27});
	Check(std::abs(output.GetVerticalPos() - before - 25.4 / 8) < 0.001,
		"ESC J failed to consume its feed argument");
	printer.ColdReset();
	output.Clear();
	// Master select explicitly cancels proportional mode (see its command
	// contract); check the spacing, not only single-character dot geometry.
	Send(printer, {27, 'p', 1, 27, '!', 0, 'i', 'i', 13});
	Check(output.PreCull(cull, rect), "Missing master-select output");
	output.ExtractNextLineAsDotsOrChars(cols, y, cull, rect);
	Check(cols.size() == 2, "Expected two master-select characters");
	Check(std::abs(cols[1].mX - cols[0].mX - 2.54f) < 0.001f,
		"ESC ! failed to cancel proportional mode");
	// Semantic Unicode mapping as well as graphical parity: German '}' is ü.
	output.Clear();
	Send(printer, {27, 'R', 2, '}', 13});
	Check(output.PreCull(cull, rect), "Missing international character");
	output.ExtractNextLineAsDotsOrChars(cols, y, cull, rect);
	Check(cols.size() == 1 && output.GetCharUnicodeChar(cols[0].mPins
		& ~cols[0].kCharBit) == 0x00FC, "German character Unicode mapping mismatch");
	printer.Shutdown();

	const auto normal = Print(0, false, 'A');
	Check(Print(16, false, 'A').size() == normal.size() * 2,
		"Double-strike did not emit both passes");
	Check(Print(0, false, 'A', false, 1, 0, true).size() > normal.size(),
		"Underline did not add dots");
	auto height = [](const auto& dots) {
		float lo = dots.front().mY, hi = lo;
		for (const auto& dot : dots) {
			lo = std::min(lo, dot.mY);
			hi = std::max(hi, dot.mY);
		}
		return hi - lo;
	};
	for (int script : {0, 1}) {
		const auto dots = Print(0, false, 'A', false, 1, 0, false, script);
		Check(!dots.empty() && height(dots) < height(normal) * 0.75f,
			"Super/subscript did not compress character height");
	}

	ATPropertySet properties;
	for (int i = 0; i < 1000; ++i) {
		properties.SetString("value", L"string property");
		properties.Unset("value");
	}

	VDPixmapBuffer pixels(17, 9, nsVDPixmap::kPixFormat_RGB888);
	for (int y = 0; y < pixels.h; ++y)
		for (int x = 0; x < pixels.w * 3; ++x)
			((uint8 *)pixels.data)[y * pixels.pitch + x] = (x * 7 + y * 13) & 255;
	vdautoptr<IVDImageEncoderPNG> encoder(VDCreateImageEncoderPNG());
	for (bool quick : {false, true}) {
		const void *data;
		uint32 size;
		encoder->Encode(pixels, data, size, quick);
		FILE *f = fopen(quick ? "quick.png" : "filtered.png", "wb");
		Check(f != nullptr, "Cannot create test PNG");
		Check(fwrite(data, 1, size, f) == size, "Cannot write test PNG");
		fclose(f);
	}
	puts("FX-80 glyph/command and property tests passed; PNG fixtures generated.");
}
