#include <stdafx.h>

#include "libretro_vkbd.h"
#include "libretro/libretro.h"
#include "inputdefs.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace {

struct VkbdKey {
	const char *label;
	ATLibretroVkbdEvent event;
};

struct VkbdState {
	bool open = false;
	bool shift = false;
	bool ctrl = false;
	uint16_t prevMask = 0;
	unsigned page = 0;
	unsigned row = 0;
	unsigned col = 0;
};

VkbdState g_vkbd;

constexpr uint32_t kBg = 0xD8101010;
constexpr uint32_t kPanel = 0xF0202020;
constexpr uint32_t kCell = 0xFF3A3A3A;
constexpr uint32_t kCellAlt = 0xFF2A4058;
constexpr uint32_t kSelected = 0xFFE0A028;
constexpr uint32_t kText = 0xFFF0F0E8;
constexpr uint32_t kTextDark = 0xFF101010;
constexpr uint32_t kAccent = 0xFF80C8FF;

constexpr ATLibretroVkbdEvent MakeKey(unsigned keycode, uint32_t ch = 0) {
	ATLibretroVkbdEvent e {};
	e.type = ATLibretroVkbdEvent::kKey;
	e.keycode = keycode;
	e.character = ch;
	return e;
}

constexpr ATLibretroVkbdEvent MakeType(ATLibretroVkbdEvent::Type type) {
	ATLibretroVkbdEvent e {};
	e.type = type;
	return e;
}

constexpr ATLibretroVkbdEvent Make5200Key(uint32_t trigger) {
	ATLibretroVkbdEvent e {};
	e.type = ATLibretroVkbdEvent::k5200Key;
	e.trigger = trigger;
	return e;
}

constexpr VkbdKey kAlphaRows[][10] = {
	{
		{ "1", MakeKey(RETROK_1, '1') },
		{ "2", MakeKey(RETROK_2, '2') },
		{ "3", MakeKey(RETROK_3, '3') },
		{ "4", MakeKey(RETROK_4, '4') },
		{ "5", MakeKey(RETROK_5, '5') },
		{ "6", MakeKey(RETROK_6, '6') },
		{ "7", MakeKey(RETROK_7, '7') },
		{ "8", MakeKey(RETROK_8, '8') },
		{ "9", MakeKey(RETROK_9, '9') },
		{ "0", MakeKey(RETROK_0, '0') },
	},
	{
		{ "Q", MakeKey(RETROK_q, 'q') },
		{ "W", MakeKey(RETROK_w, 'w') },
		{ "E", MakeKey(RETROK_e, 'e') },
		{ "R", MakeKey(RETROK_r, 'r') },
		{ "T", MakeKey(RETROK_t, 't') },
		{ "Y", MakeKey(RETROK_y, 'y') },
		{ "U", MakeKey(RETROK_u, 'u') },
		{ "I", MakeKey(RETROK_i, 'i') },
		{ "O", MakeKey(RETROK_o, 'o') },
		{ "P", MakeKey(RETROK_p, 'p') },
	},
	{
		{ "A", MakeKey(RETROK_a, 'a') },
		{ "S", MakeKey(RETROK_s, 's') },
		{ "D", MakeKey(RETROK_d, 'd') },
		{ "F", MakeKey(RETROK_f, 'f') },
		{ "G", MakeKey(RETROK_g, 'g') },
		{ "H", MakeKey(RETROK_h, 'h') },
		{ "J", MakeKey(RETROK_j, 'j') },
		{ "K", MakeKey(RETROK_k, 'k') },
		{ "L", MakeKey(RETROK_l, 'l') },
		{ ";", MakeKey(RETROK_SEMICOLON, ';') },
	},
	{
		{ "SHF", MakeKey(RETROK_LSHIFT) },
		{ "Z", MakeKey(RETROK_z, 'z') },
		{ "X", MakeKey(RETROK_x, 'x') },
		{ "C", MakeKey(RETROK_c, 'c') },
		{ "V", MakeKey(RETROK_v, 'v') },
		{ "B", MakeKey(RETROK_b, 'b') },
		{ "N", MakeKey(RETROK_n, 'n') },
		{ "M", MakeKey(RETROK_m, 'm') },
		{ ",", MakeKey(RETROK_COMMA, ',') },
		{ ".", MakeKey(RETROK_PERIOD, '.') },
	},
	{
		{ "CTL", MakeKey(RETROK_LCTRL) },
		{ "ESC", MakeKey(RETROK_ESCAPE) },
		{ "SPC", MakeKey(RETROK_SPACE, ' ') },
		{ "RET", MakeKey(RETROK_RETURN, '\r') },
		{ "BK", MakeKey(RETROK_BACKSPACE) },
		{ "-", MakeKey(RETROK_MINUS, '-') },
		{ "=", MakeKey(RETROK_EQUALS, '=') },
		{ "/", MakeKey(RETROK_SLASH, '/') },
		{ "CON", {} },
		{ "X", {} },
	},
};

