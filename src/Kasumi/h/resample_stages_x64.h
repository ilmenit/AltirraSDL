#ifndef f_VD2_KASUMI_RESAMPLE_STAGES_X64_H
#define f_VD2_KASUMI_RESAMPLE_STAGES_X64_H

#include "resample_stages_reference.h"

struct VDResamplerAxis;

///////////////////////////////////////////////////////////////////////////
//
// resampler stages (SSE2, AMD64)
//
///////////////////////////////////////////////////////////////////////////

// These two classes delegate to MASM assembly (vdasm_resize_table_*_SSE2).
// On the portable CMake build there is no MASM assembler, so they are excluded
// and the generic reference implementations are used instead.
#ifndef AT_SDL3_PORTABLE
class VDResamplerSeparableTableRowStageSSE2 final : public VDResamplerRowStageSeparableTable32 {
public:
	VDResamplerSeparableTableRowStageSSE2(const IVDResamplerFilter& filter);

	void Process(void *dst, const void *src, uint32 w, uint32 u, uint32 dudx);
};

class VDResamplerSeparableTableColStageSSE2 final : public VDResamplerColStageSeparableTable32 {
public:
	VDResamplerSeparableTableColStageSSE2(const IVDResamplerFilter& filter);

	void Process(void *dst, const void *const *src, uint32 w, sint32 phase);
};
#endif // !AT_SDL3_PORTABLE

class VDResamplerSeparableTableRowStage8SSE2 final : public VDResamplerRowStageSeparableTable32, public IVDResamplerSeparableRowStage2 {
public:
	VDResamplerSeparableTableRowStage8SSE2(const IVDResamplerFilter& filter);
	
	IVDResamplerSeparableRowStage2 *AsRowStage2() override { return this; } 

	void Init(const VDResamplerAxis& axis, uint32 srcw) override;
	void Process(void *dst, const void *src, uint32 w) override;

	void Process(void *dst, const void *src, uint32 w, uint32 u, uint32 dudx) override;

private:
	vdblock<sint16, vdaligned_alloc<sint16, 16>> mRowFilters;
	vdblock<uint8> mTempBuffer;
	vdblock<uint16> mFastLerpOffsets;
	uint32 mSrcWidth;
	uint32 mNumFastGroups;
	bool mbUseFastLerp;
};

class VDResamplerSeparableTableColStage8SSE2 final : public VDResamplerColStageSeparableTable32 {
public:
	VDResamplerSeparableTableColStage8SSE2(const IVDResamplerFilter& filter);

	void Process(void *dst, const void *const *src, uint32 w, sint32 phase);

private:
	bool mbUseFastLerp;
};

#endif
