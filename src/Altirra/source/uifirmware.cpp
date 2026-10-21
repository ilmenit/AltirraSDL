//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2014 Avery Lee
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
#include <vd2/system/bitmath.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/linearalloc.h>
#include <vd2/system/math.h>
#include <vd2/system/strutil.h>
#include <vd2/system/vdstl_algorithm.h>
#include <vd2/system/zip.h>
#include <vd2/Dita/services.h>
#include <at/atnativeui/dialog.h>
#include <at/atnativeui/theme.h>
#include <at/atui/uimenulist.h>
#include "resource.h"
#include "oshelper.h"
#include "firmwaremanager.h"
#include "firmwaredetect.h"
#include "uimenu.h"

void ATUIScanForFirmware(VDGUIHandle hParent, ATFirmwareManager& fwmgr);
const wchar_t *ATUITryGetFirmwareShortTypeName(ATFirmwareType type);

///////////////////////////////////////////////////////////////////////////

class ATUIDialogKnownFirmwareAudit final : public VDDialogFrameW32, private VDThread {
public:
	ATUIDialogKnownFirmwareAudit(ATFirmwareManager& fw);

private:
	class KnownFirmwareItem;

	struct ScanItem {
		VDStringW mPath;
		uint64 mFirmwareId = 0;
		sint32 mKnownFirmwareIndex = -1;
	};

	bool OnLoaded() override;
	void OnDestroy() override;
	void OnDataExchange(bool write) override;

	void OnScanProgress();

	void ThreadRun() override;

	ATFirmwareManager& mFirmwareManager;
	VDUIProxyListView mListView;

	vdfastvector<sint32> mKnownFirmwareIndexToListIndex;

	size_t mScannedItemsReported = 0;
	bool mbLoaded = false;

	VDRWLock mMutex;
	bool mbScanProgressPending = false;
	bool mbCancelScan = false;
	size_t mItemsScanned = 0;
	vdvector<ScanItem> mScanItems;
};

class ATUIDialogKnownFirmwareAudit::KnownFirmwareItem final : public vdrefcounted<IVDUIListViewVirtualItem> {
public:
	KnownFirmwareItem(const ATKnownFirmware& kfw, size_t idx)
		: mKnownFirmwareIndex(idx)
		, mKnownFirmware(kfw)
	{
	}

	void GetText(int subItem, VDStringW& s) const override;

	size_t mKnownFirmwareIndex = 0;
	ATKnownFirmware mKnownFirmware;
	VDStringW mFirmwareName;
};

void ATUIDialogKnownFirmwareAudit::KnownFirmwareItem::GetText(int subItem, VDStringW& s) const {
	switch(subItem) {
		case 0:
			if (const wchar_t *name = ATUITryGetFirmwareShortTypeName(mKnownFirmware.mType))
				s = name;
			break;

		case 1:
			s = mKnownFirmware.mpDesc;
			break;

		case 2:
			s.sprintf(L"%u", mKnownFirmware.mSize);
			break;

		case 3:
			s.sprintf(L"%08X", mKnownFirmware.mCRC);
			break;

		case 4:
			s = mFirmwareName;
			break;
	}
}

ATUIDialogKnownFirmwareAudit::ATUIDialogKnownFirmwareAudit(ATFirmwareManager& fw)
	: VDDialogFrameW32(IDD_FIRMWARE_KNOWN)
	, mFirmwareManager(fw)
{
}

bool ATUIDialogKnownFirmwareAudit::OnLoaded() {
	AddProxy(&mListView, IDC_LIST);

	mResizer.Add(mListView.GetWindowHandle(), mResizer.kMC);

	ATUIRestoreWindowPlacement(mhdlg, "Known firmware audit");

	mListView.SetFullRowSelectEnabled(true);

	mListView.InsertColumn(0, L"Type", 0);
	mListView.InsertColumn(1, L"Known Firmware", 0);
	mListView.InsertColumn(2, L"Size", 0);
	mListView.InsertColumn(3, L"CRC32", 0);
	mListView.InsertColumn(4, L"Firmware Image", 0);
	mListView.AutoSizeColumns(true);

	OnDataExchange(false);
	SetFocusToControl(IDC_LIST);

	// kickstart scan
	vdvector<ATFirmwareInfo> fws;
	mFirmwareManager.GetFirmwareList(fws);

	for(const ATFirmwareInfo& fw : fws) {
		if (!fw.mbVisible)
			continue;

		if (fw.mPath.empty())
			continue;

		auto& sfw = mScanItems.emplace_back();

		sfw.mPath = fw.mPath;
		sfw.mFirmwareId = fw.mId;
	}

	ThreadStart();

	return true;
}

void ATUIDialogKnownFirmwareAudit::OnDestroy() {
	vdsyncexclusive(mMutex) {
		mbCancelScan = true;
	}

	ThreadCancelSynchronousIo();
	ThreadWait();
	
	ATUISaveWindowPlacement(mhdlg, "Known firmware audit");

	VDDialogFrameW32::OnDestroy();
}

void ATUIDialogKnownFirmwareAudit::OnDataExchange(bool write) {
	if (!write) {
		if (mbLoaded)
			return;

		mbLoaded = false;

		mListView.SetRedraw(false);
		mListView.Clear();

		vdvector<vdrefptr<KnownFirmwareItem>> items;

		for(size_t idx = 0; ; ++idx) {
			const ATKnownFirmware *kfw = ATFirmwareGetKnownByIndex(idx);
			if (!kfw)
				break;

			vdrefptr<KnownFirmwareItem> item = vdmakerefcounted<KnownFirmwareItem>(*kfw, idx);

			items.emplace_back(std::move(item));
		}

		std::sort(
			items.begin(),
			items.end(),
			[](const vdrefptr<KnownFirmwareItem>& a, const vdrefptr<KnownFirmwareItem>& b) {
				if (a->mKnownFirmware.mType != b->mKnownFirmware.mType) {
					const wchar_t *aType = ATUITryGetFirmwareShortTypeName(a->mKnownFirmware.mType);
					const wchar_t *bType = ATUITryGetFirmwareShortTypeName(b->mKnownFirmware.mType);

					if (!aType)
						return bType != nullptr;

					if (!bType)
						return false;

					return vdwcsicmp(aType, bType) < 0;
				}

				return vdwcsicmp(a->mKnownFirmware.mpDesc, b->mKnownFirmware.mpDesc) < 0;
			}
		);

		sint32 listIdx = 0;

		for(const vdrefptr<KnownFirmwareItem>& item : items) {
			const size_t kfi = item->mKnownFirmwareIndex;

			if (mKnownFirmwareIndexToListIndex.size() <= kfi)
				mKnownFirmwareIndexToListIndex.resize(kfi + 1, -1);

			mKnownFirmwareIndexToListIndex[kfi] = listIdx;

			mListView.AddVirtualItem(*item);
			++listIdx;
		}

		mListView.AutoSizeColumns(true);
		mListView.SetRedraw(true);
	}
}

