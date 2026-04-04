//	AltirraSDL - Dear ImGui debugger memory viewer pane
//	Bitmap interpretation modes: font and graphics pixel generation,
//	SDL_Texture management.

#include <stdafx.h>
#include <algorithm>
#include <cstring>
#include <SDL3/SDL.h>
#include <vd2/system/vdtypes.h>
#include "ui_dbg_memory.h"

extern SDL_Window *g_pWindow;

// =========================================================================
// GenerateBitmapRow — convert one row of memory bytes to ARGB8888 pixels
// =========================================================================
//
// dst        — output pixel buffer
// dstPitch   — stride in uint32s between scanlines (for multi-scanline modes)
// src        — memory bytes for this row (mColumns bytes)
// cols       — number of columns (bytes per row)

void ATImGuiMemoryPaneImpl::GenerateBitmapRow(
	uint32 *dst, int dstPitch,
	const uint8 *src, uint32 cols)
{
	switch (mInterpretMode) {

	// -----------------------------------------------------------------
	// 1-bpp font: 8x8 character cells
	// Each group of 8 bytes encodes one character.
	// Byte j in a group represents column j; bit b = row b.
	// Windows renders bottom-up (row i uses src[7-i]).
	// Output: cols pixels wide, 8 scanlines tall.
	// -----------------------------------------------------------------
	case InterpretMode::Font1Bpp: {
		uint32 chars = cols / 8;
		for (int scanline = 0; scanline < 8; scanline++) {
			uint32 *row = dst + scanline * dstPitch;
			for (uint32 ch = 0; ch < chars; ch++) {
				uint8 byte = src[ch * 8 + (7 - scanline)];
				for (int bit = 7; bit >= 0; bit--)
					*row++ = kPal1Bpp[(byte >> bit) & 1];
			}
		}
		break;
	}

	// -----------------------------------------------------------------
	// 2-bpp font: 8x8 character cells, 2 bits per pixel
	// Each group of 8 bytes, byte j = column j, 2 bits per pixel.
	// Output: (cols/2) pixels wide, 8 scanlines tall.
	// -----------------------------------------------------------------
	case InterpretMode::Font2Bpp: {
		uint32 chars = cols / 8;
		for (int scanline = 0; scanline < 8; scanline++) {
			uint32 *row = dst + scanline * dstPitch;
			for (uint32 ch = 0; ch < chars; ch++) {
				uint8 byte = src[ch * 8 + (7 - scanline)];
				for (int k = 0; k < 4; k++)
					*row++ = kPal2Bpp[(byte >> (6 - 2 * k)) & 3];
			}
		}
		break;
	}

	// -----------------------------------------------------------------
	// 1-bpp graphics: 8 pixels per byte, single scanline
	// Output: cols*8 pixels wide, 1 scanline.
	// -----------------------------------------------------------------
	case InterpretMode::Graphics1Bpp: {
		uint32 *row = dst;
		for (uint32 i = 0; i < cols; i++) {
			uint8 byte = src[i];
			for (int bit = 7; bit >= 0; bit--)
				*row++ = kPal1Bpp[(byte >> bit) & 1];
		}
		break;
	}

	// -----------------------------------------------------------------
	// 2-bpp graphics: 4 pixels per byte, single scanline
	// Output: cols*4 pixels wide, 1 scanline.
	// -----------------------------------------------------------------
	case InterpretMode::Graphics2Bpp: {
		uint32 *row = dst;
		for (uint32 i = 0; i < cols; i++) {
			uint8 byte = src[i];
			*row++ = kPal2Bpp[(byte >> 6) & 3];
			*row++ = kPal2Bpp[(byte >> 4) & 3];
			*row++ = kPal2Bpp[(byte >> 2) & 3];
			*row++ = kPal2Bpp[(byte >> 0) & 3];
		}
		break;
	}

	// -----------------------------------------------------------------
	// 4-bpp graphics: 2 pixels per byte, single scanline
	// Output: cols*2 pixels wide, 1 scanline.
	// -----------------------------------------------------------------
	case InterpretMode::Graphics4Bpp: {
		uint32 *row = dst;
		for (uint32 i = 0; i < cols; i++) {
			uint8 byte = src[i];
			uint8 hi = (byte >> 4) & 0x0F;
			uint8 lo = byte & 0x0F;
			// 16-level grayscale
			uint8 vh = (uint8)(hi * 255 / 15);
			uint8 vl = (uint8)(lo * 255 / 15);
			*row++ = 0xFF000000 | (vh << 16) | (vh << 8) | vh;
			*row++ = 0xFF000000 | (vl << 16) | (vl << 8) | vl;
		}
		break;
	}

	// -----------------------------------------------------------------
	// 8-bpp graphics: 1 pixel per byte, single scanline
	// Output: cols pixels wide, 1 scanline.
	// -----------------------------------------------------------------
	case InterpretMode::Graphics8Bpp: {
		uint32 *row = dst;
		for (uint32 i = 0; i < cols; i++) {
			uint8 v = src[i];
			*row++ = 0xFF000000 | (v << 16) | (v << 8) | v;
		}
		break;
	}

	default:
		break;
	}
}

