//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2026 Avery Lee
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
#include <vd2/system/vdalloc.h>
#include <vd2/vdjson/jsonoutput.h>
#include <vd2/vdjson/jsonreader.h>
#include <vd2/vdjson/jsonvalue.h>
#include <vd2/vdjson/jsonwriter.h>
#include <variant>
#include "test.h"

namespace {
	class Array;
	class Object;

	class ComplexNode {
	public:
		virtual bool Verify(const VDJSONValueRef& v) const = 0;
	};

	class VerifyNode {
	public:
		VerifyNode(std::nullptr_t) : mType(Type::Null) {
		}

		VerifyNode(bool v) : mType(Type::Boolean) {
			mBoolVal = v;
		}

		template<typename T> requires std::is_integral_v<T>
		VerifyNode(T v) : mType(Type::Int) {
			mIntVal = (sint64)v;
		}

		VerifyNode(double v) : mType(Type::Double) {
			mDoubleVal = v;
		}

		VerifyNode(const wchar_t *s) : mType(Type::String) {
			mStrVal = s;
		}

		VerifyNode(const VDStringSpanW& s) : mType(Type::StringSpan) {
			mpStrSpanVal = &s;
		}

		VerifyNode(ComplexNode&& v) : mType(Type::Complex) {
			mpComplexNode = &v;
		}

		bool Verify(const VDJSONValueRef& v) const {
			switch(mType) {
				case Type::Null:
					return v.IsNull();

				case Type::Boolean:
					if (!v.IsBool())
						return false;

					return v.AsBool() == mBoolVal;

				case Type::Int:
					if (v.IsInt())
						return v.AsInt64() == mIntVal;

					if (v.IsReal())
						return mIntVal == (sint64)(double)mIntVal && (double)mIntVal == v.AsDouble();

					return false;

				case Type::Double:
					if (v.IsReal())
						return v.AsDouble() == mDoubleVal;

					if (v.IsInt())
						return (sint64)(double)v.AsInt64() == v.AsInt64() && (double)v.AsInt64() == mDoubleVal;

					return false;

				case Type::String:
					if (!v.IsString())
						return false;

					return 0 == wcscmp(v.AsString(), mStrVal);

				case Type::StringSpan:
					if (!v.IsString())
						return false;

					return v.AsStringSpan() == *mpStrSpanVal;

				case Type::Complex:
					return mpComplexNode->Verify(v);
			}

			return false;
		}

	protected:
		enum class Type : uint8 {
			Null,
			Boolean,
			Int,
			Double,
			String,
			StringSpan,
			Complex
		};

		Type mType;

		union {
			bool mBoolVal;
			sint64 mIntVal;
			double mDoubleVal;
			const wchar_t *mStrVal;
			const VDStringSpanW *mpStrSpanVal;
			const ComplexNode *mpComplexNode;
		};
	};

	class Array final : public ComplexNode {
	public:
		Array(std::initializer_list<VerifyNode> elements)
			: mElements(elements.begin(), elements.size())
		{}

		bool Verify(const VDJSONValueRef& v) const {
			if (!v.IsArray())
				return false;

			const auto& array = v.AsArray();
			const auto n = mElements.size();

			if (n != array.size())
				return false;

			for(size_t i = 0; i < n; ++i) {
				if (!mElements[i].Verify(array[i]))
					return false;
			}

			return true;
		}

		vdspan<const VerifyNode> mElements;
	};

	class Mb {
	public:
		Mb(const char *name, VerifyNode&& value)
			: mpName(name)
			, mVerifyNode(value)
		{
		}

		const char *mpName;
		const VerifyNode& mVerifyNode;
	};

	class Object final : public ComplexNode {
	public:
		Object(std::initializer_list<Mb> members)
			: mMembers(members.begin(), members.size())
		{
		}

