// AltirraBridge - Phase 3 state-write commands (impl)
//
// Memory writes use ATSimulator::DebugGlobalWriteByte (the debug-safe
// path that goes through the underlying-RAM latch without invoking
// I/O register write handlers — same semantics as the Windows
// debugger memory pane edit feature).
//
// Joystick injection uses ATPIAEmulator::AllocInput +
// ATPIAEmulator::SetInputBits for direction lines and
// ATGTIAEmulator::SetControllerTrigger for the fire button. We
// allocate one PIA input slot at Init and reuse it for all four
// joystick ports — the SWCHA/SWCHB encoding packs ports 0-3 into a
// single 16-bit input value at four bits per port.
//
// Console switches use ATGTIAEmulator::SetForcedConsoleSwitches.
//
// Keyboard injection uses ATPokeyEmulator::PushKey with the same
// raw KBCODE values as src/AltirraSDL/source/input/input_sdl3.cpp.
//
// BOOT, STATE_SAVE, STATE_LOAD use ATUIPushDeferred(...) which is
// processed once per frame by ATUIPollDeferredActions() in the SDL3
// main loop, going through the same code path as the menu File >
// Open / File > Save State commands.

#include <stdafx.h>

#include "bridge_commands_write.h"
#include "bridge_bare_stub.h"    // EnsureBareStubXexPath
#include "bridge_main_glue.h"   // ATBridgeDispatch* — provided per-target
#include "bridge_protocol.h"

#include "simulator.h"
#include "cpu.h"              // ATCPUEmulator::GetInsnPC (used by BOOT_BARE settle)
#include "cpumemory.h"        // ATCPUEmulatorMemory::WriteByte (hardware-register path)
#include "antic.h"
#include "gtia.h"
#include <at/ataudio/pokey.h>
#include "pia.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace ATBridge {

