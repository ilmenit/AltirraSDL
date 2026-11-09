//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2026 Avery Lee
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
#include <at/atcore/device.h>
#include <at/atcore/deviceparent.h>
#include <at/atcore/propertyset.h>
#include "devicemanager.h"
#include "oshelper.h"
#include "uiclipboard.h"

template<ATDeviceButton T_Button>
class ATDeviceXCmdClose final : public IATDeviceXCmd {
public:
	int AddRef() override { return 2; }
	int Release() override { return 1; }

	bool IsPostRefreshNeeded() const override { return true; }
	bool IsSupported(IATDevice *dev, int busIndex) const override;
	ATDeviceXCmdInfo GetInfo() const override;
	void Invoke(ATDeviceManager& devMgr, IATDevice *dev, int busIndex) override;
};

template<ATDeviceButton T_Button>
bool ATDeviceXCmdClose<T_Button>::IsSupported(IATDevice *dev, int busIndex) const {
	IATDeviceButtons *devbtn = vdpoly_cast<IATDeviceButtons *>(dev);

	return devbtn && (devbtn->GetSupportedButtons() & (UINT32_C(1) << T_Button)) != 0;
}

template<ATDeviceButton T_Button>
ATDeviceXCmdInfo ATDeviceXCmdClose<T_Button>::GetInfo() const {
	ATDeviceXCmdInfo info;

	info.mbRequiresElevation = false;

	if constexpr (T_Button == kATDeviceButton_CloseFiles) {
		info.mDisplayName = L"Close Open Files";
	} else if constexpr (T_Button == kATDeviceButton_CloseNetworkConnections) {
		info.mDisplayName = L"Close Network Connections";
	} else {
		static_assert(T_Button == kATDeviceButton_CloseNetworkConnections);
	}

	return info;
}

template<ATDeviceButton T_Button>
void ATDeviceXCmdClose<T_Button>::Invoke(ATDeviceManager& devmgr, IATDevice *dev, int busIndex) {
	IATDeviceButtons *devbtn = vdpoly_cast<IATDeviceButtons *>(dev);

	if (devbtn) {
		devbtn->ActivateButton(T_Button, true);
		devbtn->ActivateButton(T_Button, false);
	}
}

////////////////////////////////////////////////////////////////////////////////

template<ATDeviceButton T_Button>
ATDeviceXCmdClose<T_Button> g_ATDeviceXCmdClose {};

void ATDeviceInitXCmdClose(ATDeviceManager& dm) {
	dm.RegisterExtendedCommand(g_ATDeviceXCmdClose<kATDeviceButton_CloseFiles>);
	dm.RegisterExtendedCommand(g_ATDeviceXCmdClose<kATDeviceButton_CloseNetworkConnections>);
}
