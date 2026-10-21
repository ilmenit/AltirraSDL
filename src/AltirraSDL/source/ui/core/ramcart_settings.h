// RAM-Cart per-profile physical-switch defaults.
#pragma once
void ATUIRamCartSettingsInit();
void ATUIRamCartSettingsShutdown();

struct ATRamCartSwitchItem {
	const char *label;
	unsigned mask;
};
// Physical switches of the newly supported hardware. Shared by both menus.
inline constexpr ATRamCartSwitchItem kATRamCartSwitchItems[] = {
	{"Read/write", 0x1000},
	{"256KB mode (P1)", 0x2000},
	{"Swap 128KB modules (P2)", 0x4000},
	{"Software-selectable ABC jumpers", 0x8000},
	{"Module A", 0x4},
	{"Module B", 0x40},
	{"Module C", 0x80},
	{"Upper 1MB module", 0x100},
	{"Upper 2MB module", 0x200},
	{"Decode only $D500", 0x10000},
	{"Control register readback", 0x20000},
};
