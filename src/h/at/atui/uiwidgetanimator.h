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

#ifndef f_AT_ATUI_UIWIDGETANIMATOR_H
#define f_AT_ATUI_UIWIDGETANIMATOR_H

#include <vd2/system/refcount.h>

class ATUIWidget;

struct ATUIWidgetAnimContext {
	// Seconds passed since last render while any animation is happening. When
	// animation globally starts, the first animation update occurs with
	// delta time 0 to avoid glitching animations due to arbitrary time passed.
	float mDeltaTime = 0;
};

// Base class for animating widget properties. Animators are attached to widgets
// and may be in either active or inactive state; while active, rendering is
// forced on and the animators are updated every frame just prior to rendering
// until they go inactive.
//
class ATUIWidgetAnimator : public vdrefcount {
	ATUIWidgetAnimator(const ATUIWidgetAnimator&) = delete;
	ATUIWidgetAnimator& operator=(const ATUIWidgetAnimator&) = delete;
public:
	enum class AnimResult : uint8 {
		// The animator is running and should continue to run.
		Active,

		// The animator no longer needs to run and should be moved to the
		// inactive list.
		Inactive,

		// The animator no longer needs to exist and should be deleted.
		Expired
	};

	ATUIWidgetAnimator();
	virtual ~ATUIWidgetAnimator() = default;

	// Update the animation applied to the widget. Returns updated state of
	// the animator.
	virtual AnimResult Animate(const ATUIWidgetAnimContext& ctx) = 0;

	// Start animation from the beginning. Does nothing if already started.
	void Start();

	// Pause a running animation, but keep existing animation progress. If
	// not started, the animation will start paused.
	void Pause();

	// Resume a previously paused animation from the last progress point.
	void Resume();

	// Stop animation if it is running. Does nothing if already stopped.
	void Stop();

protected:
	virtual void OnStart();
	virtual void OnPause();
	virtual void OnResume();
	virtual void OnStop();

protected:
	ATUIWidget *mpOwner = nullptr;

	void Activate();
	void Deactivate();

private:
	friend class ATUIWidget;

	ATUIWidgetAnimator *mpNextAnimator = nullptr;
	bool mbStarted = false;
	bool mbPaused = false;
	bool mbActive = false;

	// True if the animator has been ticked since start. This is needed to
	// ensure at least one tick since start.
	bool mbStartTicked = false;
};

////////////////////////////////////////////////////////////////////////////////

// Animator for animating the position offset of a widget.
class ATUIWidgetOffsetAnimator final : public ATUIWidgetAnimator {
public:
	// Set the starting and ending position offsets for the animation.
	// This also updates the widget.
	void SetEndpoints(const vdfloat2& start, const vdfloat2& end);

	// Set the rate at which the animation runs, in animations/sec.
	// A value of 2.0 causes the animation to complete in half a second.
	// This may be changed mid-animation without glitching.
	void SetRate(float rate);

	// Sets whether the animation plays forward or backward, where forward
	// is from start to end. This may be changed mid-animation without
	// glitching.
	void SetForward(bool forward);

	// Seek to a specific progress point in the animation, from 0 to 1
	// where 0 = start and 1 = end.
	void SetProgress(float v);

	AnimResult Animate(const ATUIWidgetAnimContext& ctx) override;

private:
	void OnStart() override;

	void UpdateOutput();

	float mProgress = 0;
	float mRate = 0;
	bool mbForward = false;
	vdfloat2 mOffsetBase { 0.0f, 0.0f };
	vdfloat2 mOffsetDelta { 0.0f, 0.0f };
};

////////////////////////////////////////////////////////////////////////////////

// Animator for animating the position offset of a widget relative to another
// widget, not necessarily in the same chain of ancestry.
class ATUIWidgetRelativeAnimator final : public ATUIWidgetAnimator {
public:
	// Set the starting and ending position offsets for the animation.
	// This also updates the widget.
	void SetReference(ATUIWidget& target, const vdfloat2& relOffset, const vdpoint32& absOffset);

	AnimResult Animate(const ATUIWidgetAnimContext& ctx) override;

private:
	void OnStart() override;

	void UpdateOutput();

	uint32 mTargetInstanceId = 0;
	vdpoint32 mAbsoluteOffset { 0, 0 };
	vdfloat2 mRelativeOffset { 0, 0 };
};

#endif
