//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2014 Avery Lee
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
#include <numeric>
#include <vd2/system/binary.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/registry.h>
#include <vd2/system/thread.h>
#include <vd2/system/date.h>
#include <vd2/system/zip.h>
#include <at/atcore/vfs.h>
#include "firmwaremanager.h"
#include "oshelper.h"
#include "resource.h"

bool g_ATFirmwarePathPortabilityEnabled;

static constexpr const char *kATFirmwareTypeNames[]={
	"",
	"kernel800_osa",
	"kernel800_osb",
	"kernelxl",
	"kernelxegs",
	"kernel5200",
	"kernel1200xl",
	"basic",
	"5200cart",
	"u1mb",
	"myide",
	"myide2",
	"side",
	"side2",
	"kmkjzide",
	"kmkjzide2",
	"kmkjzide2_sdx",
	"blackbox",
	"game",
	"mio",
	"850handler",
	"850relocator",
	"1030firmware",
	"810",
	"happy810",
	"810archiver",
	"1050",
	"usdoubler",
	"speedy1050",
	"happy1050",
	"superarchiver",
	"toms1050",
	"tygrys1050",
	"1050duplicator",
	"indusgt",
	"1050turbo",
	"1050turboii",
	"xf551",
	"atr8000",
	"percom",
	"rapidus_flash",
	"rapidus_corepbi",
	"isplate",
	"warpos",
	"810turbo",
	"amdc",
	"percom_at",
	"percom_atspd",
	"815",
	"side3",
	"1090firmware",
	"1090charset",
	"bit3firmware",
	"bit3charset",
	"1400xlhandler",
	"1450xldiskhandler",
	"1450xldiskcontroller",
	"1450xltongdiskcontroller",
	"1030irom",
	"1030xrom",
	"835",
	"820",
	"1025",
	"1029",
	"850",
	"blackboxfloppy",
	"speedyxf",
	"karinmaxidrive",
};

VDASSERTCT(vdcountof(kATFirmwareTypeNames) == kATFirmwareTypeCount);

bool ATIsKernelFirmwareType(ATFirmwareType type) {
	switch(type) {
		case kATFirmwareType_Kernel800_OSA:
		case kATFirmwareType_Kernel800_OSB:
		case kATFirmwareType_KernelXL:
		case kATFirmwareType_KernelXEGS:
		case kATFirmwareType_Kernel5200:
		case kATFirmwareType_Kernel1200XL:
			return true;

		default:
			return false;
	}
}

bool ATIsBasicFirmwareType(ATFirmwareType type) {
	switch(type) {
		case kATFirmwareType_Basic:
			return true;

		default:
			return false;
	}
}

const char *ATGetFirmwareTypeName(ATFirmwareType type) {
	return kATFirmwareTypeNames[type];
}

ATFirmwareType ATGetFirmwareTypeFromName(const char *type) {
	for(int i=1; i<(int)kATFirmwareTypeCount; ++i) {
		if (!strcmp(type, kATFirmwareTypeNames[i]))
			return (ATFirmwareType)i;
	}

	return kATFirmwareType_Unknown;
}

const char *ATGetSpecificFirmwareTypeKey(ATSpecificFirmwareType ft) {
	switch(ft) {
	case kATSpecificFirmwareType_BASICRevA:		return "BASIC rev A";
	case kATSpecificFirmwareType_BASICRevB:		return "BASIC rev B";
	case kATSpecificFirmwareType_BASICRevC:		return "BASIC rev C";
	case kATSpecificFirmwareType_OSA:			return "OS-A";
	case kATSpecificFirmwareType_OSB:			return "OS-B";
	case kATSpecificFirmwareType_XLOSr2:		return "XLOS rev 2";
	case kATSpecificFirmwareType_XLOSr4:		return "XLOS rev 4";
	default:
		return nullptr;
	}
}

