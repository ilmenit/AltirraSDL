//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
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
#include <vd2/system/bitmath.h>
#include <vd2/system/color.h>
#include <vd2/system/error.h>
#include <vd2/system/vdstl_hashtable.h>
#include <vd2/system/vdstl_vectorview.h>
#include "printeroutput.h"

int ATPrinterOutputBase::AddRef() {
	return ++mRefCount;
}

int ATPrinterOutputBase::Release() {
	const int rc = --mRefCount;
	if (!rc) {
		mParent.OnDestroyingOutput(*this);
		delete this;
	}

	return rc;
}

void *ATPrinterOutputBase::AsInterface(uint32 id) {
	return nullptr;
}

const wchar_t *ATPrinterOutputBase::GetName() const {
	return mName.c_str();
}

////////////////////////////////////////////////////////////////////////////////

ATPrinterOutput::ATPrinterOutput(ATPrinterOutputManager& parent, const wchar_t *name)
	: ATPrinterOutputBase(parent, name)
{
}

ATPrinterOutput::~ATPrinterOutput() {
}

void ATPrinterOutput::SetOnInvalidation(vdfunction<void()> fn) {
	mpOnInvalidationFn = std::move(fn);
}

void ATPrinterOutput::Revalidate() {
	mbInvalidated = false;
}

size_t ATPrinterOutput::GetLength() const {
	return mText.size();
}

const wchar_t *ATPrinterOutput::GetTextPointer(size_t offset) const {
	return mText.c_str() + offset;
}

void ATPrinterOutput::Clear() {
	mText.clear();
	mColumn = 0;
}

void *ATPrinterOutput::AsInterface(uint32 id) {
	if (id == IATPrinterOutput::kTypeID)
		return static_cast<IATPrinterOutput *>(this);

	return ATPrinterOutputBase::AsInterface(id);
}

bool ATPrinterOutput::WantUnicode() const {
	return true;
}

void ATPrinterOutput::WriteRaw(const uint8 *buf, size_t len) {
	if (!len)
		return;

	const size_t startPos = mText.size();

	if (startPos >= kMaxTextLength)
		return;

	while(len--) {
		uint8 c = *buf++;

		if (c != 0x0A && c != 0x0D) {
			c &= 0x7f;
			
			if (c < 0x20 || c > 0x7F)
				c = '?';
		}

		if (mDropNextChar) {
			if (mDropNextChar == c) {
				mDropNextChar = 0;

				continue;
			}

			mDropNextChar = 0;
		}

		if (c == 0x0D || c == 0x0A) {
			mDropNextChar = c ^ (uint8)(0x0D ^ 0x0A);
			mText.push_back(0x0A);
			mColumn = 0;
		} else {
			mText.push_back(c);
			++mColumn;

			if (mColumn >= 132) {
				c = 0x0A;
				mText.push_back(c);
				mColumn = 0;
			}
		}
	}

	if (!mbInvalidated && mText.size() != startPos) {
		mbInvalidated = true;

		if (mpOnInvalidationFn)
			mpOnInvalidationFn();
	}
}

void ATPrinterOutput::WriteUnicode(const wchar_t *buf, size_t len) {
	if (!len)
		return;

	const size_t startPos = mText.size();

	while(len--) {
		wchar_t ch = *buf++;

		if (!ch)
			continue;

		mText.push_back(ch);

		if (ch == '\n')
			mColumn = 0;
		else if (++mColumn >= 132) {
			mColumn = 0;
			ch = '\n';
			mText.push_back(ch);
		}
	}

	if (!mbInvalidated && mText.size() != startPos) {
		mbInvalidated = true;

		if (mpOnInvalidationFn)
			mpOnInvalidationFn();
	}
}

////////////////////////////////////////////////////////////////////////////////

void ATPrinterGraphicalOutput::VectorQueryRect::Init(const vdrect32f& r, float dotRadius) {
	mXC = r.left + r.right;
	mYC = r.top + r.bottom;
	mXD = r.right - r.left + 2*dotRadius;
	mYD = r.bottom - r.top + 2*dotRadius;
}

