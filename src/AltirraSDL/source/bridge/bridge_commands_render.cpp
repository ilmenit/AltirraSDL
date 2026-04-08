// AltirraBridge - Phase 4 rendering commands (impl)
//
// We pull the last frame from GTIA via GetLastFrameBuffer(), convert
// to XRGB8888 using Kasumi's VDPixmapBlt, then either:
//   - write a PNG to disk (path=...)
//   - emit the PNG base64-inlined (inline=true, default)
// RAWSCREEN emits the raw pixel buffer directly (no PNG wrap) for
// clients that want to do their own encoding.
//
// The PNG encoder is vendored locally so the bridge module stays
// self-contained (single `rm -rf source/bridge` removes everything).
// It is a straight adaptation of the store-block zlib/PNG writer from
// ui_dbg_printer_png.cpp.

#include <stdafx.h>

#include "bridge_commands_render.h"
#include "bridge_protocol.h"

#include "simulator.h"
#include "gtia.h"

#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace ATBridge {

namespace {

// ---------------------------------------------------------------------------
// Token helpers. Phase 4 uses `key=value` style options (inline=true,
// path=/tmp/foo.png) rather than positional args, so we need a small
// option parser.
// ---------------------------------------------------------------------------

bool MatchKey(const std::string& tok, const char* key, std::string& value) {
	const size_t kl = std::strlen(key);
	if (tok.size() <= kl) return false;
	if (std::strncmp(tok.c_str(), key, kl) != 0) return false;
	if (tok[kl] != '=') return false;
	value.assign(tok, kl + 1, std::string::npos);
	return true;
}

bool IsTrueLiteral(const std::string& s) {
	return s == "1" || s == "true" || s == "yes" || s == "on";
}

// ---------------------------------------------------------------------------
// Vendored PNG encoder (RGB, store-block zlib, no external deps).
// Input: 32-bit pixels packed 0x00RRGGBB (XRGB8888 little-endian).
// ---------------------------------------------------------------------------

void PngWriteBE32(std::vector<uint8_t>& out, uint32_t v) {
	out.push_back((uint8_t)(v >> 24));
	out.push_back((uint8_t)(v >> 16));
	out.push_back((uint8_t)(v >>  8));
	out.push_back((uint8_t)v);
}

uint32_t PngCrc32(const uint8_t* data, size_t len, uint32_t seed = 0xFFFFFFFFu) {
	static uint32_t table[256];
	static bool init = false;
	if (!init) {
		for (uint32_t n = 0; n < 256; ++n) {
			uint32_t c = n;
			for (int k = 0; k < 8; ++k)
				c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
			table[n] = c;
		}
		init = true;
	}
	uint32_t c = seed;
	for (size_t i = 0; i < len; ++i)
		c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
	return c;
}

void PngWriteChunk(std::vector<uint8_t>& out, const char* type, const uint8_t* data, uint32_t len) {
	PngWriteBE32(out, len);
	const size_t typeOff = out.size();
	out.push_back((uint8_t)type[0]);
	out.push_back((uint8_t)type[1]);
	out.push_back((uint8_t)type[2]);
	out.push_back((uint8_t)type[3]);
	if (len) out.insert(out.end(), data, data + len);
	uint32_t crc = PngCrc32(&out[typeOff], 4 + len);
	crc ^= 0xFFFFFFFFu;
	PngWriteBE32(out, crc);
}

void EncodePng(const uint32_t* pixels, int w, int h, std::vector<uint8_t>& out) {
	out.clear();
	out.reserve(64 + (size_t)w * h * 3);
	static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
	out.insert(out.end(), sig, sig + 8);

	// IHDR
	uint8_t ihdr[13] = {};
	ihdr[0] = (uint8_t)((uint32_t)w >> 24);
	ihdr[1] = (uint8_t)((uint32_t)w >> 16);
	ihdr[2] = (uint8_t)((uint32_t)w >>  8);
	ihdr[3] = (uint8_t)((uint32_t)w);
	ihdr[4] = (uint8_t)((uint32_t)h >> 24);
	ihdr[5] = (uint8_t)((uint32_t)h >> 16);
	ihdr[6] = (uint8_t)((uint32_t)h >>  8);
	ihdr[7] = (uint8_t)((uint32_t)h);
	ihdr[8] = 8;   // bit depth
	ihdr[9] = 2;   // RGB
	PngWriteChunk(out, "IHDR", ihdr, 13);

	// Build raw filtered scanlines (filter=0, then RGB triplets)
	const uint32_t rowBytes = 1 + (uint32_t)w * 3;
	std::vector<uint8_t> raw((size_t)rowBytes * h);
	for (int y = 0; y < h; ++y) {
		uint8_t* dst = &raw[(size_t)y * rowBytes];
		dst[0] = 0;
		const uint32_t* src = &pixels[(size_t)y * w];
		for (int x = 0; x < w; ++x) {
			uint32_t p = src[x];
			dst[1 + x * 3 + 0] = (uint8_t)(p >> 16);
			dst[1 + x * 3 + 1] = (uint8_t)(p >>  8);
			dst[1 + x * 3 + 2] = (uint8_t)(p);
		}
	}

	// Adler-32
	uint32_t a = 1, b = 0;
	for (uint8_t byte : raw) {
		a = (a + byte) % 65521;
		b = (b + a)    % 65521;
	}
	uint32_t adler = (b << 16) | a;

	// zlib stored-block wrapper
	size_t numBlocks = (raw.size() + 65534) / 65535;
	if (numBlocks == 0) numBlocks = 1;
	std::vector<uint8_t> zlib;
	zlib.reserve(2 + numBlocks * 5 + raw.size() + 4);
	zlib.push_back(0x78);
	zlib.push_back(0x01);

	size_t remaining = raw.size();
	size_t srcPos    = 0;
	if (remaining == 0) {
		zlib.push_back(0x01);
		zlib.push_back(0x00);
		zlib.push_back(0x00);
		zlib.push_back(0xFF);
		zlib.push_back(0xFF);
	} else {
		while (remaining > 0) {
			uint16_t blockLen = (uint16_t)std::min<size_t>(remaining, 65535);
			bool last = (remaining <= 65535);
			zlib.push_back(last ? 0x01 : 0x00);
			zlib.push_back((uint8_t)(blockLen & 0xFF));
			zlib.push_back((uint8_t)(blockLen >> 8));
			uint16_t nlen = (uint16_t)~blockLen;
			zlib.push_back((uint8_t)(nlen & 0xFF));
			zlib.push_back((uint8_t)(nlen >> 8));
			zlib.insert(zlib.end(), raw.begin() + srcPos, raw.begin() + srcPos + blockLen);
			srcPos    += blockLen;
			remaining -= blockLen;
		}
	}
	zlib.push_back((uint8_t)(adler >> 24));
	zlib.push_back((uint8_t)(adler >> 16));
	zlib.push_back((uint8_t)(adler >>  8));
	zlib.push_back((uint8_t)(adler));

	PngWriteChunk(out, "IDAT", zlib.data(), (uint32_t)zlib.size());
	PngWriteChunk(out, "IEND", nullptr, 0);
}

// ---------------------------------------------------------------------------
// Grab the last GTIA frame and convert to an XRGB8888 row-packed buffer.
// Returns false if no frame is available (e.g. pre-boot, fresh sim).
// ---------------------------------------------------------------------------

bool CaptureXrgb(ATSimulator& sim, VDPixmapBuffer& dst) {
	VDPixmapBuffer srcBuf;
	VDPixmap src;
	if (!sim.GetGTIA().GetLastFrameBuffer(srcBuf, src))
		return false;
	if (src.w <= 0 || src.h <= 0)
		return false;

	dst.init(src.w, src.h, nsVDPixmap::kPixFormat_XRGB8888);
	VDPixmapBlt(dst, src);
	return true;
}

// Pack an XRGB8888 VDPixmapBuffer into a tightly-packed uint32 array
// (row by row, no stride padding). Kasumi's pitch may include padding,
// so we can't just memcpy the whole thing.
void PackXrgb(const VDPixmapBuffer& pm, std::vector<uint32_t>& out) {
	const int w = pm.w;
	const int h = pm.h;
	out.resize((size_t)w * h);
	const uint8_t* srcRow = (const uint8_t*)pm.data;
	for (int y = 0; y < h; ++y) {
		std::memcpy(&out[(size_t)y * w], srcRow, (size_t)w * 4);
		srcRow += pm.pitch;
	}
}

bool WriteAllToFile(const std::string& path, const uint8_t* data, size_t len) {
	std::FILE* f = std::fopen(path.c_str(), "wb");
	if (!f) return false;
	bool ok = std::fwrite(data, 1, len, f) == len;
	std::fclose(f);
	return ok;
}

void AppendDimField(std::string& payload, int w, int h) {
	payload += "\"width\":";
	payload += std::to_string(w);
	payload += ",\"height\":";
	payload += std::to_string(h);
	payload += ',';
}

}  // namespace