bool ATIsSpecificFirmwareTypeCompatible(ATFirmwareType type, ATSpecificFirmwareType specificType) {
	switch(specificType) {
		case kATSpecificFirmwareType_BASICRevA:
		case kATSpecificFirmwareType_BASICRevB:
		case kATSpecificFirmwareType_BASICRevC:
			return type == kATFirmwareType_Basic;

		case kATSpecificFirmwareType_OSA:
		case kATSpecificFirmwareType_OSB:
			return type == kATFirmwareType_Kernel800_OSA || type == kATFirmwareType_Kernel800_OSB;

		case kATSpecificFirmwareType_XLOSr2:
		case kATSpecificFirmwareType_XLOSr4:
			return type == kATFirmwareType_KernelXL || type == kATFirmwareType_KernelXEGS || type == kATFirmwareType_Kernel1200XL;
	}

	return false;
}

void ATSetFirmwarePathPortabilityMode(bool portable) {
	g_ATFirmwarePathPortabilityEnabled = portable;
}

uint64 ATGetFirmwareIdFromPath(const wchar_t *path) {
	VDStringW relPath;
	if (!VDFileIsRelativePath(path)) {
		relPath = VDFileGetRelativePath(VDGetProgramPath().c_str(), path, g_ATFirmwarePathPortabilityEnabled);

		if (!relPath.empty())
			path = relPath.c_str();
	}

	uint64 hash = 14695981039346656037ull;

	while(const wchar_t c = towlower(*path++))
		hash = (hash ^ (unsigned)c) * 1099511628211ull;

	return hash | 0x8000000000000000ull;
}

bool ATLoadInternalFirmware(uint64 id, void *dst, uint32 offset, uint32 len, bool *changed, uint32 *actualLen, vdfastvector<uint8> *dstbuf, bool *isUsable) {
	static const uint32 kResourceIds[]={
		IDR_NOKERNEL,
		IDR_KERNEL,
		IDR_KERNELXL,
		0,
		IDR_BASIC,
		IDR_5200KERNEL,
		IDR_NOCARTRIDGE,
		IDR_U1MBBIOS,
		IDR_NOHDBIOS,
		IDR_850RELOCATOR,
		IDR_850HANDLER,
		IDR_1030HANDLER,
		IDR_NOMIO,
		IDR_NOBLACKBOX,
		IDR_NOGAME,
		IDR_RAPIDUSFLASH,
		IDR_RAPIDUSPBI16,
		IDR_KERNEL816,
		0,
		0
	};

	VDASSERTCT(vdcountof(kResourceIds) == kATFirmwareId_PredefCount);

	if (id >= kATFirmwareId_PredefCount1 || id == kATFirmwareId_Kernel_HLE)
		return false;

	if (isUsable) {
		bool usable = true;

		switch(id) {
			case kATFirmwareId_Invalid:
			case kATFirmwareId_NoKernel:
			case kATFirmwareId_5200_NoCartridge:
			case kATFirmwareId_NoMIO:
			case kATFirmwareId_NoBlackBox:
			case kATFirmwareId_NoGame:
				usable = false;
				break;
		}

		*isUsable = usable;
	}

	uint32 resId = id ? kResourceIds[id - 1] : 0;

	switch(resId) {
		default:
			// uncompressed
			if (dstbuf)
				return ATLoadKernelResource(resId, *dstbuf);
			else
				return ATLoadKernelResource(resId, dst, offset, len, true);

		case 0:
			break;

		case IDR_U1MBBIOS:
		case IDR_NOMIO:
		case IDR_NOBLACKBOX:
		case IDR_RAPIDUSFLASH:
			// compressed
			break;
	}

	vdfastvector<uint8> buffer;

	if (id == kATFirmwareId_1090Charset) {
		if (!ATLoadKernelResource(IDR_KERNELXL, buffer) || buffer.size() != 16384)
			return false;

		// normal charset
		memmove(buffer.data(), buffer.data() + 0x2000, 0x0400);

		// int'l charset
		memmove(buffer.data() + 0x0400, buffer.data() + 0xC00, 0x0400);

		buffer.resize(0x0800);
	} else if (resId)
		ATLoadKernelResourceLZPacked(resId, buffer);

	if (dstbuf) {
		if (changed && *dstbuf != buffer)
			*changed = true;

		*dstbuf = buffer;
	} else {
		if (offset >= buffer.size())
			return false;

		size_t avail = buffer.size() - offset;
		if (len > avail)
			len = (uint32)avail;

		if (changed && memcmp(dst, buffer.data() + offset, len))
			*changed = true;

		memcpy(dst, buffer.data() + offset, len);
	}

	if (actualLen)
		*actualLen = len;

	return true;
}