void ATPrinterGraphicalOutput::VectorQueryRect::Translate(float dx, float dy) {
	mXC += dx * 2;
	mYC += dy * 2;
}

bool ATPrinterGraphicalOutput::VectorQueryRect::Intersects(const Vector& v) const {
#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
	__m128 vxy1 = _mm_castpd_ps(_mm_load_sd((const double *)&v.mX1));
	__m128 vxy2 = _mm_castpd_ps(_mm_load_sd((const double *)&v.mX2));
	__m128 qxc = _mm_castpd_ps(_mm_load_sd((const double *)&mXC));
	__m128 qxd = _mm_castpd_ps(_mm_load_sd((const double *)&mXD));
	__m128 absMask = _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF));

	__m128 xydiff = _mm_and_ps(_mm_sub_ps(_mm_add_ps(vxy1, vxy2), qxc), absMask);
	__m128 xyrange = _mm_add_ps(_mm_and_ps(_mm_sub_ps(vxy1, vxy2), absMask), qxd);

	return (_mm_movemask_ps(_mm_cmpge_ps(xydiff, xyrange)) & 3) == 0;
#else
	// AABB-AABB intersection test. For now we skip the complexity of a
	// more accurate AABB-OBB test. Note that the two points are the endpoints
	// of a line segment and not corners of a rectangle, so we cannot rely on
	// any particular orientation of the two points.
	const float xc = v.mX1 + v.mX2;
	const float yc = v.mY1 + v.mY2;
	const float xd = fabsf(v.mX1 - v.mX2);
	const float yd = fabsf(v.mY1 - v.mY2);

	if (fabsf(xc - mXC) >= xd + mXD || fabsf(yc - mYC) >= yd + mYD)
		return false;

	return true;
#endif
}

bool ATPrinterGraphicalOutput::VectorQueryRect::IntersectsPrecise(const Vector& v) const {
	// AABB-OBB intersection test. More precise test used during insertion in
	// order to reduce the number of false intersections during extraction,
	// which is more performance intensive.
	//
	// The OBB test introduces two additional axes to test against, the line
	// segment direction and its normal. The query rect is padded by the pen
	// width instead of the vector, so the OBB is infinitely thin along the
	// normal axis.

	const float xc = v.mX1 + v.mX2;
	const float yc = v.mY1 + v.mY2;
	const float xd = v.mX1 - v.mX2;
	const float yd = v.mY1 - v.mY2;
	const float axd = fabsf(xd);
	const float ayd = fabsf(yd);
	const float dxc = xc - mXC;
	const float dyc = yc - mYC;
	const float sxd = axd + mXD;
	const float syd = ayd + mYD;

	// test AABB normals
	if (fabsf(dxc) >= sxd || fabsf(dyc) >= syd)
		return false;

	// Test OBB normals.
	//
	// We use > instead of >= to avoid an ugly scenario with zero-length vectors
	// that would ordinarily cause a false reject.
	const float d1 = sxd * axd + syd * ayd;
	const float d2 = mXD * ayd + mYD * axd;

	if (fabsf(dxc * xd + dyc * yd) > d1)
		return false;
	
	if (fabsf(dxc * yd - dyc * xd) > d2)
		return false;

	return true;
}

////////////////////////////////////////////////////////////////////////////////

ATPrinterGraphicalOutput::ATPrinterGraphicalOutput(ATPrinterOutputManager& parent, const wchar_t *name, const ATPrinterGraphicsSpec& spec)
	: ATPrinterOutputBase(parent, name)
	, mGraphicsSpec(spec)
{
	mPageWidthMM = spec.mPageWidthMM;
	mPageVBorderMM = spec.mPageVBorderMM;
	mDotRadiusMM = spec.mDotRadiusMM;

	mHeadFirstBitOffsetY = spec.mDotRadiusMM;
	if (!spec.mbBit0Top)
		mHeadFirstBitOffsetY += spec.mVerticalDotPitchMM * (spec.mNumPins - 1);

	mDotStepY = spec.mbBit0Top ? spec.mVerticalDotPitchMM : -spec.mVerticalDotPitchMM;
	mHeadWidth = spec.mDotRadiusMM * 2;
	mHeadHeight = spec.mDotRadiusMM * 2 + spec.mVerticalDotPitchMM * (spec.mNumPins - 1);
	mHeadPinCount = spec.mNumPins;

	Clear();
}

