//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2023 Avery Lee
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
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/time.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/zip.h>
#include <test.h>

namespace {
	class TempStream final : public IVDStream {
	public:
		const wchar_t *GetNameForError() { return L""; }
		sint64	Pos() { return mPos; }

		void	Read(void *buffer, sint32 bytes) {
			if (ReadData(buffer, bytes) != bytes)
				throw MyError("unexpected EOF");
		}

		sint32	ReadData(void *buffer, sint32 bytes) {
			if (bytes <= 0)
				return 0;

			size_t avail = mBuf.size() - mPos;
			if ((size_t)bytes > avail)
				bytes = (sint32)avail;

			memcpy(buffer, mBuf.data() + mPos, bytes);
			mPos += bytes;

			return bytes;
		}

		void Write(const void *buffer, sint32 bytes) {
			if (bytes <= 0)
				return;

			size_t lim = mPos + (size_t)bytes;
			if (lim < mPos)
				throw MyError("stream overflow");

			if (mBuf.size() < lim)
				mBuf.resize(lim);

			memcpy(mBuf.data() + mPos, buffer, bytes);
			mPos += (size_t)bytes;
		}

		void Reset() { mPos = 0; }

		vdfastvector<char> mBuf;
		size_t mPos = 0;
	};
}

DEFINE_TEST_NONAUTO(System_Zip) {
	vdblock<char> buf(65536);
	vdblock<char> buf2(65536);

	int iterations = 0;
	for(;;) {
		int rle = 0;
		char rlec;
		for(int i=0; i<65536; ++i) {
			if (!rle--) {
				rle = (rand() & 511) + 1;
				rlec = (char)rand();
			}

			buf[i] = rlec;
		}

		TempStream ms;

		{
			VDDeflateStream ds(ms);
			ds.Write(buf.data(), 65536);
			ds.Finalize();
		}

		ms.Reset();

		vdautoptr is(new VDInflateStream<false>);
		is->Init(&ms, ms.mBuf.size(), false);

		is->Read(buf2.data(), buf2.size());

		TEST_ASSERT(!memcmp(buf.data(), buf2.data(), 65536));

		if (!(++iterations % 1000))
			printf("%u iterations completed\n", iterations);
	}

	return 0;
}

template<VDDeflateCompressionLevel T_Level>
void ATTestSystem_ZipBench() {
	const wchar_t *args = ATTestGetArguments();
	VDStringRefW fnsrc(args);
	VDStringRefW fndst;

	if (fnsrc.split(L',', fndst))
		std::swap(fnsrc, fndst);

	VDFileStream fs(VDStringW(fnsrc).c_str());

	vdfastvector<uint8> buf;
	vdfastvector<uint8> buf2;
	buf.resize(fs.Length());
	buf2.resize(buf.size());

	fs.read(buf.data(), buf.size());

	VDMemoryBufferStream mbs;

	uint64 cmindt = 0;
	uint64 dmindt = 0;

	{
		auto t0 = VDGetPreciseTick();

		mbs.Clear();

		{
			VDDeflateStream ds(mbs, VDDeflateChecksumMode::CRC32, T_Level);
			ds.Write(buf.data(), buf.size());
			ds.Finalize();
		}

		cmindt = VDGetPreciseTick() - t0;

		if (!fndst.empty()) {
			VDFileStream fs(VDStringW(fndst).c_str(), nsVDFile::kWrite | nsVDFile::kCreateAlways);

			fs.write("\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\xFF", 10);

			auto zbuf = mbs.GetBuffer();
			fs.write(zbuf.data(), zbuf.size());

			uint32 crcv[2];

			crcv[0] = VDCRCTable::CRC32.CRC(buf.data(), buf.size());
			crcv[1] = buf.size();

			fs.write(crcv, 8);
		}

		uint64 len = mbs.Pos();
		mbs.Seek(0);
		VDInflateStream<false> zs;
		zs.Init(&mbs, len, false);

		buf2.resize(buf.size());

		auto t1 = VDGetPreciseTick();
		auto actual = zs.ReadData(buf2.data(), buf2.size());
		dmindt = VDGetPreciseTick() - t1;

		AT_TEST_ASSERTF(actual == buf2.size(), "Decompression mismatch: only read %d bytes", (int)actual);
		AT_TEST_ASSERTF(!memcmp(buf.data(), buf2.data(), buf.size()), "Decompression data mismatch");

		char dummy = 0;
		actual = zs.ReadData(&dummy, 1);
		AT_TEST_ASSERTF(!actual, "Extra data found after decompressed stream");
	}

	double csecs = (double)cmindt * VDGetPreciseSecondsPerTick();
	double dsecs = (double)dmindt * VDGetPreciseSecondsPerTick();
	uint64 len = mbs.Pos();

	printf("%10u | %7.1f ms / %7.1f MB/sec | %7.1f ms / %7.1f MB/sec | %10u (%5.1f%%)\n"
		, (unsigned)buf.size()
		, csecs * 1000.0
		, (double)buf.size() / 1000000.0 / csecs
		, dsecs * 1000.0
		, (double)buf.size() / 1000000.0 / dsecs
		, (unsigned)len
		, 100.0 * (1.0 - (double)len / (double)buf.size())
	);
}

