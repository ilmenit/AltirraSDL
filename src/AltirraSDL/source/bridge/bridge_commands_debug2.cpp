// AltirraBridge - Phase 5b debugger commands (impl).
//
// One file, one theme: "make the debugger steerable from a client
// over the wire". Every command is a direct wrap of an existing
// public Altirra API. The helpers at the top of this file are
// local copies of the tiny JSON formatters — duplicating ~40 lines
// is cheaper than pulling bridge_commands_state.cpp internals out
// into a shared header.

#include <stdafx.h>

#include "bridge_commands_debug2.h"
#include "bridge_protocol.h"

#include "simulator.h"
#include "cpu.h"
#include "cpumemory.h"

#include "debugger.h"
#include "debuggerexp.h"
#include "verifier.h"
#include "profiler.h"

#include <at/atdebugger/symbols.h>

#include <vd2/system/error.h>
#include <vd2/system/VDString.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/text.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace ATBridge {

// ===========================================================================
// Local helpers
// ===========================================================================

namespace {

std::string Hex8 (uint32_t v) { char b[8];  std::snprintf(b, sizeof b, "\"$%02x\"", v & 0xff);   return b; }
std::string Hex16(uint32_t v) { char b[12]; std::snprintf(b, sizeof b, "\"$%04x\"", v & 0xffff); return b; }

void AddField(std::string& o, const char* k, const std::string& v) { o += '"';  o += k; o += "\":";  o += v;  o += ','; }
void AddU32  (std::string& o, const char* k, uint32_t v)           { o += '"';  o += k; o += "\":";  o += std::to_string(v); o += ','; }
void AddI32  (std::string& o, const char* k, int32_t v)            { o += '"';  o += k; o += "\":";  o += std::to_string(v); o += ','; }
void AddStr  (std::string& o, const char* k, const std::string& v) { o += '"';  o += k; o += "\":\""; o += JsonEscape(v); o += "\","; }
void AddBool (std::string& o, const char* k, bool v)               { o += '"';  o += k; o += "\":";  o += (v?"true":"false"); o += ','; }

void StripTrailingComma(std::string& s) { if (!s.empty() && s.back() == ',') s.pop_back(); }

bool ParseAddr16(const std::string& s, uint16_t& out) {
	uint32_t v = 0;
	if (!ParseUint(s, v) || v > 0xffff) return false;
	out = (uint16_t)v;
	return true;
}

// "key=value" argument splitter.
bool MatchKey(const std::string& tok, const char* key, std::string& value) {
	const size_t kl = std::strlen(key);
	if (tok.size() <= kl || std::strncmp(tok.c_str(), key, kl) != 0 || tok[kl] != '=')
		return false;
	value.assign(tok, kl + 1, std::string::npos);
	return true;
}

// Build an ATDebugExpEvalContext suitable for parsing expressions in
// the command dispatcher. Uses the debugger's own context.
IATDebugger* Dbg()             { return ATGetDebugger(); }
IATDebuggerSymbolLookup* SymLookup() { return ATGetDebuggerSymbolLookup(); }

// Parse an expression string and return the owned AST node, or emit
// a JSON error on failure. Caller owns the returned pointer.
ATDebugExpNode* ParseExprOrError(const std::string& src, std::string& outError) {
	IATDebugger* dbg = Dbg();
	if (!dbg) { outError = "debugger unavailable"; return nullptr; }
	ATDebugExpEvalContext ctx = dbg->GetEvalContext();
	try {
		ATDebugExpNode* node = ATDebuggerParseExpression(
			src.c_str(), SymLookup(), dbg->GetExprOpts(), &ctx);
		return node;  // may be nullptr on quiet failure
	} catch (const MyError& e) {
		outError = e.c_str();
		while (!outError.empty() && (outError.back() == '\n' || outError.back() == '\r'))
			outError.pop_back();
		return nullptr;
	}
}

}  // namespace

// ===========================================================================
// BP_SET addr [condition=EXPR]
//
// PC breakpoint. The condition expression (if given) is parsed with
// ATDebuggerParseExpression and attached to the breakpoint — the
// debugger will only halt when the expression is non-zero at the PC.
//
// The condition travels as key=value; any embedded spaces must be
// avoided by the client (the bridge tokenizer is space-split).
// ===========================================================================

