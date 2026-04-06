//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2025 Avery Lee
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
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <at/atio/audioreader.h>

IATAudioReader *ATCreateAudioReaderDetect(IVDRandomAccessStream& inputStream) {
	char buf[4];
	const auto actual = inputStream.ReadData(buf, 4);
	inputStream.Seek(0);

	if (actual == 4) {
		if (!memcmp(buf, "WAVE", 4))
			return ATCreateAudioReaderWAV(inputStream);
		else if (!memcmp(buf, "FLAC", 4))
			return ATCreateAudioReaderFLAC(inputStream, false);
		else if (!memcmp(buf, "OggS", 4))
			return ATCreateAudioReaderVorbis(inputStream);
	}

	return nullptr;
}
