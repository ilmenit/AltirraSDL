//	AltirraSDL - embedded MCP server: tool catalogue
//	See mcp_tools.h. Tool set and order follow docs/mcp-server-design.md
//	(phase v1).

#include <stdafx.h>

#include "mcp_tools.h"
#include "mcp_json.h"

#include "bridge_context.h"
#include "bridge_protocol.h"

#include "simulator.h"
#include "antic.h"
#include "cpu.h"
#include "debugger.h"

#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/text.h>
#include <vd2/system/VDString.h>
#include <vd2/vdjson/jsonreader.h>
#include <vd2/vdjson/jsonvalue.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// Edit > Paste Text path (ui_menus.cpp): converts text to Atari key
// presses with the same character table and {token} syntax.
void ATUIPasteTextDirect(const wchar_t *text, size_t len);

namespace ATMCP {

namespace {

// ---------------------------------------------------------------------------
// Bridge execution and reply shaping
// ---------------------------------------------------------------------------

struct BridgeReply {
	bool ok = false;
	std::string payload;    // JSON object without the "ok" member
	std::string error;      // error text when !ok
	std::string raw;        // full reply line (no trailing newline)
};

BridgeReply RunBridge(ToolEnv& env, const std::vector<std::string>& tokens) {
	BridgeReply r;
	r.raw = ATBridge::Execute(tokens, env.sim, env.ctx);
	while (!r.raw.empty() && (r.raw.back() == '\n' || r.raw.back() == '\r'))
		r.raw.pop_back();

	static const char kOkPrefix[] = "{\"ok\":true";
	const size_t okLen = sizeof kOkPrefix - 1;
	if (r.raw.compare(0, okLen, kOkPrefix) == 0) {
		r.ok = true;
		if (r.raw.size() > okLen && r.raw[okLen] == ',')
			r.payload = "{" + r.raw.substr(okLen + 1);
		else
			r.payload = "{}";
		return r;
	}

	VDJSONDocument doc;
	VDJSONReader reader;
	if (reader.Parse(r.raw.data(), r.raw.size(), doc)) {
		r.error = ValueToU8(Member(doc.Root(), "error"));
	}
	if (r.error.empty())
		r.error = r.raw.empty() ? std::string("bridge command failed") : r.raw;
	return r;
}

std::string TextContent(const std::string& text) {
	return JsonObject().Str("type", "text").Str("text", text).Build();
}

std::string ResultWith(const std::string& text, const std::string& structured) {
	JsonObject o;
	o.Raw("content", "[" + TextContent(text) + "]");
	if (!structured.empty())
		o.Raw("structuredContent", structured);
	o.Bool("isError", false);
	return o.Build();
}

// Structured result whose text mirrors the JSON (recommended by the
// spec for clients that ignore structuredContent).
std::string StructuredResult(const std::string& structured) {
	return ResultWith(structured, structured);
}

ToolOutcome Done(const std::string& result) {
	ToolOutcome o;
	o.result = result;
	return o;
}

ToolOutcome Fail(const std::string& message) {
	return Done(ToolErrorResult(message));
}

ToolOutcome FromReply(const BridgeReply& r) {
	if (!r.ok)
		return Fail(r.error);
	return Done(StructuredResult(r.payload));
}

// ---------------------------------------------------------------------------
// Argument helpers
// ---------------------------------------------------------------------------

bool ArgUint(const VDJSONValueRef& args, const char *name, bool required,
	uint32_t def, uint32_t minVal, uint32_t maxVal, uint32_t& out, std::string& err)
{
	const VDJSONValueRef v = Member(args, name);
	if (v.IsNull()) {
		if (required) {
			err = std::string("missing required argument '") + name + "'";
			return false;
		}
		out = def;
		return true;
	}

	uint64_t value = 0;
	if (v.IsInt()) {
		const sint64 i = v.AsInt64();
		if (i < 0) {
			err = std::string("'") + name + "' must not be negative";
			return false;
		}
		value = (uint64_t)i;
	} else if (v.IsReal()) {
		const double d = v.AsDouble();
		if (!(d >= 0) || d != std::floor(d) || d > 4294967295.0) {
			err = std::string("'") + name + "' must be a whole number";
			return false;
		}
		value = (uint64_t)d;
	} else if (v.IsString()) {
		// Lenient: accept "$600", "0x600" and decimal strings too.
		uint32_t parsed = 0;
		if (!ATBridge::ParseUint(ValueToU8(v), parsed)) {
			err = std::string("'") + name + "' is not a number";
			return false;
		}
		value = parsed;
	} else {
		err = std::string("'") + name + "' must be an integer";
		return false;
	}

	if (value < minVal || value > maxVal) {
		char buf[160];
		std::snprintf(buf, sizeof buf, "'%s' must be between %u and %u", name,
			(unsigned)minVal, (unsigned)maxVal);
		err = buf;
		return false;
	}

	out = (uint32_t)value;
	return true;
}

bool ArgString(const VDJSONValueRef& args, const char *name, bool required,
	std::string& out, std::string& err)
{
	const VDJSONValueRef v = Member(args, name);
	if (v.IsNull()) {
		if (required) {
			err = std::string("missing required argument '") + name + "'";
			return false;
		}
		out.clear();
		return true;
	}

	if (!v.IsString()) {
		err = std::string("'") + name + "' must be a string";
		return false;
	}

	out = ValueToU8(v);
	return true;
}

bool ArgBool(const VDJSONValueRef& args, const char *name, bool def,
	bool& out, std::string& err)
{
	const VDJSONValueRef v = Member(args, name);
	if (v.IsNull()) {
		out = def;
		return true;
	}

	if (!v.IsBool()) {
		err = std::string("'") + name + "' must be true or false";
		return false;
	}

	out = v.AsBool();
	return true;
}

bool PathExists(const std::string& path) {
	return !path.empty() && VDDoesPathExist(VDTextU8ToW(path.data(), (int)path.size()).c_str());
}

std::string HexDump(uint32_t address, const std::vector<uint8_t>& bytes, size_t maxBytes) {
	std::string out;
	const size_t n = bytes.size() < maxBytes ? bytes.size() : maxBytes;
	char buf[16];
	for (size_t i = 0; i < n; i += 16) {
		std::snprintf(buf, sizeof buf, "$%04X:", (unsigned)(address + i));
		out += buf;
		for (size_t j = i; j < i + 16 && j < n; ++j) {
			std::snprintf(buf, sizeof buf, " %02X", bytes[j]);
			out += buf;
		}
		out += '\n';
	}
	if (n < bytes.size()) {
		std::snprintf(buf, sizeof buf, "%u", (unsigned)(bytes.size() - n));
		out += "... ";
		out += buf;
		out += " more bytes in structuredContent.hex\n";
	}
	return out;
}

bool ParseHexBytes(const std::string& text, std::vector<uint8_t>& out, std::string& err,
	const char *argName = "data")
{
	out.clear();
	int nibbles = 0;
	uint8_t cur = 0;
	for (size_t i = 0; i < text.size(); ++i) {
		const char c = text[i];
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',')
			continue;

		// Allow an optional "$" or "0x" prefix on each byte.
		if (c == '$')
			continue;
		if (c == '0' && i + 1 < text.size() && (text[i + 1] == 'x' || text[i + 1] == 'X') && nibbles == 0) {
			++i;
			continue;
		}

		int v;
		if (c >= '0' && c <= '9') v = c - '0';
		else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
		else {
			err = std::string("'") + argName + "' must contain hex bytes, e.g. \"A9 00 8D C6 02\"";
			return false;
		}

		cur = (uint8_t)((cur << 4) | v);
		if (++nibbles == 2) {
			out.push_back(cur);
			cur = 0;
			nibbles = 0;
		}
	}