std::string CmdBpSet(ATSimulator& sim, const std::vector<std::string>& tokens) {
	(void)sim;
	if (tokens.size() < 2)
		return JsonError("BP_SET: usage: BP_SET addr [condition=EXPR]");

	uint16_t addr = 0;
	if (!ParseAddr16(tokens[1], addr))
		return JsonError("BP_SET: bad address");

	std::string conditionSrc;
	for (size_t i = 2; i < tokens.size(); ++i) {
		std::string v;
		if (MatchKey(tokens[i], "condition", v)) { conditionSrc = v; continue; }
		return JsonError("BP_SET: unknown option: " + tokens[i]);
	}

	IATDebugger* dbg = Dbg();
	if (!dbg) return JsonError("BP_SET: debugger unavailable");

	// Parse condition first so a syntax error doesn't half-create
	// the breakpoint. We OWN the resulting node — SetBreakpoint
	// clones it internally (debugger.cpp:1447), so we must delete
	// our copy unconditionally after the call.
	ATDebugExpNode* condNode = nullptr;
	if (!conditionSrc.empty()) {
		std::string err;
		condNode = ParseExprOrError(conditionSrc, err);
		if (!condNode)
			return JsonError(std::string("BP_SET: bad condition: ") + err);
	}

	ATDebuggerBreakpointInfo info;
	info.mTargetIndex = 0;
	info.mAddress     = addr;
	info.mLength      = 1;
	info.mbBreakOnPC  = true;
	info.mpCondition  = condNode;  // cloned by SetBreakpoint

	uint32_t id = 0;
	try {
		id = dbg->SetBreakpoint(-1, info);
	} catch (const MyError& e) {
		delete condNode;
		std::string msg = e.c_str();
		while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) msg.pop_back();
		return JsonError(std::string("BP_SET: ") + msg);
	}
	delete condNode;  // SetBreakpoint cloned; original is ours to free

	std::string payload;
	AddU32  (payload, "id",   id);
	AddField(payload, "addr", Hex16(addr));
	AddBool (payload, "conditional", !conditionSrc.empty());
	StripTrailingComma(payload);
	return JsonOk(payload);
}

// ===========================================================================
// WATCH_SET addr [len=N] [mode=r|w|rw]
//
// Access breakpoint (READ / WRITE / both). Halts execution when the
// CPU accesses the range. Backed by SetBreakpoint with
// mbBreakOnRead/Write set — exactly what the ".ba" console command
// uses. Returns the breakpoint id (same pool as BP_SET, so
// BP_CLEAR id clears watchpoints too).
// ===========================================================================

