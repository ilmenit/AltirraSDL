// AltirraBridge - Phase 5a debugger commands (impl)
//
// Every command here wraps an existing public Altirra accessor. See
// bridge_commands_debug.h for the per-command rationale.

#include <stdafx.h>

#include "bridge_commands_debug.h"
#include "bridge_protocol.h"

#include "simulator.h"
#include "cpu.h"
#include "cpumemory.h"
#include "gtia.h"
#include "pia.h"
#include "cartridge.h"

#include <at/atcpu/history.h>
#include <at/ataudio/pokey.h>
#include <at/atio/cartridgetypes.h>

#include "disasm.h"
#include "debugger.h"
#include "debuggerexp.h"

#include <vd2/system/error.h>
#include <vd2/system/VDString.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace ATBridge {

namespace {

// ---------------------------------------------------------------------------
// Small formatting helpers (local copies to keep this file self-contained
// without leaning on bridge_commands_state.cpp internals).
// ---------------------------------------------------------------------------

std::string Hex8 (uint32_t v) { char b[8];  std::snprintf(b, sizeof b, "\"$%02x\"", v & 0xff);    return b; }
std::string Hex16(uint32_t v) { char b[12]; std::snprintf(b, sizeof b, "\"$%04x\"", v & 0xffff);  return b; }

void AddField(std::string& out, const char* key, const std::string& valueLiteral) {
	out += '"';  out += key;  out += "\":";  out += valueLiteral;  out += ',';
}

void AddU32(std::string& out, const char* key, uint32_t value) {
	out += '"';  out += key;  out += "\":";  out += std::to_string(value);  out += ',';
}

void AddStr(std::string& out, const char* key, const std::string& value) {
	out += '"';  out += key;  out += "\":\"";  out += JsonEscape(value);  out += "\",";
}

void AddBool(std::string& out, const char* key, bool value) {
	out += '"';  out += key;  out += "\":";  out += (value ? "true" : "false");  out += ',';
}

void StripTrailingComma(std::string& s) {
	if (!s.empty() && s.back() == ',') s.pop_back();
}

// ---------------------------------------------------------------------------
// ParseAddr16 — accepts "$XXXX", "0xXXXX", or decimal.
// ---------------------------------------------------------------------------

bool ParseAddr16(const std::string& s, uint16_t& out) {
	uint32_t v = 0;
	if (!ParseUint(s, v)) return false;
	if (v > 0xffff) return false;
	out = (uint16_t)v;
	return true;
}

// ---------------------------------------------------------------------------
// GTIA / POKEY register state snapshot helpers.
// ---------------------------------------------------------------------------

void SnapshotGtiaRegs(ATGTIAEmulator& gtia, uint8_t out[32]) {
	ATGTIARegisterState st {};
	gtia.GetRegisterState(st);
	std::memcpy(out, st.mReg, 32);
}

void SnapshotPokeyRegs(ATPokeyEmulator& pokey, uint8_t out[32]) {
	ATPokeyRegisterState st {};
	pokey.GetRegisterState(st);
	std::memcpy(out, st.mReg, 32);
}

}  // namespace

// ===========================================================================
// DISASM addr [count]
// ===========================================================================

