// Altirra SDL3 netplay - packet encode/decode
//
// Little-endian fixed-layout Serialize/Deserialize for every packet
// type in packets.h.  Serialize writes into a caller-owned buffer and
// returns the number of bytes written (0 on buffer-too-small).
// Deserialize reads from a caller-owned buffer and returns true on
// success.  No allocations in the hot path.

#pragma once

#include "packets.h"

#include <cstddef>
#include <cstdint>

namespace ATNetplay {

enum class DecodeResult {
	Ok,
	TooShort,
	BadMagic,
	BadSize,
};

// Read the first 4 bytes as a little-endian uint32 magic.  Used to
// dispatch on the packet type after a successful recvfrom() without
// knowing the full packet size yet.
bool PeekMagic(const uint8_t* buf, size_t len, uint32_t& outMagic);

// --- NetHello --------------------------------------------------------------
size_t EncodeHello(const NetHello& h, uint8_t* buf, size_t bufSize);
DecodeResult DecodeHello(const uint8_t* buf, size_t len, NetHello& out);

// --- NetWelcome ------------------------------------------------------------
size_t EncodeWelcome(const NetWelcome& w, uint8_t* buf, size_t bufSize);
DecodeResult DecodeWelcome(const uint8_t* buf, size_t len, NetWelcome& out);

// --- NetReject -------------------------------------------------------------
size_t EncodeReject(const NetReject& r, uint8_t* buf, size_t bufSize);
DecodeResult DecodeReject(const uint8_t* buf, size_t len, NetReject& out);

// --- NetInputPacket (hot path) ---------------------------------------------
size_t EncodeInputPacket(const NetInputPacket& p, uint8_t* buf, size_t bufSize);
DecodeResult DecodeInputPacket(const uint8_t* buf, size_t len, NetInputPacket& out);

// --- NetBye ----------------------------------------------------------------
size_t EncodeBye(const NetBye& b, uint8_t* buf, size_t bufSize);
DecodeResult DecodeBye(const uint8_t* buf, size_t len, NetBye& out);

// --- NetSnapChunk ----------------------------------------------------------
// On encode, c.payload must point to c.payloadLen bytes of data.
// On decode, out.payload points into the caller's input buffer — the
// caller must copy it before the buffer is reused.
size_t EncodeSnapChunk(const NetSnapChunk& c, uint8_t* buf, size_t bufSize);
DecodeResult DecodeSnapChunk(const uint8_t* buf, size_t len, NetSnapChunk& out);

// --- NetSnapAck ------------------------------------------------------------
size_t EncodeSnapAck(const NetSnapAck& a, uint8_t* buf, size_t bufSize);
DecodeResult DecodeSnapAck(const uint8_t* buf, size_t len, NetSnapAck& out);

// --- handle/cart string helpers (null-terminated UTF-8) --------------------
// Truncates or zero-pads to fit the fixed-size wire field.
void HandleFromString(const char* s, uint8_t out[kHandleLen]);
void CartFromString(const char* s, uint8_t out[kCartLen]);

// Reads a fixed-size field as a null-terminated string (reads until the
// first NUL byte or end of field).  Returns a pointer into the caller's
// buffer; does not copy.  Length in outLen if non-null.
const char* HandleToCStr(const uint8_t in[kHandleLen], size_t* outLen = nullptr);
const char* CartToCStr(const uint8_t in[kCartLen], size_t* outLen = nullptr);

} // namespace ATNetplay