ATFirmwareManager::ATFirmwareManager() {
	// mark specific firmware cache uninitialized
	for(uint64& id : mSpecificFirmwares) {
		id = 0xFFFF;
	}

	ATVFSSetSpecialProtocolHandler(std::bind_front(&ATFirmwareManager::GetVFSSpecialFirmware, this));
}

ATFirmwareManager::~ATFirmwareManager() {
}

bool ATFirmwareManager::GetFirmwareInfo(uint64 id, ATFirmwareInfo& fwinfo) const {
	if (!id)
		return false;

	if (id < kATFirmwareId_Custom) {
		if (id >= kATFirmwareId_PredefCount1)
			return false;

		if (id == kATFirmwareId_Kernel_HLE)
			return false;

		static const struct {
			bool mbAutoselect;
			bool mbVisible;
			ATFirmwareType mType;
			const wchar_t *mpName;
		} kPredefFirmwares[]={
			{ false, false, kATFirmwareType_KernelXL, L"NoKernel" },
			{ true, true, kATFirmwareType_Kernel800_OSB, L"AltirraOS for 400/800" },
			{ true, true, kATFirmwareType_KernelXL,  L"AltirraOS for XL/XE/XEGS" },
			{ false, false, kATFirmwareType_KernelXL, L"AltirraOS HLE" },
			{ true, true, kATFirmwareType_Basic, L"Altirra BASIC" },
			{ true, true, kATFirmwareType_Kernel5200, L"AltirraOS for 5200" },
			{ true, false, kATFirmwareType_5200Cartridge, L"No5200" },
			{ true, true, kATFirmwareType_U1MB, L"Altirra U1MB Recovery OS" },
			{ true, false, kATFirmwareType_KMKJZIDE, L"Altirra NoBIOS for KMK/JZ IDE" },
			{ true, false, kATFirmwareType_850Relocator, L"Altirra 850 Relocator Firmware" },
			{ true, false, kATFirmwareType_850Handler, L"Altirra 850 Handler Firmware" },
			{ true, true, kATFirmwareType_1030Firmware, L"Altirra 1030 Modem Download Image" },
			{ true, false, kATFirmwareType_MIO, L"Altirra NoFirmware for MIO" },
			{ true, false, kATFirmwareType_BlackBox, L"Altirra NoFirmware for BlackBox" },
			{ true, false, kATFirmwareType_Game, L"Altirra placeholder NoGame" },
			{ true, true, kATFirmwareType_RapidusFlash, L"Altirra Rapidus Bootstrap Flash" },
			{ true, true, kATFirmwareType_RapidusCorePBI, L"Altirra Rapidus Bootstrap 65816 PBI Firmware" },
			{ false, true, kATFirmwareType_KernelXL, L"AltirraOS for 65C816" },
			{ true, true, kATFirmwareType_1090Firmware, L"Altirra 1090 CVC Firmware" },
			{ true, true, kATFirmwareType_1090Charset, L"Altirra 1090 CVC Charset" },
		};

		VDASSERTCT(vdcountof(kPredefFirmwares) == kATFirmwareId_PredefCount);

		fwinfo.mId = id;
		fwinfo.mbAutoselect = kPredefFirmwares[id - 1].mbAutoselect;
		fwinfo.mbVisible = kPredefFirmwares[id - 1].mbVisible;
		fwinfo.mType = kPredefFirmwares[id - 1].mType;
		fwinfo.mName = kPredefFirmwares[id - 1].mpName;
		fwinfo.mPath.clear();
		fwinfo.mFlags = 0;

		VDStringW s;
		if (id == kATFirmwareId_Kernel_LLEXL || id ==kATFirmwareId_Kernel_816) {
			if (mKernelXLVersion.empty()) {
				vdfastvector<uint8> buf(16384, 0);
				ATLoadInternalFirmware(kATFirmwareId_Kernel_LLEXL, buf.data(), 0, buf.size());

				// AltirraOS XL/XE/XEGS places the version string at the top of the
				// self-test area ($57F8).
				mKernelXLVersion = L"AltirraOS ";

				for(uint32 i = 0xD7F8 - 0xC000; i < 0xD800 - 0xC000; ++i) {
					uint8 c = buf[i];

					if (c >= 0x21 && c < 0x7F)
						mKernelXLVersion += (char)c;
					else
						break;
				}

				mKernel816Version = mKernelXLVersion;
				mKernelXLVersion += L" for XL/XE/XEGS";
				mKernel816Version += L" for 65C816";
			}

			fwinfo.mName = (id == kATFirmwareId_Kernel_LLEXL) ? mKernelXLVersion : mKernel816Version;
		} else if (id == kATFirmwareId_Kernel_LLE) {
			if (mKernelVersion.empty()) {
				vdfastvector<uint8> buf(10240, 0);
				ATLoadInternalFirmware(id, buf.data(), 0, buf.size());

				// Standard AltirraOS currently places the memo pad version string
				// at $E4A6, right below KnownRTS. The first 10 chars are 'AltirraOS '.
				mKernelVersion = L"AltirraOS ";

				for(uint32 i = 0xE4B0 - 0xD800; i < 0xE4C0 - 0xD800; ++i) {
					uint8 c = buf[i];

					if (c >= 0x21 && c < 0x7F)
						mKernelVersion += (char)c;
					else
						break;
				}

				mKernelVersion += L" for 400/800";
			}

			fwinfo.mName = mKernelVersion;
		} else if (id == kATFirmwareId_Basic_ATBasic) {
			if (mBASICVersion.empty()) {
				vdfastvector<uint8> buf(8192, 0);
				ATLoadInternalFirmware(id, buf.data(), 0, buf.size());

				// Search for the string within the first page: 'Altirra 8K BASIC '.
				mBASICVersion = L"Altirra BASIC";

				for(uint32 i = 0; i < 256; ++i) {
					if (!memcmp(&buf[i], "Altirra 8K BASIC ", 17)) {
						mBASICVersion += ' ';

						for(uint32 j=0; j<16; ++j) {
							uint8 c = buf[i+j+17];

							if (c >= 0x21 && c < 0x7F)
								mBASICVersion += (char)c;
							else
								break;
						}
						break;
					}
				}
			}

			fwinfo.mName = mBASICVersion;
		}

		return true;
	} else {
		VDStringA name;
		name.sprintf("Firmware\\Available\\%016llX", id);

		VDRegistryAppKey key(name.c_str(), false, false);
		if (!key.isReady())
			return false;

		fwinfo.mId = id;
		fwinfo.mbAutoselect = true;
		fwinfo.mbVisible = true;
		fwinfo.mType = kATFirmwareType_Unknown;
		
		if (!key.getString("Path", fwinfo.mPath))
			return false;

		if (VDFileIsRelativePath(fwinfo.mPath.c_str()))
			fwinfo.mPath = VDMakePath(VDGetProgramPath().c_str(), fwinfo.mPath.c_str());

		if (!key.getString("Name", fwinfo.mName))
			return false;

		VDStringA type;
		if (!key.getString("Type", type))
			return false;

		fwinfo.mType = ATGetFirmwareTypeFromName(type.c_str());

		fwinfo.mFlags = key.getInt("Flags");
		return true;
	}
}