std::string CmdWatchSet(ATSimulator& sim, const std::vector<std::string>& tokens) {
	(void)sim;
	if (tokens.size() < 2)
		return JsonError("WATCH_SET: usage: WATCH_SET addr [mode=r|w|rw]");

	uint16_t addr = 0;
	if (!ParseAddr16(tokens[1], addr))
		return JsonError("WATCH_SET: bad address");

	std::string mode = "r";
	for (size_t i = 2; i < tokens.size(); ++i) {
		std::string v;
		if (MatchKey(tokens[i], "mode", v)) { mode = v; continue; }
		if (MatchKey(tokens[i], "len",  v)) {
			// Altirra's access breakpoint API is per-address only
			// (debugger.cpp:1465-1467 calls SetAccessBP(mAddress,...)
			// and ignores mLength entirely). Range watchpoints would
			// require N separate breakpoints; reject explicitly so
			// the client doesn't silently get a single-byte watch.
			return JsonError("WATCH_SET: multi-byte ranges are not supported; set one WATCH_SET per address");
		}
		return JsonError("WATCH_SET: unknown option: " + tokens[i]);
	}

	bool onRead  = false;
	bool onWrite = false;
	if      (mode == "r")  onRead  = true;
	else if (mode == "w")  onWrite = true;
	else if (mode == "rw") { onRead = true; onWrite = true; }
	else return JsonError("WATCH_SET: mode must be r, w, or rw");

	IATDebugger* dbg = Dbg();
	if (!dbg) return JsonError("WATCH_SET: debugger unavailable");

	// The underlying SetBreakpoint REJECTS mbBreakOnRead ==
	// mbBreakOnWrite (debugger.cpp:1458). To honour "rw", we issue
	// two separate breakpoints — one per direction — and return
	// both IDs. On failure of the second, we roll back the first
	// so callers never see orphan state.
	auto setOne = [&](bool write) -> uint32_t {
		ATDebuggerBreakpointInfo info;
		info.mTargetIndex   = 0;
		info.mAddress       = addr;
		info.mLength        = 1;
		info.mbBreakOnRead  = !write;
		info.mbBreakOnWrite =  write;
		return dbg->SetBreakpoint(-1, info);
	};

	uint32_t idR = (uint32_t)-1;
	uint32_t idW = (uint32_t)-1;
	try {
		if (onRead)  idR = setOne(false);
		if (onWrite) idW = setOne(true);
	} catch (const MyError& e) {
		if (idR != (uint32_t)-1) dbg->ClearUserBreakpoint(idR, true);
		std::string msg = e.c_str();
		while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) msg.pop_back();
		return JsonError(std::string("WATCH_SET: ") + msg);
	}

	std::string idsArr;
	idsArr += '[';
	bool first = true;
	if (idR != (uint32_t)-1) { idsArr += std::to_string(idR); first = false; }
	if (idW != (uint32_t)-1) { if (!first) idsArr += ','; idsArr += std::to_string(idW); }
	idsArr += ']';

	std::string payload;
	AddField(payload, "ids",  idsArr);
	AddField(payload, "addr", Hex16(addr));
	AddStr  (payload, "mode", mode);
	StripTrailingComma(payload);
	return JsonOk(payload);
}

// ===========================================================================
// BP_CLEAR id
// BP_CLEAR_ALL
// ===========================================================================

std::string CmdBpClear(ATSimulator& sim, const std::vector<std::string>& tokens) {
	(void)sim;
	if (tokens.size() < 2)
		return JsonError("BP_CLEAR: usage: BP_CLEAR id");
	uint32_t id = 0;
	if (!ParseUint(tokens[1], id))
		return JsonError("BP_CLEAR: bad id");

	IATDebugger* dbg = Dbg();
	if (!dbg) return JsonError("BP_CLEAR: debugger unavailable");

	bool cleared = dbg->ClearUserBreakpoint(id, true);
	std::string payload;
	AddU32 (payload, "id", id);
	AddBool(payload, "cleared", cleared);
	StripTrailingComma(payload);
	return cleared ? JsonOk(payload) : JsonError("BP_CLEAR: id not found");
}

std::string CmdBpClearAll(ATSimulator& sim, const std::vector<std::string>& tokens) {
	(void)sim; (void)tokens;
	IATDebugger* dbg = Dbg();
	if (!dbg) return JsonError("BP_CLEAR_ALL: debugger unavailable");
	dbg->ClearAllBreakpoints();
	return JsonOk();
}

// ===========================================================================
// BP_LIST — list every user breakpoint, with flags and optional
// decoded condition expression source (when available).
// ===========================================================================

std::string CmdBpList(ATSimulator& sim, const std::vector<std::string>& tokens) {
	(void)sim; (void)tokens;
	IATDebugger* dbg = Dbg();
	if (!dbg) return JsonError("BP_LIST: debugger unavailable");

	vdfastvector<uint32> ids;
	dbg->GetBreakpointList(ids);

	std::string arr;
	arr += '[';
	bool first = true;
	for (uint32_t id : ids) {
		ATDebuggerBreakpointInfo info {};
		if (!dbg->GetBreakpointInfo(id, info))
			continue;
		if (!first) arr += ',';
		first = false;
		arr += '{';
		std::string e;
		AddU32  (e, "id",     id);
		AddField(e, "addr",   Hex16((uint32_t)info.mAddress));
		AddU32  (e, "length", info.mLength);
		AddBool (e, "pc",     info.mbBreakOnPC);
		AddBool (e, "read",   info.mbBreakOnRead);
		AddBool (e, "write",  info.mbBreakOnWrite);
		AddBool (e, "oneshot",info.mbOneShot);
		AddBool (e, "continue_on_hit", info.mbContinueExecution);
		AddBool (e, "conditional", info.mpCondition != nullptr);
		StripTrailingComma(e);
		arr += e;
		arr += '}';
	}
	arr += ']';

	std::string payload;
	AddU32(payload, "count", (uint32_t)ids.size());
	payload += "\"breakpoints\":";
	payload += arr;
	return JsonOk(payload);
}

