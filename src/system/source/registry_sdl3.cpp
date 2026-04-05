//	Altirra - Atari 800/800XL/5200 emulator
//	System library - registry provider for non-Windows
//
//	Uses VDRegistryProviderMemory as the in-memory store.  Config is
//	persisted to ~/.config/altirra/settings.ini using the same INI-like
//	format as Windows Altirra's portable mode (ATUILoadRegistry /
//	ATUISaveRegistry from uiregistry.cpp).
//
//	On startup the INI file is loaded into the memory provider.  On
//	exit (via ATRegistryFlushToDisk()) it is written back.

#include <stdafx.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <vd2/system/registry.h>
#include <vd2/system/registrymemory.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>

// Forward-declare the INI load/save functions from uiregistry.cpp.
// These work with whichever IVDRegistryProvider is currently active.
void ATUILoadRegistry(const wchar_t *path);
void ATUISaveRegistry(const wchar_t *fnpath);

// -------------------------------------------------------------------------
// Config directory and file path
// -------------------------------------------------------------------------

static VDStringA s_configDir;
static VDStringW s_configPath;

VDStringA ATGetConfigDir() {
	if (s_configDir.empty()) {
		// XDG Base Directory: use $XDG_CONFIG_HOME or ~/.config
		const char *xdgConfig = getenv("XDG_CONFIG_HOME");
		VDStringA dir;
		if (xdgConfig && *xdgConfig) {
			dir = xdgConfig;
		} else {
			const char *home = getenv("HOME");
			if (home && *home) {
				dir = home;
				dir += "/.config";
			} else {
				dir = "/tmp";
			}
		}
		dir += "/altirra";

		// Ensure directory exists
		mkdir(dir.c_str(), 0755);

		s_configDir = dir;
	}
	return s_configDir;
}

static const VDStringW& GetConfigPath() {
	if (s_configPath.empty()) {
		VDStringA filePath = ATGetConfigDir();
		filePath += "/settings.ini";
		s_configPath = VDTextU8ToW(filePath);
	}
	return s_configPath;
}

// -------------------------------------------------------------------------
// Provider and global state
// -------------------------------------------------------------------------

static IVDRegistryProvider *g_pRegistryProvider;
static VDRegistryProviderMemory *g_pMemoryProvider;

VDString VDRegistryAppKey::s_appbase;

IVDRegistryProvider *VDGetDefaultRegistryProvider() {
	// Create the memory provider once and load persisted settings into it
	static VDRegistryProviderMemory sDefaultProvider;
	static bool sLoaded = false;
	if (!sLoaded) {
		sLoaded = true;
		g_pMemoryProvider = &sDefaultProvider;
	}
	return &sDefaultProvider;
}

IVDRegistryProvider *VDGetRegistryProvider() {
	if (!g_pRegistryProvider)
		g_pRegistryProvider = VDGetDefaultRegistryProvider();
	return g_pRegistryProvider;
}

void VDSetRegistryProvider(IVDRegistryProvider *provider) {
	g_pRegistryProvider = provider;
}

// -------------------------------------------------------------------------
// Public: load from disk (call after setDefaultKey)
// -------------------------------------------------------------------------

void ATRegistryLoadFromDisk() {
	const VDStringW& path = GetConfigPath();
	try {
		// Check if file exists before trying to parse it
		FILE *f = fopen(VDTextWToU8(path).c_str(), "r");
		if (f) {
			fclose(f);
			ATUILoadRegistry(path.c_str());
			fprintf(stderr, "[AltirraSDL] Settings loaded from: %s\n",
				VDTextWToU8(path).c_str());
		} else {
			fprintf(stderr, "[AltirraSDL] No settings file found, using defaults\n");
		}
	} catch (...) {
		fprintf(stderr, "[AltirraSDL] Warning: failed to load settings from %s\n",
			VDTextWToU8(path).c_str());
	}
}

// -------------------------------------------------------------------------
// Public: flush to disk (call on exit or periodically)
// -------------------------------------------------------------------------

void ATRegistryFlushToDisk() {
	const VDStringW& path = GetConfigPath();
	try {
		ATUISaveRegistry(path.c_str());
		fprintf(stderr, "[AltirraSDL] Settings saved to: %s\n",
			VDTextWToU8(path).c_str());
	} catch (...) {
		fprintf(stderr, "[AltirraSDL] Warning: failed to save settings to %s\n",
			VDTextWToU8(path).c_str());
	}
}

// -------------------------------------------------------------------------
// VDRegistryKey
// -------------------------------------------------------------------------

VDRegistryKey::VDRegistryKey(const char *pszKey, bool global, bool write)
	: mKey(nullptr)
{
	IVDRegistryProvider *p = VDGetRegistryProvider();
	void *base = global ? p->GetMachineKey() : p->GetUserKey();
	mKey = p->CreateKey(base, pszKey, write);
}

VDRegistryKey::VDRegistryKey(VDRegistryKey& baseKey, const char *name, bool write)
	: mKey(nullptr)
{
	IVDRegistryProvider *p = VDGetRegistryProvider();
	mKey = p->CreateKey(baseKey.mKey, name, write);
}

VDRegistryKey::VDRegistryKey(VDRegistryKey&& src)
	: mKey(src.mKey)
{
	src.mKey = nullptr;
}

