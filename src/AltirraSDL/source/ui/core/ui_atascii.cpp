//	AltirraSDL - Atari character-set rendering helpers

#include <stdafx.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include <imgui.h>
#include <vd2/system/VDString.h>
#include <vd2/system/registry.h>

#include "ui_atascii.h"
#include "oshelper.h"
#include "resource.h"
#include "ui_fonts.h"

namespace {
	uint8 g_charset[1024] = {};
	bool g_charsetInitialized = false;
	int g_textColumns = 38;
	bool g_textColumnsInitialized = false;

	void InitTextColumns() {
		if (g_textColumnsInitialized)
			return;
		g_textColumnsInitialized = true;
		VDRegistryAppKey key("Settings", false);
		g_textColumns = key.getInt("Explorer: Text columns", g_textColumns);
		g_textColumns = std::max(kATUITextColumnsMin,
			std::min(kATUITextColumnsMax, g_textColumns));
	}

	void InitCharset() {
		if (g_charsetInitialized)
			return;

		g_charsetInitialized = true;
		uint8 raw[sizeof g_charset] = {};

		// This is the exact source and transformation used by
		// ATUIEnhancedTextEngine::Init() in the Windows frontend.  The kernel
		// payload is embedded by the SDL resource layer, so this remains
		// portable and does not depend on a generated build artifact.
		if (ATLoadKernelResource(IDR_KERNEL, raw, 0x0800, sizeof raw, true)) {
			memcpy(g_charset, raw, sizeof g_charset);
			std::rotate(g_charset, g_charset + 0x200, g_charset + 0x300);
		}
	}
}

int ATUIGetTextColumns() {
	InitTextColumns();
	return g_textColumns;
}

void ATUISetTextColumns(int columns) {
	InitTextColumns();
	columns = std::max(kATUITextColumnsMin,
		std::min(kATUITextColumnsMax, columns));
	if (columns == g_textColumns)
		return;
	g_textColumns = columns;
	VDRegistryAppKey key("Settings", true);
	key.setInt("Explorer: Text columns", g_textColumns);
}

