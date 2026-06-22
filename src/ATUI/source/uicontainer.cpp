//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2018 Avery Lee
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
#include <at/atui/uicontainer.h>
#include <at/atui/uimanager.h>
#include <at/atui/uianchor.h>

ATUIContainer::ATUIContainer() {
	mbFastClip = true;
	SetAlphaFillColor(0);
}

ATUIContainer::~ATUIContainer() {
	RemoveAllChildren();
}

void ATUIContainer::AddChild(ATUIWidget *w) {
	w->AddRef();
	w->SetParent(mpManager, this);

	mWidgets.push_back(w);

	mbHasAnimation = true;
	mbDescendantMayHaveAnimation = true;

	if (w->IsVisible())
		Invalidate();

	InvalidateLayout(w);
}

void ATUIContainer::RemoveChild(ATUIWidget *w) {
	for(Widgets::iterator it(mWidgets.begin()), itEnd(mWidgets.end());
		it != itEnd;
		++it)
	{
		if (*it == w) {
			mWidgets.erase(it);
			w->SetParent(NULL, NULL);
			w->Release();
			InvalidateLayout(nullptr);
			break;
		}
	}
}

void ATUIContainer::RemoveAllChildren() {
	while(!mWidgets.empty()) {
		ATUIWidget *w = mWidgets.back();

		mWidgets.pop_back();

		w->SetParent(NULL, NULL);
		w->Release();
	}
}

void ATUIContainer::SendToBack(ATUIWidget *w) {
	if (!w)
		return;

	if (w->GetParent() != this) {
		VDASSERT(!"Invalid call to SendToBack().");
		return;
	}

	auto it = std::find(mWidgets.begin(), mWidgets.end(), w);
	VDASSERT(it != mWidgets.end());

	if (it != mWidgets.begin()) {
		mWidgets.erase(it);
		mWidgets.insert(mWidgets.begin(), w);
	}
}

void ATUIContainer::BringToFront(ATUIWidget *w) {
	if (!w)
		return;

	if (w->GetParent() != this) {
		VDASSERT(!"Invalid call to SendToBack().");
		return;
	}

	auto it = std::find(mWidgets.begin(), mWidgets.end(), w);
	VDASSERT(it != mWidgets.end());

	if (it != mWidgets.end() - 1) {
		mWidgets.erase(it);
		mWidgets.push_back(w);
	}
}

void ATUIContainer::InvalidateLayout(ATUIWidget *w) {
	if (mbLayoutInvalid)
		return;

	mbLayoutInvalid = true;

	if (!IsForcedSize())
		InvalidateMeasure();

	for(ATUIContainer *p = mpParent; p; p = p->mpParent) {
		if (p->mbDescendantLayoutInvalid)
			break;

		p->mbDescendantLayoutInvalid = true;
	}
}

void ATUIContainer::UpdateLayout() {
	if (!mbLayoutInvalid) {
		if (mbDescendantLayoutInvalid) {
			for(Widgets::const_reverse_iterator it(mWidgets.rbegin()), itEnd(mWidgets.rend());
				it != itEnd;
				++it)
			{
				ATUIWidget *w = *it;

				w->UpdateLayout();
			}

			mbDescendantLayoutInvalid = false;
		}

		return;
	}

	mbLayoutInvalid = false;
	mbDescendantLayoutInvalid = false;

	vdrect32 r(mClientArea);
	vdrect32 r2;

	r.translate(-r.left, -r.top);

	for(Widgets::const_reverse_iterator it(mWidgets.rbegin()), itEnd(mWidgets.rend());
		it != itEnd;
		++it)
	{
		ATUIWidget *w = *it;
		const ATUIWidgetMetrics& metrics = w->Measure();

		switch(w->GetDockMode()) {
			case kATUIDockMode_None:
				if (IATUIAnchor *anchor = w->GetAnchor()) {
					w->SetArea(anchor->Position(r, w->GetArea().size(), metrics));
				} else {
					const vdrect32f& anchors = w->GetAnchors();
					const vdpoint32& offset = w->GetOffset();
					const vdsize32& sizeOffset = w->GetSizeOffset();
					const vdfloat2& pivot = w->GetPivot();

					vdsize32 anchoredSize;
					anchoredSize.w = VDRoundToInt32(((float)r.right - (float)r.left) * (anchors.right - anchors.left));
					anchoredSize.h = VDRoundToInt32(((float)r.bottom - (float)r.top) * (anchors.bottom - anchors.top));

					vdsize32 childSize;

					if (!w->IsAutoSize()) {
						childSize = anchoredSize;
						childSize += sizeOffset;
					} else {
						childSize = metrics.mDesiredSize;
					}

					if (!w->IsForcedSize()) {
						if (childSize.w > metrics.mMaxSize.w)
							childSize.w = metrics.mMaxSize.w;

						if (childSize.h > metrics.mMaxSize.h)
							childSize.h = metrics.mMaxSize.h;

						if (childSize.w < metrics.mMinSize.w)
							childSize.w = metrics.mMinSize.w;

						if (childSize.h < metrics.mMinSize.h)
							childSize.h = metrics.mMinSize.h;
					}

					vdrect32 rChild;
					rChild.left = VDRoundToInt32((float)r.left * (1.0f - anchors.left) + (float)r.right * anchors.left + (float)(anchoredSize.w - childSize.w)*pivot.x) + offset.x;
					rChild.top = VDRoundToInt32((float)r.top * (1.0f - anchors.top) + (float)r.bottom * anchors.top + (float)(anchoredSize.h - childSize.h)*pivot.y) + offset.y;
					rChild.right = rChild.left + childSize.w;
					rChild.bottom = rChild.top + childSize.h;

					w->Arrange(rChild);
				}
				break;

			case kATUIDockMode_Left:
				r2 = r;
				r2.right = r2.left + metrics.mDesiredSize.w;
				r.left += r2.width();
				w->Arrange(r2);
				break;

			case kATUIDockMode_Right:
				r2 = r;
				r2.left = r2.right - metrics.mDesiredSize.w;
				r.right -= r2.width();
				w->Arrange(r2);
				break;

			case kATUIDockMode_Top:
				r2 = r;
				r2.bottom = r2.top + metrics.mDesiredSize.h;
				r.top += r2.height();
				w->Arrange(r2);
				break;

			case kATUIDockMode_Bottom:
				r2 = r;
				r2.top = r2.bottom - metrics.mDesiredSize.h;
				r.bottom -= r2.height();
				w->Arrange(r2);
				break;

			case kATUIDockMode_Fill:
				w->Arrange(r);
				break;
		}

		w->UpdateLayout();
	}
}

