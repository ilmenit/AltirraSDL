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

#ifndef f_AT_PCLINK_H
#define f_AT_PCLINK_H

#include <vd2/system/date.h>
#include <vd2/system/file.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/devicesio.h>
#include <at/atcore/deviceindicators.h>

struct ATPCLinkFileName {
	uint8	mName[11];

	bool operator==(const ATPCLinkFileName& x) const {
		return memcmp(mName, x.mName, 11) == 0;
	}

	bool ParseFromNet(const uint8 fn[11]);
	bool ParseFromNative(const wchar_t *fn);
	void AppendNative(VDStringA& s) const;
	void AppendNative(VDStringW& s) const;

	bool IsWild() const;
	bool IsReservedDeviceName() const;
	bool WildMatch(const ATPCLinkFileName& fn) const;
	void WildMerge(const ATPCLinkFileName& fn);
};

struct ATPCLinkDirEnt {
	enum : uint8 {
		kFlag_OpenForWrite	= 0x80,
		kFlag_Directory		= 0x20,
		kFlag_Deleted		= 0x10,
		kFlag_InUse			= 0x08,
		kFlag_Archive		= 0x04,
		kFlag_Hidden		= 0x02,
		kFlag_Locked		= 0x01
	};

	enum : uint8 {
		kAttrMask_NoSubDir		= 0x80,
		kAttrMask_NoArchived	= 0x40,
		kAttrMask_NoHidden		= 0x20,
		kAttrMask_NoLocked		= 0x10,
		kAttrMask_OnlySubDir	= 0x08,
		kAttrMask_OnlyArchived	= 0x04,
		kAttrMask_OnlyHidden	= 0x02,
		kAttrMask_OnlyLocked	= 0x01,
	};

	uint8	mFlags;
	uint8	mSectorMapLo;
	uint8	mSectorMapHi;
	uint8	mLengthLo;
	uint8	mLengthMid;
	uint8	mLengthHi;
	uint8	mName[11];
	uint8	mDay;
	uint8	mMonth;
	uint8	mYear;
	uint8	mHour;
	uint8	mMin;
	uint8	mSec;

	void SetFlagsFromAttributes(uint32 attr);
	bool TestAttrFilter(uint8 attrFilter) const;
	void SetDate(const VDDate& date);

	static VDDate DecodeDate(const uint8 tsdata[6]);
};

class ATPCLinkFileHandle {
	ATPCLinkFileHandle(const ATPCLinkFileHandle&) = delete;
	ATPCLinkFileHandle& operator=(const ATPCLinkFileHandle&) = delete;

public:
	ATPCLinkFileHandle() = default;
	~ATPCLinkFileHandle();

	bool IsOpen() const;
	bool IsDir() const;
	bool IsReadable() const;
	bool IsWritable() const;
	bool WasCreated() const { return mbWasCreated; }

	uint32 GetLength() const;
	uint32 GetPosition() const;
	const ATPCLinkDirEnt& GetDirEnt() const;
	void SetDirEnt(const ATPCLinkDirEnt& dirEnt);

	void AddDirEnt(const ATPCLinkDirEnt& dirEnt);
	uint8 OpenFile(const wchar_t *nativePath, uint32 openFlags, bool allowRead, bool allowWrite, bool append);
	void OpenAsDirectory(const ATPCLinkFileName& dirName, const ATPCLinkFileName& fnextFilter, uint8 attrFilter, bool isRoot);
	void Close();
	uint8 Seek(uint32 pos);
	uint8 Read(void *dst, uint32 len, uint32& actual);
	uint8 Write(const void *dst, uint32 len);

	void SetTimestamp(const uint8 tsdata[6]);
 
	bool GetNextDirEnt(ATPCLinkDirEnt& dirEnt);

protected:
	bool	mbOpen = false;
	bool	mbAllowRead = false;
	bool	mbAllowWrite = false;
	bool	mbIsDirectory = false;
	bool	mbWasCreated = false;
	uint32	mPos = 0;
	uint32	mLength = 0;

	vdfastvector<ATPCLinkDirEnt> mDirEnts;

	ATPCLinkDirEnt	mDirEnt = {};
	ATPCLinkFileName mFnextPattern = {};
	uint8	mFnextAttrFilter = 0;