// =========================================================================
// UpdateBitmapTexture — generate texture for all visible rows
// =========================================================================

void ATImGuiMemoryPaneImpl::UpdateBitmapTexture(int rowCount) {
	if (!IsBitmapMode() || mViewData.empty())
		return;

	// Calculate raw pixel dimensions
	int rawW = 0, rawH = 0;
	switch (mInterpretMode) {
		case InterpretMode::Font1Bpp:
			rawW = mColumns;		// 1 pixel per bit → cols pixels
			rawH = 8;
			break;
		case InterpretMode::Font2Bpp:
			rawW = mColumns / 2;	// 4 pixels per byte, 8 bytes per char
			rawH = 8;
			break;
		case InterpretMode::Graphics1Bpp:
			rawW = mColumns * 8;
			rawH = 1;
			break;
		case InterpretMode::Graphics2Bpp:
			rawW = mColumns * 4;
			rawH = 1;
			break;
		case InterpretMode::Graphics4Bpp:
			rawW = mColumns * 2;
			rawH = 1;
			break;
		case InterpretMode::Graphics8Bpp:
			rawW = mColumns;
			rawH = 1;
			break;
		default:
			return;
	}

	if (rawW <= 0) return;
	int totalH = rawH * rowCount;

	// (Re)allocate texture if needed
	if (!mpBitmapTexture || mBitmapTexW < rawW || mBitmapTexH < totalH) {
		if (mpBitmapTexture) {
			SDL_DestroyTexture(mpBitmapTexture);
			mpBitmapTexture = nullptr;
		}

		SDL_Renderer *renderer = SDL_GetRenderer(g_pWindow);
		if (!renderer) return;

		// Round up to reasonable sizes to avoid frequent reallocation
		mBitmapTexW = std::max(rawW, 256);
		mBitmapTexH = std::max(totalH, 64);

		mpBitmapTexture = SDL_CreateTexture(
			renderer,
			SDL_PIXELFORMAT_ARGB8888,
			SDL_TEXTUREACCESS_STREAMING,
			mBitmapTexW, mBitmapTexH);

		if (!mpBitmapTexture) return;
	}

	// Lock and fill
	void *pixels = nullptr;
	int pitch = 0;
	if (!SDL_LockTexture(mpBitmapTexture, nullptr, &pixels, &pitch))
		return;

	int pitchPixels = pitch / 4;

	// Clear the texture region we'll use
	for (int y = 0; y < totalH; y++)
		std::memset((uint8 *)pixels + y * pitch, 0, rawW * 4);

	// Generate pixel data for each row
	for (int row = 0; row < rowCount; row++) {
		uint32 rowOffset = (uint32)row * mColumns;
		if (rowOffset + mColumns > (uint32)mViewData.size())
			break;

		uint32 *rowDst = (uint32 *)pixels + row * rawH * pitchPixels;
		GenerateBitmapRow(rowDst, pitchPixels,
						  &mViewData[rowOffset], mColumns);
	}

	SDL_UnlockTexture(mpBitmapTexture);
}