VDRegistryKey::~VDRegistryKey() {
	if (mKey)
		VDGetRegistryProvider()->CloseKey(mKey);
}

VDRegistryKey& VDRegistryKey::operator=(VDRegistryKey&& src) {
	if (mKey) VDGetRegistryProvider()->CloseKey(mKey);
	mKey = src.mKey;
	src.mKey = nullptr;
	return *this;
}

bool VDRegistryKey::setBool(const char *name, bool v) const {
	return VDGetRegistryProvider()->SetBool(mKey, name, v);
}

bool VDRegistryKey::setInt(const char *name, int v) const {
	return VDGetRegistryProvider()->SetInt(mKey, name, v);
}

bool VDRegistryKey::setString(const char *name, const char *s) const {
	return VDGetRegistryProvider()->SetString(mKey, name, s);
}

bool VDRegistryKey::setString(const char *name, const wchar_t *s) const {
	return VDGetRegistryProvider()->SetString(mKey, name, s);
}

bool VDRegistryKey::setBinary(const char *name, const char *data, int len) const {
	return VDGetRegistryProvider()->SetBinary(mKey, name, data, len);
}

VDRegistryKey::Type VDRegistryKey::getValueType(const char *name) const {
	IVDRegistryProvider::Type t = VDGetRegistryProvider()->GetType(mKey, name);
	switch(t) {
		case IVDRegistryProvider::kTypeInt:    return kTypeInt;
		case IVDRegistryProvider::kTypeString: return kTypeString;
		case IVDRegistryProvider::kTypeBinary: return kTypeBinary;
		default:                               return kTypeUnknown;
	}
}

bool VDRegistryKey::getBool(const char *name, bool def) const {
	bool v = def;
	VDGetRegistryProvider()->GetBool(mKey, name, v);
	return v;
}

int VDRegistryKey::getInt(const char *name, int def) const {
	int v = def;
	VDGetRegistryProvider()->GetInt(mKey, name, v);
	return v;
}

int VDRegistryKey::getEnumInt(const char *name, int maxVal, int def) const {
	int v = getInt(name, def);
	return (v >= 0 && v < maxVal) ? v : def;
}

bool VDRegistryKey::getString(const char *name, VDStringA& s) const {
	return VDGetRegistryProvider()->GetString(mKey, name, s);
}

bool VDRegistryKey::getString(const char *name, VDStringW& s) const {
	return VDGetRegistryProvider()->GetString(mKey, name, s);
}

int VDRegistryKey::getBinaryLength(const char *name) const {
	return VDGetRegistryProvider()->GetBinaryLength(mKey, name);
}

bool VDRegistryKey::getBinary(const char *name, char *buf, int maxlen) const {
	return VDGetRegistryProvider()->GetBinary(mKey, name, buf, maxlen);
}

bool VDRegistryKey::removeValue(const char *name) {
	return VDGetRegistryProvider()->RemoveValue(mKey, name);
}

bool VDRegistryKey::removeKey(const char *name) {
	return VDGetRegistryProvider()->RemoveKey(mKey, name);
}

bool VDRegistryKey::removeKeyRecursive(const char *name) {
	return VDGetRegistryProvider()->RemoveKeyRecursive(mKey, name);
}

// -------------------------------------------------------------------------
// VDRegistryValueIterator / VDRegistryKeyIterator
// -------------------------------------------------------------------------

VDRegistryValueIterator::VDRegistryValueIterator(const VDRegistryKey& key)
	: mEnumerator(VDGetRegistryProvider()->EnumValuesBegin(key.getRawHandle()))
{
}

VDRegistryValueIterator::~VDRegistryValueIterator() {
	VDGetRegistryProvider()->EnumValuesClose(mEnumerator);
}

const char *VDRegistryValueIterator::Next() {
	return VDGetRegistryProvider()->EnumValuesNext(mEnumerator);
}

VDRegistryKeyIterator::VDRegistryKeyIterator(const VDRegistryKey& key)
	: mEnumerator(VDGetRegistryProvider()->EnumKeysBegin(key.getRawHandle()))
{
}

VDRegistryKeyIterator::~VDRegistryKeyIterator() {
	VDGetRegistryProvider()->EnumKeysClose(mEnumerator);
}

const char *VDRegistryKeyIterator::Next() {
	return VDGetRegistryProvider()->EnumKeysNext(mEnumerator);
}

// -------------------------------------------------------------------------
// VDRegistryAppKey
// -------------------------------------------------------------------------

VDRegistryAppKey::VDRegistryAppKey()
	: VDRegistryKey(s_appbase.c_str(), false, true)
{
}

VDRegistryAppKey::VDRegistryAppKey(const char *pszKey, bool write, bool global)
	: VDRegistryKey((s_appbase + "\\" + pszKey).c_str(), global, write)
{
}

void VDRegistryAppKey::setDefaultKey(const char *pszAppName) {
	s_appbase = pszAppName;
}

const char *VDRegistryAppKey::getDefaultKey() {
	return s_appbase.c_str();
}

// -------------------------------------------------------------------------
// VDRegistryCopy
// -------------------------------------------------------------------------

void VDRegistryCopy(IVDRegistryProvider& dstProvider, const char *dstPath,
                    IVDRegistryProvider& srcProvider, const char *srcPath)
{
	// Stub — complex operation not needed for basic functionality
}
