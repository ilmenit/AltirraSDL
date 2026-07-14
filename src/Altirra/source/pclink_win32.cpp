//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2026 Avery Lee
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

#include <stdafx.h>
#include <windows.h>
#include <at/atcore/cio.h>

uint8 ATTranslateWin32ErrorToSIOError(uint32 err) {
	switch(err) {
		case ERROR_FILE_NOT_FOUND:
			return kATCIOStat_FileNotFound;

		case ERROR_PATH_NOT_FOUND:
			return kATCIOStat_PathNotFound;

		case ERROR_FILE_EXISTS:
		case ERROR_ALREADY_EXISTS:
			return kATCIOStat_FileExists;

		case ERROR_DISK_FULL:
			return kATCIOStat_DiskFull;

		case ERROR_DIR_NOT_EMPTY:
			return kATCIOStat_DirNotEmpty;

		case ERROR_ACCESS_DENIED:
			return kATCIOStat_AccessDenied;

		case ERROR_SHARING_VIOLATION:
			return kATCIOStat_FileLocked;

		default:
			return kATCIOStat_SystemError;
	}
}

uint8 ATTranslateCurrentExceptionToCIOError() {
	try {
		throw;
	} catch(const VDWin32Exception& e) {
		return ATTranslateWin32ErrorToSIOError(e.GetWin32Error());
	} catch(...) {
		return kATCIOStat_SystemError;
	}
}
