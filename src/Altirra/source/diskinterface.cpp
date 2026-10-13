//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2017 Avery Lee
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
#include <vd2/system/filesys.h>
#include <at/atcore/vfs.h>
#include <at/atio/vdiskimage.h>
#include "diskinterface.h"
#include "simulator.h"
#include "uirender.h"

extern ATSimulator g_sim;

ATDiskInterface::ATDiskInterface() {
}

ATDiskInterface::~ATDiskInterface() {
	mFlushTimer.Stop();
}

void ATDiskInterface::SwapSettings(ATDiskInterface& other) {
	if (&other == this)
		return;

	this->NotifyStateSuspend();
	other.NotifyStateSuspend();

	const auto timingA = this->mbAccurateSectorTiming;
	const auto timingB = other.mbAccurateSectorTiming;

	this->SetAccurateSectorTimingEnabled(timingB);
	other.SetAccurateSectorTimingEnabled(timingA);

	const auto soundsA = this->mbDriveSoundsEnabled;
	const auto soundsB = other.mbDriveSoundsEnabled;

	this->SetDriveSoundsEnabled(soundsB);
	other.SetDriveSoundsEnabled(soundsA);

	const auto counterA = this->mbShowSectorCounter;
	const auto counterB = other.mbShowSectorCounter;

	this->SetShowSectorCounter(counterB);
	other.SetShowSectorCounter(counterA);

	const auto bpA = this->mSectorBreakpoint;
	const auto bpB = other.mSectorBreakpoint;

	this->SetSectorBreakpoint(bpB);
	other.SetSectorBreakpoint(bpA);

	// swap disk images
	this->OnDiskChanging();
	other.OnDiskChanging();

	mpDiskImage.swap(other.mpDiskImage);
	mPath.swap(other.mPath);
	std::swap(mbHasPersistentSource, other.mbHasPersistentSource);

	this->OnDiskChanged(true);
	other.OnDiskChanged(true);

	// swap write modes
	const auto writeModeA = this->mWriteMode;
	const auto writeModeB = other.mWriteMode;

	this->SetWriteMode(writeModeB);
	other.SetWriteMode(writeModeA);

	// update modified states
	this->CheckForModifiedChange();
	other.CheckForModifiedChange();

	this->NotifyStateResume(false);
	other.NotifyStateResume(false);
}

void ATDiskInterface::Init(uint32 index, IATUIRenderer *uirenderer) {
	mIndex = index;
	mpUIRenderer = uirenderer;
	mErrorSourceId = uirenderer->AllocateErrorSourceId();
}

void ATDiskInterface::Shutdown() {
	UnloadDisk();
}

void ATDiskInterface::SetAccurateSectorTimingEnabled(bool enabled) {
	if (mbAccurateSectorTiming == enabled)
		return;

	mbAccurateSectorTiming = enabled;

	for(auto *client : mClients)
		client->OnTimingModeChanged();
}

void ATDiskInterface::SetDriveSoundsEnabled(bool enabled) {
	if (mbDriveSoundsEnabled == enabled)
		return;

	mbDriveSoundsEnabled = enabled;

	for(auto *client : mClients)
		client->OnAudioModeChanged();
}

bool ATDiskInterface::IsDirty() const {
	return mpDiskImage && mpDiskImage->IsDirty();
}

bool ATDiskInterface::IsDiskWritable() const {
	return mpDiskImage && !mpDiskImage->IsDynamic() && (mWriteMode & kATMediaWriteMode_AllowWrite);
}

bool ATDiskInterface::IsFormatAllowed() const {
	return mpDiskImage && !mpDiskImage->IsDynamic()
		&& (mWriteMode & kATMediaWriteMode_AllowWrite)
		&& (mWriteMode & kATMediaWriteMode_AllowFormat);
}

bool ATDiskInterface::TryEnableWrite() {
	if (!mpDiskImage)
		return false;

	if (mpDiskImage->IsDynamic())
		return false;

	if (!(mWriteMode & kATMediaWriteMode_AllowWrite)) {
		// remount VRW
		SetWriteMode(kATMediaWriteMode_VRW);
	}

	return true;
}

void ATDiskInterface::SetWriteMode(ATMediaWriteMode mode) {
	if (mWriteMode != mode) {
		mWriteMode = mode;

		if (mode & kATMediaWriteMode_AutoFlush) {
			// If auto-flush is now enabled and no auto-flush is pending, queue one
			// now.
			if (!mFlushTimeStart && CanFlush(false))
				OnDiskModified();
		} else {
			// If auto-flush is now disabled, stop the timer and kill any flush error.
			if (mFlushTimeStart) {
				mFlushTimeStart = 0;
				mFlushTimer.Stop();
			}

			mpUIRenderer->SetDiskErrorState(mIndex, false);
		}

		for(auto *client : mClients)
			client->OnWriteModeChanged();

		NotifyStateChange();
	}
}

