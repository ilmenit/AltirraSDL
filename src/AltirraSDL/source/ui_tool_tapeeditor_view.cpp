//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2021 Avery Lee
//	SDL3/ImGui tape editor — waveform/spectrogram rendering and mouse interaction
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#include <stdafx.h>
#include <cmath>
#include <algorithm>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/math.h>
#include <vd2/system/color.h>
#include <at/atio/cassetteimage.h>
#include <at/atcore/fft.h>
#include "cassette.h"
#include "simulator.h"
#include "ui_tool_tapeeditor.h"
#include "logging.h"

extern ATSimulator g_sim;

// ---- Spectrogram initialization ----

void ATTapeEditorState::InitSpectrogram() {
	if (mpFFT)
		return;

	mpFFT = new ATFFT<128>;

	// Kaiser window
	const auto I0 = [](float x) {
		float x2 = x * x;
		float sum = 1.0f;
		float term = x2 / 4.0f;
		for (int i = 1; i < 5; ++i) {
			sum += term;
			term *= x2 * 0.25f / ((float)i * (float)i);
		}
		return sum;
	};

	static constexpr float kPi = 3.14159265358979323846f;
	static constexpr float a = 4.0f;
	const float scale = 0.25f * 127.0f / I0(kPi * a);

	for (size_t i = 0; i < 128; ++i) {
		const float t = (float)i / 64.0f;
		mFFTWindow[i] = I0(kPi * a * sqrtf((2.0f - t) * t)) * scale;
	}

	// Spectrogram palette using Oklch
	const float oklabToXYZ[3][3] = {
		{1.0f, 1.0f, 1.0f},
		{0.3963377774f, -0.1055613458f, -0.0894841775f},
		{0.2158037573f, -0.0638541728f, -1.2914855480f}
	};

	const float xyzToLinRGB[3][3] = {
		{4.0767416621f, -1.2684380046f, -0.0041960863f},
		{-3.3077115913f, 2.6097574011f, -0.7034186147f},
		{0.2309699292f, -0.3413193965f, 1.7076147010f}
	};

	for (size_t i = 0; i < 256; ++i) {
		float x = (float)i / 255.0f;
		float l = x;
		float c = 0.5f * x * (1.0f - x);
		float h = 2.0f * kPi * (x + 0.5f);

		float ca = c * cosf(h);
		float cb = c * sinf(h);

		float lms[3];
		for (int j = 0; j < 3; ++j)
			lms[j] = oklabToXYZ[0][j] * l + oklabToXYZ[1][j] * ca + oklabToXYZ[2][j] * cb;

		for (int j = 0; j < 3; ++j)
			lms[j] = lms[j] * lms[j] * lms[j];

		float linrgb[3];
		for (int j = 0; j < 3; ++j)
			linrgb[j] = xyzToLinRGB[0][j] * lms[0] + xyzToLinRGB[1][j] * lms[1] + xyzToLinRGB[2][j] * lms[2];

		auto toSRGB = [](float v) -> uint8 {
			v = std::clamp(v, 0.0f, 1.0f);
			v = v <= 0.0031308f ? v * 12.92f : 1.055f * powf(v, 1.0f / 2.4f) - 0.055f;
			return (uint8)(v * 255.0f + 0.5f);
		};

		uint8 r = toSRGB(linrgb[0]);
		uint8 g = toSRGB(linrgb[1]);
		uint8 b = toSRGB(linrgb[2]);
		mSpectrogramPalette[i] = IM_COL32(r, g, b, 255);
	}
}

void ATTapeEditorState::UpdatePalettes() {
	if (mZoom >= 0)
		return;

	int palBits = std::min(8, -mZoom);
	int n = 1 << palBits;
	float x = 0;
	float xinc = (float)(1 << (8 - palBits)) / 256.0f * 2.0f;

	for (int i = 0; i <= n; ++i) {
		uint8 v = (uint8)(191.0f * powf(std::min(1.0f, x), 1.0f / 2.2f) + 0.5f);
		uint8 base = 0x40;
		mPalette[i] = IM_COL32(base + v, base + v, base + v, 255);
		x += xinc;
	}

	mPaletteShift = -mZoom - palBits;
}