// ===========================================================================
// SYM_LOAD path
//
// Accepts MADS .lab / .lbl / .lst, DEBUG.COM, Altirra .sym, and
// generic text symbol formats (whatever IATDebugger::LoadSymbols
// auto-detects — we defer format decisions to Altirra).
// ===========================================================================

std::string CmdSymLoad(ATSimulator& sim, const std::vector<std::string>& tokens) {
	(void)sim;
	if (tokens.size() < 2)
		return JsonError("SYM_LOAD: usage: SYM_LOAD path");
	// Join all trailing tokens with spaces so the user can pass a
	// path with embedded spaces (though that's unusual on Atari
	// dev filesystems). No escape handling — clients should avoid
	// paths with quotes.
	std::string path = tokens[1];
	for (size_t i = 2; i < tokens.size(); ++i) { path += ' '; path += tokens[i]; }

	IATDebugger* dbg = Dbg();
	if (!dbg) return JsonError("SYM_LOAD: debugger unavailable");

	uint32_t moduleId = 0;
	try {
		const VDStringW wpath = VDTextU8ToW(VDStringSpanA(path.c_str()));
		moduleId = dbg->LoadSymbols(wpath.c_str(), /*processDirectives*/ true,
		                            /*targetIdOverride*/ nullptr,
		                            /*loadImmediately*/ true);
	} catch (const MyError& e) {
		std::string msg = e.c_str();
		while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r'))
			msg.pop_back();
		return JsonError(std::string("SYM_LOAD: ") + msg);
	}

	std::string payload;
	AddStr (payload, "path",      path);
	AddU32 (payload, "module_id", moduleId);
	StripTrailingComma(payload);
	return JsonOk(payload);
}

// ===========================================================================
// SYM_RESOLVE name  → address
// ===========================================================================

std::string CmdSymResolve(ATSimulator& sim, const std::vector<std::string>& tokens) {
	(void)sim;
	if (tokens.size() < 2)
		return JsonError("SYM_RESOLVE: usage: SYM_RESOLVE name");

	IATDebugger* dbg = Dbg();
	if (!dbg) return JsonError("SYM_RESOLVE: debugger unavailable");

	sint32 v = dbg->ResolveSymbol(tokens[1].c_str(), /*allowGlobal*/ true);
	if (v < 0)
		return JsonError("SYM_RESOLVE: symbol not found");

	std::string payload;
	AddStr  (payload, "name",  tokens[1]);
	AddU32  (payload, "value", (uint32_t)v);
	AddField(payload, "hex",   Hex16((uint32_t)v));
	StripTrailingComma(payload);
	return JsonOk(payload);
}

// ===========================================================================
// SYM_LOOKUP addr [flags=rwx]
//
// Reverse lookup: closest symbol at or below the given address, with
// byte offset from the symbol base. Flags:
//   r = kATSymbol_Read, w = Write, x = Execute, any = all (default)
// ===========================================================================

