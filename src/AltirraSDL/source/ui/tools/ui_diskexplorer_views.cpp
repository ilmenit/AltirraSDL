//	AltirraSDL - Disk Explorer format viewers
//
//	The token tables and file layouts here follow the public Atari BASIC and
//	Syn assembler file formats.  The implementation deliberately validates
//	every length and pointer before consuming a byte: a viewer must never be
//	able to crash the emulator when handed a damaged disk file.

#include <stdafx.h>
#include <algorithm>
#include <cstring>
#include <vector>

#include <at/atcpu/execstate.h>
#include <at/atcpu/history.h>
#include <at/atdebugger/target.h>
#include <vd2/system/VDString.h>
#include "../../../../Altirra/h/disasm.h"
#include "ui_diskexplorer_views.h"

namespace {

static uint16 ReadLE16(const unsigned char *p) {
	return (uint16)p[0] | ((uint16)p[1] << 8);
}

static void AppendAtasciiByte(VDStringA& out, unsigned char c) {
	if (c == 0x9B) {
		out += '\n';
		return;
	}
	c &= 0x7F;
	if (c < 0x20) {
		out += ' ';
	} else if (c == 0x7F) {
		out += ' ';
	} else {
		out += (char)c;
	}
}

static bool RangeOK(size_t pos, size_t count, size_t len) {
	return pos <= len && count <= len - pos;
}

static const char *const kBasicCommands[] = {
	"REM", "DATA", "INPUT", "COLOR", "LIST", "ENTER", "LET", "IF",
	"FOR", "NEXT", "GOTO", "GO TO", "GOSUB", "TRAP", "BYE", "CONT",
	"COM", "CLOSE", "CLR", "DEG", "DIM", "END", "NEW", "OPEN", "LOAD",
	"SAVE", "STATUS", "NOTE", "POINT", "XIO", "ON", "POKE", "PRINT",
	"RAD", "READ", "RESTORE", "RETURN", "RUN", "STOP", "POP", "?",
	"GET", "PUT", "GRAPHICS", "PLOT", "POSITION", "DOS", "DRAWTO",
	"SETCOLOR", "LOCATE", "SOUND", "LPRINT", "CSAVE", "CLOAD", "", "ERROR - "
};

static const char *const kBasicOperators[] = {
	"", "", ",", "$", ":", ";", "", "GOTO", "GOSUB", " TO ", " STEP ",
	" THEN ", "#", "<=", "<>", ">=", "<", ">", "=", "^", "*", "+", "-", "/",
	"NOT", " OR ", " AND ", "(", ")", "=", "=", "<=", "<>", ">=", "<", ">",
	"=", "+", "-", "(", "(", "(", "(", "(", ",", "STR$", "CHR$", "USR",
	"ASC", "VAL", "LEN", "ADR", "ATN", "COS", "PEEK", "SIN", "RND", "FRE",
	"EXP", "LOG", "CLOG", "SQR", "SGN", "ABS", "INT", "PADDLE", "STICK",
	"PTRING", "STRIG"
};

static bool BasicPointerLayout(const unsigned char *data, size_t len,
	uint16& varStart, uint16& varEnd, uint16& lineStart, uint16& lineEnd) {
	if (len < 14)
		return false;

	const uint16 lomem = ReadLE16(data + 0);
	const uint16 dvntRaw = ReadLE16(data + 2);
	const uint16 dvvtRaw = ReadLE16(data + 6);
	const uint16 dstRaw = ReadLE16(data + 8);
	const uint16 dendRaw = ReadLE16(data + 12);
	const int correction = (int)dvntRaw - (int)lomem - 14;
	const int dvnt = (int)dvntRaw - correction;
	const int dvvt = (int)dvvtRaw - correction;
	const int dst = (int)dstRaw - correction;
	const int dend = (int)dendRaw - correction;

	// Atari BASIC SAVE files in the wild exist in both forms: some contain
	// pointers already relative to the file, while others retain the address
	// space of the BASIC memory image.  Prefer the format used by the original
	// viewer, then fall back to the unambiguous DVNT -> byte 14 mapping.
	auto fits = [len](int p) { return p >= 0 && (size_t)p <= len; };
	if (fits(dvnt) && fits(dvvt) && fits(dst) && fits(dend)) {
		varStart = (uint16)dvnt;
		varEnd = (uint16)dvvt;
		lineStart = (uint16)dst;
		lineEnd = (uint16)dend;
	} else {
		auto remap = [dvnt](int p) -> int { return p - dvnt + 14; };
		const int v0 = remap(dvnt), v1 = remap(dvvt), l0 = remap(dst), l1 = remap(dend);
		if (v0 < 0 || v1 < v0 || l0 < 0 || l1 < l0 ||
			!fits(v1) || !fits(l1))
			return false;
		varStart = (uint16)v0;
		varEnd = (uint16)v1;
		lineStart = (uint16)l0;
		lineEnd = (uint16)l1;
	}

	return varStart <= varEnd && lineStart <= lineEnd && varEnd <= len && lineEnd <= len;
}

static VDStringA FormatBasicNumber(const unsigned char *data, size_t pos) {
	VDStringA s;
	const unsigned exp = data[pos];
	if (!exp) {
		s = "0";
		return s;
	}

	char digits[11] = {};
	for (int i = 0; i < 5; ++i) {
		const unsigned char b = data[pos + 1 + i];
		digits[i * 2] = (char)('0' + ((b >> 4) > 9 ? (b >> 4) - 10 : b >> 4));
		digits[i * 2 + 1] = (char)('0' + ((b & 15) > 9 ? (b & 15) - 10 : b & 15));
	}

	// Atari's exponent is a decimal-position code.  Keep the exact digits and
	// use scientific notation for the less common ranges; this avoids rounding
	// a value merely because it was viewed in the explorer.
	if (exp >= 63 && exp <= 68) {
		const int point = (int)(exp - 63) * 2;
		for (int i = 0; i < 10; ++i) {
			if (i == point) s += '.';
			s += digits[i];
		}
	} else {
		s = digits;
		s.append_sprintf("E%+d", exp > 64 ? (int)(exp - 64) * 2 : ((int)(exp - 64) * 2 + 1));
	}
	return s;
}

} // namespace

