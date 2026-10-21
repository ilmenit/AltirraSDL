// Optional managed FujiNet-PC runtime. All methods run on the main thread.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

struct ATFujiNetPCOptions {
	std::string executable;
	std::string stateDirectory;
	uint16_t netsioPort = 9997;
	uint16_t webPort = 8000;
};

struct ATFujiNetPCStatus {
	bool running = false;
	bool stopping = false;
	bool logTruncated = false;
	int exitCode = 0;
	unsigned restarts = 0;
	std::string error;
	std::string logTail;
};

class ATFujiNetPC {
public:
	ATFujiNetPC();
	~ATFujiNetPC();
	ATFujiNetPC(const ATFujiNetPC&) = delete;
	ATFujiNetPC& operator=(const ATFujiNetPC&) = delete;
	static bool Available();
	bool Start(const ATFujiNetPCOptions& options);
	void Stop();
	// Called on the caller/main thread after the previous child exits and
	// before an exit-75 replacement starts; the data lease remains held.
	// The callback may reset transports, but must not reenter this controller.
	// A thrown callback aborts restart, reports the error and releases ownership.
	void Poll(const std::function<void()>& beforeRestart = {});
	// Drain output while a worker holds the managed runtime data lease.
	void PollOutput();
	void Shutdown();
	const ATFujiNetPCStatus& GetStatus() const;
private:
	struct Impl;
	std::unique_ptr<Impl> mpImpl;
};