// ---- Main tape view rendering ----

void ATTapeEditorState::RenderTapeView(float viewWidth, float viewHeight, float viewX, float viewY) {
	if (!mpImage || viewWidth <= 0 || viewHeight <= 0)
		return;

	ImDrawList *dl = ImGui::GetWindowDrawList();
	const float hw = viewWidth * 0.5f;

	// Coordinate range
	const sint64 gx1 = mScrollX - (sint64)hw;
	const sint64 gx2 = mScrollX + (sint64)hw;

	sint64 idx1 = gx1;
	sint64 idx2 = gx2;

	if (mZoom > 0) {
		idx1 = (idx1 - 1) >> mZoom;
		idx2 = (idx2 >> mZoom) + 2;
		idx2 = std::min<sint64>(idx2, (sint64)mSampleCount << mZoom);
	} else {
		--idx1;
		++idx2;
		idx2 = std::min<sint64>(idx2, (((sint64)mSampleCount - 1) >> -mZoom) + 1);
	}

	if (idx1 < 0)
		idx1 = 0;

	const float xinc = mZoom <= 0 ? 1.0f : (float)(1 << mZoom);
	float drawX = viewX + (float)((mZoom <= 0 ? idx1 : (idx1 << mZoom)) + (sint64)hw - mScrollX);

	const int posshift = mZoom <= 0 ? -mZoom : 0;
	const uint32 posinc = 1 << posshift;
	const uint32 posStart = mZoom <= 0 ? (uint32)(idx1 << -mZoom) : (uint32)idx1;

	const sint32 n = (sint32)(idx2 - idx1);
	const uint32 posEnd = posStart + posinc * n;

	// Background
	dl->AddRectFilled(ImVec2(viewX, viewY), ImVec2(viewX + viewWidth, viewY + viewHeight), IM_COL32(0, 0, 0, 255));

	if (n > 0) {
		// Draw regions (blank/mark/decoded) background colors
		static constexpr uint32 kRegionColors[] = {
			IM_COL32(0x1D, 0x1D, 0x1D, 255),
			IM_COL32(0x00, 0x00, 0x60, 255),
			IM_COL32(0x00, 0x3A, 0x00, 255),
		};

		uint32 rpos = posStart + (posinc >> 1);
		float rx1 = drawX;
		float rx2 = drawX;
		ATCassetteRegionType lastRegionType {};

		for (sint32 i = 0; i < n; ) {
			ATCassetteRegionInfo regionInfo = mpImage->GetRegionInfo(rpos + i * posinc);
			uint32 regionEnd = std::min(regionInfo.mRegionStart + regionInfo.mRegionLen, posEnd);
			uint32 pts = ((regionEnd - rpos - 1) >> posshift) + 1;
			float rxi = std::min(drawX + xinc * (float)pts, viewX + viewWidth);

			if (regionInfo.mRegionType != lastRegionType) {
				if (rx1 < rx2)
					dl->AddRectFilled(ImVec2(rx1, viewY), ImVec2(rx2, viewY + viewHeight), kRegionColors[(int)lastRegionType]);

				lastRegionType = regionInfo.mRegionType;
				rx1 = rx2;
			}

			rx2 = rxi;
			i = pts;
		}

		// Draw the last region
		if (rx1 < rx2)
			dl->AddRectFilled(ImVec2(rx1, viewY), ImVec2(rx2, viewY + viewHeight), kRegionColors[(int)lastRegionType]);
	}

	// Division markers and time labels
	{
		// Compute division spacing based on zoom
		double pixPerMarker = kATCassetteDataSampleRateD * ldexp(1.0, mZoom) / 1000.0;

		static const uint32 kSteps[] = {
			1, 2, 5, 10, 20, 50, 100, 200, 500,
			1000, 2000, 5000, 10000, 20000, 30000,
			60000*1, 60000*2, 60000*5, 60000*10, 60000*20, 60000*30
		};

		bool showMS = false;
		double targetPix = 100.0;

		for (uint32 step : kSteps) {
			double v = pixPerMarker * (double)step;
			if (v >= targetPix) {
				pixPerMarker = v;
				showMS = step < 1000;
				break;
			}
		}

		if (pixPerMarker < targetPix) {
			pixPerMarker *= 60000.0 * 60.0;
			while (pixPerMarker < targetPix)
				pixPerMarker *= 10.0;
		}

		sint64 div1 = std::max<sint64>(0, (sint64)floor((double)gx1 / pixPerMarker));
		sint64 div2 = (sint64)ceil((double)gx2 / pixPerMarker);

		for (sint64 div = div1; div < div2; ++div) {
			double divgxf = (double)div * pixPerMarker;
			float divx = viewX + (float)(VDRoundToInt64(divgxf) - mScrollX + (sint64)hw);

			dl->AddLine(ImVec2(divx, viewY), ImVec2(divx, viewY + viewHeight), IM_COL32(0x40, 0x40, 0x40, 255));

			double t = ldexp(divgxf, -mZoom) * (1000.0 / kATCassetteDataSampleRateD);
			uint32 msec = (uint32)(t + 0.5);
			uint32 sec = msec / 1000; msec %= 1000;
			uint32 min = sec / 60; sec %= 60;
			uint32 hr = min / 60; min %= 60;

			char buf[32];
			if (showMS)
				snprintf(buf, sizeof(buf), "%u:%02u:%02u.%03u", hr, min, sec, msec);
			else
				snprintf(buf, sizeof(buf), "%u:%02u:%02u", hr, min, sec);

			dl->AddText(ImVec2(divx + 4, viewY + 4), IM_COL32(0xEE, 0xEE, 0xEE, 255), buf);
		}
	}

	// Head position marker
	{
		float xhead = SampleToClientX(mHeadPosition, viewWidth) + viewX;
		if (xhead >= viewX - 16 && xhead < viewX + viewWidth + 16) {
			uint32 headColor = mbHeadPlay ? IM_COL32(0x40, 0x80, 0xFF, 255)
				: mbHeadRecord ? IM_COL32(0xFF, 0x00, 0x00, 255)
				: IM_COL32(0x80, 0x80, 0x80, 255);

			dl->AddLine(ImVec2(xhead, viewY), ImVec2(xhead, viewY + viewHeight), headColor);

			// Triangle at bottom
			ImVec2 tri[3] = {
				ImVec2(xhead - 8, viewY + viewHeight),
				ImVec2(xhead + 8, viewY + viewHeight),
				ImVec2(xhead, viewY + viewHeight - 14)
			};
			dl->AddTriangleFilled(tri[0], tri[1], tri[2], headColor);
		}
	}

	const bool hasWaveform = mpImage->GetWaveformLength() > 0;
	const bool showWaveform = mWaveformMode != TapeWaveformMode::None && hasWaveform;
	const bool showSpectrogram = mWaveformMode == TapeWaveformMode::Spectrogram;
	const float yhi = viewY + (showWaveform ? viewHeight * 5.0f / 8.0f : viewHeight * 0.25f);
	const float ylo = viewY + (showWaveform ? viewHeight * 7.0f / 8.0f : viewHeight * 0.75f);

	if (n > 0) {
		uint32 pos = posStart;

		// Waveform rendering
		if (n > 1 && showWaveform) {
			const float ywfhi = viewY + viewHeight / 8.0f;
			const float ywflo = viewY + viewHeight * 3.0f / 8.0f;

			if (showSpectrogram && mpFFT) {
				// Spectrogram rendering
				static constexpr float kGain = 18.0f;
				static constexpr float kSamplingRate = 31960.2f;
				static constexpr float kBinWidth = kSamplingRate / 128.0f;
				static constexpr float kSpaceTone = kSamplingRate / 8.0f;
				static constexpr float kMarkTone = kSamplingRate / 6.0f;
				static constexpr float kBinLo = (kSpaceTone - 2 * (kMarkTone - kSpaceTone)) / kBinWidth;
				static constexpr float kBinHi = (kMarkTone + 2 * (kMarkTone - kSpaceTone)) / kBinWidth;

				const float sh = ywflo - ywfhi;
				if (sh > 2) {
					float binSpacing = (kBinHi - kBinLo) / sh;
					float buf[128];

					for (sint32 i = 0; i < n; ++i) {
						uint32 centerPos = pos + posinc * i;
						uint32 offset = centerPos < 64 ? 64 - centerPos : 0;

						for (size_t j = 0; j < offset; ++j)
							buf[j] = 0;

						uint32 len = 128 - offset;
						uint32 actual = mpImage->ReadWaveform(buf + offset, centerPos + offset - 64, len, mbShowTurboData);

						for (size_t j = actual + offset; j < 128; ++j)
							buf[j] = 0;

						for (size_t j = 0; j < 128; ++j)
							buf[j] *= mFFTWindow[j];

						mpFFT->Forward(buf);

						buf[0] = 0;
						buf[126] = 0;

						for (size_t j = 2; j < 128; j += 2) {
							float re = buf[j];
							float im = buf[j + 1];
							float mag = re * re + im * im;
							buf[j] = logf(std::max<float>(1e-9f, mag));
						}

						// Paint vertical column
						float fx = drawX + xinc * (float)i;
						float bin = kBinHi;

						for (float y = 0; y < sh; y += 1.0f) {
							int binIdx = (int)bin;
							float t = bin - (float)binIdx;

							float v0 = (binIdx >= 1 && binIdx * 2 < 126) ? buf[binIdx * 2 - 2] : 0;
							float v1 = (binIdx >= 0 && binIdx * 2 < 126) ? buf[binIdx * 2] : 0;
							float v2 = (binIdx >= 0 && (binIdx + 1) * 2 < 128) ? buf[(binIdx + 1) * 2] : 0;
							float v3 = (binIdx >= 0 && (binIdx + 2) * 2 < 128) ? buf[(binIdx + 2) * 2] : 0;

							float val = kGain * (
								((-0.5f * t + 1.0f) * t - 0.5f) * t * v0 +
								((1.5f * t - 2.5f) * t * t + 1) * v1 +
								((-1.5f * t + 2.0f) * t + 0.5f) * t * v2 +
								(0.5f * t - 0.5f) * t * t * v3
							);

							int palIdx = (int)std::clamp(val, 0.0f, 255.0f);

							float px = fx;
							float py = ywfhi + y;
							float pxe = fx + xinc;

							dl->AddRectFilled(ImVec2(px, py), ImVec2(pxe, py + 1.0f), mSpectrogramPalette[palIdx]);

							bin -= binSpacing;
						}

						// Frequency guidelines
						if (mbShowFrequencyGuidelines) {
							float markerY0 = ywfhi + sh * (kBinHi - kSpaceTone / kBinWidth) / (kBinHi - kBinLo);
							float markerY1 = ywfhi + sh * (kBinHi - kMarkTone / kBinWidth) / (kBinHi - kBinLo);

							if (((centerPos + posinc * 4) / (posinc * 8)) & 1) {
								dl->AddRectFilled(ImVec2(fx, markerY0), ImVec2(fx + xinc, markerY0 + 1), IM_COL32(0x40, 0x40, 0x40, 255));
								dl->AddRectFilled(ImVec2(fx, markerY1), ImVec2(fx + xinc, markerY1 + 1), IM_COL32(0x40, 0x40, 0x40, 255));
							}
						}
					}
				}
			} else {
				// Waveform line rendering
				auto GetYForV = [=](float v) { return ywflo + (ywfhi - ywflo) * (v * 0.5f + 0.5f); };

				// Zero line
				dl->AddLine(ImVec2(viewX, GetYForV(0)), ImVec2(viewX + viewWidth, GetYForV(0)), IM_COL32(0x80, 0x80, 0x80, 255));

				if (mZoom < 0) {
					// Min/max bars
					for (sint32 i = 0; i < n; ++i) {
						const auto minMax = mpImage->ReadWaveformMinMax(pos + posinc * i, posinc + 1, mbShowTurboData);
						float ry1 = GetYForV(minMax.mMax);
						float ry2 = GetYForV(minMax.mMin);
						float px = drawX + (float)i;
						dl->AddLine(ImVec2(px, ry1), ImVec2(px, ry2), IM_COL32(0xFF, 0xFF, 0xFF, 255));
					}
				} else {
					// Connected line
					ImVec2 prev;
					for (sint32 i = 0; i < n; ++i) {
						float v = 0;
						mpImage->ReadWaveform(&v, pos + posinc * i, 1, mbShowTurboData);
						ImVec2 pt(drawX + xinc * (float)i, GetYForV(v));

						if (i > 0)
							dl->AddLine(prev, pt, IM_COL32(0xFF, 0xFF, 0xFF, 255));

						prev = pt;
					}
				}
			}
		}

		// Data bits rendering
		UpdatePalettes();

		if (mZoom < 0) {
			// Zoomed out — density bars
			uint32 range = 1 << -mZoom;
			pos = posStart;

			for (sint32 i = 0; i < n; ++i) {
				const IATCassetteImage::TransitionInfo ti = mpImage->GetTransitionInfo(pos, range, mbShowTurboData);
				const uint32 markCount = ti.mMarkBits + ti.mTransitionBits;
				const uint32 spaceCount = ti.mSpaceBits + ti.mTransitionBits;

				float px = drawX + (float)i;

				if (markCount > 0)
					dl->AddRectFilled(ImVec2(px, yhi), ImVec2(px + 1.0f, yhi + 1.0f), mPalette[markCount >> mPaletteShift]);

				if (spaceCount > 0)
					dl->AddRectFilled(ImVec2(px, ylo), ImVec2(px + 1.0f, ylo + 1.0f), mPalette[spaceCount >> mPaletteShift]);

				pos += posinc;
			}
		} else {
			// Zoomed in — step waveform
			pos = posStart;
			ImVec2 prev;
			bool prevPol = false;

			for (sint32 i = 0; i < n; ++i) {
				const bool polarity = mpImage->GetBit(pos, mbShowTurboData);
				float px = drawX + xinc * (float)i;
				float py = polarity ? yhi : ylo;

				if (i > 0) {
					// Vertical transition line
					if (polarity != prevPol)
						dl->AddLine(ImVec2(px, yhi), ImVec2(px, ylo), IM_COL32(0xE0, 0xE0, 0xE0, 255));

					// Horizontal line
					dl->AddLine(prev, ImVec2(px, prev.y), IM_COL32(0xE0, 0xE0, 0xE0, 255));
				}

				prev = ImVec2(px, py);
				prevPol = polarity;
				pos += posinc;
			}
		}

		// Analysis channel rendering
		for (int chIdx = 0; chIdx < 2; ++chIdx) {
			const TapeAnalysisChannel& ch = mAnalysisChannels[chIdx];
			if (ch.mDecodedBlocks.mBlocks.empty())
				continue;

			float ya = ylo + 5.0f + chIdx * 20.0f;
			float yah = 16.0f;

			for (const TapeDecodedBlock& dblock : ch.mDecodedBlocks.mBlocks) {
				if (dblock.mSampleEnd < posStart || dblock.mSampleStart > posEnd)
					continue;

				const TapeDecodedByte *allBytes = ch.mDecodedBlocks.mByteData.data() + dblock.mStartByte;

				for (uint32 bi = 0; bi < dblock.mByteCount; ++bi) {
					const TapeDecodedByte& db = allBytes[bi];
					uint32 byteEnd = allBytes[bi + 1].mStartSample;

					if (byteEnd < posStart || db.mStartSample > posEnd)
						continue;

					float bx1 = SampleEdgeToClientX(db.mStartSample, viewWidth) + viewX;
					float bx2 = SampleEdgeToClientX(byteEnd, viewWidth) + viewX;

					bool hasFramingError = (db.mFlags & TapeDecodedByteFlags::FramingError) != TapeDecodedByteFlags::None;
					bool hasValidChecksum = (sint32)bi <= (sint32)dblock.mChecksumPos && dblock.mbValidFrame;
					bool isChecksum = (sint32)bi == (sint32)dblock.mChecksumPos;

					// Separator line
					uint32 sepColor = hasValidChecksum ? IM_COL32(0x80, 0xFF, 0xC4, 255) : IM_COL32(0x70, 0x70, 0x70, 255);
					dl->AddLine(ImVec2(bx1, ya), ImVec2(bx1, ya + yah), sepColor);

					// Byte value text or block
					float space = bx2 - bx1;
					if (space > 18.0f) {
						char hexBuf[4];
						snprintf(hexBuf, sizeof(hexBuf), "%02X", db.mData);
						uint32 textColor = hasFramingError ? IM_COL32(0xFF, 0x40, 0x40, 255)
							: hasValidChecksum ? IM_COL32(0x80, 0xFF, 0xE0, 255)
							: isChecksum ? IM_COL32(0x99, 0x44, 0xFF, 255)
							: IM_COL32(0xE0, 0xE0, 0xE0, 255);
						float textX = bx1 + (space - ImGui::CalcTextSize("00").x) * 0.5f;
						dl->AddText(ImVec2(textX, ya), textColor, hexBuf);
					} else if (space > 2.0f) {
						uint32 blockColor = hasFramingError ? IM_COL32(0xDF, 0x60, 0x60, 255)
							: hasValidChecksum ? IM_COL32(0x80, 0xDF, 0xC8, 255)
							: isChecksum ? IM_COL32(0x99, 0x44, 0xFF, 255)
							: IM_COL32(0x80, 0x80, 0x80, 255);
						dl->AddRectFilled(ImVec2(bx1 + 1, ya + 1), ImVec2(bx2, ya + yah - 1), blockColor);
					}
				}
			}
		}
	}

	// Selection overlay
	if (mbSelectionValid) {
		float selx1 = SampleEdgeToClientX(mSelSortedStartSample, viewWidth) + viewX;

		if (mSelStartSample == mSelEndSample) {
			// Cursor line
			dl->AddLine(ImVec2(selx1, viewY), ImVec2(selx1, viewY + viewHeight), IM_COL32(0x80, 0x80, 0x80, 0xC0));
		} else {
			float selx2 = SampleEdgeToClientX(mSelSortedEndSample, viewWidth) + viewX;
			if (selx2 > selx1)
				dl->AddRectFilled(ImVec2(selx1, viewY), ImVec2(selx2, viewY + viewHeight), IM_COL32(0x40, 0x80, 0xFF, 0x60));
		}
	}

	// Draw tool preview
	if (mbDrawValid) {
		float drx1 = SampleToClientX(std::min(mDrawStartSample, mDrawEndSample), viewWidth) + viewX;
		float drx2 = SampleToClientX(std::max(mDrawStartSample, mDrawEndSample), viewWidth) + viewX;
		float dry = mbDrawPolarity ? yhi : ylo;

		dl->AddRectFilled(ImVec2(drx1 - 2, dry - 2), ImVec2(drx2 + 2, dry + 2), IM_COL32(0xFF, 0x00, 0x00, 255));
	}

	// ---- Mouse interaction ----
	ImGui::SetCursorScreenPos(ImVec2(viewX, viewY));
	ImGui::InvisibleButton("##tapeview", ImVec2(viewWidth, viewHeight));

	const bool isHovered = ImGui::IsItemHovered();
	const ImVec2 mousePos = ImGui::GetMousePos();
	const float localMouseX = mousePos.x - viewX;

	// Mouse wheel zoom
	if (isHovered) {
		float wheel = ImGui::GetIO().MouseWheel;
		if (wheel != 0) {
			mZoomAccum += wheel;
			int dz = (int)(mZoomAccum + (mZoomAccum < 0 ? -0.01f : 0.01f));
			mZoomAccum -= (float)dz;
			SetZoom(mZoom + dz, (sint32)localMouseX, (sint32)viewWidth);
			UpdatePalettes();
		}
	}

	// Mouse click/drag
	if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		mbDragging = true;
		mDragOriginX = mousePos.x;
		mActiveDragMode = mDrawMode;

		switch (mDrawMode) {
			case TapeDrawMode::Scroll:
				break;

			case TapeDrawMode::Select:
				if (ImGui::GetIO().KeyShift && HasSelection()) {
					SetSelection(mSelStartSample, ClientXToSampleEdge(localMouseX, viewWidth, true));
				} else {
					uint32 samp = ClientXToSampleEdge(localMouseX, viewWidth, true);
					SetSelection(samp, samp);
				}
				break;

			case TapeDrawMode::Insert:
			case TapeDrawMode::Analyze: {
				uint32 samp = ClientXToSampleEdge(localMouseX, viewWidth, true);
				SetSelection(samp, samp);
				break;
			}

			case TapeDrawMode::Draw:
				ClearSelection();
				mbDrawValid = true;
				if (mWaveformMode != TapeWaveformMode::None && mpImage->GetWaveformLength() > 0)
					mbDrawPolarity = (mousePos.y - viewY) < viewHeight * 3.0f / 4.0f;
				else
					mbDrawPolarity = (mousePos.y - viewY) < viewHeight * 0.5f;
				mDrawStartSample = mDrawEndSample = ClientXToSample(localMouseX, viewWidth);
				break;
		}
	}

	// Right-click drag for scroll
	if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
		mbDragging = true;
		mDragOriginX = mousePos.x;
		mActiveDragMode = TapeDrawMode::Scroll;
	}

	if (mbDragging && (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right))) {
		switch (mActiveDragMode) {
			case TapeDrawMode::Scroll: {
				float dx = mousePos.x - mDragOriginX;
				mDragOriginX = mousePos.x;
				mScrollX = std::clamp<sint64>(mScrollX - (sint64)dx, 0, mScrollMax);
				break;
			}

			case TapeDrawMode::Select:
			case TapeDrawMode::Analyze:
				SetSelection(mSelStartSample, ClientXToSampleEdge(localMouseX, viewWidth, true));
				break;

			case TapeDrawMode::Draw: {
				uint32 samp = ClientXToSample(localMouseX, viewWidth);
				if (mDrawEndSample != samp)
					mDrawEndSample = samp;
				break;
			}

			case TapeDrawMode::Insert:
				SetSelection(mSelStartSample, std::max(mSelStartSample, ClientXToSampleEdge(localMouseX, viewWidth, false)));
				break;
		}
	}

	// Mouse release
	if (mbDragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
		switch (mActiveDragMode) {
			case TapeDrawMode::Draw:
				if (mbDrawValid && mpImage) {
					mbDrawValid = false;
					const uint32 start = std::min(mDrawStartSample, mDrawEndSample);
					const uint32 end = std::max(mDrawStartSample, mDrawEndSample);

					if (start < end) {
						ATCassetteWriteCursor cursor { start };
						const uint32 deckPos = PreModify();
						PushUndo(start, end - start, end - start, TapeUndoSelMode::SelectionIsRange);
						mpImage->WritePulse(cursor, mbDrawPolarity, end - start, false, !mbShowTurboData);
						PostModify(deckPos);
					}
				}
				break;

			case TapeDrawMode::Insert:
				Insert();
				break;

			case TapeDrawMode::Analyze:
				if (HasNonEmptySelection()) {
					Analyze(mSelSortedStartSample, mSelSortedEndSample);
					ClearSelection();
				}
				break;

			default:
				break;
		}

		mbDragging = false;
	}
}
