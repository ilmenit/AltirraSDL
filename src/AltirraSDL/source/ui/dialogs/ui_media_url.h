// AltirraSDL fork extension; native HTTP(S) image import.
#pragma once
#include <vd2/system/vdtypes.h>
#include <settings.h>

void ATUIShowMediaURLDialog();
// Called only on the main thread. boot selects the existing Boot/Open path.
void ATUIStartMediaURLDownload(const char* url, bool boot,
	uint32 profile = kATProfileId_Invalid);
void ATUIPollMediaURLDownload();
void ATUIRenderMediaURLDialog();
void ATUIShutdownMediaURLDownload();
// Main-thread status for UI automation and command integration.
bool ATUIIsMediaURLDownloadPending();
const char* ATUIGetMediaURLDownloadError();