std::string CmdSymLookup(ATSimulator& sim, const std::vector<std::string>& tokens) {
	(void)sim;
	if (tokens.size() < 2)
		return JsonError("SYM_LOOKUP: usage: SYM_LOOKUP addr [flags=rwx]");

	uint16_t addr = 0;
	if (!ParseAddr16(tokens[1], addr))
		return JsonError("SYM_LOOKUP: bad address");

	uint32_t flags = kATSymbol_Any;
	for (size_t i = 2; i < tokens.size(); ++i) {
		std::string v;
		if (MatchKey(tokens[i], "flags", v)) {
			flags = 0;
			for (char c : v) {
				switch (c) {
				case 'r': case 'R': flags |= kATSymbol_Read;  break;
				case 'w': case 'W': flags |= kATSymbol_Write; break;
				case 'x': case 'X': flags |= kATSymbol_Execute; break;
				default: return JsonError("SYM_LOOKUP: bad flag char");
				}
			}
			continue;
		}
		return JsonError("SYM_LOOKUP: unknown option: " + tokens[i]);
	}
	if (flags == 0) flags = kATSymbol_Any;

	IATDebuggerSymbolLookup* lookup = SymLookup();
	if (!lookup)
		return JsonError("SYM_LOOKUP: symbol lookup unavailable");

	ATSymbol sym {};
	if (!lookup->LookupSymbol(addr, flags, sym))
		return JsonError("SYM_LOOKUP: no symbol at or before address");

	const uint32_t offset = (uint32_t)addr - sym.mOffset;

	std::string payload;
	AddField(payload, "addr",   Hex16(addr));
	AddStr  (payload, "name",   sym.mpName ? sym.mpName : "");
	AddField(payload, "base",   Hex16(sym.mOffset));
	AddU32  (payload, "offset", offset);
	StripTrailingComma(payload);
	return JsonOk(payload);
}

// ===========================================================================
// MEMSEARCH pattern [start=$XXXX] [end=$XXXX]
//
// Linear scan over the CPU-view address space (banking-aware via
// sim.DebugReadByte). Pattern is hex bytes with no separators
// (e.g. "a9ff" finds the 2-byte sequence A9 FF).
//
// Altirra has a ATCheatEngine that's optimised for iterative
// narrowing, but a one-shot linear scan of 64K is trivially fast
// (<1ms) so we just do it inline and avoid the setup dance.
// ===========================================================================

std::string CmdMemSearch(ATSimulator& sim, const std::vector<std::string>& tokens) {
	if (tokens.size() < 2)
		return JsonError("MEMSEARCH: usage: MEMSEARCH hexpattern [start=$XXXX] [end=$XXXX]");

	const std::string& patStr = tokens[1];
	if (patStr.empty() || (patStr.size() & 1))
		return JsonError("MEMSEARCH: pattern must be an even number of hex digits");

	std::vector<uint8_t> pattern(patStr.size() / 2);
	for (size_t i = 0; i < pattern.size(); ++i) {
		auto hex = [](char c) -> int {
			if (c >= '0' && c <= '9') return c - '0';
			if (c >= 'a' && c <= 'f') return c - 'a' + 10;
			if (c >= 'A' && c <= 'F') return c - 'A' + 10;
			return -1;
		};
		int hi = hex(patStr[i*2]);
		int lo = hex(patStr[i*2 + 1]);
		if (hi < 0 || lo < 0) return JsonError("MEMSEARCH: bad hex character");
		pattern[i] = (uint8_t)((hi << 4) | lo);
	}

	uint32_t start = 0x0000;
	uint32_t end   = 0x10000;
	for (size_t i = 2; i < tokens.size(); ++i) {
		std::string v;
		if (MatchKey(tokens[i], "start", v)) { if (!ParseUint(v, start)) return JsonError("MEMSEARCH: bad start"); continue; }
		if (MatchKey(tokens[i], "end",   v)) { if (!ParseUint(v, end))   return JsonError("MEMSEARCH: bad end");   continue; }
		return JsonError("MEMSEARCH: unknown option: " + tokens[i]);
	}
	if (start >= 0x10000 || end > 0x10000 || start >= end)
		return JsonError("MEMSEARCH: range out of bounds");

	// Snapshot the search range to avoid calling DebugReadByte in
	// the inner matching loop; also avoids inconsistencies if the
	// pattern ever spans a banking transition (rare but real).
	const uint32_t len = end - start;
	std::vector<uint8_t> buf(len);
	for (uint32_t i = 0; i < len; ++i)
		buf[i] = sim.DebugReadByte((uint16_t)(start + i));

	std::vector<uint16_t> hits;
	if (pattern.size() <= len) {
		const uint32_t last = len - (uint32_t)pattern.size();
		for (uint32_t i = 0; i <= last; ++i) {
			if (std::memcmp(buf.data() + i, pattern.data(), pattern.size()) == 0) {
				hits.push_back((uint16_t)(start + i));
				if (hits.size() >= 1024) break;  // cap
			}
		}
	}

	std::string arr;
	arr += '[';
	for (size_t i = 0; i < hits.size(); ++i) {
		if (i) arr += ',';
		arr += Hex16(hits[i]);
	}
	arr += ']';

	std::string payload;
	AddU32(payload, "count", (uint32_t)hits.size());
	payload += "\"hits\":";
	payload += arr;
	return JsonOk(payload);
}