ATPrinterGraphicalOutput::~ATPrinterGraphicalOutput() {
}

const ATPrinterGraphicsSpec& ATPrinterGraphicalOutput::GetGraphicsSpec() const {
	return mGraphicsSpec;
}

vdrect32f ATPrinterGraphicalOutput::GetDocumentBounds() const {
	vdrect32f bounds(0, 0, 0, 0);

	if (!mLines.empty()) {
		bounds.left = 0;
		bounds.right = mPageWidthMM;
		bounds.top = 0;
		bounds.bottom = mLines.back().mY + mHeadHeight + mPageVBorderMM;
	}

	if (!mVectors.empty()) {
		if (bounds.empty()) {
			bounds.left = 0;
			bounds.right = mPageWidthMM;

			bounds.top = 1e+30f;
			bounds.bottom = -1e+30f;
		}

		for(const Vector& vec : mVectors) {
			bounds.top = std::min<float>(bounds.top, vec.mY1 - mDotRadiusMM);
			bounds.top = std::min<float>(bounds.top, vec.mY2 - mDotRadiusMM);
			bounds.bottom = std::max<float>(bounds.bottom, vec.mY1 + mDotRadiusMM);
			bounds.bottom = std::max<float>(bounds.bottom, vec.mY2 + mDotRadiusMM);
		}
	}

	return bounds;
}

bool ATPrinterGraphicalOutput::HasVectors() const {
	return !mVectors.empty();
}

void ATPrinterGraphicalOutput::Clear() {
	mHeadY = mPageVBorderMM + mHeadFirstBitOffsetY;
	mpCurrentLine = nullptr;

	if (mpOnVerticalMove)
		mpOnVerticalMove(mHeadY);

	mLines.clear();
	mColumns.clear();

	mVectors.clear();
	mVectorTiles.clear();
	mVectorTileHashTable.clear();
	mVectorSlotHashSize = 0;
	mVectorSlotLoadLimit = 0;
	mVectorSlotsUsed = 0;

	mbInvalidatedAll = true;

	if (!mbInvalidated) {
		mbInvalidated = true;

		if (mpOnInvalidationFn)
			mpOnInvalidationFn();
	}

	if (mpOnClear)
		mpOnClear();
}

void ATPrinterGraphicalOutput::SetOnInvalidation(vdfunction<void()> fn) {
	mpOnInvalidationFn = std::move(fn);
}

bool ATPrinterGraphicalOutput::ExtractInvalidationRect(bool& all, vdrect32f& r) {
	all = false;
	r = {};

	if (!mbInvalidated)
		return false;

	mbInvalidated = false;

	if (mbInvalidatedAll) {
		mbInvalidatedAll = false;
		all = true;
	} else {
		r = mInvalidationRect;
	}

	return true;
}

double ATPrinterGraphicalOutput::GetVerticalPos() const {
	return mHeadY;
}

void ATPrinterGraphicalOutput::SetVerticalPos(double pos) {
	if (mHeadY != pos) {
		mHeadY = pos;

		mpCurrentLine = nullptr;
	}
}

void ATPrinterGraphicalOutput::SetOnVerticalMove(vdfunction<void(float y)> fn) {
	mpOnVerticalMove = std::move(fn);
}

bool ATPrinterGraphicalOutput::PreCull(CullInfo& cullInfo, const vdrect32f& r) const {
	auto itBegin = mLines.begin();
	auto itLine1 = std::lower_bound(itBegin, mLines.end(), r.top - mHeadHeight, LineCompareY());
	auto itLine2 = std::lower_bound(itLine1, mLines.end(), r.bottom, LineCompareY());

	cullInfo.mLineStart = (size_t)(itLine1 - itBegin);
	cullInfo.mLineEnd = (size_t)(itLine2 - itBegin);

	return cullInfo.mLineEnd > cullInfo.mLineStart;
}