ATUIWidget *ATUIContainer::HitTest(vdpoint32 pt) {
	if (!mbVisible || !mArea.contains(pt))
		return NULL;

	pt.x -= mArea.left;
	pt.y -= mArea.top;

	if (mClientArea.contains(pt)) {
		pt.x -= mClientArea.left;
		pt.y -= mClientArea.top;
		pt.x += mClientOrigin.x;
		pt.y += mClientOrigin.y;

		for(Widgets::const_reverse_iterator it(mWidgets.rbegin()), itEnd(mWidgets.rend());
			it != itEnd;
			++it)
		{
			ATUIWidget *w = *it;

			ATUIWidget *r = w->HitTest(pt);
			if (r)
				return r;
		}
	}

	return mbHitTransparent ? NULL : this;
}

void ATUIContainer::OnDestroy() {
	RemoveAllChildren();
}

void ATUIContainer::OnSize() {
	mbLayoutInvalid = true;
	UpdateLayout();
}

ATUIWidgetMetrics ATUIContainer::OnMeasure() {
	ATUIWidgetMetrics m;

	if (!IsForcedSize()) {
		for(ATUIWidget *w : mWidgets) {
			const auto& wm = w->Measure();

			switch(w->GetDockMode()) {
				case kATUIDockMode_Left:
				case kATUIDockMode_Right:
					m.mMinSize.h = std::max<sint32>(m.mMinSize.h, wm.mMinSize.h);
					m.mMinSize.w += wm.mMinSize.w;
					m.mDesiredSize.h = std::max<sint32>(m.mDesiredSize.h, wm.mDesiredSize.h);
					m.mDesiredSize.w += wm.mDesiredSize.w;
					break;
				case kATUIDockMode_Top:
				case kATUIDockMode_Bottom:
					m.mMinSize.w = std::max<sint32>(m.mMinSize.w, wm.mMinSize.w);
					m.mMinSize.h += wm.mMinSize.h;
					m.mDesiredSize.w = std::max<sint32>(m.mDesiredSize.w, wm.mDesiredSize.w);
					m.mDesiredSize.h += wm.mDesiredSize.h;
					break;
				default:
					m.mMinSize.w = std::max<sint32>(m.mMinSize.w, wm.mMinSize.w);
					m.mMinSize.h = std::max<sint32>(m.mMinSize.h, wm.mMinSize.h);
					m.mDesiredSize.w = std::max<sint32>(m.mDesiredSize.w, wm.mDesiredSize.w);
					m.mDesiredSize.h = std::max<sint32>(m.mDesiredSize.h, wm.mDesiredSize.h);
					break;
			}
		}
	}

	return m;
}

void ATUIContainer::Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) {
	VDASSERT(mbHasAnimation || !mbDescendantMayHaveAnimation);

	for(Widgets::const_iterator it(mWidgets.begin()), itEnd(mWidgets.end());
		it != itEnd;
		++it)
	{
		ATUIWidget *w = *it;

		w->Draw(rdr);
	}
}

void ATUIContainer::OnSetFocus() {
	if (!mWidgets.empty())
		mWidgets.front()->Focus();
}

ATUIWidget *ATUIContainer::DragHitTest(vdpoint32 pt) {
	if (!mbVisible || !mArea.contains(pt))
		return NULL;

	pt.x -= mArea.left;
	pt.y -= mArea.top;

	if (mClientArea.contains(pt)) {
		pt.x -= mClientArea.left;
		pt.y -= mClientArea.top;
		pt.x += mClientOrigin.x;
		pt.y += mClientOrigin.y;

		for(Widgets::const_reverse_iterator it(mWidgets.rbegin()), itEnd(mWidgets.rend());
			it != itEnd;
			++it)
		{
			ATUIWidget *w = *it;

			ATUIWidget *r = w->DragHitTest(pt);
			if (r)
				return r;
		}
	}

	return mbDropTarget ? this : nullptr;
}

bool ATUIContainer::UpdateAnimation(const ATUIWidgetAnimContext& ctx) {
	bool newDescAnim = false;

	VDASSERT(mbHasAnimation);

	if (mbDescendantMayHaveAnimation) {
		for(ATUIWidget *w : mWidgets) {
			if (w->mbHasAnimation) {
				if (w->UpdateAnimation(ctx))
					newDescAnim = true;
				else
					w->mbHasAnimation = false;
			}
		}

		if (!newDescAnim)
			mbDescendantMayHaveAnimation = false;
	}

	if (ATUIWidget::UpdateAnimation(ctx))
		return true;

	if (!newDescAnim)
		VDASSERT(!mbDescendantMayHaveAnimation);

	return newDescAnim;
}