// ===========================================================================
// PROFILE_START [mode=insns|functions|callgraph|basicblock]
// ===========================================================================

std::string CmdProfileStart(ATSimulator& sim, const std::vector<std::string>& tokens) {
	std::string modeStr = "insns";
	for (size_t i = 1; i < tokens.size(); ++i) {
		std::string v;
		if (MatchKey(tokens[i], "mode", v)) { modeStr = v; continue; }
		return JsonError("PROFILE_START: unknown option: " + tokens[i]);
	}

	ATProfileMode mode;
	if      (modeStr == "insns")      mode = kATProfileMode_Insns;
	else if (modeStr == "functions")  mode = kATProfileMode_Functions;
	else if (modeStr == "callgraph")  mode = kATProfileMode_CallGraph;
	else if (modeStr == "basicblock") mode = kATProfileMode_BasicBlock;
	else return JsonError("PROFILE_START: bad mode");

	sim.SetProfilingEnabled(true);
	ATCPUProfiler* prof = sim.GetProfiler();
	if (!prof) return JsonError("PROFILE_START: profiler init failed");

	if (prof->IsRunning())
		prof->End();

	prof->SetBoundaryRule(kATProfileBoundaryRule_None, 0, 0);
	prof->SetGlobalAddressesEnabled(true);
	// Start() calls OpenFrame() internally (profiler.cpp:949) —
	// calling BeginFrame() afterwards would AdvanceFrame(true),
	// closing the just-opened frame and discarding its samples.
	prof->Start(mode, kATProfileCounterMode_None, kATProfileCounterMode_None);

	std::string payload;
	AddStr(payload, "mode", modeStr);
	StripTrailingComma(payload);
	return JsonOk(payload);
}

// ===========================================================================
// PROFILE_STOP
// ===========================================================================

std::string CmdProfileStop(ATSimulator& sim, const std::vector<std::string>& tokens) {
	(void)tokens;
	ATCPUProfiler* prof = sim.GetProfiler();
	if (!prof || !prof->IsRunning())
		return JsonError("PROFILE_STOP: profiler not running");

	// End() already calls CloseFrame() + Finalize() — do NOT call
	// EndFrame() first (it would insert an empty trailing frame).
	prof->End();
	return JsonOk();
}

// ===========================================================================
// PROFILE_STATUS
// ===========================================================================

std::string CmdProfileStatus(ATSimulator& sim, const std::vector<std::string>& tokens) {
	(void)tokens;
	ATCPUProfiler* prof = sim.GetProfiler();
	std::string payload;
	AddBool(payload, "enabled", prof != nullptr);
	AddBool(payload, "running", prof && prof->IsRunning());
	StripTrailingComma(payload);
	return JsonOk(payload);
}

// ===========================================================================
// PROFILE_DUMP [top=N]
//
// Flat dump — works for Insns / Functions / BasicBlock modes. Sorts
// records by cycle cost descending and returns the top N (default 32,
// cap 4096). For callgraph mode use PROFILE_DUMP_TREE.
// ===========================================================================