void ATPrinterGraphicalOutput::ExtractNextLineDots(vdfastvector<RenderDot>& renderDots, CullInfo& cullInfo, const vdrect32f& r) const {
	const auto itLine1 = mLines.begin() + cullInfo.mLineStart;
	const auto itLine2 = mLines.begin() + cullInfo.mLineEnd;

	const auto itRowLine1 = std::lower_bound(itLine1, itLine2, r.top - mHeadHeight, LineCompareY());
	const auto itRowLine2 = std::lower_bound(itRowLine1, itLine2, r.bottom, LineCompareY());

	const float dotRadius = mDotRadiusMM;
	const float docRowY1 = r.top;
	const float docRowY2 = r.bottom;
	const float viewDocXC = (r.right + r.left) * 0.5f;
	const float viewDocXD = (r.right - r.left) * 0.5f;

	for(auto itLine = itRowLine1; itLine != itRowLine2; ++itLine) {
		const uint32 columnIdx = itLine->mColumnStart;
		const float firstDotY = itLine->mY + mHeadFirstBitOffsetY;
			
		// pre-coll pins on the print head -- the tricky part is that the head may be upside-down
		uint32 dotMask = (UINT32_C(2) << (this->mHeadPinCount - 1)) - 1;

		if (mDotStepY > 0) {
			// check if first dot is entirely above the clip rect
			const float firstDotY2 = firstDotY + dotRadius;
			if (firstDotY2 <= docRowY1) {
				// compute how many dots to trim off
				int topDotsToCull = 1 + (int)((docRowY1 - firstDotY2) / mDotStepY);

				if (topDotsToCull >= mHeadPinCount)
					dotMask = 0;
				else
					dotMask &= dotMask << topDotsToCull;
			}

			// check if the last dot is entirely below the clip rect
			const float lastDotY1 = firstDotY + mDotStepY * (float)(mHeadPinCount - 1) - dotRadius;
			if (lastDotY1 >= docRowY2) {
				// compute how many dots to trim off
				int bottomDotsToCull = 1 + (int)((lastDotY1 - docRowY2) / mDotStepY);

				if (bottomDotsToCull >= mHeadPinCount)
					dotMask = 0;
				else
					dotMask &= dotMask >> bottomDotsToCull;
			}
		} else if (mDotStepY < 0) {
			// check if first dot is entirely below the clip rect
			const float firstDotY1 = firstDotY - dotRadius;
			if (firstDotY1 >= docRowY2) {
				// compute how many dots to trim off
				int bottomDotsToCull = 1 + (int)((firstDotY1 - docRowY2) / mDotStepY);

				if (bottomDotsToCull >= mHeadPinCount)
					dotMask = 0;
				else
					dotMask >>= bottomDotsToCull;
			}

			// check if the last dot is entirely above the clip rect
			const float lastDotY2 = firstDotY + mDotStepY * (float)(mHeadPinCount - 1) + dotRadius;
			if (lastDotY2 <= docRowY1) {
				// compute how many dots to trim off
				int topDotsToCull = 1 + (int)((docRowY1 - lastDotY2) / mDotStepY);

				if (topDotsToCull >= mHeadPinCount)
					dotMask = 0;
				else
					dotMask &= dotMask << topDotsToCull;
			}
		}

		if (!dotMask)
			continue;
			
		for(const PrintColumn& column : vdvector_view(mColumns.data() + columnIdx, itLine->mColumnCount)) {
			if (fabsf(column.mX - viewDocXC) >= viewDocXD + dotRadius)
				continue;

			uint32 pins = column.mDots & dotMask;

			while(pins) {
				int idx = VDFindLowestSetBitFast(pins);
				pins &= (pins - 1);

				const float dotY = firstDotY + mDotStepY * (float)idx;

				renderDots.push_back(
					RenderDot {
						.mX = column.mX,
						.mY = dotY,
					}
				);
			}
		}
	}
}

