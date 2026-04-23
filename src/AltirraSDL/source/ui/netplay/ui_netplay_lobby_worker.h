//	AltirraSDL - Background lobby HTTP worker
//
//	The LobbyClient's Create/List/Heartbeat/Delete calls are blocking
//	POSIX-socket operations with a 5 s deadline.  Running them on the
//	UI thread would stutter the emulator whenever a DNS lookup or TCP
//	handshake hits that ceiling.  This worker moves the I/O to a
//	dedicated std::thread; the UI enqueues Requests and drains
//	Results once per frame.
//
//	Lifecycle:
//	  Start() — spawn the thread (idempotent).
//	  Stop()  — join and release the thread.
//	  Post()  — queue a request from the UI thread.
//	  Poll()  — drain completed results to the supplied handler.
//
//	Safe to Start() more than once; safe to destroy without Stop() —
//	the destructor joins.  Platform-agnostic: uses only <thread>,
//	<mutex>, <condition_variable>.

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "netplay/lobby_client.h"

namespace ATNetplayUI {

// Identifies the call the worker should make.  Each enum value maps
// to one method on ATNetplay::LobbyClient.  Parameters are packed
// into Request::{str1, str2, i1, createReq} as appropriate.
enum class LobbyOp {
	List,
	Create,
	Heartbeat,
	Delete,
	Stats,
	// v4 two-sided punch: joiner-side POST to announce own candidates
	// to the lobby so the host can pre-open its NAT pinhole.  Uses
	// req.sessionId (target host's session), req.token (joiner's
	// sessionNonce hex), req.state (joiner handle), req.createReq.
	// candidates (joiner candidates, first element used).
	PeerHint,
};

struct LobbyRequest {
	LobbyOp                       op = LobbyOp::List;
	ATNetplay::LobbyEndpoint      endpoint;
	ATNetplay::LobbyCreateRequest createReq;   // op == Create
	std::string                   sessionId;   // heartbeat/delete
	std::string                   token;       // heartbeat/delete
	int                           playerCount = 1;  // heartbeat
	std::string                   state;       // heartbeat: "" | "waiting" | "playing"
	uint32_t                      tag = 0;     // caller cookie (round-trip)
};

struct LobbyResult {
	LobbyOp                             op = LobbyOp::List;
	bool                                ok = false;
	std::string                         error;
	int                                 httpStatus = 0;  // 0 = network-level failure
	uint32_t                            tag = 0;

	// Populated depending on op:
	std::vector<ATNetplay::LobbySession> sessions;  // List
	ATNetplay::LobbyCreateResponse       create;    // Create
	ATNetplay::LobbyStats                stats;     // Stats
	std::string                          sourceLobby;  // section name

	// v4 two-sided punch: hints delivered by the lobby on a Heartbeat
	// response (pre-v4 servers always leave this empty).  The main
	// thread hands each one to Coordinator::IngestPeerHint().
	std::vector<ATNetplay::LobbySessionHint> hints;

	// NAT-PMP / PCP port mapping acquired during a Create op (empty
	// protocol string when no mapping was granted).  The main thread
	// stashes these on HostedGame so the delete path can release the
	// router-forward politely.
	std::string natPmpProtocol;
	std::string natPmpExternalIp;
	uint16_t    natPmpExternalPort = 0;
	uint16_t    natPmpInternalPort = 0;
	uint32_t    natPmpLifetimeSec  = 0;
};

class LobbyWorker {
public:
	LobbyWorker();
	~LobbyWorker();

	LobbyWorker(const LobbyWorker&)            = delete;
	LobbyWorker& operator=(const LobbyWorker&) = delete;

	void Start();
	void Stop();
	bool IsRunning() const { return mRunning.load(); }

	// Enqueue a request.  Returns false if the worker is shutting down.
	bool Post(LobbyRequest req, const std::string& sourceLobby = "");

	// Drain any results completed since the last call.  The callable
	// is invoked from the caller's thread.
	void Poll(const std::function<void(LobbyResult&)>& onResult);

	// How many requests are currently queued or in flight.  Used by
	// the UI to show a "refreshing..." indicator.
	size_t InFlightCount() const;

private:
	void ThreadMain();

	struct Queued {
		LobbyRequest req;
		std::string  source;
	};

	mutable std::mutex      mMu;
	std::condition_variable mCv;
	std::deque<Queued>      mInQueue;
	std::deque<LobbyResult> mOutQueue;
	std::atomic<bool>       mRunning{false};
	std::atomic<bool>       mStop{false};
	std::atomic<size_t>     mInFlight{0};
	std::thread             mThread;
};

} // namespace ATNetplayUI
