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

#ifndef AT_OPTIONS_H
#define AT_OPTIONS_H

enum ATErrorMode {
	kATErrorMode_Dialog,
	kATErrorMode_Debug,
	kATErrorMode_Pause,
	kATErrorMode_ColdReset,
	kATErrorModeCount
};

enum class ATProcessEfficiencyMode : uint8;
enum ATMediaWriteMode : uint8;

enum class ATUIThemeMode : uint8 {
	System,		// follow OS dark/light preference
	Light,
	Dark,
};

struct ATOptionsData {
	bool mbDisplayD3D9;
	bool mbDisplay3D;
	bool mbDisplay16Bit;
	bool mbDisplayAccelScreenFX;
	bool mbDisplayCustomRefresh;

	bool mbSingleInstance;
	bool mbPauseDuringMenu;
	bool mbLaunchAutoProfile;
	
	sint32 mThemeScale;
	bool mbDarkTheme;
	ATUIThemeMode mThemeMode = ATUIThemeMode::System;
	float mUIAlpha = 1.0f;		// ImGui window transparency (0.2 .. 1.0)

	ATErrorMode mErrorMode;

	bool	mbFullScreenBorderless;
	uint32	mFullScreenWidth;
	uint32	mFullScreenHeight;
	uint32	mFullScreenRefreshRate;

	VDStringA	mSICFlashChip;
	VDStringA	mU1MBFlashChip;
	VDStringA	mMaxflash1MbFlashChip;
	VDStringA	mMaxflash8MbFlashChip;

	ATMediaWriteMode mDefaultWriteMode;
	ATProcessEfficiencyMode mEfficiencyMode;

	bool mbCompatEnable;
	bool mbCompatEnableInternalDB;
	bool mbCompatEnableExternalDB;
	VDStringW mCompatExternalDBPath;

	bool mbPollDirectories;

	bool mbAccEnabled = false;

	bool mbTextOutputLFOnly = false;

	ATOptionsData();
	
	bool operator==(const ATOptionsData&) const = default;
};

struct ATOptions final : public ATOptionsData {
	bool mbDirty = false;
};

void ATOptionsLoad();
void ATOptionsSave();

void ATOptionsSuspendSave();
void ATOptionsResumeSave();

typedef void (*ATOptionsUpdateCallback)(ATOptions& opts, const ATOptions *prevOpts, void *data);
void ATOptionsAddUpdateCallback(bool runNow, ATOptionsUpdateCallback, void *data = 0);
void ATOptionsRunUpdateCallbacks(const ATOptions *prevOpts);

extern ATOptions g_ATOptions;

#endif