std::string CmdDisasm(ATSimulator& sim, const std::vector<std::string>& tokens) {
	if (tokens.size() < 2)
		return JsonError("DISASM: usage: DISASM addr [count]");

	uint16_t pc = 0;
	if (!ParseAddr16(tokens[1], pc))
		return JsonError("DISASM: bad address");

	uint32_t count = 1;
	if (tokens.size() >= 3) {
		if (!ParseUint(tokens[2], count))
			return JsonError("DISASM: bad count");
	}
	if (count == 0) count = 1;
	if (count > 1024) count = 1024;

	IATDebugger *dbg = ATGetDebugger();
	IATDebugTarget *target = dbg ? dbg->GetTarget() : nullptr;
	if (!target)
		return JsonError("DISASM: no debug target");

	ATDebugDisasmMode mode = target->GetDisasmMode();

	std::string insns;
	insns += '[';
	bool first = true;

	// Walk instructions. mNextPC wraps inside 64K naturally.
	for (uint32_t i = 0; i < count; ++i) {
		ATCPUHistoryEntry hent {};
		ATDisassembleCaptureInsnContext(target, pc, (uint8_t)0, hent);
		ATDisassembleCaptureRegisterContext(target, hent);

		VDStringA buf;
		ATDisasmResult result = ATDisassembleInsn(
			buf, target, mode, hent,
			/*decodeReferences*/   true,
			/*decodeRefsHistory*/  false,
			/*showPCAddress*/      false,
			/*showCodeBytes*/      false,
			/*showLabels*/         true,
			/*lowercaseOps*/       true,
			/*wideOpcode*/         false,
			/*showLabelNamespaces*/true,
			/*showSymbols*/        true,
			/*showGlobalPC*/       false);

		// Strip trailing whitespace / newline ATDisassembleInsn may add.
		std::string text(buf.c_str());
		while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
			text.pop_back();

		uint16_t nextPc = result.mNextPC;
		uint32_t length = (uint32_t)((uint16_t)(nextPc - pc));
		if (length == 0 || length > 4) length = 1;

		// Raw opcode bytes (length from hent)
		uint8_t bytes[4] = { hent.mOpcode[0], hent.mOpcode[1], hent.mOpcode[2], hent.mOpcode[3] };

		if (!first) insns += ',';
		first = false;
		insns += '{';
		{
			std::string entry;
			AddField(entry, "addr", Hex16(pc));
			AddU32  (entry, "length", length);
			// bytes as array of 2-digit hex strings
			entry += "\"bytes\":[";
			for (uint32_t b = 0; b < length; ++b) {
				if (b) entry += ',';
				char tmp[8];
				std::snprintf(tmp, sizeof tmp, "\"%02x\"", bytes[b]);
				entry += tmp;
			}
			entry += "],";
			AddStr  (entry, "text",   text);
			AddField(entry, "next",   Hex16(nextPc));
			StripTrailingComma(entry);
			insns += entry;
		}
		insns += '}';

		pc = nextPc;
	}
	insns += ']';

	std::string payload;
	payload += "\"insns\":";
	payload += insns;
	return JsonOk(payload);
}

// ===========================================================================
// HISTORY [count]
// ===========================================================================

std::string CmdHistory(ATSimulator& sim, const std::vector<std::string>& tokens) {
	uint32_t count = 64;
	if (tokens.size() >= 2) {
		if (!ParseUint(tokens[1], count))
			return JsonError("HISTORY: bad count");
	}
	if (count == 0) count = 1;
	if (count > 4096) count = 4096;

	ATCPUEmulator& cpu = sim.GetCPU();
	// GetHistoryLength() is the ring-buffer CAPACITY (compile-time
	// constant, 131072). GetHistoryCounter() is the total number of
	// instructions ever recorded — the only reliable measure of how
	// many entries are valid. On a freshly booted sim the first few
	// dozen entries are real; everything beyond the counter is stale
	// ring-buffer fill.
	const uint32_t capacity = (uint32_t)cpu.GetHistoryLength();
	const uint32_t recorded = cpu.GetHistoryCounter();
	uint32_t valid = recorded < capacity ? recorded : capacity;
	if (valid == 0)
		return JsonError("HISTORY: no instructions recorded yet");

	uint32_t n = count;
	if (n > valid) n = valid;

	// Entries are ordered most-recent-first at index 0. We emit
	// chronological (oldest → newest) so clients can read like a log.
	std::string entries;
	entries += '[';
	for (uint32_t i = 0; i < n; ++i) {
		const ATCPUHistoryEntry& h = cpu.GetHistory(n - 1 - i);
		if (i) entries += ',';
		entries += '{';
		std::string e;
		AddU32  (e, "cycle",    h.mCycle);
		AddU32  (e, "ucycle",   h.mUnhaltedCycle);
		AddField(e, "pc",       Hex16(h.mPC));
		AddField(e, "op",       Hex8 (h.mOpcode[0]));
		AddField(e, "a",        Hex8 (h.mA));
		AddField(e, "x",        Hex8 (h.mX));
		AddField(e, "y",        Hex8 (h.mY));
		AddField(e, "s",        Hex8 (h.mS));
		AddField(e, "p",        Hex8 (h.mP));
		AddField(e, "ea",       Hex16(h.mEA & 0xffff));
		AddBool (e, "irq",      h.mbIRQ);
		AddBool (e, "nmi",      h.mbNMI);
		StripTrailingComma(e);
		entries += e;
		entries += '}';
	}
	entries += ']';

	std::string payload;
	AddU32(payload, "count", n);
	payload += "\"entries\":";
	payload += entries;
	return JsonOk(payload);
}

