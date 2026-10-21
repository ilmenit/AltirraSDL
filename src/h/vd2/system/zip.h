//	VirtualDub - Video processing and capture application
//	System library component
//	Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
//
//	Beginning with 1.6.0, the VirtualDub system library is licensed
//	differently than the remainder of VirtualDub.  This particular file is
//	thus licensed as follows (the "zlib" license):
//
//	This software is provided 'as-is', without any express or implied
//	warranty.  In no event will the authors be held liable for any
//	damages arising from the use of this software.
//
//	Permission is granted to anyone to use this software for any purpose,
//	including commercial applications, and to alter it and redistribute it
//	freely, subject to the following restrictions:
//
//	1.	The origin of this software must not be misrepresented; you must
//		not claim that you wrote the original software. If you use this
//		software in a product, an acknowledgment in the product
//		documentation would be appreciated but is not required.
//	2.	Altered source versions must be plainly marked as such, and must
//		not be misrepresented as being the original software.
//	3.	This notice may not be removed or altered from any source
//		distribution.

#ifndef f_ZIP_H
#define f_ZIP_H

// Rest in peace, Phil Katz.

#include <string.h>
#ifdef _MSC_VER
	#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
	#include <x86intrin.h>
#endif
#include <vd2/system/vdtypes.h>
#include <vd2/system/binary.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/VDString.h>
#include <vd2/system/vdstl.h>

class VDDeflateEncoder;
class VDBufferedStream;

class VDDeflateDecompressionException final : public MyError {
public:
	VDDeflateDecompressionException();
};

class VDDeflateBitReaderL2Buffer {
public:
	struct ValidRange {
		const uint8 *mpStart;
		const uint8 *mpEnd;
	};

	void Init(IVDStream& src, uint64 readLimit);

	// Refill the buffer, consuming all of the bytes up to the given pointer
	// (optional), and returning the new valid range. The unconsumed portion
	// of the buffer is preserved (up to header=16 bytes). Note that the
	// valid range may extend beyond EOF; this is deliberate to ensure that
	// the bit reader can read ahead far enough to consume all valid bits;
	// EOF is checked later.
	ValidRange Refill(const void *consumed);

	// Throw an exception if data has been consumed beyond EOF.
	void CheckEOF(const void *consumed);

	IVDStream& GetSource() const { return *mpSrc; }

private:
	static constexpr uint32 kBufferSize = 65536;
	static constexpr uint32 kHeaderSize = 16;

	uint32	mReadValidEnd = 0;
	uint64	mReadLimitRemaining = 0;
	IVDStream *mpSrc = nullptr;
	uint8	mReadBuffer[kBufferSize + kHeaderSize];
};

class VDDeflateBitReader {
public:
#if VD_PTR_SIZE > 4
	using BitField = uint64;
	static constexpr bool kUsing64 = true;
#else
	using BitField = uint32;
	static constexpr bool kUsing64 = false;
#endif

	static constexpr uint32 kAccumBitSize = sizeof(BitField)*8;

	void init(VDDeflateBitReaderL2Buffer& src) {
		mpBuffer = &src;
		mBitsLeft = 0;
		mBitAccum = 0;
	}

	void CheckEOF();

	VDFORCEINLINE uint32 Peek32() {
		Refill();

		return (uint32)mBitAccum;
	}

	VDFORCEINLINE uint32 PeekUnchecked32() const {
		return (uint32)mBitAccum;
	}

	VDFORCEINLINE void Consume(unsigned n) {
		mBitsLeft -= n;
		mBitAccum >>= n;
	}

	VDFORCEINLINE bool getbit() {
		Refill();

		bool v = (mBitAccum & 1) != 0;
		mBitAccum >>= 1;
		--mBitsLeft;
		return v;
	}

	VDFORCEINLINE uint32 getbits(unsigned n) {
		Refill();

		return GetBitsUnchecked(n);
	}

