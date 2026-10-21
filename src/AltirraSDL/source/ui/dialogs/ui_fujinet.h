// AltirraSDL fork: native NetSIO and optional local FujiNet-PC management.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

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
bool ATUIFujiNetRunFile(const char* pathOrURL);
void ATUIFujiNetCancelFile();
bool ATUIFujiNetReconnect();
bool ATUIFujiNetDisconnect();
bool ATUIFujiNetBoot();
bool ATUIFujiNetEnableLocalD1();
// One click: install FujiNet-PC when needed, then start it and boot.
bool ATUIFujiNetAutoSetup();
bool ATUIFujiNetInstallRuntime(bool thenStart);
void ATUIFujiNetCancelInstall();
bool ATUIFujiNetRecheckLibraries();
// Called once after startup media is loaded. With the user's "start with
// AltirraSDL" preference, starts FujiNet-PC (and boots it in Desktop Mode)
// unless the command line loaded a program.
void ATUIFujiNetLaunchStartup(bool commandLineBootImage);
bool ATUIFujiNetSetAutoStart(bool enabled);

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
	bool filePending = false;
	bool installing = false;
	bool autoSetupPending = false;
	bool autoStart = false;
	uint64_t installReceived = 0;
	uint64_t installTotal = 0;
	uint64_t fileReceived = 0;
	uint64_t fileTotal = 0;
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
	std::string runtimeVersion;
	std::vector<std::string> missingLibraries;
};
ATFujiNetUIStatus ATUIGetFujiNetStatus();