void ATDiskInterface::Flush() {
	Flush(false, false);
}

bool ATDiskInterface::CanRevert() const {
	if (!IsDirty())
		return false;

	if (!mbHasPersistentSource)
		return false;

	if (mWriteMode & kATMediaWriteMode_AutoFlush)
		return false;

	return true;
}

bool ATDiskInterface::RevertDisk() {
	if (!IsDirty())
		return true;

	if (!CanRevert())
		return false;

	ATMediaWriteMode writeMode = mWriteMode;
	LoadDisk(mPath.c_str());
	SetWriteMode(writeMode);
	return true;
}

void ATDiskInterface::MountFolder(const IATVirtualDiskImage& vdiskImage) {
	MountFolder(
		vdiskImage.GetBasePath(),
		vdiskImage.GetFileSystem() == ATVirtualDiskFileSystem::SDFS,
		vdiskImage.GetSectorCount(),
		vdiskImage.GetSectorSize()
	);
}

void ATDiskInterface::MountFolder(const wchar_t *path, bool sdfs, uint32 sectorCount, uint32 sectorSize) {
	UnloadDisk();

	try {
		if (sectorSize > 128)
			sectorSize = 256;
		else
			sectorSize = 128;

		if (sdfs)
			ATMountDiskImageVirtualFolderSDFS(path, sectorSize, (uint64)this, ~mpDiskImage);
		else {
			sectorCount = std::clamp<uint32>(sectorCount, 720, 65535);

			const uint32 sectorsPerTrack = sectorCount == 1040 ? 26 : 18;
			const uint32 trackCount = std::min<uint32>((sectorCount + sectorsPerTrack - 1) / sectorsPerTrack, 65535 / sectorsPerTrack);

			ATMountDiskImageVirtualFolder(path, trackCount, sectorsPerTrack, sectorSize > 128 ? 256 : 128, ~mpDiskImage);
		}

		mPath = ATMakeVirtualDiskImagePath(
			VDStringSpanW(path),
			sectorCount,
			sectorSize,
			sdfs ? ATVirtualDiskFileSystem::SDFS : ATVirtualDiskFileSystem::DOS
		);
	} catch(const MyError&) {
		UnloadDisk();
		throw;
	}

	OnDiskChanged(true);
	SetWriteMode(kATMediaWriteMode_RO);
	mbHasPersistentSource = true;
}

void ATDiskInterface::LoadDisk(const wchar_t *s) {
	// copy the path in case it's an alias for an internal var (mPath)
	VDStringW path(s);
	s = path.c_str();

	const ATVFSParsedPath& parsedPath = ATParseVFSPath(s);
	vdrefptr<IATVirtualDiskImage> vdiskImage = ATTryLoadVirtualDiskImageFromWildFilePath(parsedPath);

	if (!vdiskImage && parsedPath.mProtocol == kATVFSProtocol_Atvdisk)
		vdiskImage = ATLoadVirtualDiskImage(parsedPath);

	if (vdiskImage)
		return MountFolder(*vdiskImage);

	vdrefptr<ATVFSFileView> view = ATVFSOpenFileView(s, parsedPath, false, false);

	vdrefptr<IATDiskImage> image;
	ATLoadDiskImage(s, view->GetFileName(), view->GetStream(), ~image);

	UnloadDisk();

	LoadDisk(s, view->GetFileName(), image);
}

void ATDiskInterface::LoadDisk(const wchar_t *path, const wchar_t *imageName, IATDiskImage *image) {
	UnloadDisk();

	mbHasPersistentSource = false;

	mpDiskImage = image;

	if (path && *path) {
		mPath = path;
		mbHasPersistentSource = true;
	} else if (imageName)
		mPath = imageName;
	else
		mPath.clear();

	OnDiskChanged(true);
	SetWriteMode(kATMediaWriteMode_RO);
}

void ATDiskInterface::SaveDisk() {
	Flush(true, true);
}

void ATDiskInterface::SaveDiskAs(const wchar_t *s, ATDiskImageFormat format) {
	if (!mpDiskImage)
		throw MyError("No disk image is currently mounted.");

	if (mpDiskImage->IsDynamic())
		throw MyError("The current disk image is dynamic and cannot be saved.");

	mpDiskImage->Save(s, format);

	mPath = s;
	mbHasPersistentSource = true;
	SetFlushError(false);

	CheckForModifiedChange();
}