	if (nibbles) {
		err = std::string("'") + argName + "' has an odd number of hex digits";
		return false;
	}
	if (out.empty()) {
		err = std::string("'") + argName + "' is empty";
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Tool handlers
// ---------------------------------------------------------------------------

typedef ToolOutcome (*ToolFn)(const VDJSONValueRef& args, ToolEnv& env);

ToolOutcome ToolEmulatorStatus(const VDJSONValueRef&, ToolEnv& env) {
	const BridgeReply config = RunBridge(env, { "CONFIG" });
	if (!config.ok)
		return Fail(config.error);
	const BridgeReply regs = RunBridge(env, { "REGS" });
	if (!regs.ok)
		return Fail(regs.error);

	return Done(StructuredResult(JsonObject()
		.Bool("paused", env.sim.IsPaused())
		.Bool("running", env.sim.IsRunning())
		.Bool("warp", env.sim.IsTurboModeEnabled())
		.Raw("config", config.payload)
		.Raw("cpu", regs.payload)
		.Build()));
}

ToolOutcome ToolPause(const VDJSONValueRef&, ToolEnv& env) {
	return FromReply(RunBridge(env, { "PAUSE" }));
}

ToolOutcome ToolResume(const VDJSONValueRef&, ToolEnv& env) {
	return FromReply(RunBridge(env, { "RESUME" }));
}

ToolOutcome ToolWarpSpeed(const VDJSONValueRef& args, ToolEnv& env) {
	if (Member(args, "enabled").IsNull())
		return FromReply(RunBridge(env, { "WARP" }));

	std::string err;
	bool enabled = false;
	if (!ArgBool(args, "enabled", false, enabled, err))
		return Fail(err);
	return FromReply(RunBridge(env, { "WARP", enabled ? "on" : "off" }));
}

ToolOutcome ToolAdvanceFrames(const VDJSONValueRef& args, ToolEnv& env) {
	std::string err;
	uint32_t count = 1, timeout = 30;
	if (!ArgUint(args, "count", false, 1, 1, 1000000, count, err)
		|| !ArgUint(args, "timeout_seconds", false, 30, 1, 600, timeout, err))
		return Fail(err);

	const BridgeReply r = RunBridge(env, { "FRAME", std::to_string(count) });
	if (!r.ok)
		return Fail(r.error);

	ToolOutcome o;
	o.pending = true;
	o.pendingFrames = count;
	o.timeoutMs = timeout * 1000;
	return o;
}

ToolOutcome ToolColdReset(const VDJSONValueRef&, ToolEnv& env) {
	return FromReply(RunBridge(env, { "COLD_RESET" }));
}

ToolOutcome ToolWarmReset(const VDJSONValueRef&, ToolEnv& env) {
	return FromReply(RunBridge(env, { "WARM_RESET" }));
}

ToolOutcome ToolBootMedia(const VDJSONValueRef& args, ToolEnv& env) {
	std::string path, err;
	if (!ArgString(args, "path", true, path, err))
		return Fail(err);
	if (!PathExists(path))
		return Fail("file not found: " + path);

	return FromReply(RunBridge(env, { "BOOT", path }));
}

ToolOutcome ToolMount(const VDJSONValueRef& args, ToolEnv& env) {
	std::string path, err;
	uint32_t drive = 1;
	if (!ArgUint(args, "drive", true, 1, 1, 15, drive, err)
		|| !ArgString(args, "path", true, path, err))
		return Fail(err);
	if (!PathExists(path))
		return Fail("file not found: " + path);

	return FromReply(RunBridge(env, { "MOUNT", std::to_string(drive - 1), path }));
}

ToolOutcome ToolEject(const VDJSONValueRef& args, ToolEnv& env) {
	std::string err;
	bool cartridge = false;
	if (!ArgBool(args, "cartridge", false, cartridge, err))
		return Fail(err);

	const bool haveDrive = !Member(args, "drive").IsNull();
	if (cartridge == haveDrive)
		return Fail("specify exactly one of 'drive' or 'cartridge': true");

	if (cartridge)
		return FromReply(RunBridge(env, { "CART_EJECT" }));

	uint32_t drive = 1;
	if (!ArgUint(args, "drive", true, 1, 1, 15, drive, err))
		return Fail(err);
	return FromReply(RunBridge(env, { "EJECT", std::to_string(drive - 1) }));
}

ToolOutcome ToolReadMemory(const VDJSONValueRef& args, ToolEnv& env) {
	std::string err;
	uint32_t address = 0, length = 16;
	if (!ArgUint(args, "address", true, 0, 0, 0xFFFFFF, address, err)
		|| !ArgUint(args, "length", false, 16, 1, 65536, length, err))
		return Fail(err);

	const BridgeReply r = RunBridge(env, { "MEMDUMP", std::to_string(address), std::to_string(length) });
	if (!r.ok)
		return Fail(r.error);

	VDJSONDocument doc;
	VDJSONReader reader;
	std::vector<uint8_t> bytes;
	if (!reader.Parse(r.payload.data(), r.payload.size(), doc)
		|| !ATBridge::Base64Decode(ValueToU8(Member(doc.Root(), "data")), bytes))
		return Fail("unexpected MEMDUMP reply");

	std::string hex;
	hex.reserve(bytes.size() * 2);
	static const char kHex[] = "0123456789ABCDEF";
	for (uint8_t b : bytes) {
		hex += kHex[b >> 4];
		hex += kHex[b & 15];
	}

	const std::string structured = JsonObject()
		.Int("address", address)
		.Int("length", (int64_t)bytes.size())
		.Str("hex", hex)
		.Build();

	return Done(ResultWith(HexDump(address, bytes, 4096), structured));
}

ToolOutcome ToolWriteMemory(const VDJSONValueRef& args, ToolEnv& env) {
	std::string err, data;
	uint32_t address = 0;
	if (!ArgUint(args, "address", true, 0, 0, 0xFFFFFF, address, err)
		|| !ArgString(args, "data", true, data, err))
		return Fail(err);

	std::vector<uint8_t> bytes;
	if (!ParseHexBytes(data, bytes, err))
		return Fail(err);
	if (bytes.size() > 65536)
		return Fail("'data' is limited to 65536 bytes");

	const BridgeReply r = RunBridge(env, { "MEMLOAD", std::to_string(address),
		ATBridge::Base64Encode(bytes.data(), bytes.size()) });
	if (!r.ok)
		return Fail(r.error);

	return Done(StructuredResult(JsonObject()
		.Int("address", address)
		.Int("length", (int64_t)bytes.size())
		.Build()));
}

ToolOutcome ToolCpuState(const VDJSONValueRef&, ToolEnv& env) {
	return FromReply(RunBridge(env, { "REGS" }));
}

ToolOutcome ToolChipState(const VDJSONValueRef& args, ToolEnv& env) {
	std::string chip, err;
	if (!ArgString(args, "chip", true, chip, err))
		return Fail(err);

	const char *verb = nullptr;
	if (chip == "antic") verb = "ANTIC";
	else if (chip == "gtia") verb = "GTIA";
	else if (chip == "pokey") verb = "POKEY";
	else if (chip == "pia") verb = "PIA";
	else if (chip == "pmg") verb = "PMG";
	else if (chip == "audio") verb = "AUDIO_STATE";
	else if (chip == "all") verb = "HWSTATE";
	else
		return Fail("'chip' must be one of antic, gtia, pokey, pia, pmg, audio, all");

	return FromReply(RunBridge(env, { verb }));
}

ToolOutcome ToolDisplayList(const VDJSONValueRef&, ToolEnv& env) {
	return FromReply(RunBridge(env, { "DLIST" }));
}

ToolOutcome ToolScreenshot(const VDJSONValueRef&, ToolEnv& env) {
	const BridgeReply r = RunBridge(env, { "SCREENSHOT", "inline=true" });
	if (!r.ok)
		return Fail(r.error);

	VDJSONDocument doc;
	VDJSONReader reader;
	if (!reader.Parse(r.payload.data(), r.payload.size(), doc))
		return Fail("unexpected SCREENSHOT reply");

	const VDJSONValueRef root = doc.Root();
	const std::string data = ValueToU8(Member(root, "data"));
	if (data.empty())
		return Fail("unexpected SCREENSHOT reply");

	const sint64 w = Member(root, "width").AsInt64();
	const sint64 h = Member(root, "height").AsInt64();

	char text[96];
	std::snprintf(text, sizeof text, "Screenshot of the emulated display, %lldx%lld PNG.",
		(long long)w, (long long)h);

	const std::string image = JsonObject()
		.Str("type", "image")
		.Str("data", data)
		.Str("mimeType", "image/png")
		.Build();

	return Done(JsonObject()
		.Raw("content", "[" + image + "," + TextContent(text) + "]")
		.Bool("isError", false)
		.Build());
}

ToolOutcome ToolPressKey(const VDJSONValueRef& args, ToolEnv& env) {
	std::string key, err;
	bool shift = false, ctrl = false;
	if (!ArgString(args, "key", true, key, err)
		|| !ArgBool(args, "shift", false, shift, err)
		|| !ArgBool(args, "ctrl", false, ctrl, err))
		return Fail(err);

	std::vector<std::string> tokens { "KEY", key };
	if (shift) tokens.push_back("shift");
	if (ctrl) tokens.push_back("ctrl");
	return FromReply(RunBridge(env, tokens));
}

ToolOutcome ToolTypeText(const VDJSONValueRef& args, ToolEnv& env) {
	std::string text, err;
	if (!ArgString(args, "text", true, text, err))
		return Fail(err);
	if (text.empty())
		return Fail("'text' is empty");
	if (text.size() > 16384)
		return Fail("'text' is limited to 16384 bytes");

	const VDStringW wide = VDTextU8ToW(text.data(), (int)text.size());
	ATUIPasteTextDirect(wide.c_str(), wide.size());

	(void)env;
	return Done(StructuredResult(JsonObject()
		.Int("characters", (int64_t)wide.size())
		.Build()));
}

ToolOutcome ToolJoystick(const VDJSONValueRef& args, ToolEnv& env) {
	std::string direction, err;
	uint32_t port = 1;
	bool fire = false;
	if (!ArgUint(args, "port", false, 1, 1, 4, port, err)
		|| !ArgString(args, "direction", false, direction, err)
		|| !ArgBool(args, "fire", false, fire, err))
		return Fail(err);
	if (direction.empty())
		direction = "centre";

	std::vector<std::string> tokens { "JOY", std::to_string(port - 1), direction };
	if (fire)
		tokens.push_back("fire");
	return FromReply(RunBridge(env, tokens));
}

ToolOutcome ToolConsoleSwitches(const VDJSONValueRef& args, ToolEnv& env) {
	std::string err;
	bool start = false, select = false, option = false;
	if (!ArgBool(args, "start", false, start, err)
		|| !ArgBool(args, "select", false, select, err)
		|| !ArgBool(args, "option", false, option, err))
		return Fail(err);

	std::vector<std::string> tokens { "CONSOL" };
	if (start) tokens.push_back("start");
	if (select) tokens.push_back("select");
	if (option) tokens.push_back("option");
	return FromReply(RunBridge(env, tokens));
}

bool StateTarget(const VDJSONValueRef& args, std::string& token, std::string& err) {
	std::string slot, path;
	if (!ArgString(args, "slot", false, slot, err)
		|| !ArgString(args, "path", false, path, err))
		return false;

	if (slot.empty() == path.empty()) {
		err = "specify exactly one of 'slot' or 'path'";
		return false;
	}

	token = slot.empty() ? "path=" + path : "slot=" + slot;
	return true;
}

ToolOutcome ToolSaveState(const VDJSONValueRef& args, ToolEnv& env) {
	std::string target, err;
	if (!StateTarget(args, target, err))
		return Fail(err);
	return FromReply(RunBridge(env, { "STATE_SAVE", target }));
}

ToolOutcome ToolLoadState(const VDJSONValueRef& args, ToolEnv& env) {
	std::string target, err;
	if (!StateTarget(args, target, err))
		return Fail(err);
	if (target.compare(0, 5, "path=") == 0 && !PathExists(target.substr(5)))
		return Fail("file not found: " + target.substr(5));
	return FromReply(RunBridge(env, { "STATE_LOAD", target }));
}

ToolOutcome ToolListStates(const VDJSONValueRef&, ToolEnv& env) {
	return FromReply(RunBridge(env, { "STATE_LIST" }));
}

ToolOutcome ToolBridgeCommand(const VDJSONValueRef& args, ToolEnv& env) {
	std::string line, err;
	if (!ArgString(args, "command", true, line, err))
		return Fail(err);

	std::vector<std::string> tokens;
	if (!ATBridge::TokenizeCommand(line, tokens, err))
		return Fail("invalid command syntax: " + err);
	if (tokens.empty())
		return Fail("empty command");

	std::string& verb = tokens[0];
	for (char& c : verb) {
		if (c >= 'a' && c <= 'z')
			c = (char)(c - 32);
	}

	// QUIT is never exposed: closing the emulator is the user's call.
	// HELLO is the socket front end's authentication.
	if (verb == "QUIT" || verb == "HELLO")
		return Fail(verb + " is not available through MCP");

	const bool async = verb == "CYCLES" || verb == "SCANLINES" || verb == "BOOT_BARE";
	if (async && ATBridge::IsAsyncBusy() && !ATBridge::IsAsyncOwnedBy(env.ctx))
		return Fail("another client's step or bare boot is in progress");

	const BridgeReply r = RunBridge(env, tokens);

	// Steps and BOOT_BARE complete later: wait for them like step and
	// boot_bare do.
	if (async && ATBridge::IsAsyncOwnedBy(env.ctx) && (r.ok || r.raw.empty())) {
		ToolOutcome o;
		o.pending = true;
		o.async = true;
		o.timeoutMs = 60 * 1000;
		return o;
	}

	if (!r.ok)
		return Fail(r.error);

	// FRAME arms this context's frame gate: wait for it like
	// advance_frames does.
	if (verb == "FRAME") {
		uint32_t n = 1;
		if (tokens.size() >= 2)
			ATBridge::ParseUint(tokens[1], n);
		ToolOutcome o;
		o.pending = true;
		o.pendingFrames = n;
		o.timeoutMs = 30 * 1000;
		return o;
	}

	return Done(StructuredResult(r.payload));
}

// ---------------------------------------------------------------------------
// Debugger tools (phase v1.1)
// ---------------------------------------------------------------------------

// Altirra's expression language uses a single '=' for equality; "==" is
// never valid there, so accept the C spelling agents tend to write.
std::string NormalizeExpression(std::string expr) {
	for (size_t pos = expr.find("=="); pos != std::string::npos; pos = expr.find("==", pos))
		expr.erase(pos, 1);
	return expr;
}

std::string HexAddr(uint32_t addr) {
	char buf[16];
	std::snprintf(buf, sizeof buf, "$%04X", (unsigned)addr);
	return buf;
}

ToolOutcome ToolDisassemble(const VDJSONValueRef& args, ToolEnv& env) {
	std::string err;
	uint32_t address = 0, count = 16;
	if (!ArgUint(args, "address", true, 0, 0, 0xFFFF, address, err)
		|| !ArgUint(args, "count", false, 16, 1, 1024, count, err))
		return Fail(err);

	const BridgeReply r = RunBridge(env, { "DISASM", HexAddr(address), std::to_string(count) });
	if (!r.ok)
		return Fail(r.error);

	// Text: one listing line per instruction ("$E477  4C DC EE  COLDSV jmp ...").
	std::string listing;
	VDJSONDocument doc;
	VDJSONReader reader;
	if (reader.Parse(r.payload.data(), r.payload.size(), doc)) {
		for (const VDJSONValueRef& insn : Member(doc.Root(), "insns").AsArray()) {
			std::string bytes;
			for (const VDJSONValueRef& b : Member(insn, "bytes").AsArray()) {
				if (!bytes.empty())
					bytes += ' ';
				bytes += ValueToU8(b);
			}
			for (char& c : bytes) {
				if (c >= 'a' && c <= 'f')
					c = (char)(c - 32);
			}
			std::string addr = ValueToU8(Member(insn, "addr"));
			for (char& c : addr) {
				if (c >= 'a' && c <= 'f')
					c = (char)(c - 32);
			}
			listing += addr;
			listing += "  ";
			listing += bytes;
			listing.append(bytes.size() < 12 ? 12 - bytes.size() : 1, ' ');
			listing += ValueToU8(Member(insn, "text"));
			listing += '\n';
		}
	}

	if (listing.empty())
		listing = r.payload;

	return Done(ResultWith(listing, r.payload));
}

ToolOutcome ToolEvaluate(const VDJSONValueRef& args, ToolEnv& env) {
	std::string expr, err;
	if (!ArgString(args, "expression", true, expr, err))
		return Fail(err);
	if (expr.empty())
		return Fail("'expression' is empty");

	const BridgeReply r = RunBridge(env, { "EVAL", NormalizeExpression(expr) });
	if (!r.ok)
		return Fail(r.error);
	return Done(StructuredResult(r.payload));
}

ToolOutcome ToolSetBreakpoint(const VDJSONValueRef& args, ToolEnv& env) {
	std::string condition, access, err;
	uint32_t address = 0;
	if (!ArgUint(args, "address", true, 0, 0, 0xFFFF, address, err)
		|| !ArgString(args, "condition", false, condition, err)
		|| !ArgString(args, "access", false, access, err))
		return Fail(err);

	if (access.empty() || access == "execute") {
		std::vector<std::string> tokens { "BP_SET", HexAddr(address) };
		if (!condition.empty())
			tokens.push_back("condition=" + NormalizeExpression(condition));
		return FromReply(RunBridge(env, tokens));
	}

	const char *mode = nullptr;
	if (access == "read") mode = "r";
	else if (access == "write") mode = "w";
	else if (access == "readwrite") mode = "rw";
	else
		return Fail("'access' must be one of execute, read, write, readwrite");

	if (!condition.empty())
		return Fail("'condition' is only supported for execute breakpoints");

	return FromReply(RunBridge(env, { "WATCH_SET", HexAddr(address), std::string("mode=") + mode }));
}

ToolOutcome ToolClearBreakpoint(const VDJSONValueRef& args, ToolEnv& env) {
	std::string err;
	bool all = false;
	if (!ArgBool(args, "all", false, all, err))
		return Fail(err);

	const bool haveId = !Member(args, "id").IsNull();
	if (all == haveId)
		return Fail("specify exactly one of 'id' or 'all': true");

	if (all)
		return FromReply(RunBridge(env, { "BP_CLEAR_ALL" }));

	uint32_t id = 0;
	if (!ArgUint(args, "id", true, 0, 0, 0xFFFFFFFFu, id, err))
		return Fail(err);
	return FromReply(RunBridge(env, { "BP_CLEAR", std::to_string(id) }));
}

ToolOutcome ToolListBreakpoints(const VDJSONValueRef&, ToolEnv& env) {
	return FromReply(RunBridge(env, { "BP_LIST" }));
}

ToolOutcome ToolHistory(const VDJSONValueRef& args, ToolEnv& env) {
	std::string err;
	uint32_t count = 64;
	if (!ArgUint(args, "count", false, 64, 1, 4096, count, err))
		return Fail(err);

	const BridgeReply r = RunBridge(env, { "HISTORY", std::to_string(count) });
	if (!r.ok) {
		if (r.error.find("no instructions recorded") != std::string::npos)
			return Fail(r.error + ". CPU history recording may be off: enable it with "
				"bridge_command \"CONFIG history true\", run some frames, then call history again.");
		return Fail(r.error);
	}
	return Done(StructuredResult(r.payload));
}

ToolOutcome ToolCallStack(const VDJSONValueRef& args, ToolEnv& env) {
	std::string err;
	uint32_t count = 32;
	if (!ArgUint(args, "count", false, 32, 1, 256, count, err))
		return Fail(err);
	return FromReply(RunBridge(env, { "CALLSTACK", std::to_string(count) }));
}

ToolOutcome ToolSearchMemory(const VDJSONValueRef& args, ToolEnv& env) {
	std::string pattern, err;
	uint32_t start = 0, end = 0x10000;
	if (!ArgString(args, "pattern", true, pattern, err)
		|| !ArgUint(args, "start", false, 0, 0, 0xFFFF, start, err)
		|| !ArgUint(args, "end", false, 0x10000, 1, 0x10000, end, err))
		return Fail(err);
	if (start >= end)
		return Fail("'start' must be below 'end'");

	std::vector<uint8_t> bytes;
	if (!ParseHexBytes(pattern, bytes, err, "pattern"))
		return Fail(err);

	std::string hex;
	static const char kHex[] = "0123456789abcdef";
	for (uint8_t b : bytes) {
		hex += kHex[b >> 4];
		hex += kHex[b & 15];
	}

	char range[2][16];
	std::snprintf(range[0], sizeof range[0], "start=$%x", (unsigned)start);
	std::snprintf(range[1], sizeof range[1], "end=$%x", (unsigned)end);
	return FromReply(RunBridge(env, { "MEMSEARCH", hex, range[0], range[1] }));
}

ToolOutcome ToolSymbols(const VDJSONValueRef& args, ToolEnv& env) {
	std::string action, err;
	if (!ArgString(args, "action", true, action, err))
		return Fail(err);

	if (action == "load") {
		std::string path;
		if (!ArgString(args, "path", true, path, err))
			return Fail(err);
		if (!PathExists(path))
			return Fail("file not found: " + path);
		return FromReply(RunBridge(env, { "SYM_LOAD", path }));
	}

	if (action == "resolve") {
		std::string name;
		if (!ArgString(args, "name", true, name, err))
			return Fail(err);
		if (name.empty())
			return Fail("'name' is empty");
		return FromReply(RunBridge(env, { "SYM_RESOLVE", name }));
	}

	if (action == "lookup") {
		std::string flags;
		uint32_t address = 0;
		if (!ArgUint(args, "address", true, 0, 0, 0xFFFF, address, err)
			|| !ArgString(args, "flags", false, flags, err))
			return Fail(err);
		if (flags.empty())
			flags = "rwx";
		return FromReply(RunBridge(env, { "SYM_LOOKUP", HexAddr(address), "flags=" + flags }));
	}

	if (action == "unload") {
		uint32_t module = 0;
		if (!ArgUint(args, "module_id", true, 0, 0, 0xFFFFFFFFu, module, err))
			return Fail(err);
		return FromReply(RunBridge(env, { "SYM_UNLOAD", std::to_string(module) }));
	}

	if (action == "clear_all")
		return FromReply(RunBridge(env, { "SYM_CLEAR_ALL" }));

	return Fail("'action' must be one of load, lookup, resolve, unload, clear_all");
}

ToolOutcome ToolProfiler(const VDJSONValueRef& args, ToolEnv& env) {
	std::string action, mode, err;
	uint32_t top = 32;
	if (!ArgString(args, "action", true, action, err)
		|| !ArgString(args, "mode", false, mode, err)
		|| !ArgUint(args, "top", false, 32, 1, 4096, top, err))
		return Fail(err);

	if (action == "start") {
		if (mode.empty())
			mode = "insns";
		return FromReply(RunBridge(env, { "PROFILE_START", "mode=" + mode }));
	}
	if (action == "stop")
		return FromReply(RunBridge(env, { "PROFILE_STOP" }));
	if (action == "status")
		return FromReply(RunBridge(env, { "PROFILE_STATUS" }));
	if (action == "dump")
		return FromReply(RunBridge(env, { "PROFILE_DUMP", "top=" + std::to_string(top) }));
	if (action == "dump_tree")
		return FromReply(RunBridge(env, { "PROFILE_DUMP_TREE" }));

	return Fail("'action' must be one of start, stop, status, dump, dump_tree");
}

// ---------------------------------------------------------------------------
// Remaining bridge commands
// ---------------------------------------------------------------------------

// Value of a JSON scalar as a bridge token ("true"/"false", decimal, text).
bool ScalarToken(const VDJSONValueRef& v, std::string& out) {
	if (v.IsString()) { out = ValueToU8(v); return true; }
	if (v.IsBool())   { out = v.AsBool() ? "true" : "false"; return true; }
	if (v.IsInt())    { out = std::to_string((long long)v.AsInt64()); return true; }
	if (v.IsReal())   { out = SerializeValue(v); return true; }
	return false;
}

ToolOutcome ToolConfigure(const VDJSONValueRef& args, ToolEnv& env) {
	std::string key, err;
	if (!ArgString(args, "key", false, key, err))
		return Fail(err);

	std::vector<std::string> tokens { "CONFIG" };
	if (!key.empty())
		tokens.push_back(key);

	const VDJSONValueRef value = Member(args, "value");
	if (!value.IsNull()) {
		if (key.empty())
			return Fail("'value' needs a 'key'");
		std::string v;
		if (!ScalarToken(value, v))
			return Fail("'value' must be a string, number or boolean");
		tokens.push_back(v);
	}

	return FromReply(RunBridge(env, tokens));
}

ToolOutcome ToolDevices(const VDJSONValueRef& args, ToolEnv& env) {
	std::string action, device, parent, err;
	if (!ArgString(args, "action", true, action, err)
		|| !ArgString(args, "device", false, device, err)
		|| !ArgString(args, "parent", false, parent, err))
		return Fail(err);

	if (action == "list")
		return FromReply(RunBridge(env, { "DEVICE_LIST" }));
	if (action == "clear")
		return FromReply(RunBridge(env, { "DEVICE_CLEAR" }));

	if (device.empty())
		return Fail("'device' is required for action '" + action + "'");

	if (action == "get")
		return FromReply(RunBridge(env, { "DEVICE_GET", device }));
	if (action == "remove")
		return FromReply(RunBridge(env, { "DEVICE_REMOVE", device }));

	// Device settings become key=value tokens.
	std::vector<std::string> settings;
	const VDJSONValueRef settingsVal = Member(args, "settings");
	if (!settingsVal.IsNull()) {
		if (!settingsVal.IsObject())
			return Fail("'settings' must be an object of name: value pairs");
		for (const VDJSONMemberRef& m : settingsVal.AsObject()) {
			std::string v;
			if (!ScalarToken(m.GetValue(), v))
				return Fail("device settings must be strings, numbers or booleans");
			const VDStringA name = VDTextWToU8(VDStringSpanW(m.GetName()));
			settings.push_back(std::string(name.c_str()) + "=" + v);
		}
	}

	if (action == "add") {
		std::vector<std::string> tokens { "DEVICE_ADD", device, "parent=" + (parent.empty() ? std::string("/") : parent) };
		tokens.insert(tokens.end(), settings.begin(), settings.end());
		return FromReply(RunBridge(env, tokens));
	}

	if (action == "set") {
		bool enabled = true;
		if (!ArgBool(args, "enabled", true, enabled, err))
			return Fail(err);
		std::vector<std::string> tokens { "DEVICE_SET", device, enabled ? "on" : "off" };
		tokens.insert(tokens.end(), settings.begin(), settings.end());
		return FromReply(RunBridge(env, tokens));
	}

	return Fail("'action' must be one of list, get, add, set, remove, clear");
}

ToolOutcome ToolFresh(const VDJSONValueRef& args, ToolEnv& env) {
	std::vector<std::string> tokens { "FRESH" };
	const VDJSONValueRef keep = Member(args, "keep");
	if (!keep.IsNull()) {
		if (!keep.IsArray())
			return Fail("'keep' must be an array");
		std::string list;
		for (const VDJSONValueRef& k : keep.AsArray()) {
			const std::string item = ValueToU8(k);
			if (item != "bps" && item != "symbols" && item != "profile" && item != "verifier"
				&& item != "media" && item != "reset")
				return Fail("'keep' items must be bps, symbols, profile, verifier, media or reset");
			if (!list.empty())
				list += ',';
			list += item;
		}
		if (!list.empty())
			tokens.push_back("keep=" + list);
	}
	return FromReply(RunBridge(env, tokens));
}

ToolOutcome ToolWriteHardwareRegister(const VDJSONValueRef& args, ToolEnv& env) {
	std::string err;
	uint32_t address = 0, value = 0;
	if (!ArgUint(args, "address", true, 0, 0, 0xFFFF, address, err)
		|| !ArgUint(args, "value", true, 0, 0, 255, value, err))
		return Fail(err);
	return FromReply(RunBridge(env, { "HWPOKE", HexAddr(address), std::to_string(value) }));
}

ToolOutcome ToolMemoryMap(const VDJSONValueRef& args, ToolEnv& env) {
	std::string view, err;
	if (!ArgString(args, "view", false, view, err))
		return Fail(err);
	if (view.empty() || view == "regions")
		return FromReply(RunBridge(env, { "MEMMAP" }));
	if (view == "banks")
		return FromReply(RunBridge(env, { "BANK_INFO" }));
	if (view == "cartridge")
		return FromReply(RunBridge(env, { "CART_INFO" }));
	return Fail("'view' must be one of regions, banks, cartridge");
}

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& data, std::string& err) {
	try {
		VDFile f(VDTextU8ToW(path.data(), (int)path.size()).c_str());
		const sint64 size = f.size();
		if (size < 0 || size > 1024 * 1024) {
			err = "file is too large";
			return false;
		}
		data.resize((size_t)size);
		if (size)
			f.read(data.data(), (long)size);
		return true;
	} catch (const MyError& e) {
		err = e.c_str();
		return false;
	}
}

ToolOutcome ToolPalette(const VDJSONValueRef& args, ToolEnv& env) {
	std::string action, path, err;
	if (!ArgString(args, "action", true, action, err)
		|| !ArgString(args, "path", false, path, err))
		return Fail(err);

	if (action == "get")
		return FromReply(RunBridge(env, { "PALETTE" }));
	if (action == "reset")
		return FromReply(RunBridge(env, { "PALETTE_RESET" }));

	if (action == "load") {
		if (path.empty())
			return Fail("'path' is required for action 'load'");
		std::vector<uint8_t> data;
		if (!ReadWholeFile(path, data, err))
			return Fail("cannot read " + path + ": " + err);
		// Adobe .act palettes are 768 bytes of RGB, optionally followed
		// by a 4-byte colour count / transparency trailer.
		if (data.size() < 768)
			return Fail("palette file must contain at least 768 bytes (256 RGB triples)");
		return FromReply(RunBridge(env, { "PALETTE_LOAD_ACT", ATBridge::Base64Encode(data.data(), 768) }));
	}

	return Fail("'action' must be one of get, load, reset");
}

ToolOutcome ToolRawScreen(const VDJSONValueRef& args, ToolEnv& env) {
	std::string path, err;
	if (!ArgString(args, "path", false, path, err))
		return Fail(err);
	if (!path.empty())
		return FromReply(RunBridge(env, { "RAWSCREEN", "path=" + path }));
	return FromReply(RunBridge(env, { "RAWSCREEN", "inline=true" }));
}

ToolOutcome ToolHoldKey(const VDJSONValueRef& args, ToolEnv& env) {
	std::string key, err;
	bool down = true, shift = false, ctrl = false;
	if (!ArgString(args, "key", true, key, err)
		|| !ArgBool(args, "down", true, down, err)
		|| !ArgBool(args, "shift", false, shift, err)
		|| !ArgBool(args, "ctrl", false, ctrl, err))
		return Fail(err);

	std::vector<std::string> tokens { "KEYRAW", key, down ? "down" : "up" };
	if (shift) tokens.push_back("shift");
	if (ctrl) tokens.push_back("ctrl");
	return FromReply(RunBridge(env, tokens));
}

ToolOutcome ToolStep(const VDJSONValueRef& args, ToolEnv& env) {
	std::string unit, err;
	uint32_t count = 1, timeout = 30;
	if (!ArgString(args, "unit", false, unit, err)
		|| !ArgUint(args, "count", false, 1, 1, 100000000, count, err)
		|| !ArgUint(args, "timeout_seconds", false, 30, 1, 600, timeout, err))
		return Fail(err);

	if (unit.empty())
		unit = "cycles";
	if (unit != "cycles" && unit != "scanlines")
		return Fail("'unit' must be cycles or scanlines");

	if (ATBridge::IsAsyncBusy() && !ATBridge::IsAsyncOwnedBy(env.ctx))
		return Fail("another client's step or bare boot is in progress");

	const BridgeReply r = RunBridge(env, { unit == "cycles" ? "CYCLES" : "SCANLINES", std::to_string(count) });
	if (!r.ok)
		return Fail(r.error);

	ToolOutcome o;
	o.pending = true;
	o.async = true;
	o.timeoutMs = timeout * 1000;
	return o;
}

ToolOutcome ToolClock(const VDJSONValueRef&, ToolEnv& env) {
	return FromReply(RunBridge(env, { "CLOCK" }));
}

ToolOutcome ToolPokeyTrace(const VDJSONValueRef& args, ToolEnv& env) {
	std::string action, err;
	uint32_t capacity = 65536, maxEvents = 4096;
	if (!ArgString(args, "action", true, action, err)
		|| !ArgUint(args, "capacity", false, 65536, 1, 1000000, capacity, err)
		|| !ArgUint(args, "max_events", false, 4096, 1, 1000000, maxEvents, err))
		return Fail(err);

	if (action == "start")
		return FromReply(RunBridge(env, { "POKEY_TRACE", "start", std::to_string(capacity) }));
	if (action == "read")
		return FromReply(RunBridge(env, { "POKEY_TRACE", "read", std::to_string(maxEvents) }));
	if (action == "stop" || action == "status")
		return FromReply(RunBridge(env, { "POKEY_TRACE", action }));
	return Fail("'action' must be one of start, read, stop, status");
}

ToolOutcome ToolBootBare(const VDJSONValueRef&, ToolEnv& env) {
	if (ATBridge::IsAsyncBusy())
		return Fail("a step or bare boot is already in progress");

	const BridgeReply r = RunBridge(env, { "BOOT_BARE" });
	if (!r.raw.empty())
		return r.ok ? Done(StructuredResult(r.payload)) : Fail(r.error);

	// The reply is deferred until the stub reaches its park loop.
	ToolOutcome o;
	o.pending = true;
	o.async = true;
	o.timeoutMs = 60 * 1000;
	return o;
}

ToolOutcome ToolDeleteState(const VDJSONValueRef& args, ToolEnv& env) {
	std::string slot, err;
	bool all = false;
	if (!ArgString(args, "slot", false, slot, err) || !ArgBool(args, "all", false, all, err))
		return Fail(err);
	if (all == !slot.empty())
		return Fail("specify exactly one of 'slot' or 'all': true");
	return FromReply(RunBridge(env, { "STATE_DROP", all ? std::string("all=true") : "slot=" + slot }));
}

ToolOutcome ToolVerifier(const VDJSONValueRef& args, ToolEnv& env) {
	std::string action, err;
	if (!ArgString(args, "action", true, action, err))
		return Fail(err);

	if (action == "status")
		return FromReply(RunBridge(env, { "VERIFIER_STATUS" }));
	if (action == "disable")
		return FromReply(RunBridge(env, { "VERIFIER_SET", "flags=off" }));
	if (action == "set") {
		uint32_t flags = 0;
		if (!ArgUint(args, "flags", true, 0, 0, 0xFFFFFFFFu, flags, err))
			return Fail(err);
		char buf[24];
		std::snprintf(buf, sizeof buf, "flags=0x%x", (unsigned)flags);
		return FromReply(RunBridge(env, { "VERIFIER_SET", buf }));
	}
	return Fail("'action' must be one of status, set, disable");
}

#ifdef ALTIRRA_BRIDGE_AUDIO_REC
ToolOutcome ToolAudioRecord(const VDJSONValueRef& args, ToolEnv& env) {
	std::string action, path, err;
	bool raw = false, stereo = false;
	if (!ArgString(args, "action", true, action, err)
		|| !ArgString(args, "path", false, path, err)
		|| !ArgBool(args, "raw", false, raw, err)
		|| !ArgBool(args, "stereo", false, stereo, err))
		return Fail(err);

	if (action == "stop" || action == "status")
		return FromReply(RunBridge(env, { "AUDIO_RECORD", action }));
	if (action == "start") {
		if (path.empty())
			return Fail("'path' is required for action 'start'");
		std::vector<std::string> tokens { "AUDIO_RECORD", "start", "path=" + path };
		if (raw) tokens.push_back("raw");
		if (stereo) tokens.push_back("stereo");
		return FromReply(RunBridge(env, tokens));
	}
	return Fail("'action' must be one of start, stop, status");
}
#endif

// ---------------------------------------------------------------------------
// Tool table (fixed order; tools/list returns it as-is)
// ---------------------------------------------------------------------------

struct ToolDef {
	const char *name;
	const char *title;
	const char *description;
	const char *inputSchema;
	bool readOnly;
	bool destructive;
	bool idempotent;
	ToolFn fn;
};

#define MCP_NO_ARGS "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}"

const ToolDef kTools[] = {
	{ "emulator_status", "Emulator status",
		"Report whether the emulator is paused or in warp speed, the machine "
		"configuration (hardware model, video standard, memory, BASIC, "
		"kernel, attached devices) and the CPU registers.",
		MCP_NO_ARGS, true, false, true, ToolEmulatorStatus },

	{ "pause", "Pause emulation",
		"Pause the emulated machine. Inspect memory and registers while paused "
		"so they don't change under you.",
		MCP_NO_ARGS, false, false, true, ToolPause },

	{ "resume", "Resume emulation",
		"Resume free-running emulation at normal speed.",
		MCP_NO_ARGS, false, false, true, ToolResume },

	{ "warp_speed", "Warp speed",
		"Read or set warp speed (System > Warp Speed): run emulation as fast "
		"as the host allows instead of at the PAL/NTSC rate. Emulated timing "
		"is unchanged, so advance_frames and step just finish sooner. Omit "
		"'enabled' to read. Returns 'warp' (the toggle) and 'active' (warp "
		"in effect, including while the warp key is held). Unavailable "
		"during netplay.",
		"{\"type\":\"object\",\"properties\":{"
			"\"enabled\":{\"type\":\"boolean\",\"description\":\"true to enable, false to disable; omit to read.\"}"
		"},\"additionalProperties\":false}",
		false, false, true, ToolWarpSpeed },

	{ "advance_frames", "Advance frames",
		"Run the emulator for a number of video frames (about 60 per second on "
		"NTSC, 50 on PAL), then pause. The call returns only after the frames "
		"have run. Use this after boot_media (120-300 frames) and between "
		"input actions so the Atari program can react.",
		"{\"type\":\"object\",\"properties\":{"
			"\"count\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":1000000,\"default\":1,"
				"\"description\":\"Frames to run.\"},"
			"\"timeout_seconds\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":600,\"default\":30,"
				"\"description\":\"Give up and pause after this many seconds of wall-clock time.\"}"
		"},\"additionalProperties\":false}",
		false, false, false, ToolAdvanceFrames },

	{ "cold_reset", "Cold reset",
		"Power-cycle the emulated machine (cold start). The paused/running "
		"state is kept.",
		MCP_NO_ARGS, false, true, false, ToolColdReset },

	{ "warm_reset", "Warm reset",
		"Press the RESET key (warm start). The paused/running state is kept.",
		MCP_NO_ARGS, false, true, false, ToolWarmReset },

	{ "boot_media", "Boot media",
		"Boot a program or disk/tape/cartridge image (XEX, COM, ATR, ATX, XFD, "
		"CAS, CAR, BIN, ROM) from a path on this computer, replacing the "
		"current media and cold-starting the machine. Returns once the image "
		"is accepted; then call advance_frames (typically 120-300) before "
		"inspecting the screen or memory.",
		"{\"type\":\"object\",\"properties\":{"
			"\"path\":{\"type\":\"string\",\"description\":\"Absolute path of the file to boot.\"}"
		"},\"required\":[\"path\"],\"additionalProperties\":false}",
		false, true, false, ToolBootMedia },

	{ "mount", "Mount disk",
		"Insert a disk image into drive D1:-D15: without rebooting.",
		"{\"type\":\"object\",\"properties\":{"
			"\"drive\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":15,\"description\":\"Drive number (1 = D1:).\"},"
			"\"path\":{\"type\":\"string\",\"description\":\"Absolute path of the disk image.\"}"
		"},\"required\":[\"drive\",\"path\"],\"additionalProperties\":false}",
		false, true, false, ToolMount },

	{ "eject", "Eject media",
		"Eject the disk in a drive, or the cartridge.",
		"{\"type\":\"object\",\"properties\":{"
			"\"drive\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":15,\"description\":\"Drive number (1 = D1:).\"},"
			"\"cartridge\":{\"type\":\"boolean\",\"description\":\"Eject the cartridge instead of a disk.\"}"
		"},\"additionalProperties\":false}",
		false, true, false, ToolEject },

	{ "read_memory", "Read memory",
		"Read bytes from the CPU address space without side effects (reading "
		"hardware registers at $D000-$D7FF does not trigger them). Addresses "
		"are integers; convert Atari hex notation ($0600) to decimal (1536).",
		"{\"type\":\"object\",\"properties\":{"
			"\"address\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":16777215,\"description\":\"Start address.\"},"
			"\"length\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":65536,\"default\":16,\"description\":\"Number of bytes.\"}"
		"},\"required\":[\"address\"],\"additionalProperties\":false}",
		true, false, true, ToolReadMemory },

	{ "write_memory", "Write memory",
		"Write bytes into the CPU address space (RAM; ROM areas are ignored by "
		"the hardware).",
		"{\"type\":\"object\",\"properties\":{"
			"\"address\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":16777215,\"description\":\"Start address.\"},"
			"\"data\":{\"type\":\"string\",\"description\":\"Hex bytes, e.g. \\\"A9 00 8D C6 02\\\".\"}"
		"},\"required\":[\"address\",\"data\"],\"additionalProperties\":false}",
		false, true, false, ToolWriteMemory },

	{ "cpu_state", "CPU state",
		"CPU registers (PC, A, X, Y, S, P and flags) and cycle counters.",
		MCP_NO_ARGS, true, false, true, ToolCpuState },

	{ "chip_state", "Chip state",
		"Register state of a custom chip: ANTIC (display), GTIA (graphics, "
		"colours, collisions), POKEY (sound, keyboard, timers), PIA (ports), "
		"decoded player/missile graphics (pmg), decoded POKEY audio channels "
		"with frequencies (audio), or all registers.",
		"{\"type\":\"object\",\"properties\":{"
			"\"chip\":{\"type\":\"string\",\"enum\":[\"antic\",\"gtia\",\"pokey\",\"pia\",\"pmg\",\"audio\",\"all\"]}"
		"},\"required\":[\"chip\"],\"additionalProperties\":false}",
		true, false, true, ToolChipState },

	{ "display_list", "Display list",
		"Decode the current ANTIC display list (screen layout by mode line).",
		MCP_NO_ARGS, true, false, true, ToolDisplayList },

	{ "screenshot", "Screenshot",
		"Capture the current emulated display as a PNG image.",
		MCP_NO_ARGS, true, false, true, ToolScreenshot },

	{ "press_key", "Press key",
		"Press and release one Atari key. Names: A-Z, 0-9, RETURN, SPACE, ESC, "
		"TAB, DELETE, MINUS, EQUALS, COMMA, PERIOD, SLASH, SEMICOLON, PLUS, "
		"ASTERISK, LESS, GREATER, CAPS, HELP, INVERSE, F1-F4, LEFT, RIGHT, UP, "
		"DOWN. Advance a few frames afterwards so the program sees it.",
		"{\"type\":\"object\",\"properties\":{"
			"\"key\":{\"type\":\"string\",\"description\":\"Key name, e.g. RETURN or A.\"},"
			"\"shift\":{\"type\":\"boolean\",\"default\":false},"
			"\"ctrl\":{\"type\":\"boolean\",\"default\":false}"
		"},\"required\":[\"key\"],\"additionalProperties\":false}",
		false, false, false, ToolPressKey },

	{ "type_text", "Type text",
		"Type text on the Atari keyboard, like Edit > Paste Text. Newlines "
		"press RETURN; {enter}, {esc}, {tab}, {left}, {up} and similar "
		"tokens press special keys. Advance frames afterwards so the program "
		"consumes the keys.",
		"{\"type\":\"object\",\"properties\":{"
			"\"text\":{\"type\":\"string\"}"
		"},\"required\":[\"text\"],\"additionalProperties\":false}",
		false, false, false, ToolTypeText },

	{ "joystick", "Joystick",
		"Set a joystick's direction and fire button. The state is held until "
		"changed; set direction to centre and fire to false to release.",
		"{\"type\":\"object\",\"properties\":{"
			"\"port\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":4,\"default\":1},"
			"\"direction\":{\"type\":\"string\",\"enum\":[\"centre\",\"up\",\"down\",\"left\",\"right\","
				"\"upleft\",\"upright\",\"downleft\",\"downright\"],\"default\":\"centre\"},"
			"\"fire\":{\"type\":\"boolean\",\"default\":false}"
		"},\"additionalProperties\":false}",
		false, false, true, ToolJoystick },

	{ "console_switches", "Console switches",
		"Hold the START, SELECT and OPTION console keys. Switches not set to "
		"true are released; call with no arguments to release all.",
		"{\"type\":\"object\",\"properties\":{"
			"\"start\":{\"type\":\"boolean\",\"default\":false},"
			"\"select\":{\"type\":\"boolean\",\"default\":false},"
			"\"option\":{\"type\":\"boolean\",\"default\":false}"
		"},\"additionalProperties\":false}",
		false, false, true, ToolConsoleSwitches },

	{ "save_state", "Save state",
		"Save the complete machine state to an in-memory slot (fast, kept "
		"until the emulator exits) or to a .altstate2 file.",
		"{\"type\":\"object\",\"properties\":{"
			"\"slot\":{\"type\":\"string\",\"description\":\"In-memory slot name.\"},"
			"\"path\":{\"type\":\"string\",\"description\":\"File path to write.\"}"
		"},\"additionalProperties\":false}",
		false, false, false, ToolSaveState },

	{ "load_state", "Load state",
		"Restore machine state from a slot or file. The paused/running state "
		"is kept.",
		"{\"type\":\"object\",\"properties\":{"
			"\"slot\":{\"type\":\"string\",\"description\":\"In-memory slot name.\"},"
			"\"path\":{\"type\":\"string\",\"description\":\"File path to read.\"}"
		"},\"additionalProperties\":false}",
		false, true, false, ToolLoadState },

	{ "list_states", "List states",
		"List the in-memory save-state slots.",
		MCP_NO_ARGS, true, false, true, ToolListStates },

	{ "disassemble", "Disassemble",
		"Disassemble instructions starting at an address, with symbol names "
		"(e.g. COLDSV) and the CPU mode (6502/65C02/65C816) chosen "
		"automatically.",
		"{\"type\":\"object\",\"properties\":{"
			"\"address\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":65535,\"description\":\"Start address.\"},"
			"\"count\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":1024,\"default\":16,\"description\":\"Instructions to disassemble.\"}"
		"},\"required\":[\"address\"],\"additionalProperties\":false}",
		true, false, true, ToolDisassemble },

	{ "evaluate", "Evaluate expression",
		"Evaluate an Altirra debugger expression against the current CPU "
		"state: registers (pc, a, x, y, s, p), db($80) / dw($fffc) memory "
		"reads, symbol names, $hex numbers, arithmetic, bitwise and comparison "
		"operators (= or == for equality, !=, <, <=, >, >=, and, or).",
		"{\"type\":\"object\",\"properties\":{"
			"\"expression\":{\"type\":\"string\",\"description\":\"For example dw($fffc) or a + x * 2.\"}"
		"},\"required\":[\"expression\"],\"additionalProperties\":false}",
		true, false, true, ToolEvaluate },

	{ "set_breakpoint", "Set breakpoint",
		"Set a breakpoint. 'execute' (default) stops when the CPU reaches the "
		"address, optionally only when 'condition' (a debugger expression) is "
		"non-zero; 'read', 'write' and 'readwrite' stop on memory access. "
		"When hit, emulation stops: advance_frames returns early with "
		"stopped_early true, and the CPU is at the breakpoint. Returns the "
		"breakpoint id(s).",
		"{\"type\":\"object\",\"properties\":{"
			"\"address\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":65535},"
			"\"access\":{\"type\":\"string\",\"enum\":[\"execute\",\"read\",\"write\",\"readwrite\"],\"default\":\"execute\"},"
			"\"condition\":{\"type\":\"string\",\"description\":\"Execute breakpoints only; a debugger expression such as x=0 or db($14)=5.\"}"
		"},\"required\":[\"address\"],\"additionalProperties\":false}",
		false, false, false, ToolSetBreakpoint },

