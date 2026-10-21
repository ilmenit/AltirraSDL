// AltirraSDL native FujiNet/NetSIO transport. GPL-2.0-or-later.
#ifndef f_AT_NETSIO_H
#define f_AT_NETSIO_H
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>

// All control methods and status queries run on the emulation thread.
// The endpoint is a UDP listener: FujiNet initiates the connection.
class IATDeviceNetSIOControl {
public:
	static constexpr uint32 kTypeID = "IATDeviceNetSIOControl"_vdtypeid;
	struct Status {
		bool listening = false;
		bool connected = false;
		uint16 port = 9997;
		uint32 baud = 19200;
		uint64 rxPackets = 0, txPackets = 0;
		uint64 droppedPackets = 0, syncTimeouts = 0;
		VDStringA peer;
		VDStringW lastError;
	};
	virtual Status GetNetSIOStatus() const = 0;
	virtual void Reconnect() = 0;
	virtual void Disconnect() = 0;
	virtual void ResetPeer(bool cold) = 0;
};
#endif
