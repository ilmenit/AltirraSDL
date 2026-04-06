//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2023 Avery Lee
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
#include <mmsystem.h>
#include <combaseapi.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/time.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/vdstl.h>
#include <at/ataudio/audioout.h>
#include <at/atio/vorbisdecoder.h>
#include "test.h"

AT_DEFINE_TEST_NONAUTO(IO_VorbisPlayback) {
	const wchar_t *filename = ATTestGetArguments();

	VDFile f(filename);
	sint64 fileSize = f.size();
	if (fileSize > 0x0FFFFFFF)
		throw MyError("File too big");

	vdblock<uint8> data((size_t)fileSize);
	f.read(data.data(), (long)fileSize);
	f.close();

	vdautoptr dec { new ATVorbisDecoder };
	dec->Init(
		[p = data.data(), left = data.size()](void *buf, size_t len) mutable -> size_t {
			if (len > left)
				len = left;

			memcpy(buf, p, len);
			p += len;
			left -= len;

			return len;
		}
	);

	dec->ReadHeaders();

	CoInitializeEx(0, COINIT_MULTITHREADED);

	vdautoptr<IVDAudioOutput> output { VDCreateAudioOutputWaveOutW32() };

	WAVEFORMATEX wfex;
	wfex.wFormatTag = WAVE_FORMAT_PCM;
	wfex.nSamplesPerSec = dec->GetSampleRate();
	wfex.nChannels = dec->GetChannelCount();
	wfex.nBlockAlign = 2 * wfex.nChannels;
	wfex.nAvgBytesPerSec = wfex.nBlockAlign * wfex.nSamplesPerSec;
	wfex.wBitsPerSample = 16;
	wfex.cbSize = 0;

	if (!output->Init(16384, 4, &wfex, nullptr))
		throw MyError("Unable to init sound device");

	output->Start();

	vdblock<sint16> readbuf(4096);

	while(dec->ReadAudioPacket()) {
		uint32 maxRead = 4096 / wfex.nChannels;

		for(;;) {
			uint32 actual = dec->ReadInterleavedSamplesS16(readbuf.data(), maxRead);
			if (!actual)
				break;

			output->Write(readbuf.data(), actual * wfex.nBlockAlign);
		}
	}

	CoUninitialize();

	return 0;
}

AT_DEFINE_TEST_NONAUTO(IO_VorbisBench) {
	const wchar_t *filename = ATTestGetArguments();

	VDFile f(filename);
	sint64 fileSize = f.size();
	if (fileSize > 0x0FFFFFFF)
		throw MyError("File too big");

	vdblock<uint8> data((size_t)fileSize);
	f.read(data.data(), (long)fileSize);
	f.close();

	vdblock<sint16> buf(16384);

	for(;;) {
		sint64 startTick = VDGetPreciseTick();

		vdautoptr dec { new ATVorbisDecoder };
		dec->Init(
			[p = data.data(), left = data.size()](void *buf, size_t len) mutable -> size_t {
				if (len > left)
					len = left;

				memcpy(buf, p, len);
				p += len;
				left -= len;

				return len;
			}
		);

		dec->ReadHeaders();

		while(dec->ReadAudioPacket()) {
			while(dec->ReadInterleavedSamplesStereoS16(buf.data(), 8192))
				;
		}

		const double realSeconds = (VDGetPreciseTick() - startTick) * VDGetPreciseSecondsPerTick();

		const uint32 samplingRate = dec->GetSampleRate();
		const double decodedSeconds = (double)dec->GetSampleCount() / (double)samplingRate;
		printf("%.2f seconds of audio decoded in %.2f sec. (%.3f%% CPU usage)\n", decodedSeconds, realSeconds, realSeconds / decodedSeconds * 100.0);
	}

	return 0;
}

AT_DEFINE_TEST_NONAUTO(IO_VorbisDecode) {
	const wchar_t *filename = ATTestGetArguments();

	VDFile f(filename);
	sint64 fileSize = f.size();
	if (fileSize > 0x0FFFFFFF)
		throw MyError("File too big");

	vdblock<uint8> data((size_t)fileSize);
	f.read(data.data(), (long)fileSize);
	f.close();

	vdblock<sint16> buf(16384);

	vdautoptr dec { new ATVorbisDecoder };
	dec->Init(
		[p = data.data(), left = data.size()](void *buf, size_t len) mutable -> size_t {
			if (len > left)
				len = left;

			memcpy(buf, p, len);
			p += len;
			left -= len;

			return len;
		}
	);

	dec->ReadHeaders();

	VDFile f2("output.pcm", nsVDFile::kWrite | nsVDFile::kCreateAlways);
	while(dec->ReadAudioPacket()) {
		uint32_t n = dec->ReadInterleavedSamplesStereoS16(buf.data(), 8192);

		f2.write(buf.data(), n * 4);
	}

	f2.close();

	return 0;
}
