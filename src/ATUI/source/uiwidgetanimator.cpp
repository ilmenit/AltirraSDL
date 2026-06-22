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
#include <at/atui/uicontainer.h>
#include <at/atui/uimanager.h>
#include <at/atui/uiwidget.h>
#include <at/atui/uiwidgetanimator.h>

ATUIWidgetAnimator::ATUIWidgetAnimator() {
}

void ATUIWidgetAnimator::Start() {
	if (mbStarted)
		return;

	mbStarted = true;
	mbStartTicked = false;
	Activate();
	OnStart();
}

void ATUIWidgetAnimator::Pause() {
	if (!mbStarted || mbPaused)
		return;

	mbPaused = true;

	if (mbStartTicked)
		Deactivate();

	OnPause();
}

void ATUIWidgetAnimator::Resume() {
	if (!mbStarted || !mbPaused)
		return;

	mbPaused = false;
	Activate();
	OnResume();
}

void ATUIWidgetAnimator::Stop() {
	if (!mbStarted)
		return;

	mbStarted = false;
	Deactivate();
}

void ATUIWidgetAnimator::OnStart() {}
void ATUIWidgetAnimator::OnPause() {}
void ATUIWidgetAnimator::OnResume() {}
void ATUIWidgetAnimator::OnStop() {}

void ATUIWidgetAnimator::Activate() {
	if (!mbActive) {
		if (mpOwner)
			mpOwner->MakeAnimationActive(*this);
		else
			mbActive = true;
	}
}

void ATUIWidgetAnimator::Deactivate() {
	if (mbActive) {
		if (mpOwner)
			mpOwner->MakeAnimationInactive(*this);
		else
			mbActive = false;
	}
}

////////////////////////////////////////////////////////////////////////////////

void ATUIWidgetOffsetAnimator::SetEndpoints(const vdfloat2& start, const vdfloat2& end) {
	mOffsetBase = start;
	mOffsetDelta = end - start;

	UpdateOutput();
}

void ATUIWidgetOffsetAnimator::SetRate(float rate) {
	mRate = rate;
}

void ATUIWidgetOffsetAnimator::SetForward(bool forward) {
	if (mbForward != forward) {
		mbForward = forward;

		Activate();
	}
}

void ATUIWidgetOffsetAnimator::SetProgress(float v) {
	v = std::clamp<float>(v, 0.0f, 1.0f);
	if (mProgress != v) {
		mProgress = v;

		UpdateOutput();
	}
}

ATUIWidgetOffsetAnimator::AnimResult ATUIWidgetOffsetAnimator::Animate(const ATUIWidgetAnimContext& ctx) {
	bool atEnd = false;

	if (mbForward) {
		mProgress += mRate * ctx.mDeltaTime;

		if (mProgress >= 1.0f) {
			mProgress = 1.0f;
			atEnd = true;
		}
	} else {
		mProgress -= mRate * ctx.mDeltaTime;

		if (mProgress <= 0.0f) {
			mProgress = 0.0f;
			atEnd = true;
		}
	}

	UpdateOutput();

	return atEnd ? AnimResult::Inactive : AnimResult::Active;
}

void ATUIWidgetOffsetAnimator::OnStart() {
	UpdateOutput();
}

void ATUIWidgetOffsetAnimator::UpdateOutput() {
	if (mpOwner) {
		float t = mProgress;

		t = (t * t) * (3.0f - 2.0f * t);

		const vdfloat2 v = mOffsetBase + mOffsetDelta * t;

		mpOwner->SetOffset(
			vdpoint32(
				VDRoundToInt32(v.x),
				VDRoundToInt32(v.y)
			)
		);
	}
}

////////////////////////////////////////////////////////////////////////////////

void ATUIWidgetRelativeAnimator::SetReference(ATUIWidget& target, const vdfloat2& relOffset, const vdpoint32& absOffset) {
	mTargetInstanceId = target.GetInstanceId();
	mRelativeOffset = relOffset;
	mAbsoluteOffset = absOffset;

	UpdateOutput();
	Start();
}

ATUIWidgetRelativeAnimator::AnimResult ATUIWidgetRelativeAnimator::Animate(const ATUIWidgetAnimContext& ctx) {
	UpdateOutput();

	return AnimResult::Active;
}

void ATUIWidgetRelativeAnimator::OnStart() {
	UpdateOutput();
}

void ATUIWidgetRelativeAnimator::UpdateOutput() {
	if (mpOwner) {
		ATUIWidget *w = mpOwner->GetManager()->GetWindowByInstance(mTargetInstanceId);

		if (w) {
			const vdrect32& r = w->GetClientArea();

			// Local AltirraSDL correction: upstream test13 used absolute right/top
			// coordinates here, which breaks non-zero relative anchors. Keep this
			// width/height-relative calculation when merging future upstream drops.
			vdpoint32 pt1 {
				r.left + VDRoundToInt32((float)r.width() * mRelativeOffset.x),
				r.top + VDRoundToInt32((float)r.height() * mRelativeOffset.y)
			};

			vdpoint32 pt2 = w->TranslateClientPtToScreenPt(pt1);
			vdpoint32 pt3;

			ATUIWidget *w2 = mpOwner->GetParent();
			if (w2)
				w2->TranslateScreenPtToClientPt(pt2, pt3);
			else
				pt3 = pt2;

			pt3.x += mAbsoluteOffset.x;
			pt3.y += mAbsoluteOffset.y;

			mpOwner->SetOffset(pt3);
		}
	}
}
