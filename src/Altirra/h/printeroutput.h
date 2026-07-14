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

#ifndef f_AT_PRINTEROUTPUT_H
#define f_AT_PRINTEROUTPUT_H

#include <vd2/system/atomic.h>
#include <vd2/system/function.h>
#include <vd2/system/VDString.h>
#include <vd2/system/vectors.h>
#include <at/atcore/deviceprinter.h>
#include <at/atcore/notifylist.h>

class ATPrinterOutputManager;

class ATPrinterOutputBase : public virtual IVDRefUnknown {
public:
	ATPrinterOutputBase(ATPrinterOutputManager& parent, const wchar_t *name)
		: mParent(parent), mName(name)
	{
	}

	virtual ~ATPrinterOutputBase() = default;

	int AddRef() override;
	int Release() override;
	void *AsInterface(uint32 id) override;

	const wchar_t *GetName() const;

protected:
	ATPrinterOutputManager& mParent;
	VDStringW mName;
	VDAtomicInt mRefCount{0};
};

// Printer output interface for text output. This gets presented in the
// printer view as a text edit widget.
//
class ATPrinterOutput final : public ATPrinterOutputBase, public IATPrinterOutput {
	ATPrinterOutput(const ATPrinterOutput&) = delete;
	ATPrinterOutput& operator=(const ATPrinterOutput&) = delete;
public:
	ATPrinterOutput(ATPrinterOutputManager& parent, const wchar_t *name);
	~ATPrinterOutput();

	void SetOnInvalidation(vdfunction<void()> fn);
	void Revalidate();

	size_t GetLength() const;
	const wchar_t *GetTextPointer(size_t offset) const;

	void Clear();

public:
	void *AsInterface(uint32 id) override;

	bool WantUnicode() const override;
	void WriteRaw(const uint8 *buf, size_t len) override;
	void WriteUnicode(const wchar_t *buf, size_t len) override;

private:
	static constexpr size_t kMaxTextLength = 0x7F000000;

	VDStringW mText;
	uint32 mColumn = 0;
	uint8 mDropNextChar = 0;
	bool mbInvalidated = false;

	vdfunction<void()> mpOnInvalidationFn;
};

// Printer output handler for graphical output, produced by dot matrix patterns.
// This gets presented in the printer view as a rasterized image.
//
// The paper coordinate system is top-down with (0,0) at the top left of the
// paper and all coordinates in millimeters. The paper width is specified when
// the output is opened and the paper height dynamically extends as content
// is printed.
//
// There are two kinds of entities that can be present in the output. One is
// dots or filled discs, and the other is vectors. A vector is a line segment
// with a width the same as the dot diameter, capped by two dots at the
// end points. Dots are internally organized by horizontal lines corresponding
// to sweeps of the print head, and are culled accordingly; vectors are
// free-form and extracted by bounding rects.
//
class ATPrinterGraphicalOutput final : public ATPrinterOutputBase, public IATPrinterGraphicalOutput {
	ATPrinterGraphicalOutput(const ATPrinterGraphicalOutput&) = delete;
	ATPrinterGraphicalOutput& operator=(const ATPrinterGraphicalOutput&) = delete;
public:
	ATPrinterGraphicalOutput(ATPrinterOutputManager& parent, const wchar_t *name, const ATPrinterGraphicsSpec& spec);
	~ATPrinterGraphicalOutput();

	const ATPrinterGraphicsSpec& GetGraphicsSpec() const;
	vdrect32f GetDocumentBounds() const;

	bool HasVectors() const;

	void Clear();

	void SetOnInvalidation(vdfunction<void()> fn);
	bool ExtractInvalidationRect(bool& all, vdrect32f& r);

	uint32 GetPenColor() const;
	void SetOnPenChange(vdfunction<void(uint32 color)> fn);

	double GetHorizontalPos() const;
	double GetVerticalPos() const;

	void SetVerticalPos(double pos);
	void SetOnHorizontalMove(vdfunction<void(float x)> fn);
	void SetOnVerticalMove(vdfunction<void(float y)> fn);

	// Convert a linear color from the printer output layer to an sRGB output
	// color.
	uint32 ConvertLinearColorToSrgb(uint32 c) const;

	struct CullInfo {
		size_t mLineStart;
		size_t mLineEnd;
	};

	// Pre-cull to a given rectangle for line extraction. This extracts a range of lines which is then used
	// to speed up ExtractNextLine/ExtractNextLineDots().
	bool PreCull(CullInfo& cullInfo, const vdrect32f& r) const;

	struct RenderDot {
		float mX = 0;
		float mY = 0;
		uint32 mLinearColor = 0;

		bool operator==(const RenderDot& other) const = default;
	};

	// Extract dots from a line within the pre-cull rect. The top of the rectangle must be at or below
	// the top height of the last rectangle.
	void ExtractNextLineDots(vdfastvector<RenderDot>& renderDots, CullInfo& cullInfo, const vdrect32f& r) const;

	struct RenderColumn {
		float mX;
		uint32 mPins;
	};

	// Extract columns from a line within the pre-cull rect. The top of the rectangle must be at or below
	// the top height of the last rectangle.
	bool ExtractNextLine(vdfastvector<RenderColumn>& renderColumns, float& renderY, CullInfo& cullInfo, const vdrect32f& r) const;

	// Vector line. This is always oriented top-down (y2 > y1).
	struct RenderVector {
		float mX1 = 0;
		float mY1 = 0;
		float mX2 = 0;
		float mY2 = 0;
		uint32 mLinearColor = 0;
	};

