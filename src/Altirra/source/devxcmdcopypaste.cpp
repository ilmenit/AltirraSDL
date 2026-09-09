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
#include <at/atcore/deviceparent.h>
#include <at/atcore/propertyset.h>
#include "devicemanager.h"
#include "oshelper.h"
#include "uiclipboard.h"

class ATDeviceXCmdCopyBase : public IATDeviceXCmd {
public:
	int AddRef() override { return 2; }
	int Release() override { return 1; }
};

////////////////////////////////////////////////////////////////////////////////

template<bool T_IncludeChildren>
class ATDeviceXCmdCopy final : public ATDeviceXCmdCopyBase {
public:
	bool IsPostRefreshNeeded() const override { return false; }
	bool IsSupported(IATDevice *dev, int busIndex) const override;
	ATDeviceXCmdInfo GetInfo() const override;
	void Invoke(ATDeviceManager& devMgr, IATDevice *dev, int busIndex) override;
};

template<bool T_IncludeChildren>
ATDeviceXCmdCopy<T_IncludeChildren> g_ATDeviceXCmdCopy;

template<bool T_IncludeChildren>
bool ATDeviceXCmdCopy<T_IncludeChildren>::IsSupported(IATDevice *dev, int busIndex) const {
	return busIndex < 0;
}

template<bool T_IncludeChildren>
ATDeviceXCmdInfo ATDeviceXCmdCopy<T_IncludeChildren>::GetInfo() const {
	ATDeviceXCmdInfo info;

	info.mbRequiresElevation = false;
	info.mDisplayName = T_IncludeChildren ? L"Copy (With Children)" : L"Copy";
	return info;
}

template<bool T_IncludeChildren>
void ATDeviceXCmdCopy<T_IncludeChildren>::Invoke(ATDeviceManager& devmgr, IATDevice *dev, int busIndex) {
	VDStringW s;
	devmgr.SerializeDevice(dev, s, true, T_IncludeChildren);

	if (!s.empty()) {
		ATCopyTextToClipboard(nullptr, s.c_str());
	}
}

////////////////////////////////////////////////////////////////////////////////

class ATDeviceXCmdCopyCmdLine final : public ATDeviceXCmdCopyBase {
public:
	bool IsPostRefreshNeeded() const override { return false; }
	bool IsSupported(IATDevice *dev, int busIndex) const override;
	ATDeviceXCmdInfo GetInfo() const override;
	void Invoke(ATDeviceManager& devMgr, IATDevice *dev, int busIndex) override;
};

ATDeviceXCmdCopyCmdLine g_ATDeviceXCmdCopyCmdLine;

bool ATDeviceXCmdCopyCmdLine::IsSupported(IATDevice *dev, int busIndex) const {
	if (!dev)
		return false;

	return true;
}

ATDeviceXCmdInfo ATDeviceXCmdCopyCmdLine::GetInfo() const {
	ATDeviceXCmdInfo info;

	info.mbRequiresElevation = false;
	info.mDisplayName = L"Copy For Command Line";
	return info;
}

void ATDeviceXCmdCopyCmdLine::Invoke(ATDeviceManager& devmgr, IATDevice *dev, int busIndex) {
	if (!dev)
		return;

	VDStringW s;

	ATDeviceInfo devInfo;
	dev->GetDeviceInfo(devInfo);

	if (devInfo.mpDef->mFlags & kATDeviceDefFlag_Internal)
		s = L"/setdevice:";
	else
		s = L"/adddevice:";

	s += VDTextAToW(devInfo.mpDef->mpConfigTag);

	ATPropertySet pset;
	dev->GetSettings(pset);

	if (IATDevice *parent = vdpoly_cast<IATDevice *>(dev->GetParent())) {
		ATDeviceInfo info;
		parent->GetDeviceInfo(info);

		pset.SetString("parent", VDTextAToW(info.mpDef->mpTag).c_str());
	}

	if (!pset.IsEmpty()) {
		s += L',';
		s += pset.ToCommandLineString();
	}

	if (!s.empty())
		ATCopyTextToClipboard(nullptr, s.c_str());
}

////////////////////////////////////////////////////////////////////////////////

class ATDeviceXCmdPaste final : public ATDeviceXCmdCopyBase {
public:
	bool IsSupported(IATDevice *dev, int busIndex) const override;
	ATDeviceXCmdInfo GetInfo() const override;
	void Invoke(ATDeviceManager& devMgr, IATDevice *dev, sint32 busId) override;
} g_ATDeviceXCmdPaste;

bool ATDeviceXCmdPaste::IsSupported(IATDevice *dev, sint32 busId) const {
	if (dev && busId >= 0) {
		if (!vdpoly_cast<IATDeviceParent *>(dev))
			return false;
	}

	VDStringW str;
	if (!ATUIClipGetText(str))
		return false;

	// do some minimal checking that we have a JSON object
	const auto isJSONSpace = [](wchar_t ch) {
		return ch == 0x09 || ch == 0x0a || ch == 0x0d || ch == 0x20;
	};

	auto it1 = str.begin();
	auto it2 = str.end();

	while(it1 != it2 && isJSONSpace(*it1))
		++it1;

	while(it1 != it2 && isJSONSpace(it2[-1]))
		--it2;

	return it1 != it2 && *it1 == L'{' && it2[-1] == L'}';
}

ATDeviceXCmdInfo ATDeviceXCmdPaste::GetInfo() const {
	ATDeviceXCmdInfo info;

	info.mbRequiresElevation = false;
	info.mDisplayName = L"Paste";
	return info;
}

void ATDeviceXCmdPaste::Invoke(ATDeviceManager& devmgr, IATDevice *dev, sint32 busId) {
	IATDeviceParent *parent = nullptr;
	IATDeviceBus *bus = nullptr;

	if (dev) {
		if (busId >= 0) {
			parent = vdpoly_cast<IATDeviceParent *>(dev);

			if (!parent)
				throw MyError("The currently selected device cannot support child devices.");
		} else {
			busId = dev->GetParentBusId();
			parent = dev->GetParent();
		}

		if (parent) {
			bus = parent->GetDeviceBusById(busId);
			if (!bus)
				throw MyError("Cannot find the target device bus.");
		}
	}

	VDStringW str;
	if (!ATUIClipGetText(str))
		throw MyError("Unable to paste devices from clipboard.");

	devmgr.DeserializeDevices(parent, bus, str.c_str());
}

////////////////////////////////////////////////////////////////////////////////

IATDeviceXCmd& ATGetDeviceXCmdCopyWithChildren() {
	return g_ATDeviceXCmdCopy<true>;
}

IATDeviceXCmd& ATGetDeviceXCmdPaste() {
	return g_ATDeviceXCmdPaste;
}

void ATDeviceInitXCmdCopyPaste(ATDeviceManager& dm) {
	dm.RegisterExtendedCommand(g_ATDeviceXCmdCopy<false>);
	dm.RegisterExtendedCommand(g_ATDeviceXCmdCopy<true>);
	dm.RegisterExtendedCommand(g_ATDeviceXCmdCopyCmdLine);
	dm.RegisterExtendedCommand(g_ATDeviceXCmdPaste);
}
