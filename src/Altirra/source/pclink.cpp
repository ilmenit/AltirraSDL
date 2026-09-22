//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2011 Avery Lee
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
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/function.h>
#include <at/atcore/cio.h>
#include <at/atcore/consoleoutput.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/devicesio.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/scheduler.h>
#include "pclink.h"
#include "console.h"
#include "cpu.h"
#include "kerneldb.h"
#include "uirender.h"
#include "debuggerlog.h"

ATDebuggerLogChannel g_ATLCPCLink(false, false, "PCLINK", "PCLink activity");

#ifdef _WIN32
uint8 ATTranslateWin32ErrorToSIOError(uint32 err);
#endif

namespace {
	// 2011 and we still have to put up with this crap
	static const char *const kReservedDeviceNames[]={
		"CON",
		"PRN",
		"AUX",
		"NUL",
		"COM1",
		"COM2",
		"COM3",
		"COM4",
		"COM5",
		"COM6",
		"COM7",
		"COM8",
		"COM9",
		"LPT1",
		"LPT2",
		"LPT3",
		"LPT4",
		"LPT5",
		"LPT6",
		"LPT7",
		"LPT8",
		"LPT9",
		NULL
	};
}

///////////////////////////////////////////////////////////////////////////

struct ATPCLinkDiskInfo {
	uint8	mInfoVersion;
	uint8	mRootDirLo;
	uint8	mRootDirHi;
	uint8	mSectorCountLo;
	uint8	mSectorCountHi;
	uint8	mSectorsFreeLo;
	uint8	mSectorsFreeHi;
	uint8	mVTOCSectorCount;
	uint8	mVTOCSectorStartLo;
	uint8	mVTOCSectorStartHi;
	uint8	mNextFileSectorLo;
	uint8	mNextFileSectorHi;
	uint8	mNextDirSectorLo;
	uint8	mNextDirSectorHi;
	uint8	mVolumeLabel[8];
	uint8	mTrackCount;
	uint8	mBytesPerSectorCode;
	uint8	mVersion;
	uint8	mBytesPerSectorLo;
	uint8	mBytesPerSectorHi;
	uint8	mMapSectorCountLo;
	uint8	mMapSectorCountHi;
	uint8	mSectorsPerCluster;
	uint8	mNoSeq;
	uint8	mNoRnd;
	uint8	mBootLo;
	uint8	mBootHi;
	uint8	mWriteProtectFlag;
	uint8	mPad[29];
};

void ATPCLinkDirEnt::SetFlagsFromAttributes(uint32 attr) {
	mFlags = ATPCLinkDirEnt::kFlag_InUse;
	if (attr & kVDFileAttr_Hidden)
		mFlags |= ATPCLinkDirEnt::kFlag_Hidden;

	if (attr & kVDFileAttr_Archive)
		mFlags |= ATPCLinkDirEnt::kFlag_Archive;

	if (attr & kVDFileAttr_ReadOnly)
		mFlags |= ATPCLinkDirEnt::kFlag_Locked;

	if (attr & kVDFileAttr_Directory)
		mFlags |= ATPCLinkDirEnt::kFlag_Directory;

}

bool ATPCLinkDirEnt::TestAttrFilter(uint8 attrFilter) const {
	if (!attrFilter)
		return true;

	if (attrFilter & kAttrMask_NoArchived) {
		if (mFlags & ATPCLinkDirEnt::kFlag_Archive)
			return false;
	}

	if (attrFilter & kAttrMask_NoHidden) {
		if (mFlags & ATPCLinkDirEnt::kFlag_Hidden)
			return false;
	}

	if (attrFilter & kAttrMask_NoLocked) {
		if (mFlags & ATPCLinkDirEnt::kFlag_Locked)
			return false;
	}

	if (attrFilter & kAttrMask_NoSubDir) {
		if (mFlags & ATPCLinkDirEnt::kFlag_Directory)
			return false;
	}

	if (attrFilter & kAttrMask_OnlyArchived) {
		if (!(mFlags & ATPCLinkDirEnt::kFlag_Archive))
			return false;
	}

	if (attrFilter & kAttrMask_OnlyHidden) {
		if (!(mFlags & ATPCLinkDirEnt::kFlag_Hidden))
			return false;
	}

	if (attrFilter & kAttrMask_OnlyLocked) {
		if (!(mFlags & ATPCLinkDirEnt::kFlag_Locked))
			return false;
	}

	if (attrFilter & kAttrMask_OnlySubDir) {
		if (!(mFlags & ATPCLinkDirEnt::kFlag_Directory))
			return false;
	}

	return true;
}

void ATPCLinkDirEnt::SetDate(const VDDate& date) {
	VDExpandedDate date2(VDGetLocalDate(date));
	mDay = date2.mDay;
	mMonth = date2.mMonth;
	mYear = date2.mYear % 100;
	mHour = date2.mHour;
	mMin = date2.mMinute;
	mSec = date2.mSecond;
}

VDDate ATPCLinkDirEnt::DecodeDate(const uint8 tsdata[6]) {
	VDExpandedDate xdate {};

	xdate.mDay = tsdata[0];
	xdate.mMonth = tsdata[1];
	xdate.mYear = tsdata[2] < 50 ? 2000 + tsdata[2] : 1900 + tsdata[2];
	xdate.mHour = tsdata[3];
	xdate.mMinute = tsdata[4];
	xdate.mSecond = tsdata[5];
	xdate.mMilliseconds = 0;

	return VDDateFromLocalDate(xdate);
}

struct ATPCLinkDirEntSort {
	bool operator()(const ATPCLinkDirEnt& x, const ATPCLinkDirEnt& y) {
		if ((x.mFlags ^ y.mFlags) & ATPCLinkDirEnt::kFlag_Directory)
			return (x.mFlags & ATPCLinkDirEnt::kFlag_Directory) != 0;

		return memcmp(x.mName, y.mName, 11) < 0;
	}
};

bool ATPCLinkFileName::ParseFromNet(const uint8 fn[11]) {
	uint8 fill = 0;

	for(int i=0; i<11; ++i) {
		if (i == 8)
			fill = 0;

		if (fill)
			mName[i] = fill;
		else {
			uint8 c = fn[i];

			if (c == '*') {
				c = '?';
				fill = c;
			}

			if (c == ' ')
				fill = c;
			else if (c >= L'a' && c <= L'z')
				c &= ~0x20;
			else if ((c < L'A' || c > L'Z') && (c < L'0' || c > L'9') && c != L'_' && c != '?')
				return false;

			mName[i] = c;
		}
	}

	return true;
}

bool ATPCLinkFileName::ParseFromNative(const wchar_t *fn) {
	int i = 0;
	bool inext = false;
	bool isescaped = false;

	if (*fn == '!' || *fn == '$') {
		++fn;
		isescaped = true;
	}

	while(wchar_t c = *fn++) {
		if (c == L'.') {
			if (i > 8)
				return false;

			while(i < 8)
				mName[i++] = L' ';

			inext = true;
			continue;
		}

		if (inext) {
			if (i >= 11)
				return false;
		} else {
			if (i >= 8)
				return false;
		}

		if (c >= L'a' && c <= L'z')
			mName[i++] = (uint8)(c - 0x20);
		else if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'_')
			mName[i++] = (uint8)c;
		else
			return false;
	}

	while(i < 11)
		mName[i++] = L' ';

	if (isescaped != IsReservedDeviceName())
		return false;

	return true;
}

