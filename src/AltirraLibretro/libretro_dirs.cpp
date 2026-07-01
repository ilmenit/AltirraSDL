// Altirra libretro core - standalone directory and registry replacement TU.
//
// This provides the config-dir and registry symbols used by Altirra without
// extracting the SDL-oriented registry object from libsystem.a.

#include <stdafx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#include <vd2/system/VDString.h>
#include <vd2/system/registry.h>
#include <vd2/system/registrymemory.h>
#include <vd2/system/text.h>

#include "libretro_log.h"

void ATUILoadRegistry(const wchar_t *path);
void ATUISaveRegistry(const wchar_t *fnpath);
VDStringA ATGetConfigDir();

namespace {
	VDStringA s_configDir;
	VDStringW s_configPath;
	IVDRegistryProvider *g_pRegistryProvider = nullptr;
	VDRegistryProviderMemory *g_pMemoryProvider = nullptr;

	void MakeDirectory(const VDStringA& path) {
		if (path.empty())
			return;

		std::string partial;
		size_t pos = 0;

#if defined(_WIN32)
		if (path.size() >= 2 && path[1] == ':') {
			partial.assign(path.c_str(), 2);
			pos = 2;
		}
#endif

		while(pos < path.size()) {
			const char ch = path[pos++];
			partial += ch;

			if (ch != '/' && ch != '\\' && pos != path.size())
				continue;

			while(pos < path.size() && (path[pos] == '/' || path[pos] == '\\'))
				partial += path[pos++];

			if (partial.empty() || partial == "/" || partial == "\\")
				continue;

#if defined(_WIN32)
			_mkdir(partial.c_str());
#else
			mkdir(partial.c_str(), 0755);
#endif
		}
	}

	VDStringA GetDefaultConfigDir() {
		VDStringA dir;

#if defined(_WIN32)
		const char *appData = getenv("APPDATA");
		if (appData && *appData)
			dir = appData;
		else {
			const char *userProfile = getenv("USERPROFILE");
			dir = (userProfile && *userProfile) ? userProfile : ".";
		}
		dir += "\\Altirra";
#else
		const char *xdgConfig = getenv("XDG_CONFIG_HOME");
		if (xdgConfig && *xdgConfig)
			dir = xdgConfig;
		else {
			const char *home = getenv("HOME");
			if (home && *home) {
				dir = home;
				dir += "/.config";
			} else {
				dir = "/tmp";
			}
		}
		dir += "/altirra";
#endif

		return dir;
	}

	const VDStringW& GetConfigPath() {
		if (s_configPath.empty()) {
			VDStringA filePath = ATGetConfigDir();
#if defined(_WIN32)
			filePath += "\\settings.ini";
#else
			filePath += "/settings.ini";
#endif
			s_configPath = VDTextU8ToW(filePath);
		}

		return s_configPath;
	}

	void VDRegistryCopyTree(IVDRegistryProvider& dstProvider,
		void *dstParentKey, const char *dstPath,
		IVDRegistryProvider& srcProvider, void *srcParentKey,
		const char *srcPath)
	{
		void *srcKey = srcProvider.CreateKey(srcParentKey, srcPath, false);
		if (!srcKey)
			return;

		void *dstKey = dstProvider.CreateKey(dstParentKey, dstPath, true);
		if (dstKey) {
			void *srcValueEnum = srcProvider.EnumValuesBegin(srcKey);
			if (srcValueEnum) {
				while (const char *valueName =
					srcProvider.EnumValuesNext(srcValueEnum))
				{
					switch (srcProvider.GetType(srcKey, valueName)) {
						case IVDRegistryProvider::kTypeInt:
							if (int ival = 0;
								srcProvider.GetInt(srcKey, valueName, ival))
							{
								dstProvider.SetInt(dstKey, valueName, ival);
							}
							break;

						case IVDRegistryProvider::kTypeString:
							if (VDStringW sval;
								srcProvider.GetString(srcKey, valueName, sval))
							{
								dstProvider.SetString(dstKey, valueName,
									sval.c_str());
							}
							break;

						case IVDRegistryProvider::kTypeBinary:
							if (int len =
								srcProvider.GetBinaryLength(srcKey, valueName);
								len >= 0)
							{
								std::vector<char> buf((size_t)len);
								if (srcProvider.GetBinary(srcKey, valueName,
									buf.data(), len))
								{
									dstProvider.SetBinary(dstKey, valueName,
										buf.data(), len);
								}
							}
							break;

						default:
							break;
					}
				}

				srcProvider.EnumValuesClose(srcValueEnum);
			}

			void *srcKeyEnum = srcProvider.EnumKeysBegin(srcKey);
			if (srcKeyEnum) {
				while (const char *subKeyName =
					srcProvider.EnumKeysNext(srcKeyEnum))
				{
					VDRegistryCopyTree(dstProvider, dstKey, subKeyName,
						srcProvider, srcKey, subKeyName);
				}

				srcProvider.EnumKeysClose(srcKeyEnum);
			}

			dstProvider.CloseKey(dstKey);
		}

		srcProvider.CloseKey(srcKey);
	}
}