// ---------------------------------------------------------------------------
// SCREENSHOT [path=FILE] [inline=true|false]
//   Default: inline=true (base64-encoded PNG in "data").
//   With path=FILE: writes PNG to FILE on the server-side filesystem.
//                   "data" is omitted; "path" echoes the written path.
// ---------------------------------------------------------------------------

std::string CmdScreenshot(ATSimulator& sim, const std::vector<std::string>& tokens) {
	std::string path;
	bool wantInline = true;
	bool pathSet    = false;
	for (size_t i = 1; i < tokens.size(); ++i) {
		std::string v;
		if (MatchKey(tokens[i], "path", v))   { path = v; pathSet = true; continue; }
		if (MatchKey(tokens[i], "inline", v)) { wantInline = IsTrueLiteral(v); continue; }
		return JsonError("SCREENSHOT: unknown option: " + tokens[i]);
	}
	if (pathSet) wantInline = false;
	if (!pathSet && !wantInline)
		return JsonError("SCREENSHOT: must specify path= or inline=true");

	VDPixmapBuffer pm;
	if (!CaptureXrgb(sim, pm))
		return JsonError("SCREENSHOT: no frame available yet");

	std::vector<uint32_t> packed;
	PackXrgb(pm, packed);

	std::vector<uint8_t> png;
	EncodePng(packed.data(), pm.w, pm.h, png);

	std::string payload;
	AppendDimField(payload, pm.w, pm.h);
	payload += "\"format\":\"png\",";

	if (pathSet) {
		if (!WriteAllToFile(path, png.data(), png.size()))
			return JsonError("SCREENSHOT: failed to write " + path);
		payload += "\"path\":\"";
		payload += JsonEscape(path);
		payload += "\",";
		payload += "\"bytes\":";
		payload += std::to_string(png.size());
	} else {
		payload += "\"encoding\":\"base64\",";
		payload += "\"data\":\"";
		payload += Base64Encode(png.data(), png.size());
		payload += "\"";
	}
	return JsonOk(payload);
}

