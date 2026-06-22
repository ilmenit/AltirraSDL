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

#ifndef f_AT_UIQUICKBAR_H
#define f_AT_UIQUICKBAR_H

#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <at/atui/uiwidget.h>
#include <at/atui/uicontainer.h>

struct ATUICommand;
class IVDDisplayFont;

class ATUIQuickBarWidget final : public ATUIContainer {
public:
	static constexpr auto kTypeID = "ATUIQuickBarWidget"_vdtypeid;

	ATUIQuickBarWidget();
	~ATUIQuickBarWidget();

	void SetBaseIconSize(sint32 size);
	void SetAutoDestroyOnLoseFocus();

	void SetSubmenuOverlay(const VDPixmap& px);

	void Clear();
	void AddCommand(const char *cmdName, const wchar_t *label, const VDPixmap *icon, bool subMenuOverlay = false);
	void AddItem(uint32 numCommands);
	void AddSeparator();
	void BeginSubMenu();
	void EndSubMenu(const wchar_t *label, const VDPixmap *icon);
	void FinalizeItems();

	// Force the quick bar to re-query the status of all commands. This is a
	// workaround for it not being able to get update notifications for command
	// status without polling.
	void ForceRefresh();

	void SetOnItemActivated(vdfunction<void()> fn);
	void SetOnCaptureLost(vdfunction<void()> fn);
	void SetOnSubMenuChange(vdfunction<void(bool opened)> fn);

private:
	struct IconInfo;
	struct ItemInfo;
	struct CommandEntry;

	void OnCreate() override;
	void OnDestroy() override;
	void OnSize() override;
	void OnDeactivate() override;

	ATUIWidgetMetrics OnMeasure() override;

	void Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) override;

	void OnMouseDownL(sint32 x, sint32 y) override;
	void OnMouseUpL(sint32 x, sint32 y) override;
	void OnMouseMove(sint32 x, sint32 y) override;
	void OnMouseLeave() override;
	void OnCaptureLost() override;

	sint32 PointToItem(sint32 x, sint32 y) const;
	void SelectItem(sint32 item, bool depressed);
	void InvalidateItem(sint32 item);

	void InitSubMenu(vdspan<const CommandEntry> commands, vdspan<const ItemInfo> items, vdspan<const vdrefptr<IconInfo>> icons);

	void UpdateItemSize();
	void UpdateItems();
	void UpdateScaledIcon(IconInfo& iconInfo);
	void ScaleIcon(VDPixmap& dst, const VDPixmap& src);

	void ActivateItem(uint32 idx);
	void ActivateCommand(uint32 idx);

	void CloseSubMenu();

	sint32 mMouseDownItem = -1;
	sint32 mSelectedItem = -1;
	bool mbSelectedItemDepressed = true;

	bool mbVertical = false;
	bool mbBuildingSubMenu = false;
	bool mbIsSubMenu = false;

	sint32 mMarginX = 2;
	sint32 mMarginY = 2;
	sint32 mSpacing = 2;
	sint32 mSeparatorLength = 1;
	sint32 mItemWidth = 1;
	sint32 mItemHeight = 1;
	sint32 mBaseItemSize = 24;
	sint32 mWidth = 0;
	sint32 mHeight = 0;

	sint32 mNumTopLevelItems = 0;
	sint32 mNumSeparators = 0;

	enum class CommandMode : uint8 {
		Normal,
		TurnOn,
		TurnOff,
		Invert
	};

	struct CommandEntry {
		const ATUICommand *mpCommand = nullptr;
		VDStringW mText;
		sint32 mIconIndex;
		CommandMode mMode = CommandMode::Normal;
		bool mbCmdQuiet = false;
	};

	struct ItemInfo {
		uint32 mCommandStartIndex = 0;
		uint32 mCommandCount = 0;
		uint32 mActiveCommandIndex = 0;
		uint32 mNextCommandIndex = 0;
		bool mbActiveState = false;
		bool mbSubItem = false;
		bool mbSubMenu = false;
		bool mbAutoCloseSubMenu = false;
	};

	vdvector<CommandEntry> mCommands;
	vdvector<ItemInfo> mItems;

	vdvector<vdrefptr<IconInfo>> mIcons;

	vdrefptr<IVDDisplayFont> mpFont;

	vdrefptr<ATUIQuickBarWidget> mpActiveSubMenu;

	vdfunction<void()> mpOnItemActivated;
	vdfunction<void()> mpOnCaptureLost;

	vdfunction<void(bool opened)> mpOnSubMenuChange;

	VDPixmapBuffer mOverlayImage;
	VDPixmapBuffer mScaledOverlayImage;
};

#endif