void ATFirmwareManager::GetFirmwareList(vdvector<ATFirmwareInfo>& firmwares) const {
	// add predefined
	firmwares.resize(kATFirmwareId_PredefCount);
	for(uint32 i=0; i<kATFirmwareId_PredefCount; ++i) {
		if (i+1 == kATFirmwareId_Kernel_HLE)
			continue;

		VDVERIFY(GetFirmwareInfo(i+1, firmwares[i]));
	}

	// add custom
	VDRegistryAppKey key("Firmware\\Available", false, false);

	VDRegistryKeyIterator it(key);
	while(const char *name = it.Next()) {
		unsigned long long id;
		char dummy;
		if (1 != sscanf(name, "%llx%c", &id, &dummy))
			continue;

		if (id < (1ull << 63))
			continue;

		ATFirmwareInfo fw;
		if (GetFirmwareInfo(id, fw))
			firmwares.push_back(fw);
	}

	std::sort(firmwares.begin(), firmwares.end(),
		[](const ATFirmwareInfo& x, const ATFirmwareInfo& y) {
			return x.mId < y.mId;
		}
	);
}

uint64 ATFirmwareManager::GetCompatibleFirmware(ATFirmwareType type) const {

	uint64 id = GetFirmwareOfType(type, true);
	if (id)
		return id;

	switch(type) {
		case kATFirmwareType_Kernel800_OSA:
			id = GetCompatibleFirmware(kATFirmwareType_Kernel800_OSB);
			break;

		case kATFirmwareType_Kernel1200XL:
		case kATFirmwareType_KernelXEGS:
			id = GetCompatibleFirmware(kATFirmwareType_KernelXL);
			break;
	}

	return id;
}