// ---------------------------------------------------------------------------
// RAWSCREEN [path=FILE] [inline=true|false]
//   Emits the raw XRGB8888 buffer. Pixel format is declared in the
//   response so a client can decode without guessing:
//     {"format":"xrgb8888","endian":"little","width":W,"height":H,...}
//   Each 32-bit word is 0x00RRGGBB in native little-endian byte order
//   (so bytes on the wire / in the file are B,G,R,0).
//   Total size is W*H*4 bytes.
// ---------------------------------------------------------------------------

std::string CmdRawScreen(ATSimulator& sim, const std::vector<std::string>& tokens) {
	std::string path;
	bool wantInline = true;
	bool pathSet    = false;
	for (size_t i = 1; i < tokens.size(); ++i) {
		std::string v;
		if (MatchKey(tokens[i], "path", v))   { path = v; pathSet = true; continue; }
		if (MatchKey(tokens[i], "inline", v)) { wantInline = IsTrueLiteral(v); continue; }
		return JsonError("RAWSCREEN: unknown option: " + tokens[i]);
	}
	if (pathSet) wantInline = false;
	if (!pathSet && !wantInline)
		return JsonError("RAWSCREEN: must specify path= or inline=true");

	VDPixmapBuffer pm;
	if (!CaptureXrgb(sim, pm))
		return JsonError("RAWSCREEN: no frame available yet");

	std::vector<uint32_t> packed;
	PackXrgb(pm, packed);

	const uint8_t* bytes = (const uint8_t*)packed.data();
	const size_t   nb    = packed.size() * 4;

	std::string payload;
	AppendDimField(payload, pm.w, pm.h);
	payload += "\"format\":\"xrgb8888\",";
	payload += "\"endian\":\"little\",";
	payload += "\"stride\":";
	payload += std::to_string(pm.w * 4);
	payload += ',';

	if (pathSet) {
		if (!WriteAllToFile(path, bytes, nb))
			return JsonError("RAWSCREEN: failed to write " + path);
		payload += "\"path\":\"";
		payload += JsonEscape(path);
		payload += "\",";
		payload += "\"bytes\":";
		payload += std::to_string(nb);
	} else {
		payload += "\"encoding\":\"base64\",";
		payload += "\"data\":\"";
		payload += Base64Encode(bytes, nb);
		payload += "\"";
	}
	return JsonOk(payload);
}

// ---------------------------------------------------------------------------
// RENDER_FRAME
//   Phase 4 convenience: always inline, always PNG. Equivalent to
//   SCREENSHOT inline=true. Reserved for future extensions (state
//   override block) so clients can lock against a stable command name.
// ---------------------------------------------------------------------------

std::string CmdRenderFrame(ATSimulator& sim, const std::vector<std::string>& tokens) {
	// Reject unknown args so a client that learned a newer server's
	// RENDER_FRAME extension (state override, format selection) gets
	// a hard error against an older server instead of a silently
	// dropped option.
	for (size_t i = 1; i < tokens.size(); ++i)
		return JsonError("RENDER_FRAME: unknown option: " + tokens[i]);
	std::vector<std::string> forced = { "SCREENSHOT", "inline=true" };
	return CmdScreenshot(sim, forced);
}

}  // namespace ATBridge