bool ATPrinterGraphicalOutput::ExtractNextLine(vdfastvector<RenderColumn>& renderColumns, float& renderY, CullInfo& cullInfo, const vdrect32f& r) const {
	renderY = 0;
	renderColumns.clear();

	const float cullx1 = r.left - mDotRadiusMM * 2;
	const float cullx2 = r.right;

	while(cullInfo.mLineStart < cullInfo.mLineEnd) {
		const Line& line = mLines[cullInfo.mLineStart++];
		if (!line.mColumnCount)
			continue;

		renderY = line.mY;

		for(const PrintColumn& col : vdspan(&mColumns[line.mColumnStart], line.mColumnCount)) {
			if (col.mX >= cullx1 && col.mX <= cullx2)
				renderColumns.emplace_back(col.mX, col.mDots);
		}

		if (!renderColumns.empty())
			return true;
	}

	return false;
}

void ATPrinterGraphicalOutput::ExtractVectors(vdfastvector<RenderVector>& renderVectors, const vdrect32f& r) {
	// convert rect to tile rect
	vdrect32 tileRect = GetVectorTileRect(r);

	// compute query rect
	VectorQueryRect vq;
	vq.Init(r, mDotRadiusMM);

	std::fill(mVectorBitSet.begin(), mVectorBitSet.end(), 0);
	mVectorBitSet.resize((mVectors.size() + 31) >> 5, 0);

	// iterate over tiles
	auto *VDRESTRICT bitSetView = mVectorBitSet.data();
	const vdspan vectors(mVectors);

	for(sint32 tileY = tileRect.top; tileY < tileRect.bottom; ++tileY) {
		for(sint32 tileX = tileRect.left; tileX < tileRect.right; ++tileX) {
			auto [tileSlot, found] = FindVectorTile(tileX, tileY);

			if (!found)
				continue;

			uint32 tileId = mVectorTileHashTable[tileSlot].mFirstTile;
			while(tileId) {
				const VectorTile& tile = mVectorTiles[tileId - 1];

				for(uint32 vectorId : tile.mVectorIndices) {
					if (!vectorId)
						break;

					const uint32 bitSetIdx = (vectorId - 1) >> 5;
					const uint32 bitSetBit = UINT32_C(1) << ((vectorId - 1) & 31);

					if (bitSetView[bitSetIdx] & bitSetBit)
						continue;

					bitSetView[bitSetIdx] |= bitSetBit;

					const Vector& v = vectors[vectorId - 1];

					if (vq.Intersects(v))
						renderVectors.push_back(v);
				}

				tileId = tile.mNextTile;
			}
		}
	}
}

void *ATPrinterGraphicalOutput::AsInterface(uint32 id) {
	if (id == IATPrinterGraphicalOutput::kTypeID)
		return static_cast<IATPrinterGraphicalOutput *>(this);

	return ATPrinterOutputBase::AsInterface(id);
}

void ATPrinterGraphicalOutput::SetOnClear(vdfunction<void()> fn) {
	mpOnClear = std::move(fn);
}

void ATPrinterGraphicalOutput::FeedPaper(float distanceMM) {
	if (distanceMM != 0) {
		mHeadY += distanceMM;
		mpCurrentLine = nullptr;

		if (mpOnVerticalMove)
			mpOnVerticalMove(mHeadY);
	}
}

void ATPrinterGraphicalOutput::Print(float x, uint32 pins) {
	// if no dots, ignore
	if (!pins)
		return;

	// if no line, establish now
	if (!mpCurrentLine) {
		Line newLine {};
		newLine.mY = mHeadY - mDotRadiusMM;
		newLine.mColumnStart = (uint32)mColumns.size();
		newLine.mColumnCount = 0;

		auto itLine = std::lower_bound(
			mLines.begin(),
			mLines.end(), 
			newLine,
			[](const Line& a, const Line& b) {
				return a.mY < b.mY;
			}
		);

		mpCurrentLine = &*mLines.insert(itLine, newLine);
	}

	auto& column = mColumns.emplace_back();
	column.mX = x;
	column.mDots = pins;

	++mpCurrentLine->mColumnCount;

	if (!mbInvalidatedAll) {
		vdrect32f r;
		r.left = x - mDotRadiusMM;
		r.top = mHeadY - mDotRadiusMM;
		r.right = r.left + mHeadWidth;
		r.bottom = r.top + mHeadHeight;

		Invalidate(r);
	}
}

