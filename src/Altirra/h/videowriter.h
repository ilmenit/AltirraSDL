#ifndef f_AT_VIDEOWRITER_H
#define f_AT_VIDEOWRITER_H

#include <vd2/system/vdtypes.h>
#include "uitypes.h"

struct VDPixmap;
class VDFraction;
class IATGTIAVideoTap;
class IATAudioTap;

enum ATVideoEncoding : uint8 {
	kATVideoEncoding_Raw,
	kATVideoEncoding_RLE,
	kATVideoEncoding_ZMBV,
	kATVideoEncoding_WMV7,
	kATVideoEncoding_WMV9,
	kATVideoEncoding_H264_AAC,
	kATVideoEncoding_H264_MP3,
	kATVideoEncodingCount
};

enum class ATVideoRecordingResamplingMode : uint8 {
	Nearest,
	SharpBilinear,
	Bilinear,
	Count
};

enum class ATVideoRecordingAspectRatioMode : uint8 {
	None,
	IntegerOnly,
	FullCorrection,
	Count
};

enum class ATVideoRecordingScalingMode : uint8 {
	None,
	Scale480Narrow,
	Scale480Wide,
	Scale720Narrow,
	Scale720Wide,
	Count
};

struct ATVideoRecordingSettings {
	ATVideoEncoding mEncoding {};
	ATVideoRecordingFrameRate mFrameRate {};
	ATVideoRecordingAspectRatioMode mAspectRatioMode {};
	ATVideoRecordingResamplingMode mResamplingMode {};
	ATVideoRecordingScalingMode mScalingMode {};
	uint32 mVideoBitRate {};
	uint32 mAudioBitRate {};
	bool mbHalfRate {};
	bool mbEncodeAll {};
	bool mbRecordAudio = true;
};

struct ATVideoRecordingDebugInfo {
	// Output video resolution, in pixels.
	uint32 mImageWidth;
	uint32 mImageHeight;

	// Destination rectangle for original video image, in output coordinates.
	// This provides the mapping for any letterboxing that is occurring.
	vdrect32f mVideoDestRect;

	// Number of blocks across/down and the block size for ZMBV encoding.
	uint32 mNumBlocksX;
	uint32 mNumBlocksY;
	uint32 mBlockWidth;
	uint32 mBlockHeight;

	struct MotionVector {
		sint8 mX;
		sint8 mY;
	};

	vdfastvector<MotionVector> mMotionVectors;
};

class IATVideoWriter {
public:
	virtual ~IATVideoWriter() = default;

	virtual IATGTIAVideoTap *AsVideoTap() = 0;
	virtual IATAudioTap *AsAudioTap() = 0;

	virtual void CheckExceptions() = 0;

	virtual void Init(const wchar_t *filename, ATVideoEncoding venc,
		uint32 videoBitRate, uint32 audioBitRate,
		uint32 w, uint32 h, const VDFraction& frameRate, double pixelAspectRatio,
		ATVideoRecordingResamplingMode resamplingMode,
		ATVideoRecordingScalingMode scalingMode,
		const uint32 *palette, double samplingRate, bool stereo, bool recordAudio, double timestampRate, bool halfRate, bool encodeAllFrames, IATUIRenderer *r) = 0;
	virtual void Shutdown() = 0;

	virtual bool IsPaused() const = 0;
	virtual void Pause() = 0;
	virtual void Resume() = 0;

	virtual bool GetDebugInfo(ATVideoRecordingDebugInfo& debugInfo) = 0;
};

void ATCreateVideoWriter(IATVideoWriter **w);

#endif	// f_AT_VIDEOWRITER_H
