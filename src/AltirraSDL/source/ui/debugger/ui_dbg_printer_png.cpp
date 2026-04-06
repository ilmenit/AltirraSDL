//	AltirraSDL - Printer output PNG writer (split from ui_dbg_printer.cpp Phase 3h)

#include <stdafx.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <vd2/system/binary.h>
#include <vd2/system/color.h>
#include <vd2/system/vdtypes.h>
#include "printeroutput.h"
#include "printer_internal.h"

bool SaveFramebufferAsPNG(const uint32 *framebuffer, int w, int h, const char *path) {
	FILE *f = fopen(path, "wb");
	if (!f)
		return false;

	auto writeBE32 = [](uint8 *p, uint32 v) {
		p[0] = (uint8)(v >> 24);
		p[1] = (uint8)(v >> 16);
		p[2] = (uint8)(v >> 8);
		p[3] = (uint8)v;
	};

	// CRC32 table
	static uint32 crcTable[256];
	static bool crcInit = false;
	if (!crcInit) {
		for (uint32 n = 0; n < 256; n++) {
			uint32 c = n;
			for (int k = 0; k < 8; k++)
				c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
			crcTable[n] = c;
		}
		crcInit = true;
	}

	auto writeChunk = [&](const char *type, const uint8 *data, uint32 len) {
		uint8 hdr[8];
		writeBE32(hdr, len);
		memcpy(hdr + 4, type, 4);
		fwrite(hdr, 1, 8, f);
		// CRC covers type + data
		uint32 crc = 0xFFFFFFFF;
		for (int i = 4; i < 8; i++)
			crc = crcTable[(crc ^ hdr[i]) & 0xFF] ^ (crc >> 8);
		for (uint32 i = 0; i < len; i++)
			crc = crcTable[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
		crc ^= 0xFFFFFFFF;
		if (len > 0)
			fwrite(data, 1, len, f);
		uint8 crcBuf[4];
		writeBE32(crcBuf, crc);
		fwrite(crcBuf, 1, 4, f);
	};

	// PNG signature
	static const uint8 sig[] = { 137, 80, 78, 71, 13, 10, 26, 10 };
	fwrite(sig, 1, 8, f);

	// IHDR
	{
		uint8 ihdr[13];
		writeBE32(ihdr, (uint32)w);
		writeBE32(ihdr + 4, (uint32)h);
		ihdr[8] = 8;	// bit depth
		ihdr[9] = 2;	// color type: RGB
		ihdr[10] = 0;	// compression
		ihdr[11] = 0;	// filter
		ihdr[12] = 0;	// interlace
		writeChunk("IHDR", ihdr, 13);
	}

	// IDAT — raw image data wrapped in zlib store blocks.
	// Each row: 1 filter byte + w*3 RGB bytes
	const uint32 rowBytes = 1 + w * 3;
	const uint64 rawSize = (uint64)rowBytes * h;

	// Build raw image
	std::vector<uint8> raw(rawSize);
	for (int y = 0; y < h; y++) {
		uint8 *rowDst = &raw[y * rowBytes];
		rowDst[0] = 0;	// no filter
		const uint32 *rowSrc = &framebuffer[y * w];
		for (int x = 0; x < w; x++) {
			uint32 px = rowSrc[x];
			rowDst[1 + x * 3 + 0] = (uint8)(px >> 16);	// R
			rowDst[1 + x * 3 + 1] = (uint8)(px >> 8);		// G
			rowDst[1 + x * 3 + 2] = (uint8)(px);			// B
		}
	}

	// Adler-32 checksum for zlib
	uint32 adler_a = 1, adler_b = 0;
	for (size_t i = 0; i < raw.size(); i++) {
		adler_a = (adler_a + raw[i]) % 65521;
		adler_b = (adler_b + adler_a) % 65521;
	}
	uint32 adler32val = (adler_b << 16) | adler_a;

	// Wrap in zlib store blocks: header(2) + blocks + adler32(4)
	size_t numBlocks = (raw.size() + 65534) / 65535;
	size_t zlibSize = 2 + numBlocks * 5 + raw.size() + 4;
	std::vector<uint8> zlib(zlibSize);
	zlib[0] = 0x78;	// CMF
	zlib[1] = 0x01;	// FLG (no dict, level 0)

	size_t zpos = 2;
	size_t remaining = raw.size();
	size_t srcPos = 0;
	while (remaining > 0) {
		uint16 blockLen = (uint16)std::min<size_t>(remaining, 65535);
		bool last = (remaining <= 65535);
		zlib[zpos++] = last ? 0x01 : 0x00;
		zlib[zpos++] = (uint8)(blockLen & 0xFF);
		zlib[zpos++] = (uint8)(blockLen >> 8);
		uint16 nlen = ~blockLen;
		zlib[zpos++] = (uint8)(nlen & 0xFF);
		zlib[zpos++] = (uint8)(nlen >> 8);
		memcpy(&zlib[zpos], &raw[srcPos], blockLen);
		zpos += blockLen;
		srcPos += blockLen;
		remaining -= blockLen;
	}
	writeBE32(&zlib[zpos], adler32val);
	zpos += 4;

	writeChunk("IDAT", zlib.data(), (uint32)zpos);

	// IEND
	writeChunk("IEND", nullptr, 0);

	fclose(f);
	return true;
}