	{ "clear_breakpoint", "Clear breakpoint",
		"Remove one breakpoint by id, or all breakpoints.",
		"{\"type\":\"object\",\"properties\":{"
			"\"id\":{\"type\":\"integer\",\"minimum\":0,\"description\":\"Breakpoint id from set_breakpoint or list_breakpoints.\"},"
			"\"all\":{\"type\":\"boolean\",\"description\":\"Remove every breakpoint.\"}"
		"},\"additionalProperties\":false}",
		false, true, true, ToolClearBreakpoint },

	{ "list_breakpoints", "List breakpoints",
		"List all breakpoints with their ids, addresses, access types and "
		"conditions.",
		MCP_NO_ARGS, true, false, true, ToolListBreakpoints },

	{ "history", "CPU history",
		"The most recently executed instructions (oldest first) with "
		"registers and cycle counts. Requires CPU history recording "
		"(bridge_command \"CONFIG history true\") while the code runs.",
		"{\"type\":\"object\",\"properties\":{"
			"\"count\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":4096,\"default\":64}"
		"},\"additionalProperties\":false}",
		true, false, true, ToolHistory },

	{ "call_stack", "Call stack",
		"Walk the JSR/RTS call stack: return addresses, stack pointer and "
		"flags per frame.",
		"{\"type\":\"object\",\"properties\":{"
			"\"count\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":256,\"default\":32}"
		"},\"additionalProperties\":false}",
		true, false, true, ToolCallStack },

