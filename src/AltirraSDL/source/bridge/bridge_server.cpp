// AltirraBridge - server core
//
// Owns the listening socket, the per-session token, the frame gate,
// and Phase 1 command dispatch. All work happens on the SDL3 main
// thread inside Poll(); see bridge_server.h for the model.

#include <stdafx.h>

#include "bridge_server.h"
#include "bridge_commands_state.h"
#include "bridge_commands_write.h"
#include "bridge_commands_render.h"
#include "bridge_commands_debug.h"
#include "bridge_commands_debug2.h"
#include "bridge_protocol.h"
#include "bridge_transport.h"

#include "logging.h"
#include "simulator.h"

// Defined in main_sdl3.cpp; flips g_running to false so the SDL3 main
// loop exits cleanly. Declared at file scope (NOT inside the ATBridge
// namespace) so the linker resolves it to the file-scope definition
// over in main_sdl3.cpp.
void ATBridgeRequestAppQuit();

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>

#if defined(_WIN32)
#  include <process.h>
#  include <io.h>
#  define BR_GETPID()        _getpid()
#  define BR_TMPDIR_DEFAULT  "."
#else
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
#  define BR_GETPID()        getpid()
#  define BR_TMPDIR_DEFAULT  "/tmp"
#endif

namespace ATBridge {

// Bumped on any wire-format change. Clients echo this back via the
// HELLO response and may refuse to operate against a higher major
// version than they understand. See AltirraBridge/docs/PROTOCOL.md.
constexpr int kProtocolVersion = 1;

namespace {

// ---------------------------------------------------------------------------
// Module state. Single instance — there is only one bridge server per
// AltirraSDL process.
// ---------------------------------------------------------------------------

struct ServerState {
	bool        enabled = false;       // --bridge was passed and Init succeeded
	bool        authenticated = false; // current client passed HELLO
	bool        writeInitialised = false; // InitWriteCommands has been called against a sim
	Transport   transport;
	std::string token;                 // 32 hex chars (128 bits)
	std::string tokenFilePath;         // path on disk; written by Init
	std::string boundDescription;      // e.g. "tcp:127.0.0.1:54321"

	// Frame gate. While > 0, the simulator is running through queued
	// frames and the bridge does NOT dispatch any commands. Bytes
	// continue to accumulate in recvBuf so that they're ready to
	// process the moment the gate releases. This gives the documented
	// `frame(60); peek(...)` synchronisation contract: the peek waits
	// in the buffer for ~60 frames, then dispatches against a paused
	// simulator at exactly the right cycle.
	uint32_t    framesRemaining = 0;

	// Recv assembly buffer for line-delimited commands.
	std::string recvBuf;

	// Hard cap on inbound buffer size. Phase 1 commands are <100
	// bytes; even Phase 5+ commands sent by the client (not the
	// large RESPONSES from the server) stay tiny. 1 MB is generous
	// and protects against buggy or malicious clients sending junk
	// without ever sending '\n' until OOM.
	static constexpr size_t kMaxRecvBuf = 1024 * 1024;

