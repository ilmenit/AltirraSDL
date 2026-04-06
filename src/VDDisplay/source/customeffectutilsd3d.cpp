//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2025 Avery Lee
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
#include <d3dcommon.h>
#include <d3d10.h>
#include <d3dcompiler.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/refcount.h>
#include <vd2/system/w32assist.h>
#include <vd2/VDDisplay/internal/customeffectutilsd3d.h>

////////////////////////////////////////////////////////////////////////////////

VDDCustomShaderCompileException::VDDCustomShaderCompileException(HRESULT hr)
	: VDException("unknown error (%08X)", (unsigned)hr)
{
}

VDDCustomShaderCompileException::VDDCustomShaderCompileException(const char *msg)
	: VDException(msg)
{
}

////////////////////////////////////////////////////////////////////////////////

class VDDCustomEffectCompilerD3D::IncludeHandler final : public ID3D10Include {
public:
	IncludeHandler(VDStringSpanW defaultBasePath)
		: mDefaultBasePath(defaultBasePath)
	{
	}

	COM_DECLSPEC_NOTHROW HRESULT STDMETHODCALLTYPE Open(D3D10_INCLUDE_TYPE includeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID *ppData, UINT *pBytes);
	COM_DECLSPEC_NOTHROW HRESULT STDMETHODCALLTYPE Close(LPCVOID pData);

private:
	struct IncludeHeader {
		wchar_t *mpBasePath;
	};

	VDStringW mDefaultBasePath;
};

COM_DECLSPEC_NOTHROW HRESULT STDMETHODCALLTYPE VDDCustomEffectCompilerD3D::IncludeHandler::Open(D3D10_INCLUDE_TYPE includeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID *ppData, UINT *pBytes) {
	const wchar_t *basePath = pParentData ? ((IncludeHeader *)((const char *)pParentData - sizeof(IncludeHeader)))->mpBasePath : mDefaultBasePath.c_str();
	const auto newPath = VDMakePath(VDStringSpanW(basePath), VDTextAToW(pFileName));

	*ppData = nullptr;
	*pBytes = 0;

	try {
		VDFile f(newPath.c_str());
		sint64 len = f.size();

		if (len < 0 || len > 0xFFFFFF)
			return E_FAIL;

		const VDStringSpanW newBasePath = VDFileSplitPathLeftSpan(newPath);

		uint32 len32 = (uint32)len;
		uint32 len32Aligned = (len32 + 3) & ~3;
		vdautoblockptr mem(malloc(len32Aligned + (newBasePath.size() + 1) * sizeof(wchar_t) + sizeof(IncludeHeader)));
		if (!mem)
			return E_OUTOFMEMORY;

		IncludeHeader *hdr = (IncludeHeader *)mem.get();
		char *buf = (char *)(hdr + 1);
		hdr->mpBasePath = (wchar_t *)(buf + len32Aligned);
		memcpy(hdr->mpBasePath, newBasePath.data(), newBasePath.size() * sizeof(wchar_t));
		hdr->mpBasePath[newBasePath.size()] = 0;

		f.read(buf, len32);

		mem.release();
		*ppData = buf;
		*pBytes = len32;
	} catch(const VDException&) {
		return E_FAIL;
	}

	return S_OK;
}

COM_DECLSPEC_NOTHROW HRESULT STDMETHODCALLTYPE VDDCustomEffectCompilerD3D::IncludeHandler::Close(LPCVOID pData) {
	if (pData) {
		free(((IncludeHeader *)pData) - 1);
	}

	return S_OK;
}

////////////////////////////////////////////////////////////////////////////////

VDDCustomEffectCompilerD3D::VDDCustomEffectCompilerD3D() {
}

VDDCustomEffectCompilerD3D::~VDDCustomEffectCompilerD3D() {
	Unload();
}