void ATUIRenderTextColumnControls(const char *id) {
	int columns = ATUIGetTextColumns();
	ImGui::PushID(id ? id : "text-columns");
	// Keep the control row separate from the explanatory text above it.  This
	// is important in the narrow child panes used by the sector/cart/XEX
	// previews, where SameLine() would otherwise make the controls collide
	// with the legend.
	ImGui::NewLine();
	ImGui::Text("Columns");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(120.0f);
	if (ImGui::InputInt("##value", &columns, 1, 8))
		ATUISetTextColumns(columns);
	const int effectiveColumns = ATUIGetTextColumns();
	if (ImGui::IsItemHovered()) {
		ImGui::BeginTooltip();
		ImGui::Text("Text layout width (%d-%d columns)", kATUITextColumnsMin, kATUITextColumnsMax);
		ImGui::EndTooltip();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(96.0f);
	VDStringA preset;
	if (effectiveColumns == 38 || effectiveColumns == 40 || effectiveColumns == 80)
		preset.sprintf("%d columns", effectiveColumns);
	else
		preset = "Custom";
	if (ImGui::BeginCombo("##preset", preset.c_str())) {
		static constexpr int kPresets[] = { 38, 40, 80 };
		for (int presetValue : kPresets) {
			VDStringA label;
			label.sprintf("%d columns", presetValue);
			const bool selected = effectiveColumns == presetValue;
			if (ImGui::Selectable(label.c_str(), selected))
				ATUISetTextColumns(presetValue);
			if (selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	ImGui::PopID();
}

const uint8 *ATUIGetATASCIICharset() {
	InitCharset();
	return g_charset;
}

void ATUIRenderATASCII(const uint8 *data, size_t size, int columns, float scale) {
	if (!data || !size)
		return;

	scale = std::max(scale, 1.0f);
	const float cellW = 8.0f * scale;
	const float cellH = 8.0f * scale;

	// Keep the temporary representation bounded for very large disk/cart
	// images.  The hex view remains available for inspecting every byte.
	const size_t renderSize = std::min<size_t>(size, 64 * 1024);
	struct Row {
		size_t start;
		size_t length;
	};
	std::vector<Row> rows;
	rows.push_back({0, 0});

	for (size_t i = 0; i < renderSize; ++i) {
		const uint8 c = data[i];
		if (c == 0x9B) {
			rows.push_back({i + 1, 0});
			continue;
		}

		if (columns > 0 && (int)rows.back().length >= columns)
			rows.push_back({i, 0});

		++rows.back().length;
	}

	if (rows.empty())
		return;

	size_t maxColumns = 1;
	for (const auto& row : rows)
		maxColumns = std::max(maxColumns, row.length);

	const float totalW = (float)maxColumns * cellW;
	const float totalH = (float)rows.size() * cellH;
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const ImU32 fg = ImGui::GetColorU32(ImGuiCol_Text);
	const ImU32 bg = ImGui::GetColorU32(ImGuiCol_ChildBg);
	// ATASCII inverse video is a literal foreground/background swap.  Keep it
	// theme-aware so normal text remains readable in both light and dark themes,
	// but do not introduce a decorative tint: inverse bytes should look like
	// black-on-white becoming white-on-black (and vice versa).
	const ImU32 inversePaper = fg;
	const ImU32 inverseInk = bg;
	ImDrawList *draw = ImGui::GetWindowDrawList();

	// Reserve the full scrollable canvas, but submit geometry only for visible
	// rows. ImDrawList's clip rectangle clips pixels at render time; it does not
	// avoid the CPU cost or vertex allocation of AddRectFilled().
	ImGui::Dummy(ImVec2(totalW, totalH));
	const ImVec2 clipMin = draw->GetClipRectMin();
	const ImVec2 clipMax = draw->GetClipRectMax();
	const size_t firstRow = (size_t)std::max(0.0f,
		std::floor((clipMin.y - origin.y) / cellH));
	const size_t endRow = std::min(rows.size(), (size_t)std::max(0.0f,
		std::ceil((clipMax.y - origin.y) / cellH)));

	for (size_t y = firstRow; y < endRow; ++y) {
		const auto& row = rows[y];
		for (size_t x = 0; x < row.length; ++x) {
			const uint8 raw = data[row.start + x];
			const bool inverse = (raw & 0x80) != 0;
			const uint8 glyph = raw & 0x7F;
			const ImU32 pixel = inverse ? inverseInk : fg;
			const ImU32 paper = inverse ? inversePaper : bg;
			const float x0 = origin.x + (float)x * cellW;
			const float y0 = origin.y + (float)y * cellH;

			if (inverse)
				draw->AddRectFilled(ImVec2(x0, y0),
					ImVec2(x0 + cellW, y0 + cellH), paper);
			const uint8 *glyphData = &ATUIGetATASCIICharset()[glyph * 8];
			for (int gy = 0; gy < 8; ++gy) {
				const uint8 bits = glyphData[gy];
				for (int gx = 0; gx < 8;) {
					while (gx < 8 && !(bits & (0x80 >> gx)))
						++gx;
					const int runStart = gx;
					while (gx < 8 && (bits & (0x80 >> gx)))
						++gx;
					if (runStart < gx) {
						const float py = y0 + (float)gy * scale;
						draw->AddRectFilled(
							ImVec2(x0 + (float)runStart * scale, py),
							ImVec2(x0 + (float)gx * scale, py + scale), pixel);
					}
				}
			}
		}
	}

	if (renderSize < size) {
		ImGui::TextDisabled("Preview limited to the first 64 KiB; use Hex dump for the complete image.");
	}
}

void ATUIRenderASCII(const uint8 *data, size_t size, int columns) {
	if (!data || !size)
		return;

	VDStringA text;
	const size_t renderSize = std::min<size_t>(size, 64 * 1024);
	text.reserve(renderSize);
	int column = 0;
	for (size_t i = 0; i < renderSize; ++i) {
		const uint8 raw = data[i];
		if (raw == 0x9B || raw == '\n') {
			text += '\n';
			column = 0;
			continue;
		}
		if (raw == '\r')
			continue;
		if (columns > 0 && column >= columns) {
			text += '\n';
			column = 0;
		}
		const uint8 c = raw & 0x7F;
		if (c == '\t') {
			const int spaces = 8 - (column & 7);
			for (int j = 0; j < spaces; ++j) text += ' ';
			column += spaces;
		} else {
			text += (c >= 0x20 && c < 0x7F) ? (char)c : '.';
			++column;
		}
	}

	ImGui::PushFont(ATUIGetFontMono());
	ImGui::TextUnformatted(text.c_str());
	ImGui::PopFont();
	if (renderSize < size)
		ImGui::TextDisabled("Preview limited to the first 64 KiB; use Hex dump for the complete data.");
}

void ATUIRenderHexDump(const uint8 *data, size_t size, uint32 addressBase) {
	if (!data || !size)
		return;

	VDStringA text;
	for (size_t i = 0; i < size; i += 16) {
		text.append_sprintf("%06X: ", (unsigned)(addressBase + (uint32)i));
		for (size_t j = 0; j < 16; ++j) {
			if (i + j < size)
				text.append_sprintf("%02X ", data[i + j]);
			else
				text += "   ";
			if (j == 7)
				text += ' ';
		}
		text += " |";
		for (size_t j = 0; j < 16 && i + j < size; ++j) {
			const uint8 c = data[i + j];
			text += (c >= 0x20 && c < 0x7F) ? (char)c : '.';
		}
		text += "|\n";
	}

	ImGui::PushFont(ATUIGetFontMono());
	ImGui::TextUnformatted(text.c_str());
	ImGui::PopFont();
}