	// Cap commands per Poll() to bound work and prevent emulation
	// starvation by a chatty client.
	static constexpr int kMaxCommandsPerPoll = 64;
};

ServerState g_state;

// ---------------------------------------------------------------------------
// Token generation and token file
// ---------------------------------------------------------------------------

std::string GenerateToken() {
	// 128 bits of randomness, hex-encoded. We pull directly from
	// std::random_device — no PRNG stretching. On POSIX
	// random_device reads /dev/urandom; on modern MSVC it uses
	// RtlGenRandom. Both are appropriate for a local-only, single-
	// session shared secret. The threat model is unprivileged local
	// processes (file-perms gate the token file at 0600), not
	// remote attackers, so the entropy quality required is modest;
	// 128 bits direct from the OS RNG is comfortably more than
	// enough.
	std::random_device rd;
	auto rd64 = [&rd]() {
		uint64_t hi = (uint64_t)rd();
		uint64_t lo = (uint64_t)rd();
		return (hi << 32) | lo;
	};
	uint64_t a = rd64();
	uint64_t b = rd64();

	char buf[33];
	std::snprintf(buf, sizeof buf, "%016llx%016llx",
		(unsigned long long)a, (unsigned long long)b);
	return std::string(buf);
}

// Choose a directory for the token file. Honors $TMPDIR / $TEMP if set,
// otherwise /tmp on POSIX, current dir on Windows. Best-effort: failure
// to write the token file is logged but not fatal — the user can still
// see the token in the stderr log line.
std::string TmpDir() {
	const char* env = std::getenv("TMPDIR");
	if (!env || !*env) env = std::getenv("TEMP");
	if (!env || !*env) env = std::getenv("TMP");
	if (!env || !*env) env = BR_TMPDIR_DEFAULT;
	return std::string(env);
}

void WriteTokenFile(const std::string& path,
                    const std::string& token,
                    const std::string& boundDescription) {
	FILE* fp = std::fopen(path.c_str(), "w");
	if (!fp) {
		LOG_ERROR("Bridge", "Could not write token file %s", path.c_str());
		return;
	}
	std::fprintf(fp, "%s\n%s\n", boundDescription.c_str(), token.c_str());
	std::fclose(fp);
#if !defined(_WIN32)
	::chmod(path.c_str(), 0600);
#endif
}

void RemoveTokenFile() {
	if (g_state.tokenFilePath.empty()) return;
#if defined(_WIN32)
	::_unlink(g_state.tokenFilePath.c_str());
#else
	::unlink(g_state.tokenFilePath.c_str());
#endif
	g_state.tokenFilePath.clear();
}

// ---------------------------------------------------------------------------
// Disconnect cleanup
//
// Per CLAUDE.md: every acquired state must have a release path. The
// disconnected client may have left state behind that the next client
// would otherwise inherit. We release everything the bridge owns and
// leave everything else (running/paused state, loaded media,
// configuration) untouched — the new client should see the simulator
// in whatever state it was in, just without any debris from the
// previous session.
//
// Phase 3 will extend this with joystick / key / CONSOL releases.
// ---------------------------------------------------------------------------

void OnClientDisconnected(ATSimulator& sim) {
	g_state.authenticated = false;
	g_state.recvBuf.clear();
	// Cancel any active frame gate. Without this a client that
	// crashes mid-FRAME-loop would leave the next client connecting
	// to a sim that's still counting down through queued frames.
	// We do NOT call sim.Pause() here — leave the sim in whatever
	// running state it currently has so the new client sees
	// consistent state.
	g_state.framesRemaining = 0;
	// Release every input the disconnected client may have left
	// asserted: joystick directions/fire for all 4 ports, console
	// switches, and the POKEY key matrix. The bridge is the sole
	// owner of the PIA input slot it allocated, so this never
	// stomps on real-gamepad or real-keyboard input.
	if (g_state.writeInitialised)
		CleanupInjectedInput(sim);
}

// ---------------------------------------------------------------------------
// Phase 1 command dispatch
// ---------------------------------------------------------------------------

std::string DispatchCommand(const std::string& line, ATSimulator& sim) {
	auto tokens = TokenizeCommand(line);
	if (tokens.empty())
		return JsonError("empty command");

	const std::string& verb = tokens[0];

	// HELLO must always be processable, even before authentication.
	if (verb == "HELLO") {
		if (tokens.size() < 2)
			return JsonError("HELLO requires <token>");
		if (tokens[1] != g_state.token) {
			LOG_INFO("Bridge", "HELLO with wrong token from client (rejecting)");
			return JsonError("bad token");
		}
		g_state.authenticated = true;
		// Echo a small payload so the client can confirm and pin the
		// protocol version it's talking to.
		return JsonOk(
			"\"protocol\":" + std::to_string(kProtocolVersion) + ","
			"\"server\":\"AltirraSDL\","
			"\"paused\":" + std::string(sim.IsPaused() ? "true" : "false"));
	}

	// Everything else requires authentication.
	if (!g_state.authenticated)
		return JsonError("auth required");

	if (verb == "PING") {
		return JsonOk();
	}

	if (verb == "PAUSE") {
		sim.Pause();
		g_state.framesRemaining = 0;
		return JsonOk();
	}

	if (verb == "RESUME") {
		sim.Resume();
		g_state.framesRemaining = 0;
		return JsonOk();
	}

	if (verb == "FRAME") {
		uint32_t n = 1;
		if (tokens.size() >= 2) {
			if (!ParseUint(tokens[1], n))
				return JsonError("FRAME: bad count");
			if (n == 0)
				return JsonError("FRAME: count must be >= 1");
			if (n > 1000000u)
				return JsonError("FRAME: count too large");
		}
		// Resume the simulator if it's paused and arm the gate. The
		// main loop's per-frame OnFrameCompleted() hook decrements
		// the counter once per produced frame and re-pauses when it
		// hits zero. The FRAME response is sent immediately; the
		// client's NEXT command will sit in the recv buffer
		// (un-dispatched) until the gate releases — see the gate
		// check at the top of TryDispatchOneLine().
		g_state.framesRemaining = n;
		sim.Resume();
		std::string payload = "\"frames\":" + std::to_string(n);
		return JsonOk(payload);
	}

	if (verb == "QUIT") {
		// Asks main_sdl3.cpp to flip g_running to false. The simulator
		// finishes its current frame, the main loop exits, the bridge
		// is Shutdown()'d in the teardown path, and AltirraSDL exits.
		ATBridgeRequestAppQuit();
		return JsonOk();
	}

	// ----- Phase 2: state-read commands. Pure reads, no side effects.
	// All implemented in bridge_commands_state.cpp.
	if (verb == "REGS")    return CmdRegs(sim, tokens);
	if (verb == "PEEK")    return CmdPeek(sim, tokens);
	if (verb == "PEEK16")  return CmdPeek16(sim, tokens);
	if (verb == "ANTIC")   return CmdAntic(sim, tokens);
	if (verb == "GTIA")    return CmdGtia(sim, tokens);
	if (verb == "POKEY")   return CmdPokey(sim, tokens);
	if (verb == "PIA")     return CmdPia(sim, tokens);
	if (verb == "DLIST")   return CmdDlist(sim, tokens);
	if (verb == "HWSTATE") return CmdHwstate(sim, tokens);
	if (verb == "PALETTE")          return CmdPalette(sim, tokens);
	if (verb == "PALETTE_LOAD_ACT") return CmdPaletteLoadAct(sim, tokens);
	if (verb == "PALETTE_RESET")    return CmdPaletteReset(sim, tokens);

	// ----- Phase 3: state-write and input-injection commands.
	// All implemented in bridge_commands_write.cpp.
	if (verb == "POKE")       return CmdPoke(sim, tokens);
	if (verb == "POKE16")     return CmdPoke16(sim, tokens);
	if (verb == "HWPOKE")     return CmdHwPoke(sim, tokens);
	if (verb == "MEMDUMP")    return CmdMemDump(sim, tokens);
	if (verb == "MEMLOAD")    return CmdMemLoad(sim, tokens);
	if (verb == "JOY")        return CmdJoy(sim, tokens);
	if (verb == "KEY")        return CmdKey(sim, tokens);
	if (verb == "CONSOL")     return CmdConsol(sim, tokens);
	if (verb == "BOOT")       return CmdBoot(sim, tokens);
	if (verb == "BOOT_BARE")  return CmdBootBare(sim, tokens);
	if (verb == "MOUNT")      return CmdMount(sim, tokens);
	if (verb == "COLD_RESET") return CmdColdReset(sim, tokens);
	if (verb == "WARM_RESET") return CmdWarmReset(sim, tokens);
	if (verb == "STATE_SAVE") return CmdStateSave(sim, tokens);
	if (verb == "STATE_LOAD") return CmdStateLoad(sim, tokens);

	// Phase 4: rendering commands
	if (verb == "SCREENSHOT")   return CmdScreenshot (sim, tokens);
	if (verb == "RAWSCREEN")    return CmdRawScreen  (sim, tokens);
	if (verb == "RENDER_FRAME") return CmdRenderFrame(sim, tokens);

	// Phase 5a: debugger/profiler commands
	if (verb == "DISASM")      return CmdDisasm    (sim, tokens);
	if (verb == "HISTORY")     return CmdHistory   (sim, tokens);
	if (verb == "EVAL")        return CmdEval      (sim, tokens);
	if (verb == "CALLSTACK")   return CmdCallStack (sim, tokens);
	if (verb == "MEMMAP")      return CmdMemMap    (sim, tokens);
	if (verb == "BANK_INFO")   return CmdBankInfo  (sim, tokens);
	if (verb == "CART_INFO")   return CmdCartInfo  (sim, tokens);
	if (verb == "PMG")         return CmdPmg       (sim, tokens);
	if (verb == "AUDIO_STATE") return CmdAudioState(sim, tokens);

	// Phase 5b: breakpoints, symbols, memsearch, profiler, verifier
	if (verb == "BP_SET")            return CmdBpSet         (sim, tokens);
	if (verb == "BP_CLEAR")          return CmdBpClear       (sim, tokens);
	if (verb == "BP_CLEAR_ALL")      return CmdBpClearAll    (sim, tokens);
	if (verb == "BP_LIST")           return CmdBpList        (sim, tokens);
	if (verb == "WATCH_SET")         return CmdWatchSet      (sim, tokens);
	if (verb == "SYM_LOAD")          return CmdSymLoad       (sim, tokens);
	if (verb == "SYM_RESOLVE")       return CmdSymResolve    (sim, tokens);
	if (verb == "SYM_LOOKUP")        return CmdSymLookup     (sim, tokens);
	if (verb == "MEMSEARCH")         return CmdMemSearch     (sim, tokens);
	if (verb == "PROFILE_START")     return CmdProfileStart  (sim, tokens);
	if (verb == "PROFILE_STOP")      return CmdProfileStop   (sim, tokens);
	if (verb == "PROFILE_STATUS")    return CmdProfileStatus (sim, tokens);
	if (verb == "PROFILE_DUMP")      return CmdProfileDump   (sim, tokens);
	if (verb == "PROFILE_DUMP_TREE") return CmdProfileDumpTree(sim, tokens);
	if (verb == "VERIFIER_STATUS")   return CmdVerifierStatus(sim, tokens);
	if (verb == "VERIFIER_SET")      return CmdVerifierSet   (sim, tokens);

	return JsonError(std::string("unknown command: ") + verb);
}

// Append received bytes to recvBuf, enforcing the per-connection
// inbound cap. Returns false (and drops the client) if the cap would
// be exceeded — protects against a misbehaving client sending bytes
// without ever sending '\n' until OOM.
bool AppendRecvBytes(ATSimulator& sim, const char* buf, size_t got) {
	if (g_state.recvBuf.size() + got > ServerState::kMaxRecvBuf) {
		LOG_ERROR("Bridge", "client exceeded inbound buffer cap (%zu bytes); dropping",
			ServerState::kMaxRecvBuf);
		g_state.transport.DropClient();
		OnClientDisconnected(sim);
		return false;
	}
	g_state.recvBuf.append(buf, got);
	return true;
}

// Try to pull one complete (newline-terminated) line out of the recv
// buffer and dispatch it.
//
// Returns true if work was done (a line was consumed and either
// dispatched or skipped as empty), false otherwise. "False" can mean
// either "no complete line in buffer yet" or "the next line is gated
// behind an active FRAME counter and we are deferring it" — either
// way the caller should stop trying to dispatch and either read more
// bytes or return.
bool TryDispatchOneLine(ATSimulator& sim) {
	auto nl = g_state.recvBuf.find('\n');
	if (nl == std::string::npos)
		return false;

	// While the frame gate is active, leave the line untouched in
	// the buffer. We'll dispatch it on a future Poll() once the gate
	// releases. This implements the documented synchronisation
	// contract: a client that sends `frame(60); peek(...)` sees the
	// peek dispatched against a paused simulator at exactly the
	// right cycle, not at the start of the frame run.
	//
	// Note: this means PAUSE / RESUME / QUIT also wait their turn.
	// Interrupting an in-flight FRAME run requires closing the
	// socket (which clears the gate via OnClientDisconnected) and
	// reconnecting.
	if (g_state.framesRemaining > 0)
		return false;

	std::string line = g_state.recvBuf.substr(0, nl);
	g_state.recvBuf.erase(0, nl + 1);
	// Strip trailing CR for clients that send CRLF.
	if (!line.empty() && line.back() == '\r')
		line.pop_back();

	if (line.empty()) {
		// Empty line is a no-op; don't even respond. Lets clients
		// send a bare \n as a keepalive.
		return true;
	}

	std::string response = DispatchCommand(line, sim);
	IoResult sr = g_state.transport.SendAll(response.data(), response.size());
	if (sr == IoResult::Error) {
		g_state.transport.DropClient();
		OnClientDisconnected(sim);
	}
	return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool Init(const std::string& addrSpec) {
	if (g_state.enabled) {
		LOG_INFO("Bridge", "Init called twice; ignoring");
		return true;
	}

	if (!g_state.transport.Listen(addrSpec, g_state.boundDescription)) {
		LOG_ERROR("Bridge", "Listen() failed for spec '%s'", addrSpec.c_str());
		return false;
	}

	g_state.token = GenerateToken();

	// Token file path: <tmpdir>/altirra-bridge-<pid>.token
	{
		char buf[64];
		std::snprintf(buf, sizeof buf, "altirra-bridge-%d.token",
			(int)BR_GETPID());
		g_state.tokenFilePath = TmpDir() + "/" + buf;
	}
	WriteTokenFile(g_state.tokenFilePath, g_state.token, g_state.boundDescription);

	// Two banner lines on stderr (LOG_INFO routes there). Stdout is
	// reserved for any future emulator output that a client might
	// want to capture.
	LOG_INFO("Bridge", "listening on %s", g_state.boundDescription.c_str());
	LOG_INFO("Bridge", "token-file: %s", g_state.tokenFilePath.c_str());
	LOG_INFO("Bridge", "token: %s", g_state.token.c_str());

	g_state.enabled = true;
	g_state.authenticated = false;
	g_state.framesRemaining = 0;
	g_state.recvBuf.clear();
	return true;
}

void Shutdown(ATSimulator& sim) {
	if (!g_state.enabled && !g_state.transport.IsListening() && !g_state.writeInitialised)
		return;
	if (g_state.writeInitialised) {
		ShutdownWriteCommands(sim);
		g_state.writeInitialised = false;
	}
	g_state.transport.Shutdown();
	RemoveTokenFile();
	g_state.enabled = false;
	g_state.authenticated = false;
	g_state.framesRemaining = 0;
	g_state.recvBuf.clear();
	LOG_INFO("Bridge", "shutdown");
}

void Poll(ATSimulator& sim, ATUIState& /*ui*/) {
	if (!g_state.enabled)
		return;

	// Lazy-init the Phase 3 write-side state on the first Poll
	// after Init(). The bridge module deliberately does not store a
	// long-lived simulator reference, so we wait until we have one
	// in hand here before allocating the PIA input slot for
	// joystick injection.
	if (!g_state.writeInitialised) {
		InitWriteCommands(sim);
		g_state.writeInitialised = true;
	}

	// Step 1. If we already have a client, probe it for data (or for
	// a peer-close) BEFORE trying to accept a new connection. This
	// matters: if a client cleanly disconnects and a new one connects
	// in the same frame, we need to reap the dead one first so the
	// single-client slot is free for the new connection. Without this
	// the second client races into TryAccept() and gets rejected by
	// the "we already have a client" branch.
	if (g_state.transport.HasClient()) {
		char buf[4096];
		size_t got = 0;
		IoResult rr = g_state.transport.Recv(buf, sizeof buf, &got);
		if (rr == IoResult::Ok && got > 0) {
			if (!AppendRecvBytes(sim, buf, got))
				return;  // dropped on cap exceeded
		} else if (rr == IoResult::PeerClosed || rr == IoResult::Error) {
			g_state.transport.DropClient();
			OnClientDisconnected(sim);
		}
		// WouldBlock = client still alive, just nothing to read.
	}

	// Step 2. Accept a new client if the slot is free.
	g_state.transport.TryAccept();

	if (!g_state.transport.HasClient())
		return;

	// Step 3. Drain any pending bytes and dispatch up to N commands
	// per Poll() to bound work and prevent emulation starvation.
	for (int i = 0; i < ServerState::kMaxCommandsPerPoll; ++i) {
		char buf[4096];
		size_t got = 0;
		IoResult rr = g_state.transport.Recv(buf, sizeof buf, &got);
		if (rr == IoResult::Ok && got > 0) {
			if (!AppendRecvBytes(sim, buf, got))
				return;
		} else if (rr == IoResult::WouldBlock) {
			// Nothing more to read right now. Dispatch whatever
			// complete lines we already have (subject to the gate),
			// then return.
			while (TryDispatchOneLine(sim)) {}
			return;
		} else if (rr == IoResult::PeerClosed || rr == IoResult::Error) {
			// Drain any complete lines we already have first, so a
			// client that sends "QUIT\n" then immediately closes
			// still gets the QUIT processed (unless gated).
			while (TryDispatchOneLine(sim)) {}
			g_state.transport.DropClient();
			OnClientDisconnected(sim);
			return;
		}

		// Dispatch one line per outer iteration so the cap is
		// command-based, not byte-based.
		TryDispatchOneLine(sim);
	}
}

void OnFrameCompleted(ATSimulator& sim) {
	if (!g_state.enabled || g_state.framesRemaining == 0)
		return;
	--g_state.framesRemaining;
	if (g_state.framesRemaining == 0) {
		sim.Pause();
	}
}

bool IsEnabled() {
	return g_state.enabled;
}

}  // namespace ATBridge

// ---------------------------------------------------------------------------
// QUIT plumbing
//
// The bridge needs a way to ask the SDL3 main loop to exit cleanly
// without depending directly on main_sdl3.cpp internals (g_running).
// We forward-declare a small extern function here and define it inside
// main_sdl3.cpp where g_running lives. This is the only coupling
// between the two files beyond the integration points listed in the
// plan.
// ---------------------------------------------------------------------------