	VDFile	mFile;
	VDDate	mPendingDate {};
};

class ATPCLinkDevice final
			: public ATDevice
			, public IATDeviceSIO
			, public IATDeviceIndicators
			, public IATDeviceDiagnostics
			, public IATDeviceButtons
{
	friend class ATPCLinkDeviceTest;

	ATPCLinkDevice(const ATPCLinkDevice&) = delete;
	ATPCLinkDevice& operator=(const ATPCLinkDevice&) = delete;
public:
	ATPCLinkDevice();
	~ATPCLinkDevice();

	void *AsInterface(uint32 id) override;

	bool IsReadOnly() { return mbReadOnly; }
	void SetReadOnly(bool readOnly);

	const wchar_t *GetBasePath() { return mBasePathNative.c_str(); }
	void SetBasePath(const wchar_t *basePath);

public:
	void GetDeviceInfo(ATDeviceInfo& info) override;
	void GetSettings(ATPropertySet& settings) override;
	bool SetSettings(const ATPropertySet& settings) override;
	void Shutdown() override;
	void ColdReset() override;

public:
	void InitIndicators(IATDeviceIndicatorManager *uir) override;

public:
	void InitSIO(IATDeviceSIOManager *mgr) override;
	CmdResponse OnSerialBeginCommand(const ATDeviceSIOCommand& cmd) override;
	void OnSerialAbortCommand() override;
	void OnSerialReceiveComplete(uint32 id, const void *data, uint32 len, bool checksumOK) override;
	void OnSerialFence(uint32 id) override;
	CmdResponse OnSerialAccelCommand(const ATDeviceSIORequest& request) override;

public:
	void DumpStatus(ATConsoleOutput& output) override;

public:	// IATDeviceButtons
	uint32 GetSupportedButtons() const override;
	bool IsButtonDepressed(ATDeviceButton idx) const override;
	void ActivateButton(ATDeviceButton idx, bool state) override;

protected:
	enum Command {
		kCommandNone,
		kCommandGetHiSpeedIndex,
		kCommandStatus,
		kCommandPut,
		kCommandRead
	};

	void CloseAllFiles();

	void AbortCommand();
	void BeginCommand(Command cmd);
	void AdvanceCommand();
	void FinishCommand();

	bool OnPut();
	bool OnRead();

	bool CheckValidFileHandle(bool setError);
	bool IsDirEntIncluded(const ATPCLinkDirEnt& dirEnt) const;
	bool ResolvePath(VDStringA& resultPath);
	static uint8 ResolvePathStatic(const char *path, const char *curDir, VDStringA& resultPath);
	bool ResolveNativePath(VDStringW& resultPath);
	bool ResolveNativePath(VDStringW& resultPath, const VDStringA& netPath);
	void OnReadActivity();
	void OnWriteActivity();

	IATDeviceSIOManager *mpSIOMgr = nullptr;
	vdrefptr<IATDeviceSIOInterface> mpSIOInterface;
	IATDeviceIndicatorManager *mpUIRenderer = nullptr;

	VDStringW	mBasePathNative;
	bool	mbReadOnly = false;
	bool	mbSetTimestamps = false;

	vdfunction<void(const void *, uint32)> mpReceiveFn;
	vdfunction<void()> mpFenceFn;

	uint8	mStatusFlags = 0;
	uint8	mStatusError = 0;
	uint8	mStatusLengthLo = 0;
	uint8	mStatusLengthHi = 0;

	Command	mCommand = kCommandNone;
	uint32	mCommandPhase = 0;
	uint8	mCommandAux1 = 0;
	uint8	mCommandAux2 = 0;

	VDStringA	mCurDir;

	struct ParameterBuffer {
		uint8	mFunction;	// function number
		uint8	mHandle;	// file handle
		uint8	mF[6];
		uint8	mMode;		// file open mode
		uint8	mAttr1;
		uint8	mAttr2;
		uint8	mName1[12];
		uint8	mName2[12];
		uint8	mPath[65];
	};

	ParameterBuffer mParBuf;

	ATPCLinkFileHandle mFileHandles[15];

	uint8	mTransferBuffer[65536];
};

#endif