void ATUIDialogKnownFirmwareAudit::OnScanProgress() {
	size_t itemsScanned;
	bool atEnd = false;

	vdsyncexclusive(mMutex) {
		itemsScanned = mItemsScanned;
		mbScanProgressPending = false;

		if (itemsScanned >= mScanItems.size())
			atEnd = true;
	}

	while(mScannedItemsReported < itemsScanned) {
		sint32 kfi = -1;
		uint64 fwId = 0;

		vdsyncexclusive(mMutex) {
			kfi = mScanItems[mScannedItemsReported].mKnownFirmwareIndex;
			fwId = mScanItems[mScannedItemsReported].mFirmwareId;
		}

		if (kfi >= 0) {
			if ((size_t)kfi < mKnownFirmwareIndexToListIndex.size()) {
				sint32 listIdx = mKnownFirmwareIndexToListIndex[(size_t)kfi];

				if (listIdx >= 0) {
					auto *listItem = static_cast<KnownFirmwareItem *>(mListView.GetVirtualItem(listIdx));

					if (listItem) {
						ATFirmwareInfo fwInfo;
						if (mFirmwareManager.GetFirmwareInfo(fwId, fwInfo)) {
							listItem->mFirmwareName = fwInfo.mName;

							mListView.RefreshItem(listIdx);
						}
					}
				}
			}
		}

		++mScannedItemsReported;
	}

	if (atEnd)
		mListView.AutoSizeColumns(true);
}