		bool Verify(const VDJSONValueRef& v) const {
			if (!v.IsObject())
				return false;

			if (mMembers.size() != v.GetMemberCount())
				return false;

			for(const Mb& mb : mMembers) {
				const VDJSONValueRef& value = v[mb.mpName];

				if (!mb.mVerifyNode.Verify(value))
					return false;
			}

			return true;
		}

	private:
		vdspan<const Mb> mMembers;
	};

	bool Verify(const char *str, size_t len, const VerifyNode& vn) {
		VDJSONReader r;
		VDJSONDocument doc;
		if (!r.Parse(str, len, doc))
			return false;

		if (!vn.Verify(doc.Root()))
			return false;

		VDStringW buf;

		{
			VDJSONStringOutput output(buf);
			VDJSONWriter w;
			w.Begin(&output, true);
			VDJSONWriteValue(w, *doc.Root(), doc.mNameTable);
			w.End();
		}

		const auto& buf8 = VDTextWToU8(buf);

		VDJSONReader r2;
		VDJSONDocument doc2;
		if (!r2.Parse(buf8.data(), buf8.size() * sizeof(buf8[0]), doc2))
			return false;

		if (!vn.Verify(doc2.Root()))
			return false;

		return true;
	}

	bool Verify(const char *str, const VerifyNode& vn) {
		return Verify(str, strlen(str), vn);
	}

	bool Verify(const char8_t *str, const VerifyNode& vn) {
		return Verify((const char *)str, std::char_traits<char8_t>::length(str) * sizeof(char8_t), vn);
	}

	bool Verify(const char16_t *str, const VerifyNode& vn) {
		return Verify((const char *)str, std::char_traits<char16_t>::length(str) * sizeof(char16_t), vn);
	}

	bool Verify(const char32_t *str, const VerifyNode& vn) {
		return Verify((const char *)str, std::char_traits<char32_t>::length(str) * sizeof(char32_t), vn);
	}

	bool VerifyParseFail(const char *str, size_t len) {
		VDJSONReader r;
		VDJSONDocument doc;
		return !r.Parse(str, len, doc);
	}

	bool VerifyParseFail(const char *str) {
		return VerifyParseFail(str, strlen(str));
	}

	bool VerifyParseFail(const char8_t *str) {
		return VerifyParseFail((const char *)str, std::char_traits<char8_t>::length(str) * sizeof(char8_t));
	}

	bool VerifyParseFail(const char16_t *str) {
		return VerifyParseFail((const char *)str, std::char_traits<char16_t>::length(str) * sizeof(char16_t));
	}

	bool VerifyParseFail(const char32_t *str) {
		return VerifyParseFail((const char *)str, std::char_traits<char32_t>::length(str) * sizeof(char32_t));
	}
}

