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
#include <vd2/VDDisplay/textrenderer.h>
#include <at/atui/uianchor.h>
#include <at/atui/uimanager.h>
#include <at/atui/uicontainer.h>
#include <at/atui/uidrawingutils.h>
#include <at/atui/uidragdrop.h>
#include <at/atui/uiwidget.h>
#include <at/atui/uiwidgetanimator.h>

ATUIWidget::ATUIWidget()
	: mpManager(NULL)
	, mpParent(NULL)
	, mArea(0, 0, 0, 0)
	, mClientArea(0, 0, 0, 0)
	, mClientOrigin(0, 0)
	, mFillColor(0xFF000000)
	, mCursorImage(0)
	, mDockMode(kATUIDockMode_None)
	, mFrameMode(kATUIFrameMode_None)
	, mTouchMode(kATUITouchMode_Default)
	, mpAnchor(NULL)
	, mInstanceId(0)
	, mOwnerId(0)
	, mbActivated(false)
	, mbVisible(true)
	, mbEnabled(true)
	, mbDropTarget(false)
	, mbFastClip(false)
	, mbHitTransparent(false)
	, mPointersOwned(0)
	, mSizeOffset(-1, -1)
	, mAnchors(0, 0, 0, 0)
	, mOffset(0, 0)
	, mPivot{0, 0}
	, mbFixedPosition(true)
	, mbForcedSize(false)
	, mbAutoSize(true)
	, mbMeasureCacheValid(false)
{
}

ATUIWidget::~ATUIWidget() {
	RemoveAllAnimators();

	vdsaferelease <<= mpAnchor;
}

void *ATUIWidget::AsInterface(uint32 iid) {
	return nullptr;
}

ATUICloseResult ATUIWidget::Close() {
	Destroy();

	return ATUICloseResult::Success;
}

void ATUIWidget::Destroy() {
	if (mpManager && mpManager->GetModalWindow() == this)
		mpManager->EndModal();

	if (mpParent)
		mpParent->RemoveChild(this);
}

void ATUIWidget::Focus() {
	if (mpManager)
		mpManager->SetActiveWindow(this);
}

ATUIWidget *ATUIWidget::GetOwner() const {
	if (!mOwnerId || !mpManager)
		return NULL;

	return mpManager->GetWindowByInstance(mOwnerId);
}

void ATUIWidget::SetOwner(ATUIWidget *w) {
	mOwnerId = w->GetInstanceId();
}

ATUIWidget *ATUIWidget::GetParentOrOwner() const {
	ATUIWidget *w = GetOwner();

	if (!w)
		w = GetParent();

	return w;
}

bool ATUIWidget::HasFocus() const {
	return mpManager && mpManager->GetFocusWindow() == this;
}

bool ATUIWidget::HasCursor() const {
	return mPointersOwned != 0;
}

bool ATUIWidget::IsCursorCaptured() const {
	return mpManager && mpManager->GetCursorCaptureWindow() == this;
}

void ATUIWidget::CaptureCursor(bool motionMode, bool constrained) {
	if (mpManager)
		mpManager->CaptureCursor(this, motionMode, constrained);
}

void ATUIWidget::ReleaseCursor() {
	if (mpManager)
		mpManager->CaptureCursor(NULL);
}

void ATUIWidget::SetFillColor(uint32 color) {
	SetAlphaFillColor(color | 0xFF000000);
}

void ATUIWidget::SetAlphaFillColor(uint32 color) {
	if (mFillColor != color) {
		mFillColor = color;
		Invalidate();
	}
}

void ATUIWidget::SetFrameMode(ATUIFrameMode frameMode) {
	if (mFrameMode != frameMode) {
		mFrameMode = frameMode;

		RecomputeClientArea();
	}
}

void ATUIWidget::SetCursorImage(uint32 id) {
	if (mCursorImage == id)
		return;

	mCursorImage = id;

	if (mpManager)
		mpManager->UpdateCursorImage(this);
}