void ATPCLinkFileName::AppendNative(VDStringA& s) const {
	if (IsReservedDeviceName())
		s += '!';

	for(int i=0; i<11; ++i) {
		const char c = (char)mName[i];

		if (c == ' ') {
			if (i == 8)
				break;
			continue;
		}

		if (i == 8)
			s += '.';

		s += c;
	}
}

void ATPCLinkFileName::AppendNative(VDStringW& s) const {
	if (IsReservedDeviceName())
		s += L'!';

	for(int i=0; i<11; ++i) {
		const wchar_t c = (wchar_t)mName[i];

		if (c == ' ') {
			if (i == 8)
				break;
			continue;
		}

		if (i == 8)
			s += L'.';

		s += c;
	}
}

bool ATPCLinkFileName::IsWild() const {
	for(int i=0; i<11; ++i) {
		if (mName[i] == '?')
			return true;
	}

	return false;
}

bool ATPCLinkFileName::IsReservedDeviceName() const {
	const char *const *p = kReservedDeviceNames;

	while(const char *s = *p++) {
		const uint8 *t = mName;

		while(const char c = *s++) {
			if (*t++ != c)
				goto fail;
		}

		// no reserved name is >7 chars
		if (*t == ' ')
			return true;

fail:
		;
	}

	return false;
}

bool ATPCLinkFileName::WildMatch(const ATPCLinkFileName& fn) const {
	for(int i=0; i<11; ++i) {
		const uint8 c = mName[i];
		const uint8 d = fn.mName[i];

		if (c != d && c != '?')
			return false;
	}

	return true;
}