// ===========================================================================
// EVAL expr...
//
// All tokens after the verb are joined with spaces and passed to
// EvaluateThrow. The client is responsible for quoting / escaping if
// the expression needs to survive the whitespace tokenizer.
// ===========================================================================

std::string CmdEval(ATSimulator& sim, const std::vector<std::string>& tokens) {
	(void)sim;
	if (tokens.size() < 2)
		return JsonError("EVAL: usage: EVAL expression");

	std::string expr;
	for (size_t i = 1; i < tokens.size(); ++i) {
		if (i > 1) expr += ' ';
		expr += tokens[i];
	}

	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return JsonError("EVAL: debugger unavailable");

	int32_t result = 0;
	try {
		result = dbg->EvaluateThrow(expr.c_str());
	} catch (const MyError& e) {
		std::string msg = e.c_str();
		while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r'))
			msg.pop_back();
		return JsonError(std::string("EVAL: ") + msg);
	}

	std::string payload;
	AddStr  (payload, "expr",  expr);
	AddU32  (payload, "value", (uint32_t)result);
	AddField(payload, "hex",   Hex16((uint32_t)result & 0xffff));
	StripTrailingComma(payload);
	return JsonOk(payload);
}

// ===========================================================================
// CALLSTACK [count]
// ===========================================================================

std::string CmdCallStack(ATSimulator& sim, const std::vector<std::string>& tokens) {
	(void)sim;
	uint32_t count = 64;
	if (tokens.size() >= 2) {
		if (!ParseUint(tokens[1], count))
			return JsonError("CALLSTACK: bad count");
	}
	if (count == 0) count = 1;
	if (count > 256) count = 256;

	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return JsonError("CALLSTACK: debugger unavailable");

	std::vector<ATCallStackFrame> frames(count);
	uint32_t n = dbg->GetCallStack(frames.data(), count);

	std::string arr;
	arr += '[';
	for (uint32_t i = 0; i < n; ++i) {
		if (i) arr += ',';
		arr += '{';
		std::string e;
		AddField(e, "pc", Hex16(frames[i].mPC & 0xffff));
		AddField(e, "sp", Hex8 (frames[i].mSP & 0xff));
		AddField(e, "p",  Hex8 (frames[i].mP));
		StripTrailingComma(e);
		arr += e;
		arr += '}';
	}
	arr += ']';

	std::string payload;
	AddU32(payload, "count", n);
	payload += "\"frames\":";
	payload += arr;
	return JsonOk(payload);
}

// ===========================================================================
// BANK_INFO — decode PIA PORTB (XL/XE banking control).
//
// PORTB bit meaning on 800XL/130XE (reference: Altirra Hardware Ref):
//   bit 0: OS ROM enable          (1 = ROM, 0 = RAM under $C000-FFFF)
//   bit 1: BASIC enable           (1 = BASIC disabled, 0 = enabled
//                                  at $A000-BFFF — inverted sense)
//   bit 2: 130XE bank select bit 0
//   bit 3: 130XE bank select bit 1
//   bit 4: 130XE CPU bank access  (0 = bank mapped at $4000-7FFF
//                                  for CPU)
//   bit 5: 130XE ANTIC bank access(0 = bank mapped at $4000-7FFF
//                                  for ANTIC)
//   bit 6: unused on stock 130XE  (some clones repurpose it)
//   bit 7: self-test ROM enable   (0 = self-test at $5000, 1 = RAM)
//
// The 4-bank select gives 4 × 16KB = 64KB of extended RAM on a
// stock 130XE. Machine-specific aftermarket banking (Rambo 256K,
// etc.) remaps some of these bits; clients that need that level of
// detail should read the raw PORTB byte we return alongside the
// decoded fields.
// ===========================================================================

