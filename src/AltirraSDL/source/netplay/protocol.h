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

// --- NetResyncStart / NetResyncDone ---------------------------------------
size_t EncodeResyncStart(const NetResyncStart& s, uint8_t* buf, size_t bufSize);
DecodeResult DecodeResyncStart(const uint8_t* buf, size_t len, NetResyncStart& out);

size_t EncodeResyncDone(const NetResyncDone& d, uint8_t* buf, size_t bufSize);
DecodeResult DecodeResyncDone(const uint8_t* buf, size_t len, NetResyncDone& out);

// --- NetEmote --------------------------------------------------------------
size_t EncodeEmote(const NetEmote& e, uint8_t* buf, size_t bufSize);
DecodeResult DecodeEmote(const uint8_t* buf, size_t len, NetEmote& out);

// --- NetSimHashDiag (per-subsystem hash breakdown exchange) ---------------
size_t EncodeSimHashDiag(const NetSimHashDiag& d, uint8_t* buf, size_t bufSize);
DecodeResult DecodeSimHashDiag(const uint8_t* buf, size_t len, NetSimHashDiag& out);

// --- NetPunch (v4 two-sided punch) -----------------------------------------
size_t EncodePunch(const NetPunch& p, uint8_t* buf, size_t bufSize);
DecodeResult DecodePunch(const uint8_t* buf, size_t len, NetPunch& out);

// --- NetSnapSkip (joiner-side cache hit) -----------------------------------
size_t EncodeSnapSkip(const NetSnapSkip& s, uint8_t* buf, size_t bufSize);
DecodeResult DecodeSnapSkip(const uint8_t* buf, size_t len, NetSnapSkip& out);

// --- NetRelayRegister / NetRelayDataHeader (v4 UDP relay) ------------------
size_t EncodeRelayRegister(const NetRelayRegister& r,
                           uint8_t* buf, size_t bufSize);
DecodeResult DecodeRelayRegister(const uint8_t* buf, size_t len,
                                 NetRelayRegister& out);

// Encode a relay frame: 24-byte header + inner payload.  Inner is
// caller-owned; the encoder copies `innerLen` bytes into `buf` after
// the header.  Returns total bytes written, or 0 if bufSize is too
// small.  Header fields (sessionId / role) come from `h`.
size_t EncodeRelayFrame(const NetRelayDataHeader& h,
                        const uint8_t* inner, size_t innerLen,
                        uint8_t* buf, size_t bufSize);

// Decode a relay frame.  On success `outHeader` holds the header and
// (outInner, outInnerLen) points into `buf` at the inner payload.
// Returns TooShort / BadMagic if not a valid ASDF frame.  The caller
// must copy inner bytes before reusing `buf`.
DecodeResult DecodeRelayFrame(const uint8_t* buf, size_t len,
                              NetRelayDataHeader& outHeader,
                              const uint8_t*& outInner,
                              size_t& outInnerLen);

// --- UUID helpers (v4) -----------------------------------------------------
// Parse a lobby session UUID (any of "<32 hex>", "8-4-4-4-12", or
// prefix thereof) into 16 raw bytes.  Returns true iff exactly 32
// hex digits were parsed.  Used by both sides to derive the 16-byte
// relay session key the server uses in its RelayTable.
bool UuidHexToBytes16(const char* s, uint8_t out[16]);

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