void ATPrinterGraphicalOutput::AddVector(const vdfloat2& pt1orig, const vdfloat2& pt2orig, uint32 color) {
	// bias vectors by current vertical position
	vdfloat2 pt1 = pt1orig + vdfloat2{0, mHeadY};
	vdfloat2 pt2 = pt2orig + vdfloat2{0, mHeadY};

	// add vector, reorienting to top down
	Vector& v = mVectors.emplace_back();
	
	if (pt1.y <= pt2.y) {
		v.mX1 = pt1.x;
		v.mY1 = pt1.y;
		v.mX2 = pt2.x;
		v.mY2 = pt2.y;
	} else {
		v.mX1 = pt2.x;
		v.mY1 = pt2.y;
		v.mX2 = pt1.x;
		v.mY2 = pt1.y;
	}

	v.mLinearColor = color;

	const uint32 vectorId = (uint32)mVectors.size();

	// compute bounding box for the vector
	vdrect32f rvec = GetVectorBoundingBox(v);

	// compute to tile coordinates
	auto [tileX1, tileY1, tileX2, tileY2] = GetVectorTileRect(rvec);

	// do tile scan
	const float x0 = (float)tileX1 * kVectorTileSize;
	const float y0 = (float)tileY1 * kVectorTileSize;
	VectorQueryRect q;
	q.Init(vdrect32f(x0, y0, x0 + kVectorTileSize, y0 + kVectorTileSize), mDotRadiusMM);

	for(sint32 tileY = tileY1; tileY < tileY2; ++tileY) {
		VectorQueryRect q2 = q;
		for(sint32 tileX = tileX1; tileX < tileX2; ++tileX) {
			if (q2.IntersectsPrecise(v)) {
				AddVectorToTile(tileX, tileY, vectorId);
			}

			q2.Translate(kVectorTileSize, 0);
		}

		q.Translate(0, kVectorTileSize);
	}

	Invalidate(
		vdrect32f {
			std::min<float>(pt1.x, pt2.x) - mDotRadiusMM,
			std::min<float>(pt1.y, pt2.y) - mDotRadiusMM,
			std::max<float>(pt1.x, pt2.x) + mDotRadiusMM,
			std::max<float>(pt1.y, pt2.y) + mDotRadiusMM
		}
	);
	
	// adjust head vertical position
	if (mHeadY != pt2.y) {
		mHeadY = pt2.y;

		if (mpOnVerticalMove)
			mpOnVerticalMove(mHeadY);
	}
}

uint32 ATPrinterGraphicalOutput::ConvertColor(uint32 srgb) const {
	return nsVDVecMath::packus8(vdfloat32x3(VDColorRGB::FromBGR8(srgb).SRGBToLinear()) * 64.0f) & 0xFFFFFF;
}

size_t ATPrinterGraphicalOutput::HashVectorTile(sint32 tileX, sint32 tileY) const {
	uint32 tx = (uint32)tileX;
	uint32 ty = (uint32)tileY;

	uint32 frac = tx * mVectorSlotHashF1 + ty * mVectorSlotHashF2;

	return (uint32)(((uint64)frac * mVectorSlotHashSize) >> 32);
}