std::string CmdBankInfo(ATSimulator& sim, const std::vector<std::string>& tokens) {
	(void)tokens;
	const uint8_t portb = sim.GetPIA().GetPortBOutput();

	const bool    os_rom              = (portb & 0x01) != 0;
	const bool    basic_off           = (portb & 0x02) != 0;  // inverted
	const bool    selftest_ram        = (portb & 0x80) != 0;  // inverted
	const bool    cpu_bank_at_window  = (portb & 0x10) == 0;
	const bool    antic_bank_at_window= (portb & 0x20) == 0;
	const uint8_t xe_bank             = (uint8_t)((portb >> 2) & 0x03);

	std::string payload;
	AddField(payload, "portb",               Hex8(portb));
	AddBool (payload, "os_rom",              os_rom);
	AddBool (payload, "basic_enabled",       !basic_off);
	AddBool (payload, "selftest_rom",        !selftest_ram);
	AddBool (payload, "cpu_bank_at_window",  cpu_bank_at_window);
	AddBool (payload, "antic_bank_at_window",antic_bank_at_window);
	AddU32  (payload, "xe_bank",             xe_bank);
	StripTrailingComma(payload);
	return JsonOk(payload);
}

// ===========================================================================
// CART_INFO — mapper mode (numeric) + current bank.
//
// We emit the numeric ATCartridgeMode value; clients that care about
// the name can map it via a small constant table shipped alongside
// the SDK (the enum values are stable in cartridgetypes.h).
// ===========================================================================

std::string CmdCartInfo(ATSimulator& sim, const std::vector<std::string>& tokens) {
	(void)tokens;
	ATCartridgeEmulator *cart = sim.GetCartridge(0);

	std::string payload;
	if (!cart) {
		AddBool(payload, "present", false);
		AddU32 (payload, "mode",    0);
		StripTrailingComma(payload);
		return JsonOk(payload);
	}

	ATCartridgeMode mode = cart->GetMode();
	int bank = cart->GetCartBank();

	AddBool(payload, "present", mode != kATCartridgeMode_None);
	AddU32 (payload, "mode",    (uint32_t)mode);
	AddU32 (payload, "size",    ATGetImageSizeForCartridgeType(mode));
	AddField(payload, "bank",   std::to_string(bank));
	StripTrailingComma(payload);
	return JsonOk(payload);
}

// ===========================================================================
// MEMMAP — high-level layout summary (decoded from PORTB + cart state).
//
// This is NOT a per-byte probe; it's the same summary a human reads
// off the "memory map" debug pane. Regions reported:
//   $0000-$BFFF : RAM (with 130XE bank annotation if in bank window)
//   $8000-$BFFF : BASIC ROM (if basic_enabled and not cart-overridden)
//   $5000-$57FF : self-test ROM (if selftest_rom)
//   $C000-$FFFF : OS ROM (if os_rom) else RAM
//   $D000-$D7FF : hardware
//   cart window : decoded from CART_INFO
// ===========================================================================