vdrect32 ATUIWidget::GetClientArea() const {
	return vdrect32(mClientOrigin.x, mClientOrigin.y, mClientOrigin.x + mClientArea.width(), mClientOrigin.y + mClientArea.height());
}

void ATUIWidget::SetPosition(const vdpoint32& pt) {
	SetAnchors(vdrect32f(0, 0, 0, 0));
	SetOffset(pt);
	SetPivot(vdfloat2 { 0, 0 });
}

void ATUIWidget::SetAutoSize() {
	if (mSizeOffset.w >= 0 || mbForcedSize || !mbAutoSize) {
		mSizeOffset.w = -1;
		mSizeOffset.h = -1;
		mbForcedSize = false;
		mbAutoSize = true;

		InvalidateArrange();
	}
}

void ATUIWidget::SetSizeOffset(const vdsize32& sz) {
	vdsize32 sz2(sz);

	if (sz2.w < 0)
		sz2.w = 0;

	if (sz2.h < 0)
		sz2.h = 0;

	if (mSizeOffset != sz || mbForcedSize || mbAutoSize) { 
		mSizeOffset = sz;
		mbForcedSize = false;
		mbAutoSize = false;

		InvalidateArrange();
	}
}

void ATUIWidget::SetSize(const vdsize32& sz) {
	vdsize32 sz2(sz);

	if (sz2.w < 0)
		sz2.w = 0;

	if (sz2.h < 0)
		sz2.h = 0;

	if (mSizeOffset != sz || !mbForcedSize || mbAutoSize) {
		mSizeOffset = sz;
		mbForcedSize = true;
		mbAutoSize = true;

		mAnchors.right = mAnchors.left;
		mAnchors.bottom = mAnchors.top;

		mbFixedPosition = (mAnchors == vdrect32f(0, 0, 0, 0));

		InvalidateArrange();
	}
}

void ATUIWidget::SetArea(const vdrect32& r) {
	bool changed = false;

	if (mAnchors != vdrect32f(0, 0, 0, 0)) {
		mAnchors = vdrect32f(0, 0, 0, 0);
		mbFixedPosition = true;
		changed = true;
	}

	if (mOffset != r.top_left()) {
		mOffset = r.top_left();
		changed = true;
	}

	vdsize32 size = r.size();

	if (size.w < 0)
		size.w = 0;

	if (size.h < 0)
		size.h = 0;

	if (mSizeOffset != size) {
		mSizeOffset = size;

		InvalidateArrange();
		changed = true;
	}

	mbForcedSize = true;
	mbAutoSize = false;

	if (!changed)
		return;

	vdrect32 r2(r.left, r.top, r.left + size.w, r.top + size.h);

	Arrange(r2);
}

void ATUIWidget::SetAnchors(const vdrect32f& anchors) {
	if (mAnchors != anchors) {
		mAnchors = anchors;

		mbFixedPosition = (anchors == vdrect32f(0, 0, 0, 0));

		if (mpParent)
			mpParent->InvalidateLayout(this);
	}
}

void ATUIWidget::SetOffset(const vdpoint32& offset) {
	if (mOffset != offset) {
		mOffset = offset;

		if (mpParent)
			mpParent->InvalidateLayout(this);
	}
}

void ATUIWidget::SetPivot(const vdfloat2& pivot) {
	if (mPivot != pivot) {
		mPivot = pivot;

		if (mpParent)
			mpParent->InvalidateLayout(this);
	}
}

void ATUIWidget::SetPlacement(const vdrect32f& anchors, const vdpoint32& offset, const vdfloat2& pivot) {
	bool changed = false;

	if (mAnchors != anchors) {
		mAnchors = anchors;
		mbFixedPosition = (mAnchors == vdrect32f(0, 0, 0, 0));
		changed = true;
	}

	if (mOffset != offset) {
		mOffset = offset;
		changed = true;
	}

	if (mPivot != pivot) {
		mPivot = pivot;
		changed = true;
	}

	if (changed) {
		if (mpParent)
			mpParent->InvalidateLayout(this);
	}
}