	{ "search_memory", "Search memory",
		"Find every occurrence of a byte pattern in the CPU address space "
		"(as currently banked). Returns up to 1024 hit addresses.",
		"{\"type\":\"object\",\"properties\":{"
			"\"pattern\":{\"type\":\"string\",\"description\":\"Hex bytes, e.g. \\\"20 59 E4\\\" (JSR $E459).\"},"
			"\"start\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":65535,\"default\":0},"
			"\"end\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":65536,\"default\":65536,\"description\":\"Exclusive end address.\"}"
		"},\"required\":[\"pattern\"],\"additionalProperties\":false}",
		true, false, true, ToolSearchMemory },

	{ "symbols", "Symbols",
		"Debugger symbols. action 'load' loads a symbol file (MADS .lab/.lst, "
		"DEBUG.COM, Altirra .sym) from 'path' and returns its module_id; "
		"'resolve' turns a symbol 'name' into its address; 'lookup' finds the "
		"nearest symbol at or before 'address' ('flags' r/w/x limits the "
		"kinds, default rwx); 'unload' removes a loaded module by "
		"'module_id'; 'clear_all' removes every loaded module.",
		"{\"type\":\"object\",\"properties\":{"
			"\"action\":{\"type\":\"string\",\"enum\":[\"load\",\"resolve\",\"lookup\",\"unload\",\"clear_all\"]},"
			"\"module_id\":{\"type\":\"integer\",\"minimum\":0,\"description\":\"For unload.\"},"
			"\"path\":{\"type\":\"string\",\"description\":\"For load.\"},"
			"\"name\":{\"type\":\"string\",\"description\":\"For resolve.\"},"
			"\"address\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":65535,\"description\":\"For lookup.\"},"
			"\"flags\":{\"type\":\"string\",\"description\":\"For lookup: any of r, w, x.\"}"
		"},\"required\":[\"action\"],\"additionalProperties\":false}",
		false, false, false, ToolSymbols },