uint64 ATFirmwareManager::GetFirmwareOfType(ATFirmwareType type, bool allowInternal) const {
	typedef vdvector<ATFirmwareInfo> Firmwares;
	Firmwares firmwares;
	GetFirmwareList(firmwares);

	uint64 bestId = GetDefaultFirmware(type);

	if (bestId && std::find_if(firmwares.begin(), firmwares.end(), [bestId](const ATFirmwareInfo& info) { return info.mId == bestId; }) != firmwares.end())
		return bestId;

	for(Firmwares::const_iterator it(firmwares.begin()), itEnd(firmwares.end());
		it != itEnd;
		++it)
	{
		if (it->mType != type)
			continue;

		if (it->mId >= kATFirmwareId_Custom) {
			bestId = it->mId;
			break;
		}

		if (allowInternal && !bestId && it->mbAutoselect)
			bestId = it->mId;
	}

	return bestId;
}

VDStringW ATFirmwareManager::GetFirmwareRefString(uint64 id) const {
	if (!id)
		return VDStringW();

	if (id < kATFirmwareId_Custom) {
		VDStringW path;
		path.sprintf(L"internal:%08x", (unsigned)id);
		return path;
	}

	ATFirmwareInfo info;
	GetFirmwareInfo(id, info);

	return info.mPath;
}

