//	VirtualDub - Video processing and capture application
//	JSON I/O library
//	Copyright (C) 1998-2010 Avery Lee
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include <stdafx.h>
#include <vd2/vdjson/jsonreader.h>
#include <vd2/vdjson/jsonnametable.h>
#include <vd2/vdjson/jsonvalue.h>

#define DEBUG_TRACE (void)sizeof printf

///////////////////////////////////////////////////////////////////////////

VDJSONReader::VDJSONReader()
	: mNameBuffer(NULL)
	, mNameBufferIndex(0)
	, mNameBufferLength(0)
{
}

VDJSONReader::~VDJSONReader() {
	if (mNameBuffer)
		delete[] mNameBuffer;
}

bool VDJSONReader::Parse(const void *buf, size_t len, VDJSONDocument& doc) {
	mpNameTable = &doc.mNameTable;
	mpDocument = &doc;

	mpSrc = (const uint8 *)buf;
	mpSrcEnd = mpSrc + len;
	mbSrcError = false;
	mpInputBase = NULL;
	mpInputNext = NULL;
	mpInputEnd = NULL;

	mbUTF16Mode = false;
	mbUTF32Mode = false;

	// Check the sequence of null bytes at the beginning of the file to determine the encoding
	// (see RFC4627 section 3). Note that a byte order mark (BOM) is NOT allowed in JSON, and
	// that a valid UTF-8 JSON file can be two bytes (empty array or object). All valid UTF-16
	// files must be four bytes and all valid UTF-32 files must be at least eight bytes.
	if (len >= 4) {
		if (!(mpSrc[0] | mpSrc[1] | mpSrc[2])) {
			mbUTF32Mode = true;
			mbBigEndian = true;
		} else if (!(mpSrc[3] | mpSrc[1] | mpSrc[2])) {
			mbUTF32Mode = true;
			mbBigEndian = false;
		} else if (!(mpSrc[0] | mpSrc[2])) {
			mbUTF16Mode = true;
			mbBigEndian = true;
		} else if (!(mpSrc[1] | mpSrc[3])) {
			mbUTF16Mode = true;
			mbBigEndian = false;
		}
	}

	// Check for UTF-8 BOM. These are non-standard, but sometimes appear.
	if (len >= 3 && mpSrc[0] == 0xEF && mpSrc[1] == 0xBB && mpSrc[2] == 0xBF) {
		mpSrc += 3;
	} else if (len >= 2) {
		// check for UTF-16 BOM
		if (mpSrc[0] == 0xFE && mpSrc[1] == 0xFF) {
			mpSrc += 2;
			mbUTF16Mode = true;
			mbBigEndian = true;
		} else if (mpSrc[0] == 0xFF && mpSrc[1] == 0xFE) {
			mpSrc += 2;
			mbUTF16Mode = true;
			mbBigEndian = false;
		}

		if (mbUTF16Mode && ((mpSrcEnd - mpSrc) & 1))
			--mpSrcEnd;
	}

	return ParseDocument();
}

// We use an early strict definition of JSON that requires an object or array
// at the top level (RFC 4627). This is lifted in RFC 8259, which we don't
// support yet.
//
// JSON-text := object | array
// object := '{' member (',' member)* '}'
// member := string ':' value
// array := '[' value (',' value)* ']'
// value := 'false' | 'null' | 'true' | object | array | number | string
// number := '-'? int frac? exp?
// int := (0 | [1-9][0-9]*)
// frac := ('.' [0-9]+)
// exp := ([eE] ('+' | '-') [0-9]+)
// string := '"' char* '"'
// char := unescaped | escaped
// unescaped := U+0000 - U+0021 | U+0023 - U+005B | U+005D - U+10FFFF
// escaped := '\' (["/\bfnrt] | u[0-9a-fA-F]{4})

bool VDJSONReader::ParseDocument() {
	wchar_t c = GetNonWhitespaceChar();

	if (c == '{') {
		if (!ParseObject(mpDocument->mValue))
			return false;
	} else if (c == '[') {
		if (!ParseArray(mpDocument->mValue))
			return false;
	} else {
		return false;
	}

	return true;
}