std::string CmdMemMap(ATSimulator& sim, const std::vector<std::string>& tokens) {
	(void)tokens;
	const uint8_t portb = sim.GetPIA().GetPortBOutput();
	const bool os_rom       = (portb & 0x01) != 0;
	const bool basic_off    = (portb & 0x02) != 0;
	const bool selftest_rom = (portb & 0x80) == 0;

	ATCartridgeEmulator *cart = sim.GetCartridge(0);
	const ATCartridgeMode cartMode = cart ? cart->GetMode() : kATCartridgeMode_None;
	const bool cartPresent = cartMode != kATCartridgeMode_None;

	auto addRegion = [](std::string& arr, bool& first, const char* name,
	                    uint16_t lo, uint16_t hi, const char* kind, const std::string& note) {
		if (!first) arr += ',';
		first = false;
		arr += '{';
		std::string e;
		AddStr  (e, "name", name);
		AddField(e, "lo",   Hex16(lo));
		AddField(e, "hi",   Hex16(hi));
		AddStr  (e, "kind", kind);
		if (!note.empty()) AddStr(e, "note", note);
		StripTrailingComma(e);
		arr += e;
		arr += '}';
	};

	std::string regions;
	regions += '[';
	bool first = true;

	addRegion(regions, first, "low RAM",   0x0000, 0x3FFF, "ram", "");
	addRegion(regions, first, "bank RAM",  0x4000, 0x7FFF, "ram", "130XE bank window (see BANK_INFO)");

	if (cartPresent)
		addRegion(regions, first, "cartridge", 0x8000, 0xBFFF, "rom",
		          std::string("mode=") + std::to_string((int)cartMode));
	else if (!basic_off)
		addRegion(regions, first, "BASIC",    0xA000, 0xBFFF, "rom", "PORTB bit1=0");
	else
		addRegion(regions, first, "high RAM", 0x8000, 0xBFFF, "ram", "");

	if (selftest_rom)
		addRegion(regions, first, "self-test", 0x5000, 0x57FF, "rom", "PORTB bit7=0");

	addRegion(regions, first, "hardware",     0xD000, 0xD7FF, "io", "GTIA/POKEY/PIA/ANTIC");
	addRegion(regions, first, "OS area",
	          0xD800, 0xFFFF,
	          os_rom ? "rom" : "ram",
	          os_rom ? "OS ROM (PORTB bit0=1)" : "RAM (PORTB bit0=0)");

	regions += ']';

	std::string payload;
	AddField(payload, "portb", Hex8(portb));
	payload += "\"regions\":";
	payload += regions;
	return JsonOk(payload);
}

// ===========================================================================
// PMG — decoded player/missile state from the GTIA register shadow.
//
// Field layout:
//   hposp[0..3], hposm[0..3]     — horizontal positions
//   sizep[0..3], sizem           — size modes
//   grafp[0..3], grafm           — graphics data
//   colpm[0..3], colpf[0..3]     — colors
//   prior                         — priority / PM5 / GTIA mode
//   vdelay, gractl, hitclr       — control
//   coll_mpf[0..3] coll_ppf[0..3] coll_mpl[0..3] coll_ppl[0..3]
//                                  — collision read-side registers
// ===========================================================================

std::string CmdPmg(ATSimulator& sim, const std::vector<std::string>& tokens) {
	(void)tokens;
	ATGTIAEmulator& gtia = sim.GetGTIA();

	uint8_t reg[32];
	SnapshotGtiaRegs(gtia, reg);

	// Write-side register layout (GTIA $D000-$D01F shadow):
	//   $00-$03 HPOSP0-HPOSP3
	//   $04-$07 HPOSM0-HPOSM3
	//   $08-$0B SIZEP0-SIZEP3
	//   $0C     SIZEM
	//   $0D-$10 GRAFP0-GRAFP3
	//   $11     GRAFM
	//   $12-$15 COLPM0-COLPM3
	//   $16-$19 COLPF0-COLPF3
	//   $1A     COLBK
	//   $1B     PRIOR
	//   $1C     VDELAY
	//   $1D     GRACTL
	//   $1E     HITCLR (write-only, no meaningful read)

	auto arr4 = [](const uint8_t* b) {
		std::string s = "[";
		for (int i = 0; i < 4; ++i) {
			if (i) s += ',';
			s += Hex8(b[i]);
		}
		s += ']';
		return s;
	};

	std::string payload;
	AddField(payload, "hposp", arr4(&reg[0x00]));
	AddField(payload, "hposm", arr4(&reg[0x04]));
	AddField(payload, "sizep", arr4(&reg[0x08]));
	AddField(payload, "sizem", Hex8(reg[0x0c]));
	AddField(payload, "grafp", arr4(&reg[0x0d]));
	AddField(payload, "grafm", Hex8(reg[0x11]));
	AddField(payload, "colpm", arr4(&reg[0x12]));
	AddField(payload, "colpf", arr4(&reg[0x16]));
	AddField(payload, "colbk", Hex8(reg[0x1a]));
	AddField(payload, "prior", Hex8(reg[0x1b]));
	AddField(payload, "vdelay",Hex8(reg[0x1c]));
	AddField(payload, "gractl",Hex8(reg[0x1d]));

	// Collision read-side ($D000-$D00F):
	//   $00-$03 M0PF-M3PF     missile-playfield
	//   $04-$07 P0PF-P3PF     player-playfield
	//   $08-$0B M0PL-M3PL     missile-player
	//   $0C-$0F P0PL-P3PL     player-player
	uint8_t coll[16];
	for (int i = 0; i < 16; ++i) coll[i] = gtia.DebugReadByte((uint8_t)i);

	AddField(payload, "coll_mpf", arr4(&coll[0]));
	AddField(payload, "coll_ppf", arr4(&coll[4]));
	AddField(payload, "coll_mpl", arr4(&coll[8]));
	AddField(payload, "coll_ppl", arr4(&coll[12]));

	StripTrailingComma(payload);
	return JsonOk(payload);
}