DEFINE_TEST_NONAUTO(System_ZipBenchQ) {
	ATTestSystem_ZipBench<VDDeflateCompressionLevel::Quick>();
	return 0;
}

DEFINE_TEST_NONAUTO(System_ZipBenchF) {
	ATTestSystem_ZipBench<VDDeflateCompressionLevel::Filtered>();
	return 0;
}

DEFINE_TEST_NONAUTO(System_ZipBenchX) {
	ATTestSystem_ZipBench<VDDeflateCompressionLevel::Best>();
	return 0;
}

DEFINE_TEST_NONAUTO(System_ZipBenchUnzip) {
	const wchar_t *args = ATTestGetArguments();

	if (!*args)
		throw ATTestAssertionException("No filename specified on command line.");

	VDStringW fnbuf(args);
	bool summary = false;

	if (fnbuf.size() >= 3 && fnbuf.subspan(fnbuf.size()-2, 2) == L"/s") {
		fnbuf.pop_back();
		fnbuf.pop_back();
		summary = true;
	}

	VDDirectoryIterator dirIt(fnbuf.c_str());

	while(dirIt.Next()) {
		const VDStringW path(dirIt.GetFullPath());
		VDFileStream fs(path.c_str());
		VDZipArchive za;
		za.Init(&fs);

		vdblock<char> buf(65536);

		const sint32 n = za.GetFileCount();
		double totalTime = 0;
		sint64 totalInBytes = 0;
		sint64 totalOutBytes = 0;

		for(sint32 i=0; i<n; ++i) {
			vdautoptr<IVDInflateStream> is(za.OpenDecodedStream(i, true));
			const auto& fi = za.GetFileInfo(i);
			sint32 len = 0;
			sint64 left = fi.mUncompressedSize;

			is->EnableCRC();
			is->SetExpectedCRC(fi.mCRC32);

			const uint64 startTick = VDGetPreciseTick();
			while(left > 0) {
				sint32 tc = (sint32)std::min<sint64>(left, 65536);
				left -= tc;

				sint32 actual = is->ReadData(buf.data(), tc);

				if (actual == 0)
					break;

				if (actual < 0)
					throw ATTestAssertionException("Decompression error: %ls", za.GetFileInfo(i).mDecodedFileName.c_str());

				len += actual;
			}

			is->VerifyCRC();

			const uint64 endTick = VDGetPreciseTick();
			const double seconds = (double)(endTick - startTick) * VDGetPreciseSecondsPerTick();

			totalTime += seconds;
			totalInBytes += fi.mCompressedSize;
			totalOutBytes += len;

			if (!summary) {
				printf("%6.1fMB  %6.1fMB/sec -> %7.1fMB  %6.1fMB/sec | %ls\n"
					, (double)fi.mCompressedSize / (1024.0 * 1024.0)
					, (double)fi.mCompressedSize / seconds / (1024.0 * 1024.0)
					, (double)len / (1024.0 * 1024.0)
					, (double)len / seconds / (1024.0 * 1024.0)
					, fi.mDecodedFileName.c_str());
			}
		}

		if (n > 1 || summary) {
			printf("%6.1fMB  %6.1fMB/sec -> %7.1fMB  %6.1fMB/sec | <Total> %ls\n"
				, (double)totalInBytes / (1024.0 * 1024.0)
				, (double)totalInBytes / totalTime / (1024.0 * 1024.0)
				, (double)totalOutBytes / (1024.0 * 1024.0)
				, (double)totalOutBytes / totalTime / (1024.0 * 1024.0)
				, path.c_str()
			);
		}
	}
	return 0;
}