	// Extract vectors that may intersect the given rectangle (some extras may be returned due to imperfect
	// culling).
	void ExtractVectors(vdfastvector<RenderVector>& renderVectors, const vdrect32f& r);

public:
	void *AsInterface(uint32 id) override;

	void SetOnClear(vdfunction<void()> fn) override;
	
	void FeedPaper(double distanceMM) override;
	void Print(double x, uint32 dots) override;
	void MoveVector(const vddouble2& pt) override;
	void AddVector(const vddouble2& pt1, const vddouble2& pt2, uint32 color) override;
	void ChangePenColor(uint32 color) override;
	uint32 ConvertColor(uint32 srgb) const override;

private:
	struct Line {
		float mY = 0;
		uint32 mColumnStart = 0;
		uint32 mColumnCount = 0;
	};

	struct LineCompareY {
		bool operator()(const Line& line, float y) {
			return line.mY < y;
		}
	};

	struct PrintColumn {
		float mX = 0;
		uint32 mDots = 0;
	};

	// 1cm x 1cm tiles
	static constexpr float kVectorTileSize = 10.0f;
	static constexpr float kInvVectorTileSize = 1.0f / kVectorTileSize;
	static constexpr int kInvLoadFactor = 5;

	struct VectorTileSlot {
		sint32 mTileX = 0;
		sint32 mTileY = 0;
		uint32 mFirstTile;
	};

	struct VectorTile {
		uint32 mNextTile = 0;
		uint32 mVectorIndices[15] {};
	};

	using Vector = RenderVector;

	class VectorQueryRect {
	public:
		void Init(const vdrect32f& r, float dotRadius);
		void Translate(float dx, float dy);
		bool Intersects(const Vector& v) const;
		bool IntersectsPrecise(const Vector& v) const;

	private:
		float mXC = 0;
		float mYC = 0;
		float mXD = 0;
		float mYD = 0;
	};

	size_t HashVectorTile(sint32 tileX, sint32 tileY) const;
	std::pair<size_t, bool> FindVectorTile(sint32 tileX, sint32 tileY) const;
	void AddVectorToTile(sint32 tileX, sint32 tileY, uint32 vectorId);
	void RehashVectorTileTable();
	vdrect32f GetVectorBoundingBox(const Vector& v) const;
	vdrect32 GetVectorTileRect(const vdrect32f& v) const;
	void Invalidate(const vdrect32f& r);

	const float mPageWidthMM = 0;
	const float mPageVBorderMM = 0;
	const float mDotRadiusMM = 0;
	const float mHeadFirstBitOffsetY = 0;
	const float mDotStepY = 0;
	const float mHeadWidth = 0;
	const float mHeadHeight = 0;
	const int mHeadPinCount = 0;

	double mHeadX = 0;
	double mHeadY = 0;
	uint32 mPenColor = 0;

	Line *mpCurrentLine = nullptr;
	vdfastvector<Line> mLines;
	vdfastvector<PrintColumn> mColumns;

	vdvector<VectorTileSlot> mVectorTileHashTable;
	size_t mVectorSlotHashSize = 0;
	size_t mVectorSlotsUsed = 0;
	size_t mVectorSlotLoadLimit = 0;
	uint32 mVectorSlotHashF1 = 0;
	uint32 mVectorSlotHashF2 = 0;

	vdvector<VectorTile> mVectorTiles;
	vdfastvector<Vector> mVectors;
	vdfastvector<uint32> mVectorBitSet;

	bool mbInvalidated = false;
	bool mbInvalidatedAll = false;
	vdrect32f mInvalidationRect;
	vdfunction<void()> mpOnInvalidationFn;
	vdfunction<void(uint32, uint32)> mpOnPenChangedFn;

	vdfunction<void()> mpOnClear;
	vdfunction<void(uint32 c)> mpOnPenChange;
	vdfunction<void(float x)> mpOnHorizontalMove;
	vdfunction<void(float y)> mpOnVerticalMove;

	const ATPrinterGraphicsSpec mGraphicsSpec;
};

class ATPrinterOutputManager final : public vdrefcounted<IATPrinterOutputManager> {
	ATPrinterOutputManager(const ATPrinterOutputManager&);
	ATPrinterOutputManager& operator=(const ATPrinterOutputManager&);
public:
	ATPrinterOutputManager();
	~ATPrinterOutputManager();

	uint32 GetOutputCount() const;
	uint32 GetGraphicalOutputCount() const;

	ATPrinterOutput& GetOutput(uint32 idx) const;
	ATPrinterGraphicalOutput& GetGraphicalOutput(uint32 idx) const;

	ATNotifyList<const vdfunction<void(ATPrinterOutput&)> *> OnAddedOutput;
	ATNotifyList<const vdfunction<void(ATPrinterOutput&)> *> OnRemovingOutput;
	ATNotifyList<const vdfunction<void(ATPrinterGraphicalOutput&)> *> OnAddedGraphicalOutput;
	ATNotifyList<const vdfunction<void(ATPrinterGraphicalOutput&)> *> OnRemovingGraphicalOutput;

public:
	vdrefptr<IATPrinterOutput> CreatePrinterOutput(const wchar_t *name) override;
	vdrefptr<IATPrinterGraphicalOutput> CreatePrinterGraphicalOutput(const wchar_t *name, const ATPrinterGraphicsSpec& spec) override;

public:
	void OnDestroyingOutput(ATPrinterOutputBase& output);

private:
	vdfastvector<ATPrinterOutput *> mOutputs;
	vdfastvector<ATPrinterGraphicalOutput *> mGraphicalOutputs;
};

#endif
