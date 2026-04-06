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

#ifndef f_VD2_VDDISPLAY_INTERNAL_CUSTOMEFFECTUTILSD3D_H
#define f_VD2_VDDISPLAY_INTERNAL_CUSTOMEFFECTUTILSD3D_H

#include <windows.h>
#include <vd2/system/error.h>
#include <vd2/system/vdstl.h>

struct ID3D11ShaderReflection;

class VDDCustomShaderCompileException final : public VDException {
public:
	VDDCustomShaderCompileException(HRESULT hr);
	VDDCustomShaderCompileException(const char *msg);
};

class VDDCustomEffectCompilerD3D {
	VDDCustomEffectCompilerD3D(const VDDCustomEffectCompilerD3D&) = delete;
	VDDCustomEffectCompilerD3D& operator=(const VDDCustomEffectCompilerD3D&) = delete;

public:
	VDDCustomEffectCompilerD3D();
	~VDDCustomEffectCompilerD3D();

	vdfastvector<uint8> Compile(const wchar_t *path, const char *entryPointName, const char *shaderProfile, bool d3d11, bool supportMinPrec);
	vdrefptr<ID3D11ShaderReflection> ReflectD3D11(const void *data, size_t len);
	void Load();
	void Unload();

private:
	class IncludeHandler;

	HMODULE mhmodD3D = nullptr;
	FARPROC mpD3DCompileFromFile = nullptr;
	FARPROC mpD3DReflect = nullptr;
};

#endif