DEFINE_TEST_NONAUTO(System_ZipBenchCompress) {
	VDMemoryBufferStream mbs;

	vdblock<uint8> buf(10*1000*1000);
	vdblock<uint8> buf2(10*1000*1000);

	const wchar_t *outpath = ATTestGetArguments();

	const auto testBench = [&](const char *name, const wchar_t *fn, VDDeflateCompressionLevel level) {
		uint64 cmindt = 0;
		uint64 dmindt = 0;
		const int lim = *outpath ? 1 : 5;

		for(int i=0; i<lim; ++i) {
			auto t0 = VDGetPreciseTick();

			mbs.Clear();

			{
				VDDeflateStream ds(mbs, VDDeflateChecksumMode::CRC32, level);
				ds.Write(buf.data(), buf.size());
				ds.Finalize();
			}

			auto dt = VDGetPreciseTick() - t0;

			if (*outpath) {
				VDFileStream fs(VDMakePath(outpath, fn).c_str(), nsVDFile::kWrite | nsVDFile::kCreateAlways);

				fs.write("\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\xFF", 10);

				auto zbuf = mbs.GetBuffer();
				fs.write(zbuf.data(), zbuf.size());

				uint32 crcv[2];

				crcv[0] = VDCRCTable::CRC32.CRC(buf.data(), buf.size());
				crcv[1] = buf.size();

				fs.write(crcv, 8);
			}

			if (!i || dt < cmindt)
				cmindt = dt;

			uint64 len = mbs.Pos();
			mbs.Seek(0);
			VDInflateStream<false> zs;
			zs.Init(&mbs, len, false);
			memset(buf2.data(), 0xA5, buf2.size());

			auto t1 = VDGetPreciseTick();
			auto actual = zs.ReadData(buf2.data(), buf2.size());
			auto dt2 = VDGetPreciseTick() - t1;

			if (!i || dt2 < dmindt)
				dmindt = dt2;

			AT_TEST_ASSERTF(actual == buf2.size(), "Decompression mismatch: only read %d bytes", (int)actual);
			AT_TEST_ASSERTF(!memcmp(buf.data(), buf2.data(), buf.size()), "Decompression data mismatch");

			char dummy = 0;
			actual = zs.ReadData(&dummy, 1);
			AT_TEST_ASSERTF(!actual, "Extra data found after decompressed stream");
		}

		double csecs = (double)cmindt * VDGetPreciseSecondsPerTick();
		double dsecs = (double)dmindt * VDGetPreciseSecondsPerTick();
		uint64 len = mbs.Pos();

		printf("%7.1f ms / %7.1f MB/sec | %7.1f ms / %7.1f MB/sec | %10u (%5.1f%%) | %s\n"
			, csecs * 1000.0
			, (double)buf.size() / 1000000.0 / csecs
			, dsecs * 1000.0
			, (double)buf.size() / 1000000.0 / dsecs
			, (unsigned)len
			, 100.0 * (1.0 - (double)len / (double)buf.size())
			, name
		);
	};

	memset(buf.data(), 0, buf.size());

	testBench("10MB zero data", L"10mb-zero-x.gz", VDDeflateCompressionLevel::Best);
	testBench("10MB zero data (quick)", L"10mb-zero-q.gz", VDDeflateCompressionLevel::Quick);
	testBench("10MB zero data (filt)", L"10mb-zero-f.gz", VDDeflateCompressionLevel::Filtered);

	uint32 seed = 1;
	for(uint8& v : buf) {
		v = (uint8)seed++;
	}

	testBench("10MB repeated data", L"10mb-repeat-x.gz", VDDeflateCompressionLevel::Best);
	testBench("10MB repeated data (quick)", L"10mb-repeat-q.gz", VDDeflateCompressionLevel::Quick);
	testBench("10MB repeated data (filt)", L"10mb-repeat-f.gz", VDDeflateCompressionLevel::Filtered);

	seed = 1;
	for(uint8& v : buf) {
		v = seed & 0x0F;

		seed = (seed << 8) + (((seed >> 24) ^ (seed >> 22) ^ (seed >> 18) ^ (seed >> 17)) & 0xFF);
	}

	testBench("10MB random 4-bit data", L"10mb-4bitrand-x.gz", VDDeflateCompressionLevel::Best);
	testBench("10MB random 4-bit data (quick)", L"10mb-4bitrand-q.gz",VDDeflateCompressionLevel::Quick);
	testBench("10MB random 4-bit data (filt)", L"10mb-4bitrand-f.gz", VDDeflateCompressionLevel::Filtered);
	testBench("10MB random 4-bit data (stored)", L"10mb-4bitrand-s.gz", VDDeflateCompressionLevel::Store);

	seed = 1;
	for(uint8& v : buf) {

		v = seed;

		seed = (seed << 8) + (((seed >> 24) ^ (seed >> 22) ^ (seed >> 18) ^ (seed >> 17)) & 0xFF);
	}

	testBench("10MB random 8-bit data", L"10mb-8bitrand-x.gz", VDDeflateCompressionLevel::Best);
	testBench("10MB random 8-bit data (quick)", L"10mb-8bitrand-x.gz", VDDeflateCompressionLevel::Quick);
	testBench("10MB random 8-bit data (filt)", L"10mb-8bitrand-f.gz", VDDeflateCompressionLevel::Filtered);
	testBench("10MB random 8-bit data (stored)", L"10mb-8bitrand-s.gz", VDDeflateCompressionLevel::Store);

	return 0;
}