void ATUIDialogKnownFirmwareAudit::ThreadRun() {
	size_t scanIdx = 0;
	vdfastvector<uint8> buf;

	for(;;) {
		VDStringW path;

		vdsyncexclusive(mMutex) {
			if (scanIdx >= mScanItems.size() || mbCancelScan)
				break;

			path = mScanItems[scanIdx].mPath;
		}

		sint32 knownFirmwareIndex = -1;

		try {
			VDFile f(path.c_str(), nsVDFile::kRead | nsVDFile::kOpenExisting | nsVDFile::kDenyWrite);

			vdsyncexclusive(mMutex) {
				if (mbCancelScan)
					break;
			}

			const sint64 sz = f.size();

			vdsyncexclusive(mMutex) {
				if (mbCancelScan)
					break;
			}

			if (ATFirmwareAutodetectCheckSize(sz)) {
				buf.resize((size_t)sz);

				f.read(buf.data(), buf.size());

				ATFirmwareInfo info;
				ATSpecificFirmwareType specFwType {};
				ATFirmwareAutodetect(buf.data(), (uint32)buf.size(), info, specFwType, knownFirmwareIndex);
			}
		} catch(...) {
		}

		vdsyncexclusive(mMutex) {
			if (scanIdx >= mScanItems.size() || mbCancelScan)
				break;

			if (knownFirmwareIndex >= 0)
				mScanItems[scanIdx].mKnownFirmwareIndex = knownFirmwareIndex;

			mItemsScanned = ++scanIdx;

			if (!mbScanProgressPending) {
				mbScanProgressPending = true;

				PostCall([this] { OnScanProgress(); });
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////

namespace {
	const wchar_t *GetSpecificFirmwareLabel(ATSpecificFirmwareType ft) {
		switch(ft) {
			case kATSpecificFirmwareType_BASICRevA:	return L"BASIC rev. A";
			case kATSpecificFirmwareType_BASICRevB:	return L"BASIC rev. B";
			case kATSpecificFirmwareType_BASICRevC:	return L"BASIC rev. C";
			case kATSpecificFirmwareType_OSA:		return L"OS-A";
			case kATSpecificFirmwareType_OSB:		return L"OS-B";
			case kATSpecificFirmwareType_XLOSr2:	return L"XL/XE OS ver. 2";
			case kATSpecificFirmwareType_XLOSr4:	return L"XL/XE/XEGS OS ver. 4";
			default:
				return nullptr;
		}
	}

	struct FirmwareItem final : public vdrefcounted<IVDUIListViewVirtualItem> {
		FirmwareItem(uint64 id, ATFirmwareType type, const wchar_t *text, const wchar_t *path, bool showType)
			: mId(id)
			, mType(type)
			, mText(text)
			, mPath(path)
			, mFlags(0)
			, mbShowType(showType)
			, mbDefault(false)
		{
		}

		void GetText(int subItem, VDStringW& s) const override;

		const uint64 mId;
		ATFirmwareType mType;
		VDStringW mText;
		VDStringW mPath;
		uint32 mFlags;
		const bool mbShowType;
		bool mbDefault;

		uint32 mSpecificMask = 0;
	};

	void FirmwareItem::GetText(int subIndex, VDStringW& s) const {
		if (subIndex == 0) {
			if (mId == 0)
				s = mText;
			else
				s.sprintf(L"%ls (%ls)", mText.c_str(), mId < kATFirmwareId_Custom ? L"internal" : VDFileSplitPath(mPath.c_str()));

			return;
		}

		if (mbShowType) {
			if (subIndex == 1) {
				const wchar_t *name = ATUITryGetFirmwareShortTypeName(mType);

				if (name)
					s = name;

				return;
			}

			--subIndex;
		}

		switch(subIndex) {
			case 1:
				if (mSpecificMask) {
					uint32 mask = mSpecificMask;
					while(mask) {
						uint32 nextMask = mask & (mask - 1);
	
						if (!s.empty())
							s += L", ";

						s += GetSpecificFirmwareLabel((ATSpecificFirmwareType)VDFindLowestSetBitFast(nextMask ^ mask));

						mask = nextMask;
					}
				}
				break;
		}
	}

	struct FirmwareItemComparer final : public IVDUIListViewVirtualComparer {
		FirmwareItemComparer() {
			// compute type short name sort table
			const wchar_t *shortTypeName[kATFirmwareTypeCount] {};

			for(size_t i=0; i<kATFirmwareTypeCount; ++i)
				shortTypeName[i] = ATUITryGetFirmwareShortTypeName((ATFirmwareType)i);

			uint8 sortedTypeOrder[kATFirmwareTypeCount] {};
			for(size_t i=0; i<kATFirmwareTypeCount; ++i)
				sortedTypeOrder[i] = (uint8)i;

			std::sort(
				std::begin(sortedTypeOrder),
				std::end(sortedTypeOrder),
				[&](uint8 a, uint8 b) {
					const wchar_t *sa = shortTypeName[a];
					const wchar_t *sb = shortTypeName[b];

					if (!sa)
						return sb != nullptr;

					if (!sb)
						return false;

					return vdwcsicmp(sa, sb) < 0;
				}
			);

			for(size_t i=0; i<kATFirmwareTypeCount; ++i)
				mTypeShortNameSortTable[sortedTypeOrder[i]] = (uint8)i;
		}

		int Compare(IVDUIListViewVirtualItem *x, IVDUIListViewVirtualItem *y) override {
			const FirmwareItem& itemX = static_cast<const FirmwareItem&>(*x);
			const FirmwareItem& itemY = static_cast<const FirmwareItem&>(*y);

			if (itemX.mType != itemY.mType)
				return (int)mTypeShortNameSortTable[(uint8)itemX.mType] - (int)mTypeShortNameSortTable[(uint8)itemY.mType];

			return itemX.mText.comparei(itemY.mText);
		}

		uint8 mTypeShortNameSortTable[kATFirmwareTypeCount] {};
	};

	VDStringW BrowseForFirmware(VDDialogFrameW32 *parent) {
		return VDGetLoadFileName("ROMI"_vdfcctypeid, (VDGUIHandle)parent->GetWindowHandle(), L"Browse for ROM image", L"ROM image\0*.rom;*.bin;*.epr;*.epm\0All files\0*.*\0", NULL);
	}
}

///////////////////////////////////////////////////////////////////////////

struct ATFirmwareTypeEntry {
	ATFirmwareType mType;
	const wchar_t *mpShortName;
	const wchar_t *mpLongName = nullptr;
};

struct ATUIFirmwareCategoryEntry {
	const wchar_t *mpCategoryName;
	std::initializer_list<ATFirmwareTypeEntry> mEntries;
};

static const ATUIFirmwareCategoryEntry kATUIFirmwareCategories[] {
	{
		L"Computer",
		{
			{ kATFirmwareType_Kernel800_OSA,			L"400/800 OS-A" },
			{ kATFirmwareType_Kernel800_OSB,			L"400/800 OS-B" },
			{ kATFirmwareType_Kernel1200XL,				L"1200XL OS" },
			{ kATFirmwareType_KernelXL,					L"XL/XE OS" },
			{ kATFirmwareType_KernelXEGS,				L"XEGS OS" },
			{ kATFirmwareType_Game,						L"XEGS Game" },
			{ kATFirmwareType_1400XLHandler,			L"1400XL Handler", L"1400XL/XLD Handler" },
			{ kATFirmwareType_1450XLDiskHandler,		L"1450XLD Disk Handler" },
			{ kATFirmwareType_1450XLDiskController,		L"1450XLD Disk Controller" },
			{ kATFirmwareType_1450XLTONGDiskController,	L"\"TONG\" Disk Controller", L"1450XLD \"TONG\" Disk Controller" },
			{ kATFirmwareType_Kernel5200,				L"5200 OS" },
			{ kATFirmwareType_Basic,					L"BASIC", L"Internal BASIC (XL/XE/XEGS)" },
		}
	},
	{
		L"Printers",
		{
			{ kATFirmwareType_820,		L"820",		L"820 40-Column Printer Firmware" },
			{ kATFirmwareType_1025,		L"1025",	L"1025 80-Column Printer Firmware" },
			{ kATFirmwareType_1029,		L"1029",	L"1029 80-Column Printer Firmware" },
		}
	},
	{
		L"Disk Drives",
		{
			{ kATFirmwareType_810,				L"810", L"810 Disk Drive Firmware" },
			{ kATFirmwareType_Happy810,			L"Happy 810", L"Happy 810 Disk Drive Firmware" },
			{ kATFirmwareType_810Archiver,		L"810 Archiver", L"810 Archiver Disk Drive Firmware" },
			{ kATFirmwareType_810Turbo,			L"810 Turbo", L"810 Turbo Disk Drive Firmware" },
			{ kATFirmwareType_815,				L"815", L"815 Disk Drive Firmware" },
			{ kATFirmwareType_1050,				L"1050", L"1050 Disk Drive Firmware" },
			{ kATFirmwareType_1050Duplicator,	L"1050 Duplicator", L"1050 Duplicator Disk Drive Firmware" },
			{ kATFirmwareType_USDoubler,		L"US Doubler", L"US Doubler Disk Drive Firmware" },
			{ kATFirmwareType_Speedy1050,		L"Speedy 1050", L"Speedy 1050 Disk Drive Firmware" },
			{ kATFirmwareType_Happy1050,		L"Happy 1050", L"Happy 1050 Disk Drive Firmware" },
			{ kATFirmwareType_SuperArchiver,	L"Super Archiver", L"Super Archiver Disk Drive Firmware" },
			{ kATFirmwareType_TOMS1050,			L"TOMS 1050", L"TOMS 1050 Disk Drive Firmware" },
			{ kATFirmwareType_Tygrys1050,		L"Tygrys 1050", L"Tygrys 1050 Disk Drive Firmware" },
			{ kATFirmwareType_IndusGT,			L"Indus GT", L"Indus GT Disk Drive Firmware" },
			{ kATFirmwareType_1050Turbo,		L"1050 Turbo", L"1050 Turbo Disk Drive Firmware" },
			{ kATFirmwareType_1050TurboII,		L"1050 Turbo II", L"1050 Turbo II Disk Drive Firmware" },
			{ kATFirmwareType_ISPlate,			L"I.S. Plate", L"I.S. Plate Disk Drive Firmware" },
			{ kATFirmwareType_XF551,			L"XF551", L"XF551 Disk Drive Firmware" },
			{ kATFirmwareType_ATR8000,			L"ATR8000", L"ATR8000 Disk Drive Firmware" },
			{ kATFirmwareType_Percom,			L"PERCOM RFD", L"PERCOM RFD Disk Drive Firmware" },
			{ kATFirmwareType_PercomAT,			L"PERCOM AT-88", L"PERCOM AT-88 Disk Drive Firmware" },
			{ kATFirmwareType_PercomATSPD,		L"PERCOM AT88-SPD", L"PERCOM AT88-SPD Disk Drive Firmware" },
			{ kATFirmwareType_AMDC,				L"Amdek AMDC", L"Amdek AMDC-I/II Disk Drive Firmware" },
			{ kATFirmwareType_SpeedyXF,			L"Speedy XF", L"Speedy XF Disk Drive Firmware" },
		}
	},
	{
		L"Hardware",
		{
			{ kATFirmwareType_U1MB,				L"Ultimate1MB" },
			{ kATFirmwareType_MyIDE2,			L"MyIDE-II" },
			{ kATFirmwareType_SIDE,				L"SIDE" },
			{ kATFirmwareType_SIDE2,			L"SIDE 2" },
			{ kATFirmwareType_SIDE3,			L"SIDE 3" },
			{ kATFirmwareType_KMKJZIDE,			L"KMK/JZ IDE" },
			{ kATFirmwareType_KMKJZIDE2,		L"KMK/JZ IDE 2 Main", L"KMK/JZ IDE 2 (IDEPlus) main" },
			{ kATFirmwareType_KMKJZIDE2_SDX,	L"KMK/JZ IDE 2 SDX", L"KMK/JZ IDE 2 (IDEPlus) SDX" },
			{ kATFirmwareType_BlackBox,			L"Black Box" },
			{ kATFirmwareType_BlackBoxFloppy,	L"BB Floppy Board", L"Black Box Floppy Board" },
			{ kATFirmwareType_MIO,				L"MIO" },
			{ kATFirmwareType_835,				L"835", L"835 Modem Internal ROM Firmware" },
			{ kATFirmwareType_850,				L"850", L"850 Interface Module" },
			{ kATFirmwareType_1030Firmware,		L"1030 Download", L"1030 Modem Download Image" },
			{ kATFirmwareType_1030InternalROM,	L"1030 Internal", L"1030 Modem Internal ROM Firmware" },
			{ kATFirmwareType_1030ExternalROM,	L"1030 External", L"1030 Modem External ROM Firmware" },
			{ kATFirmwareType_RapidusFlash,		L"Rapidus Flash", L"Rapidus Flash Firmware" },
			{ kATFirmwareType_RapidusCorePBI,	L"Rapidus Core", L"Rapidus Core PBI Firmware" },
			{ kATFirmwareType_WarpOS,			L"APE Warp+", L"APE Warp+ OS 32-in-1 Firmware" },
			{ kATFirmwareType_1090Firmware,		L"1090 Firmware", L"1090 80-Column Video Card Firmware" },
			{ kATFirmwareType_1090Charset,		L"1090 Charset", L"1090 80-Column Video Card Charset" },
			{ kATFirmwareType_AF80Firmware, L"AF80 Firmware", L"Austin Franklin 80 Firmware (4K)" },
			{ kATFirmwareType_AF80Charset, L"AF80 Charset", L"Austin Franklin 80 Charset (4K)" },
			{ kATFirmwareType_Bit3Firmware,		L"Bit3 Firmware", L"Bit 3 Full-View 80 Firmware" },
			{ kATFirmwareType_Bit3Charset,		L"Bit3 Charset", L"Bit 3 Full-View 80 Charset" },
			{ kATFirmwareType_KarinMaxiDrive,	L"Karin Maxi Drive", L"Karin Maxi Drive Firmware" },
		}
	},
};

void ATUIBuildFirmwareTypeMenu(vdfastvector<ATFirmwareType>& firmwareLookup, ATUIPopupMenuBuilder& builder) {
	for(const ATUIFirmwareCategoryEntry& category : kATUIFirmwareCategories) {
		builder.BeginSubMenu(category.mpCategoryName);

		size_t n = category.mEntries.size();
		vdfastvector<const ATFirmwareTypeEntry *> entries(n);

		for(size_t i = 0; i < n; ++i)
			entries[i] = &*(category.mEntries.begin() + i);

		std::sort(
			entries.begin(),
			entries.end(),
			[](const ATFirmwareTypeEntry *a, const ATFirmwareTypeEntry *b) {
				return vdwcsicmp(a->mpShortName, b->mpShortName) < 0;
			}
		);

		for(const ATFirmwareTypeEntry *entry : entries) {
			builder.AddItem(entry->mpShortName);

			firmwareLookup.emplace_back(entry->mType);
		}

		builder.EndSubMenu();
	}
}

const wchar_t *ATUITryGetFirmwareShortTypeName(ATFirmwareType type) {
	for(const ATUIFirmwareCategoryEntry& category : kATUIFirmwareCategories) {
		for(const ATFirmwareTypeEntry& entry : category.mEntries) {
			if (entry.mType == type)
				return entry.mpShortName;
		}
	}

	return nullptr;
}

const wchar_t *ATUITryGetFirmwareLongTypeName(ATFirmwareType type) {
	for(const ATUIFirmwareCategoryEntry& category : kATUIFirmwareCategories) {
		for(const ATFirmwareTypeEntry& entry : category.mEntries) {
			if (entry.mType == type)
				return entry.mpLongName ? entry.mpLongName : entry.mpShortName;
		}
	}

	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////

class ATUIDialogEditFirmwareSettings : public VDDialogFrameW32 {
public:
	ATUIDialogEditFirmwareSettings(FirmwareItem& item);

protected:
	bool OnLoaded();
	void OnDataExchange(bool write);

	void OnSelectType();
	void UpdateTypeLabel();
	void RedoOptions(ATFirmwareType type);

	FirmwareItem& mItem;
	VDUIProxyButtonControl mTypeView;
	VDUIProxyListView mOptionsView;
	uint32 mFlagCount;
	ATFirmwareType mType {};

	VDDelegate mDelTypeChanged;
};

ATUIDialogEditFirmwareSettings::ATUIDialogEditFirmwareSettings(FirmwareItem& item)
	: VDDialogFrameW32(IDD_FIRMWARE_EDIT)
	, mItem(item)
	, mFlagCount(0)
	, mType(item.mType)
{
	mTypeView.SetOnClicked([this] { OnSelectType(); });
}

bool ATUIDialogEditFirmwareSettings::OnLoaded() {
	AddProxy(&mOptionsView, IDC_OPTIONS);
	AddProxy(&mTypeView, IDC_TYPE);

	mOptionsView.SetFullRowSelectEnabled(true);
	mOptionsView.SetItemCheckboxesEnabled(true);

	try {
		VDFile f(mItem.mPath.c_str());

		sint64 size64 = f.size();

		if (size64 <= 4*1024*1024) {
			uint32 size32 = (uint32)size64;

			vdblock<uint8> buf(size32);
			f.read(buf.data(), size32);

			const uint32 crc32 = VDCRCTable::CRC32.CRC(buf.data(), size32);

			SetControlTextF(IDC_CRC32, L"%08X", crc32);
		}
	} catch(const MyError&) {
	}

	VDDialogFrameW32::OnLoaded();
	SetFocusToControl(IDC_NAME);
	return false;
}

void ATUIDialogEditFirmwareSettings::OnDataExchange(bool write) {
	ExchangeControlValueString(write, IDC_NAME, mItem.mText);

	if (write) {
		if (mType == kATFirmwareType_Unknown) {
			FailValidation(IDC_TYPE, L"The firmware type has not been set.");
			return;
		}

		mItem.mType = mType;

		if (mFlagCount)
			mItem.mFlags = mOptionsView.IsItemChecked(0) ? 1 : 0;
		else
			mItem.mFlags = 0;
	} else {
		SetControlText(IDC_PATH, mItem.mPath.c_str());

		UpdateTypeLabel();

		RedoOptions(mItem.mType);

		if (mFlagCount)
			mOptionsView.SetItemChecked(0, (mItem.mFlags & 1) != 0);
	}
}

void ATUIDialogEditFirmwareSettings::OnSelectType() {
	ATUIPopupMenuBuilder builder;

	vdfastvector<ATFirmwareType> types;
	ATUIBuildFirmwareTypeMenu(types, builder);

	uint32 selectedId = ActivateCommandPopupMenuReturnId(mTypeView, builder.GetPopupMenu(), nullptr);
	int index = builder.GetIndexFromItemId(selectedId);

	if (index >= 0) {
		ATFirmwareType type = types[index];

		if (mType != type) {
			mType = type;

			UpdateTypeLabel();
			RedoOptions(mType);
		}
	}
}

void ATUIDialogEditFirmwareSettings::UpdateTypeLabel() {
	const wchar_t *label = ATUITryGetFirmwareLongTypeName(mType);

	mTypeView.SetCaption(label ? label : L"(Select type)");
}

void ATUIDialogEditFirmwareSettings::RedoOptions(ATFirmwareType type) {
	mOptionsView.Clear();
	mFlagCount = 0;

	switch(type) {
		case kATFirmwareType_KernelXL:
		case kATFirmwareType_KernelXEGS:
			mFlagCount = 1;
			mOptionsView.InsertItem(-1, L"OPTION key inverted (hold to enable BASIC)");
			mOptionsView.Show();
			break;

		default:
			mOptionsView.Hide();
			break;
	}
}

///////////////////////////////////////////////////////////////////////////

class ATUIDialogFirmware final : public VDResizableDialogFrameW32 {
public:
	ATUIDialogFirmware(ATFirmwareManager& sim);

	bool AnyChanges() const { return mbAnyChanges; }

protected:
	bool OnLoaded() override;
	void OnDestroy() override;
	void OnDataExchange(bool write) override;
	bool OnCommand(uint32 id, uint32 extcode) override;
	void OnDropFiles(IVDUIDropFileList *dropFileList) override;

	void Add();
	void Add(const wchar_t *);
	void Remove();
	void EditSettings();
	void SetAsDefault();
	void SetAsSpecific();
	void Audit();
	void UpdateFirmwareItem(const FirmwareItem& item);
	uint32 GetItemSpecificMask(FirmwareItem& item) const;
	void UpdateSpecificNodes(FirmwareItem *item);
	void UpdateNoFirmwareNodes();
	void UpdateTypeLabel();

	void OnSelectType();
	void OnSelChanged(VDUIProxyListView *sender, int idx);
	void OnDeferredSelChanged();

	void OnItemDoubleClicked(VDUIProxyListView *sender, int index);
	bool OnItemLabelChanging(VDUIProxyListView::LabelChangingEvent& event);
	void OnItemLabelChanged(VDUIProxyListView *sender, VDUIProxyListView::LabelChangedEvent *event);
	bool OnItemCustomStyle(IVDUIListViewVirtualItem& item, sint32& color, bool& bold);
	void OnItemContextMenu(VDUIProxyListView::ContextMenuEvent& event);

	ATFirmwareManager& mFwManager;
	bool mbAnyChanges = false;
	ATFirmwareType mType = kATFirmwareType_Unknown;

	bool mbDeferredSelChanged = false;

	uint64 mDefaultIds[kATFirmwareTypeCount];

	VDUIProxyButtonControl mTypeView;
	VDUIProxyListView mEntryView;

	VDDelegate mDelSelChanged;
	VDDelegate mDelDblClk;
	VDDelegate mDelItemLabelChanged;

	FirmwareItemComparer mComparer;
};

ATUIDialogFirmware::ATUIDialogFirmware(ATFirmwareManager& fw)
	: VDResizableDialogFrameW32(IDD_FIRMWARE)
	, mFwManager(fw)
	, mbAnyChanges(false)
{
	mTypeView.SetOnClicked([this] { OnSelectType(); });
	mTypeView.SetOnDropDown([this] { OnSelectType(); });

	mEntryView.SetOnItemCustomStyle(std::bind_front(&ATUIDialogFirmware::OnItemCustomStyle, this));
}

bool ATUIDialogFirmware::OnLoaded() {
	AddProxy(&mTypeView, IDC_TYPE);
	AddProxy(&mEntryView, IDC_ENTRIES);

	typedef VDDialogResizerW32 RS;

	mResizer.Add(mTypeView.GetHandle(), RS::kTC);
	mResizer.Add(mEntryView.GetHandle(), RS::kMC | RS::kAvoidFlicker);
	mResizer.Add(IDC_ADD, RS::kBL);
	mResizer.Add(IDC_REMOVE, RS::kBL);
	mResizer.Add(IDC_SETTINGS, RS::kBL);
	mResizer.Add(IDC_SCAN, RS::kBL);
	mResizer.Add(IDC_SETASDEFAULT, RS::kBL);
	mResizer.Add(IDC_SETASSPECIFIC, RS::kBL);
	mResizer.Add(IDC_AUDIT, RS::kBL);
	mResizer.Add(IDC_CLEAR, RS::kBL);
	mResizer.Add(IDOK, RS::kBR);

	ATUIRestoreWindowPlacement(mhdlg, "Firmware dialog");

	mEntryView.SetFullRowSelectEnabled(true);

	UpdateTypeLabel();

	OnDataExchange(false);

	{
		VDPixmapBuffer buf;
		if (ATLoadImageResource(IDB_FIRMWARE_ICONS, buf))
			mEntryView.AddImages(buf);
	}

	mEntryView.OnItemSelectionChanged() += mDelSelChanged.Bind(this, &ATUIDialogFirmware::OnSelChanged);
	mEntryView.OnItemDoubleClicked() += mDelDblClk.Bind(this, &ATUIDialogFirmware::OnItemDoubleClicked);
	mEntryView.SetOnItemLabelChanging(std::bind_front(&ATUIDialogFirmware::OnItemLabelChanging, this));
	mEntryView.OnItemLabelChanged() += mDelItemLabelChanged.Bind(this, &ATUIDialogFirmware::OnItemLabelChanged);
	mEntryView.SetOnItemContextMenu(std::bind_front(&ATUIDialogFirmware::OnItemContextMenu, this));

	mEntryView.Focus();
	return true;
}

void ATUIDialogFirmware::OnDestroy() {
	mbDeferredSelChanged = false;

	mEntryView.Clear();

	ATUISaveWindowPlacement(mhdlg, "Firmware dialog");

	VDDialogFrameW32::OnDestroy();
}

void ATUIDialogFirmware::OnDataExchange(bool write) {
	if (write) {
	} else {
		mEntryView.SetRedraw(false);
		mEntryView.Clear();

		mEntryView.ClearAllColumns();

		int cols = 0;

		mEntryView.InsertColumn(cols++, L"Name", 100);

		const bool showType = (mType == kATFirmwareType_Unknown);
		if (showType)
			mEntryView.InsertColumn(cols++, L"Type", 100);

		mEntryView.InsertColumn(cols++, L"Use for", 100);

		std::fill(mDefaultIds, mDefaultIds + vdcountof(mDefaultIds), 0);

		for(const ATUIFirmwareCategoryEntry& category : kATUIFirmwareCategories) {
			for(const ATFirmwareTypeEntry& entry : category.mEntries) {
				const ATFirmwareType type = entry.mType;

				mDefaultIds[type] = mFwManager.GetDefaultFirmware(type);
			}
		}

		typedef vdvector<ATFirmwareInfo> Firmwares;
		Firmwares firmwares;
		mFwManager.GetFirmwareList(firmwares);

		vdvector<vdrefptr<FirmwareItem>> items;

		for(const ATFirmwareInfo& fwInfo : firmwares) {
			if (!fwInfo.mbVisible || (mType != kATFirmwareType_Unknown && fwInfo.mType != mType))
				continue;

			vdrefptr<FirmwareItem> item(new FirmwareItem(fwInfo.mId, fwInfo.mType, fwInfo.mName.c_str(), fwInfo.mPath.c_str(), showType));
			item->mFlags = fwInfo.mFlags;
			item->mbDefault = (mDefaultIds[fwInfo.mType] == fwInfo.mId);
			item->mSpecificMask = GetItemSpecificMask(*item);

			items.emplace_back(std::move(item));
		}

		for(const auto& item : items) {
			int index = mEntryView.AddVirtualItem(*item);

			mEntryView.SetItemImage(index, item->mbDefault ? 1 : 0);
		}

		UpdateNoFirmwareNodes();

		mEntryView.AutoSizeColumns(true);
		mEntryView.Sort(mComparer);

		mEntryView.SetRedraw(true);
	}
}

bool ATUIDialogFirmware::OnCommand(uint32 id, uint32 extcode) {
	switch(id) {
		case IDC_ADD:
			Add();
			return true;

		case IDC_REMOVE:
			Remove();
			return true;

		case IDC_CLEAR:
			if (Confirm(L"This will remove all non-built-in firmware entries. Are you sure?")) {

				vdvector<ATFirmwareInfo> fws;
				mFwManager.GetFirmwareList(fws);

				for(auto it = fws.begin(), itEnd = fws.end(); it != itEnd; ++it) {
					mFwManager.RemoveFirmware(it->mId);
				}

				OnDataExchange(false);
			}
			return true;

		case IDC_SETTINGS:
			EditSettings();
			return true;

		case IDC_SCAN:
			ATUIScanForFirmware((VDGUIHandle)mhdlg, mFwManager);
			OnDataExchange(false);
			return true;

		case IDC_SETASDEFAULT:
			SetAsDefault();
			return true;

		case IDC_SETASSPECIFIC:
			SetAsSpecific();
			return true;

		case IDC_AUDIT:
			Audit();
			return true;
	}

	return false;
}

void ATUIDialogFirmware::OnDropFiles(IVDUIDropFileList *dropFileList) {
	VDStringW fn;

	try {
		if (dropFileList->GetFileName(0, fn))
			Add(fn.c_str());
	} catch(const MyError& e) {
		ShowError(e);
	}
}

void ATUIDialogFirmware::Add() {
	const VDStringW& path = BrowseForFirmware(this);

	if (path.empty())
		return;

	Add(path.c_str());
}

void ATUIDialogFirmware::Add(const wchar_t *path) {
	const uint64 id = ATGetFirmwareIdFromPath(path);

	vdrefptr<FirmwareItem> newItem(new FirmwareItem(id, kATFirmwareType_Unknown, VDFileSplitExtLeft(VDStringW(VDFileSplitPath(path))).c_str(), path, mType == kATFirmwareType_Unknown));

	// try to autodetect it
	ATSpecificFirmwareType specificType = kATSpecificFirmwareType_None;
	try {
		VDFile f(path);

		sint64 size = f.size();

		if (ATFirmwareAutodetectCheckSize(size)) {
			uint32 size32 = (uint32)size;
			vdblock<char> buf(size32);

			f.read(buf.data(), size32);

			// check if the ROM file is a constant byte value
			if (!buf.empty())
			{
				auto it = std::find_if_not(buf.begin(), buf.end(), [v = buf.front()](char c) { return c == v; });

				if (it == buf.end()) {
					if (!Confirm2("blankFirmware", L"The selected file is blank and has no firmware data. Use it anyway?", L"Blank firmware file"))
						return;
				}
			}

			ATFirmwareInfo info;
			sint32 knownFirmwareIndex = -1;
			switch (ATFirmwareAutodetect(buf.data(), size32, info, specificType, knownFirmwareIndex)) {
				case ATFirmwareDetection::SpecificImage:
					newItem->mText = info.mName;
					newItem->mFlags = info.mFlags;
					newItem->mType = info.mType;
					break;

				case ATFirmwareDetection::TypeOnly:
					newItem->mType = info.mType;
					break;

				case ATFirmwareDetection::None:
					break;
			}
		}
	} catch(const MyError&) {
	}

	ATUIDialogEditFirmwareSettings dlg2(*newItem);
	if (dlg2.ShowDialog(this)) {
		if (mEntryView.AddVirtualItem(*newItem) >= 0) {
			UpdateFirmwareItem(*newItem);

			if (specificType && !mFwManager.GetSpecificFirmware(specificType))
				mFwManager.SetSpecificFirmware(specificType, newItem->mId);

			UpdateSpecificNodes(newItem);
			UpdateNoFirmwareNodes();

			mEntryView.Sort(mComparer);

			int index = mEntryView.FindVirtualItem(*newItem);
			mEntryView.RefreshItem(index);
			mEntryView.EnsureItemVisible(index);
			mEntryView.SetSelectedIndex(index);

			mbAnyChanges = true;
		}
	}
}

void ATUIDialogFirmware::Remove() {
	vdrefptr<FirmwareItem> item(static_cast<FirmwareItem *>(mEntryView.GetSelectedVirtualItem()));
	if (!item)
		return;

	if (item->mId < kATFirmwareId_Custom)
		return;

	mEntryView.DeleteItem(mEntryView.GetSelectedIndex());
	mFwManager.RemoveFirmware(item->mId);

	UpdateNoFirmwareNodes();
	mbAnyChanges = true;
}

void ATUIDialogFirmware::EditSettings() {
	vdrefptr<FirmwareItem> item(static_cast<FirmwareItem *>(mEntryView.GetSelectedVirtualItem()));
	if (!item)
		return;

	if (item->mId < kATFirmwareId_Custom)
		return;

	ATUIDialogEditFirmwareSettings dlg2(*item);
	VDStringW name(item->mText);
	if (dlg2.ShowDialog(this)) {
		mEntryView.RefreshItem(mEntryView.GetSelectedIndex());

		UpdateFirmwareItem(*item);
		UpdateNoFirmwareNodes();

		mEntryView.Sort(mComparer);

		int index = mEntryView.FindVirtualItem(*item);
		mEntryView.EnsureItemVisible(index);
		mEntryView.SetSelectedIndex(index);

		mbAnyChanges = true;
	}
}

void ATUIDialogFirmware::SetAsDefault() {
	vdrefptr<FirmwareItem> item(static_cast<FirmwareItem *>(mEntryView.GetSelectedVirtualItem()));
	if (!item)
		return;

	int num = mEntryView.GetItemCount();

	for(int i=0; i<num; ++i) {
		auto& fwitem = *static_cast<FirmwareItem *>(mEntryView.GetVirtualItem(i));

		if (fwitem.mType != item->mType)
			continue;

		bool isDefault = (&fwitem == &*item);
		if (fwitem.mbDefault != isDefault) {
			fwitem.mbDefault = isDefault;

			mEntryView.SetItemImage(i, fwitem.mbDefault ? 1 : 0);
			mEntryView.RefreshItem(i);
		}
	}

	mFwManager.SetDefaultFirmware(item->mType, item->mId);
	mbAnyChanges = true;
}

void ATUIDialogFirmware::SetAsSpecific() {
	vdrefptr<FirmwareItem> item(static_cast<FirmwareItem *>(mEntryView.GetSelectedVirtualItem()));
	if (!item || !item->mId)
		return;

	VDLinearAllocator alloc;
	vdfastvector<const wchar_t *> items(1, L"Clear compatibility flags");
	vdfastvector<ATSpecificFirmwareType> menuLookup(1, kATSpecificFirmwareType_None);

	VDStringW label;
	for(uint32 i = 1; i < kATSpecificFirmwareTypeCount; ++i) {
		const auto ft = (ATSpecificFirmwareType)i;

		if (ATIsSpecificFirmwareTypeCompatible(item->mType, ft)) {
			label.sprintf(L"Use for software requiring: %ls", GetSpecificFirmwareLabel(ft));

			size_t len = sizeof(wchar_t) * (label.size() + 1);
			wchar_t *buf = (wchar_t *)alloc.Allocate(len);
			memcpy(buf, label.c_str(), len);
			items.push_back(buf);
			menuLookup.push_back(ft);
		}
	}

	items.push_back(nullptr);

	int index = ActivateMenuButton(IDC_SETASSPECIFIC, items.data());
	if (index == 0) {
		for(uint32 i = 1; i < kATSpecificFirmwareTypeCount; ++i) {
			ATSpecificFirmwareType ft = (ATSpecificFirmwareType)i;

			if (mFwManager.GetSpecificFirmware(ft) == item->mId)
				mFwManager.SetSpecificFirmware(ft, 0);
		}

		UpdateSpecificNodes(item);
	} else if (index > 0 && index < (int)menuLookup.size()) {
		const ATSpecificFirmwareType ft = menuLookup[index];

		uint64 prevId = mFwManager.GetSpecificFirmware(ft);
		if (prevId == item->mId)
			mFwManager.SetSpecificFirmware(ft, 0);
		else {
			FirmwareItem *itemToRefresh = nullptr;

			if (prevId) {
				int num = mEntryView.GetItemCount();
				for(int i = 0; i < num; ++i) {
					FirmwareItem *curItem = static_cast<FirmwareItem *>(mEntryView.GetVirtualItem(i));

					if (curItem && curItem->mId == prevId) {
						itemToRefresh = curItem;
						break;
					}
				}
			}

			mFwManager.SetSpecificFirmware(ft, item->mId);

			if (itemToRefresh)
				UpdateSpecificNodes(itemToRefresh);
		}

		UpdateSpecificNodes(item);
		mEntryView.RefreshItem(mEntryView.GetSelectedIndex());
	}
}

void ATUIDialogFirmware::Audit() {
	ATUIDialogKnownFirmwareAudit dlg(mFwManager);
	dlg.ShowDialog(this);
}

void ATUIDialogFirmware::UpdateFirmwareItem(const FirmwareItem& item) {
	ATFirmwareInfo info;
	info.mId = item.mId;
	info.mName = item.mText;
	info.mPath = item.mPath;
	info.mType = item.mType;
	info.mFlags = item.mFlags;
	mFwManager.AddFirmware(info);
}

uint32 ATUIDialogFirmware::GetItemSpecificMask(FirmwareItem& item) const {
	const auto id = item.mId;
	uint32 specificMask = 0;

	for(uint32 i = 1; i < kATSpecificFirmwareTypeCount; ++i) {
		auto type = (ATSpecificFirmwareType)i;

		if (id && mFwManager.GetSpecificFirmware(type) == id)
			specificMask |= 1 << i;
	}

	return specificMask;
}

void ATUIDialogFirmware::UpdateSpecificNodes(FirmwareItem *item) {
	if (!item)
		return;

	const uint32 specificMask = GetItemSpecificMask(*item);

	if (item->mSpecificMask != specificMask) {
		item->mSpecificMask = specificMask;

		mEntryView.RefreshItem(mEntryView.FindVirtualItem(*item));
	}
}

void ATUIDialogFirmware::UpdateNoFirmwareNodes() {
	if (mType != kATFirmwareType_Unknown)
		return;

	// build list of all represented firmware types (except for no-firmware
	// placeholders)
	const int n = mEntryView.GetItemCount();
	vdfastvector<ATFirmwareType> foundTypes;
	vdfastvector<ATFirmwareType> havePlaceholderTypes;

	for(int i = 0; i < n; ++i) {
		FirmwareItem *item = static_cast<FirmwareItem *>(mEntryView.GetVirtualItem(i));

		if (item) {
			if (item->mId)
				foundTypes.push_back(item->mType);
			else
				havePlaceholderTypes.push_back(item->mType);
		}
	}

	std::sort(foundTypes.begin(), foundTypes.end());
	foundTypes.erase(std::unique(foundTypes.begin(), foundTypes.end()), foundTypes.end());

	// remove all no-firmware placeholders that now have represented types
	for(int i = n - 1; i >= 0; --i) {
		FirmwareItem *item = static_cast<FirmwareItem *>(mEntryView.GetVirtualItem(i));

		if (item && !item->mId && std::binary_search(foundTypes.begin(), foundTypes.end(), item->mType))
			mEntryView.DeleteItem(i);
	}

	std::sort(havePlaceholderTypes.begin(), havePlaceholderTypes.end());
	havePlaceholderTypes.erase(std::unique(havePlaceholderTypes.begin(), havePlaceholderTypes.end()), havePlaceholderTypes.end());

	// add any missing placeholders
	bool added = false;

	for(const ATUIFirmwareCategoryEntry& category : kATUIFirmwareCategories) {
		for(const ATFirmwareTypeEntry& entry : category.mEntries) {
			const ATFirmwareType type = entry.mType;

			if (!std::binary_search(foundTypes.begin(), foundTypes.end(), type)
				&& !std::binary_search(havePlaceholderTypes.begin(), havePlaceholderTypes.end(), type)) {
				vdrefptr<FirmwareItem> item(new FirmwareItem(0, type, L"- No firmware -", L"", true));

				mEntryView.InsertVirtualItem(-1, item);
				added = true;
			}
		}
	}

	if (added) {
		mEntryView.AutoSizeColumns(true);
		mEntryView.Sort(mComparer);
	}
}

void ATUIDialogFirmware::UpdateTypeLabel() {
	const wchar_t *label = ATUITryGetFirmwareLongTypeName(mType);

	mTypeView.SetCaption(label ? label : L"(All Types)");
}

void ATUIDialogFirmware::OnSelectType() {
	ATUIPopupMenuBuilder builder;

	vdfastvector<ATFirmwareType> types;

	types.emplace_back(kATFirmwareType_Unknown);
	builder.AddItem(L"(All types)");
	builder.AddSeparator();

	ATUIBuildFirmwareTypeMenu(types, builder);

	uint32 selectedId = ActivateCommandPopupMenuReturnId(mTypeView, builder.GetPopupMenu(), nullptr);
	int index = builder.GetIndexFromItemId(selectedId);

	if (index >= 0) {
		ATFirmwareType type = types[index];

		if (mType != type) {
			mType = type;

			UpdateTypeLabel();
			OnDataExchange(false);
		}
	}
}

void ATUIDialogFirmware::OnSelChanged(VDUIProxyListView *sender, int idx) {
	if (!mbDeferredSelChanged) {
		mbDeferredSelChanged = true;

		PostCall([this] { OnDeferredSelChanged(); });
	}
}

void ATUIDialogFirmware::OnDeferredSelChanged() {
	if (!mbDeferredSelChanged)
		return;

	mbDeferredSelChanged = false;

	FirmwareItem *pItem = static_cast<FirmwareItem *>(mEntryView.GetSelectedVirtualItem());

	if (pItem && pItem->mId) {
		EnableControl(IDC_REMOVE, pItem->mId >= kATFirmwareId_Custom);
		EnableControl(IDC_SETTINGS, pItem->mId >= kATFirmwareId_Custom);
		EnableControl(IDC_SETASDEFAULT, true);
		EnableControl(IDC_SETASSPECIFIC, true);
	} else {
		EnableControl(IDC_REMOVE, false);
		EnableControl(IDC_SETTINGS, false);
		EnableControl(IDC_SETASDEFAULT, false);
		EnableControl(IDC_SETASSPECIFIC, false);
	}
}

void ATUIDialogFirmware::OnItemDoubleClicked(VDUIProxyListView *sender, int index) {
	FirmwareItem *pItem = static_cast<FirmwareItem *>(sender->GetSelectedVirtualItem());

	if (pItem && pItem->mId >= kATFirmwareId_Custom) {
		EditSettings();
	}
}

bool ATUIDialogFirmware::OnItemLabelChanging(VDUIProxyListView::LabelChangingEvent& event) {
	FirmwareItem *pItem = static_cast<FirmwareItem *>(mEntryView.GetVirtualItem(event.mIndex));

	if (!pItem || pItem->mId < kATFirmwareId_Custom)
		return false;

	event.mReplacementEditText = pItem->mText;
	event.mbUseReplacementEditText = true;
	return true;
}

void ATUIDialogFirmware::OnItemLabelChanged(VDUIProxyListView *sender, VDUIProxyListView::LabelChangedEvent *event) {
	FirmwareItem *pItem = static_cast<FirmwareItem *>(mEntryView.GetVirtualItem(event->mIndex));

	if (pItem && pItem->mId >= kATFirmwareId_Custom && event->mpNewLabel) {
		pItem->mText = event->mpNewLabel;
		event->mbAllowEdit = true;
	} else {
		event->mbAllowEdit = false;
	}
}

bool ATUIDialogFirmware::OnItemCustomStyle(IVDUIListViewVirtualItem& item, sint32& color, bool& bold) {
	FirmwareItem& fwItem = static_cast<FirmwareItem&>(item);

	if (fwItem.mId < kATFirmwareId_Custom) {
		color = ATUIGetThemeColors().mDisabledFg;
		bold = false;
		return true;
	}

	return false;
}

void ATUIDialogFirmware::OnItemContextMenu(VDUIProxyListView::ContextMenuEvent& event) {
	if (event.mIndex >= 0) {
		mEntryView.SetSelectedIndex(event.mIndex);

		switch(ActivateCommandPopupMenuReturnId(event.mX, event.mY, IDR_FIRMWARE_CONTEXT_MENU, nullptr)) {
			case ID_POPUP_SETASDEFAULT:
				SetAsDefault();
				break;

			case ID_POPUP_EDIT:
				EditSettings();
				break;
		}

		event.mbHandled = true;
	}
}

////////////////////////////////////////////////////////////////////////////////

void ATUIShowDialogFirmware(VDGUIHandle hParent, ATFirmwareManager& fw, bool *anyChanges) {
	ATUIDialogFirmware dlg(fw);

	dlg.ShowDialog(hParent);

	*anyChanges = dlg.AnyChanges();
}