bool ATUIParseXEX(const unsigned char *data, size_t len,
	std::vector<ATUIXEXSegmentInfo>& segments, VDStringA& error) {
	segments.clear();
	error.clear();
	if (!data || len < 4) {
		error = "The file is shorter than an Atari executable header.";
		return false;
	}
	if (data[0] != 0xFF || data[1] != 0xFF) {
		error = "The file does not begin with the $FFFF Atari executable header.";
		return false;
	}

	size_t pos = 0;
	while (pos < len) {
		if (pos + 2 <= len && data[pos] == 0xFF && data[pos + 1] == 0xFF)
			pos += 2;
		if (pos == len)
			break;
		if (!RangeOK(pos, 4, len)) {
			error.sprintf("Truncated XEX segment header at offset %u.", (unsigned)pos);
			return false;
		}

		const unsigned start = ReadLE16(data + pos);
		const unsigned end = ReadLE16(data + pos + 2);
		pos += 4;
		if (end < start) {
			error.sprintf("Invalid XEX segment range $%04X-$%04X.", start, end);
			return false;
		}
		const size_t size = end - start + 1;
		if (!RangeOK(pos, size, len)) {
			error.sprintf("Truncated XEX segment $%04X-$%04X.", start, end);
			return false;
		}

		segments.push_back({start, end, pos});
		pos += size;
	}

	if (segments.empty()) {
		error = "The file contains no XEX segments.";
		return false;
	}
	return true;
}