// ===========================================================================
// AUDIO_STATE — decoded POKEY per-channel audio state.
//
// AUDCTL bit layout (POKEY $D208):
//   bit 7: 1 = 9-bit poly, 0 = 17-bit
//   bit 6: 1 = channel 1 clocked at 1.79 MHz
//   bit 5: 1 = channel 3 clocked at 1.79 MHz
//   bit 4: 1 = join channels 1+2 into 16-bit counter
//   bit 3: 1 = join channels 3+4 into 16-bit counter
//   bit 2: 1 = high-pass channel 1 through channel 3
//   bit 1: 1 = high-pass channel 2 through channel 4
//   bit 0: 1 = 15 kHz base clock (else 64 kHz)
//
// AUDCx bit layout:
//   bits 7-5: distortion (poly) select
//   bit 4:    force volume only (no waveform)
//   bits 3-0: volume (0..15)
// ===========================================================================

std::string CmdAudioState(ATSimulator& sim, const std::vector<std::string>& tokens) {
	(void)tokens;
	ATPokeyEmulator& pokey = sim.GetPokey();

	uint8_t reg[32];
	SnapshotPokeyRegs(pokey, reg);

	const uint8_t audctl = reg[0x08];
	const bool nine_bit  = (audctl & 0x80) != 0;
	const bool fast_ch1  = (audctl & 0x40) != 0;
	const bool fast_ch3  = (audctl & 0x20) != 0;
	const bool join_12   = (audctl & 0x10) != 0;
	const bool join_34   = (audctl & 0x08) != 0;
	const bool hp_1_3    = (audctl & 0x04) != 0;
	const bool hp_2_4    = (audctl & 0x02) != 0;
	const bool base_15k  = (audctl & 0x01) != 0;

	std::string channels;
	channels += '[';
	for (int ch = 0; ch < 4; ++ch) {
		const uint8_t audf = reg[0x00 + ch * 2];  // AUDF1..4 at $D200,$D202,$D204,$D206
		const uint8_t audc = reg[0x01 + ch * 2];  // AUDC1..4 at $D201,$D203,$D205,$D207
		const uint8_t distortion = (uint8_t)((audc >> 5) & 0x07);
		const bool    volume_only = (audc & 0x10) != 0;
		const uint8_t volume     = (uint8_t)(audc & 0x0f);

		// Clock source for this channel
		const char* clock_name = base_15k ? "15kHz" : "64kHz";
		if (ch == 0 && fast_ch1) clock_name = "1.79MHz";
		if (ch == 2 && fast_ch3) clock_name = "1.79MHz";

		if (ch) channels += ',';
		channels += '{';
		std::string e;
		AddU32  (e, "channel",    (uint32_t)(ch + 1));
		AddField(e, "audf",       Hex8(audf));
		AddField(e, "audc",       Hex8(audc));
		AddU32  (e, "volume",     volume);
		AddBool (e, "volume_only",volume_only);
		AddU32  (e, "distortion", distortion);
		AddStr  (e, "clock",      clock_name);
		StripTrailingComma(e);
		channels += e;
		channels += '}';
	}
	channels += ']';

	std::string payload;
	AddField(payload, "audctl",    Hex8(audctl));
	AddBool (payload, "nine_bit_poly", nine_bit);
	AddBool (payload, "join_1_2",  join_12);
	AddBool (payload, "join_3_4",  join_34);
	AddBool (payload, "highpass_1_3", hp_1_3);
	AddBool (payload, "highpass_2_4", hp_2_4);
	AddBool (payload, "base_15khz", base_15k);
	payload += "\"channels\":";
	payload += channels;
	return JsonOk(payload);
}

}  // namespace ATBridge