void ATUIWidget::SetPlacementFill() {
	SetPlacement(vdrect32f(0, 0, 1, 1), vdpoint32(0, 0), vdfloat2{0, 0});
	SetSizeOffset(vdsize32(0, 0));
}

const ATUIWidgetMetrics& ATUIWidget::Measure() {
	if (!mbMeasureCacheValid) {
		if (IsForcedSize())
			mMeasureCache.mDesiredSize = mSizeOffset;
		else
			mMeasureCache = OnMeasure();

		mbMeasureCacheValid = true;
	}

	return mMeasureCache;
}

void ATUIWidget::Arrange(const vdrect32& r) {
	if (mArea != r) {
		const bool sizeMismatch = (mArea.size() != r.size());

		mArea = r;

		// We only need to invalidate on a change of size, not position. Otherwise, we only need to
		// invalidate the parent.
		if (sizeMismatch)
			Invalidate();
		else if (mpParent && mbVisible)
			mpParent->Invalidate();

		RecomputeClientArea();
	}
}

vdrect32 ATUIWidget::ComputeWindowSize(const vdrect32& clientArea) const {
	vdrect32 r(clientArea);

	if (mFrameMode) {
		r.left -= 2;
		r.top -= 2;
		r.right += 2;
		r.bottom += 2;
	}

	return r;
}

void ATUIWidget::SetDockMode(ATUIDockMode mode) {
	if (mDockMode == mode)
		return;

	mDockMode = mode;

	if (mpParent)
		mpParent->InvalidateLayout(this);
}

void ATUIWidget::SetAnchor(IATUIAnchor *anchor) {
	if (mpAnchor == anchor)
		return;

	if (anchor)
		anchor->AddRef();

	if (mpAnchor)
		mpAnchor->Release();

	mpAnchor = anchor;

	if (mpParent)
		mpParent->InvalidateLayout(this);
}

void ATUIWidget::InvalidateCachedAnchor() {
	if (mpAnchor && mpParent)
		mpParent->InvalidateLayout(this);
}

void ATUIWidget::SetVisible(bool visible) {
	if (mbVisible == visible)
		return;

	if (mbVisible && mpManager)
		mpManager->Invalidate(this);

	mbVisible = visible;

	if (visible)
		Invalidate();
}

void ATUIWidget::SetEnabled(bool enabled) {
	if (mbEnabled == enabled)
		return;

	mbEnabled = enabled;
	OnEnableChanged();
}

bool ATUIWidget::IsSameOrAncestorOf(ATUIWidget *w) const {
	while(w) {
		if (w == this)
			return true;

		w = w->GetParent();
	}

	return false;
}

ATUIWidget *ATUIWidget::HitTest(vdpoint32 pt) {
	return mbVisible && !mbHitTransparent && mArea.contains(pt) ? this : NULL;
}

void ATUIWidget::SetClientOrigin(vdpoint32 pt) {
	if (mClientOrigin != pt) {
		mClientOrigin = pt;

		Invalidate();
	}
}

bool ATUIWidget::TranslateScreenPtToClientPt(vdpoint32 spt, vdpoint32& cpt) {
	sint32 x = spt.x;
	sint32 y = spt.y;

	// Screen to client transforms should conceptually be done from
	// the outermost window inward, but we can do them in reverse order
	// since the transforms are commutative.
	for(ATUIWidget *w = this; w; w = w->GetParent()) {
		x -= w->mArea.left;
		y -= w->mArea.top;
		x -= w->mClientArea.left;
		y -= w->mClientArea.top;
		x += w->mClientOrigin.x;
		y += w->mClientOrigin.y;
	}

	cpt = vdpoint32(x, y);
	return (uint32)(x - mClientOrigin.x) < (uint32)mClientArea.width() && (uint32)(y - mClientOrigin.y) < (uint32)mClientArea.height();
}

bool ATUIWidget::TranslateWindowPtToClientPt(vdpoint32 wpt, vdpoint32& cpt) {
	cpt.x = wpt.x - mClientArea.left + mClientOrigin.x;
	cpt.y = wpt.y - mClientArea.top + mClientOrigin.y;

	return mClientArea.contains(wpt);
}