	VDFORCEINLINE uint32 GetBitsUnchecked(unsigned n) {
		const uint32 v = (uint32)mBitAccum & ((1 << n) - 1);
		mBitAccum >>= n;
		mBitsLeft -= n;

		return v;
	}

	void align() {
		const uint32 alignCount = mBitsLeft & 7;
		mBitAccum >>= alignCount;
		mBitsLeft -= alignCount;
	}

	void readbytes(void *dst, size_t len);

private:
	VDFORCEINLINE void Refill() {
		if (mpSrc >= mpSrcLimit)
			[[unlikely]] RefillBuffer();

		// Refill using fgiesen's variant 4 strategy:
		// https://fgiesen.wordpress.com/2018/02/20/reading-bits-in-far-too-many-ways-part-2/

		if constexpr (sizeof(mBitAccum) > 4)
			mBitAccum |= VDReadUnalignedLEU64(mpSrc) << mBitsLeft;
		else
			mBitAccum |= VDReadUnalignedLEU32(mpSrc) << mBitsLeft;

		// Advance by the number of bytes that were added to the accumulator.
		mpSrc += ((kAccumBitSize - 1) - mBitsLeft) >> 3;

		// update bit counter for the number of bytes added; the result will always be
		// 24-31 (32-bit) or 56-63 (64-bit).
		mBitsLeft |= kAccumBitSize - 8;
	}

	VDNOINLINE void RefillBuffer();

	BitField mBitAccum = 0;

	// Number of valid bits in accumulator, biased by -N where N = accum bit width.
	uint32 mBitsLeft = 0;

	const uint8 *mpSrc = nullptr;
	const uint8 *mpSrcLimit = nullptr;

	VDDeflateBitReaderL2Buffer *mpBuffer = nullptr;
};

class VDCRCTable {
public:
	enum : uint32 {
		kCRC32		= 0xEDB88320		// CRC-32 used by PKZIP, PNG (x^32 + x^26 + x^23 + x^22 + x^16 + x^12 + x^11 + x^10 + x^8 + x^7 + x^5 + x^4 + x^2 + x^1 + 1)
	};

	VDCRCTable() = default;
	explicit VDCRCTable(uint32 crc) { Init(crc); }

	void Init(uint32 crc);

	uint32 Process(uint32 crc, const void *src, size_t len) const;
	uint32 CRC(const void *src, size_t len) const {
		return ~Process(0xFFFFFFFF, src, len);
	}

	static const VDCRCTable CRC32;

private:
	constexpr VDCRCTable(uint32 crc, int);
	constexpr void InitConst(uint32 crc);

	uint32 mTable[256];

};

class VDCRCChecker {
public:
	VDCRCChecker(const VDCRCTable& table);

	void Init() { mValue = 0xFFFFFFFF; }
	void Process(const void *src, sint32 len);

	uint32 CRC() const { return ~mValue; }

protected:
	uint32	mValue;
	const VDCRCTable *mpTable;
};

class VDAdler32Checker {
public:
	void Process(const void *src, size_t len);
	uint32 Adler32() const;

	static uint32 Adler32(const void *src, size_t len);

private:
	uint32 mA = 1;
	uint32 mB = 0;
};

class IVDInflateStream : public IVDStream {
public:
	virtual ~IVDInflateStream() = default;

	virtual void EnableCRC() = 0;
	virtual void SetExpectedCRC(uint32 crc) = 0;
	virtual uint32 CRC() const = 0;
	virtual void VerifyCRC() const = 0;
};

template<bool T_Enhanced>
class VDInflateStream : public IVDInflateStream {
	VDInflateStream(const VDInflateStream&) = delete;
	VDInflateStream& operator=(const VDInflateStream&) = delete;
public:
	VDInflateStream() : mCRCChecker(VDCRCTable::CRC32) {}
	~VDInflateStream();

	void	Init(IVDStream *pSrc, uint64 limit, bool bStored);
	void	EnableCRC() override { mbCRCEnabled = true; }
	void	SetExpectedCRC(uint32 expectedCRC) override { mExpectedCRC = expectedCRC; }
	uint32	CRC() const override { return mCRCChecker.CRC(); }
	void	VerifyCRC() const override;

