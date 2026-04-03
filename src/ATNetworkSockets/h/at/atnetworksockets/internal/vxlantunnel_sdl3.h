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

#ifndef f_AT_ATNETWORKSOCKETS_INTERNAL_VXLANTUNNEL_SDL3_H
#define f_AT_ATNETWORKSOCKETS_INTERNAL_VXLANTUNNEL_SDL3_H

#include <vd2/system/vdstl.h>
#include <at/atnetwork/ethernet.h>
#include <at/atnetworksockets/vxlantunnel.h>

class ATNetSockVxlanTunnel final : public vdrefcounted<IATNetSockVxlanTunnel>, public IATEthernetEndpoint {
public:
	ATNetSockVxlanTunnel();
	~ATNetSockVxlanTunnel();

	bool Init(uint32 tunnelAddr, uint16 tunnelSrcPort, uint16 tunnelTgtPort, IATEthernetSegment *ethSeg, uint32 ethClockIndex, IATAsyncDispatcher *dispatcher);
	void Shutdown();

public:
	void ReceiveFrame(const ATEthernetPacket& packet, ATEthernetFrameDecodedType decType, const void *decInfo) override;

private:
	void OnReadPacket();

	vdrefptr<IATDatagramSocket> mpTunnelSocket;
	uint16 mTunnelSrcPort = 0;
	ATSocketAddress mTunnelAddress;

	IATEthernetSegment *mpEthSegment = nullptr;
	uint32 mEthSource = 0;
	uint32 mEthClockIndex = 0;

	vdblock<uint8> mPacketBuffer;
};

#endif