vdpoint32 ATUIWidget::TranslateClientPtToScreenPt(vdpoint32 cpt) {
	for(ATUIWidget *w = this; w; w = w->GetParent()) {
		cpt.x += w->mClientArea.left - w->mClientOrigin.x;
		cpt.y += w->mClientArea.top - w->mClientOrigin.y;
		cpt.x += w->mArea.left;
		cpt.y += w->mArea.top;
	}

	return cpt;
}

void ATUIWidget::UnbindAction(uint32 vk, uint32 mod) {
	auto it = std::find_if(mActionMap.begin(), mActionMap.end(),
		[=](const ATUITriggerBinding& binding) {
			return !binding.mbForward && binding.mVk == vk && binding.mModVal == mod;
		}
	);

	if (it != mActionMap.end())
		mActionMap.erase(it);
}

void ATUIWidget::UnforwardAction(uint32 action) {
	auto it = std::find_if(mActionMap.begin(), mActionMap.end(),
		[=](const ATUITriggerBinding& binding) {
			return binding.mbForward && binding.mVk == action;
		}
	);

	if (it != mActionMap.end())
		mActionMap.erase(it);
}

void ATUIWidget::UnbindAllActions() {
	mActionMap.clear();
}

void ATUIWidget::BindAction(const ATUITriggerBinding& binding) {
	mActionMap.push_back(binding);
}

void ATUIWidget::BindAction(uint32 vk, uint32 action, uint32 mod, uint32 instanceid) {
	ATUITriggerBinding binding {};
	binding.mVk = vk;
	binding.mModMask = ATUITriggerBinding::kModAll;
	binding.mModVal = mod;
	binding.mAction = action;
	binding.mbForward = false;
	binding.mTargetInstanceId = instanceid;

	BindAction(binding);
}

void ATUIWidget::ForwardAction(uint32 srcAction, uint32 targetAction, uint32 instanceid) {
	if (!instanceid) {
		VDFAIL("ForwardAction() requires non-zero instance ID");
		return;
	}

	ATUITriggerBinding binding {};
	binding.mVk = srcAction;
	binding.mAction = targetAction;
	binding.mTargetInstanceId = instanceid;
	binding.mbForward = true;

	BindAction(binding);
}

const ATUITriggerBinding *ATUIWidget::FindAction(uint32 vk, uint32 extvk, uint32 mods) const {
	for(const ATUITriggerBinding& binding : mActionMap) {
		if (!binding.mbForward && (binding.mVk == vk || binding.mVk == extvk)) {
			if (!((mods ^ binding.mModVal) & binding.mModMask))
				return &binding;
		}
	}

	return nullptr;
}

const ATUITriggerBinding *ATUIWidget::FindForwardedAction(uint32 action) const {
	for(const ATUITriggerBinding& binding : mActionMap) {
		if (binding.mbForward && binding.mVk == action)
			return &binding;
	}

	return nullptr;
}

ATUITimerHandle ATUIWidget::StartTimer(float initialDelay, float period, vdfunction<void()> fn) {
	if (!mpManager) {
		VDFAIL("Cannot start timer on an unattached widget.");
		return ATUITimerHandle();
	}

	return mpManager->StartTimer(*this, initialDelay, period, std::move(fn));
}

void ATUIWidget::StopTimer(ATUITimerHandle h) {
	if (mpManager)
		mpManager->StopTimer(h);
}

ATUITouchMode ATUIWidget::GetTouchModeAtPoint(const vdpoint32& pt) const {
	return kATUITouchMode_Default;
}

void ATUIWidget::OnMouseRelativeMove(sint32 x, sint32 y) {
}

void ATUIWidget::OnMouseMove(sint32 x, sint32 y) {
}

void ATUIWidget::OnMouseDownL(sint32 x, sint32 y) {
}

void ATUIWidget::OnMouseDblClkL(sint32 x, sint32 y) {
	OnMouseDownL(x, y);
}

void ATUIWidget::OnMouseUpL(sint32 x, sint32 y) {
}