std::string CmdProfileDump(ATSimulator& sim, const std::vector<std::string>& tokens) {
	uint32_t top = 32;
	for (size_t i = 1; i < tokens.size(); ++i) {
		std::string v;
		if (MatchKey(tokens[i], "top", v)) { if (!ParseUint(v, top)) return JsonError("PROFILE_DUMP: bad top"); continue; }
		return JsonError("PROFILE_DUMP: unknown option: " + tokens[i]);
	}
	if (top == 0) top = 1;
	if (top > 4096) top = 4096;

	ATCPUProfiler* prof = sim.GetProfiler();
	if (!prof) return JsonError("PROFILE_DUMP: profiler not enabled");
	// GetSession() is destructive: the builder's TakeSession does
	// std::move(mSession), leaving the profiler's internal state
	// empty. Refuse to dump while the collector is still running,
	// otherwise subsequent profiling samples would feed into a
	// moved-from session and produce garbage.
	if (prof->IsRunning())
		return JsonError("PROFILE_DUMP: call PROFILE_STOP first");

	ATProfileSession session;
	prof->GetSession(session);

	// Merge all frames' records into a single map keyed by address.
	struct Row { uint32_t addr; uint64_t cycles; uint64_t insns; uint64_t calls; };
	std::vector<Row> rows;
	uint64_t totalCycles = 0;
	uint64_t totalInsns  = 0;

	for (ATProfileFrame* frame : session.mpFrames) {
		if (!frame) continue;
		totalCycles += frame->mTotalCycles;
		totalInsns  += frame->mTotalInsns;
		for (const ATProfileRecord& r : frame->mRecords) {
			// Linear merge — profile tables are typically small
			// (<1000 unique addresses) so O(n²) is fine.
			bool found = false;
			for (Row& ex : rows) {
				if (ex.addr == r.mAddress) {
					ex.cycles += r.mCycles;
					ex.insns  += r.mInsns;
					ex.calls  += r.mCalls;
					found = true;
					break;
				}
			}
			if (!found)
				rows.push_back({ r.mAddress, r.mCycles, r.mInsns, r.mCalls });
		}
	}

	std::sort(rows.begin(), rows.end(),
		[](const Row& a, const Row& b) { return a.cycles > b.cycles; });
	if (rows.size() > top) rows.resize(top);

	std::string arr;
	arr += '[';
	for (size_t i = 0; i < rows.size(); ++i) {
		if (i) arr += ',';
		arr += '{';
		std::string e;
		AddField(e, "addr",   Hex16(rows[i].addr & 0xffff));
		AddU32  (e, "cycles", (uint32_t)rows[i].cycles);
		AddU32  (e, "insns",  (uint32_t)rows[i].insns);
		AddU32  (e, "calls",  (uint32_t)rows[i].calls);
		StripTrailingComma(e);
		arr += e;
		arr += '}';
	}
	arr += ']';

	std::string payload;
	AddU32(payload, "total_cycles", (uint32_t)totalCycles);
	AddU32(payload, "total_insns",  (uint32_t)totalInsns);
	AddU32(payload, "count",        (uint32_t)rows.size());
	payload += "\"hot\":";
	payload += arr;
	return JsonOk(payload);
}

// ===========================================================================
// PROFILE_DUMP_TREE
//
// Hierarchical call tree from kATProfileMode_CallGraph sessions.
// Each entry reports the function address, its parent context index,
// direct (exclusive) counters, and inclusive counters rolled up via
// ATProfileComputeInclusiveStats. Clients render the tree by walking
// the parent chain.
// ===========================================================================

