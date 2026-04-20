//	AltirraSDL - Background lobby HTTP worker (impl)

#include <stdafx.h>

#include "ui_netplay_lobby_worker.h"

namespace ATNetplayUI {

LobbyWorker::LobbyWorker() = default;

LobbyWorker::~LobbyWorker() { Stop(); }

void LobbyWorker::Start() {
	if (mRunning.exchange(true)) return;
	mStop.store(false);
	mThread = std::thread(&LobbyWorker::ThreadMain, this);
}

void LobbyWorker::Stop() {
	if (!mRunning.exchange(false)) {
		if (mThread.joinable()) mThread.join();
		return;
	}
	mStop.store(true);
	mCv.notify_all();
	if (mThread.joinable()) mThread.join();

	// Drain remaining queues so subsequent Poll() sees no stale data.
	std::lock_guard<std::mutex> lk(mMu);
	mInQueue.clear();
	mOutQueue.clear();
	mInFlight.store(0);
}

bool LobbyWorker::Post(LobbyRequest req, const std::string& source) {
	if (!mRunning.load()) return false;
	{
		std::lock_guard<std::mutex> lk(mMu);
		mInQueue.push_back({std::move(req), source});
		++mInFlight;
	}
	mCv.notify_one();
	return true;
}

void LobbyWorker::Poll(const std::function<void(LobbyResult&)>& onResult) {
	std::deque<LobbyResult> local;
	{
		std::lock_guard<std::mutex> lk(mMu);
		local.swap(mOutQueue);
	}
	for (auto& r : local) onResult(r);
}

size_t LobbyWorker::InFlightCount() const { return mInFlight.load(); }

void LobbyWorker::ThreadMain() {
	ATNetplay::LobbyClient client;
	for (;;) {
		Queued q;
		{
			std::unique_lock<std::mutex> lk(mMu);
			mCv.wait(lk, [&]{ return mStop.load() || !mInQueue.empty(); });
			if (mStop.load() && mInQueue.empty()) return;
			q = std::move(mInQueue.front());
			mInQueue.pop_front();
		}

		client.SetEndpoint(q.req.endpoint);
		LobbyResult out{};
		out.op          = q.req.op;
		out.tag         = q.req.tag;
		out.sourceLobby = q.source;

		switch (q.req.op) {
			case LobbyOp::List:
				out.ok = client.List(out.sessions);
				break;
			case LobbyOp::Create:
				out.ok = client.Create(q.req.createReq, out.create);
				break;
			case LobbyOp::Heartbeat:
				out.ok = client.Heartbeat(q.req.sessionId, q.req.token,
					q.req.playerCount, q.req.state);
				break;
			case LobbyOp::Delete:
				out.ok = client.Delete(q.req.sessionId, q.req.token);
				break;
			case LobbyOp::Stats:
				out.ok = client.Stats(out.stats);
				break;
		}
		if (!out.ok) out.error = client.LastError();
		out.httpStatus = client.LastStatus();

		{
			std::lock_guard<std::mutex> lk(mMu);
			mOutQueue.push_back(std::move(out));
			--mInFlight;
		}
	}
}

} // namespace ATNetplayUI