std::pair<size_t, bool> ATPrinterGraphicalOutput::FindVectorTile(sint32 tileX, sint32 tileY) const {
	if (mVectorTileHashTable.empty())
		return { 0, false };
	size_t idx = HashVectorTile(tileX, tileY);
	size_t n1 = std::min<size_t>(mVectorSlotLoadLimit, mVectorSlotHashSize - idx);
	size_t n2 = mVectorSlotHashSize - n1;

	const VectorTileSlot *slot = &mVectorTileHashTable[idx];
	for(size_t i = 0; i < n1; ++i, ++slot) {
		if (!slot->mFirstTile)
			return { idx + i, false };

		if (slot->mTileX == tileX && slot->mTileY == tileY)
			return { idx + i, true };
	}

	slot = mVectorTileHashTable.data();
	for(size_t i = 0; i < n1; ++i, ++slot) {
		if (!slot->mFirstTile)
			return { idx + i, false };

		if (slot->mTileX == tileX && slot->mTileY == tileY)
			return { idx + i, true };
	}

	return { n2, false };
}

void ATPrinterGraphicalOutput::AddVectorToTile(sint32 tileX, sint32 tileY, uint32 vectorId) {
	for(;;) {
		auto [tileSlot, found] = FindVectorTile(tileX, tileY);

		if (found) {
			VectorTileSlot& slot = mVectorTileHashTable[tileSlot];
			uint32 tileId = slot.mFirstTile;

			VectorTile& tile = mVectorTiles[tileId - 1];

			if (tile.mVectorIndices[vdcountof(tile.mVectorIndices) - 1]) {
				VectorTile& tile2 = mVectorTiles.emplace_back();
				tile2.mNextTile = tileId;
				tile2.mVectorIndices[0] = vectorId;

				slot.mFirstTile = (uint32)mVectorTiles.size();
			} else {
				int i = vdcountof(tile.mVectorIndices) - 1;
				while(!tile.mVectorIndices[i - 1])
					--i;

				tile.mVectorIndices[i] = vectorId;
			}
			return;
		}

		if (mVectorSlotsUsed >= mVectorSlotLoadLimit) {
			RehashVectorTileTable();
			continue;
		}

		++mVectorSlotsUsed;

		auto& tile = mVectorTiles.emplace_back();
		tile.mVectorIndices[0] = vectorId;

		auto& slot = mVectorTileHashTable[tileSlot];
		slot.mTileX = tileX;
		slot.mTileY = tileY;
		slot.mFirstTile = (uint32)mVectorTiles.size();
		return;
	}
}

void ATPrinterGraphicalOutput::RehashVectorTileTable() {
	// We need to ensure that the hash table is at least the size of the Y scaling
	// factor to ensure that the Y scaling factor fits within 32 bits.
	size_t newSize = VDComputePrimeBucketCount(std::max<size_t>((mVectorSlotsUsed * kInvLoadFactor * 3) / 2, 133));

	vdvector<VectorTileSlot> oldHashTable;
	mVectorTileHashTable.swap(oldHashTable);
	mVectorTileHashTable.resize(newSize);

	mVectorSlotHashF1 = (uint32)((UINT64_C(0xFFFFFFFF) + newSize) / newSize);
	mVectorSlotHashF2 = (uint32)((((uint64)107 << 32) - 1 + newSize) / newSize);
	mVectorSlotHashSize = newSize;
	mVectorSlotLoadLimit = newSize / kInvLoadFactor;

	for(const VectorTileSlot& slot : oldHashTable) {
		if (slot.mFirstTile) {
			size_t hc = HashVectorTile(slot.mTileX, slot.mTileY);
			
			for(;;) {
				if (!mVectorTileHashTable[hc].mFirstTile)
					break;

				if (++hc >= newSize)
					hc = 0;
			}

			mVectorTileHashTable[hc] = slot;
		}
	}
}

vdrect32f ATPrinterGraphicalOutput::GetVectorBoundingBox(const Vector& v) const {
	return vdrect32f {
		std::min<float>(v.mX1, v.mX2) - mDotRadiusMM,
		std::min<float>(v.mY1, v.mY2) - mDotRadiusMM,
		std::max<float>(v.mX1, v.mX2) + mDotRadiusMM,
		std::max<float>(v.mY1, v.mY2) + mDotRadiusMM
	};
}