void ATUIWidget::OnMouseDown(sint32 x, sint32 y, uint32 vk, bool dblclk) {
	if (vk == kATUIVK_LButton) {
		if (dblclk)
			OnMouseDblClkL(x, y);
		else
			OnMouseDownL(x, y);
	}
}

void ATUIWidget::OnMouseUp(sint32 x, sint32 y, uint32 vk) {
	if (vk == kATUIVK_LButton)
		OnMouseUpL(x, y);
}

bool ATUIWidget::OnMouseWheel(sint32 x, sint32 y, float delta, bool doPages) {
	return false;
}

bool ATUIWidget::OnMouseHWheel(sint32 x, sint32 y, float delta, bool doPages) {
	return false;
}

void ATUIWidget::OnMouseLeave() {
}

void ATUIWidget::OnMouseHover(sint32 x, sint32 y) {
}

bool ATUIWidget::OnContextMenu(const vdpoint32 *pt) {
	return false;
}

bool ATUIWidget::OnKeyDown(const ATUIKeyEvent& event) {
	uint32 mods = 0;

	if (mpManager->IsKeyDown(kATUIVK_Control))
		mods += ATUITriggerBinding::kModCtrl;

	if (mpManager->IsKeyDown(kATUIVK_Shift))
		mods += ATUITriggerBinding::kModShift;

	if (mpManager->IsKeyDown(kATUIVK_Alt))
		mods += ATUITriggerBinding::kModAlt;

	const ATUITriggerBinding *binding = FindAction(event.mVirtKey, event.mExtendedVirtKey, mods);
	if (binding) {
		mpManager->BeginAction(this, *binding);
		return true;
	}

	return false;
}

bool ATUIWidget::OnKeyUp(const ATUIKeyEvent& event) {
	return false;
}

bool ATUIWidget::OnChar(const ATUICharEvent& event) {
	return false;
}

bool ATUIWidget::OnCharUp(const ATUICharEvent& event) {
	return false;
}

void ATUIWidget::OnForceKeysUp() {
}

void ATUIWidget::OnActionStart(uint32 id) {
	if (id == kActionFocus)
		Focus();
}

void ATUIWidget::OnActionRepeat(uint32 trid) {
}

void ATUIWidget::OnActionStop(uint32 trid) {
}

void ATUIWidget::OnCreate() {
}

void ATUIWidget::OnDestroy() {
}

void ATUIWidget::OnSize() {
}

ATUIWidgetMetrics ATUIWidget::OnMeasure() {
	return {};
}

void ATUIWidget::OnKillFocus() {
}

void ATUIWidget::OnSetFocus() {
}

void ATUIWidget::OnCaptureLost() {
}

void ATUIWidget::OnActivate() {
}

void ATUIWidget::OnDeactivate() {
}

void ATUIWidget::OnEnableChanged() {
}

void ATUIWidget::OnTrackCursorChanges(ATUIWidget *w) {
}

ATUIWidget *ATUIWidget::DragHitTest(vdpoint32 pt) {
	return mbDropTarget ? this : nullptr;
}

ATUIDragEffect ATUIWidget::OnDragEnter(sint32 x, sint32 y, ATUIDragModifiers modifiers, IATUIDragDropObject *obj) {
	return ATUIDragEffect::None;
}

ATUIDragEffect ATUIWidget::OnDragOver(sint32 x, sint32 y, ATUIDragModifiers modifiers, IATUIDragDropObject *obj) {
	return ATUIDragEffect::None;
}

void ATUIWidget::OnDragLeave() {
}

ATUIDragEffect ATUIWidget::OnDragDrop(sint32 x, sint32 y, ATUIDragModifiers modifiers, IATUIDragDropObject *obj) {
	return ATUIDragEffect::None;
}

void ATUIWidget::AddAnimator(ATUIWidgetAnimator& anim) {
	if (anim.mpOwner) {
		VDFAIL("Attempting to add animator already added to another widget.");
		return;
	}

	anim.AddRef();
	anim.mpOwner = this;

	anim.mpNextAnimator = mpInactiveAnimators;
	mpInactiveAnimators = &anim;

	if (anim.mbActive) {
		anim.mbActive = false;
		MakeAnimationActive(anim);
	}
}