	{ "profiler", "Profiler",
		"CPU profiler. 'start' begins collecting (mode insns, functions, "
		"callgraph or basicblock), then run code (advance_frames), 'stop', "
		"and 'dump' the top hot spots (or 'dump_tree' for a callgraph "
		"session). 'status' reports whether it is running.",
		"{\"type\":\"object\",\"properties\":{"
			"\"action\":{\"type\":\"string\",\"enum\":[\"start\",\"stop\",\"status\",\"dump\",\"dump_tree\"]},"
			"\"mode\":{\"type\":\"string\",\"enum\":[\"insns\",\"functions\",\"callgraph\",\"basicblock\"],\"default\":\"insns\","
				"\"description\":\"For start.\"},"
			"\"top\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":4096,\"default\":32,\"description\":\"For dump.\"}"
		"},\"required\":[\"action\"],\"additionalProperties\":false}",
		false, false, false, ToolProfiler },

	{ "configure", "Configure machine",
		"Read or change the emulated machine's configuration. With no 'key' "
		"returns everything; with 'key' only returns that setting; with "
		"'key' and 'value' changes it (returns the full configuration). Keys "
		"include machine (800, 800XL, 1200XL, 130XE, XEGS, 1400XL, 5200), "
		"memory (8K..1088K), video (ntsc, pal, secam, pal60, ntsc50), basic, "
		"kernel, basicrom, stereo, addons, vbxe, covox, soundboard, rapidus, "
		"u1mb, siopatch, diskemu, artifact, fastboot, selftest, history and "
		"more (see the AltirraBridge PROTOCOL reference). Changing machine, "
		"memory, video, kernel and hardware add-ons cold-resets the machine.",
		"{\"type\":\"object\",\"properties\":{"
			"\"key\":{\"type\":\"string\",\"description\":\"Setting name, e.g. machine.\"},"
			"\"value\":{\"type\":[\"string\",\"integer\",\"boolean\"],\"description\":\"New value; omit to read.\"}"
		"},\"additionalProperties\":false}",
		false, true, false, ToolConfigure },