bool VDJSONReader::ParseObject(VDJSONValue& obj) {
	obj.mType = VDJSONValue::kTypeObject;
	obj.mpObject = nullptr;

	VDJSONMember *tail = nullptr;

	wchar_t c = GetNonWhitespaceChar();
	if (c == L'}')
		return true;

	UngetChar();

	typedef vdhashmap<uint32, VDJSONValue *> Lookup;
	Lookup lookup;

	constexpr size_t kFastSize = 16;
	uint32 fastTokens[kFastSize];
	VDJSONValue *fastValues[kFastSize];
	size_t numFastValues = 0;

	for(;;) {
		if (GetNonWhitespaceChar() != L'"')
			return false;

		if (!ParseString())
			return false;

		uint32 nameToken = GetTokenForName();

		if (GetNonWhitespaceChar() != L':')
			return false;

		wchar_t c = GetNonWhitespaceChar();

		VDJSONValue val;
		if (!ParseValue(c, val))
			return false;

		if (numFastValues <= kFastSize) {
			size_t index = 0;

			while(index < numFastValues) {
				if (fastTokens[index] == nameToken) {
					*fastValues[index] = val;
					break;
				}

				++index;
			}

			if (index >= numFastValues) {
				++numFastValues;

				VDJSONValue *newValue = mpDocument->mPool.AddObjectMember(obj, nameToken, tail);
				*newValue = val;

				if (numFastValues <= kFastSize) {
					fastTokens[index] = nameToken;
					fastValues[index] = newValue;
				} else {
					for(size_t i = 0; i < kFastSize; ++i)
						lookup.insert(Lookup::value_type(fastTokens[i], fastValues[i]));

					lookup.insert(Lookup::value_type(nameToken, newValue));
				}
			}
		} else {
			std::pair<Lookup::iterator, bool> result(lookup.insert(Lookup::value_type(nameToken, (VDJSONValue *)nullptr)));
			if (result.second)
				result.first->second = mpDocument->mPool.AddObjectMember(obj, nameToken, tail);

			*result.first->second = val;
		}

		c = GetNonWhitespaceChar();
		if (c == L'}')
			break;

		if (c != L',')
			return false;
	}

	return true;
}

bool VDJSONReader::ParseArray(VDJSONValue& arr) {
	size_t arrayBase = mArrayStack.size();
	size_t arrayLen = arrayBase;

	wchar_t c = GetNonWhitespaceChar();
	if (c != L']') {
		for(;;) {
			VDJSONValue *val = mpDocument->mPool.AddValue();

			mArrayStack.push_back(val);
			++arrayLen;

			if (!ParseValue(c, *val))
				return false;

			c = GetNonWhitespaceChar();
			if (c == L']')
				break;

			if (c != L',')
				return false;

			c = GetNonWhitespaceChar();
		}
	}


	if (arrayLen != arrayBase) {
		mpDocument->mPool.AddArray(arr, &mArrayStack[arrayBase], arrayLen - arrayBase);
		mArrayStack.resize(arrayBase);
	} else
		mpDocument->mPool.AddArray(arr, nullptr, 0);

	return true;
}

bool VDJSONReader::ParseValue(VDJSONValue& val) {
	return ParseValue(GetNonWhitespaceChar(), val);
}