constexpr VkbdKey kConsoleRows[][10] = {
	{
		{ "START", MakeType(ATLibretroVkbdEvent::kConsoleStart) },
		{ "SELECT", MakeType(ATLibretroVkbdEvent::kConsoleSelect) },
		{ "OPTION", MakeType(ATLibretroVkbdEvent::kConsoleOption) },
		{ "WARM", MakeType(ATLibretroVkbdEvent::kWarmReset) },
		{ "COLD", MakeType(ATLibretroVkbdEvent::kColdReset) },
		{ "BREAK", MakeKey(RETROK_BREAK) },
		{ "ALPHA", {} },
		{ "X", {} },
		{ "", {} },
		{ "", {} },
	},
	{
		{ "UP", MakeKey(RETROK_UP) },
		{ "DOWN", MakeKey(RETROK_DOWN) },
		{ "LEFT", MakeKey(RETROK_LEFT) },
		{ "RIGHT", MakeKey(RETROK_RIGHT) },
		{ "INS", MakeKey(RETROK_INSERT) },
		{ "DEL", MakeKey(RETROK_DELETE) },
		{ "HOME", MakeKey(RETROK_HOME) },
		{ "END", MakeKey(RETROK_END) },
		{ "", {} },
		{ "", {} },
	},
	{
		{ "F1", MakeKey(RETROK_F1) },
		{ "F2", MakeKey(RETROK_F2) },
		{ "F3", MakeKey(RETROK_F3) },
		{ "F4", MakeKey(RETROK_F4) },
		{ "F5", MakeKey(RETROK_F5) },
		{ "F6", MakeKey(RETROK_F6) },
		{ "F7", MakeKey(RETROK_F7) },
		{ "F8", MakeKey(RETROK_F8) },
		{ "", {} },
		{ "", {} },
	},
};

constexpr VkbdKey kKeypadRows[][10] = {
	{
		{ "", {} },
		{ "1", Make5200Key(kATInputTrigger_5200_1) },
		{ "2", Make5200Key(kATInputTrigger_5200_2) },
		{ "3", Make5200Key(kATInputTrigger_5200_3) },
		{ "START", Make5200Key(kATInputTrigger_5200_Start) },
		{ "ALPHA", {} },
		{ "X", {} },
		{ "", {} },
		{ "", {} },
		{ "", {} },
	},
	{
		{ "*", Make5200Key(kATInputTrigger_5200_Star) },
		{ "4", Make5200Key(kATInputTrigger_5200_4) },
		{ "5", Make5200Key(kATInputTrigger_5200_5) },
		{ "6", Make5200Key(kATInputTrigger_5200_6) },
		{ "PAUSE", Make5200Key(kATInputTrigger_5200_Pause) },
		{ "", {} },
		{ "", {} },
		{ "", {} },
		{ "", {} },
		{ "", {} },
	},
	{
		{ "#", Make5200Key(kATInputTrigger_5200_Pound) },
		{ "7", Make5200Key(kATInputTrigger_5200_7) },
		{ "8", Make5200Key(kATInputTrigger_5200_8) },
		{ "9", Make5200Key(kATInputTrigger_5200_9) },
		{ "RESET", Make5200Key(kATInputTrigger_5200_Reset) },
		{ "", {} },
		{ "", {} },
		{ "", {} },
		{ "", {} },
		{ "", {} },
	},
	{
		{ "0", Make5200Key(kATInputTrigger_5200_0) },
		{ "CON", {} },
		{ "WARM", MakeType(ATLibretroVkbdEvent::kWarmReset) },
		{ "COLD", MakeType(ATLibretroVkbdEvent::kColdReset) },
		{ "", {} },
		{ "", {} },
		{ "", {} },
		{ "", {} },
		{ "", {} },
		{ "", {} },
	},
};

struct Layout {
	const VkbdKey *keys;
	unsigned rows;
	unsigned cols;
};