void ATPCLinkFileName::WildMerge(const ATPCLinkFileName& fn) {
	for(int i=0; i<11; ++i) {
		const uint8 d = fn.mName[i];

		if (d != '?')
			mName[i] = d;

		if (mName[i] == ' ') {
			if (i < 8) {
				while(i < 7)
					mName[++i] = ' ';
			} else {
				while(i < 10)
					mName[++i] = ' ';
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////

ATPCLinkFileHandle::~ATPCLinkFileHandle() {
}

bool ATPCLinkFileHandle::IsOpen() const {
	return mbOpen;
}

bool ATPCLinkFileHandle::IsReadable() const {
	return mbAllowRead;
}

bool ATPCLinkFileHandle::IsWritable() const {
	return mbAllowWrite;
}

bool ATPCLinkFileHandle::IsDir() const {
	return mbIsDirectory;
}

uint32 ATPCLinkFileHandle::GetPosition() const {
	return mPos;
}

uint32 ATPCLinkFileHandle::GetLength() const {
	return mLength;
}

const ATPCLinkDirEnt& ATPCLinkFileHandle::GetDirEnt() const {
	return mDirEnt;
}

void ATPCLinkFileHandle::SetDirEnt(const ATPCLinkDirEnt& dirEnt) {
	mDirEnt = dirEnt;
}

void ATPCLinkFileHandle::AddDirEnt(const ATPCLinkDirEnt& dirEnt) {
	mDirEnts.push_back(dirEnt);
}

uint8 ATPCLinkFileHandle::OpenFile(const wchar_t *nativePath, uint32 openFlags, bool allowRead, bool allowWrite, bool append) {
	try {
		const uint32 creationMode = openFlags & nsVDFile::kCreationMask;

		mbWasCreated = false;

		switch(creationMode) {
			case nsVDFile::kOpenExisting:
			case nsVDFile::kOpenAlways:
			case nsVDFile::kTruncateExisting:
				break;

			case nsVDFile::kCreateAlways:
			case nsVDFile::kCreateNew:
				mbWasCreated = true;
				break;
		}

		if (creationMode == nsVDFile::kOpenAlways) {
			if (!mFile.openAlways(nativePath, openFlags))
				mbWasCreated = true;
		} else {
			mFile.open(nativePath, openFlags);
		}
#ifdef _WIN32
	} catch(const MyWin32Error& e) {
		return ATTranslateWin32ErrorToSIOError(e.GetWin32Error());
#endif
	} catch(const MyError&) {
		return kATCIOStat_SystemError;
	}

	mbOpen = true;
	mbIsDirectory = false;
	mbAllowRead = allowRead;
	mbAllowWrite = allowWrite;

	sint64 len = mFile.size();
	if (len > 0xffffff)
		mLength = 0xffffff;
	else
		mLength = (uint32)len;

	mPos = 0;

	if (append)
		mFile.seekNT(mLength);

	return kATCIOStat_Success;
}

void ATPCLinkFileHandle::OpenAsDirectory(const ATPCLinkFileName& dirName, const ATPCLinkFileName& pattern, uint8 attrFilter, bool isRoot) {
	std::sort(mDirEnts.begin(), mDirEnts.end(), ATPCLinkDirEntSort());
	mbOpen = true;
	mbIsDirectory = true;
	mLength = 23 * ((uint32)mDirEnts.size() + 1);
	mPos = 23;
	mbAllowRead = true;
	mbAllowWrite = false;

	memset(&mDirEnt, 0, sizeof mDirEnt);

	// For subdirectories, set a non-zero parent link. This doesn't show in DIR,
	// but it does in SC.
	if (!isRoot)
	{
		mDirEnt.mSectorMapLo = 1;
	}

	mDirEnt.mFlags = ATPCLinkDirEnt::kFlag_InUse | ATPCLinkDirEnt::kFlag_Directory;
	mDirEnt.mLengthLo = (uint8)mLength;
	mDirEnt.mLengthMid = (uint8)(mLength >> 8);
	mDirEnt.mLengthHi = (uint8)(mLength >> 16);
	memcpy(mDirEnt.mName, dirName.mName, 11);

	mDirEnts.insert(mDirEnts.begin(), mDirEnt);

	mFnextPattern = pattern;
	mFnextAttrFilter = attrFilter;
}

void ATPCLinkFileHandle::Close() {
	if (mbOpen && mPendingDate != VDDate{})
		mFile.setLastWriteTime(mPendingDate);

	mFile.closeNT();
	mbOpen = false;
	mbAllowRead = false;
	mbAllowWrite = false;
	vdfastvector<ATPCLinkDirEnt> tmp;
	mDirEnts.swap(tmp);
}

uint8 ATPCLinkFileHandle::Seek(uint32 pos) {
	if (pos > mLength && !mbAllowRead)
		return kATCIOStat_PointDLen;

	if (!mbIsDirectory) {
		try {
			mFile.seek(pos);
#ifdef _WIN32
		} catch(const MyWin32Error& e) {
			mFile.seekNT(mPos);
			return ATTranslateWin32ErrorToSIOError(e.GetWin32Error());
#endif
		} catch(const MyError&) {
			mFile.seekNT(mPos);
			return kATCIOStat_SystemError;
		}
	}

	mPos = pos;
	return kATCIOStat_Success;
}

uint8 ATPCLinkFileHandle::Read(void *dst, uint32 len, uint32& actual) {
	actual = 0;

	if (!mbOpen)
		return kATCIOStat_NotOpen;

	if (!mbAllowRead)
		return kATCIOStat_WriteOnly;

	long act = 0;

	if (mPos < mLength) {
		if (mbIsDirectory) {
			uint32 dirIndex = mPos / 23;
			uint32 offset = mPos % 23;
			uint32 left = mLength - mPos;

			if (left > len)
				left = len;
		
			while(left > 0) {
				const uint8 *src = (const uint8 *)&mDirEnts[dirIndex];
				uint32 tc = 23 - offset;
				if (tc > left)
					tc = left;

				memcpy((char *)dst + act, src + offset, tc);
				left -= tc;
				act += tc;
				offset = 0;
				++dirIndex;
			}
		} else {
			uint32 tc = len;

			if (mLength - mPos < tc)
				tc = mLength - mPos;

			act = mFile.readData(dst, tc);
			if (act < 0)
				return kATCIOStat_FatalDiskIO;
		}
	}

	actual = (uint32)act;

	g_ATLCPCLink("Read at pos %d/%d, len %d, actual %d\n", mPos, mLength, len, actual);

	mPos += actual;

	if (actual < len) {
		memset((char *)dst + actual, 0, len - actual);
		return kATCIOStat_TruncRecord;
	}

	return kATCIOStat_Success;
}

uint8 ATPCLinkFileHandle::Write(const void *dst, uint32 len) {
	if (!mbOpen)
		return kATCIOStat_NotOpen;

	if (!mbAllowWrite)
		return kATCIOStat_ReadOnly;

	uint32 tc = len;
	uint32 actual = 0;

	if (mPos < 0xffffff) {
		if (0xffffff - mPos < tc)
			tc = 0xffffff - mPos;

		actual = mFile.writeData(dst, tc);
		if (actual != tc) {
			mFile.seekNT(mPos);
			return kATCIOStat_FatalDiskIO;
		}
	}

	g_ATLCPCLink("Write at pos %d/%d, len %d, actual %d\n", mPos, mLength, len, actual);

	mPos += actual;
	if (mPos > mLength)
		mLength = mPos;

	return actual != len ? kATCIOStat_DiskFull : kATCIOStat_Success;
}

void ATPCLinkFileHandle::SetTimestamp(const uint8 tsdata[6]) {
	VDDate date = ATPCLinkDirEnt::DecodeDate(tsdata);

	mPendingDate = date;
}

bool ATPCLinkFileHandle::GetNextDirEnt(ATPCLinkDirEnt& dirEnt) {
	uint32 actual;

	while(Read(&dirEnt, 23, actual) == kATCIOStat_Success && actual >= 23) {
		ATPCLinkFileName name;
		name.ParseFromNet(dirEnt.mName);

		if (mFnextPattern.WildMatch(name) && dirEnt.TestAttrFilter(mFnextAttrFilter))
			return true;
	}

	return false;
}

///////////////////////////////////////////////////////////////////////////

void ATCreateDevicePCLink(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATPCLinkDevice> p(new ATPCLinkDevice);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefPCLink = { "pclink", "pclink", L"PCLink", ATCreateDevicePCLink };

ATPCLinkDevice::ATPCLinkDevice() {
}

ATPCLinkDevice::~ATPCLinkDevice() {
}

void *ATPCLinkDevice::AsInterface(uint32 id) {
	switch(id) {
		case IATDeviceSIO::kTypeID:
			return static_cast<IATDeviceSIO *>(this);

		case IATDeviceIndicators::kTypeID:
			return static_cast<IATDeviceIndicators *>(this);

		case IATDeviceDiagnostics::kTypeID:
			return static_cast<IATDeviceDiagnostics *>(this);
	}

	return ATDevice::AsInterface(id);
}

void ATPCLinkDevice::SetReadOnly(bool readOnly) {
	mbReadOnly = readOnly;
}

// AltirraSDL: fork fix, not yet in Windows Altirra; preserve when re-syncing
// from upstream.  PCLink keeps its SDX-side "net path" with '\' separators,
// which is correct and unchanged, but ResolveNativePath used to copy that
// path verbatim into the native path handed to the host OS.  On Linux/macOS
// a '\' is an ordinary filename character, so every PCLink subdirectory
// collapsed into one impossible file name in the mount's parent.  Only the
// native side is translated here.
#ifdef _WIN32
constexpr wchar_t kATPCLinkNativeSep = L'\\';
#else
constexpr wchar_t kATPCLinkNativeSep = L'/';
#endif

void ATPCLinkDevice::SetBasePath(const wchar_t *basePath) {
	if (VDFileIsRelativePath(basePath))
		mBasePathNative = VDMakePath(VDGetProgramPath().c_str(), basePath);
	else
		mBasePathNative = basePath;

	if (!mBasePathNative.empty() && !VDIsPathSeparator(mBasePathNative.back()))
		mBasePathNative += kATPCLinkNativeSep;
}

void ATPCLinkDevice::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefPCLink;
}

void ATPCLinkDevice::GetSettings(ATPropertySet& settings) {
	if (mbReadOnly)
		settings.Unset("write");
	else
		settings.SetBool("write", true);

	if (mbSetTimestamps)
		settings.SetBool("set_timestamps", true);
	else
		settings.Unset("set_timestamps");

	settings.SetString("path", mBasePathNative.c_str());
}

bool ATPCLinkDevice::SetSettings(const ATPropertySet& settings) {
	mbSetTimestamps = settings.GetBool("set_timestamps");
	SetReadOnly(!settings.GetBool("write"));
	SetBasePath(settings.GetString("path", L""));
	return true;
}

void ATPCLinkDevice::Shutdown() {
	AbortCommand();

	mpUIRenderer = nullptr;

	mpSIOInterface = nullptr;
	mpSIOMgr = nullptr;
}

void ATPCLinkDevice::ColdReset() {
	for(auto& fh : mFileHandles)
		fh.Close();
}

void ATPCLinkDevice::InitIndicators(IATDeviceIndicatorManager *uir) {
	mpUIRenderer = uir;
}

void ATPCLinkDevice::InitSIO(IATDeviceSIOManager *mgr) {
	mpSIOMgr = mgr;
	mpSIOInterface = mpSIOMgr->AddDevice(this);
}

IATDeviceSIO::CmdResponse ATPCLinkDevice::OnSerialBeginCommand(const ATDeviceSIOCommand& cmd) {
	if (cmd.mDevice != 0x6F)
		return kCmdResponse_NotHandled;

	if (mBasePathNative.empty())
		return kCmdResponse_NotHandled;

	if (!cmd.mbStandardRate) {
		if (cmd.mCyclesPerBit < 30 || cmd.mCyclesPerBit > 34)
			return kCmdResponse_NotHandled;
	}

	const uint8 commandId = cmd.mCommand & 0x7f;

	mCommandAux1 = cmd.mAUX[0];
	mCommandAux2 = cmd.mAUX[1];

	Command command = kCommandNone;

	if (commandId == 0x53)			// status
		command = kCommandStatus;
	else if (commandId == 0x50)		// put
		command = kCommandPut;
	else if (commandId == 0x52)		// read
		command = kCommandRead;
	else if (commandId == 0x3F)
		command = kCommandGetHiSpeedIndex;
	else {
		g_ATLCPCLink("Unsupported command $%02x\n", cmd);
		return kCmdResponse_Fail_NAK;
	}

	mpSIOInterface->BeginCommand();

	// High-speed via bit 7 uses 38400 baud.
	// High-speed via HS command frame uses 52Kbaud. Currently we use US Doubler timings.
	if (cmd.mCommand & 0x80)
		mpSIOInterface->SetTransferRate(45, 450);
	else if (!cmd.mbStandardRate)
		mpSIOInterface->SetTransferRate(34, 394);

	mpSIOInterface->SendACK();

	BeginCommand(command);
	return kCmdResponse_Start;
}

void ATPCLinkDevice::OnSerialAbortCommand() {
	AbortCommand();
}

void ATPCLinkDevice::OnSerialReceiveComplete(uint32 id, const void *data, uint32 len, bool checksumOK) {
	mpReceiveFn(data, len);
}

void ATPCLinkDevice::OnSerialFence(uint32 id) {
	mpFenceFn();
}

IATDeviceSIO::CmdResponse ATPCLinkDevice::OnSerialAccelCommand(const ATDeviceSIORequest& request) {
	return OnSerialBeginCommand(request);
}

void ATPCLinkDevice::DumpStatus(ATConsoleOutput& output) {
	output("Native base path: %ls", mBasePathNative.c_str());
	output("Current directory: %s", mCurDir.c_str());

	output("");

	VDStringA s;
	for(int i=0; i<15; ++i) {
		ATPCLinkFileHandle& fh = mFileHandles[i];

		s.sprintf("Handle $%02x: ", i + 1);

		if (!fh.IsOpen()) {
			s += "Not open";
		} else {
			if (fh.IsDir())
				s += "Directory";
			else if (fh.IsReadable()) {
				if (fh.IsWritable())
					s += "Read/Write";
				else
					s += "Read";
			} else
				s += "Write";

			s += " [";

			ATPCLinkFileName fn;

			fn.ParseFromNet(fh.GetDirEnt().mName);
			fn.AppendNative(s);
			s += ']';
		}

		output <<= s.c_str();
	}
}

void ATPCLinkDevice::BeginCommand(Command cmd) {
	mCommand = cmd;
	mCommandPhase = 0;

	AdvanceCommand();
}

void ATPCLinkDevice::AbortCommand() {
	if (mCommand) {
		mCommand = kCommandNone;
		mCommandPhase = 0;

		mpSIOInterface->EndCommand();
	}
}

void ATPCLinkDevice::AdvanceCommand() {
	switch(mCommand) {
		case kCommandGetHiSpeedIndex:
			g_ATLCPCLink("Sending high-speed index\n");
			mpSIOInterface->SendComplete();
			{
				uint8 hsindex = 9;
				mpSIOInterface->SendData(&hsindex, 1, true);
			}
			mpSIOInterface->EndCommand();
			break;

		case kCommandStatus:
			g_ATLCPCLink("Sending status: Flags=$%02x, Error=%3d, Length=%02x%02x\n", mStatusFlags, mStatusError, mStatusLengthHi, mStatusLengthLo);
			mpSIOInterface->SendComplete();
			{
				const uint8 data[4] = {
					mStatusFlags,
					mStatusError,
					mStatusLengthLo,
					mStatusLengthHi
				};

				mpSIOInterface->SendData(data, 4, true);
			}
			mpSIOInterface->EndCommand();
			break;

		case kCommandPut:
			mpReceiveFn = [this](const void *src, uint32 len) {
				memcpy(&mParBuf, src, std::min<uint32>(len, sizeof(mParBuf)));
			};
			mpSIOInterface->ReceiveData(0, mCommandAux1 ? mCommandAux1 : 256, true);
			mpFenceFn = [this]() {
				if (OnPut())
					mpSIOInterface->SendComplete();
				else
					mpSIOInterface->SendError();
				mpSIOInterface->EndCommand();
			};
			mpSIOInterface->InsertFence(0);
			break;

		case kCommandRead:
			// fwrite ($01) is special
			if (mParBuf.mFunction == 0x01) {
				mpReceiveFn = [this](const void *src, uint32 len) {
					memcpy(mTransferBuffer, src, len);
				};
				mpSIOInterface->ReceiveData(0, mParBuf.mF[0] + ((uint32)mParBuf.mF[1] << 8), true);
				mpSIOInterface->InsertFence(0);
				mpFenceFn = [this]() {
					OnRead();
					mpSIOInterface->EndCommand();
				};
				mpSIOInterface->SendComplete();
			} else {
				mpSIOInterface->SendComplete();
				mpFenceFn = [this]() {
					OnRead();
					mpSIOInterface->EndCommand();
				};
				mpSIOInterface->InsertFence(0);
			}

			break;
	}
}

void ATPCLinkDevice::FinishCommand() {
	AbortCommand();
}

bool ATPCLinkDevice::OnPut() {
	switch(mParBuf.mFunction) {
		case 0:		// fread
			{
				uint32 bufLen = mParBuf.mF[0] + 256*mParBuf.mF[1];
				g_ATLCPCLink("Received fread($%02x,%d) command.\n", mParBuf.mHandle, bufLen);

				if (CheckValidFileHandle(true)) {
					ATPCLinkFileHandle& fh = mFileHandles[mParBuf.mHandle - 1];

					if (!fh.IsOpen()) {
						mStatusError = kATCIOStat_NotOpen;
						return true;
					}

					if (!fh.IsReadable()) {
						mStatusError = kATCIOStat_WriteOnly;
						return true;
					}

					const uint32 pos = fh.GetPosition();
					const uint32 len = fh.GetLength();


					if (pos >= len)
						bufLen = 0;
					else if (len - pos < bufLen)
						bufLen = len - pos;

					mStatusLengthLo = (uint8)bufLen;
					mStatusLengthHi = (uint8)(bufLen >> 8);
					mStatusError = bufLen ? kATCIOStat_Success : kATCIOStat_EndOfFile;
				}
			}
			return true;

		case 1:		// fwrite
			if (mbReadOnly) {
				mStatusError = kATCIOStat_ReadOnly;
				return true;
			}

			{
				uint32 bufLen = mParBuf.mF[0] + 256*mParBuf.mF[1];
				g_ATLCPCLink("Received fwrite($%02x,%d) command.\n", mParBuf.mHandle, bufLen);

				if (CheckValidFileHandle(true)) {
					ATPCLinkFileHandle& fh = mFileHandles[mParBuf.mHandle - 1];

					if (!fh.IsOpen()) {
						mStatusError = kATCIOStat_NotOpen;
						return true;
					}

					if (!fh.IsWritable()) {
						mStatusError = kATCIOStat_ReadOnly;
						return true;
					}

					const uint32 pos = fh.GetPosition();

					if (pos >= 0xffffff)
						bufLen = 0;
					else if (0xffffff - pos < bufLen)
						bufLen = 0xffffff - pos;

					mStatusLengthLo = (uint8)bufLen;
					mStatusLengthHi = (uint8)(bufLen >> 8);
					mStatusError = bufLen ? kATCIOStat_Success : kATCIOStat_DiskFull;
				}
			}
			return true;

		case 2:		// fseek
			{
				const uint32 pos = mParBuf.mF[0] + ((uint32)mParBuf.mF[1] << 8) + ((uint32)mParBuf.mF[2] << 16);
				g_ATLCPCLink("Received fseek($%02x,%d) command.\n", mParBuf.mHandle, pos);

				if (CheckValidFileHandle(true)) {
					ATPCLinkFileHandle& fh = mFileHandles[mParBuf.mHandle - 1];

					if (!fh.IsOpen()) {
						mStatusError = kATCIOStat_NotOpen;
						return true;
					}

					mStatusError = fh.Seek(pos);
				}
			}
			return true;

		case 3:		// ftell
			g_ATLCPCLink("Received ftell($%02x) command.\n", mParBuf.mHandle);

			if (!CheckValidFileHandle(true))
				return true;

			{
				ATPCLinkFileHandle& fh = mFileHandles[mParBuf.mHandle - 1];

				if (!fh.IsOpen()) {
					mStatusError = kATCIOStat_NotOpen;
					return true;
				}

				mStatusError = kATCIOStat_Success;
			}
			return true;

		case 4:		// flen
			if (!CheckValidFileHandle(true))
				return true;

			mStatusError = kATCIOStat_Success;
			return true;

		case 5:		// reserved
			mStatusError = kATCIOStat_NotSupported;
			return true;

		case 6:		// fnext
			g_ATLCPCLink("Received fnext($%02x) command.\n", mParBuf.mHandle);
			if (!CheckValidFileHandle(true))
				return true;

			mStatusError = kATCIOStat_Success;
			return true;

		case 7:		// fclose
			g_ATLCPCLink("Received close($%02x) command.\n", mParBuf.mHandle);
			if (CheckValidFileHandle(false))
				mFileHandles[mParBuf.mHandle - 1].Close();

			mStatusError = kATCIOStat_Success;
			return true;

		case 8:		// init
			g_ATLCPCLink("Received init command.\n");
			for(size_t i = 0; i < sizeof(mFileHandles)/sizeof(mFileHandles[0]); ++i)
				mFileHandles[i].Close();

			mStatusFlags = 0;
			mStatusError = kATCIOStat_Success;
			mStatusLengthLo = 0;
			mStatusLengthHi = 0x6F;

			mCurDir.clear();
			return true;

		case 9:		// fopen
		case 10:	// ffirst
			if (g_ATLCPCLink.IsEnabled()) {
				char pathBuf[66];

				memcpy(pathBuf, mParBuf.mPath, 65);
				pathBuf[65] = 0;

				if (mParBuf.mFunction == 9)
					g_ATLCPCLink("Received fopen() command: [%s]\n", pathBuf);
				else
					g_ATLCPCLink("Received ffirst() command: [%s]\n", pathBuf);
			}

			OnReadActivity();
			{
				mParBuf.mHandle = 1;

				for(;;) {
					if (mParBuf.mHandle > sizeof(mFileHandles)/sizeof(mFileHandles[0])) {
						mStatusError = kATCIOStat_TooManyFiles;
						return true;
					}

					if (!mFileHandles[mParBuf.mHandle - 1].IsOpen())
						break;

					++mParBuf.mHandle;
				}

				ATPCLinkFileHandle& fh = mFileHandles[mParBuf.mHandle - 1];
				VDStringA netPath;
				VDStringW nativePath;

				if (!ResolvePath(netPath) || !ResolveNativePath(nativePath, netPath))
					return true;

				ATPCLinkFileName pattern;

				if (!pattern.ParseFromNet(mParBuf.mName1))
					return true;

				const bool openDir = mParBuf.mFunction == 10 || (mParBuf.mMode & 0x10) != 0;

				VDDirectoryIterator it(VDMakePath(nativePath.c_str(), L"*").c_str());
				ATPCLinkDirEnt dirEnt;
				VDStringW nativeFilePath;

				bool matched = false;
				while(it.Next()) {
					ATPCLinkFileName fn;

					if (!fn.ParseFromNative(it.GetName()))
						continue;

					// We can't filter at this point for a directory, because the byte stream
					// needs to reflect all files while the FNEXT output shouldn't. Therefore,
					// we need to cache the pattern with the file handle instead.
					if (!openDir && !pattern.WildMatch(fn))
						continue;

					sint64 len = it.GetSize();

					if (len > 0xFFFFFF)
						len = 0xFFFFFF;

					dirEnt.SetFlagsFromAttributes(it.GetAttributes());

					if (!IsDirEntIncluded(dirEnt))
						continue;

					dirEnt.mSectorMapLo = 0;
					dirEnt.mSectorMapHi = 0;
					dirEnt.mLengthLo = (uint8)len;
					dirEnt.mLengthMid = (uint8)((uint32)len >> 8);
					dirEnt.mLengthHi = (uint8)((uint32)len >> 16);
					memcpy(dirEnt.mName, fn.mName, sizeof dirEnt.mName);
					dirEnt.SetDate(it.IsDirectory() ? it.GetCreationDate() : it.GetLastWriteDate());

					if (!openDir) {
						matched = true;
						nativeFilePath = it.GetFullPath();
						break;
					}

					fh.AddDirEnt(dirEnt);
				}

				if (openDir) {
					// extract name from net path
					const char *s = netPath.c_str();
					const char *t = strrchr(s, '\\');
					if (t)
						s = t + 1;

					const char *end = s + strlen(s);
					const char *ext = strchr(s, '.');
					const char *fnend = ext ? ext : end;

					size_t fnlen = fnend - s;
					size_t extlen = end - ext;

					if (fnlen > 8)
						fnlen = 8;

					if (extlen > 3)
						extlen = 8;

					ATPCLinkFileName dirName;
					memset(dirName.mName, ' ', 11);

					const bool isRoot = (fnlen == 0);
					if (isRoot)
						memcpy(dirName.mName, "MAIN", 4);
					else
						memcpy(dirName.mName, s, fnlen);

					if (ext)
						memcpy(dirName.mName + 8, ext, extlen);

					fh.OpenAsDirectory(dirName, pattern, mParBuf.mAttr1, isRoot);

					mStatusError = kATCIOStat_Success;
				} else {
					if (!matched) {
						// cannot create file with a wildcard
						if (!(mParBuf.mMode & 4) && pattern.IsWild()) {
							mStatusError = kATCIOStat_IllegalWild;
							return true;
						}

						nativeFilePath = nativePath;
						pattern.AppendNative(nativeFilePath);
					}

					if ((mParBuf.mMode & 8) && mbReadOnly) {
						mStatusError = kATCIOStat_ReadOnly;
					} else {
						bool setTimestamp = false;

						switch(mParBuf.mMode & 15) {
							case 4:		// read
								if (!matched)
									mStatusError = kATCIOStat_FileNotFound;
								else {
									mStatusError = fh.OpenFile(nativeFilePath.c_str(),
										nsVDFile::kRead | nsVDFile::kDenyWrite | nsVDFile::kOpenExisting,
										true, false, false);
								}
								break;

							case 8:		// write
								mStatusError = fh.OpenFile(nativeFilePath.c_str(),
									nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways,
									false, true, false);
								setTimestamp = true;
								break;

							case 9:		// append
								mStatusError = fh.OpenFile(nativeFilePath.c_str(),
									nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kOpenAlways,
									false, true, true);

								setTimestamp = true;
								break;

							case 12:	// update
								if (!matched)
									mStatusError = kATCIOStat_FileNotFound;
								else {
									mStatusError = fh.OpenFile(nativeFilePath.c_str(),
										nsVDFile::kReadWrite | nsVDFile::kDenyAll | nsVDFile::kOpenExisting,
										true, true, false);

									setTimestamp = true;
								}
								break;

							default:
								mStatusError = kATCIOStat_InvalidCmd;
								break;
						}

						if (mbSetTimestamps && setTimestamp && fh.IsOpen()) {
							fh.SetTimestamp(mParBuf.mF);
						}
					}

					fh.SetDirEnt(dirEnt);
				}
			}
			return true;

		case 11:	// rename
			g_ATLCPCLink("Received rename() command.\n");

			if (mbReadOnly) {
				mStatusError = kATCIOStat_ReadOnly;
				return true;
			}

			OnWriteActivity();
			{
				VDStringW path;

				if (!ResolveNativePath(path))
					return true;

				ATPCLinkFileName srcpat;
				ATPCLinkFileName dstpat;

				if (!srcpat.ParseFromNet(mParBuf.mName1)
					|| !dstpat.ParseFromNet(mParBuf.mName2))
				{
					mStatusError = kATCIOStat_FileNameErr;
					return true;
				}

				VDStringW srcNativePath;
				VDStringW dstNativePath;

				try {
					VDDirectoryIterator it(VDMakePath(path.c_str(), L"*").c_str());
					bool matched = false;

					while(it.Next()) {
						ATPCLinkDirEnt dirEnt;
						dirEnt.SetFlagsFromAttributes(it.GetAttributes());

						if (!IsDirEntIncluded(dirEnt))
							continue;

						ATPCLinkFileName fn;
						if (!fn.ParseFromNative(it.GetName()))
							continue;

						if (!srcpat.WildMatch(fn))
							continue;

						ATPCLinkFileName fn2(fn);
						fn2.WildMerge(dstpat);

						if (fn == fn2)
							continue;

						srcNativePath = path;
						fn.AppendNative(srcNativePath);

						dstNativePath = path;
						fn2.AppendNative(dstNativePath);

						VDMoveFile(srcNativePath.c_str(), dstNativePath.c_str());
						matched = true;
					}

					if (matched)
						mStatusError = kATCIOStat_Success;
					else
						mStatusError = kATCIOStat_FileNotFound;
#ifdef _WIN32
				} catch(const MyWin32Error& e) {
					mStatusError = ATTranslateWin32ErrorToSIOError(e.GetWin32Error());
#endif
				} catch(const MyError&) {
					mStatusError = kATCIOStat_SystemError;
				}
			}
			return true;

		case 12:	// remove
			g_ATLCPCLink("Received remove() command.\n");

			if (mbReadOnly) {
				mStatusError = kATCIOStat_ReadOnly;
				return true;
			}

			OnWriteActivity();
			{
				VDStringW resultPath;

				if (!ResolveNativePath(resultPath))
					return true;

				ATPCLinkFileName fname;
				if (!fname.ParseFromNet(mParBuf.mName1)) {
					mStatusError = kATCIOStat_FileNameErr;
					return true;
				}

				if (fname.IsWild()) {
					VDDirectoryIterator it(VDMakePath(resultPath.c_str(), L"*").c_str());
					bool matched = false;

					while(it.Next()) {
						if (it.IsDirectory())
							continue;

						ATPCLinkDirEnt dirEnt;
						dirEnt.SetFlagsFromAttributes(it.GetAttributes());

						if (!IsDirEntIncluded(dirEnt))
							continue;

						ATPCLinkFileName entryName;
						if (!entryName.ParseFromNative(it.GetName()))
							continue;

						if (!fname.WildMatch(entryName))
							continue;

						try {
							VDRemoveFile(it.GetFullPath().c_str());
							matched = true;
#ifdef _WIN32
						} catch(const MyWin32Error& e) {
							mStatusError = ATTranslateWin32ErrorToSIOError(e.GetWin32Error());
							return true;
#endif
						} catch(const MyError&) {
							mStatusError = kATCIOStat_SystemError;
							return true;
						}
					}

					if (!matched) {
						mStatusError = kATCIOStat_FileNotFound;
						return true;
					}
				} else {
					fname.AppendNative(resultPath);

					try {
						uint32 attrs = VDFileGetAttributes(resultPath.c_str());

						if (attrs == kVDFileAttr_Invalid) {
							mStatusError = kATCIOStat_FileNotFound;
							return true;
						}

						ATPCLinkDirEnt dirEnt;
						dirEnt.SetFlagsFromAttributes(attrs);

						if (IsDirEntIncluded(dirEnt))
							VDRemoveFile(resultPath.c_str());
						else {
							mStatusError = kATCIOStat_FileNotFound;
							return true;
						}
#ifdef _WIN32
					} catch(const MyWin32Error& e) {
						mStatusError = ATTranslateWin32ErrorToSIOError(e.GetWin32Error());
						return true;
#endif
					} catch(const MyError&) {
						mStatusError = kATCIOStat_SystemError;
						return true;
					}
				}

				mStatusError = kATCIOStat_Success;
			}
			return true;

		case 13:	// chmod
			g_ATLCPCLink("Received chmod() command.\n");

			if (mbReadOnly) {
				mStatusError = kATCIOStat_ReadOnly;
				return true;
			}

			OnWriteActivity();
			{
				VDStringW path;

				if (!ResolveNativePath(path))
					return true;

				ATPCLinkFileName srcpat;

				if (!srcpat.ParseFromNet(mParBuf.mName1)) {
					mStatusError = kATCIOStat_FileNameErr;
					return true;
				}

				VDStringW srcNativePath;

				try {
					VDDirectoryIterator it(VDMakePath(path.c_str(), L"*").c_str());
					bool matched = false;

					while(it.Next()) {
						ATPCLinkDirEnt dirEnt;
						dirEnt.SetFlagsFromAttributes(it.GetAttributes());

						if (!IsDirEntIncluded(dirEnt))
							continue;

						ATPCLinkFileName fn;
						if (!fn.ParseFromNative(it.GetName()))
							continue;

						if (!srcpat.WildMatch(fn))
							continue;

						srcNativePath = path;
						fn.AppendNative(srcNativePath);

						uint32 attrMask = 0;
						uint32 attrVals = 0;

						if (mParBuf.mAttr2 & 0x11)
							attrMask |= kVDFileAttr_ReadOnly;

						if (mParBuf.mAttr2 & 0x22)
							attrMask |= kVDFileAttr_Hidden;

						if (mParBuf.mAttr2 & 0x44)
							attrMask |= kVDFileAttr_Archive;

						if (mParBuf.mAttr2 & 0x01)
							attrVals |= kVDFileAttr_ReadOnly;

						if (mParBuf.mAttr2 & 0x02)
							attrVals |= kVDFileAttr_Hidden;

						if (mParBuf.mAttr2 & 0x04)
							attrVals |= kVDFileAttr_Archive;

						VDFileSetAttributes(srcNativePath.c_str(), attrMask, attrVals);

						matched = true;
					}

					if (matched)
						mStatusError = kATCIOStat_Success;
					else
						mStatusError = kATCIOStat_FileNotFound;
#ifdef _WIN32
				} catch(const MyWin32Error& e) {
					mStatusError = ATTranslateWin32ErrorToSIOError(e.GetWin32Error());
#endif
				} catch(const MyError&) {
					mStatusError = kATCIOStat_SystemError;
				}
			}
			return true;

		case 14:	// mkdir
			g_ATLCPCLink("Received mkdir() command.\n");

			if (mbReadOnly) {
				mStatusError = kATCIOStat_ReadOnly;
				return true;
			}

			OnWriteActivity();
			{
				VDStringW resultPath;

				if (!ResolveNativePath(resultPath))
					return true;

				ATPCLinkFileName fname;
				if (!fname.ParseFromNet(mParBuf.mName1)) {
					mStatusError = kATCIOStat_FileNameErr;
					return true;
				}

				if (fname.IsWild()) {
					mStatusError = kATCIOStat_IllegalWild;
					return true;
				}

				fname.AppendNative(resultPath);

				try {
					VDCreateDirectory(resultPath.c_str());

					if (mbSetTimestamps) {
						VDDate date = ATPCLinkDirEnt::DecodeDate(mParBuf.mF);

						if (date != VDDate {})
							VDSetDirectoryCreationTime(resultPath.c_str(), date);
					}

#ifdef _WIN32
				} catch(const MyWin32Error& e) {
					mStatusError = ATTranslateWin32ErrorToSIOError(e.GetWin32Error());
					return true;
#endif
				} catch(const MyError&) {
					mStatusError = kATCIOStat_SystemError;
					return true;
				}

				mStatusError = kATCIOStat_Success;
			}
			return true;

		case 15:	// rmdir
			g_ATLCPCLink("Received rmdir() command.\n");

			if (mbReadOnly) {
				mStatusError = kATCIOStat_ReadOnly;
				return true;
			}

			OnWriteActivity();
			{
				VDStringW resultPath;

				if (!ResolveNativePath(resultPath))
					return true;

				ATPCLinkFileName fname;
				if (!fname.ParseFromNet(mParBuf.mName1)) {
					mStatusError = kATCIOStat_FileNameErr;
					return true;
				}

				if (fname.IsWild()) {
					mStatusError = kATCIOStat_IllegalWild;
					return true;
				}

				fname.AppendNative(resultPath);

				try {
					VDRemoveDirectory(resultPath.c_str());
#ifdef _WIN32
				} catch(const MyWin32Error& e) {
					mStatusError = ATTranslateWin32ErrorToSIOError(e.GetWin32Error());
					return true;
#endif
				} catch(const MyError&) {	
					mStatusError = kATCIOStat_SystemError;
					return true;
				}

				mStatusError = kATCIOStat_Success;
			}
			return true;

		case 16:	// chdir
			if (g_ATLCPCLink.IsEnabled()) {
				char pathBuf[66];
				memcpy(pathBuf, mParBuf.mPath, 65);
				pathBuf[65] = 0;

				g_ATLCPCLink("Received chdir() command: [%s]\n", pathBuf);
			}

			{
				VDStringA resultPath;

				if (!ResolvePath(resultPath))
					return true;

				if (resultPath.size() > 64) {
					mStatusError = kATCIOStat_PathTooLong;
					return true;
				}

				VDStringW nativePath;
				if (!ResolveNativePath(nativePath, resultPath))
					return true;

				uint32 attr = VDFileGetAttributes(nativePath.c_str());

				if (attr == kVDFileAttr_Invalid || !(attr & kVDFileAttr_Directory)) {
					mStatusError = kATCIOStat_PathNotFound;
					return true;
				}

				mCurDir = resultPath;

				mStatusError = kATCIOStat_Success;
			}
			return true;

		case 17:	// getcwd
			mStatusError = kATCIOStat_Success;
			return true;

		case 18:	// setboot
			mStatusError = kATCIOStat_NotSupported;
			return true;

		case 19:	// getdfree
			mStatusError = kATCIOStat_Success;
			return true;
	}

	g_ATLCPCLink("Unsupported put for function $%02x\n", mParBuf.mFunction);
	mStatusError = kATCIOStat_NotSupported;
	return true;
}

bool ATPCLinkDevice::OnRead() {
	switch(mParBuf.mFunction) {
		case 0:		// fread
			OnReadActivity();
			{
				uint32 blocklen = mStatusLengthLo + ((uint32)mStatusLengthHi << 8);
				if (CheckValidFileHandle(true)) {
					ATPCLinkFileHandle& fh = mFileHandles[mParBuf.mHandle - 1];
					uint32 actual = 0;

					mStatusError = fh.Read(mTransferBuffer, blocklen, actual);

					mStatusLengthLo = (uint8)actual;
					mStatusLengthHi = (uint8)(actual >> 8);
				}

				mpSIOInterface->SendData(mTransferBuffer, blocklen, true);
			}
			return true;

		case 1:		// fwrite
			if (mbReadOnly) {
				mStatusError = kATCIOStat_ReadOnly;
				return true;
			}

			OnWriteActivity();
			{
				uint32 blocklen = mStatusLengthLo + ((uint32)mStatusLengthHi << 8);
				if (CheckValidFileHandle(true)) {
					ATPCLinkFileHandle& fh = mFileHandles[mParBuf.mHandle - 1];

					mStatusError = fh.Write(mTransferBuffer, blocklen);
				}
			}
			return true;

		case 3:		// ftell
			if (CheckValidFileHandle(true)) {
				ATPCLinkFileHandle& fh = mFileHandles[mParBuf.mHandle - 1];

				if (!fh.IsOpen()) {
					mStatusError = kATCIOStat_NotOpen;
					return true;
				}

				const uint32 len = fh.GetPosition();
				mTransferBuffer[0] = (uint8)len;
				mTransferBuffer[1] = (uint8)(len >> 8);
				mTransferBuffer[2] = (uint8)(len >> 16);
				mStatusError = kATCIOStat_Success;
			}
			mpSIOInterface->SendData(mTransferBuffer, 3, true);
			return true;

		case 4:		// flen
			memset(mTransferBuffer, 0, 3);
			if (CheckValidFileHandle(true)) {
				ATPCLinkFileHandle& fh = mFileHandles[mParBuf.mHandle - 1];

				if (!fh.IsOpen())
					mStatusError = kATCIOStat_NotOpen;
				else {
					uint32 len = fh.GetLength();

					mTransferBuffer[0] = (uint8)len;
					mTransferBuffer[1] = (uint8)(len >> 8);
					mTransferBuffer[2] = (uint8)(len >> 16);
				}
			}
			mpSIOInterface->SendData(mTransferBuffer, 3, true);
			return true;

		case 5:		// reserved
			mStatusError = kATCIOStat_NotSupported;
			return true;

		case 6:		// fnext
			OnReadActivity();
			memset(mTransferBuffer, 0, sizeof(ATPCLinkDirEnt) + 1);

			if (CheckValidFileHandle(true)) {
				ATPCLinkFileHandle& fh = mFileHandles[mParBuf.mHandle - 1];

				if (!fh.IsDir()) {
					mStatusError = kATCIOStat_BadParameter;
				} else {
					ATPCLinkDirEnt dirEnt = {0};

					if (!fh.GetNextDirEnt(dirEnt))
						mStatusError = kATCIOStat_EndOfFile;
					else
						mStatusError = kATCIOStat_Success;

					memcpy(mTransferBuffer + 1, &dirEnt, sizeof(ATPCLinkDirEnt));
				}
			}

			mTransferBuffer[0] = mStatusError;
			mpSIOInterface->SendData(mTransferBuffer, sizeof(ATPCLinkDirEnt) + 1, true);
			return true;

		case 7:		// fclose
		case 8:		// init
			mStatusError = kATCIOStat_NotSupported;
			return true;

		case 9:		// open
		case 10:	// ffirst
			mTransferBuffer[0] = mParBuf.mHandle;
			memset(mTransferBuffer + 1, 0, sizeof(ATPCLinkDirEnt));

			if (CheckValidFileHandle(true)) {
				ATPCLinkFileHandle& fh = mFileHandles[mParBuf.mHandle - 1];

				const ATPCLinkDirEnt dirEnt = fh.GetDirEnt();

				mStatusError = kATCIOStat_Success;

				memcpy(mTransferBuffer + 1, &dirEnt, sizeof(ATPCLinkDirEnt));
			}
			mpSIOInterface->SendData(mTransferBuffer, sizeof(ATPCLinkDirEnt) + 1, true);
			return true;

		case 11:	// rename
		case 12:	// remove
		case 13:	// chmod
		case 14:	// mkdir
		case 15:	// rmdir
		case 16:	// chdir
			mStatusError = kATCIOStat_NotSupported;
			return true;

		case 17:	// getcwd
			{
				memset(mTransferBuffer, 0, 65);
				strncpy((char *)mTransferBuffer, mCurDir.c_str(), 64);
				mpSIOInterface->SendData(mTransferBuffer, 64, true);
			}
			return true;

		case 18:	// setboot
			mStatusError = kATCIOStat_NotSupported;
			return true;

		case 19:	// getdfree
			{
				ATPCLinkDiskInfo diskInfo = {};

				VDASSERTCT(sizeof(diskInfo) == 64);

				diskInfo.mInfoVersion = 0x21;
				diskInfo.mSectorCountLo = 0xff;
				diskInfo.mSectorCountHi = 0xff;
				diskInfo.mSectorsFreeLo = 0xff;
				diskInfo.mSectorsFreeHi = 0xff;
				diskInfo.mBytesPerSectorCode = 1;
				diskInfo.mVersion = 0x80;
				diskInfo.mBytesPerSectorHi = 2;
				diskInfo.mSectorsPerCluster = 1;
				memcpy(diskInfo.mVolumeLabel, "PCLink  ", 8);

				memcpy(mTransferBuffer, &diskInfo, 64);
				mpSIOInterface->SendData(mTransferBuffer, 64, true);
			}
			return true;

		case 20:	// chvol
			mStatusError = kATCIOStat_NotSupported;
			return true;
	}

	return false;
}

bool ATPCLinkDevice::CheckValidFileHandle(bool setError) {
	if (mParBuf.mHandle == 0 || mParBuf.mHandle >= 16) {
		if (setError)
			mStatusError = kATCIOStat_InvalidIOCB;

		return false;
	}

	return true;
}

bool ATPCLinkDevice::IsDirEntIncluded(const ATPCLinkDirEnt& dirEnt) const {
	return dirEnt.TestAttrFilter(mParBuf.mAttr1);
}

bool ATPCLinkDevice::ResolvePath(VDStringA& resultPath) {
	char pathBuf[66];

	memcpy(pathBuf, mParBuf.mPath, 65);
	pathBuf[65] = 0;

	uint8 status = ResolvePathStatic(pathBuf, mCurDir.c_str(), resultPath);

	if (status)
		mStatusError = status;

	return status == kATCIOStat_Success;
}

uint8 ATPCLinkDevice::ResolvePathStatic(const char *path, const char *curDir, VDStringA& resultPath) {
	const char *s = path;

	// check for absolute path
	if (*s == '>' || *s == '\\') {
		resultPath.clear();
		++s;
	} else
		resultPath = curDir;

	// parse out remaining components

	bool inext = false;
	int fnchars = 0;
	int extchars = 0;

	while(uint8 c = *s++) {
		// check for path separator
		if (c == '>' || c == '\\') {
			// doubled up path separators are not allowed by SDX
			if (!extchars && !fnchars)
				return kATCIOStat_FileNameErr;

			if (fnchars || inext)
				resultPath += '\\';

			inext = false;
			fnchars = 0;
			extchars = 0;
			continue;
		}

		// check for upward traversal
		if (c == '<') {
			// must be at start of path component
			if (fnchars || inext)
				return kATCIOStat_FileNameErr;

			// remove a component
			if (!resultPath.empty() && resultPath.back() == '\\')
				resultPath.pop_back();

			// upward traversal from the root is not allowed by SDX
			if (resultPath.empty())
				return kATCIOStat_PathNotFound;

			while(!resultPath.empty()) {
				uint8 c = resultPath.back();

				resultPath.pop_back();

				if (c == '\\')
					break;
			}

			continue;
		}

		if (c == '.') {
			if (!fnchars) {
				if (s[0] == '.' && (s[1] == 0 || s[1] == '>' || s[1] == '\\')) {
					// remove a component
					if (!resultPath.empty() && resultPath.back() == '\\')
						resultPath.pop_back();

					for(;;) {
						// upward traversal from the root is not allowed by SDX
						if (resultPath.empty())
							return kATCIOStat_PathNotFound;

						uint8 c = resultPath.back();

						resultPath.pop_back();

						if (c == '\\')
							break;
					}

					++s;

					if (!*s)
						break;

					++s;
					continue;
				}
			}

			if (inext)
				return kATCIOStat_FileNameErr;

			if (!fnchars && (resultPath.empty() || resultPath.back() != '\\'))
				resultPath += '\\';

			resultPath += '.';
			inext = true;
			continue;
		}

		// we always add a \ here even if the path is empty -- in SDX, the MAIN
		// has an empty cwd, but the first dir FOO is at \FOO
		if (!fnchars && !inext && (resultPath.empty() || resultPath.back() != '\\'))
			resultPath += '\\';

		if ((uint8)(c - 'a') < 26)
			c &= ~0x20;

		if (c != '_' && (uint8)(c - '0') >= 10 && (uint8)(c - 'A') >= 26)
			return kATCIOStat_FileNameErr;

		if (inext) {
			if (++extchars > 3)
				return kATCIOStat_FileNameErr;
		} else {
			if (++fnchars > 8)
				return kATCIOStat_FileNameErr;
		}

		resultPath += c;
	}

	if (inext) {
		// plain dot is invalid
		if (!extchars && !fnchars)
			return kATCIOStat_FileNameErr;

		// trailing dot is not significant
		if (!extchars)
			resultPath.pop_back();
	}

	// strip off trailing separator if present
	if (!resultPath.empty() && resultPath.back() == '\\')
		resultPath.pop_back();

	return kATCIOStat_Success;
}

bool ATPCLinkDevice::ResolveNativePath(VDStringW& resultPath) {
	VDStringA netPath;

	if (!ResolvePath(netPath))
		return false;

	return ResolveNativePath(resultPath, netPath);
}

bool ATPCLinkDevice::ResolveNativePath(VDStringW& resultPath, const VDStringA& netPath) {

	// translate path
	resultPath = mBasePathNative;

	for(VDStringA::const_iterator it(netPath.begin()), itEnd(netPath.end());
		it != itEnd;
		++it)
	{
		char c = *it;

		if (c >= 'A' && c <= 'Z')
			c &= ~0x20;

		// AltirraSDL: the net path's separators become the host's.
		resultPath += c == '\\' ? kATPCLinkNativeSep : (wchar_t)c;
	}

	// ensure trailing separator
	if (!resultPath.empty() && resultPath.back() != kATPCLinkNativeSep)
		resultPath += kATPCLinkNativeSep;

	return true;
}

void ATPCLinkDevice::OnReadActivity() {
	if (mpUIRenderer)
		mpUIRenderer->SetPCLinkActivity(false);
}

void ATPCLinkDevice::OnWriteActivity() {
	if (mpUIRenderer)
		mpUIRenderer->SetPCLinkActivity(true);
}