	{ "devices", "Devices",
		"Manage attached emulated devices (Configure System > Peripherals). "
		"'list' all devices; 'get' one by tag or path; 'add' a device under "
		"'parent' (bus path, default /); 'set' enables or disables a device by "
		"tag and applies 'settings'; 'remove' one; 'clear' removes all.",
		"{\"type\":\"object\",\"properties\":{"
			"\"action\":{\"type\":\"string\",\"enum\":[\"list\",\"get\",\"add\",\"set\",\"remove\",\"clear\"]},"
			"\"device\":{\"type\":\"string\",\"description\":\"Device tag (e.g. vbxe, covox, printer) or path.\"},"
			"\"parent\":{\"type\":\"string\",\"description\":\"For add: parent bus path, e.g. / or /printer/parport.\"},"
			"\"enabled\":{\"type\":\"boolean\",\"default\":true,\"description\":\"For set.\"},"
			"\"settings\":{\"type\":\"object\",\"description\":\"For add/set: device settings, e.g. {\\\"version\\\":126,\\\"base\\\":\\\"d600\\\"}.\","
				"\"additionalProperties\":{\"type\":[\"string\",\"integer\",\"number\",\"boolean\"]}}"
		"},\"required\":[\"action\"],\"additionalProperties\":false}",
		false, true, false, ToolDevices },

	{ "fresh", "Fresh start",
		"Return the emulator to a clean state for a new experiment: clears "
		"breakpoints, symbols, profiler, verifier and media, releases injected "
		"input and cold-resets. 'keep' preserves categories.",
		"{\"type\":\"object\",\"properties\":{"
			"\"keep\":{\"type\":\"array\",\"items\":{\"type\":\"string\",\"enum\":[\"bps\",\"symbols\",\"profile\",\"verifier\",\"media\",\"reset\"]},"
				"\"description\":\"Categories to keep; 'reset' skips the cold reset.\"}"
		"},\"additionalProperties\":false}",
		false, true, false, ToolFresh },

