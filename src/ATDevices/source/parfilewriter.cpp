//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2021 Avery Lee
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
#include <at/atcore/propertyset.h>
#include <at/atdevices/parfilewriter.h>

void ATCreateDeviceFileWriter(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr p(new ATDeviceFileWriter);
	p->SetSettings(pset);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefFileWriter = { "parfilewriter", "parfilewriter", L"File Writer", ATCreateDeviceFileWriter };

void ATDeviceFileWriter::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefFileWriter;
}

void ATDeviceFileWriter::GetSettingsBlurb(VDStringW& buf) {
	buf += mPath;

	buf += mbTextTranslation ? L"; text" : L"; binary";
}

void ATDeviceFileWriter::GetSettings(ATPropertySet& pset) {
	pset.SetString("path", mPath.c_str());
	pset.SetBool("text_mode", mbTextTranslation);
}

bool ATDeviceFileWriter::SetSettings(const ATPropertySet& pset) {
	const wchar_t *path = pset.GetString("path", L"");

	if (mPath != path) {
		mPath = path;

		mFile.closeNT();

		TryOpenOutput();
	}

	mbTextTranslation = pset.GetBool("text_mode", false);

	return true;
}

void ATDeviceFileWriter::Init() {
	mbInited = true;
	TryOpenOutput();
}

void ATDeviceFileWriter::Shutdown() {
	mFile.closeNT();
	mbInited = false;
}

void ATDeviceFileWriter::ColdReset() {
	TryOpenOutput();
}

bool ATDeviceFileWriter::GetErrorStatus(uint32 idx, VDStringW& error) {
	if (idx == 0 && !mCurrentError.empty()) {
		error = mCurrentError.wc_str();
		return true;
	}

	return false;
}

void ATDeviceFileWriter::WriteRaw(const uint8 *src, size_t len) {
	if (!mbTextTranslation) {
		Write(src, len);
		Flush();
		return;
	}

	if (VDTextOutputStream::GetDefaultLFOnly()) {
		while(len--) {
			uint8 c = *src++;

			if (c == 0x9B) {
				c = 0x0A;
			} else if (c == 0x0D) {
				c = 0x0A;
				mbTextIgnoreNextLF = true;
			} else if (c == 0x0A) {
				if (mbTextIgnoreNextLF) {
					mbTextIgnoreNextLF = false;
					continue;
				}
			} else {
				mbTextIgnoreNextLF = false;
			}

			WriteByte(c);
		}
	} else {
		while(len--) {
			uint8 c = *src++;

			if (c == 0x9B) {
				WriteByte(0x0D);
				c = 0x0A;
			} else if (c == 0x0D) {
				WriteByte(0x0D);
				c = 0x0A;
				mbTextIgnoreNextLF = true;
			} else if (c == 0x0A) {
				if (mbTextIgnoreNextLF) {
					mbTextIgnoreNextLF = false;
					continue;
				}

				WriteByte(0x0D);
				c = 0x0A;
			} else {
				mbTextIgnoreNextLF = false;
			}

			WriteByte(c);
		}
	}

	Flush();
}

void ATDeviceFileWriter::SetOnStatusChange(const vdfunction<void(const ATDeviceSerialStatus&)>& fn) {
}

void ATDeviceFileWriter::SetTerminalState(const ATDeviceSerialTerminalState&) {
}

ATDeviceSerialStatus ATDeviceFileWriter::GetStatus() {
	return {};
}

void ATDeviceFileWriter::SetOnReadReady(vdfunction<void()> fn) {
}

bool ATDeviceFileWriter::Read(uint32 baudRate, uint8& c, bool& framingError) {
	return false;
}

bool ATDeviceFileWriter::Read(uint32& baudRate, uint8& c) {
	return false;
}

void ATDeviceFileWriter::Write(uint32 baudRate, uint8 c) {
	WriteToFile(&c, 1);
}

void ATDeviceFileWriter::FlushBuffers() {
}

void ATDeviceFileWriter::WriteByte(uint8 c) {
	if (mBufferLevel >= kBufferSize)
		Flush();

	mBuffer[mBufferLevel++] = c;
}

void ATDeviceFileWriter::Write(const void *buf, size_t len) {
	while(len) {
		if (mBufferLevel == 0) {
			WriteToFile(buf, len);
			break;
		}

		const size_t tc = std::min<size_t>(len, kBufferSize - mBufferLevel);

		if (tc) {
			memcpy(mBuffer + mBufferLevel, buf, tc);
			mBufferLevel += tc;

			buf = (const char *)buf + tc;
			len -= tc;
		}

		Flush();
	}
}

void ATDeviceFileWriter::Flush() {
	if (mBufferLevel) {
		WriteToFile(mBuffer, mBufferLevel);
		mBufferLevel = 0;
	}
}

void ATDeviceFileWriter::WriteToFile(const void *buf, size_t len) {
	if (!len || !mFile.isOpen())
		return;

	try {
		mFile.write(buf, len);
	} catch(VDException& e) {
		mCurrentError = std::move(e);
		mFile.closeNT();
	}
}

void ATDeviceFileWriter::TryOpenOutput() {
	if (mbInited && !mPath.empty() && !mFile.isOpen()) {
		try {
			mFile.open(mPath.c_str(), nsVDFile::kWrite | nsVDFile::kDenyNone | nsVDFile::kOpenAlways);
			mFile.seek(0, nsVDFile::kSeekEnd);
			mCurrentError.clear();
		} catch(VDException& e) {
			mCurrentError = std::move(e);
			mFile.closeNT();
		}
	}
}