vdfastvector<uint8> VDDCustomEffectCompilerD3D::Compile(const wchar_t *path, const char *entryPointName, const char *shaderProfile, bool supportMinPrec, bool d3d11) {
	typedef HRESULT (WINAPI *tpD3DCompileFromFile)(LPCWSTR, const D3D10_SHADER_MACRO *, ID3D10Include *, LPCSTR, LPCSTR, UINT, UINT, ID3D10Blob **, ID3D10Blob **);

	Load();

	IncludeHandler includeHandler(VDFileSplitPathLeft(VDStringSpanW(path)));

	vdfastvector<D3D10_SHADER_MACRO> macros;

	if (d3d11) {
		if (supportMinPrec) {
			macros.push_back(D3D10_SHADER_MACRO { "half", "min16float" });
			macros.push_back(D3D10_SHADER_MACRO { "half2", "min16float2" });
			macros.push_back(D3D10_SHADER_MACRO { "half3", "min16float3" });
			macros.push_back(D3D10_SHADER_MACRO { "half4", "min16float4" });
			macros.push_back(D3D10_SHADER_MACRO { "half2x2", "min16float2x2" });
			macros.push_back(D3D10_SHADER_MACRO { "half2x3", "min16float2x3" });
			macros.push_back(D3D10_SHADER_MACRO { "half2x4", "min16float2x4" });
			macros.push_back(D3D10_SHADER_MACRO { "half3x2", "min16float3x2" });
			macros.push_back(D3D10_SHADER_MACRO { "half3x3", "min16float3x3" });
			macros.push_back(D3D10_SHADER_MACRO { "half3x4", "min16float3x4" });
			macros.push_back(D3D10_SHADER_MACRO { "half4x2", "min16float4x2" });
			macros.push_back(D3D10_SHADER_MACRO { "half4x3", "min16float4x3" });
			macros.push_back(D3D10_SHADER_MACRO { "half4x4", "min16float4x4" });
		}

		macros.emplace_back("HLSL_4", "1");
	}

	macros.emplace_back(nullptr, nullptr);

	vdrefptr<ID3D10Blob> outputBlob;
	vdrefptr<ID3D10Blob> errorMessages;
	HRESULT hr = ((tpD3DCompileFromFile)mpD3DCompileFromFile)(
		path,
		macros.data(),
		&includeHandler,
		entryPointName,
		shaderProfile,
		D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY,
		0,
		~outputBlob,
		~errorMessages);

	if (FAILED(hr) || !outputBlob) {
		if (errorMessages) {
			const char *str = (const char *)errorMessages->GetBufferPointer();
			const size_t len = errorMessages->GetBufferSize();

			VDStringSpanA errors(str, str + len);
			errors = errors.subspan(0, errors.find((char)0));

			VDCharMaskA linebreaks("\r\n");

			VDStringSpanA errorParser = errors;
			while(!errorParser.empty()) {
				auto eolPos = errorParser.find_first_of(linebreaks);

				VDStringSpanA line(errorParser.subspan(0, eolPos));
				auto pos = line.find("error X");

				// annoyingly, the compiler can put the error at the start of a line
				if (pos != line.npos && (!pos || line[pos-1] == ' ' || line[pos-1] == '\n'))
					throw VDDCustomShaderCompileException(VDStringA(line).c_str());

				if (eolPos == errorParser.npos)
					break;

				errorParser = errorParser.subspan(eolPos + 1);
			}

			errors = errors.subspan(0, errors.find('\r'));
			errors = errors.subspan(0, errors.find('\n'));

			throw VDDCustomShaderCompileException(VDStringA(errors).c_str());
		}

		throw VDDCustomShaderCompileException(hr);
	}

	const size_t len = outputBlob->GetBufferSize();
	vdfastvector<uint8> data(len);

	memcpy(data.data(), outputBlob->GetBufferPointer(), len);

	return data;
}

vdrefptr<ID3D11ShaderReflection> VDDCustomEffectCompilerD3D::ReflectD3D11(const void *data, size_t len) {
	typedef HRESULT (WINAPI *tpD3DReflect)(LPCVOID, SIZE_T, REFIID, void **ppReflector);

	Load();

	vdrefptr<ID3D11ShaderReflection> refl;
	HRESULT hr = ((tpD3DReflect)mpD3DReflect)(data, len, IID_ID3D11ShaderReflection, (void **)~refl);

	if (FAILED(hr) || !refl)
		throw VDException(L"Cannot reflect shader (error code %08X)", (unsigned)hr);

	return refl;
}

void VDDCustomEffectCompilerD3D::Load() {
	if (!mhmodD3D) {
		mhmodD3D = VDLoadSystemLibraryW32("d3dcompiler_47.dll");

		if (mhmodD3D) {
			mpD3DCompileFromFile = GetProcAddress(mhmodD3D, "D3DCompileFromFile");
			mpD3DReflect = GetProcAddress(mhmodD3D, "D3DReflect");
		}

		if (!mpD3DCompileFromFile || !mpD3DReflect)
			throw VDException("Cannot compile shader as d3dcompiler_47.dll is not available");
	}
}

void VDDCustomEffectCompilerD3D::Unload() {
	if (mhmodD3D) {
		FreeLibrary(mhmodD3D);
		mhmodD3D = nullptr;
		mpD3DCompileFromFile = nullptr;
		mpD3DReflect = nullptr;
	}
}