bool ATUIDecodeAtariBasic(const unsigned char *data, size_t len, VDStringA& text, VDStringA& error) {
	text.clear();
	error.clear();
	if (!data || len < 14) {
		error = "The file is shorter than an Atari BASIC header.";
		return false;
	}

	uint16 varStart, varEnd, lineStart, lineEnd;
	if (!BasicPointerLayout(data, len, varStart, varEnd, lineStart, lineEnd) || varStart > varEnd) {
		error = "The Atari BASIC memory pointers are invalid.";
		return false;
	}

	std::vector<VDStringA> variables;
	VDStringA variable;
	for (size_t p = varStart; p < varEnd; ++p) {
		const unsigned char c = data[p];
		variable += (char)(c & 0x7F);
		if (c & 0x80) {
			variables.push_back(variable);
			variable.clear();
		}
	}
	if (!variable.empty())
		variables.push_back(variable);

	if (lineStart == lineEnd) {
		error = "The Atari BASIC file contains no program lines.";
		return false;
	}

	bool decodedLine = false;
	size_t pos = lineStart;
	while (pos < lineEnd) {
		if (!RangeOK(pos, 3, len) || pos + 3 > lineEnd) {
			error = "A BASIC line header extends past the end of the file.";
			return false;
		}
		const uint16 lineNo = ReadLE16(data + pos);
		if (lineNo == 0x8000)
			break;
		const unsigned lineLength = data[pos + 2];
		if (lineLength < 3 || lineLength > lineEnd - pos || !RangeOK(pos, lineLength, len)) {
			error = "A BASIC line has an invalid length.";
			return false;
		}

		text.append_sprintf("%u ", lineNo);
		const size_t end = pos + lineLength;
		size_t p = pos + 3;
		bool firstStatement = true;
		while (p < end) {
			if (!RangeOK(p, 2, end)) {
				error = "A BASIC statement header is truncated.";
				return false;
			}
			const unsigned statementLength = data[p];
			const unsigned statement = data[p + 1];
			if (statementLength < 2 || statementLength > end - p) {
				error = "A BASIC statement has an invalid length.";
				return false;
			}
			if (statement >= 0x38) {
				error = "The BASIC program contains an unknown statement token.";
				return false;
			}
			const size_t stmtEnd = p + statementLength;
			if (!firstStatement)
				text += ":";
			firstStatement = false;
			if (statement < 0x38 && statement != 0x36)
				text += kBasicCommands[statement];
			if (statement != 0x36)
				text += ' ';
			p += 2;

			if (statement == 0 || statement == 1) {
				while (p < stmtEnd && data[p] != 0x9B)
					AppendAtasciiByte(text, data[p++]);
			} else {
				while (p < stmtEnd) {
					const unsigned char token = data[p++];
					if (token >= 0x80) {
						const size_t vi = token - 0x80;
						if (vi < variables.size()) {
							VDStringA name = variables[vi];
							const char suffix = name.empty() ? 0 : name[name.size() - 1];
							if (suffix == '(' || suffix == '$') name.resize(name.size() - 1);
							text += name;
							if (suffix == '$') text += '$';
							else if (suffix == '(') text += "()";
						} else {
							text.append_sprintf("VAR%u", (unsigned)vi);
						}
					} else if (token >= 0x10 && token < 0x55) {
						text += kBasicOperators[token - 0x10];
					} else if (token == 0x0F) {
						if (p >= stmtEnd) { error = "A BASIC string constant is truncated."; return false; }
						const unsigned n = data[p++];
						if (n > stmtEnd - p) { error = "A BASIC string constant is truncated."; return false; }
						text += '"';
						for (unsigned i = 0; i < n; ++i) AppendAtasciiByte(text, data[p++]);
						text += '"';
					} else if (token == 0x0E) {
						if (!RangeOK(p, 6, stmtEnd)) { error = "A BASIC numeric constant is truncated."; return false; }
						text += FormatBasicNumber(data, p);
						p += 6;
					} else if (token == 0x9B) {
						break;
					}
				}
			}
			p = stmtEnd;
		}
		text += '\n';
		decodedLine = true;
		pos = end;
	}

	if (!decodedLine) {
		error = "The Atari BASIC end marker appears before the first line.";
		return false;
	}
	return true;
}