vdrect32 ATPrinterGraphicalOutput::GetVectorTileRect(const vdrect32f& r) const {
	return vdrect32 {
		VDFloorToInt(r.left   * kInvVectorTileSize),
		VDFloorToInt(r.top    * kInvVectorTileSize),
		VDCeilToInt (r.right  * kInvVectorTileSize),
		VDCeilToInt (r.bottom * kInvVectorTileSize)
	};
}

void ATPrinterGraphicalOutput::Invalidate(const vdrect32f& r) {
	if (!mbInvalidated) {
		mbInvalidated = true;

		mInvalidationRect = r;

		if (mpOnInvalidationFn)
			mpOnInvalidationFn();
	} else {
		mInvalidationRect.left = std::min(mInvalidationRect.left, r.left);
		mInvalidationRect.top = std::min(mInvalidationRect.top, r.top);
		mInvalidationRect.right = std::max(mInvalidationRect.right, r.right);
		mInvalidationRect.bottom = std::max(mInvalidationRect.bottom, r.bottom);
	}
}

////////////////////////////////////////////////////////////////////////////////

ATPrinterOutputManager::ATPrinterOutputManager() {
}

ATPrinterOutputManager::~ATPrinterOutputManager() {
}

uint32 ATPrinterOutputManager::GetOutputCount() const {
	return (uint32)mOutputs.size();
}

uint32 ATPrinterOutputManager::GetGraphicalOutputCount() const {
	return (uint32)mGraphicalOutputs.size();
}

ATPrinterOutput& ATPrinterOutputManager::GetOutput(uint32 idx) const {
	return *mOutputs[idx];
}

ATPrinterGraphicalOutput& ATPrinterOutputManager::GetGraphicalOutput(uint32 idx) const {
	return *mGraphicalOutputs[idx];
}

vdrefptr<IATPrinterOutput> ATPrinterOutputManager::CreatePrinterOutput(const wchar_t *name) {
	vdrefptr<ATPrinterOutput> output(new ATPrinterOutput(*this, name));

	mOutputs.push_back(output);
	OnAddedOutput.InvokeAll(*output);

	return output;
}

vdrefptr<IATPrinterGraphicalOutput> ATPrinterOutputManager::CreatePrinterGraphicalOutput(const wchar_t *name, const ATPrinterGraphicsSpec& spec) {
	vdrefptr<ATPrinterGraphicalOutput> output(new ATPrinterGraphicalOutput(*this, name, spec));

	mGraphicalOutputs.push_back(output);
	OnAddedGraphicalOutput.InvokeAll(*output);

	return output;
}

void ATPrinterOutputManager::OnDestroyingOutput(ATPrinterOutputBase& output) {
	IATPrinterOutput *outputInterface = vdpoly_cast<IATPrinterOutput *>(&output);
	if (outputInterface) {
		ATPrinterOutput& output = *static_cast<ATPrinterOutput *>(outputInterface);
		auto it = std::find(mOutputs.begin(), mOutputs.end(), &output);

		if (it == mOutputs.end())
			VDRaiseInternalFailure();

		mOutputs.erase(it);

		OnRemovingOutput.InvokeAll(output);
	}

	IATPrinterGraphicalOutput *graphicalOutputInterface = vdpoly_cast<IATPrinterGraphicalOutput *>(&output);
	if (graphicalOutputInterface) {
		ATPrinterGraphicalOutput& goutput = *static_cast<ATPrinterGraphicalOutput *>(graphicalOutputInterface);

		auto it = std::find(mGraphicalOutputs.begin(), mGraphicalOutputs.end(), &goutput);

		if (it == mGraphicalOutputs.end())
			VDRaiseInternalFailure();

		mGraphicalOutputs.erase(it);

		OnRemovingGraphicalOutput.InvokeAll(goutput);
	}
}

////////////////////////////////////////////////////////////////////////////////

vdrefptr<IATPrinterOutputManager> ATCreatePrinterOutputManager() {
	return vdrefptr<IATPrinterOutputManager>(new ATPrinterOutputManager);
}