DEFINE_TEST(Json) {
	AT_TEST_ASSERT(Verify(R"---({})---", Object{}));
	AT_TEST_ASSERT(Verify(R"---({"a": 0})---", Object{Mb("a", 0)}));
	AT_TEST_ASSERT(VerifyParseFail(R"---({"a"})---"));
	AT_TEST_ASSERT(VerifyParseFail(R"---({a:0})---"));

	AT_TEST_ASSERT(Verify(R"---([])---", Array{}));
	AT_TEST_ASSERT(Verify(R"---([0])---", Array{0}));
	AT_TEST_ASSERT(Verify(R"---([ 0])---", Array{0}));
	AT_TEST_ASSERT(Verify(R"---([0 ])---", Array{0}));
	AT_TEST_ASSERT(Verify(R"---([ 0 ])---", Array{0}));
	AT_TEST_ASSERT(Verify(R"---([0,1])---", Array{0, 1}));
	AT_TEST_ASSERT(Verify(R"---([ 0 , 1 ])---", Array{0, 1}));
	AT_TEST_ASSERT(Verify(R"---([0,[1]])---", Array{0, Array{1}}));

	// trailing comma not supported
	AT_TEST_ASSERT(VerifyParseFail(R"---([,])---"));
	AT_TEST_ASSERT(VerifyParseFail(R"---([0,])---"));
	AT_TEST_ASSERT(VerifyParseFail(R"---({,})---"));
	AT_TEST_ASSERT(VerifyParseFail(R"---({"a":0,})---"));

	// === check doubles ===
	AT_TEST_ASSERT(Verify(R"---([0])---", Array{0}));
	AT_TEST_ASSERT(Verify(R"---([-1])---", Array{-1}));
	AT_TEST_ASSERT(Verify(R"---([1.5])---", Array{1.5}));
	AT_TEST_ASSERT(Verify(R"---([-1.5])---", Array{-1.5}));
	AT_TEST_ASSERT(VerifyParseFail(R"---([+1])---"));
	AT_TEST_ASSERT(VerifyParseFail(R"---([- 1])---"));
	AT_TEST_ASSERT(VerifyParseFail(R"---([1.a])---"));
	AT_TEST_ASSERT(VerifyParseFail(R"---([1.])---"));
	AT_TEST_ASSERT(VerifyParseFail(R"---([1 .])---"));
	AT_TEST_ASSERT(VerifyParseFail(R"---([inf])---"));
	AT_TEST_ASSERT(VerifyParseFail(R"---([nan])---"));

	// leading zeroes are not allowed on the integer portion unless it is actually zero
	AT_TEST_ASSERT(Verify(R"---([10])---", Array{10}));
	AT_TEST_ASSERT(VerifyParseFail(R"---([00])---"));
	AT_TEST_ASSERT(VerifyParseFail(R"---([001])---"));
	AT_TEST_ASSERT(VerifyParseFail(R"---([010])---"));

	// exponent sign is optional
	AT_TEST_ASSERT(Verify(R"---([1e0])---", Array{1.0}));
	AT_TEST_ASSERT(Verify(R"---([1e1])---", Array{10.0}));

	AT_TEST_ASSERT(Verify(R"---([1e+0, 1e-0])---", Array{1.0, 1.0}));
	AT_TEST_ASSERT(Verify(R"---([1e+1, 1e-1])---", Array{10, 0.1}));
	AT_TEST_ASSERT(Verify(R"---([1e+10, 1e-10])---", Array{1e+10, 1e-10}));

	// hex numbers are not allowed
	AT_TEST_ASSERT(VerifyParseFail(R"---([0x1])---"));

	// check that 64-bit integer precision is preserved
	AT_TEST_ASSERT(Verify(R"---([9223372036854775807])---", Array{0x7FFFFFFF'FFFFFFFF}));
	AT_TEST_ASSERT(Verify(R"---([-9223372036854775807])---", Array{-0x7FFFFFFF'FFFFFFFF}));
	AT_TEST_ASSERT(Verify(R"---([-9223372036854775808])---", Array{-0x7FFFFFFF'FFFFFFFF-1}));

	// check that larger integers are supported and preserved as doubles
	AT_TEST_ASSERT(Verify(R"---([18446744073709551616])---", Array{18446744073709551616.0}));
	AT_TEST_ASSERT(Verify(R"---([-18446744073709551616])---", Array{-18446744073709551616.0}));

	// === null ===
	AT_TEST_ASSERT(Verify(R"---([null])---", Array{nullptr}));
	AT_TEST_ASSERT(VerifyParseFail(R"---([Null])---"));

	// === boolean (MUST be lowercase) ===
	AT_TEST_ASSERT(Verify(R"---([false, true])---", Array{false, true}));
	AT_TEST_ASSERT(VerifyParseFail(R"---([False])---"));
	AT_TEST_ASSERT(VerifyParseFail(R"---([True])---"));

	// With redundant members, last has precedence
	AT_TEST_ASSERT(Verify(R"---({"a": 0, "a": 1})---", Object{Mb("a", 1)}));
	AT_TEST_ASSERT(Verify(R"---({"a": 0, "b": 1, "a": 2})---", Object{Mb("a", 2), Mb("b", 1)}));
	AT_TEST_ASSERT(Verify(R"---({"a": 0, "b": 1, "b": 2})---", Object{Mb("a", 0), Mb("b", 2)}));
	AT_TEST_ASSERT(Verify(R"---({"a": 0, "b": 1, "a": 2, "b": 3})---", Object{Mb("a", 2), Mb("b", 3)}));
	AT_TEST_ASSERT(Verify(R"---({"a": 0, "\u0061": 1})---", Object{Mb("a", 1)}));

	// test specifically across 16 members since the reader has a fast path for it
	AT_TEST_ASSERT(Verify(R"---(
		{
			"c0":0,
			"c1":1,
			"c2":2,
			"c3":3,
			"c4":4,
			"c5":5,
			"c6":6,
			"c7":7,
			"c8":8,
			"c9":9,
			"c10":10,
			"c11":11,
			"c12":12,
			"c13":13,
			"c14":14,
			"c15":15,
			"c0":100
		}
	)---",
		Object{
			Mb("c0", 100),
			Mb("c1", 1),
			Mb("c2", 2),
			Mb("c3", 3),
			Mb("c4", 4),
			Mb("c5", 5),
			Mb("c6", 6),
			Mb("c7", 7),
			Mb("c8", 8),
			Mb("c9", 9),
			Mb("c10", 10),
			Mb("c11", 11),
			Mb("c12", 12),
			Mb("c13", 13),
			Mb("c14", 14),
			Mb("c15", 15)
		}
	));

	AT_TEST_ASSERT(Verify(R"---(
		{
			"c0":0,
			"c1":1,
			"c2":2,
			"c3":3,
			"c4":4,
			"c5":5,
			"c6":6,
			"c7":7,
			"c8":8,
			"c9":9,
			"c10":10,
			"c11":11,
			"c12":12,
			"c13":13,
			"c14":14,
			"c15":15,
			"c16":16,
			"c0":100
		}
	)---",
		Object{
			Mb("c0", 100),
			Mb("c1", 1),
			Mb("c2", 2),
			Mb("c3", 3),
			Mb("c4", 4),
			Mb("c5", 5),
			Mb("c6", 6),
			Mb("c7", 7),
			Mb("c8", 8),
			Mb("c9", 9),
			Mb("c10", 10),
			Mb("c11", 11),
			Mb("c12", 12),
			Mb("c13", 13),
			Mb("c14", 14),
			Mb("c15", 15),
			Mb("c16", 16)
		}
	));

	// Currently we require an object/array at top level per RFC 4627; the
	// general value at root specified in RFC 8259 (we have no use for it yet).
	AT_TEST_ASSERT(VerifyParseFail(R"---("a")---"));

	// === Test string escapes ===
	AT_TEST_ASSERT(Verify(R"---(["\uA000"])---", Array{L"\uA000"}));

	// Unpaired surrogates are allowed by RFC 8259. But we don't have a good
	// reason to allow it, and it won't survive conversion to UTF-8 -- and I'd
	// rather not support WTF-8.
	AT_TEST_ASSERT(VerifyParseFail(R"---(["\uD800"])---"));
	AT_TEST_ASSERT(VerifyParseFail(R"---(["\uD800\uA000"])---"));
	AT_TEST_ASSERT(VerifyParseFail(R"---(["\uDC00"])---"));
	AT_TEST_ASSERT(VerifyParseFail(R"---(["\uDC00\uDC00"])---"));
	AT_TEST_ASSERT(Verify(R"---(["\uD800\uDC00"])---", Array{L"\U00010000"}));
	AT_TEST_ASSERT(Verify(R"---(["\uD800\uDC01"])---", Array{L"\U00010001"}));
	AT_TEST_ASSERT(Verify(R"---(["\uD801\uDC00"])---", Array{L"\U00010400"}));
	AT_TEST_ASSERT(Verify(R"---(["\uDBFF\uDFFF"])---", Array{L"\U0010FFFF"}));

	AT_TEST_ASSERT(VerifyParseFail(R"---(["\])---"));
	AT_TEST_ASSERT(VerifyParseFail(R"---(["\"])---"));
	AT_TEST_ASSERT(VerifyParseFail(R"---(["\0"])---"));

	const wchar_t nullChar[1] {};

	AT_TEST_ASSERT(Verify(R"---(["\u0000"])---", Array{VDStringSpanW(nullChar, nullChar+1)}));

	AT_TEST_ASSERT(Verify(R"---(["\""])---", Array{L"\""}));
	AT_TEST_ASSERT(Verify(R"---(["\\"])---", Array{L"\\"}));
	AT_TEST_ASSERT(Verify(R"---(["\/"])---", Array{L"/"}));
	AT_TEST_ASSERT(Verify(R"---(["\b"])---", Array{L"\b"}));
	AT_TEST_ASSERT(Verify(R"---(["\f"])---", Array{L"\f"}));
	AT_TEST_ASSERT(Verify(R"---(["\n"])---", Array{L"\n"}));
	AT_TEST_ASSERT(Verify(R"---(["\r"])---", Array{L"\r"}));
	AT_TEST_ASSERT(Verify(R"---(["\t"])---", Array{L"\t"}));

	// test invalid unescaped control character
	AT_TEST_ASSERT(VerifyParseFail("[\"\t\"]"));

	// test non-printable character that doesn't need to be escaped, along with UTF-8
	// encodings
	AT_TEST_ASSERT(Verify("[\"\u007F\"]", Array{L"\u007F"}));
	AT_TEST_ASSERT(Verify(u8"[\"\u0080\"]", Array{L"\u0080"}));
	AT_TEST_ASSERT(Verify(u8"[\"\u00FF\"]", Array{L"\u00FF"}));
	AT_TEST_ASSERT(Verify(u8"[\"\u07FF\"]", Array{L"\u07FF"}));
	AT_TEST_ASSERT(Verify(u8"[\"\u0800\"]", Array{L"\u0800"}));
	AT_TEST_ASSERT(Verify(u8"[\"\uFFFF\"]", Array{L"\uFFFF"}));
	AT_TEST_ASSERT(Verify(u8"[\"\U00010000\"]", Array{L"\U00010000"}));
	AT_TEST_ASSERT(Verify(u8"[\"\U00011000\"]", Array{L"\U00011000"}));

	// test invalid UTF-8 encodings (note that we need to use characters that
	// don't need escaping)
	AT_TEST_ASSERT(VerifyParseFail("[\"\xC0\"]"));					// dangling leading byte
	AT_TEST_ASSERT(VerifyParseFail("[\"\x80\"]"));					// dangling continuation byte
	AT_TEST_ASSERT(VerifyParseFail("[\"\xE0\x82\x80\"]"));			// overlong for U+0080
	AT_TEST_ASSERT(VerifyParseFail("[\"\xF0\x80\x82\x80\"]"));		// overlong for U+0080
	AT_TEST_ASSERT(VerifyParseFail("[\"\xF8\xBF\xBF\xBF\xBF\"]"));	// above U+10FFFF

	// Test UTF-16 decoding (deprecated)
	AT_TEST_ASSERT(Verify(u"[0]", Array{0}));

	// Test UTF-32 decoding (deprecated)
	AT_TEST_ASSERT(Verify(U"[0]", Array{0}));

	// A byte order mark is not allowed in the original JSON spec, but can be
	// optionally accepted per RFC 8259. We only allow it for UTF-8 and UTF-16.
	AT_TEST_ASSERT(Verify(u8"\uFEFF[0]", Array{0}));
	AT_TEST_ASSERT(Verify(u"\uFEFF[0]", Array{0}));
	AT_TEST_ASSERT(VerifyParseFail(U"\uFEFF[0]"));

	return 0;
}