bool ATUIDecodeSynAssembler(const unsigned char *data, size_t len, VDStringA& text, VDStringA& error) {
	text.clear();
	error.clear();
	if (!data || len < 6) {
		error = "The file is shorter than a Syn assembler header.";
		return false;
	}

	// SynAssemblerLister's format is a six-byte preamble followed by a chain of
	// [line length][line number LE16][tokenized text...][NUL] records.
	size_t pos = 6;
	bool gotLine = false;
	while (pos < len) {
		const unsigned lineLength = data[pos++];
		if (!lineLength)
			break;
		if (lineLength < 3 || !RangeOK(pos, 2, len)) {
			error = "A Syn assembler line has an invalid header.";
			return false;
		}
		const uint16 lineNo = ReadLE16(data + pos);
		text.append_sprintf("%u ", lineNo);
		pos += 2;
		// The original Syn writer uses the NUL terminator as the reliable end
		// marker; the length byte is retained for format validation only.
		while (pos < len && data[pos] != 0) {
			const unsigned char c = data[pos++];
			if (c == 0x81) text += ' ';
			else if (c >= 0x20 && c < 0x7F) text += (char)c;
			else text.append_sprintf("<$%02X>", c);
		}
		if (pos < len && data[pos] == 0)
			++pos;
		else {
			error = "A Syn assembler line is missing its terminator.";
			return false;
		}
		text += '\n';
		gotLine = true;
	}

	if (!gotLine) {
		error = "The Syn assembler file contains no source lines.";
		return false;
	}
	return true;
}

namespace {
class MemoryDisasmTarget final : public IATDebugTarget {
public:
	MemoryDisasmTarget(const unsigned char *data, size_t len, uint16 base)
		: mpData(data), mLength(len), mBase(base) {}

	void *AsInterface(uint32) override { return nullptr; }
	const char *GetName() override { return "Disk Explorer buffer"; }
	ATDebugDisasmMode GetDisasmMode() override { return kATDebugDisasmMode_6502; }
	float GetDisplayCPUClock() const override { return 0.0f; }
	void GetExecState(ATCPUExecState& state) override { memset(&state, 0, sizeof state); }
	void SetExecState(const ATCPUExecState&) override {}
	sint32 GetTimeSkew() override { return 0; }
	uint8 ReadByte(uint32 address) override { return DebugReadByte(address); }
	void ReadMemory(uint32 address, void *dst, uint32 n) override { DebugReadMemory(address, dst, n); }
	uint8 DebugReadByte(uint32 address) override {
		if (address < mBase || (size_t)(address - mBase) >= mLength)
			return 0;
		return mpData[address - mBase];
	}
	void DebugReadMemory(uint32 address, void *dst, uint32 n) override {
		unsigned char *out = (unsigned char *)dst;
		for (uint32 i = 0; i < n; ++i) out[i] = DebugReadByte(address + i);
	}
	void WriteByte(uint32, uint8) override {}
	void WriteMemory(uint32, const void *, uint32) override {}

private:
	const unsigned char *mpData;
	size_t mLength;
	uint16 mBase;
};
}

bool ATUIDisassemble6502(const unsigned char *data, size_t len, unsigned baseAddress, VDStringA& text, VDStringA& error) {
	text.clear();
	error.clear();
	if (!data || !len) {
		error = "There is no data to disassemble.";
		return false;
	}
	if (baseAddress > 0xFFFF || len > 0x10000u - baseAddress) {
		error = "The disassembly range does not fit in the 6502 address space.";
		return false;
	}

	MemoryDisasmTarget target(data, len, (uint16)baseAddress);
	size_t offset = 0;
	while (offset < len) {
		ATCPUHistoryEntry h = {};
		const uint16 address = (uint16)(baseAddress + offset);
		ATDisassembleCaptureInsnContext(&target, address, 0, h);
		VDStringA line;
		const ATDisasmResult result = ATDisassembleInsn(line, &target, kATDebugDisasmMode_6502,
			h, false, false, true, true, false, false, false, true, true, false);
		text += line;
		text += '\n';
		unsigned step = (unsigned)(result.mNextPC - h.mPC);
		if (!step || step > 3)
			step = (unsigned)std::max(1, ATGetOpcodeLength(h.mOpcode[0]));
		if (step > len - offset)
			step = (unsigned)(len - offset);
		offset += step;
	}
	return true;
}