	{ "write_hardware_register", "Write hardware register",
		"Write a byte through the CPU bus with hardware side effects (e.g. "
		"ANTIC, GTIA, POKEY, PIA registers at $D000-$D3FF), unlike "
		"write_memory which writes RAM directly.",
		"{\"type\":\"object\",\"properties\":{"
			"\"address\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":65535},"
			"\"value\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":255}"
		"},\"required\":[\"address\",\"value\"],\"additionalProperties\":false}",
		false, true, false, ToolWriteHardwareRegister },

	{ "memory_map", "Memory map",
		"How the address space is currently mapped: 'regions' (RAM/ROM/IO "
		"ranges), 'banks' (PORTB banking: OS ROM, BASIC, self-test, extended "
		"RAM window) or 'cartridge' (presence, mode, size, bank).",
		"{\"type\":\"object\",\"properties\":{"
			"\"view\":{\"type\":\"string\",\"enum\":[\"regions\",\"banks\",\"cartridge\"],\"default\":\"regions\"}"
		"},\"additionalProperties\":false}",
		true, false, true, ToolMemoryMap },

	{ "palette", "Palette",
		"The GTIA colour palette: 'get' the 256 RGB entries, 'load' an Adobe "
		".act palette file from 'path', or 'reset' to the default.",
		"{\"type\":\"object\",\"properties\":{"
			"\"action\":{\"type\":\"string\",\"enum\":[\"get\",\"load\",\"reset\"]},"
			"\"path\":{\"type\":\"string\",\"description\":\"For load: .act file (768 bytes of RGB).\"}"
		"},\"required\":[\"action\"],\"additionalProperties\":false}",
		false, false, false, ToolPalette },

	{ "raw_screen", "Raw screen",
		"The current frame as raw 32-bit pixels (B,G,R,0 byte order) for "
		"pixel-exact analysis: inline as base64, or written to 'path'. Use "
		"screenshot to look at the screen.",
		"{\"type\":\"object\",\"properties\":{"
			"\"path\":{\"type\":\"string\",\"description\":\"Write the pixels to this file instead of returning them.\"}"
		"},\"additionalProperties\":false}",
		true, false, true, ToolRawScreen },

	{ "hold_key", "Hold key",
		"Hold an Atari key down in the keyboard matrix, or release it, the way "
		"a real keyboard does (for programs that poll the keyboard with "
		"interrupts off). The key stays down until released; key 'all' with "
		"down false releases everything. Same key names as press_key.",
		"{\"type\":\"object\",\"properties\":{"
			"\"key\":{\"type\":\"string\"},"
			"\"down\":{\"type\":\"boolean\",\"default\":true},"
			"\"shift\":{\"type\":\"boolean\",\"default\":false},"
			"\"ctrl\":{\"type\":\"boolean\",\"default\":false}"
		"},\"required\":[\"key\"],\"additionalProperties\":false}",
		false, false, false, ToolHoldKey },

	{ "step", "Step cycles",
		"Run the CPU for an exact number of machine cycles (including DMA) or "
		"scanlines (114 cycles each), then pause. Returns the clock state; "
		"step_completed is false if a breakpoint stopped it first.",
		"{\"type\":\"object\",\"properties\":{"
			"\"unit\":{\"type\":\"string\",\"enum\":[\"cycles\",\"scanlines\"],\"default\":\"cycles\"},"
			"\"count\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":100000000,\"default\":1},"
			"\"timeout_seconds\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":600,\"default\":30}"
		"},\"additionalProperties\":false}",
		false, false, false, ToolStep },

	{ "clock", "Clock",
		"The machine cycle counter, whether emulation is running, and the "
		"outcome of the last step.",
		MCP_NO_ARGS, true, false, true, ToolClock },

	{ "pokey_trace", "POKEY trace",
		"Capture every POKEY register write with its exact cycle, for "
		"sub-frame music and sound analysis: 'start' (optional capacity), "
		"run code, 'read' events (up to max_events per call), 'stop', or "
		"'status'.",
		"{\"type\":\"object\",\"properties\":{"
			"\"action\":{\"type\":\"string\",\"enum\":[\"start\",\"read\",\"stop\",\"status\"]},"
			"\"capacity\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":1000000,\"default\":65536,\"description\":\"For start.\"},"
			"\"max_events\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":1000000,\"default\":4096,\"description\":\"For read.\"}"
		"},\"required\":[\"action\"],\"additionalProperties\":false}",
		false, false, false, ToolPokeyTrace },

	{ "boot_bare", "Boot bare machine",
		"Boot a tiny built-in program that disables interrupts and display "
		"DMA and parks the CPU, so the machine can be driven directly as a "
		"display device (write display lists, colours and pixel data with "
		"write_memory / write_hardware_register). Returns once parked.",
		MCP_NO_ARGS, false, true, false, ToolBootBare },

	{ "delete_state", "Delete state",
		"Delete an in-memory save-state slot, or all of them.",
		"{\"type\":\"object\",\"properties\":{"
			"\"slot\":{\"type\":\"string\"},"
			"\"all\":{\"type\":\"boolean\"}"
		"},\"additionalProperties\":false}",
		false, true, true, ToolDeleteState },

	{ "verifier", "CPU verifier",
		"Altirra's CPU verifier, which flags suspicious program behaviour "
		"(e.g. stack wrap 0x200, recursive NMI 0x02) in the debugger console: "
		"'status', 'set' a flags bitmask, or 'disable'.",
		"{\"type\":\"object\",\"properties\":{"
			"\"action\":{\"type\":\"string\",\"enum\":[\"status\",\"set\",\"disable\"]},"
			"\"flags\":{\"type\":\"integer\",\"minimum\":0,\"description\":\"For set: verifier flag bits.\"}"
		"},\"required\":[\"action\"],\"additionalProperties\":false}",
		false, false, false, ToolVerifier },
#ifdef ALTIRRA_BRIDGE_AUDIO_REC

	{ "audio_record", "Audio recording",
		"Record the emulator's audio output to a file: 'start' writing to "
		"'path' (WAV, or raw PCM with raw true; stereo for dual POKEY), "
		"'stop', or 'status'.",
		"{\"type\":\"object\",\"properties\":{"
			"\"action\":{\"type\":\"string\",\"enum\":[\"start\",\"stop\",\"status\"]},"
			"\"path\":{\"type\":\"string\"},"
			"\"raw\":{\"type\":\"boolean\",\"default\":false},"
			"\"stereo\":{\"type\":\"boolean\",\"default\":false}"
		"},\"required\":[\"action\"],\"additionalProperties\":false}",
		false, false, false, ToolAudioRecord },
#endif

	{ "bridge_command", "Bridge command",
		"Run any AltirraBridge command line (see the AltirraBridge COMMANDS "
		"reference) directly. Every bridge feature also has a typed tool; "
		"use this for exact bridge syntax or options the typed tools don't "
		"expose. QUIT is not available.",
		"{\"type\":\"object\",\"properties\":{"
			"\"command\":{\"type\":\"string\",\"description\":\"Command line, e.g. \\\"DISASM $e477 8\\\".\"}"
		"},\"required\":[\"command\"],\"additionalProperties\":false}",
		false, true, false, ToolBridgeCommand },
};