namespace {

// ---------------------------------------------------------------------------
// Module state.
// ---------------------------------------------------------------------------

struct WriteState {
	int     piaInputSlot = -1;     // ATPIAEmulator::AllocInput() handle
	uint8_t consol       = 0x07;   // active-low: bit 0=START, 1=SELECT, 2=OPTION, 1=released
	bool    fireDown[4]  = {};
	uint8_t dirNibble[4] = {};     // 4-bit per port: bit0=up, 1=down, 2=left, 3=right; 1=pressed
};

WriteState g_write;

// ---------------------------------------------------------------------------
// Memory address parsing helpers.
// ---------------------------------------------------------------------------

bool ParseAddr16(const std::string& tok, uint16_t& addr) {
	uint32_t v = 0;
	if (!ParseUint(tok, v)) return false;
	if (v > 0xFFFF) return false;
	addr = (uint16_t)v;
	return true;
}

// Hex8/Hex16 helpers — duplicated from bridge_commands_state.cpp on
// purpose so each file is self-contained. The total LOC is tiny.
std::string Hex8(uint32_t v)  { char b[8];  std::snprintf(b, sizeof b, "\"$%02x\"",  v & 0xff);   return b; }
std::string Hex16(uint32_t v) { char b[12]; std::snprintf(b, sizeof b, "\"$%04x\"",  v & 0xffff); return b; }

void AddField(std::string& out, const char* key, const std::string& valueLiteral) {
	out += '"';
	out += key;
	out += "\":";
	out += valueLiteral;
	out += ',';
}
void AddU32(std::string& out, const char* key, uint32_t value) {
	out += '"';
	out += key;
	out += "\":";
	out += std::to_string(value);
	out += ',';
}
// String field: surrounds with quotes and JSON-escapes the value.
// Use this for any user-provided string (paths, names) — never
// interpolate user data into JSON without going through JsonEscape,
// or a path containing `"` or `\` will produce malformed JSON.
void AddString(std::string& out, const char* key, const std::string& s) {
	out += '"';
	out += key;
	out += "\":\"";
	out += JsonEscape(s);
	out += "\",";
}
void StripTrailingComma(std::string& s) {
	if (!s.empty() && s.back() == ',') s.pop_back();
}

// ---------------------------------------------------------------------------
// PIA joystick encoding helpers.
// ---------------------------------------------------------------------------

// Apply g_write.dirNibble[] to the PIA input slot. Bit encoding (per
// the formula in src/Altirra/source/portmanager.cpp:269):
//   - Each joystick port owns 4 bits in the 16-bit input value.
//   - Port N occupies bits (N*4)..(N*4+3).
//   - Bits 0-7  → SWCHA (joystick ports 1, 2)
//   - Bits 8-15 → SWCHB (joystick ports 3, 4)
//   - Active-low: 0 = pressed, 1 = released.
void RefreshJoystickInput(ATSimulator& sim) {
	if (g_write.piaInputSlot < 0) return;
	uint32_t pressedMask = 0;
	for (int port = 0; port < 4; ++port) {
		pressedMask |= ((uint32_t)g_write.dirNibble[port] & 0x0F) << (port * 4);
	}
	// Active-low: turn pressed-bits-set into the rval that has those
	// bits CLEARED (and all other bits set so we don't fight other
	// input sources at idle).
	const uint32_t rval = ~pressedMask;
	sim.GetPIA().SetInputBits(g_write.piaInputSlot, rval, 0xFFFF);
}

// Map a direction string to the 4-bit nibble encoding. Accepts the
// 9 cardinal/diagonal/centre forms used by pyA8 and most arcade
// scripts: "centre"/"center"/"none", "n"/"up", "ne"/"upright",
// "e"/"right", "se"/"downright", "s"/"down", "sw"/"downleft",
// "w"/"left", "nw"/"upleft". Case-insensitive. Returns true on
// recognised input; out is set to the bit pattern.
bool ParseJoyDir(const std::string& s, uint8_t& out) {
	std::string lower;
	lower.reserve(s.size());
	for (char c : s) lower += (char)((c >= 'A' && c <= 'Z') ? (c + 32) : c);

	if (lower == "centre" || lower == "center" || lower == "none" || lower == "c")
		{ out = 0x00; return true; }
	if (lower == "up"        || lower == "n"  || lower == "north")     { out = 0x01; return true; }
	if (lower == "down"      || lower == "s"  || lower == "south")     { out = 0x02; return true; }
	if (lower == "left"      || lower == "w"  || lower == "west")      { out = 0x04; return true; }
	if (lower == "right"     || lower == "e"  || lower == "east")      { out = 0x08; return true; }
	if (lower == "upleft"    || lower == "nw" || lower == "northwest") { out = 0x05; return true; }
	if (lower == "upright"   || lower == "ne" || lower == "northeast") { out = 0x09; return true; }
	if (lower == "downleft"  || lower == "sw" || lower == "southwest") { out = 0x06; return true; }
	if (lower == "downright" || lower == "se" || lower == "southeast") { out = 0x0A; return true; }
	return false;
}

// ---------------------------------------------------------------------------
// Keyboard scancode table.
//
// Maps client-facing key names ("RETURN", "SPACE", "A", "1", ...) to
// the raw POKEY KBCODE values used by ATPokeyEmulator::PushKey. The
// numeric values come straight from
// src/AltirraSDL/source/input/input_sdl3.cpp's
// SDLScancodeToAtari() — the canonical SDL3 keyboard mapping that
// already ships with AltirraSDL. Per CLAUDE.md, hardware scan codes
// are never guessed: every entry here corresponds 1:1 to a row in
// that table.
// ---------------------------------------------------------------------------

struct KeyMapEntry { const char* name; uint8_t kbcode; };
const KeyMapEntry kKeyMap[] = {
	// Letters (lowercase letter == key on Atari kb; shift handled
	// separately).
	{ "A", 0x3F }, { "B", 0x15 }, { "C", 0x12 }, { "D", 0x3A },
	{ "E", 0x2A }, { "F", 0x38 }, { "G", 0x3D }, { "H", 0x39 },
	{ "I", 0x0D }, { "J", 0x01 }, { "K", 0x05 }, { "L", 0x00 },
	{ "M", 0x25 }, { "N", 0x23 }, { "O", 0x08 }, { "P", 0x0A },
	{ "Q", 0x2F }, { "R", 0x28 }, { "S", 0x3E }, { "T", 0x2D },
	{ "U", 0x0B }, { "V", 0x10 }, { "W", 0x2E }, { "X", 0x16 },
	{ "Y", 0x2B }, { "Z", 0x17 },
	// Digits
	{ "0", 0x32 }, { "1", 0x1F }, { "2", 0x1E }, { "3", 0x1A },
	{ "4", 0x18 }, { "5", 0x1D }, { "6", 0x1B }, { "7", 0x33 },
	{ "8", 0x35 }, { "9", 0x30 },
	// Special keys (names match the ones a script writer would
	// expect).
	{ "RETURN", 0x0C }, { "ENTER", 0x0C },
	{ "SPACE",  0x21 },
	{ "ESC",    0x1C }, { "ESCAPE", 0x1C },
	{ "TAB",    0x2C },
	{ "BACKSPACE", 0x34 }, { "DELETE", 0x34 }, { "DEL", 0x34 },
	{ "MINUS",  0x0E }, { "EQUALS", 0x0F },
	{ "COMMA",  0x20 }, { "PERIOD", 0x22 }, { "SLASH",  0x26 },
	{ "SEMICOLON", 0x02 },
	{ "CAPS",   0x3C }, { "CAPSLOCK", 0x3C },
	{ "HELP",   0x11 },
};

bool LookupKey(const std::string& name, uint8_t& kbcode) {
	std::string upper;
	upper.reserve(name.size());
	for (char c : name) upper += (char)((c >= 'a' && c <= 'z') ? (c - 32) : c);
	for (const auto& e : kKeyMap) {
		if (upper == e.name) { kbcode = e.kbcode; return true; }
	}
	return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void InitWriteCommands(ATSimulator& sim) {
	if (g_write.piaInputSlot >= 0) return;
	g_write.piaInputSlot = sim.GetPIA().AllocInput();
	g_write.consol = 0x07;
	for (int i = 0; i < 4; ++i) {
		g_write.dirNibble[i] = 0;
		g_write.fireDown[i]  = false;
	}
}

void ShutdownWriteCommands(ATSimulator& sim) {
	CleanupInjectedInput(sim);
	if (g_write.piaInputSlot >= 0) {
		sim.GetPIA().FreeInput(g_write.piaInputSlot);
		g_write.piaInputSlot = -1;
	}
}

void CleanupInjectedInput(ATSimulator& sim) {
	for (int i = 0; i < 4; ++i) {
		g_write.dirNibble[i] = 0;
		g_write.fireDown[i]  = false;
		sim.GetGTIA().SetControllerTrigger(i, false);
	}
	g_write.consol = 0x07;
	sim.GetGTIA().SetForcedConsoleSwitches(0x07);
	RefreshJoystickInput(sim);
	sim.GetPokey().ReleaseAllRawKeys(true);
}

// ---------------------------------------------------------------------------
// POKE addr value
// ---------------------------------------------------------------------------

std::string CmdPoke(ATSimulator& sim, const std::vector<std::string>& tokens) {
	if (tokens.size() < 3)
		return JsonError("POKE: usage: POKE addr value");
	uint16_t addr = 0;
	if (!ParseAddr16(tokens[1], addr))
		return JsonError("POKE: bad address (must be 0..$FFFF)");
	uint32_t value = 0;
	if (!ParseUint(tokens[2], value))
		return JsonError("POKE: bad value");
	if (value > 0xFF)
		return JsonError("POKE: value > $FF");

	// DebugGlobalWriteByte writes the underlying RAM latch, bypassing
	// I/O register write handlers (no ANTIC/GTIA/POKEY side effects).
	// Address space 0 (kATAddressSpace_CPU) is the default.
	sim.DebugGlobalWriteByte((uint32_t)addr, (uint8_t)value);

	std::string payload;
	AddField(payload, "addr",  Hex16(addr));
	AddField(payload, "value", Hex8(value));
	StripTrailingComma(payload);
	return JsonOk(payload);
}

// ---------------------------------------------------------------------------
// HWPOKE addr value
//
// Same parameters as POKE, but routes the write through the real
// CPU bus (ATCPUEmulatorMemory::WriteByte) instead of the debug
// RAM latch. For addresses in the $D000-$D7FF I/O range this
// actually hits the ANTIC / GTIA / POKEY / PIA write handlers,
// with the same cycle-accurate effect a running 6502 `STA` would
// have.
//
// Use POKE for RAM writes that must be debug-safe (no side
// effects). Use HWPOKE for hardware register writes from a
// bare-metal client that has parked the CPU and wants to drive
// ANTIC/GTIA directly — the normal case is 04_paint's
// setup_machine writing $D400/$D402/$D403 and $D016..$D01A.
// ---------------------------------------------------------------------------

std::string CmdHwPoke(ATSimulator& sim, const std::vector<std::string>& tokens) {
	if (tokens.size() < 3)
		return JsonError("HWPOKE: usage: HWPOKE addr value");
	uint16_t addr = 0;
	if (!ParseAddr16(tokens[1], addr))
		return JsonError("HWPOKE: bad address (must be 0..$FFFF)");
	uint32_t value = 0;
	if (!ParseUint(tokens[2], value))
		return JsonError("HWPOKE: bad value");
	if (value > 0xFF)
		return JsonError("HWPOKE: value > $FF");

	// Real CPU bus write — dispatches to CPUWriteByte() for I/O
	// pages, which invokes the chip's write handler.
	sim.GetCPUMemory().WriteByte((uint16)addr, (uint8)value);

	std::string payload;
	AddField(payload, "addr",  Hex16(addr));
	AddField(payload, "value", Hex8(value));
	StripTrailingComma(payload);
	return JsonOk(payload);
}

// POKE16 addr value — convenience for writing a little-endian word.
std::string CmdPoke16(ATSimulator& sim, const std::vector<std::string>& tokens) {
	if (tokens.size() < 3)
		return JsonError("POKE16: usage: POKE16 addr value");
	uint16_t addr = 0;
	if (!ParseAddr16(tokens[1], addr))
		return JsonError("POKE16: bad address");
	if (addr >= 0xFFFF)
		return JsonError("POKE16: address > $FFFE");
	uint32_t value = 0;
	if (!ParseUint(tokens[2], value))
		return JsonError("POKE16: bad value");
	if (value > 0xFFFF)
		return JsonError("POKE16: value > $FFFF");

	sim.DebugGlobalWriteByte((uint32_t)addr,         (uint8_t)(value & 0xFF));
	sim.DebugGlobalWriteByte((uint32_t)(addr + 1),   (uint8_t)((value >> 8) & 0xFF));

	std::string payload;
	AddField(payload, "addr",  Hex16(addr));
	AddField(payload, "value", Hex16(value));
	StripTrailingComma(payload);
	return JsonOk(payload);
}

// ---------------------------------------------------------------------------
// MEMDUMP addr length
//
// Returns the bytes inline as base64. The pyA8 reference uses
// per-server filesystem paths; we always use inline so the same
// command works over `adb forward` on Android. Cap matches PEEK
// (16384 bytes) by default; can be raised if a use case appears.
// ---------------------------------------------------------------------------

std::string CmdMemDump(ATSimulator& sim, const std::vector<std::string>& tokens) {
	if (tokens.size() < 3)
		return JsonError("MEMDUMP: usage: MEMDUMP addr length");
	uint16_t addr = 0;
	if (!ParseAddr16(tokens[1], addr))
		return JsonError("MEMDUMP: bad address");
	uint32_t length = 0;
	if (!ParseUint(tokens[2], length))
		return JsonError("MEMDUMP: bad length");
	if (length == 0)
		return JsonError("MEMDUMP: length must be >= 1");
	if (length > 65536u)
		return JsonError("MEMDUMP: length too large (max 65536)");
	if ((uint32_t)addr + length > 0x10000u)
		return JsonError("MEMDUMP: range crosses end of 64K address space");

	std::vector<uint8_t> buf(length);
	for (uint32_t i = 0; i < length; ++i)
		buf[i] = sim.DebugReadByte((uint16_t)(addr + i));

	std::string payload;
	AddField(payload, "addr",   Hex16(addr));
	AddU32  (payload, "length", length);
	payload += "\"format\":\"base64\",";
	payload += "\"data\":\"";
	payload += Base64Encode(buf.data(), buf.size());
	payload += "\"";
	return JsonOk(payload);
}

// ---------------------------------------------------------------------------
// MEMLOAD addr base64
//
// Inline-only for now (matches MEMDUMP). The base64 data must be the
// 3rd token. The bytes are written via DebugGlobalWriteByte so I/O
// register side effects are NOT triggered.
// ---------------------------------------------------------------------------

std::string CmdMemLoad(ATSimulator& sim, const std::vector<std::string>& tokens) {
	if (tokens.size() < 3)
		return JsonError("MEMLOAD: usage: MEMLOAD addr base64data");
	uint16_t addr = 0;
	if (!ParseAddr16(tokens[1], addr))
		return JsonError("MEMLOAD: bad address");
	std::vector<uint8_t> bytes;
	if (!Base64Decode(tokens[2], bytes))
		return JsonError("MEMLOAD: bad base64 payload");
	if (bytes.empty())
		return JsonError("MEMLOAD: empty payload");
	if ((uint32_t)addr + bytes.size() > 0x10000u)
		return JsonError("MEMLOAD: payload exceeds 64K address space");

	for (size_t i = 0; i < bytes.size(); ++i)
		sim.DebugGlobalWriteByte((uint32_t)(addr + i), bytes[i]);

	std::string payload;
	AddField(payload, "addr",   Hex16(addr));
	AddU32  (payload, "length", (uint32_t)bytes.size());
	StripTrailingComma(payload);
	return JsonOk(payload);
}

// ---------------------------------------------------------------------------
// JOY port direction [fire]
//
//   port:      0..3
//   direction: centre/up/down/left/right/upleft/upright/downleft/downright
//              (also accepts c/n/s/e/w/nw/ne/sw/se and the
//              northcardinal forms — see ParseJoyDir)
//   fire:      optional "fire" / "1" / "true" to press the trigger,
//              omitted or "release"/"0"/"false" to release.
// ---------------------------------------------------------------------------

std::string CmdJoy(ATSimulator& sim, const std::vector<std::string>& tokens) {
	if (tokens.size() < 3)
		return JsonError("JOY: usage: JOY port direction [fire]");
	uint32_t port = 0;
	if (!ParseUint(tokens[1], port) || port > 3)
		return JsonError("JOY: port must be 0..3");
	uint8_t nibble = 0;
	if (!ParseJoyDir(tokens[2], nibble))
		return JsonError("JOY: bad direction");
	bool fire = false;
	if (tokens.size() >= 4) {
		const std::string& f = tokens[3];
		fire = (f == "fire" || f == "1" || f == "true" || f == "down" || f == "press");
	}

	g_write.dirNibble[port] = nibble;
	g_write.fireDown[port]  = fire;
	RefreshJoystickInput(sim);
	sim.GetGTIA().SetControllerTrigger((int)port, fire);

	std::string payload;
	AddU32(payload, "port", port);
	AddString(payload, "dir", tokens[2]);
	payload += "\"fire\":";
	payload += (fire ? "true" : "false");
	return JsonOk(payload);
}

// ---------------------------------------------------------------------------
// KEY name [shift] [ctrl]
//
// Pushes one keystroke into POKEY's queue. The 'shift' and 'ctrl'
// optional words OR the appropriate bits into the KBCODE.
// ---------------------------------------------------------------------------

std::string CmdKey(ATSimulator& sim, const std::vector<std::string>& tokens) {
	if (tokens.size() < 2)
		return JsonError("KEY: usage: KEY name [shift] [ctrl]");
	uint8_t kb = 0;
	if (!LookupKey(tokens[1], kb))
		return JsonError("KEY: unknown key name");

	bool shift = false, ctrl = false;
	for (size_t i = 2; i < tokens.size(); ++i) {
		if (tokens[i] == "shift") shift = true;
		else if (tokens[i] == "ctrl" || tokens[i] == "control") ctrl = true;
		else return JsonError("KEY: unexpected modifier");
	}

	// KBCODE bit 6 = control, bit 7 = shift (Atari keyboard scan
	// code convention, used by POKEY's KBCODE register at $D209).
	uint8_t code = kb;
	if (ctrl)  code |= 0x40;
	if (shift) code |= 0x80;

	// useCooldown=true → respects key repeat timing.
	// flushQueue=false → don't drop pending keystrokes.
	// allowQueue=true  → queue if needed instead of dropping.
	sim.GetPokey().PushKey(code, false, true, false, true);

	std::string payload;
	AddString(payload, "name", tokens[1]);
	AddField(payload, "kbcode", Hex8(code));
	StripTrailingComma(payload);
	return JsonOk(payload);
}

// ---------------------------------------------------------------------------
// CONSOL [start] [select] [option]
//
// Each named token is held down (active-low bit cleared); any
// switches not named are released (bit set). Pass no tokens to
// release all three switches.
// ---------------------------------------------------------------------------

std::string CmdConsol(ATSimulator& sim, const std::vector<std::string>& tokens) {
	uint8_t mask = 0x07;  // all released (bits 0-2 = 1)
	for (size_t i = 1; i < tokens.size(); ++i) {
		if      (tokens[i] == "start")  mask &= ~0x01u;
		else if (tokens[i] == "select") mask &= ~0x02u;
		else if (tokens[i] == "option") mask &= ~0x04u;
		else return JsonError("CONSOL: unknown switch (use: start, select, option)");
	}
	g_write.consol = mask;
	sim.GetGTIA().SetForcedConsoleSwitches(mask);

	std::string payload;
	AddField(payload, "consol", Hex8(mask));
	payload += "\"start\":";  payload += ((mask & 0x01) ? "false," : "true,");
	payload += "\"select\":"; payload += ((mask & 0x02) ? "false," : "true,");
	payload += "\"option\":"; payload += ((mask & 0x04) ? "false"  : "true");
	return JsonOk(payload);
}

// ---------------------------------------------------------------------------
// BOOT path / MOUNT drive path / STATE_SAVE path / STATE_LOAD path
//
// All four use the SDL3 deferred-action queue (ATUIPushDeferred).
// The action is processed by ATUIPollDeferredActions() once per
// main-loop iteration, going through the same code path as the
// menu File > Open / File > Save State commands. This means:
//   1. The bridge command response is sent IMMEDIATELY, before the
//      action has actually run. The action runs after the bridge's
//      Poll() returns, in the same frame.
//   2. To read state that depends on the boot/load completing,
//      issue FRAME 1 (or more) after the BOOT command. The frame
//      gate will block subsequent reads until the simulator has
//      had a chance to process the deferred action and emit a
//      frame.
// ---------------------------------------------------------------------------

// Re-join tokens 2..N with single spaces to support paths containing
// spaces. Phase 1 tokenisation doesn't support quoted strings; the
// cleanest long-term fix is a quoted-string tokeniser added in
// Phase 5. Until then this works for any path with single
// space-separated components.
namespace {
std::string JoinPath(const std::vector<std::string>& tokens, size_t start) {
	if (start >= tokens.size()) return std::string();
	std::string out = tokens[start];
	for (size_t i = start + 1; i < tokens.size(); ++i) {
		out += ' ';
		out += tokens[i];
	}
	return out;
}
}  // namespace

std::string CmdBoot(ATSimulator& sim, const std::vector<std::string>& tokens) {
	if (tokens.size() < 2)
		return JsonError("BOOT: usage: BOOT path");
	std::string path = JoinPath(tokens, 1);
	if (!ATBridgeDispatchBoot(sim, path))
		return JsonError("BOOT: dispatch failed");
	std::string payload;
	AddString(payload, "path", path);
	StripTrailingComma(payload);
	return JsonOk(payload);
}

// ---------------------------------------------------------------------------
// BOOT_BARE  —  boot an embedded tiny stub that parks the CPU
//
// Use case: clients that want to drive the Atari as a raw display
// device (see 04_paint for the canonical example). The stub disables
// IRQs, NMIs (no VBI / DLI), and ANTIC DMA, then enters an infinite
// JMP * loop. After the stub runs the client owns the machine:
//   - direct POKE to ANTIC $D402/$D403 installs a display list
//   - POKE to DMACTL $D400 wakes ANTIC up
//   - MEMLOAD writes pixel data, font data, colour tables
//   - no VBI / DLI / OS code ever modifies these registers
//
// Works identically whether the kernel is the real Atari OS or
// AltirraOS: we're not cooperating with either, we're replacing
// them.
//
// Implementation: the stub bytes are embedded in the server binary
// (see bridge_bare_stub.cpp). We write them to a cross-platform
// per-process temp file on first call, then route through the
// normal ATBridgeDispatchBoot() path. Subsequent calls reuse the
// same file. See bridge_bare_stub.h for the design rationale.
//
// Takes no arguments. Response includes the path used, for
// debugging / tracing only — clients should not depend on it.
// ---------------------------------------------------------------------------

std::string CmdBootBare(ATSimulator& sim, const std::vector<std::string>& /*tokens*/) {
	const std::string path = EnsureBareStubXexPath();
	if (path.empty())
		return JsonError("BOOT_BARE: failed to materialise stub xex");
	if (!ATBridgeDispatchBoot(sim, path))
		return JsonError("BOOT_BARE: dispatch failed");

	// The XEX load is asynchronous from the bridge's point of
	// view: ATBridgeDispatchBoot either queues a deferred boot
	// (SDL3 target) or immediately calls sim.Load + sim.ColdReset
	// (headless target). Either way, the actual "our stub code is
	// now in $0600 and the CPU is parked in its JMP * loop" state
	// only arrives several *hundred* frames later, once the OS
	// cold-boot completes, the OS XEX loader runs, our stub's
	// segment is written to RAM, and RUNAD has fired. Earlier
	// versions of this command returned after a fixed 180-frame
	// settle, which was NOT long enough on NTSC cold boot and
	// caused a race where setup commands issued immediately
	// afterward were clobbered by the still-running OS VBI.
	//
	// Fix: advance the simulator in small chunks and actively poll
	// for the stub's "fully landed" signature — the known 18-byte
	// byte sequence at $0600 and a CPU PC inside the JMP * loop at
	// $060F..$0611. Bail out with success as soon as we see it, or
	// return an error if we never see it within an absolute cap.
	static constexpr uint8 kStubSig[18] = {
		0x78, 0xD8, 0xA9, 0xFF, 0x8D, 0x01, 0xD3, 0xA9,
		0x00, 0x8D, 0x0E, 0xD4, 0x8d, 0x00, 0xD4, 0x4C, 0x0F, 0x06,
	};
	// Normalise case of one hand-typed nibble above.
	uint8 stubSig[18];
	std::memcpy(stubSig, kStubSig, sizeof stubSig);
	stubSig[12] = 0x8D;

	auto stub_ready = [&]() -> bool {
		// Cheap-to-read CPU PC and $0600 bytes. No allocation.
		for (int i = 0; i < 18; ++i) {
			if (sim.DebugGlobalReadByte((uint16)(0x0600 + i)) != stubSig[i])
				return false;
		}
		const uint16 pc = sim.GetCPU().GetInsnPC();
		// The stub parks at JMP * which is three bytes at $060F
		// ($4C $0F $06). A CPU executing that loop will be seen
		// at either the start of the JMP ($060F) or within its
		// three-cycle execution window; in practice the debug
		// snapshot reliably reads PC = $060F or $0612 (next
		// fetch). Accept the whole $060F..$0612 window.
		return pc >= 0x060F && pc <= 0x0612;
	};

	// Advance up to ~10 seconds of wall-clock Atari time (600 frames
	// NTSC). OS cold boot on an 800XL typically finishes in ~300
	// frames; XEX load + RUNAD adds another ~50. 600 gives a wide
	// margin without being unbearable if something is wrong.
	constexpr int kMaxSettleFrames = 600;
	constexpr int kSettleStep      = 20;
	int total = 0;
	while (total < kMaxSettleFrames) {
		// We cannot call atb_frame() from inside a command handler
		// — that's a client-side convenience. Directly poke the
		// sim's frame counter: resume, advance N frames, re-pause.
		const bool wasPaused = sim.IsPaused();
		if (wasPaused) sim.Resume();
		for (int i = 0; i < kSettleStep; ++i)
			sim.Advance(true);
		if (wasPaused) sim.Pause();

		total += kSettleStep;
		if (stub_ready())
			break;
	}

	if (!stub_ready()) {
		return JsonError(
			"BOOT_BARE: stub did not reach its JMP * park loop "
			"within the settle budget — is the kernel missing or "
			"the bootloader broken?");
	}

	std::string payload;
	AddString(payload, "path", path);
	AddU32   (payload, "settle_frames", (uint32_t)total);
	StripTrailingComma(payload);
	return JsonOk(payload);
}

std::string CmdMount(ATSimulator& sim, const std::vector<std::string>& tokens) {
	if (tokens.size() < 3)
		return JsonError("MOUNT: usage: MOUNT drive path");
	uint32_t drive = 0;
	if (!ParseUint(tokens[1], drive) || drive > 14)
		return JsonError("MOUNT: drive must be 0..14");
	std::string path = JoinPath(tokens, 2);
	if (!ATBridgeDispatchMount(sim, (int)drive, path))
		return JsonError("MOUNT: dispatch failed");
	std::string payload;
	AddU32(payload, "drive", drive);
	AddString(payload, "path", path);
	StripTrailingComma(payload);
	return JsonOk(payload);
}

std::string CmdStateSave(ATSimulator& sim, const std::vector<std::string>& tokens) {
	if (tokens.size() < 2)
		return JsonError("STATE_SAVE: usage: STATE_SAVE path");
	std::string path = JoinPath(tokens, 1);
	if (!ATBridgeDispatchStateSave(sim, path))
		return JsonError("STATE_SAVE: dispatch failed");
	std::string payload;
	AddString(payload, "path", path);
	StripTrailingComma(payload);
	return JsonOk(payload);
}

std::string CmdStateLoad(ATSimulator& sim, const std::vector<std::string>& tokens) {
	if (tokens.size() < 2)
		return JsonError("STATE_LOAD: usage: STATE_LOAD path");
	std::string path = JoinPath(tokens, 1);
	if (!ATBridgeDispatchStateLoad(sim, path))
		return JsonError("STATE_LOAD: dispatch failed");
	std::string payload;
	AddString(payload, "path", path);
	StripTrailingComma(payload);
	return JsonOk(payload);
}

// ---------------------------------------------------------------------------
// COLD_RESET / WARM_RESET
//
// Per CLAUDE.md invariant: hardware-state changes must NOT silently
// change running state. We capture the pause flag, perform the
// reset, and explicitly restore the pause flag afterward. The
// frame gate is preserved for the same reason.
// ---------------------------------------------------------------------------

std::string CmdColdReset(ATSimulator& sim, const std::vector<std::string>& /*tokens*/) {
	const bool wasPaused = sim.IsPaused();
	sim.ColdReset();
	if (wasPaused) sim.Pause(); else sim.Resume();
	return JsonOk();
}

std::string CmdWarmReset(ATSimulator& sim, const std::vector<std::string>& /*tokens*/) {
	const bool wasPaused = sim.IsPaused();
	sim.WarmReset();
	if (wasPaused) sim.Pause(); else sim.Resume();
	return JsonOk();
}

}  // namespace ATBridge