void ATDiskInterface::CreateDisk(uint32 sectorCount, uint32 bootSectorCount, uint32 sectorSize) {
	UnloadDisk();
	FormatDisk(sectorCount, bootSectorCount, sectorSize);
	mPath = L"(New disk)";
	SetWriteMode(kATMediaWriteMode_VRW);
}

void ATDiskInterface::CreateDisk(const ATDiskGeometryInfo& geometry) {
	UnloadDisk();
	FormatDisk(geometry);
	mPath = L"(New disk)";
	SetWriteMode(kATMediaWriteMode_VRW);
}

void ATDiskInterface::FormatDisk(uint32 sectorCount, uint32 bootSectorCount, uint32 sectorSize) {
	ATDiskImageFormat imageFormat = kATDiskImageFormat_None;

	if (mpDiskImage)
		imageFormat = mpDiskImage->GetImageFormat();

	OnDiskChanging();

	ATCreateDiskImage(sectorCount, bootSectorCount, sectorSize, ~mpDiskImage);

	if (mbHasPersistentSource)
		mpDiskImage->SetPath(mPath.c_str(), imageFormat);

	OnDiskChanged(false);
}

void ATDiskInterface::FormatDisk(const ATDiskGeometryInfo& geometry) {
	ATDiskImageFormat imageFormat = kATDiskImageFormat_None;

	if (mpDiskImage)
		imageFormat = mpDiskImage->GetImageFormat();

	OnDiskChanging();

	ATCreateDiskImage(geometry, ~mpDiskImage);

	if (mbHasPersistentSource)
		mpDiskImage->SetPath(mPath.c_str(), imageFormat);

	OnDiskChanged(false);
}

void ATDiskInterface::UnloadDisk() {
	if (mpDiskImage)
		OnDiskChanging();

	mpDiskImage.clear();
	mPath.clear();
	mbHasPersistentSource = false;

	SetFlushError(false);

	OnDiskChanged(true);
}

void ATDiskInterface::OnDiskModified() {
	CheckForModifiedChange();

	if (CanFlush(false)) {
		if (!mFlushTimeStart)
			mFlushTimer.SetPeriodicFn([this] { OnFlushTimerFire(); }, 500);

		// mark current time; oddify it so it can't be 0 (which means not started).
		mFlushTimeStart = VDGetCurrentTick() | 1;
	}
}

void ATDiskInterface::OnDiskChanging() {
	NotifyStateSuspend();

	for(auto *client : mClients)
		client->OnDiskChanging();

	NotifyStateResume(true);
}

void ATDiskInterface::OnDiskChanged(bool mediaRemoved) {
	NotifyStateSuspend();

	OnDiskModified();

	for(auto *client : mClients)
		client->OnDiskChanged(mediaRemoved);

	NotifyStateResume(true);
}

VDStringW ATDiskInterface::GetMountedImageLabel() const {
	if (!mpDiskImage)
		return VDStringW(L"(No disk)");

	VDStringW label = VDFileSplitPathRight(mPath);

	if (mPath.empty())
		label = L"New disk";

	if (mpDiskImage->IsDirty())
		label += L" (modified)";

	return label;
}

uint32 ATDiskInterface::GetSectorSize(uint16 sector) const {
	if (!mpDiskImage)
		return 0;

	return mpDiskImage->GetSectorSize(sector);
}

uint32 ATDiskInterface::GetSectorPhantomCount(uint16 sector) const {
	if (!mpDiskImage)
		return 0;

	if (!sector || sector > mpDiskImage->GetVirtualSectorCount())
		return 0;

	ATDiskVirtualSectorInfo vsi;
	mpDiskImage->GetVirtualSectorInfo(sector - 1, vsi);

	return vsi.mNumPhysSectors;
}

bool ATDiskInterface::GetSectorInfo(uint16 sector, int phantomIdx, ATDiskPhysicalSectorInfo& info) const {
	if (!mpDiskImage)
		return false;

	if (!sector || sector > mpDiskImage->GetVirtualSectorCount())
		return false;

	ATDiskVirtualSectorInfo vsi;
	mpDiskImage->GetVirtualSectorInfo(sector - 1, vsi);

	if (phantomIdx < 0 || (uint32)phantomIdx >= vsi.mNumPhysSectors)
		return false;

	uint32 psec = vsi.mFirstPhysSector;
	for(int i = 0; i < phantomIdx; ++i)
		psec = mpDiskImage->GetNextPhysicalSector(psec);

	mpDiskImage->GetPhysicalSectorInfo(psec, info);
	return true;
}

void ATDiskInterface::AddStateChangeHandler(const vdfunction<void()> *fn) {
	mStateChangeHandlers.Add(fn);
}

void ATDiskInterface::RemoveStateChangeHandler(const vdfunction<void()> *fn) {
	mStateChangeHandlers.Remove(fn);
}

