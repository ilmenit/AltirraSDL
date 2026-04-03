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

#include <stdafx.h>
#include <vd2/system/binary.h>
#include <vd2/system/error.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include <at/atnetwork/ethernetframe.h>
#include <at/atnetwork/socket.h>
#include <at/atnetworksockets/internal/vxlantunnel_sdl3.h>
#include <at/atnetworksockets/nativesockets.h>

ATNetSockVxlanTunnel::ATNetSockVxlanTunnel() {
}

ATNetSockVxlanTunnel::~ATNetSockVxlanTunnel() {
	Shutdown();
}

bool ATNetSockVxlanTunnel::Init(uint32 tunnelAddr, uint16 tunnelSrcPort, uint16 tunnelTgtPort, IATEthernetSegment *ethSeg, uint32 ethClockIndex, IATAsyncDispatcher *dispatcher) {
	mTunnelSrcPort = tunnelSrcPort;
	mTunnelAddress = ATSocketAddress::CreateIPv4(VDFromBE32(tunnelAddr), tunnelTgtPort);

	mpEthSegment = ethSeg;
	mEthSource = mpEthSegment->AddEndpoint(this);

	mPacketBuffer.resize(4096);

	mpTunnelSocket = ATNetBind(ATSocketAddress::CreateIPv4(mTunnelSrcPort), true);
	mpTunnelSocket->SetOnEvent(dispatcher,
		[this](const ATSocketStatus& status) {
			if (status.mbCanRead) {
				OnReadPacket();
			}
		},
		true
	);

	return true;
}

void ATNetSockVxlanTunnel::Shutdown() {
	if (mEthSource) {
		mpEthSegment->RemoveEndpoint(mEthSource);
		mEthSource = 0;
	}

	if (mpTunnelSocket) {
		mpTunnelSocket->CloseSocket(true);
		mpTunnelSocket = nullptr;
	}
}

void ATNetSockVxlanTunnel::ReceiveFrame(const ATEthernetPacket& packet, ATEthernetFrameDecodedType decType, const void *decInfo) {
	uint32 len = 20 + packet.mLength;

	if (mPacketBuffer.size() < len)
		mPacketBuffer.resize(len);

	// set VXLAN header to VLAN absent
	memset(mPacketBuffer.data(), 0, 8);
	mPacketBuffer[0] = 0x08;

	// init Ethernet header
	memcpy(&mPacketBuffer[8], &packet.mDstAddr, 6);
	memcpy(&mPacketBuffer[14], &packet.mSrcAddr, 6);
	memcpy(&mPacketBuffer[20], packet.mpData, packet.mLength);

	// send VXLAN packet
	mpTunnelSocket->SendTo(mTunnelAddress, mPacketBuffer.data(), len);
}

void ATNetSockVxlanTunnel::OnReadPacket() {
	for(;;) {
		ATSocketAddress addr;
		sint32 len = mpTunnelSocket->RecvFrom(addr, mPacketBuffer.data(), mPacketBuffer.size());

		if (len < 0)
			break;

		// Okay, next check that we have a valid VXLAN header and ethernet packet after it.
		if (len < 8 + 12 + 2)
			continue;

		const uint8 *vxlanhdr = mPacketBuffer.data();

		// must be VLAN 0
		if ((vxlanhdr[0] & 0x08) && (VDReadUnalignedBEU32(&vxlanhdr[4]) & 0xFFFFFF00))
			continue;

		const uint8 *payload = vxlanhdr + 8;
		const uint32 payloadLen = len - 8;

		if (payloadLen < 14)
			continue;

		if (payloadLen > 1502)
			continue;

		// forward packet to ethernet segment
		ATEthernetPacket packet = {};
		packet.mClockIndex = mEthClockIndex;
		packet.mTimestamp = 100;
		memcpy(&packet.mSrcAddr, payload + 6, 6);
		memcpy(&packet.mDstAddr, payload, 6);
		packet.mpData = payload + 12;
		packet.mLength = payloadLen - 12;
		mpEthSegment->TransmitFrame(mEthSource, packet);
	}
}

///////////////////////////////////////////////////////////////////////////

void ATCreateNetSockVxlanTunnel(uint32 tunnelAddr, uint16 tunnelSrcPort, uint16 tunnelTgtPort, IATEthernetSegment *ethSeg, uint32 ethClockIndex, IATAsyncDispatcher *dispatcher, IATNetSockVxlanTunnel **pp) {
	ATNetSockVxlanTunnel *p = new ATNetSockVxlanTunnel;

	if (!p->Init(tunnelAddr, tunnelSrcPort, tunnelTgtPort, ethSeg, ethClockIndex, dispatcher)) {
		delete p;
		throw MyMemoryError();
	}

	p->AddRef();
	*pp = p;
}
