//	AltirraSDL - Device configuration shared types
//	Shared between ui_devconfig.cpp and ui_devconfig_devices.cpp.

#pragma once

#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <at/atcore/propertyset.h>

class IATDevice;

// =========================================================================
// Dialog state — persistent across frames while dialog is open
// =========================================================================

struct ATDeviceConfigState {
	bool open = false;
	bool justOpened = false;
	IATDevice *pDev = nullptr;
	ATPropertySet props;
	VDStringA configTag;
	VDStringA deviceName;

	// Scratch buffers for text inputs
	char pathBuf[1024] = {};
	char addrBuf[256] = {};
	char svcBuf[256] = {};
	char portBuf[32] = {};
	char baudBuf[32] = {};
	char mappingBuf[64] = {};
	// Extra path buffers for HostFS (4 paths need >32 bytes each)
	char extraPaths[3][512] = {};

	// Combo selections
	int combo[16] = {};
	// Checkbox states
	bool check[16] = {};
	// Int values
	int intVal[8] = {};

	void Reset() {
		open = false;
		justOpened = false;
		pDev = nullptr;
		props.Clear();
		configTag.clear();
		deviceName.clear();
		memset(pathBuf, 0, sizeof(pathBuf));
		memset(addrBuf, 0, sizeof(addrBuf));
		memset(svcBuf, 0, sizeof(svcBuf));
		memset(portBuf, 0, sizeof(portBuf));
		memset(baudBuf, 0, sizeof(baudBuf));
		memset(mappingBuf, 0, sizeof(mappingBuf));
		memset(extraPaths, 0, sizeof(extraPaths));
		memset(combo, 0, sizeof(combo));
		memset(check, 0, sizeof(check));
		memset(intVal, 0, sizeof(intVal));
	}
};

// =========================================================================
// Helper functions (defined in ui_devconfig.cpp)
// =========================================================================

VDStringA WToU8(const wchar_t *s);
VDStringW U8ToW(const char *s);

// Shared dialog state instance
extern ATDeviceConfigState g_devCfg;

// =========================================================================
// Per-device dialog renderers (defined in ui_devconfig_devices.cpp)
// Each returns true if user clicked OK (apply settings)
// =========================================================================

bool RenderCovoxConfig(ATPropertySet& props, ATDeviceConfigState& st);
bool Render850Config(ATPropertySet& props, ATDeviceConfigState& st);
bool Render850FullConfig(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderModemConfig(ATPropertySet& props, ATDeviceConfigState& st, bool fullEmu, bool is835, bool hasConnectRate = true);
bool RenderSX212Config(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderPocketModemConfig(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderHardDiskConfig(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderBlackBoxConfig(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderVBXEConfig(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderSoundBoardConfig(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderDiskDriveFullConfig(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderSIDE3Config(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderXEP80Config(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderCorvusConfig(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderVeronicaConfig(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderDongleConfig(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderKMKJZIDEConfig(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderKMKJZIDE2Config(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderMyIDE2Config(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderHDVirtFATConfig(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderPCLinkConfig(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderHostFSConfig(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderCustomDeviceConfig(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderComputerEyesConfig(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderVideoStillImageConfig(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderNetSerialConfig(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderPipeSerialConfig(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderPrinterConfig(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderPrinterHLEConfig(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderPercomConfig(ATPropertySet& props, ATDeviceConfigState& st, bool atMode, bool atSPDMode);
bool RenderAMDCConfig(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderBlackBoxFloppyConfig(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderHappy810Config(ATPropertySet& props, ATDeviceConfigState& st);
bool Render815Config(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderATR8000Config(ATPropertySet& props, ATDeviceConfigState& st);
bool Render1020Config(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderMultiplexerConfig(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderParFileWriterConfig(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderKarinMaxiDriveConfig(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderDragonCartConfig(ATPropertySet& props, ATDeviceConfigState& st);
bool RenderGenericConfig(ATPropertySet& props, ATDeviceConfigState& st);
