// Fork extension: import remote games and large packs into the Game Library.
#pragma once
#include <cstdint>
#include <string>

struct ATUILibraryURLStatus {
	bool pending = false;
	bool cancelling = false;
	bool open = false;
	bool added = false;
	bool scanning = false;
	uint64_t received = 0;
	uint64_t total = 0;
	uint64_t files = 0;
	std::string error;
	std::string path;
	std::string destination;
};

void ATUIShowLibraryURLDialog();
bool ATUIStartLibraryURLImport(const char* url);
bool ATUISetLibraryURLDestination(const char* path);
void ATUICancelLibraryURLImport();
ATUILibraryURLStatus ATUIGetLibraryURLStatus();
void ATUIPollLibraryURLImport();
void ATUIRenderLibraryURLDialog();
void ATUIShutdownLibraryURLImport();