uint64 ATFirmwareManager::GetFirmwareByRefString(const wchar_t *refstr, vdfunction<bool(ATFirmwareType)> typeFilter) const {
	if (!*refstr)
		return 0;

	ATFirmwareInfo info;

	if (!wcsncmp(refstr, L"internal:", 9)) {
		const uint64 id = wcstoull(refstr + 9, NULL, 16);

		if (id >= kATFirmwareId_Custom)
			return 0;

		// verify validity
		if (!GetFirmwareInfo(id, info))
			return 0;

		if (typeFilter && !typeFilter(info.mType))
			return 0;

		return id;
	}

	// try to find firmware by path
	uint64 id = ATGetFirmwareIdFromPath(refstr);

	if (GetFirmwareInfo(id, info)) {
		if (!typeFilter || typeFilter(info.mType))
			return id;
	}

	// try to find firmware by name
	const VDStringSpanW refspan(VDFileSplitExtLeftSpan(VDFileSplitPathRightSpan(VDStringSpanW(refstr))));
	// MERGE NOTE: Preserve complete descriptions, which may themselves
	// contain periods or path separators. Only filenames are normalized.
	const VDStringSpanW descriptionSpan(refstr);

	vdvector<ATFirmwareInfo> fws;
	GetFirmwareList(fws);

	for(const ATFirmwareInfo& fw : fws) {
		if (typeFilter && !typeFilter(fw.mType))
			continue;

		// try to match by filename
		const VDStringSpanW& fn = VDFileSplitExtLeftSpan(VDFileSplitPathRightSpan(fw.mPath));
		if (fn.comparei(refspan) == 0)
			return fw.mId;

		// try to match by description
		if (fw.mName.comparei(descriptionSpan) == 0)
			return fw.mId;
	}

	// If we have a name of the form [XXXXXXXX] where X is a hex digit, then
	// try to match by CRC32. This can be slow, so it is only allowed when a type filter
	// is supplied.
	if (typeFilter
		&& refspan.size() == 10
		&& refspan[0] == L'['
		&& refspan[9] == L']'
		&& std::all_of(refspan.begin() + 1, refspan.end() -1, iswxdigit))
	{
		const uint32 crc32 = (uint32)wcstoul(refspan.data() + 1, nullptr, 16);
		vdfastvector<uint8> buf;

		for(const ATFirmwareInfo& fw : fws) {
			if (typeFilter && !typeFilter(fw.mType))
				continue;

			if (LoadFirmware(fw.mId, nullptr, 0, 0, nullptr, nullptr, &buf)
				&& VDCRCTable::CRC32.CRC(buf.data(), buf.size()) == crc32)
			{
				return fw.mId;
			}
		}
	}

	// we failed
	return 0;
}

uint64 ATFirmwareManager::GetDefaultFirmware(ATFirmwareType type) const {
	VDRegistryAppKey key("Firmware\\Default", false);
	VDStringW refStr;
	key.getString(ATGetFirmwareTypeName(type), refStr);

	return GetFirmwareByRefString(refStr.c_str());
}

void ATFirmwareManager::SetDefaultFirmware(ATFirmwareType type, uint64 id) {
	VDRegistryAppKey key("Firmware\\Default", true);
	key.setString(ATGetFirmwareTypeName(type), GetFirmwareRefString(id).c_str());
}

uint64 ATFirmwareManager::GetSpecificFirmware(ATSpecificFirmwareType ft) const {
	if (mSpecificFirmwares[ft] != 0xFFFF)
		return mSpecificFirmwares[ft];

	VDRegistryAppKey key("Firmware\\Specific", false);

	VDStringA s;
	if (!key.getString(ATGetSpecificFirmwareTypeKey(ft), s))
		return 0;

	unsigned long long id;
	char c;
	if (1 != sscanf(s.c_str(), "%llx%c", &id, &c))
		id = 0;

	mSpecificFirmwares[ft] = id;
	return id;
}

void ATFirmwareManager::SetSpecificFirmware(ATSpecificFirmwareType ft, uint64 id) {
	VDRegistryAppKey key("Firmware\\Specific", true);

	VDStringA s;
	s.sprintf("%016llx", id);

	key.setString(ATGetSpecificFirmwareTypeKey(ft), s.c_str());

	mSpecificFirmwares[ft] = id;
}

