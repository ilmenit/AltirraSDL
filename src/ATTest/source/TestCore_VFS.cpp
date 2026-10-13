//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2020 Avery Lee
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
#include <vd2/system/vdtypes.h>
#include <at/atcore/vfs.h>
#include <test.h>

DEFINE_TEST(Core_VFSMakePath) {
	VDStringW s;

#define TEST_PATH_CASE(basePath, relPath, result) \
	if ((s = ATMakeVFSPath((basePath), (relPath))) != (result)) { \
		printf("Test failed:\n" \
			"\tBase path:       [%ls]\n" \
			"\tRelative path:   [%ls]\n" \
			"\tResolved path:   [%ls]\n" \
			"\tExpected result: [%ls]\n", basePath, relPath, s.c_str(), result); \
		TEST_ASSERT(false); \
	 }

	TEST_PATH_CASE(L"c:", L"x", L"c:x");
	TEST_PATH_CASE(L"c:\\", L"x", L"c:\\x");
	TEST_PATH_CASE(L"c:\\y", L"x", L"c:\\y\\x");
	TEST_PATH_CASE(L"c:\\y\\", L"x", L"c:\\y\\x");
	TEST_PATH_CASE(L"c:\\a", L"b\\c", L"c:\\a\\b\\c");
	TEST_PATH_CASE(L"gz://c:/test", L"b\\c", L"gz://c:/test/b/c");
	TEST_PATH_CASE(L"zip://c:/test.zip!dir", L"b\\c", L"zip://c:/test.zip!dir/b/c");
	TEST_PATH_CASE(L"atfs://c:/test.zip!dir", L"b\\c", L"atfs://c:/test.zip!dir/b/c");
	TEST_PATH_CASE(L"c:\\", L"zip://x/test.zip!foo.bin", L"zip://c:/x/test.zip!foo.bin");

#undef X

	return 0;
}

DEFINE_TEST(Core_VFSEncodeString) {
	VDStringSpanW rawPath(L"\u0080\u0081\u0BAF\u20AC\xD800\xDC00\xD801\xDC02");

	VDStringW url;
	ATEncodeVFSString(url, rawPath, true);

	VDStringW path;
	TEST_ASSERT(ATDecodeVFSString(path, url));

	TEST_ASSERT(path == rawPath);

	return 0;
}

DEFINE_TEST(Core_VFSParsePath) {
	const auto check = [](const wchar_t *testPath, ATVFSProtocol protocol, const wchar_t *basePath, const wchar_t *subPath, std::initializer_list<std::pair<const wchar_t *, const wchar_t *>> params) {
		const ATVFSParsedPath& parsedPath = ATParseVFSPath(testPath);

		if (parsedPath.mProtocol == protocol
			&& parsedPath.mBasePath == basePath
			&& parsedPath.mSubPath == subPath
			&& parsedPath.mParams.size() == params.size())
		{
			bool success = true;

			for(const auto& param : params) {
				const wchar_t *value = parsedPath.FindParam(param.first);

				if (!value || wcscmp(value, param.second) != 0) {
					success = false;
					break;
				}
			}

			if (success)
				return true;
		}

		printf("Test failed:\n"
			"    Input path: [%ls]\n"
			"    Parsed protocol: %u\n"
			"    Parsed base path: [%ls]\n"
			"    Parsed sub path: [%ls]\n"
			"    Parsed parameters:\n"
			, testPath
			, (unsigned)parsedPath.mProtocol
			, parsedPath.mBasePath.c_str()
			, parsedPath.mSubPath.c_str());

		for(const auto& param : parsedPath.mParams) {
			printf("        [%ls]=[%ls]\n", param.mName.c_str(), param.mValue.c_str());
		}

		return false;
	};

	TEST_ASSERT(check(L"c:/test", kATVFSProtocol_File, L"c:\\test", L"", {}));
	TEST_ASSERT(check(L"c:/test/", kATVFSProtocol_File, L"c:\\test", L"", {}));
	TEST_ASSERT(check(L"c://test", kATVFSProtocol_File, L"c:\\test", L"", {}));
	TEST_ASSERT(check(L"c:\\test", kATVFSProtocol_File, L"c:\\test", L"", {}));

	// Query strings are not parsed for bare filenames
	TEST_ASSERT(check(L"c:\\test?", kATVFSProtocol_File, L"c:\\test?", L"", {}));
	TEST_ASSERT(check(L"c:\\test?x=y", kATVFSProtocol_File, L"c:\\test?x=y", L"", {}));

	TEST_ASSERT(check(L"c:\\test\\..\\test2", kATVFSProtocol_File, L"c:\\test2", L"", {}));

	// file: requires three slashes, due to the unused but validated hostname
	TEST_ASSERT(check(L"file://c:/test", kATVFSProtocol_None, L"", L"", {}));

	TEST_ASSERT(check(L"file:///c:/test", kATVFSProtocol_File, L"c:\\test", L"", {}));
	TEST_ASSERT(check(L"file:///c:/test+x", kATVFSProtocol_File, L"c:\\test+x", L"", {}));
	TEST_ASSERT(check(L"file:///c:/test?", kATVFSProtocol_File, L"c:\\test", L"", {}));
	TEST_ASSERT(check(L"file:///c:/test?x=y", kATVFSProtocol_File, L"c:\\test", L"", {{ L"x", L"y" }} ));
	TEST_ASSERT(check(L"file:///c:/test?x=y&y=z+x", kATVFSProtocol_File, L"c:\\test", L"", {{ L"x", L"y" }, {L"y", L"z x"}} ));
	TEST_ASSERT(check(L"file:///c:/test?x=y&y=z x", kATVFSProtocol_File, L"c:\\test", L"", {{ L"x", L"y" }, {L"y", L"z x"}} ));
	TEST_ASSERT(check(L"file:///c:/test?x=y&y=z%2Bx", kATVFSProtocol_File, L"c:\\test", L"", {{ L"x", L"y" }, {L"y", L"z+x"}} ));
	TEST_ASSERT(check(L"file:///c:/test?x=y&y=z&", kATVFSProtocol_File, L"c:\\test", L"", {{ L"x", L"y" }, {L"y", L"z"}} ));
	TEST_ASSERT(check(L"file:///c:/test?x=y&y", kATVFSProtocol_None, L"", L"", {} ));
	TEST_ASSERT(check(L"file:///c:/test/", kATVFSProtocol_File, L"c:\\test", L"", {}));
	TEST_ASSERT(check(L"file:///c://test", kATVFSProtocol_File, L"c:\\test", L"", {}));

	// Note that the base path is not normalized; that won't happen until it is
	// parsed itself as a VFS path.
	TEST_ASSERT(check(L"zip://c:/test.zip!x.atr", kATVFSProtocol_Zip, L"c:/test.zip", L"x.atr", {}));
	TEST_ASSERT(check(L"zip://c:/test.zip!foo!x.atr", kATVFSProtocol_Zip, L"c:/test.zip!foo", L"x.atr", {}));
	TEST_ASSERT(check(L"zip://c:/test.zip%3F!x.atr", kATVFSProtocol_Zip, L"c:/test.zip?", L"x.atr", {}));

	return 0;
}