const ToolDef *FindTool(const std::string& name) {
	for (const ToolDef& t : kTools) {
		if (name == t.name)
			return &t;
	}
	return nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------

const std::string& GetToolsListJson() {
	static const std::string json = [] {
		std::string out = "[";
		bool first = true;
		for (const ToolDef& t : kTools) {
			if (!first)
				out += ',';
			first = false;

			const std::string annotations = JsonObject()
				.Str("title", t.title)
				.Bool("readOnlyHint", t.readOnly)
				.Bool("destructiveHint", t.destructive)
				.Bool("idempotentHint", t.idempotent)
				.Bool("openWorldHint", false)
				.Build();

			out += JsonObject()
				.Str("name", t.name)
				.Str("title", t.title)
				.Str("description", t.description)
				.Raw("inputSchema", t.inputSchema)
				.Raw("annotations", annotations)
				.Build();
		}
		return out + "]";
	}();

	return json;
}

bool IsKnownTool(const std::string& name) {
	return FindTool(name) != nullptr;
}

bool IsWriteTool(const std::string& name) {
	const ToolDef *t = FindTool(name);
	return t && !t->readOnly;
}

bool IsWriteCall(const std::string& name, const VDJSONValueRef& args) {
	// Multi-action tools mutate only for some actions.
	const std::string action = ValueToU8(Member(args, "action"));

	if (name == "symbols")
		return action == "load" || action == "unload" || action == "clear_all";

	if (name == "configure")
		return !Member(args, "value").IsNull();

	if (name == "devices")
		return action != "list" && action != "get";

	if (name == "palette")
		return action == "load" || action == "reset";

	if (name == "pokey_trace" || name == "audio_record")
		return action == "start" || action == "stop";

	if (name == "verifier")
		return action != "status";

	if (name == "profiler")
		return action == "start" || action == "stop";

	return IsWriteTool(name);
}

ToolOutcome CallTool(const std::string& name, const VDJSONValueRef& args, ToolEnv& env) {
	const ToolDef *t = FindTool(name);
	if (!t)
		return Fail("unknown tool: " + name);

	if (!args.IsNull() && !args.IsObject())
		return Fail("'arguments' must be an object");

	return t->fn(args, env);
}

std::string BuildFramesCompletedResult(ToolEnv& env, uint32_t frames) {
	const BridgeReply regs = RunBridge(env, { "REGS" });

	// A breakpoint (or the user pausing) stops the run early; report how
	// far it got so the caller knows the CPU is at the stop point.
	const uint32_t interrupted = env.ctx.framesInterrupted < frames ? env.ctx.framesInterrupted : frames;

	JsonObject o;
	o.Int("frames", frames);
	o.Int("frames_run", frames - interrupted);
	o.Bool("stopped_early", interrupted != 0);
	o.Bool("paused", env.sim.IsPaused());
	if (regs.ok)
		o.Raw("cpu", regs.payload);

	return StructuredResult(o.Build());
}

std::string BuildAsyncCompletedResult(ToolEnv& env, const std::string& reply) {
	// BOOT_BARE delivers its own reply; a step reports through CLOCK.
	if (!reply.empty()) {
		BridgeReply r;
		r.raw = reply;
		while (!r.raw.empty() && (r.raw.back() == '\n' || r.raw.back() == '\r'))
			r.raw.pop_back();
		static const char kOkPrefix[] = "{\"ok\":true";
		const size_t okLen = sizeof kOkPrefix - 1;
		if (r.raw.compare(0, okLen, kOkPrefix) == 0) {
			const std::string payload = r.raw.size() > okLen && r.raw[okLen] == ','
				? "{" + r.raw.substr(okLen + 1) : std::string("{}");
			return StructuredResult(payload);
		}
		VDJSONDocument doc;
		VDJSONReader reader;
		std::string error;
		if (reader.Parse(r.raw.data(), r.raw.size(), doc))
			error = ValueToU8(Member(doc.Root(), "error"));
		return ToolErrorResult(error.empty() ? r.raw : error);
	}

	const BridgeReply clock = RunBridge(env, { "CLOCK" });
	return clock.ok ? StructuredResult(clock.payload) : ToolErrorResult(clock.error);
}

// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------

namespace {
	struct ResourceDef {
		const char *uri;
		const char *name;
		const char *title;
		const char *description;
		const char *mimeType;
	};

	const ResourceDef kResources[] = {
		{ "altirra://status", "status", "Emulator status",
			"Paused/running state, machine configuration and CPU registers.", "application/json" },
		{ "altirra://screen", "screen", "Screen",
			"The current emulated display as a PNG image.", "image/png" },
		{ "altirra://cpu", "cpu", "CPU registers",
			"CPU registers and cycle counters.", "application/json" },
		{ "altirra://hardware", "hardware", "Hardware registers",
			"ANTIC, GTIA, POKEY and PIA register state.", "application/json" },
		{ "altirra://display-list", "display-list", "Display list",
			"The decoded ANTIC display list.", "application/json" },
		{ "altirra://memory-map", "memory-map", "Memory map",
			"Current RAM/ROM/IO mapping of the address space.", "application/json" },
		{ "altirra://breakpoints", "breakpoints", "Breakpoints",
			"All breakpoints and watchpoints.", "application/json" },
		{ "altirra://config", "config", "Configuration",
			"The emulated machine's configuration.", "application/json" },
		{ "altirra://debugger/last-stop", "last-stop", "Last debugger stop",
			"Why emulation last stopped in the debugger (breakpoint id, address, "
			"access type, PC, registers, frame, cycle). Subscribe to it to be "
			"notified when a breakpoint hits.", "application/json" },
	};

	struct TemplateDef {
		const char *uriTemplate;
		const char *name;
		const char *title;
		const char *description;
		const char *mimeType;
	};

	const TemplateDef kTemplates[] = {
		{ "altirra://memory/{address}/{length}", "memory", "Memory range",
			"Bytes from the CPU address space (address and length in decimal, "
			"or hex with a 0x or $ prefix; length up to 65536).", "application/octet-stream" },
		{ "altirra://disassembly/{address}/{count}", "disassembly", "Disassembly",
			"Disassembled instructions starting at address (count up to 1024).", "text/plain" },
	};

	// Last debugger stop, served as altirra://debugger/last-stop.
	std::string g_lastStopJson = "{\"seq\":0,\"reason\":\"none\"}";

	std::string TextContents(const std::string& uri, const char *mime, const std::string& text) {
		return JsonObject().Str("uri", uri).Str("mimeType", mime).Str("text", text).Build();
	}

	std::string BlobContents(const std::string& uri, const char *mime, const std::string& base64) {
		return JsonObject().Str("uri", uri).Str("mimeType", mime).Str("blob", base64).Build();
	}

	// Split "altirra://kind/a/b" into kind and the two parameters.
	bool SplitTemplateUri(const std::string& uri, const char *kind, std::string& a, std::string& b) {
		const std::string prefix = std::string("altirra://") + kind + "/";
		if (uri.compare(0, prefix.size(), prefix) != 0)
			return false;
		const std::string rest = uri.substr(prefix.size());
		const size_t slash = rest.find('/');
		if (slash == std::string::npos || rest.find('/', slash + 1) != std::string::npos)
			return false;
		a = rest.substr(0, slash);
		b = rest.substr(slash + 1);
		return !a.empty() && !b.empty();
	}
}

bool IsKnownResourceUri(const std::string& uri) {
	for (const ResourceDef& r : kResources) {
		if (uri == r.uri)
			return true;
	}

	std::string a, b;
	return SplitTemplateUri(uri, "memory", a, b) || SplitTemplateUri(uri, "disassembly", a, b);
}

void SetLastStopJson(const std::string& json) {
	g_lastStopJson = json;
}

std::string BuildStopReport(ToolEnv& env, const char *reason, StopAccess access, uint32_t seq) {
	ATSimulator& sim = env.sim;
	const uint16_t pc = sim.GetCPU().GetInsnPC();

	// Read/write breakpoints: the debugger records the accessed address
	// for the expression evaluator ("address").
	bool haveAccessAddr = false;
	uint32_t accessAddr = 0;
	if (access != StopAccess::None) {
		const BridgeReply r = RunBridge(env, { "EVAL", "address" });
		VDJSONDocument doc;
		VDJSONReader reader;
		if (r.ok && reader.Parse(r.payload.data(), r.payload.size(), doc)) {
			accessAddr = (uint32_t)Member(doc.Root(), "value").AsInt64() & 0xFFFF;
			haveAccessAddr = true;
		}
	}

	// Identify the user breakpoint that stopped execution. One-shot
	// breakpoints are already gone by now and report null.
	std::string bpJson = "null";
	if (IATDebugger *dbg = ATGetDebugger()) {
		vdfastvector<uint32> ids;
		dbg->GetBreakpointList(ids);
		for (uint32 id : ids) {
			ATDebuggerBreakpointInfo info {};
			if (!dbg->GetBreakpointInfo(id, info))
				continue;

			const uint32_t lo = (uint32_t)info.mAddress & 0xFFFF;
			const uint32_t len = info.mLength ? info.mLength : 1;
			bool match = false;
			const char *type = "execute";
			if (access == StopAccess::None) {
				match = info.mbBreakOnPC && lo == pc;
			} else if (haveAccessAddr && accessAddr >= lo && accessAddr < lo + len) {
				if (access == StopAccess::Read && info.mbBreakOnRead) {
					match = true;
					type = "read";
				} else if (access == StopAccess::Write && info.mbBreakOnWrite) {
					match = true;
					type = "write";
				}
			}

			if (match) {
				char addr[16];
				std::snprintf(addr, sizeof addr, "$%04X", (unsigned)lo);
				bpJson = JsonObject()
					.Int("id", id)
					.Str("address", addr)
					.Str("access", type)
					.Bool("conditional", info.mpCondition != nullptr)
					.Bool("one_shot", info.mbOneShot)
					.Build();
				break;
			}
		}
	}

	const BridgeReply regs = RunBridge(env, { "REGS" });

	char pcText[16];
	std::snprintf(pcText, sizeof pcText, "$%04X", (unsigned)pc);

	JsonObject o;
	o.Int("seq", seq);
	o.Str("reason", reason);
	o.Raw("breakpoint", bpJson);
	if (haveAccessAddr) {
		char a[16];
		std::snprintf(a, sizeof a, "$%04X", (unsigned)accessAddr);
		o.Str("access_address", a);
	}
	o.Str("pc", pcText);
	o.Int("frame", sim.GetAntic().GetRawFrameCounter());
	o.Int("cycle", (int64_t)sim.GetScheduler()->GetTick64());
	o.Bool("running", sim.IsRunning());
	if (regs.ok)
		o.Raw("registers", regs.payload);
	return o.Build();
}

const std::string& GetResourcesListJson() {
	static const std::string json = [] {
		std::string out = "[";
		for (const ResourceDef& r : kResources) {
			if (out.size() > 1)
				out += ',';
			out += JsonObject().Str("uri", r.uri).Str("name", r.name).Str("title", r.title)
				.Str("description", r.description).Str("mimeType", r.mimeType).Build();
		}
		return out + "]";
	}();
	return json;
}

const std::string& GetResourceTemplatesListJson() {
	static const std::string json = [] {
		std::string out = "[";
		for (const TemplateDef& t : kTemplates) {
			if (out.size() > 1)
				out += ',';
			out += JsonObject().Str("uriTemplate", t.uriTemplate).Str("name", t.name).Str("title", t.title)
				.Str("description", t.description).Str("mimeType", t.mimeType).Build();
		}
		return out + "]";
	}();
	return json;
}

ResourceReadStatus ReadResource(const std::string& uri, ToolEnv& env, std::string& contents, std::string& error) {
	const auto simple = [&](std::initializer_list<const char *> verb) -> ResourceReadStatus {
		const BridgeReply r = RunBridge(env, std::vector<std::string>(verb.begin(), verb.end()));
		if (!r.ok) {
			error = r.error;
			return ResourceReadStatus::Failed;
		}
		contents = TextContents(uri, "application/json", r.payload);
		return ResourceReadStatus::Ok;
	};

	if (uri == "altirra://status") {
		const BridgeReply config = RunBridge(env, { "CONFIG" });
		const BridgeReply regs = RunBridge(env, { "REGS" });
		if (!config.ok || !regs.ok) {
			error = config.ok ? regs.error : config.error;
			return ResourceReadStatus::Failed;
		}
		contents = TextContents(uri, "application/json", JsonObject()
			.Bool("paused", env.sim.IsPaused())
			.Bool("running", env.sim.IsRunning())
			.Raw("config", config.payload)
			.Raw("cpu", regs.payload)
			.Build());
		return ResourceReadStatus::Ok;
	}

	if (uri == "altirra://screen") {
		const BridgeReply r = RunBridge(env, { "SCREENSHOT", "inline=true" });
		VDJSONDocument doc;
		VDJSONReader reader;
		if (!r.ok || !reader.Parse(r.payload.data(), r.payload.size(), doc)) {
			error = r.ok ? std::string("unexpected SCREENSHOT reply") : r.error;
			return ResourceReadStatus::Failed;
		}
		contents = BlobContents(uri, "image/png", ValueToU8(Member(doc.Root(), "data")));
		return ResourceReadStatus::Ok;
	}

	if (uri == "altirra://cpu")          return simple({ "REGS" });
	if (uri == "altirra://hardware")     return simple({ "HWSTATE" });
	if (uri == "altirra://display-list") return simple({ "DLIST" });
	if (uri == "altirra://memory-map")   return simple({ "MEMMAP" });
	if (uri == "altirra://breakpoints")  return simple({ "BP_LIST" });
	if (uri == "altirra://config")       return simple({ "CONFIG" });

	if (uri == "altirra://debugger/last-stop") {
		contents = TextContents(uri, "application/json", g_lastStopJson);
		return ResourceReadStatus::Ok;
	}

	std::string a, b;
	if (SplitTemplateUri(uri, "memory", a, b)) {
		uint32_t address = 0, length = 0;
		if (!ATBridge::ParseUint(a, address) || !ATBridge::ParseUint(b, length)
			|| !length || length > 65536 || address > 0xFFFFFF)
		{
			error = "bad memory range in " + uri;
			return ResourceReadStatus::Failed;
		}
		const BridgeReply r = RunBridge(env, { "MEMDUMP", std::to_string(address), std::to_string(length) });
		VDJSONDocument doc;
		VDJSONReader reader;
		if (!r.ok || !reader.Parse(r.payload.data(), r.payload.size(), doc)) {
			error = r.ok ? std::string("unexpected MEMDUMP reply") : r.error;
			return ResourceReadStatus::Failed;
		}
		contents = BlobContents(uri, "application/octet-stream", ValueToU8(Member(doc.Root(), "data")));
		return ResourceReadStatus::Ok;
	}

	if (SplitTemplateUri(uri, "disassembly", a, b)) {
		uint32_t address = 0, count = 0;
		if (!ATBridge::ParseUint(a, address) || !ATBridge::ParseUint(b, count)
			|| !count || count > 1024 || address > 0xFFFF)
		{
			error = "bad disassembly range in " + uri;
			return ResourceReadStatus::Failed;
		}
		JsonObject args;
		args.Int("address", address).Int("count", count);
		const std::string argsJson = args.Build();
		VDJSONDocument doc;
		VDJSONReader reader;
		reader.Parse(argsJson.data(), argsJson.size(), doc);
		const ToolOutcome o = ToolDisassemble(doc.Root(), env);

		VDJSONDocument res;
		VDJSONReader reader2;
		if (!reader2.Parse(o.result.data(), o.result.size(), res) || Member(res.Root(), "isError").AsBool()) {
			error = "disassembly failed";
			return ResourceReadStatus::Failed;
		}
		const std::string text = ValueToU8(Member(*Member(res.Root(), "content").AsArray().begin(), "text"));
		contents = TextContents(uri, "text/plain", text);
		return ResourceReadStatus::Ok;
	}

	return ResourceReadStatus::NotFound;
}

std::string ToolErrorResult(const std::string& message) {
	return JsonObject()
		.Raw("content", "[" + TextContent(message) + "]")
		.Bool("isError", true)
		.Build();
}

const char *GetServerInstructions() {
	return
		"Controls a running AltirraSDL Atari 8-bit emulator. "
		"The machine keeps running unless paused; call pause before inspecting "
		"memory or registers so they stay consistent. "
		"Emulated time only passes while running: after boot_media, key "
		"presses or joystick changes, call advance_frames (60 frames is about "
		"one second on NTSC) so the program can react, then take a screenshot "
		"or read memory. "
		"Addresses are integers (convert $D40B to 54283). "
		"Injected joystick and console-switch input is held until changed and "
		"is released when the MCP server stops. "
		"bridge_command reaches every other AltirraBridge feature (debugger, "
		"breakpoints, symbols, profiler). "
		"To be told when a breakpoint hits, subscribe to the resource "
		"altirra://debugger/last-stop (subscriptions/listen, or "
		"resources/subscribe on older protocol versions) and read it when it "
		"is updated.";
}

}  // namespace ATMCP
