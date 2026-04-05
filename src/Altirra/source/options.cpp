//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2011 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include <stdafx.h>
#include <algorithm>
#include <vd2/system/registry.h>
#include <at/atcore/media.h>
#include "options.h"
#include "oshelper.h"

uint32 g_ATOptionsSaveSuspended;

ATOptionsData::ATOptionsData()
	: mbDisplayD3D9(true)
	, mbDisplay3D(true)
	, mbDisplay16Bit(false)
	, mbDisplayAccelScreenFX(true)
	, mbDisplayCustomRefresh(false)
	, mbSingleInstance(false)
	, mbPauseDuringMenu(false)
	, mbLaunchAutoProfile(true)
	, mThemeScale(100)
	, mbDarkTheme(false)
	, mErrorMode(kATErrorMode_Dialog)
	, mbFullScreenBorderless(true)
	, mFullScreenWidth(0)
	, mFullScreenHeight(0)
	, mFullScreenRefreshRate(0)
	, mDefaultWriteMode(kATMediaWriteMode_VRWSafe)
	, mbCompatEnable(true)
	, mbCompatEnableInternalDB(true)
	, mbCompatEnableExternalDB(false)
	, mCompatExternalDBPath()
	, mbPollDirectories(false)
{
}

void ATOptionsExchange(VDRegistryKey& key, bool write, const char *name, bool& val) {
	if (write)
		key.setBool(name, val);
	else
		val = key.getBool(name, val);
}

void ATOptionsExchange(VDRegistryKey& key, bool write, const char *name, uint32& val) {
	if (write)
		key.setInt(name, val);
	else
		val = key.getInt(name, val);
}

void ATOptionsExchange(VDRegistryKey& key, bool write, const char *name, sint32& val) {
	if (write)
		key.setInt(name, val);
	else
		val = key.getInt(name, val);
}

void ATOptionsExchange(VDRegistryKey& key, bool write, const char *name, VDStringA& val) {
	if (write)
		key.setString(name, val.c_str());
	else
		key.getString(name, val);
}

void ATOptionsExchange(VDRegistryKey& key, bool write, const char *name, VDStringW& val) {
	if (write)
		key.setString(name, val.c_str());
	else
		key.getString(name, val);
}

template<class T>
void ATOptionsExchangeEnum(VDRegistryKey& key, bool write, const char *name, T& val) {
	if (write)
		key.setString(name, ATEnumToString(val));
	else {
		VDStringW str;
		key.getString(name, str);
		val = ATParseEnum<T>(str).mValue;
	}
}

template<class T>
void ATOptionsExchangeEnum(VDRegistryKey& key, bool write, const char *name, T& val, T limit) {
	if (write)
		key.setInt(name, val);
	else
		val = (T)key.getEnumInt(name, limit, val);
}