bool VDJSONReader::ParseValue(wchar_t c, VDJSONValue& val) {
	if (c == L'{') {
		if (!ParseObject(val))
			return false;
	} else if (c == L'[') {
		if (!ParseArray(val))
			return false;
	} else if (c == L'"') {
		if (!ParseString())
			return false;

		mpDocument->mPool.AddString(val, mNameBuffer, mNameBufferIndex);
	} else if (c == L't') {
		if (!Expect(L'r') || !Expect(L'u') || !Expect(L'e'))
			return false;

		val.Set(true);
	} else if (c == L'f') {
		if (!Expect(L'a') || !Expect(L'l') || !Expect(L's') || !Expect(L'e'))
			return false;

		val.Set(false);
	} else if (c == L'n') {
		if (!Expect(L'u') || !Expect(L'l') || !Expect(L'l'))
			return false;

		val.Set();
	} else if (c == L'-' || (c >= L'0' && c <= L'9')) {
		bool neg = false;
		bool isReal = false;

		ClearNameBuffer();
		uint64 v = 0;

		if (c == L'-') {
			neg = true;
			AddNameChar(c);
			c = GetChar();
		}

		if (c == L'0') {
			AddNameChar(c);
			c = GetChar();
		} else if (c >= L'1' && c <= L'9') {
			do {
				if (!isReal) {
					uint64 vNew = v * 10 + (int)(c - L'0');

					if (vNew < v)
						isReal = true;
					else
						v = vNew;
				}

				AddNameChar(c);
				c = GetChar();
			} while(c >= L'0' && c <= L'9');

			if (neg) {
				if (v > UINT64_C(0x8000'0000'0000'0000))
					isReal = true;
			} else {
				if (v > UINT64_C(0x7FFF'FFFF'FFFF'FFFF))
					isReal = true;
			}
		} else
			return false;

		if (c == L'.') {
			isReal = true;

			AddNameChar(c);

			// at least one fractional digit required
			c = GetChar();

			if (c < L'0' || c > L'9')
				return false;

			do {
				AddNameChar(c);
				c = GetChar();
			} while(c >= L'0' && c <= L'9');
		}

		if (c == L'e' || c == L'E') {
			isReal = true;

			AddNameChar(c);
			c = GetChar();

			// optional sign
			if (c == L'-' || c == L'+') {
				AddNameChar(c);
				c = GetChar();
			}

			do {
				AddNameChar(c);
				c = GetChar();
			} while(c >= L'0' && c <= L'9');
		}

		EndName();

		if (isReal)
			val.Set(wcstod(mNameBuffer, NULL));
		else if (neg)
			val.Set((sint64)(UINT64_C(0)-v));
		else
			val.Set((sint64)v);

		UngetChar();
	} else
		return false;

	return true;
}

bool VDJSONReader::ParseString() {
	ClearNameBuffer();

	for(;;) {
		wchar_t c = GetChar();

		if (c == L'"')
			break;

		if ((unsigned)c < 0x20)
			return false;

		if (c == L'\\') {
			c = GetChar();

			switch(c) {
				case L'"':
				case L'\\':
				case L'/':
					break;

				case L'b':
					c = (wchar_t)L'\b';
					break;

				case L'f':
					c = (wchar_t)L'\f';
					break;

				case L'n':
					c = (wchar_t)L'\n';
					break;

				case L'r':
					c = (wchar_t)L'\r';
					break;

				case L't':
					c = (wchar_t)L'\t';
					break;

				case L'u': {
					uint32 ch = 0;

					// Character escapes in JSON are always UTF-16 encoded, so any escaped
					// characters outside of the BMP must be encoded as an adjacent pair
					// of escapes.
					for(int pass = 0; pass < 2; ++pass) {
						uint32 hexVal = 0;

						for(int i=0; i<4; ++i) {
							c = GetChar();

							hexVal <<= 4;

							if (c >= L'0' && c <= '9')
								hexVal += (uint32)(c - L'0');
							else if (c >= L'a' && c <= 'f')
								hexVal += (uint32)(c - L'a') + 10;
							else if (c >= L'A' && c <= 'F')
								hexVal += (uint32)(c - L'A') + 10;
							else
								return false;
						}

						if (pass == 0) {
							// save off the first character
							ch = hexVal;

							// if the escaped character is not a surrogate, done
							if (ch < 0xD800 || ch >= 0xE000) {
								c = (wchar_t)ch;
								break;
							}

							// starting with a low surrogate is invalid
							if (ch >= 0xDC00)
								return false;

							// check that another escape immediately follows
							if (GetChar() != L'\\')
								return false;

							if (GetChar() != L'u')
								return false;
						} else {
							// second escape must be low surrogate
							if (hexVal < 0xDC00 || hexVal >= 0xE000)
								return false;

							// store or decode surrogates
							if constexpr (sizeof(wchar_t) >= 4) {
								c = (wchar_t)(0x10000 + ((ch - 0xD800) << 10) + (hexVal - 0xDC00));
							} else {
								AddNameChar((wchar_t)ch);
								c = (wchar_t)hexVal;
							}
						}
					}
					break;
				}

				default:
					// invalid escape character
					return false;
			}
		}

		AddNameChar(c);
	}

	EndName();

	return true;
}

bool VDJSONReader::Expect(wchar_t expected) {
	wchar_t c = GetChar();

	if (c != expected)
		return false;

	return true;
}

void VDJSONReader::ClearNameBuffer() {
	mNameBufferIndex = 0;
}

void VDJSONReader::ExpandNameCharBuffer() {
	int newLen = mNameBufferLength ? mNameBufferLength + (mNameBufferLength >> 1) : 64;
	wchar_t *newbuf = new wchar_t[newLen];
	if (mNameBufferIndex)
		memcpy(newbuf, mNameBuffer, mNameBufferIndex * sizeof(mNameBuffer[0]));
	delete[] mNameBuffer;
	mNameBuffer = newbuf;
	mNameBufferLength = newLen;
}

void VDJSONReader::EndName() {
	AddNameChar(0);
	--mNameBufferIndex;
}

bool VDJSONReader::IsWhitespaceChar(wchar_t c) {
	return (c == 0x20 || c == 0x09 || c == 0x0d || c == 0x0a);
}

void VDJSONReader::UngetChar() {
	if (mpInputNext != mpInputBase) {
		--mpInputNext;
	}
}

wchar_t VDJSONReader::GetNonWhitespaceChar() {
	for(;;) {
		wchar_t c = GetChar();

		if (!IsWhitespaceChar(c))
			return c;
	}
}

wchar_t VDJSONReader::GetChar() {
	if (mpInputNext == mpInputEnd)
		return GetCharSlow();

	wchar_t c = *mpInputNext++;

	return c;
}

wchar_t VDJSONReader::GetCharSlow() {
	if (mbSrcError)
		return 0;
	
	if (mpSrc != mpSrcEnd) {
		const uint8 *VDRESTRICT src = mpSrc;
		const uint8 *const VDRESTRICT srcEnd = mpSrcEnd;
		wchar_t *VDRESTRICT dst = mInputBuffer;

		int len = 0;
		bool encodingError = false;

		if (mbUTF32Mode) {
			if constexpr (sizeof(wchar_t) >= 4) {
				int tc = (srcEnd - src) >> 2;

				if (tc > kInputBufferSize - len)
					tc = kInputBufferSize - len;

				if (mbBigEndian) {
					for(int i=0; i<tc; ++i) {
						dst[i+len] = (wchar_t)(((uint32)src[0] << 24) + ((uint32)src[1] << 16) + ((uint32)src[2] << 8) + src[3]);
						src += 4;
					}
				} else {
					memcpy(dst + len, src, tc * sizeof(wchar_t));
					src += tc*4;
				}

				// validate code points
				for(int i=0; i<tc; ++i) {
					uint32 c = dst[i + len];

					if ((c - 0xD800) < 0x0800 || c >= 0x110000) {
						encodingError = true;
						tc = i;
						break;
					}
				}

				len += tc;
			} else {
				if (mbBigEndian) {
					while(len < kInputBufferSize && srcEnd - src >= 4) {
						uint32 c = ((uint32)src[0] << 24) + ((uint32)src[1] << 16) + ((uint32)src[2] << 8) + src[3];
						src += 4;

						// validate code point
						if ((c - 0xD800) < 0x0800 || c >= 0x110000) {
							encodingError = true;
							break;
						}

						// check if we need to fragment
						if (c >= 0x10000) {
							// check if have space for the second surrogate
							if (len >= kInputBufferSize - 1) {
								// MERGE NOTE: retry this scalar after the UTF-16 refill.
								src -= 4;
								break;
							}

							// write the first surrogate
							c -= 0x10000;

							dst[len++] = 0xD800 + (c >> 10);
							c = (wchar_t)(0xDC00 + (c & 0x03FF));
						}

						dst[len++] = (wchar_t)c;
					}
				} else {
					while(len < kInputBufferSize && srcEnd - src >= 4) {
						uint32 c = ((uint32)src[3] << 24) + ((uint32)src[2] << 16) + ((uint32)src[1] << 8) + src[0];
						src += 4;

						// validate code point
						if ((c - 0xD800) < 0x0800 || c >= 0x110000) {
							encodingError = true;
							break;
						}

						// check if we need to fragment
						if (c >= 0x10000) {
							// check if have space for the second surrogate
							if (len >= kInputBufferSize - 1) {
								// MERGE NOTE: retry this scalar after the UTF-16 refill.
								src -= 4;
								break;
							}

							// write the first surrogate
							c -= 0x10000;

							dst[len++] = 0xD800 + (c >> 10);
							c = (wchar_t)(0xDC00 + (c & 0x03FF));
						}

						dst[len++] = (wchar_t)c;
					}
				}
			}
		} else if (mbUTF16Mode) {
			if constexpr (sizeof(wchar_t) > 2) {		// UTF-16 -> UTF-32 conversion
				if (mbBigEndian) {
					while(len < kInputBufferSize && (srcEnd - src) >= 2) {
						uint32 c0 = ((uint32)src[0] << 8) + src[1];
						src += 2;

						if ((c0 - 0xD800) < 0x0800) {
							if (c0 >= 0xDC00 || srcEnd - src < 2) {
								encodingError = true;
								break;
							}

							uint32 c1 = ((uint32)src[0] << 8) + src[1];
							src += 2;

							if ((c1 - 0xDC00) >= 0x0400) {
								encodingError = true;
								break;
							}

							c0 = 0x10000 + (c0 << 10) + c1 - ((0xD800 << 10) + 0xDC00);
						}

						dst[len++] = c0;
					}
				} else {
					while(len < kInputBufferSize && (srcEnd - src) >= 2) {
						uint32 c0 = ((uint32)src[1] << 8) + src[0];
						src += 2;

						if ((c0 - 0xD800) < 0x0800) {
							if (c0 >= 0xDC00 || srcEnd - src < 2) {
								encodingError = true;
								break;
							}

							uint32 c1 = ((uint32)src[1] << 8) + src[0];
							src += 2;

							if ((c1 - 0xDC00) >= 0x0400) {
								encodingError = true;
								break;
							}

							c0 = 0x10000 + (c0 << 10) + c1 - ((0xD800 << 10) + 0xDC00);
						}

						dst[len++] = c0;
					}
				}
			} else {
				int tc = (srcEnd - src) >> 1;

				if (tc > kInputBufferSize - len)
					tc = kInputBufferSize - len;

				if (mbBigEndian) {
					for(int i=0; i<tc; ++i) {
						mInputBuffer[i+len] = (wchar_t)((src[0] << 8) + src[1]);
						src += 2;
					}
				} else {
					memcpy(dst + len, src, tc * sizeof(wchar_t));
					src += tc*2;
				}

				len += tc;
			}
		} else {
			while(len < kInputBufferSize && src != srcEnd) {
				uint8 c = *src++;
				wchar_t d = c;

				if (c >= 0x80) {
					if ((c >= 0x80 && c < 0xC2) || c >= 0xF5) {		// invalid: follower without leader or too high of a code point
						encodingError = true;
						break;
					} else if (c >= 0xC2 && c <= 0xDF) {		// U+0080 to U+07FF
						if (src == srcEnd) {
							encodingError = true;
							break;
						}

						uint8 x0 = *src++;
						if (x0 < 0x80 || x0 > 0xBF) {
							encodingError = true;
							break;
						}

						d = ((c - 0xC0) << 6) + (x0 - 0x80);
					} else if (c >= 0xE0 && c <= 0xEF) {	// U+0800 to U+FFFF
						if (srcEnd - src < 2) {
							encodingError = true;
							break;
						}

						uint8 x0 = *src++;
						uint8 x1 = *src++;
						if (x0 < 0x80 || x0 > 0xBF || x1 < 0x80 || x1 > 0xBF) {
							encodingError = true;
							break;
						}

						d = ((c - 0xE0) << 12) + (((wchar_t)x0 - 0x80) << 6) + (wchar_t)(x1 - 0x80);

						// reject invalid code points
						if (d < 0x0800 || (uint32)(d - 0xD800) < 0x0800) {
							encodingError = true;
							break;
						}
					} else if (c >= 0xF0 && c <= 0xF7) {
						if (srcEnd - src < 3) {
							encodingError = true;
							break;
						}

						uint8 x0 = *src++;
						uint8 x1 = *src++;
						uint8 x2 = *src++;
						if (x0 < 0x80 || x0 > 0xBF || x1 < 0x80 || x1 > 0xBF || x2 < 0x80 || x2 > 0xBF) {
							encodingError = true;
							break;
						}

						uint32 e = ((uint32)(c - 0xF0) << 18) + (((uint32)x0 - 0x80) << 12) + (((uint32)x1 - 0x80) << 6) + (uint32)(x2 - 0x80);
						if (e < 0x10000 || e >= 0x110000) {
							encodingError = true;
							break;
						}

						// fragment into surrogates if required
						if constexpr (sizeof(wchar_t) <= 2) {
							if (len >= kInputBufferSize - 1) {
								// MERGE NOTE: retry this scalar after the UTF-16 refill.
								src -= 4;
								break;
							}

							e -= 0x10000;

							dst[len++] = (wchar_t)(0xD800 + (e >> 10));
							d = (wchar_t)(0xDC00 + (e & 0x03FF));
						} else {
							// MERGE NOTE: POSIX wchar_t stores the full scalar.
							d = (wchar_t)e;
						}
					}
				}

				dst[len++] = d;
			}
		}

		mpSrc = src;

		mpInputBase = mInputBuffer;
		mpInputNext = mInputBuffer;
		mpInputEnd = &mInputBuffer[len];

		if (encodingError) {
			mpInputNext = mpInputEnd;
			mbSrcError = true;
			return 0;
		}

		if (len)
			return *mpInputNext++;
	}

	mpInputBase = NULL;
	mpInputNext = NULL;
	mpInputEnd = NULL;
	return 0;
}

uint32 VDJSONReader::GetTokenForName() {
	return mpNameTable->AddName(mNameBuffer, wcslen(mNameBuffer));
}