void ATUIWidget::RemoveAnimator(ATUIWidgetAnimator& anim) {
	if (anim.mpOwner != this)
		return;

	ATUIWidgetAnimator **it = anim.mbActive ? &mpActiveAnimators : &mpInactiveAnimators;
	while(ATUIWidgetAnimator *anim2 = *it) {
		if (anim2 == &anim) {
			*it = anim.mpNextAnimator;
			anim.mpNextAnimator = nullptr;
			anim.mpOwner = nullptr;
			anim.mbActive = false;
			anim.Release();
			return;
		}

		it = &anim2->mpNextAnimator;
	}

	VDFAIL("Animator not found");
}

void ATUIWidget::RemoveAllAnimators() {
	while(mpActiveAnimators)
		RemoveAnimator(*mpActiveAnimators);

	while(mpInactiveAnimators)
		RemoveAnimator(*mpInactiveAnimators);
}

void ATUIWidget::Draw(IVDDisplayRenderer& rdr) {
	if (!mbVisible)
		return;

	IVDDisplayRenderer *sr = &rdr;
	
	if (mbFastClip) {
		if (!rdr.PushViewport(mArea, mArea.left, mArea.top))
			return;
	} else {
		sr = rdr.BeginSubRender(mArea, mRenderCache);

		if (!sr)
			return;
	}

	bool drawInner = true;

	if (mFrameMode) {
		vdrect32 frameRect(0, 0, mArea.width(), mArea.height());

		switch(mFrameMode) {
			case kATUIFrameMode_Raised:
				ATUIDraw3DRect(*sr, frameRect, false);
				break;
			case kATUIFrameMode_Sunken:
				ATUIDraw3DRect(*sr, frameRect, true);
				break;
			case kATUIFrameMode_SunkenThin:
				ATUIDrawThin3DRect(*sr, frameRect, false);
				break;
			case kATUIFrameMode_RaisedEdge:
				ATUIDrawThin3DRect(*sr, frameRect, false);
				++frameRect.left;
				++frameRect.top;
				--frameRect.right;
				--frameRect.bottom;
				ATUIDrawThin3DRect(*sr, frameRect, true);
				break;
		}
	}

	drawInner = sr->PushViewport(mClientArea, mClientArea.left - mClientOrigin.x, mClientArea.top - mClientOrigin.y);

	if (drawInner) {
		if (mFillColor >= 0x01000000) {
			if (sr == &rdr && sr->GetCaps().mbSupportsAlphaBlending && mFillColor < 0xFF000000) {
				sr->AlphaFillRect(0, 0, mArea.width(), mArea.height(), mFillColor);
			} else {
				sr->SetColorRGB(mFillColor);
				sr->FillRect(0, 0, mArea.width(), mArea.height());
			}
		}

		sr->SetColorRGB(0);

		Paint(*sr, mClientArea.width(), mClientArea.height());

		sr->PopViewport();
	}

	if (mbFastClip)
		rdr.PopViewport();
	else
		rdr.EndSubRender();
}

void ATUIWidget::Invalidate() {
	if (mbVisible) {
		// Currently, the software renderer can ignore nested subcache requests, so we have
		// to invalidate all the way up the chain.
		for(ATUIWidget *w = this; w; w = w->GetParent())
			w->mRenderCache.Invalidate();

		if (mpManager)
			mpManager->Invalidate(this);
	}
}

void ATUIWidget::InvalidateMeasure() {
	mbMeasureCacheValid = false;

	if (mpParent)
		mpParent->InvalidateLayout(this);
}

void ATUIWidget::InvalidateArrange() {
	if (mpParent)
		mpParent->InvalidateLayout(this);
}

