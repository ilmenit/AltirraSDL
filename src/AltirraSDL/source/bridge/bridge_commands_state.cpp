// AltirraBridge - Phase 2 state-read commands (impl)
//
// Every command here wraps an existing public accessor on
// ATSimulator/ATCPUEmulator/ATAnticEmulator/ATGTIAEmulator/
// ATPokeyEmulator/ATPIAEmulator. We do not reimplement chip semantics
// or memory decoding — that work is single-sourced in the Altirra
// core (and used by the Windows debugger panes).

#include <stdafx.h>

#include "bridge_commands_state.h"
#include "bridge_protocol.h"

#include "simulator.h"
#include "cpu.h"
#include "cpumemory.h"
#include "antic.h"
#include "gtia.h"
#include <at/ataudio/pokey.h>
#include "pia.h"
#include "palettesolver.h"      // ATCreateColorPaletteSolver — shared with Windows UI

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace ATBridge {

namespace {

// ---------------------------------------------------------------------------
// Local formatting helpers (kept tiny on purpose — no JSON library).
// ---------------------------------------------------------------------------

// Format a value as "$XXXX" (uppercase, fixed width). The Atari
// convention is `$` for hex; the Python and C clients accept both
// `$3c` and `0x3c` so the choice is purely cosmetic.
std::string Hex8(uint32_t v)  { char b[8];  std::snprintf(b, sizeof b, "\"$%02x\"",  v & 0xff);   return b; }
std::string Hex16(uint32_t v) { char b[12]; std::snprintf(b, sizeof b, "\"$%04x\"",  v & 0xffff); return b; }
std::string Hex24(uint32_t v) { char b[12]; std::snprintf(b, sizeof b, "\"$%06x\"",  v & 0xffffff); return b; }

// Append "key:value," to out. Caller is responsible for stripping
// the trailing comma before closing the object — we do this in the
// "build a JSON object body" helpers below.
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

// Strip a single trailing comma if present, so the caller can wrap
// in {...}.
void StripTrailingComma(std::string& s) {
	if (!s.empty() && s.back() == ',')
		s.pop_back();
}

// Hex-encode `n` bytes from `bytes` into a 2*n-character lowercase
// string with no separators. Used by PEEK and the GTIA palette dump.
std::string HexBytes(const uint8_t* bytes, size_t n) {
	static const char* hex = "0123456789abcdef";
	std::string out;
	out.reserve(n * 2);
	for (size_t i = 0; i < n; ++i) {
		out += hex[bytes[i] >> 4];
		out += hex[bytes[i] & 0xf];
	}
	return out;
}

// ---------------------------------------------------------------------------
// CPU register payload (used by REGS and HWSTATE)
// ---------------------------------------------------------------------------

std::string BuildCpuPayload(ATSimulator& sim) {
	auto& cpu = sim.GetCPU();
	const uint8_t p = cpu.GetP();

	// Decode the status flags into a human-readable mask. Uppercase
	// letter = set, '-' = clear. Bits 4-5 have different meanings on
	// 65C816 native mode, so make the display mode-aware:
	//
	//   6502 / 65C02 / 65C816 emulation mode: bit 5 = unused
	//                                          bit 4 = B (break)
	//   65C816 native mode:                    bit 5 = M (acc width)
	//                                          bit 4 = X (idx width)
	//
	// On real 6502 hw the unused bit reads as 1; we render it as
	// '-' for consistency with the "set bits are letters, clear are
	// dashes" convention.
	const bool native816 =
		cpu.GetCPUMode() == kATCPUMode_65C816 && !cpu.GetEmulationFlag();

	char flagStr[9];
	flagStr[0] = (p & AT6502::kFlagN) ? 'N' : '-';
	flagStr[1] = (p & AT6502::kFlagV) ? 'V' : '-';
	if (native816) {
		flagStr[2] = (p & AT6502::kFlagM) ? 'M' : '-';
		flagStr[3] = (p & AT6502::kFlagX) ? 'X' : '-';
	} else {
		flagStr[2] = '-';
		flagStr[3] = (p & AT6502::kFlagB) ? 'B' : '-';
	}
	flagStr[4] = (p & AT6502::kFlagD) ? 'D' : '-';
	flagStr[5] = (p & AT6502::kFlagI) ? 'I' : '-';
	flagStr[6] = (p & AT6502::kFlagZ) ? 'Z' : '-';
	flagStr[7] = (p & AT6502::kFlagC) ? 'C' : '-';
	flagStr[8] = '\0';

	std::string body;
	AddField(body, "PC", Hex16(cpu.GetPC()));
	AddField(body, "A",  Hex8(cpu.GetA()));
	AddField(body, "X",  Hex8(cpu.GetX()));
	AddField(body, "Y",  Hex8(cpu.GetY()));
	AddField(body, "S",  Hex8(cpu.GetS()));
	AddField(body, "P",  Hex8(p));
	body += "\"flags\":\"";
	body += flagStr;
	body += "\",";
	AddU32(body, "cycles", sim.GetCpuCycleCounter());

	const ATCPUMode mode = cpu.GetCPUMode();
	const char* modeStr =
		mode == kATCPUMode_6502   ? "6502"   :
		mode == kATCPUMode_65C02  ? "65C02"  :
		mode == kATCPUMode_65C816 ? "65C816" : "unknown";
	body += "\"mode\":\"";
	body += modeStr;
	body += "\",";

	StripTrailingComma(body);
	return body;
}

// ---------------------------------------------------------------------------
// ANTIC payload
// ---------------------------------------------------------------------------

std::string BuildAnticPayload(ATSimulator& sim) {
	auto& antic = sim.GetAntic();
	ATAnticRegisterState rs;
	antic.GetRegisterState(rs);

	std::string body;
	AddField(body, "DMACTL", Hex8(rs.mDMACTL));
	AddField(body, "CHACTL", Hex8(rs.mCHACTL));
	AddField(body, "DLISTL", Hex8(rs.mDLISTL));
	AddField(body, "DLISTH", Hex8(rs.mDLISTH));
	AddField(body, "HSCROL", Hex8(rs.mHSCROL));
	AddField(body, "VSCROL", Hex8(rs.mVSCROL));
	AddField(body, "PMBASE", Hex8(rs.mPMBASE));
	AddField(body, "CHBASE", Hex8(rs.mCHBASE));
	AddField(body, "NMIEN",  Hex8(rs.mNMIEN));
	AddField(body, "DLIST",  Hex16(antic.GetDisplayListPtr()));
	AddU32(body, "beam_x", antic.GetBeamX());
	AddU32(body, "beam_y", antic.GetBeamY());
	StripTrailingComma(body);
	return body;
}

// ---------------------------------------------------------------------------
// GTIA payload
// ---------------------------------------------------------------------------

// Map GTIA register offsets ($D000-$D01F) to JSON field names. The
// register file is 32 bytes; we name only the meaningful entries
// (write-only registers like HITCLR are skipped because the read-back
// value is the last write, which is informative; we expose them as
// raw `regNN` fields too for completeness).
const char* GtiaRegName(int idx) {
	switch (idx) {
	case 0x00: return "HPOSP0"; case 0x01: return "HPOSP1";
	case 0x02: return "HPOSP2"; case 0x03: return "HPOSP3";
	case 0x04: return "HPOSM0"; case 0x05: return "HPOSM1";
	case 0x06: return "HPOSM2"; case 0x07: return "HPOSM3";
	case 0x08: return "SIZEP0"; case 0x09: return "SIZEP1";
	case 0x0A: return "SIZEP2"; case 0x0B: return "SIZEP3";
	case 0x0C: return "SIZEM";
	case 0x0D: return "GRAFP0"; case 0x0E: return "GRAFP1";
	case 0x0F: return "GRAFP2"; case 0x10: return "GRAFP3";
	case 0x11: return "GRAFM";
	case 0x12: return "COLPM0"; case 0x13: return "COLPM1";
	case 0x14: return "COLPM2"; case 0x15: return "COLPM3";
	case 0x16: return "COLPF0"; case 0x17: return "COLPF1";
	case 0x18: return "COLPF2"; case 0x19: return "COLPF3";
	case 0x1A: return "COLBK";
	case 0x1B: return "PRIOR";
	case 0x1C: return "VDELAY";
	case 0x1D: return "GRACTL";
	case 0x1E: return "HITCLR";
	case 0x1F: return "CONSOL";
	}
	return nullptr;
}

std::string BuildGtiaPayload(ATSimulator& sim) {
	auto& gtia = sim.GetGTIA();
	ATGTIARegisterState rs;
	gtia.GetRegisterState(rs);

	std::string body;
	for (int i = 0; i < 0x20; ++i) {
		const char* name = GtiaRegName(i);
		if (name) AddField(body, name, Hex8(rs.mReg[i]));
	}
	AddField(body, "consol_in", Hex8(gtia.ReadConsoleSwitchInputs()));
	StripTrailingComma(body);
	return body;
}

// ---------------------------------------------------------------------------
// POKEY payload
// ---------------------------------------------------------------------------

const char* PokeyRegName(int idx) {
	switch (idx) {
	case 0x00: return "AUDF1"; case 0x01: return "AUDC1";
	case 0x02: return "AUDF2"; case 0x03: return "AUDC2";
	case 0x04: return "AUDF3"; case 0x05: return "AUDC3";
	case 0x06: return "AUDF4"; case 0x07: return "AUDC4";
	case 0x08: return "AUDCTL";
	case 0x09: return "STIMER";
	case 0x0A: return "SKREST";
	case 0x0B: return "POTGO";
	case 0x0D: return "SEROUT";
	case 0x0E: return "IRQEN";
	case 0x0F: return "SKCTL";
	}
	return nullptr;
}

std::string BuildPokeyPayload(ATSimulator& sim) {
	auto& pokey = sim.GetPokey();
	ATPokeyRegisterState rs;
	pokey.GetRegisterState(rs);

	std::string body;
	for (int i = 0; i < 0x20; ++i) {
		const char* name = PokeyRegName(i);
		if (name) AddField(body, name, Hex8(rs.mReg[i]));
	}
	StripTrailingComma(body);
	return body;
}

// ---------------------------------------------------------------------------
// PIA payload
// ---------------------------------------------------------------------------

std::string BuildPiaPayload(ATSimulator& sim) {
	auto& pia = sim.GetPIA();
	ATPIAState ps;
	pia.GetState(ps);

	std::string body;
	AddField(body, "ORA",  Hex8(ps.mORA));
	AddField(body, "DDRA", Hex8(ps.mDDRA));
	AddField(body, "CRA",  Hex8(ps.mCRA));
	AddField(body, "ORB",  Hex8(ps.mORB));
	AddField(body, "DDRB", Hex8(ps.mDDRB));
	AddField(body, "CRB",  Hex8(ps.mCRB));
	// PORTA_OUT / PORTB_OUT are the values the PIA is DRIVING out
	// of the chip's pins (with DDR mask applied). They reflect what
	// the CPU has written to the output latches.
	AddField(body, "PORTA_OUT", Hex8(pia.GetPortAOutput()));
	AddField(body, "PORTB_OUT", Hex8(pia.GetPortBOutput()));
	// PORTA / PORTB are the values the CPU would READ from SWCHA /
	// SWCHB right now: a combination of the output latch (for bits
	// configured as output by DDR) and the external input pins (for
	// bits configured as input). The Atari joystick directions
	// arrive on these read-side bits — this is where bridge
	// injected joystick state shows up.
	AddField(body, "PORTA", Hex8(pia.DebugReadByte(0)));
	AddField(body, "PORTB", Hex8(pia.DebugReadByte(1)));
	StripTrailingComma(body);
	return body;
}

}  // namespace

// ---------------------------------------------------------------------------
// REGS
// ---------------------------------------------------------------------------

std::string CmdRegs(ATSimulator& sim, const std::vector<std::string>& /*tokens*/) {
	return JsonOk(BuildCpuPayload(sim));
}

// ---------------------------------------------------------------------------
// PEEK addr [length]
// ---------------------------------------------------------------------------

namespace {
constexpr uint32_t kMaxPeekLen = 16384;
}

std::string CmdPeek(ATSimulator& sim, const std::vector<std::string>& tokens) {
	if (tokens.size() < 2)
		return JsonError("PEEK: usage: PEEK addr [length]");
	uint32_t addr = 0;
	if (!ParseUint(tokens[1], addr))
		return JsonError("PEEK: bad address");
	if (addr > 0xFFFF)
		return JsonError("PEEK: address > $FFFF (use PEEK_BANK for extended memory; coming in phase 5)");
	uint32_t length = 1;
	if (tokens.size() >= 3) {
		if (!ParseUint(tokens[2], length))
			return JsonError("PEEK: bad length");
		if (length == 0)
			return JsonError("PEEK: length must be >= 1");
		if (length > kMaxPeekLen)
			return JsonError("PEEK: length too large (max 16384)");
	}
	if (addr + length > 0x10000)
		return JsonError("PEEK: range crosses end of 64K address space");

	std::vector<uint8_t> buf(length);
	for (uint32_t i = 0; i < length; ++i) {
		// DebugReadByte uses the CPU's view of memory: PORTB
		// banking, cartridge mapping, and OS ROM overlay all
		// applied. This matches what a debugger pane shows. The
		// "Debug" prefix bypasses I/O register side effects so
		// reading $D000-$D7FF doesn't trigger ANTIC/GTIA/POKEY
		// state changes.
		buf[i] = sim.DebugReadByte((uint16_t)(addr + i));
	}

	std::string payload;
	AddField(payload, "addr",   Hex16(addr));
	AddU32  (payload, "length", length);
	payload += "\"data\":\"";
	payload += HexBytes(buf.data(), buf.size());
	payload += "\"";
	return JsonOk(payload);
}

// ---------------------------------------------------------------------------
// PEEK16 addr
// ---------------------------------------------------------------------------

std::string CmdPeek16(ATSimulator& sim, const std::vector<std::string>& tokens) {
	if (tokens.size() < 2)
		return JsonError("PEEK16: usage: PEEK16 addr");
	uint32_t addr = 0;
	if (!ParseUint(tokens[1], addr))
		return JsonError("PEEK16: bad address");
	if (addr > 0xFFFE)
		return JsonError("PEEK16: address > $FFFE");

	uint8_t lo = sim.DebugReadByte((uint16_t)addr);
	uint8_t hi = sim.DebugReadByte((uint16_t)(addr + 1));
	uint16_t value = (uint16_t)lo | ((uint16_t)hi << 8);

	std::string payload;
	AddField(payload, "addr",  Hex16(addr));
	AddField(payload, "value", Hex16(value));
	StripTrailingComma(payload);
	return JsonOk(payload);
}

// ---------------------------------------------------------------------------
// ANTIC / GTIA / POKEY / PIA
// ---------------------------------------------------------------------------

std::string CmdAntic(ATSimulator& sim, const std::vector<std::string>& /*tokens*/) {
	return JsonOk(BuildAnticPayload(sim));
}

std::string CmdGtia(ATSimulator& sim, const std::vector<std::string>& /*tokens*/) {
	return JsonOk(BuildGtiaPayload(sim));
}

std::string CmdPokey(ATSimulator& sim, const std::vector<std::string>& /*tokens*/) {
	return JsonOk(BuildPokeyPayload(sim));
}

std::string CmdPia(ATSimulator& sim, const std::vector<std::string>& /*tokens*/) {
	return JsonOk(BuildPiaPayload(sim));
}

// ---------------------------------------------------------------------------
// DLIST
// ---------------------------------------------------------------------------

std::string CmdDlist(ATSimulator& sim, const std::vector<std::string>& /*tokens*/) {
	auto& antic = sim.GetAntic();
	const auto* hist = antic.GetDLHistory();

	std::string entriesArr = "[";
	bool first = true;
	for (int i = 0; i < 312; ++i) {
		const auto& e = hist[i];
		if (!e.mbValid)
			continue;
		if (!first) entriesArr += ',';
		first = false;

		// Decode the ANTIC display list instruction byte. The
		// high nibble has different semantics depending on the
		// mode bits in the low nibble:
		//
		//   Mode 0 (blank lines):
		//     bits 4-6 = (number_of_blank_lines - 1), so 0 = 1
		//                line, 7 = 8 lines.
		//     bit 7    = DLI.
		//
		//   Mode 1 (jump):
		//     bit 6    = JVB (jump and wait for vertical blank).
		//     bit 7    = DLI.
		//     The next 2 bytes of the DL stream are the jump
		//     target address (we read them via DebugReadByte
		//     below for convenience).
		//
		//   Modes 2..15 (graphics modes):
		//     bit 4    = HSCROL enable for this row.
		//     bit 5    = VSCROL enable for this row.
		//     bit 6    = LMS (load memory scan): the next 2 bytes
		//                of the DL stream are the new playfield
		//                fetch address (mPFAddress reflects this).
		//     bit 7    = DLI.
		//
		// We always emit `mode`, `ctl`, `kind`, `dli`. The other
		// fields are mode-specific: graphics modes get
		// `lms`/`hscrol`/`vscrol`/`pf`/`chbase`/`dmactl`; blank
		// modes get `blank_lines`; jump modes get `jvb` and
		// `target`. The `kind` field tells the client which
		// fields to look for.
		const uint8_t ctl  = e.mControl;
		const uint8_t mode = ctl & 0x0F;
		const bool    dli  = (ctl & 0x80) != 0;

		std::string entry = "{";
		AddField(entry, "addr", Hex16(e.mDLAddress));
		AddField(entry, "ctl",  Hex8(ctl));
		AddU32  (entry, "mode", mode);
		entry += "\"dli\":"; entry += (dli ? "true," : "false,");

		if (mode == 0) {
			// Blank lines.
			const uint32_t blankLines = ((ctl >> 4) & 0x07) + 1;
			entry += "\"kind\":\"blank\",";
			AddU32(entry, "blank_lines", blankLines);
		} else if (mode == 1) {
			// Jump or jump-and-wait-for-VBI. The 2-byte target
			// follows the instruction byte in the DL stream.
			const bool jvb = (ctl & 0x40) != 0;
			entry += "\"kind\":\"";
			entry += jvb ? "jvb" : "jmp";
			entry += "\",";
			entry += "\"jvb\":"; entry += (jvb ? "true," : "false,");
			const uint8_t lo = sim.DebugReadByte((uint16_t)(e.mDLAddress + 1));
			const uint8_t hi = sim.DebugReadByte((uint16_t)(e.mDLAddress + 2));
			const uint16_t target = (uint16_t)lo | ((uint16_t)hi << 8);
			AddField(entry, "target", Hex16(target));
		} else {
			// Graphics modes 2..15.
			const bool lms    = (ctl & 0x40) != 0;
			const bool hscrol = (ctl & 0x10) != 0;
			const bool vscrol = (ctl & 0x20) != 0;
			entry += "\"kind\":\"graphics\",";
			entry += "\"lms\":";    entry += (lms    ? "true," : "false,");
			entry += "\"hscrol\":"; entry += (hscrol ? "true," : "false,");
			entry += "\"vscrol\":"; entry += (vscrol ? "true," : "false,");
			AddField(entry, "pf",     Hex16(e.mPFAddress));
			AddField(entry, "dmactl", Hex8(e.mDMACTL));
			AddU32  (entry, "chbase", (uint32_t)e.mCHBASE);
		}

		StripTrailingComma(entry);
		entry += "}";
		entriesArr += entry;
	}
	entriesArr += "]";

	std::string payload;
	AddField(payload, "dlist", Hex16(antic.GetDisplayListPtr()));
	payload += "\"entries\":";
	payload += entriesArr;
	return JsonOk(payload);
}

// ---------------------------------------------------------------------------
// HWSTATE — combined CPU + ANTIC + GTIA + POKEY + PIA
// ---------------------------------------------------------------------------

std::string CmdHwstate(ATSimulator& sim, const std::vector<std::string>& /*tokens*/) {
	std::string payload;
	payload += "\"cpu\":{";   payload += BuildCpuPayload(sim);   payload += "},";
	payload += "\"antic\":{"; payload += BuildAnticPayload(sim); payload += "},";
	payload += "\"gtia\":{";  payload += BuildGtiaPayload(sim);  payload += "},";
	payload += "\"pokey\":{"; payload += BuildPokeyPayload(sim); payload += "},";
	payload += "\"pia\":{";   payload += BuildPiaPayload(sim);   payload += "}";
	return JsonOk(payload);
}

// ---------------------------------------------------------------------------
// PALETTE — GTIA analysis palette, 256 entries × 24-bit RGB
// ---------------------------------------------------------------------------

std::string CmdPalette(ATSimulator& sim, const std::vector<std::string>& /*tokens*/) {
	auto& gtia = sim.GetGTIA();
	uint32_t pal[256];
	gtia.GetPalette(pal);

	// Pack 256 RGB triples into a flat hex string. Each entry is 3
	// bytes (RGB, big-endian within the triple). Total payload is
	// 768 bytes -> 1536 hex chars. Significantly cheaper than 256
	// JSON sub-objects.
	uint8_t bytes[256 * 3];
	for (int i = 0; i < 256; ++i) {
		uint32_t rgb = pal[i] & 0xFFFFFF;
		bytes[i * 3 + 0] = (uint8_t)((rgb >> 16) & 0xff);
		bytes[i * 3 + 1] = (uint8_t)((rgb >>  8) & 0xff);
		bytes[i * 3 + 2] = (uint8_t)( rgb        & 0xff);
	}

	std::string payload;
	AddU32(payload, "entries", 256);
	payload += "\"format\":\"rgb24\",";
	payload += "\"data\":\"";
	payload += HexBytes(bytes, sizeof bytes);
	payload += "\"";
	return JsonOk(payload);
}

// ---------------------------------------------------------------------------
// PALETTE_LOAD_ACT  <base64-of-768-bytes>
//
// Adobe Color Table .act loader. Mirrors Windows Altirra's Color
// Image Reference dialog code path (src/Altirra/source/uicolors.cpp
// OnCommandLoad / OnCommandMatch) 1:1, using the same solver, the
// same two-pass schedule, and applying the result via the same
// GTIA::SetColorSettings() accessor. Runs synchronously on the
// bridge main thread, which typically takes under a second.
//
// The .act format is a flat 768-byte array of 256 × (R, G, B).
// We unpack it to a 256-entry 0x00RRGGBB uint32 table, seed the
// solver with the currently active ATColorParams, run it to
// convergence (capped), then re-seed with matching=sRGB and run
// again — same as Windows does on its background worker thread.
//
// Bounded iteration count: ~2000 passes per phase. Empirically
// Windows converges in well under that on typical .act inputs;
// the cap just protects against a pathological input that never
// stops improving. See palettesolver.cpp for the algorithm.
// ---------------------------------------------------------------------------

std::string CmdPaletteLoadAct(ATSimulator& sim, const std::vector<std::string>& tokens) {
	if (tokens.size() < 2)
		return JsonError("PALETTE_LOAD_ACT: usage: PALETTE_LOAD_ACT base64_of_768_bytes");

	std::vector<uint8_t> rgb;
	if (!Base64Decode(tokens[1], rgb))
		return JsonError("PALETTE_LOAD_ACT: bad base64 payload");
	if (rgb.size() != 768)
		return JsonError("PALETTE_LOAD_ACT: expected 768 bytes (256 x RGB), got different size");

	// Unpack RGB triples into GTIA's uint32 0x00RRGGBB layout.
	uint32 target[256];
	for (int i = 0; i < 256; ++i) {
		target[i] = ((uint32)rgb[i*3 + 0] << 16)
		          | ((uint32)rgb[i*3 + 1] <<  8)
		          | ((uint32)rgb[i*3 + 2]);
	}

	// Snapshot the active color profile (NTSC or PAL depending on
	// the current hardware mode). The Windows UI does this split on
	// dialog open; we do it on each call so multiple clients don't
	// step on each other across mode changes.
	ATGTIAEmulator& gtia = sim.GetGTIA();
	ATColorSettings settings = gtia.GetColorSettings();
	const bool usePAL = settings.mbUsePALParams && gtia.IsPALMode();
	ATColorParams initialParams = usePAL ? (ATColorParams&)settings.mPALParams
	                                     : (ATColorParams&)settings.mNTSCParams;

	// --- Solver, pass 1: matching mode None -----------------------
	std::unique_ptr<IATColorPaletteSolver> solver(ATCreateColorPaletteSolver());
	// Init seeds the solver with the initial params + target
	// palette. lockHueStart/lockGamma are both false — the dialog
	// exposes them as checkboxes but defaults both off.
	solver->Init(initialParams, target, false, false);

	ATColorParams pass1Init = initialParams;
	pass1Init.mColorMatchingMode = ATColorMatchingMode::None;
	solver->Reinit(pass1Init);

	const int kMaxIterPerPass = 2000;
	int iter = 0;
	while (iter++ < kMaxIterPerPass) {
		auto status = solver->Iterate();
		if (status == IATColorPaletteSolver::Status::Finished)
			break;
	}

	ATColorParams pass1Result = pass1Init;
	solver->GetCurrentSolution(pass1Result);
	uint32 pass1Err = solver->GetCurrentError().value_or(0);

	// --- Solver, pass 2: matching mode sRGB -----------------------
	ATColorParams pass2Init = pass1Result;
	pass2Init.mColorMatchingMode = ATColorMatchingMode::SRGB;
	solver->Reinit(pass2Init);

	iter = 0;
	while (iter++ < kMaxIterPerPass) {
		auto status = solver->Iterate();
		if (status == IATColorPaletteSolver::Status::Finished)
			break;
	}

	ATColorParams finalResult = pass2Init;
	solver->GetCurrentSolution(finalResult);
	uint32 finalErr = solver->GetCurrentError().value_or(pass1Err);

	// --- Apply to GTIA --------------------------------------------
	if (usePAL) (ATColorParams&)settings.mPALParams = finalResult;
	else        (ATColorParams&)settings.mNTSCParams = finalResult;
	gtia.SetColorSettings(settings);

	// Convert the solver's raw sum-of-squared-byte-errors into the
	// same per-channel standard error Windows reports: sqrt(err /
	// 719) where 719 = 240 colors × 3 channels − 1 (unbiased
	// estimator). See uicolors.cpp:1418.
	const float stdError = std::sqrt((float)finalErr / 719.0f);

	std::string payload;
	char buf[64];
	std::snprintf(buf, sizeof buf, "%.6g", (double)stdError);
	payload += "\"rms_error\":";
	payload += buf;
	payload += ",";
	payload += "\"mode\":\"";
	payload += (usePAL ? "PAL" : "NTSC");
	payload += "\"";
	return JsonOk(payload);
}

// ---------------------------------------------------------------------------
// PALETTE_RESET — restore the factory-default NTSC and PAL color
// parameters, undoing any prior PALETTE_LOAD_ACT. We copy the
// presets the way GTIA's own constructor does (see gtia.cpp:849),
// preserving the names of the ATNamedColorParams wrappers so the
// UI and save-state names stay meaningful.
// ---------------------------------------------------------------------------

std::string CmdPaletteReset(ATSimulator& sim, const std::vector<std::string>& /*tokens*/) {
	ATGTIAEmulator& gtia = sim.GetGTIA();
	ATColorSettings settings = gtia.GetColorSettings();

	const sint32 ntscIdx = ATGetColorPresetIndexByTag("default_ntsc");
	const sint32 palIdx  = ATGetColorPresetIndexByTag("default_pal");
	if (ntscIdx < 0 || palIdx < 0)
		return JsonError("PALETTE_RESET: default_ntsc / default_pal preset missing");

	(ATColorParams&)settings.mNTSCParams = ATGetColorPresetByIndex((uint32)ntscIdx);
	(ATColorParams&)settings.mPALParams  = ATGetColorPresetByIndex((uint32)palIdx);
	gtia.SetColorSettings(settings);
	return JsonOk();
}

}  // namespace ATBridge