void ATOptionsExchange(VDRegistryKey& key, bool write, ATOptions& opts) {
	ATOptionsExchange(key, write, "Startup: Reuse program instance", opts.mbSingleInstance);
	ATOptionsExchange(key, write, "Display: Direct3D9", opts.mbDisplayD3D9);
	ATOptionsExchange(key, write, "Display: 3D", opts.mbDisplay3D);
	ATOptionsExchange(key, write, "Display: Use 16-bit surfaces", opts.mbDisplay16Bit);
	ATOptionsExchange(key, write, "Display: Accelerate screen FX", opts.mbDisplayAccelScreenFX);
	ATOptionsExchange(key, write, "Display: Use seamless custom refresh rate", opts.mbDisplayCustomRefresh);
	ATOptionsExchangeEnum(key, write, "Simulator: Error mode", opts.mErrorMode, kATErrorModeCount);
	ATOptionsExchange(key, write, "Display: Full screen mode width", opts.mFullScreenWidth);
	ATOptionsExchange(key, write, "Display: Full screen mode height", opts.mFullScreenHeight);
	ATOptionsExchange(key, write, "Display: Full screen mode refresh rate", opts.mFullScreenRefreshRate);
	ATOptionsExchange(key, write, "Display: Borderless mode", opts.mbFullScreenBorderless);
	ATOptionsExchange(key, write, "Flash: SIC! cartridge flash mode", opts.mSICFlashChip);
	ATOptionsExchange(key, write, "Flash: Ultimate1MB flash mode", opts.mU1MBFlashChip);
	ATOptionsExchange(key, write, "Flash: Maxflash 1Mb flash mode", opts.mMaxflash1MbFlashChip);
	ATOptionsExchange(key, write, "Flash: Maxflash 8Mb flash mode", opts.mMaxflash8MbFlashChip);
	ATOptionsExchange(key, write, "UI: Theme scale factor", opts.mThemeScale);
	ATOptionsExchange(key, write, "UI: Use dark theme", opts.mbDarkTheme);

	// Theme mode: 0=System, 1=Light, 2=Dark.  Falls back to mbDarkTheme
	// for settings files that predate mThemeMode.  On a fresh install
	// (neither key exists), keeps the struct default of System.
	{
		sint32 mode = (sint32)opts.mThemeMode;
		if (write) {
			key.setInt("UI: Theme mode", mode);
		} else {
			mode = key.getInt("UI: Theme mode", -1);
			if (mode >= 0 && mode <= 2)
				opts.mThemeMode = (ATUIThemeMode)mode;
			else if (key.getValueType("UI: Use dark theme") != VDRegistryKey::kTypeUnknown)
				opts.mThemeMode = opts.mbDarkTheme ? ATUIThemeMode::Dark : ATUIThemeMode::Light;
			// else: fresh install — keep default ATUIThemeMode::System
		}
	}

	// UI window transparency (stored as percentage 20-100)
	{
		sint32 alphaPct = (sint32)(opts.mUIAlpha * 100.0f + 0.5f);
		ATOptionsExchange(key, write, "UI: Window alpha percent", alphaPct);
		if (!write)
			opts.mUIAlpha = std::clamp(alphaPct / 100.0f, 0.2f, 1.0f);
	}
	ATOptionsExchange(key, write, "UI: Pause during menus", opts.mbPauseDuringMenu);
	ATOptionsExchange(key, write, "UI: Launch with automatic profile", opts.mbLaunchAutoProfile);
	ATOptionsExchangeEnum(key, write, "Media: Default write mode", opts.mDefaultWriteMode, (ATMediaWriteMode)(kATMediaWriteMode_All + 1));

	ATOptionsExchange(key, write, "CompatDB: Enable", opts.mbCompatEnable);
	ATOptionsExchange(key, write, "CompatDB: Enable internal DB", opts.mbCompatEnableInternalDB);
	ATOptionsExchange(key, write, "CompatDB: Enable external DB", opts.mbCompatEnableExternalDB);
	ATOptionsExchange(key, write, "CompatDB: External DB path", opts.mCompatExternalDBPath);

	ATOptionsExchange(key, write, "System: Force directory change polling", opts.mbPollDirectories);
	ATOptionsExchangeEnum(key, write, "System: Efficiency mode", opts.mEfficiencyMode);

	ATOptionsExchange(key, write, "Accessibility: Enable", opts.mbAccEnabled);
}

void ATOptionsLoad() {
	VDRegistryAppKey key("Settings", false);

	g_ATOptions = ATOptions();
	ATOptionsExchange(key, false, g_ATOptions);
}

void ATOptionsSave() {
	if (!g_ATOptions.mbDirty || g_ATOptionsSaveSuspended)
		return;

	VDRegistryAppKey key("Settings");

	ATOptionsExchange(key, true, g_ATOptions);
	g_ATOptions.mbDirty = false;
}

void ATOptionsSuspendSave() {
	++g_ATOptionsSaveSuspended;

	VDASSERT(g_ATOptionsSaveSuspended > 0 && g_ATOptionsSaveSuspended < 100);
}

void ATOptionsResumeSave() {
	VDASSERT(g_ATOptionsSaveSuspended > 0 && g_ATOptionsSaveSuspended < 100);

	if (!--g_ATOptionsSaveSuspended)
		ATOptionsSave();
}

///////////////////////////////////////////////////////////////////////////

namespace {
	struct UpdateEntry {
		ATOptionsUpdateCallback mpCallback;
		void *mpData;
	};
}

vdfastvector<UpdateEntry> g_ATOptionsUpdateEntries;

void ATOptionsAddUpdateCallback(bool runNow, ATOptionsUpdateCallback cb, void *data) {
	UpdateEntry& e = g_ATOptionsUpdateEntries.push_back();
	e.mpCallback = cb;
	e.mpData = data;

	if (runNow)
		cb(g_ATOptions, NULL, data);
}

void ATOptionsRunUpdateCallbacks(const ATOptions *prevOpts) {
	for(vdfastvector<UpdateEntry>::const_iterator it = g_ATOptionsUpdateEntries.begin(), itEnd = g_ATOptionsUpdateEntries.end();
		it != itEnd;
		++it)
	{
		it->mpCallback(g_ATOptions, prevOpts, it->mpData);
	}
}

///////////////////////////////////////////////////////////////////////////

ATOptions g_ATOptions;