Layout GetLayout(bool is5200Mode) {
	if (g_vkbd.page == 0)
		return { &kAlphaRows[0][0], (unsigned)std::size(kAlphaRows), 10 };

	if (is5200Mode && g_vkbd.page == 2)
		return { &kKeypadRows[0][0], (unsigned)std::size(kKeypadRows), 10 };

	return { &kConsoleRows[0][0], (unsigned)std::size(kConsoleRows), 10 };
}

const VkbdKey& GetKey(const Layout& layout, unsigned row, unsigned col) {
	return layout.keys[row * layout.cols + col];
}

bool IsSelectable(const VkbdKey& key) {
	return key.label && key.label[0];
}

void ClampSelection(bool is5200Mode) {
	const Layout layout = GetLayout(is5200Mode);
	g_vkbd.row = std::min(g_vkbd.row, layout.rows - 1);
	g_vkbd.col = std::min(g_vkbd.col, layout.cols - 1);

	if (IsSelectable(GetKey(layout, g_vkbd.row, g_vkbd.col)))
		return;

	for(unsigned r = 0; r < layout.rows; ++r) {
		for(unsigned c = 0; c < layout.cols; ++c) {
			if (IsSelectable(GetKey(layout, r, c))) {
				g_vkbd.row = r;
				g_vkbd.col = c;
				return;
			}
		}
	}
}

bool MoveSelection(int dx, int dy, bool is5200Mode) {
	const Layout layout = GetLayout(is5200Mode);
	const int startRow = (int)g_vkbd.row;
	const int startCol = (int)g_vkbd.col;
	int row = startRow;
	int col = startCol;

	for(unsigned i = 0; i < layout.rows * layout.cols; ++i) {
		row = (row + dy + (int)layout.rows) % (int)layout.rows;
		col = (col + dx + (int)layout.cols) % (int)layout.cols;

		if (IsSelectable(GetKey(layout, (unsigned)row, (unsigned)col))) {
			g_vkbd.row = (unsigned)row;
			g_vkbd.col = (unsigned)col;
			return row != startRow || col != startCol;
		}
	}

	return false;
}

bool Pressed(uint16_t mask, uint16_t prev, unsigned id) {
	const uint16_t bit = (uint16_t)(1U << id);
	return (mask & bit) && !(prev & bit);
}

void FillRect(uint32_t *row0, int pitchPixels, int x, int y, int w, int h,
	int fbw, int fbh, uint32_t color) {
	const int x0 = std::clamp(x, 0, fbw);
	const int y0 = std::clamp(y, 0, fbh);
	const int x1 = std::clamp(x + w, 0, fbw);
	const int y1 = std::clamp(y + h, 0, fbh);

	for(int yy = y0; yy < y1; ++yy) {
		uint32_t *dst = row0 + yy * pitchPixels + x0;
		std::fill(dst, dst + (x1 - x0), color);
	}
}

bool GlyphRows(char ch, std::array<uint8_t, 7>& rows) {
	rows = {};
	if (ch >= 'a' && ch <= 'z')
		ch = (char)(ch - 'a' + 'A');

	switch(ch) {
		case '0': rows = { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E }; return true;
		case '1': rows = { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E }; return true;
		case '2': rows = { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F }; return true;
		case '3': rows = { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E }; return true;
		case '4': rows = { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }; return true;
		case '5': rows = { 0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E }; return true;
		case '6': rows = { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }; return true;
		case '7': rows = { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 }; return true;
		case '8': rows = { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }; return true;
		case '9': rows = { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C }; return true;
		case 'A': rows = { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }; return true;
		case 'B': rows = { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E }; return true;
		case 'C': rows = { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E }; return true;
		case 'D': rows = { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E }; return true;
		case 'E': rows = { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F }; return true;
		case 'F': rows = { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 }; return true;
		case 'G': rows = { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E }; return true;
		case 'H': rows = { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }; return true;
		case 'I': rows = { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E }; return true;
		case 'J': rows = { 0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C }; return true;
		case 'K': rows = { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }; return true;
		case 'L': rows = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F }; return true;
		case 'M': rows = { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 }; return true;
		case 'N': rows = { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 }; return true;
		case 'O': rows = { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }; return true;
		case 'P': rows = { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 }; return true;
		case 'Q': rows = { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D }; return true;
		case 'R': rows = { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 }; return true;
		case 'S': rows = { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E }; return true;
		case 'T': rows = { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 }; return true;
		case 'U': rows = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }; return true;
		case 'V': rows = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 }; return true;
		case 'W': rows = { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A }; return true;
		case 'X': rows = { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 }; return true;
		case 'Y': rows = { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 }; return true;
		case 'Z': rows = { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F }; return true;
		case '*': rows = { 0x00, 0x15, 0x0E, 0x1F, 0x0E, 0x15, 0x00 }; return true;
		case '#': rows = { 0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A }; return true;
		case '-': rows = { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 }; return true;
		case '=': rows = { 0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00 }; return true;
		case '/': rows = { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 }; return true;
		case ';': rows = { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x08 }; return true;
		case ',': rows = { 0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x08 }; return true;
		case '.': rows = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C }; return true;
		case ' ': rows = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; return true;
		default: return false;
	}
}

