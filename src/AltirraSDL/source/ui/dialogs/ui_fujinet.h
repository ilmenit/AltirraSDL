// AltirraSDL fork: native NetSIO and optional local FujiNet-PC management.
#pragma once
#include <cstdint>
#include <string>

void ATUIShowFujiNetDialog();
void ATUIRenderFujiNetDialog();
void ATUIPollFujiNet();
void ATUIShutdownFujiNet();

// Main-thread actions shared by the panel and UI automation.
bool ATUIFujiNetSetRuntime(const char* executable, unsigned webPort);
bool ATUIFujiNetConfigureListener(bool allowLAN, unsigned port,
	unsigned syncTimeoutMs = 100);
bool ATUIFujiNetStartRuntime();
bool ATUIFujiNetStartAndBoot();
void ATUIFujiNetStopRuntime();
bool ATUIFujiNetReconnect();
bool ATUIFujiNetDisconnect();
bool ATUIFujiNetBoot();
bool ATUIFujiNetEnableLocalD1();

struct ATFujiNetUIStatus {
	bool open = false;
	bool processAvailable = false;
	bool running = false;
	bool stopping = false;
	bool present = false;
	bool listening = false;
	bool connected = false;
	bool localD1Enabled = false;
	bool fastBoot = false;
	bool bootPending = false;
	unsigned port = 9997;
	unsigned webPort = 8000;
	unsigned restarts = 0;
	int exitCode = 0;
	uint64_t rxPackets = 0;
	uint64_t txPackets = 0;
	uint64_t droppedPackets = 0;
	uint64_t syncTimeouts = 0;
	std::string executable;
	std::string stateDirectory;
	std::string peer;
	std::string error;
	std::string logTail;
};
ATFujiNetUIStatus ATUIGetFujiNetStatus();