	const wchar_t *GetNameForError() final override;

	sint64	Pos() final override;
	void	Read(void *buffer, sint32 bytes) final override;
	sint32	ReadData(void *buffer, sint32 bytes) final override;
	void	Write(const void *buffer, sint32 bytes) final override;

protected:
	void	ParseBlockHeader();
	bool	Inflate();
	VDNOINLINE void	InflateBlock();

	VDDeflateBitReader mBits;					// critical -- make this first!
	uint32	mReadPt = 0;
	uint32	mWritePt = 0;
	uint32	mBufferLevel = 0;

	enum {
		kNoBlock,
		kStoredBlock,
		kDeflatedBlock
	} mBlockType = kNoBlock;

	uint32	mStoredBytesLeft = 0;
	bool	mbNoMoreBlocks = false;
	bool	mbCRCEnabled = false;

	sint64	mPos = 0;

	static constexpr uint32 kQuickBits = 10;
	static constexpr uint32 kQuickCodes = 1 << kQuickBits;
	static constexpr uint32 kQuickCodeMask = kQuickCodes - 1;

	static constexpr uint32 kBufferSize = T_Enhanced ? 131072 : 65536;
	static constexpr uint32 kBufferMask = kBufferSize - 1;
	
	// +32 bytes so we can overrun a copy
	uint8	mBuffer[kBufferSize + 32] {};

	uint16	mCodeQuickDecode[kQuickCodes][2] {};
	uint16	mDistQuickDecode[kQuickCodes][2] {};
	uint8	mCodeLengths[288 + 32] {};

	uint16	mCodeDecode[32768] {};
	uint8	mDistDecode[32768] {};

	VDCRCChecker	mCRCChecker;
	uint32			mExpectedCRC;

	// L2 read buffer behind the bit reader's small buffer. We can't use
	// the regular buffered stream because we only require a non random access
	// stream.
	VDDeflateBitReaderL2Buffer	mL2Buffer;
};

class VDZipArchive {
public:
	struct FileInfo {
		VDString	mRawFileName;
		VDStringW	mDecodedFileName;
		uint64		mCompressedSize = 0;
		uint64		mUncompressedSize = 0;
		uint32		mCRC32 = 0;
		bool		mbPacked = false;
		bool		mbEnhancedDeflate = false;
		bool		mbSupported = false;
	};

	VDZipArchive();
	~VDZipArchive();

	void Init(IVDRandomAccessStream *pSrc);

	sint32			GetFileCount();
	const FileInfo&	GetFileInfo(sint32 idx) const;
	sint32			FindFile(const char *rawName) const;
	sint32			FindFile(const wchar_t *decodedName, bool caseSensitive) const;

	IVDStream		*OpenRawStream(sint32 idx);
	IVDInflateStream *OpenDecodedStream(sint32 idx, bool allowLarge = false);

	// Read the raw contents of the file into the given buffer. Returns true if
	// the contents are uncompressed and can be directly used, false if they
	// need to be decompressed.
	bool ReadRawStream(sint32 idx, vdfastvector<uint8>& buf, bool allowLarge = false);

	// Decompress a previously read raw stream for the given file (by index).
	void DecompressStream(sint32 idx, vdfastvector<uint8>& buf) const;

protected:
	struct FileInfoInternal : public FileInfo {
		uint64		mDataStart;
	};

	vdvector<FileInfoInternal>	mDirectory;
	IVDRandomAccessStream			*mpStream;
};

template<bool T_Enhanced>
class VDZipStream final : public VDInflateStream<T_Enhanced> {
public:
	VDZipStream() = default;

	VDZipStream(IVDStream *pSrc, uint64 limit, bool bStored) {
		this->Init(pSrc, limit, bStored);
	}
};

class VDGUnzipStream final : public VDInflateStream<false> {
public:
	VDGUnzipStream() = default;
	VDGUnzipStream(IVDStream *pSrc, uint64 limit) {
		Init(pSrc, limit);
	}