bool ATFirmwareManager::LoadFirmware(uint64 id, void *dst, uint32 offset, uint32 len, bool *changed, uint32 *actualLen, vdfastvector<uint8> *dstbuf, const uint8 *fill, bool *isUsable) const {
	if (id < kATFirmwareId_Custom)
		return ATLoadInternalFirmware(id, dst, offset, len, changed, actualLen, dstbuf, isUsable);

	if (isUsable)
		*isUsable = false;

	ATFirmwareInfo fwinfo;

	if (!GetFirmwareInfo(id, fwinfo))
		return false;

	long actual;

	try {
		VDFile f;
		f.open(fwinfo.mPath.c_str());

		if (offset)
			f.seek(offset);

		if (dstbuf) {
			auto fileSize = f.size();

			if (fileSize >= 16*1024*1024)
				throw MyError("Firmware image is too big: %llu bytes.", (unsigned long long)fileSize);

			dstbuf->clear();
			dstbuf->resize((size_t)fileSize, fill ? *fill : 0);

			dst = dstbuf->data();
			len = (uint32)dstbuf->size();
		}

		if (changed) {
			vdblock<char> tmpData(len);
			actual = f.readData(tmpData.data(), len);

			if (actual >= 0) {
				*changed = memcmp(dst, tmpData.data(), actual) != 0;

				memcpy(dst, tmpData.data(), actual);
			}
		} else {
			actual = f.readData(dst, len);
		}

		if (fill) {
			uint32 uactual = actual < 0 ? 0 : (uint32)actual;
			const uint8 fillc = *fill;

			if (uactual < len) {
				uint32 extralen = len - uactual;
				uint8 *extra = (uint8 *)dst + uactual;

				if (changed && !*changed) {
					for(uint32 i=0; i<extralen; ++i) {
						if (extra[i] != fillc) {
							*changed = true;
							break;
						}
					}
				}

				memset(extra, fillc, extralen);
			}
		}
	} catch(const MyError&) {
		return false;
	}

	if (isUsable) {
		bool usable = false;

		const uint8 *dst8 = (const uint8 *)dst;
		for(long i=0; i<actual; ++i) {
			if (dst8[i] != 0 && dst8[i] != 0xFF) {
				usable = true;
				break;
			}
		}

		*isUsable = usable;
	}

	if (actualLen)
		*actualLen = (actual < 0) ? 0 : (uint32)actual;

	return true;
}

void ATFirmwareManager::AddFirmware(const ATFirmwareInfo& info) {
	VDASSERT(info.mType != kATFirmwareType_Unknown);

	VDStringA name;
	name.sprintf("Firmware\\Available\\%016llX", ATGetFirmwareIdFromPath(info.mPath.c_str()));
	VDRegistryAppKey key(name.c_str());
	key.setString("Name", info.mName.c_str());

	const VDStringW& programPath = VDGetProgramPath();
	const VDStringW& relPath = VDFileGetRelativePath(programPath.c_str(), info.mPath.c_str(), g_ATFirmwarePathPortabilityEnabled);
	const VDStringW *path = relPath.empty() ? &info.mPath : &relPath;

	key.setString("Path", path->c_str());

	key.setString("Type", ATGetFirmwareTypeName(info.mType));
	key.setInt("Flags", info.mFlags);
}

void ATFirmwareManager::RemoveFirmware(uint64 id) {
	if (id < kATFirmwareId_Custom)
		return;

	VDStringA name;
	name.sprintf("%016llX", id);

	VDRegistryAppKey key("Firmware\\Available");
	key.removeKeyRecursive(name.c_str());
}