void ATUIWidget::SetParent(ATUIManager *mgr, ATUIContainer *parent) {
	if (mpManager) {
		OnDestroy();
		mpManager->Detach(this);

		if (mbVisible && !mArea.empty())
			mpManager->Invalidate(this);
	}

	mpManager = mgr;
	mpParent = parent;

	if (mgr) {
		mgr->Attach(this);
		InvalidateMeasure();
		OnCreate();

		if (mbVisible && !mArea.empty())
			mgr->Invalidate(this);
	}
}

void ATUIWidget::SetActivated(bool activated) {
	if (mbActivated != activated) {
		mbActivated = activated;

		if (activated)
			OnActivate();
		else
			OnDeactivate();
	}
}

void ATUIWidget::OnPointerEnter(uint8 bit) {
	mPointersOwned |= bit;
}

void ATUIWidget::OnPointerLeave(uint8 bit) {
	if (mPointersOwned & bit) {
		mPointersOwned -= bit;

		OnMouseLeave();
	}
}

void ATUIWidget::OnPointerClear() {
	mPointersOwned = 0;
}

void ATUIWidget::RecomputeClientArea() {
	vdrect32 clientArea(0, 0, mArea.width(), mArea.height());

	if (mFrameMode) {
		if (mFrameMode == kATUIFrameMode_SunkenThin) {
			++clientArea.left;
			++clientArea.top;
			--clientArea.right;
			--clientArea.bottom;
		} else {
			clientArea.left += 2;
			clientArea.top += 2;
			clientArea.right -= 2;
			clientArea.bottom -= 2;
		}

		if (clientArea.empty())
			clientArea.set(0, 0, 0, 0);
	}

	if (mClientArea != clientArea) {
		mClientArea = clientArea;

		Invalidate();
		OnSize();
	}
}

bool ATUIWidget::UpdateAnimation(const ATUIWidgetAnimContext& ctx) {
	ATUIWidgetAnimator **it = &mpActiveAnimators;

	if (*it) {
		while(ATUIWidgetAnimator *anim = *it) {
			ATUIWidgetAnimator::AnimResult result = anim->Animate(ctx);

			if (result == ATUIWidgetAnimator::AnimResult::Active) {
				it = &anim->mpNextAnimator;
			} else {
				anim->mbActive = false;
				*it = anim->mpNextAnimator;

				if (result == ATUIWidgetAnimator::AnimResult::Inactive) {
					anim->mpNextAnimator = mpInactiveAnimators;
					mpInactiveAnimators = anim;
				} else {
					anim->mpNextAnimator = nullptr;
					anim->mpOwner = nullptr;
					anim->Release();
				}
			}
		}
	}

	return mpActiveAnimators != nullptr;
}

void ATUIWidget::MakeAnimationInactive(ATUIWidgetAnimator& anim) {
	VDASSERT(anim.mbActive);

	ATUIWidgetAnimator **it = &mpActiveAnimators;
	while(*it) {
		if (*it == &anim) {
			*it = anim.mpNextAnimator;
			anim.mbActive = false;
			anim.mpNextAnimator = mpInactiveAnimators;
			mpInactiveAnimators = &anim;
			return;
		}

		it = &anim.mpNextAnimator;
	}

	VDFAIL("Animator not found in active list.");
}

void ATUIWidget::MakeAnimationActive(ATUIWidgetAnimator& anim) {
	VDASSERT(!anim.mbActive);

	ATUIWidgetAnimator **it = &mpInactiveAnimators;
	while(*it) {
		if (*it == &anim) {
			*it = anim.mpNextAnimator;
			anim.mbActive = true;
			anim.mpNextAnimator = mpActiveAnimators;
			mpActiveAnimators = &anim;

			if (!mbHasAnimation) {
				mbHasAnimation = true;

				ATUIContainer *c = mpParent;
				for(; c && !c->mbDescendantMayHaveAnimation; c = c->mpParent) {
					c->mbDescendantMayHaveAnimation = true;
					c->mbHasAnimation = true;
				}

				VDASSERT(!c || (c->mbHasAnimation && c->mbDescendantMayHaveAnimation));

				Invalidate();
			}
			return;
		}

		it = &anim.mpNextAnimator;
	}

	VDFAIL("Animator not found in inactive list.");
}