std::string CmdProfileDumpTree(ATSimulator& sim, const std::vector<std::string>& tokens) {
	(void)tokens;
	ATCPUProfiler* prof = sim.GetProfiler();
	if (!prof) return JsonError("PROFILE_DUMP_TREE: profiler not enabled");
	if (prof->IsRunning())
		return JsonError("PROFILE_DUMP_TREE: call PROFILE_STOP first");

	ATProfileSession session;
	prof->GetSession(session);

	if (session.mProfileMode != kATProfileMode_CallGraph)
		return JsonError("PROFILE_DUMP_TREE: profiler is not in callgraph mode");

	// Merge all frames' callgraph records by context index.
	if (session.mpFrames.empty())
		return JsonError("PROFILE_DUMP_TREE: no frames recorded");

	const size_t nContexts = session.mContexts.size();
	std::vector<ATProfileCallGraphRecord> merged(nContexts);
	for (ATProfileFrame* frame : session.mpFrames) {
		if (!frame) continue;
		const size_t n = std::min(merged.size(), frame->mCallGraphRecords.size());
		for (size_t i = 0; i < n; ++i) {
			merged[i].mInsns         += frame->mCallGraphRecords[i].mInsns;
			merged[i].mCycles        += frame->mCallGraphRecords[i].mCycles;
			merged[i].mUnhaltedCycles+= frame->mCallGraphRecords[i].mUnhaltedCycles;
			merged[i].mCalls         += frame->mCallGraphRecords[i].mCalls;
		}
	}

	std::vector<ATProfileCallGraphInclusiveRecord> inclusive(nContexts);
	ATProfileComputeInclusiveStats(inclusive.data(), merged.data(),
		session.mContexts.data(), nContexts);

	std::string arr;
	arr += '[';
	for (size_t i = 0; i < nContexts; ++i) {
		if (i) arr += ',';
		arr += '{';
		std::string e;
		AddU32  (e, "ctx",               (uint32_t)i);
		AddU32  (e, "parent",            session.mContexts[i].mParent);
		AddField(e, "addr",              Hex16(session.mContexts[i].mAddress & 0xffff));
		AddU32  (e, "calls",             merged[i].mCalls);
		AddU32  (e, "excl_cycles",       merged[i].mCycles);
		AddU32  (e, "excl_insns",        merged[i].mInsns);
		AddU32  (e, "incl_cycles",       inclusive[i].mInclusiveCycles);
		AddU32  (e, "incl_insns",        inclusive[i].mInclusiveInsns);
		StripTrailingComma(e);
		arr += e;
		arr += '}';
	}
	arr += ']';

	std::string payload;
	AddU32(payload, "count", (uint32_t)nContexts);
	payload += "\"nodes\":";
	payload += arr;
	return JsonOk(payload);
}

// ===========================================================================
// VERIFIER_STATUS
//   → enabled, flags (hex bitmask)
//
// Bit meanings are documented in src/Altirra/h/verifier.h
// (kATVerifierFlag_*). We pass the bitmask through unchanged —
// client-side constants mirror the enum.
// ===========================================================================

std::string CmdVerifierStatus(ATSimulator& sim, const std::vector<std::string>& tokens) {
	(void)tokens;
	ATCPUVerifier* v = sim.GetVerifier();
	std::string payload;
	AddBool(payload, "enabled", v != nullptr);
	if (v) {
		char buf[16];
		std::snprintf(buf, sizeof buf, "\"$%03x\"", v->GetFlags() & 0xfff);
		AddField(payload, "flags", buf);
	}
	StripTrailingComma(payload);
	return JsonOk(payload);
}

// ===========================================================================
// VERIFIER_SET flags=HEX    (e.g. flags=0x208 enables StackWrap+Recursive)
//                            or flags=off to disable entirely
// ===========================================================================

std::string CmdVerifierSet(ATSimulator& sim, const std::vector<std::string>& tokens) {
	std::string flagsArg;
	for (size_t i = 1; i < tokens.size(); ++i) {
		std::string v;
		if (MatchKey(tokens[i], "flags", v)) { flagsArg = v; continue; }
		return JsonError("VERIFIER_SET: unknown option: " + tokens[i]);
	}
	if (flagsArg.empty())
		return JsonError("VERIFIER_SET: usage: VERIFIER_SET flags=HEX|off");

	if (flagsArg == "off" || flagsArg == "0") {
		sim.SetVerifierEnabled(false);
		return JsonOk();
	}

	uint32_t flags = 0;
	if (!ParseUint(flagsArg, flags))
		return JsonError("VERIFIER_SET: bad flags");

	sim.SetVerifierEnabled(true);
	ATCPUVerifier* v = sim.GetVerifier();
	if (!v) return JsonError("VERIFIER_SET: could not enable verifier");
	v->SetFlags(flags);

	char buf[16];
	std::snprintf(buf, sizeof buf, "\"$%03x\"", flags & 0xfff);
	std::string payload;
	AddField(payload, "flags", buf);
	StripTrailingComma(payload);
	return JsonOk(payload);
}

}  // namespace ATBridge