void ATFirmwareManager::GetVFSSpecialFirmware(const wchar_t *path, bool write, bool update, ATVFSFileView **outView) {
	VDStringW lowerPath(path);
	for(wchar_t& ch : lowerPath)
		ch = towlower(ch);

	class VFSMemoryView final : public ATVFSFileView {
	public:
		VFSMemoryView() {
			mpStream = &mMemStream;
		}

		void SetFileName(const wchar_t *name) {
			mFileName = name;
		}

		bool IsSourceReadOnly() const override { return true; }

		vdfastvector<uint8> mBuf;
		VDMemoryStream mMemStream { nullptr, 0 };
	};

	vdrefptr<VFSMemoryView> view(new VFSMemoryView);
	bool addCarHeader = false;

	static constexpr struct {
		const wchar_t *mpName;
		uint32 mFirmwareId = 0;
		bool mbAddCarHeader = false;
	} kSpecificFirmwares[] = {
		{ L"atbasic.bin", kATFirmwareId_Basic_ATBasic, false },
		{ L"atbasic.car", kATFirmwareId_Basic_ATBasic, true },
	};

	for(const auto& fw : kSpecificFirmwares) {
		if (lowerPath == fw.mpName) {
			VDVERIFY(LoadFirmware(fw.mFirmwareId, nullptr, 0, 0, nullptr, nullptr, &view->mBuf, nullptr, nullptr));
			addCarHeader = fw.mbAddCarHeader;
			break;
		}
	}

	if (view->mBuf.empty()) {
		static constexpr struct {
			const wchar_t *mpName;
			ATFirmwareType mFirmwareType;
			bool mbAddCarHeader = false;
		} kDefaultFirmwares[] = {
			{ L"basic.bin", kATFirmwareType_Basic, false },
			{ L"basic.car", kATFirmwareType_Basic, true },
		};

		for(const auto& fw : kDefaultFirmwares) {
			if (lowerPath == fw.mpName) {
				auto firmwareId = GetDefaultFirmware(fw.mFirmwareType);

				if (!firmwareId) {
					firmwareId = GetCompatibleFirmware(fw.mFirmwareType);

					if (!firmwareId)
						throw ATVFSNotAvailableException();
				}

				if (!LoadFirmware(firmwareId, nullptr, 0, 0, nullptr, nullptr, &view->mBuf, nullptr, nullptr))
					throw ATVFSNotAvailableException();

				addCarHeader = fw.mbAddCarHeader;
				break;
			}
		}
	}

	if (view->mBuf.empty()) {
		static constexpr struct {
			const wchar_t *mpName;
			ATSpecificFirmwareType mSpecificFirmware;
			bool mbAddCarHeader = false;
		} kSpecificFirmwares[] = {
			{ L"ataribas-a.bin", kATSpecificFirmwareType_BASICRevA, false },
			{ L"ataribas-a.car", kATSpecificFirmwareType_BASICRevA, true },
			{ L"ataribas-b.bin", kATSpecificFirmwareType_BASICRevB, false },
			{ L"ataribas-b.car", kATSpecificFirmwareType_BASICRevB, true },
			{ L"ataribas-c.bin", kATSpecificFirmwareType_BASICRevC, false },
			{ L"ataribas-c.car", kATSpecificFirmwareType_BASICRevC, true },
		};

		for(const auto& fw : kSpecificFirmwares) {
			if (lowerPath == fw.mpName) {
				const auto firmwareId = GetSpecificFirmware(fw.mSpecificFirmware);

				if (!firmwareId)
					throw ATVFSNotAvailableException();

				if (!LoadFirmware(firmwareId, nullptr, 0, 0, nullptr, nullptr, &view->mBuf, nullptr, nullptr))
					throw ATVFSNotAvailableException();

				addCarHeader = fw.mbAddCarHeader;
				break;
			}
		}
	}

	if (view->mBuf.empty())
		throw ATVFSNotFoundException();

	if (write)
		throw ATVFSReadOnlyException();

	const wchar_t *name = path;
	if (const wchar_t *split = wcsrchr(name, L'/'))
		name = split;

	view->SetFileName(name);

	// currently, all cart images we support are 8K
	if (addCarHeader) {
		alignas(uint32) uint8 mHeader[16] {
			(uint8)'C',
			(uint8)'A',
			(uint8)'R',
			(uint8)'T',
			0, 0, 0, 1,		// 8K cart
			0, 0, 0, 0,
			0, 0, 0, 0,
		};

		VDWriteUnalignedBEU32(&mHeader[8], std::accumulate(view->mBuf.begin(), view->mBuf.end(), (uint32)0));

		view->mBuf.insert(view->mBuf.begin(), std::begin(mHeader), std::end(mHeader));
	}

	view->mMemStream = VDMemoryStream(view->mBuf.data(), view->mBuf.size());

	*outView = view.release();
}