bool ATDiskInterface::IsImageSupported(const IATDiskImage& image) const {
	for(auto *client : mClients) {
		if (!client->IsImageSupported(image))
			return false;
	}

	return true;
}

size_t ATDiskInterface::GetClientCount() const {
	return mClients.size();
}

void ATDiskInterface::AddClient(IATDiskInterfaceClient *client) {
	mClients.push_back(client);
}

void ATDiskInterface::RemoveClient(IATDiskInterfaceClient *client) {
	auto it = std::find(mClients.begin(), mClients.end(), client);
	
	if (it != mClients.end())
		mClients.erase(it);
}

void ATDiskInterface::CheckSectorBreakpoint(uint32 sector) {
	if (mSectorBreakpoint >= 0 && sector == (uint32)mSectorBreakpoint)
		g_sim.PostInterruptingEvent(kATSimEvent_DiskSectorBreakpoint);
}

void ATDiskInterface::SetShowMotorActive(bool active) {
	mpUIRenderer->SetDiskMotorActivity(mIndex, active);
}

void ATDiskInterface::SetShowActivity(bool active, uint32 sector, uint32 holdTime) {
	if (active) {
		uint32 value = sector;

		if (!mbShowSectorCounter)
			value = mIndex + 1;

		mpUIRenderer->SetStatusCounter(mIndex, value);

		mpUIRenderer->SetStatusFlags(1 << mIndex);
	} else
		mpUIRenderer->ResetStatusFlags(1 << mIndex, holdTime);
}

void ATDiskInterface::SetShowLEDReadout(sint32 ledDisplay) {
	mpUIRenderer->SetDiskLEDState(mIndex, ledDisplay);
}

void ATDiskInterface::Flush(bool ignoreAutoFlush, bool rethrowErrors) {
	// stop the auto-flush timer if it is running
	if (mFlushTimeStart) {
		mFlushTimeStart = 0;
		mFlushTimer.Stop();
	}

	if (!CanFlush(ignoreAutoFlush))
		return;

	try {
		mpDiskImage->Flush();

		SetFlushError(false);
	} catch(const MyError& e) {
		// remount VRW
		SetWriteMode((ATMediaWriteMode)(mWriteMode & ~kATMediaWriteMode_AutoFlush));

		// set flush error indicator
		SetFlushError(true);

		if (rethrowErrors)
			throw;
		else if (mpUIRenderer) {
			VDStringW msg(e.wc_str());

			for(wchar_t& c : msg) {
				if (c == L'\n')
					c = ' ';
			}
			
			VDStringW s;
			s.sprintf(L"D%u: remounted virtual read/write due to write error: %ls", mIndex + 1, msg.c_str());
			mpUIRenderer->ReportError(mErrorSourceId, s.c_str());
		}
	}

	CheckForModifiedChange();
}

bool ATDiskInterface::CanFlush(bool ignoreAutoFlush) const {
	if (!mpDiskImage)
		return false;

	if (!ignoreAutoFlush) {
		if (!(mWriteMode & kATMediaWriteMode_AutoFlush))
			return false;
	}

	return mpDiskImage->IsDirty();
}

void ATDiskInterface::OnFlushTimerFire() {
	if (mFlushTimeStart) {
		constexpr uint32 kFlushDelay = 1750;		// 2 seconds - half timer interval

		if ((uint32)(mFlushTimeStart - VDGetCurrentTick() + kFlushDelay) >= kFlushDelay*2)
			return;

		mFlushTimeStart = 0;
	}

	mFlushTimer.Stop();

	Flush(false, false);
}

void ATDiskInterface::SetFlushError(bool error) {
	mpUIRenderer->SetDiskErrorState(mIndex, error);
}

void ATDiskInterface::CheckForModifiedChange() {
	const bool modified = mpDiskImage && mpDiskImage->IsDirty();

	if (mbModified != modified) {
		mbModified = modified;

		NotifyStateChange();
	}
}

void ATDiskInterface::NotifyStateSuspend() {
	mStateChangeSuspendCount += 2;
}

void ATDiskInterface::NotifyStateResume(bool notify) {
	if (mStateChangeSuspendCount < 4) {
		if (mStateChangeSuspendCount & 1)
			notify = true;

		mStateChangeSuspendCount = 0;

		if (notify)
			NotifyStateChange();
	} else {
		mStateChangeSuspendCount -= 2;

		if (notify)
			mStateChangeSuspendCount |= 1;
	}
}

void ATDiskInterface::NotifyStateChange() {
	if (mStateChangeSuspendCount >= 2) {
		mStateChangeSuspendCount |= 1;
	} else {
		mStateChangeHandlers.Notify([](auto fn) -> bool { (*fn)(); return false; });
	}
}