void DrawGlyph(uint32_t *row0, int pitchPixels, int fbw, int fbh, int x,
	int y, int scale, char ch, uint32_t color) {
	std::array<uint8_t, 7> rows {};
	if (!GlyphRows(ch, rows))
		return;

	for(int gy = 0; gy < 7; ++gy) {
		for(int gx = 0; gx < 5; ++gx) {
			if (rows[(size_t)gy] & (uint8_t)(1U << (4 - gx)))
				FillRect(row0, pitchPixels, x + gx * scale, y + gy * scale,
					scale, scale, fbw, fbh, color);
		}
	}
}

void DrawText(uint32_t *row0, int pitchPixels, int fbw, int fbh, int x,
	int y, int scale, const char *text, uint32_t color) {
	for(const char *s = text; *s; ++s) {
		DrawGlyph(row0, pitchPixels, fbw, fbh, x, y, scale, *s, color);
		x += 6 * scale;
	}
}

} // namespace

void ATLibretroVkbdReset() {
	g_vkbd = VkbdState {};
}

void ATLibretroVkbdSetOpen(bool open) {
	if (g_vkbd.open == open)
		return;

	g_vkbd.open = open;
	g_vkbd.prevMask = 0;
	if (!open) {
		g_vkbd.shift = false;
		g_vkbd.ctrl = false;
		g_vkbd.page = 0;
		g_vkbd.row = 0;
		g_vkbd.col = 0;
	}
	ClampSelection(false);
}

bool ATLibretroVkbdIsOpen() {
	return g_vkbd.open;
}

bool ATLibretroVkbdUpdate(uint16_t joypadState, uint16_t toggleButtonMask,
	bool toggleSelectR2, bool is5200Mode, ATLibretroVkbdEvent& event) {
	event = {};

	const uint16_t prev = g_vkbd.prevMask;
	g_vkbd.prevMask = joypadState;

	bool toggle = false;
	for(unsigned id = 0; id < 16; ++id) {
		if ((toggleButtonMask & (uint16_t)(1U << id))
			&& Pressed(joypadState, prev, id))
		{
			toggle = true;
			break;
		}
	}

	toggle = toggle
		|| (toggleSelectR2
			&& (joypadState & (uint16_t)(1U << RETRO_DEVICE_ID_JOYPAD_SELECT))
			&& Pressed(joypadState, prev, RETRO_DEVICE_ID_JOYPAD_R2));

	if (toggle) {
		ATLibretroVkbdSetOpen(!g_vkbd.open);
		return false;
	}

	if (!g_vkbd.open)
		return false;

	if (Pressed(joypadState, prev, RETRO_DEVICE_ID_JOYPAD_LEFT))
		MoveSelection(-1, 0, is5200Mode);
	if (Pressed(joypadState, prev, RETRO_DEVICE_ID_JOYPAD_RIGHT))
		MoveSelection(1, 0, is5200Mode);
	if (Pressed(joypadState, prev, RETRO_DEVICE_ID_JOYPAD_UP))
		MoveSelection(0, -1, is5200Mode);
	if (Pressed(joypadState, prev, RETRO_DEVICE_ID_JOYPAD_DOWN))
		MoveSelection(0, 1, is5200Mode);

	if (Pressed(joypadState, prev, RETRO_DEVICE_ID_JOYPAD_X)) {
		g_vkbd.page = (g_vkbd.page == 0) ? 1 : 0;
		ClampSelection(is5200Mode);
		return false;
	}

	if (is5200Mode && Pressed(joypadState, prev, RETRO_DEVICE_ID_JOYPAD_Y)) {
		g_vkbd.page = (g_vkbd.page == 2) ? 0 : 2;
		ClampSelection(is5200Mode);
		return false;
	}

	if (Pressed(joypadState, prev, RETRO_DEVICE_ID_JOYPAD_A)) {
		ATLibretroVkbdSetOpen(false);
		return false;
	}

	if (!Pressed(joypadState, prev, RETRO_DEVICE_ID_JOYPAD_B))
		return false;

	ClampSelection(is5200Mode);
	const Layout layout = GetLayout(is5200Mode);
	const VkbdKey& key = GetKey(layout, g_vkbd.row, g_vkbd.col);
	if (!IsSelectable(key))
		return false;

	if (!std::strcmp(key.label, "SHF")) {
		g_vkbd.shift = !g_vkbd.shift;
		return false;
	}

	if (!std::strcmp(key.label, "CTL")) {
		g_vkbd.ctrl = !g_vkbd.ctrl;
		return false;
	}

	if (!std::strcmp(key.label, "CON")) {
		g_vkbd.page = 1;
		ClampSelection(is5200Mode);
		return false;
	}

	if (!std::strcmp(key.label, "ALPHA")) {
		g_vkbd.page = 0;
		ClampSelection(is5200Mode);
		return false;
	}

	if (!std::strcmp(key.label, "X")) {
		ATLibretroVkbdSetOpen(false);
		return false;
	}

	event = key.event;
	event.shift = g_vkbd.shift;
	event.ctrl = g_vkbd.ctrl;
	if (event.type == ATLibretroVkbdEvent::kKey
		&& event.keycode != RETROK_LSHIFT
		&& event.keycode != RETROK_RSHIFT
		&& event.keycode != RETROK_LCTRL
		&& event.keycode != RETROK_RCTRL)
	{
		g_vkbd.shift = false;
		g_vkbd.ctrl = false;
	}
	return event.type != ATLibretroVkbdEvent::kNone;
}