	void Init(IVDStream *pSrc, uint64 limit);

	const char *GetFilename() const { return mFilename.c_str(); }

protected:
	VDStringA mFilename;
};

enum class VDDeflateCompressionLevel : uint8 {
	// Optimize for speed at the expense of compression ratio.
	Quick,

	// Optimize for compression ratio at the expense of speed.
	Best,

	// Optimize for larger 6+ byte matches only, using heuristics similar to
	// zlib's Z_FILTERED. Used specifically for PNG encoding.
	Filtered,

	// Do not compress, use stored blocks only.
	Store
};

enum class VDDeflateChecksumMode : uint8 {
	None,
	Adler32,
	CRC32
};

// Adapter stream for compressing data with the Deflate algorithm, as
// described in RFC1951. The child stream receives the raw Deflate
// stream; no header is added. A CRC-32 is computed on the uncompressed
// data, as needed for gzip and zip, but different than the Adler-32
// used by zlib and png.
//
class VDDeflateStream final : public IVDStream {
	VDDeflateStream(const VDDeflateStream&) = delete;
	VDDeflateStream& operator=(const VDDeflateStream&) = delete;
public:
	VDDeflateStream(IVDStream& dest,
		VDDeflateChecksumMode checksumMode = VDDeflateChecksumMode::CRC32,
		VDDeflateCompressionLevel level = VDDeflateCompressionLevel::Best);
	~VDDeflateStream();

	// Reset the stream state to prepare for compressing another source
	// stream to the same destination stream. This must be called after
	// Finalize() for the previous stream.
	void Reset(VDDeflateCompressionLevel level);

	// Flush out any accumulated data and align the stream to a byte
	// boundary. The stream can be continued afterward. This creates
	// a continuous stream that can be transmitted and decoded in byte
	// chunks.
	void FlushToByteBoundary();

	// Flush any remaining data out, writing a partial block if necessary.
	// This must be called prior to destruction for the compressed
	// stream to be valid.
	void Finalize();

	// Return the CRC-32 of the uncompressed data. Only valid after
	// Finalize().
	uint32 GetCRC() const { return mCRCChecker.CRC(); }
	uint32 Adler32() const { return mAdler32Checker.Adler32(); }

public:
	const wchar_t *GetNameForError() override;

	sint64 Pos() override {
		return mPos;
	}

	void	Read(void *buffer, sint32 bytes) override;
	sint32	ReadData(void *buffer, sint32 bytes) override;
	void	Write(const void *buffer, sint32 bytes) override;

private:
	void PreProcessInputCRC32(const void *p, uint32 n);
	void PreProcessInputAdler32(const void *p, uint32 n);
	void WriteOutput(const void *p, uint32 n);

	IVDStream& mDestStream;
	VDDeflateEncoder *mpEncoder = nullptr;
	sint64 mPos = 0;
	VDDeflateCompressionLevel mCompressionLevel = VDDeflateCompressionLevel::Best;
	VDDeflateChecksumMode mChecksumMode {};

	VDCRCChecker mCRCChecker;
	VDAdler32Checker mAdler32Checker;
};

class IVDZipArchiveWriter {
public:
	virtual ~IVDZipArchiveWriter() = default;

	// Begin writing a new file to the archive using the given path; returns
	// a reference to stream to use for writing the uncompressed data. Only
	// one file can be written at a time. EndFile() must be called after the
	// file data is written. The path may contain subdirectories; the path
	// is normalized to zip file conventions.
	virtual VDDeflateStream& BeginFile(const wchar_t *path, VDDeflateCompressionLevel compressionLevel = VDDeflateCompressionLevel::Best) = 0;

	// End writing a file and complete the file metadata.
	virtual void EndFile() = 0;

	// Complete the zip archive by writing the central directory.
	virtual void Finalize() = 0;
};

IVDZipArchiveWriter *VDCreateZipArchiveWriter(IVDStream& stream);

#endif