VDStringA ATGetConfigDir() {
	if (s_configDir.empty()) {
		s_configDir = GetDefaultConfigDir();
		MakeDirectory(s_configDir);
	}

	return s_configDir;
}

void ATSetConfigDirOverride(const char *path) {
	if (!path || !*path)
		return;

	s_configDir = path;
	s_configPath.clear();
	MakeDirectory(s_configDir);
}

IVDRegistryProvider *VDGetDefaultRegistryProvider() {
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

void ATRegistryLoadFromDisk() {
	const VDStringW& path = GetConfigPath();
	const VDStringA pathU8 = VDTextWToU8(path);

	try {
		FILE *f = fopen(pathU8.c_str(), "r");
		if (f) {
			fclose(f);
			ATUILoadRegistry(path.c_str());
			ATLibretroLog(RETRO_LOG_INFO, "Settings loaded from: %s\n",
				pathU8.c_str());
		} else {
			ATLibretroLog(RETRO_LOG_INFO,
				"No settings file found, using defaults\n");
		}
	} catch (...) {
		ATLibretroLog(RETRO_LOG_WARN,
			"failed to load settings from %s\n",
			pathU8.c_str());
	}
}

void ATRegistryFlushToDisk() {
	const VDStringW& path = GetConfigPath();
	const VDStringA pathU8 = VDTextWToU8(path);

	try {
		ATUISaveRegistry(path.c_str());
		ATLibretroLog(RETRO_LOG_INFO, "Settings saved to: %s\n",
			pathU8.c_str());
	} catch (...) {
		ATLibretroLog(RETRO_LOG_WARN,
			"failed to save settings to %s\n",
			pathU8.c_str());
	}
}

VDRegistryKey::VDRegistryKey(const char *keyName, bool global, bool write)
	: mKey(nullptr)
{
	IVDRegistryProvider *provider = VDGetRegistryProvider();
	void *rootKey = global ? provider->GetMachineKey() : provider->GetUserKey();

	mKey = provider->CreateKey(rootKey, keyName, write);
}

VDRegistryKey::VDRegistryKey(VDRegistryKey& baseKey, const char *name,
	bool write)
	: mKey(nullptr)
{
	IVDRegistryProvider *provider = VDGetRegistryProvider();
	void *rootKey = baseKey.getRawHandle();

	mKey = rootKey ? provider->CreateKey(rootKey, name, write) : nullptr;
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
	if (&src != this) {
		if (mKey)
			VDGetRegistryProvider()->CloseKey(mKey);

		mKey = src.mKey;
		src.mKey = nullptr;
	}

	return *this;
}

bool VDRegistryKey::setBool(const char *name, bool v) const {
	return mKey && VDGetRegistryProvider()->SetBool(mKey, name, v);
}

bool VDRegistryKey::setInt(const char *name, int v) const {
	return mKey && VDGetRegistryProvider()->SetInt(mKey, name, v);
}

bool VDRegistryKey::setString(const char *name, const char *s) const {
	return mKey && VDGetRegistryProvider()->SetString(mKey, name, s);
}

bool VDRegistryKey::setString(const char *name, const wchar_t *s) const {
	return mKey && VDGetRegistryProvider()->SetString(mKey, name, s);
}

bool VDRegistryKey::setBinary(const char *name, const char *data,
	int len) const
{
	return mKey && VDGetRegistryProvider()->SetBinary(mKey, name, data, len);
}

VDRegistryKey::Type VDRegistryKey::getValueType(const char *name) const {
	if (!mKey)
		return kTypeUnknown;

	switch (VDGetRegistryProvider()->GetType(mKey, name)) {
		case IVDRegistryProvider::kTypeInt:
			return kTypeInt;
		case IVDRegistryProvider::kTypeString:
			return kTypeString;
		case IVDRegistryProvider::kTypeBinary:
			return kTypeBinary;
		default:
			return kTypeUnknown;
	}
}

bool VDRegistryKey::getBool(const char *name, bool def) const {
	bool v = def;
	return mKey && VDGetRegistryProvider()->GetBool(mKey, name, v) ? v : def;
}

int VDRegistryKey::getInt(const char *name, int def) const {
	int v = def;
	return mKey && VDGetRegistryProvider()->GetInt(mKey, name, v) ? v : def;
}

int VDRegistryKey::getEnumInt(const char *name, int maxVal, int def) const {
	const int v = getInt(name, def);
	return (v >= 0 && v < maxVal) ? v : def;
}

bool VDRegistryKey::getString(const char *name, VDStringA& s) const {
	return mKey && VDGetRegistryProvider()->GetString(mKey, name, s);
}

bool VDRegistryKey::getString(const char *name, VDStringW& s) const {
	return mKey && VDGetRegistryProvider()->GetString(mKey, name, s);
}

int VDRegistryKey::getBinaryLength(const char *name) const {
	return mKey ? VDGetRegistryProvider()->GetBinaryLength(mKey, name) : -1;
}

bool VDRegistryKey::getBinary(const char *name, char *buf, int maxlen) const {
	return mKey
		&& VDGetRegistryProvider()->GetBinary(mKey, name, buf, maxlen);
}

bool VDRegistryKey::removeValue(const char *name) {
	return mKey && VDGetRegistryProvider()->RemoveValue(mKey, name);
}

bool VDRegistryKey::removeKey(const char *name) {
	return mKey && VDGetRegistryProvider()->RemoveKey(mKey, name);
}

bool VDRegistryKey::removeKeyRecursive(const char *name) {
	return mKey && VDGetRegistryProvider()->RemoveKeyRecursive(mKey, name);
}

VDRegistryValueIterator::VDRegistryValueIterator(const VDRegistryKey& key)
	: mEnumerator(key.getRawHandle()
		? VDGetRegistryProvider()->EnumValuesBegin(key.getRawHandle())
		: nullptr)
{
}

VDRegistryValueIterator::~VDRegistryValueIterator() {
	if (mEnumerator)
		VDGetRegistryProvider()->EnumValuesClose(mEnumerator);
}

const char *VDRegistryValueIterator::Next() {
	return mEnumerator
		? VDGetRegistryProvider()->EnumValuesNext(mEnumerator)
		: nullptr;
}

VDRegistryKeyIterator::VDRegistryKeyIterator(const VDRegistryKey& key)
	: mEnumerator(key.getRawHandle()
		? VDGetRegistryProvider()->EnumKeysBegin(key.getRawHandle())
		: nullptr)
{
}

VDRegistryKeyIterator::~VDRegistryKeyIterator() {
	if (mEnumerator)
		VDGetRegistryProvider()->EnumKeysClose(mEnumerator);
}

const char *VDRegistryKeyIterator::Next() {
	return mEnumerator
		? VDGetRegistryProvider()->EnumKeysNext(mEnumerator)
		: nullptr;
}

VDString VDRegistryAppKey::s_appbase;

VDRegistryAppKey::VDRegistryAppKey()
	: VDRegistryKey(s_appbase.c_str(), false, true)
{
}

VDRegistryAppKey::VDRegistryAppKey(const char *pszKey, bool write,
	bool global)
	: VDRegistryKey((s_appbase + "\\" + pszKey).c_str(), global, write)
{
}

void VDRegistryAppKey::setDefaultKey(const char *pszAppName) {
	s_appbase = pszAppName;
}

const char *VDRegistryAppKey::getDefaultKey() {
	return s_appbase.c_str();
}

void VDRegistryCopy(IVDRegistryProvider& dstProvider, const char *dstPath,
	IVDRegistryProvider& srcProvider, const char *srcPath)
{
	VDRegistryCopyTree(dstProvider, dstProvider.GetUserKey(), dstPath,
		srcProvider, srcProvider.GetUserKey(), srcPath);
}