void ATLibretroVkbdRenderXrgb8888(void *data, int w, int h, ptrdiff_t pitch,
	bool is5200Mode) {
	if (!g_vkbd.open || !data || w <= 0 || h <= 0)
		return;

	uint32_t *const pixels = (uint32_t *)data;
	const int pitchPixels = (int)(pitch / 4);
	const Layout layout = GetLayout(is5200Mode);
	const int margin = std::max(4, std::min(w, h) / 48);
	const int cellW = std::max(24, (w - margin * 2) / (int)layout.cols);
	const int cellH = std::max(16, h / 12);
	const int panelW = cellW * (int)layout.cols + margin * 2;
	const int panelH = cellH * (int)layout.rows + margin * 3 + 10;
	const int panelX = std::max(0, (w - panelW) / 2);
	const int panelY = std::max(0, h - panelH - margin);
	const int scale = std::max(1, std::min(cellW / 34, cellH / 12));

	FillRect(pixels, pitchPixels, 0, panelY - margin, w,
		panelH + margin * 2, w, h, kBg);
	FillRect(pixels, pitchPixels, panelX, panelY, panelW, panelH,
		w, h, kPanel);

	for(unsigned r = 0; r < layout.rows; ++r) {
		for(unsigned c = 0; c < layout.cols; ++c) {
			const VkbdKey& key = GetKey(layout, r, c);
			if (!IsSelectable(key))
				continue;

			const int x = panelX + margin + (int)c * cellW;
			const int y = panelY + margin + (int)r * cellH;
			const bool selected = r == g_vkbd.row && c == g_vkbd.col;
			const bool activeModifier =
				(!std::strcmp(key.label, "SHF") && g_vkbd.shift)
				|| (!std::strcmp(key.label, "CTL") && g_vkbd.ctrl);
			const uint32_t bg = selected ? kSelected
				: (activeModifier ? kCellAlt : kCell);
			const uint32_t fg = selected ? kTextDark
				: (activeModifier ? kAccent : kText);
			FillRect(pixels, pitchPixels, x + 1, y + 1, cellW - 2,
				cellH - 2, w, h, bg);

			const int textW = (int)std::strlen(key.label) * 6 * scale;
			const int tx = x + std::max(2, (cellW - textW) / 2);
			const int ty = y + std::max(2, (cellH - 7 * scale) / 2);
			DrawText(pixels, pitchPixels, w, h, tx, ty, scale, key.label, fg);
		}
	}
}
