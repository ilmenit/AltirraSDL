//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2023 Avery Lee
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
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#ifndef f_UIFRONTEND_H
#define f_UIFRONTEND_H

#include "uiaccessors.h"

class IATDeviceSnapshot;
struct ATVideoRecordingSettings;

class IATUIFrontEnd {
public:
	virtual ATUIRecordingStatus GetRecordingStatus() const = 0;
	virtual bool IsRecording() const = 0;

	virtual void RecordRawAudio(const wchar_t *path) = 0;
	virtual void RecordAudio(const wchar_t *path) = 0;
	virtual void RecordVideo(const wchar_t *path, const ATVideoRecordingSettings& settings) = 0;
	virtual void RecordSap(const wchar_t *path) = 0;
	virtual void RecordVgm(const wchar_t *path) = 0;

	virtual void StopRecording() = 0;

	virtual void PauseRecording() = 0;
	virtual void ResumeRecording() = 0;
	virtual void TogglePauseResumeRecording() = 0;

	virtual void CheckRecordingExceptions() = 0;

	virtual void ShowSuggestions() = 0;
	virtual bool IsAutoSuggestEnabled() = 0;
	virtual void SetAutoSuggestEnabled(bool enabled) = 0;
};

IATDeviceSnapshot *ATUIGetFrontEndSnapshot();
IATUIFrontEnd& ATUIGetFrontEnd();
void ATUIInitFrontEnd();
void ATUIShutdownFrontEnd();

#endif
