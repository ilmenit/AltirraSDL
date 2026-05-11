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

#ifndef f_AT_UILISTVIEW_H
#define f_AT_UILISTVIEW_H

#include <vd2/system/error.h>	// local: lowercase for case-sensitive filesystems
#include <vd2/system/function.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/vdstl_any.h>
#include <vd2/system/VDString.h>
#include <vd2/VDDisplay/font.h>
#include <at/atui/uicontainer.h>

class ATUISlider;

class IATUIListViewVirtualItem : public IVDRefUnknown {
public:
	virtual void GetText(VDStringW& s) = 0;
};

class IATUIListViewSorter {
public:
	virtual bool Compare(IATUIListViewVirtualItem *a, IATUIListViewVirtualItem *b) const = 0;
};

struct ATUIListViewItem {
	vdrefptr<ATUIWidget> mpView;
	VDAny mData;
};

class IATUIListViewItemPresenter : public IVDRefCount {
public:
	virtual vdrefptr<ATUIWidget> CreateView() = 0;
	virtual void SetViewData(ATUIWidget& w, const VDAny& data) = 0;
};

class ATUIListView : public ATUIContainer {
public:
	enum {
		kActionMoveFirst = kActionCustom,
		kActionMoveLast,
		kActionMoveUp,
		kActionMoveDown,
		kActionMovePagePrev,
		kActionMovePageNext,
		kActionActivateItem
	};

	ATUIListView();
	~ATUIListView();

	void SetItemHeight(sint32 height);
	void SetItemPresenter(IATUIListViewItemPresenter *presenter);

	void AddItem(const wchar_t *text);
	void AddItem(IATUIListViewVirtualItem *item);

	template<typename T> requires(!std::is_same_v<std::decay_t<T>, VDAny>)
	void AddItemData(T&& data) {
		InsertRawItemData(INT32_MAX, VDAny(std::forward<T>(data)));
	}

	void InsertItem(sint32 pos, const wchar_t *text);
	void InsertItem(sint32 pos, IATUIListViewVirtualItem *text);

	template<typename T> requires(!std::is_same_v<std::decay_t<T>, VDAny>)
	void InsertItemData(sint32 pos, T&& data) {
		InsertRawItemData(pos, VDAny(std::forward<T>(data)));
	}

	void InsertRawItemData(sint32 pos, VDAny data);

	void RemoveItem(sint32 pos);
	void RemoveAllItems();

	void Sort(const IATUIListViewSorter& item);

	IATUIListViewVirtualItem *GetSelectedVirtualItem();
	sint32 GetSelectedItem() const { return mSelectedIndex; }

	template<typename T>
	T* GetSelectedItemData() const {
		const VDAny *p = GetSelectedRawItemData();

		return p ? p->TryGetValue<T>() : nullptr;
	}

	template<typename T>
	const T& GetSelectedItemDataOrDefault() const {
		const VDAny *p = GetSelectedRawItemData();

		return p ? p->ValueOrDefault<T>() : T{};
	}

	const VDAny *GetSelectedRawItemData() const;

	void SetSelectedItem(sint32 idx);
	void EnsureSelectedItemVisible(bool fullyVisible);

	void ScrollToPixel(sint32 py, bool updateSlider);

	vdfunction<void(sint32)>& OnItemSelectedEvent() { return mItemSelectedEvent; }
	vdfunction<void(sint32)>& OnItemActivatedEvent() { return mItemActivatedEvent; }

public:
	void OnMouseDownL(sint32 x, sint32 y) override;
	void OnMouseDblClkL(sint32 x, sint32 y) override;
	bool OnMouseWheel(sint32 x, sint32 y, float delta, bool doPages) override;

	void OnActionStart(uint32 id) override;
	void OnActionRepeat(uint32 id) override;

	void OnCreate() override;
	void OnDestroy() override;
	void OnSize() override;
	void OnSetFocus() override;
	void OnKillFocus() override;

	void Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) override;

protected:
	void OnScroll(sint32 pos);
	void RecomputeSlider();
	void RemakeViews();
	void RecycleView(vdrefptr<ATUIWidget>&& viewRef);
	void DestroyViewCache();

	sint32	mScrollY;
	sint32	mSelectedIndex;
	sint32	mItemHeight;
	float	mScrollAccum;

	uint32	mTextColor;
	uint32	mHighlightBackgroundColor;
	uint32	mHighlightTextColor;
	uint32	mInactiveHighlightBackgroundColor;

	VDStringW	mTempStr;

	vdrefptr<IVDDisplayFont> mpFont;
	vdvector<ATUIListViewItem> mItems;

	vdrefptr<IATUIListViewItemPresenter> mpItemPresenter;
	sint32 mItemViewBegin = 0;
	sint32 mItemViewEnd = 0;

	vdfastvector<ATUIWidget *> mViewCache;

	ATUISlider *mpSlider = nullptr;

	vdfunction<void(sint32)> mItemSelectedEvent;
	vdfunction<void(sint32)> mItemActivatedEvent;
};

#endif