template<VDDeflateCompressionLevel T_Level>
void ATTestSystem_ZipBenchReZip() {
	VDFileStream fs(ATTestGetArguments());
	VDZipArchive arch;

	arch.Init(&fs);

	sint32 n = arch.GetFileCount();
	vdfastvector<uint8> buf;
	vdfastvector<uint8> buf2;
	VDMemoryBufferStream mbs;
	for(sint32 i = 0; i < n; ++i) {
		const auto& fileInfo = arch.GetFileInfo(i);

		vdautoptr<IVDInflateStream> ds { arch.OpenDecodedStream(i, true) };
		// Validate the 64-bit member size before a 32-bit allocation.
		buf.resize((size_t)fileInfo.mUncompressedSize);
		ds->Read(buf.data(), buf.size());
		ds = nullptr;

		uint64 cmindt = 0;
		uint64 dmindt = 0;

		{
			auto t0 = VDGetPreciseTick();

			mbs.Clear();

			{
				VDDeflateStream ds(mbs, VDDeflateChecksumMode::CRC32, T_Level);
				ds.Write(buf.data(), buf.size());
				ds.Finalize();
			}

			cmindt = VDGetPreciseTick() - t0;

			uint64 len = mbs.Pos();
			mbs.Seek(0);
			VDInflateStream<false> zs;
			zs.Init(&mbs, len, false);

			buf2.resize(buf.size());

			auto t1 = VDGetPreciseTick();
			auto actual = zs.ReadData(buf2.data(), buf2.size());
			dmindt = VDGetPreciseTick() - t1;

			AT_TEST_ASSERTF(actual == buf2.size(), "Decompression mismatch: only read %d bytes", (int)actual);
			AT_TEST_ASSERTF(!memcmp(buf.data(), buf2.data(), buf.size()), "Decompression data mismatch");

			char dummy = 0;
			actual = zs.ReadData(&dummy, 1);
			AT_TEST_ASSERTF(!actual, "Extra data found after decompressed stream");
		}

		double csecs = (double)cmindt * VDGetPreciseSecondsPerTick();
		double dsecs = (double)dmindt * VDGetPreciseSecondsPerTick();
		uint64 len = mbs.Pos();

		printf("%10llu | %10llu | %7.1f ms / %7.1f MB/sec / %7.1f MB/sec | %7.1f ms / %7.1f MB/sec | %10u (%5.1f%%) | %s\n"
			, (unsigned long long)fileInfo.mUncompressedSize
			, (unsigned long long)fileInfo.mCompressedSize
			, csecs * 1000.0
			, (double)buf.size() / 1000000.0 / csecs
			, (double)mbs.GetBuffer().size() / 1000000.0 / csecs
			, dsecs * 1000.0
			, (double)buf.size() / 1000000.0 / dsecs
			, (unsigned)len
			, 100.0 * (1.0 - (double)len / (double)buf.size())
			, fileInfo.mRawFileName.c_str()
		);
	};
}

DEFINE_TEST_NONAUTO(System_ZipBenchRezipN) {
	ATTestSystem_ZipBenchReZip<VDDeflateCompressionLevel::Best>();
	return 0;
}

DEFINE_TEST_NONAUTO(System_ZipBenchRezipQ) {
	ATTestSystem_ZipBenchReZip<VDDeflateCompressionLevel::Quick>();
	return 0;
}
