//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2022 Avery Lee
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

#include "stdafx.h"
#include <vd2/Dita/services.h>
#include <at/atcore/enumparseimpl.h>
#include <at/atcore/propertyset.h>
#include <at/atnativeui/dialog.h>
#include "resource.h"
#include "uiaccessors.h"
#include "uiconfgeneric.h"

class IATUIConfigController {
public:
	virtual void BuildDialog(IATUIConfigView& view) = 0;
};

class ATUIConfDialogGenericPanel final : public VDDialogFrameW32, public IATUIConfigView {
public:
	ATUIConfDialogGenericPanel(IATUIConfigController& controller);
	~ATUIConfDialogGenericPanel();

	IATUIConfigStaticTextView& AddStaticText() override;
	IATUIConfigMultilineTextView& AddMultilineText(bool fullWidth) override;
	IATUIConfigMultilineTextView& AddMultilineLinkableText(bool fullWidth) override;
	IATUIConfigCheckboxView& AddCheckbox() override;
	IATUIConfigBoolChoiceView& AddBoolChoice() override;
	IATUIConfigPathView& AddPath() override;
	IATUIConfigStringEditView& AddStringEdit() override;
	IATUIConfigIntEditView& AddIntEdit() override;
	IATUIConfigIntSliderView& AddIntSlider() override;
	IATUIConfigColorView& AddColor() override;
	IATUIConfigBoolDropDownView& AddBoolDropDown() override;
	IATUIConfigIntDropDownView& AddIntDropDown() override;
	IATUIConfigDropDownView& AddDropDown(const ATEnumLookupTable& enumTable) override;
	IATUIConfigCustomDropDownView& AddCustomDropDown() override;

	void AddVerticalSpace() override;

	void Read(const ATPropertySet& pset);
	bool Write(ATPropertySet& pset);

	void SetCustomRead(vdfunction<void(const ATPropertySet&)> fn) override;
	void SetCustomWrite(vdfunction<void(ATPropertySet&)> fn) override;

private:
	class BaseView;
	class BoolView;
	class StringView;
	class IntView;

	class StaticTextView;
	class MultilineTextView;
	class CheckboxView;
	class BoolChoiceView;
	class PathView;
	class StringEditView;
	class IntEditView;
	class IntSliderView;
	class ColorView;
	class BoolDropDownView;
	class IntDropDownView;
	class EnumDropDownView;
	class CustomDropDownView;

	bool OnLoaded() override;
	void OnSize() override;

	void OnValueChanged();
	void OnViewHeightChanged(BaseView& view);
	void UpdateEnables();

	void AddView(BaseView *view);

	template<typename T, typename... Args>
	T& AddViewT(Args&&... args) {
		T* view = new T(std::forward<Args>(args)...);
		AddView(view);

		return *view;
	}

	void Relayout();

	static constexpr uint32 kBaseId = 30000;

	IATUIConfigController& mController;
	vdfastvector<BaseView *> mViews;

	sint32 mYPosDLUs = 0;
	sint32 mYPaddingDLUs = 0;

	vdfunction<void(const ATPropertySet&)> mpCustomRead;
	vdfunction<void(ATPropertySet&)> mpCustomWrite;
};

class ATUIConfDialogGenericPanel::BaseView : public VDDialogFrameW32, public virtual IATUIConfigPropView {
public:
	using VDDialogFrameW32::VDDialogFrameW32;

	void SetViewIndex(ATUIConfDialogGenericPanel& parent, uint32 viewIndex) {
		mpParent = &parent;
		mViewIndex = viewIndex;
	}

	void SetTagImpl(const char *tag) override {
		mTag = tag;
	}

	void SetLabelImpl(const wchar_t *label) override {
		mLabel = label;
		UpdateLabel();
	}

	void SetEnableExprImpl(vdfunction<bool()>&& fn) override {
		mpEnableExpr = std::move(fn);
	}

	void SetOnChangedImpl(vdfunction<void()>&& fn) override {
		mpOnChangedExpr = std::move(fn);
	}

	void UpdateEnable() {
		if (mpEnableExpr)
			SetEnabled(mpEnableExpr());
	}

	virtual void Read(const ATPropertySet& pset) = 0;
	virtual void Write(ATPropertySet& pset) const = 0;

	sint32 GetPaddingTopDLUs() const {
		return mPaddingTopDLUs;
	}

	void SetPaddingTopDLUs(sint32 v) {
		mPaddingTopDLUs = v;
	}

	virtual sint32 GetHeightDLUs() const {
		return GetTemplateSizeDLUs().h;
	}

	virtual void Measure(sint32 w) {}

protected:
	virtual void UpdateLabel() = 0;
	virtual void UpdateView() = 0;

	void OnValueChanged() {
		if (mpOnChangedExpr)
			mpOnChangedExpr();

		// Re-evaluate dependent controls after the callback updates values.
		mpParent->OnValueChanged();
	}

	ATUIConfDialogGenericPanel *mpParent = nullptr;
	uint32 mViewIndex = 0;
	sint32 mPaddingTopDLUs = 0;
	VDStringA mTag;
	VDStringW mLabel;
	vdfunction<bool()> mpEnableExpr;
	vdfunction<void()> mpOnChangedExpr;
};

class ATUIConfDialogGenericPanel::BoolView : public BaseView, public virtual IATUIConfigBoolView {
public:
	using BaseView::BaseView;
	using BaseView::SetTag;

	void SetDefaultImpl(bool val, bool writeDefault) override {
		mbDefault = val;
		mbWriteDefault = writeDefault;
	}

	bool GetValue() const override {
		return mbValue;
	}

	void SetValueImpl(bool v) override {
		if (mbValue != v) {
			mbValue = v;
			UpdateView();
		}
	}

	void Read(const ATPropertySet& pset) override {
		if (!mTag.empty())
			SetValue(pset.GetBool(mTag.c_str(), mbDefault));
	}

	void Write(ATPropertySet& pset) const override {
		if (!mTag.empty() &&  (mbWriteDefault || mbValue != mbDefault))
			pset.SetBool(mTag.c_str(), mbValue);
	}

protected:
	bool mbDefault = false;
	bool mbValue = false;
	bool mbWriteDefault = false;
};

class ATUIConfDialogGenericPanel::IntView : public BaseView, public virtual IATUIConfigIntView {
public:
	using BaseView::BaseView;
	using BaseView::SetTag;

	void SetDefaultImpl(sint32 val, bool writeDefault) override {
		mDefault = val;
		mbWriteDefault = writeDefault;

		if (!mbValueSet)
			SetValue(val);
	}

	sint32 GetValue() const override {
		return mValue;
	}

	void SetValueImpl(sint32 v) override {
		if (mValue != v) {
			mValue = v;
			mbValueSet = true;
			UpdateView();
		}
	}

	void Read(const ATPropertySet& pset) override {
		if (!mTag.empty())
			SetValue(pset.GetInt32(mTag.c_str(), mDefault));
	}

	void Write(ATPropertySet& pset) const override {
		if (!mTag.empty() && (mbWriteDefault || mValue != mDefault))
			pset.SetInt32(mTag.c_str(), mValue);
	}

protected:
	sint32 mDefault = 0;
	sint32 mValue = 0;
	bool mbValueSet = false;
	bool mbWriteDefault = false;
};

class ATUIConfDialogGenericPanel::StringView : public BaseView, public virtual IATUIConfigStringView {
public:
	using BaseView::BaseView;
	using BaseView::SetTag;

	const wchar_t *GetValue() const override { return mValue.c_str(); }

	void SetValueImpl(const wchar_t *s) override {
		const VDStringSpanW sp(s);

		if (mValue != sp) {
			mValue = sp;

			UpdateView();
		}
	}

	void SetDefaultImpl(const wchar_t *s, bool writeDefault) override {
		mDefaultValue = s;
		mbWriteDefault = writeDefault;
	}

	void Read(const ATPropertySet& pset) override {
		if (!mTag.empty())
			SetValue(pset.GetString(mTag.c_str(), mDefaultValue.c_str()));
	}

	void Write(ATPropertySet& pset) const override {
		if (!mTag.empty() && (mbWriteDefault || mValue != mDefaultValue))
			pset.SetString(mTag.c_str(), mValue.c_str());
	}

protected:
	VDStringW mValue;
	VDStringW mDefaultValue;
	bool mbWriteDefault = false;
};

class ATUIConfDialogGenericPanel::StaticTextView final : public StringView, public IATUIConfigStaticTextView {
public:
	StaticTextView() : StringView(IDD_CFGPROP_STATICTEXT) {
	}

private:
	VDUIProxyControl mLabelView;
	VDUIProxyControl mStaticTextView;

	bool OnLoaded() override {
		AddProxy(&mLabelView, IDC_LABEL);
		AddProxy(&mStaticTextView, IDC_STATICTEXT);
		mResizer.Add(mStaticTextView.GetWindowHandle(), mResizer.kTC);

		UpdateView();
		UpdateLabel();
		return false;
	}

	void OnEnable(bool enable) override {
		mStaticTextView.SetEnabled(enable);
	}

	void UpdateView() override {
		mStaticTextView.SetCaption(mValue.c_str());
	}

	void UpdateLabel() override {
		mLabelView.SetCaption(mLabel.c_str());
	}
};

class ATUIConfDialogGenericPanel::MultilineTextView final : public StringView, public IATUIConfigMultilineTextView {
public:
	MultilineTextView(bool linkable, bool fullWidth)
		: StringView(fullWidth ? IDD_CFGPROP_SYSLINK2 : IDD_CFGPROP_SYSLINK)
		, mbLinkable(linkable)
		, mbFullWidth(fullWidth)
	{
	}

	void SetOnLinkSelected(vdfunction<void()> fn) override {
		mSysLinkView.SetOnClickedWithUrl(
			[fn = std::move(fn)](const wchar_t *) {
				fn();
			}
		);
	}

	void SetOnLinkSelected(vdfunction<void(const wchar_t *url)> fn) override {
		mSysLinkView.SetOnClickedWithUrl(std::move(fn));
	}

private:
	VDUIProxyControl mLabelView;
	VDUIProxySysLinkControl mSysLinkView;
	const bool mbLinkable;
	const bool mbFullWidth;

	bool OnLoaded() override {
		if (!mbFullWidth)
			AddProxy(&mLabelView, IDC_LABEL);

		AddProxy(&mSysLinkView, IDC_SYSLINK);
		mResizer.Add(mSysLinkView.GetWindowHandle(), mResizer.kMC);

		UpdateView();
		UpdateLabel();
		return false;
	}

	void OnEnable(bool enable) override {
		mSysLinkView.SetEnabled(enable);
	}

	void UpdateView() override {
		// We use a Syslink control due to its sizing capability. However, it
		// has no escaping support, which we need to bypass. The workaround is
		// to insert zero width joiners after each less-than char, so it doesn't
		// detect a tag.
		if (mbLinkable) {
			mSysLinkView.SetCaption(mValue.c_str());
		} else {
			VDStringW escapedStr;
			wchar_t lastCh = (wchar_t)0;

			for(wchar_t ch : mValue) {
				if (lastCh == L'<')
					escapedStr += (wchar_t)0x200B;

				escapedStr += ch;

				lastCh = ch;
			}

			mSysLinkView.SetCaption(escapedStr.c_str());
		}

		mLastMeasuredWidth = -1;
	}

	void UpdateLabel() override {
		if (!mbFullWidth)
			mLabelView.SetCaption(mLabel.c_str());
	}

	sint32 GetHeightDLUs() const override {
		return mHeight;
	}

	void Measure(sint32 w) override {
		if (mLastMeasuredWidth != w) {
			mLastMeasuredWidth = w;
			mHeight = PixelSizeToDLUsCeil(vdsize32(0, mSysLinkView.GetIdealHeightForWidth(w))).h;
		}
	}

private:
	sint32 mHeight = 0;
	sint32 mLastMeasuredWidth = -1;
};

class ATUIConfDialogGenericPanel::CheckboxView final : public BoolView, public IATUIConfigCheckboxView {
public:
	CheckboxView() : BoolView(IDD_CFGPROP_CHECKBOX) {
		mCheckView.SetOnClicked(
			[this] {
				const bool v = mCheckView.GetChecked();
				if (mbValue != v) {
					mbValue = v;
					OnValueChanged();
				}
			}
		);
	}

	IATUIConfigCheckboxView& SetText(const wchar_t *text) override {
		if (mText != text) {
			mText = text;

			mCheckView.SetCaption(mText.c_str());
		}

		return *this;
	}

private:
	VDUIProxyControl mLabelView;
	VDUIProxyButtonControl mCheckView;
	VDStringW mText;

	bool OnLoaded() override {
		AddProxy(&mLabelView, IDC_LABEL);
		AddProxy(&mCheckView, IDC_CHECK);
		mResizer.Add(mCheckView.GetWindowHandle(), mResizer.kTC);

		mCheckView.SetCaption(mText.c_str());
		UpdateLabel();
		return false;
	}

	void OnEnable(bool enable) override {
		mCheckView.SetEnabled(enable);
	}

	void UpdateView() override {
		mCheckView.SetChecked(mbValue);
	}

	void UpdateLabel() override {
		mLabelView.SetCaption(mLabel.c_str());
	}
};

class ATUIConfDialogGenericPanel::BoolChoiceView final : public BoolView, public IATUIConfigBoolChoiceView {
public:
	BoolChoiceView() : BoolView(IDD_CFGPROP_BOOLCHOICE) {
		mFirstRadio.SetOnClicked(
			[this] {
				const bool v = mbFirstChoiceValue;

				if (mbValue != v) {
					mbValue = v;
					OnValueChanged();
				}
			}
		);

		mSecondRadio.SetOnClicked(
			[this] {
				const bool v = !mbFirstChoiceValue;

				if (mbValue != v) {
					mbValue = v;
					OnValueChanged();
				}
			}
		);
	}

	IATUIConfigBoolChoiceView& SetFirstChoiceValue(bool v) override {
		if (mbFirstChoiceValue != v) {
			mbFirstChoiceValue = v;

			UpdateView();
		}

		return *this;
	}

	IATUIConfigBoolChoiceView& SetFirstChoiceText(const wchar_t *text) override {
		if (mFirstChoiceText != text) {
			mFirstChoiceText = text;

			mFirstRadio.SetCaption(text);
		}

		return *this;
	}

	IATUIConfigBoolChoiceView& SetSecondChoiceText(const wchar_t *text) override {
		if (mSecondChoiceText != text) {
			mSecondChoiceText = text;

			mSecondRadio.SetCaption(text);
		}

		return *this;
	}

private:
	VDUIProxyControl mLabelView;
	VDUIProxyButtonControl mFirstRadio;
	VDUIProxyButtonControl mSecondRadio;
	VDStringW mFirstChoiceText;
	VDStringW mSecondChoiceText;
	bool mbFirstChoiceValue = true;

	bool OnLoaded() override {
		AddProxy(&mLabelView, IDC_LABEL);
		AddProxy(&mFirstRadio, IDC_RADIO1);
		AddProxy(&mSecondRadio, IDC_RADIO2);
		mResizer.Add(mFirstRadio.GetWindowHandle(), mResizer.kTC);
		mResizer.Add(mSecondRadio.GetWindowHandle(), mResizer.kTC);

		mFirstRadio.SetCaption(mFirstChoiceText.c_str());
		mSecondRadio.SetCaption(mSecondChoiceText.c_str());

		UpdateLabel();
		UpdateView();
		return false;
	}

	void OnEnable(bool enable) override {
		mFirstRadio.SetEnabled(enable);
		mSecondRadio.SetEnabled(enable);
	}

	void UpdateView() override {
		mFirstRadio.SetChecked(mbValue == mbFirstChoiceValue);
		mSecondRadio.SetChecked(mbValue != mbFirstChoiceValue);
	}

	void UpdateLabel() override {
		mLabelView.SetCaption(mLabel.c_str());
	}
};

class ATUIConfDialogGenericPanel::PathView final : public StringView, public IATUIConfigPathView {
public:
	PathView() : StringView(IDD_CFGPROP_PATH) {
		mPathView.SetOnTextChanged(
			[this](VDUIProxyEditControl *c) {
				VDStringW s = c->GetText();
				if (mValue != s) {
					mValue = s;

					OnValueChanged();
				}
			}
		);

		mBrowseView.SetOnClicked(
			[this] {
				OnBrowse();
			}
		);
	}

	IATUIConfigPathView& SetBrowseCaption(const wchar_t *caption) override {
		mBrowseCaption = caption;
		return *this;
	}
	
	IATUIConfigPathView& SetBrowseKey(uint32 key) override {
		mBrowseKey = key;
		return *this;
	}

	IATUIConfigPathView& SetSave() override {
		mbSave = true;
		return *this;
	}

	IATUIConfigPathView& SetTypeDirectory() override {
		mbDirectory = true;
		return *this;
	}

	IATUIConfigPathView& SetType(const wchar_t *filter, const wchar_t *ext) override {
		const wchar_t *filterEnd = filter;
		while(*filterEnd) {
			filterEnd += wcslen(filterEnd) + 1;
		}

		mBrowseFilter.assign(filter, filterEnd);
		mBrowseExt = ext ? ext : L"";
		return *this;
	}

	IATUIConfigPathView& SetTypeImage() override {
		SetBrowseKey("img "_vdfcctypeid);
		return SetType(L"Supported image files\0*.png;*.jpg;*.jpeg\0All files\0*.*\0", nullptr);
	}

private:
	VDUIProxyControl mLabelView;
	VDUIProxyEditControl mPathView;
	VDUIProxyButtonControl mBrowseView;
	bool mbSave = false;
	bool mbDirectory = false;
	VDStringW mBrowseFilter;
	VDStringW mBrowseExt;
	VDStringW mBrowseCaption;
	uint32 mBrowseKey = "path"_vdfcctypeid;

	bool OnLoaded() override {
		AddProxy(&mLabelView, IDC_LABEL);
		AddProxy(&mPathView, IDC_PATH);
		AddProxy(&mBrowseView, IDC_BROWSE);
		mResizer.Add(mPathView.GetWindowHandle(), mResizer.kTC);
		mResizer.Add(mBrowseView.GetWindowHandle(), mResizer.kTR);
		UpdateLabel();
		return false;
	}

	void OnEnable(bool enable) override {
		mPathView.SetEnabled(enable);
		mBrowseView.SetEnabled(enable);
	}

	void UpdateView() override {
		mPathView.SetText(mValue.c_str());
	}

	void UpdateLabel() override {
		mLabelView.SetCaption(mLabel.c_str());
	}

	void OnBrowse() {
		if (mbDirectory) {
			const VDStringW& fn = VDGetDirectory(mBrowseKey, (VDGUIHandle)mhdlg, mBrowseCaption.empty() ? L"Select Directory" : mBrowseCaption.c_str());

			if (!fn.empty())
				SetValueImpl(fn.c_str());
		} else {
			const wchar_t *filter = mBrowseFilter.empty() ? L"All files\0*.*\0" : mBrowseFilter.c_str();
			const wchar_t *ext = mBrowseExt.empty() ? nullptr : mBrowseExt.c_str();
			const wchar_t *caption = mBrowseCaption.empty() ? mBrowseCaption.c_str() : L"Select File";

			VDStringW fn;
			if (mbSave)
				fn = VDGetSaveFileName(mBrowseKey, (VDGUIHandle)mhdlg, caption, filter, ext);
			else
				fn = VDGetLoadFileName(mBrowseKey, (VDGUIHandle)mhdlg, caption, filter, ext);

			if (!fn.empty())
				SetValueImpl(fn.c_str());
		}
	}
};

////////////////////////////////////////////////////////////////////////////

class ATUIConfDialogGenericPanel::StringEditView final : public StringView, public IATUIConfigStringEditView {
public:
	StringEditView() : StringView(IDD_CFGPROP_EDIT) {
		mEditView.SetOnTextChanged(
			[this](VDUIProxyEditControl *c) {
				VDStringW s = c->GetText();
				if (mValue != s) {
					mValue = s;

					OnValueChanged();
				}
			}
		);
	}

private:
	VDUIProxyControl mLabelView;
	VDUIProxyEditControl mEditView;

	bool OnLoaded() override {
		AddProxy(&mLabelView, IDC_LABEL);
		AddProxy(&mEditView, IDC_EDIT);
		mResizer.Add(mEditView.GetWindowHandle(), mResizer.kTC);
		UpdateLabel();
		return false;
	}

	void OnEnable(bool enable) override {
		mEditView.SetEnabled(enable);
	}

	void UpdateView() override {
		mEditView.SetText(mValue.c_str());
	}

	void UpdateLabel() override {
		mLabelView.SetCaption(mLabel.c_str());
	}
};

////////////////////////////////////////////////////////////////////////////

class ATUIConfDialogGenericPanel::IntEditView final : public IntView, public IATUIConfigIntEditView {
public:
	IntEditView() : IntView(IDD_CFGPROP_EDIT) {
		mEditView.SetOnTextChanged(
			[this](VDUIProxyEditControl *c) {
				VDStringW s = c->GetText();
				long long v = 0;
				wchar_t ch = 0;

				if (1 != swscanf(s.c_str(), L"%llu %lc", &v, &ch) || v != (sint32)v) {
					s.sprintf(L"%u", mValue);
					c->SetText(s.c_str());
					return;
				}

				sint32 val = (sint32)v;
				if (mValue != val) {
					mValue = val;
					mbValueSet = true;

					OnValueChanged();
				}
			}
		);
	}
	
	IATUIConfigIntEditView& SetRange(sint32 minVal, sint32 maxVal) override {
		return *this;
	}

private:
	VDUIProxyControl mLabelView;
	VDUIProxyEditControl mEditView;

	bool OnLoaded() override {
		AddProxy(&mLabelView, IDC_LABEL);
		AddProxy(&mEditView, IDC_EDIT);
		mResizer.Add(mEditView.GetWindowHandle(), mResizer.kTC);
		UpdateLabel();
		UpdateView();
		return false;
	}

	void OnEnable(bool enable) override {
		mEditView.SetEnabled(enable);
	}

	void UpdateView() override {
		VDStringW s;
		s.sprintf(L"%u", mValue);
		mEditView.SetText(s.c_str());
	}

	void UpdateLabel() override {
		mLabelView.SetCaption(mLabel.c_str());
	}
};

////////////////////////////////////////////////////////////////////////////

class ATUIConfDialogGenericPanel::IntSliderView final : public IntView, public IATUIConfigIntSliderView {
public:
	IntSliderView() : IntView(IDD_CFGPROP_SLIDER) {
		mTrackBarView.SetOnValueChanged(
			[this](sint32 v, bool tracking) {
				if (mValue != v) {
					mValue = v;
					mbValueSet = true;

					UpdateValue();
					OnValueChanged();
				}
			}
		);
	}
	
	IATUIConfigIntSliderView& SetRange(sint32 minVal, sint32 maxVal) override {
		mTrackBarView.SetRange(minVal, maxVal);
		mTrackBarView.SetValue(mValue);

		return *this;
	}

	IATUIConfigIntSliderView& SetFormatter(vdfunction<void(VDStringW&, sint32)> fn) override {
		mpValueFn = std::move(fn);

		return *this;
	}

private:
	VDUIProxyControl mLabelView;
	VDUIProxyControl mValueView;
	VDUIProxyTrackbarControl mTrackBarView;
	vdfunction<void(VDStringW&, sint32)> mpValueFn;

	bool OnLoaded() override {
		AddProxy(&mLabelView, IDC_LABEL);
		AddProxy(&mTrackBarView, IDC_SLIDER);
		AddProxy(&mValueView, IDC_STATIC_VALUE);
		mResizer.Add(mTrackBarView.GetWindowHandle(), mResizer.kTC);
		mResizer.Add(mValueView.GetWindowHandle(), mResizer.kTR);
		UpdateLabel();
		UpdateView();
		UpdateValue();
		return false;
	}

	void OnEnable(bool enable) override {
		mTrackBarView.SetEnabled(enable);
		mValueView.SetEnabled(enable);
	}

	void UpdateView() override {
		mTrackBarView.SetValue(mValue);

		UpdateValue();
	}

	void UpdateLabel() override {
		mLabelView.SetCaption(mLabel.c_str());
	}

	void UpdateValue() {
		VDStringW s;

		if (mpValueFn) {
			mpValueFn(s, mValue);
		} else {
			s.sprintf(L"%d", (int)mValue);
		}

		mValueView.SetCaption(s.c_str());
	}
};

////////////////////////////////////////////////////////////////////////////

class ATUIConfDialogGenericPanel::ColorView final : public BaseView, public IATUIConfigColorView {
public:	
	using BaseView::BaseView;
	using BaseView::SetTag;

	ColorView() : BaseView(IDD_CFGPROP_COLOR) {
		mEnableView.SetOnClicked(
			[this] {
				if (mEnableView.GetChecked()) {
					if (mValue < 0)
						SetValue(0);
				} else {
					SetValue(-1);
				}
			}
		);

		mBrowseView.SetOnClicked(
			[this] {
				OnBrowse();
			}
		);
	}

	sint32 GetValue() const override { return mValue; }
	void SetValueImpl(sint32 c) override {
		if (c < 0)
			c = -1;
		else
			c &= 0xFFFFFF;

		if (mValue != c) {
			mValue = c;

			UpdateView();
		}
	}
	
	void SetFixedPaletteImpl(vdspan<const uint32> colors) override {
		mFixedPalette.assign(colors.begin(), colors.end());
	}

	void SetCustomPaletteKeyImpl(const char *key) override {
		mCustomPaletteKey = key ? key : "";
	}


	void Read(const ATPropertySet& pset) override {
		if (!mTag.empty()) {
			uint32 c = 0;

			if (pset.TryGetUint32(mTag.c_str(), c))
				SetValue(c & 0xFFFFFF);
			else
				SetValue(-1);
		}
	}

	void Write(ATPropertySet& pset) const override {
		if (!mTag.empty() && mValue >= 0)
			pset.SetUint32(mTag.c_str(), mValue);
	}

private:
	VDUIProxyControl mLabelView;
	VDUIProxyButtonControl mEnableView;
	VDUIProxyStaticControl mColorView;
	VDUIProxyButtonControl mBrowseView;
	sint32 mValue = -1;
	vdfastvector<uint32> mFixedPalette;
	VDStringA mCustomPaletteKey;

	bool OnLoaded() override {
		AddProxy(&mLabelView, IDC_LABEL);
		AddProxy(&mEnableView, IDC_ENABLE);
		AddProxy(&mColorView, IDC_STATIC_COLOR);
		AddProxy(&mBrowseView, IDC_BROWSE);
		mResizer.Add(mColorView.GetWindowHandle(), mResizer.kMC);
		mResizer.Add(mBrowseView.GetWindowHandle(), mResizer.kTR);
		UpdateLabel();
		UpdateView();
		return false;
	}

	void OnEnable(bool enable) override {
		mEnableView.SetEnabled(enable);
		mColorView.SetEnabled(enable);
		mBrowseView.SetEnabled(enable);
	}

	void UpdateView() override {
		mEnableView.SetChecked(mValue >= 0);

		if (mValue >= 0) {
			mColorView.SetBgOverrideColor(mValue);
			mColorView.SetVisible(true);
			mBrowseView.SetVisible(true);
		} else {
			mColorView.SetVisible(false);
			mBrowseView.SetVisible(false);
		}
	}

	void UpdateLabel() override {
		mLabelView.SetCaption(mLabel.c_str());
	}

	void OnBrowse() {
		sint32 v = VDUIShowColorPicker((VDGUIHandle)mhwnd, mValue, mFixedPalette, mCustomPaletteKey.c_str());

		if (v >= 0) {
			SetValue((uint32)v);
		}
	}
};

////////////////////////////////////////////////////////////////////////////

class ATUIConfDialogGenericPanel::BoolDropDownView final : public BaseView, public IATUIConfigBoolDropDownView {
public:
	using BaseView::BaseView;
	using BaseView::SetTag;

	BoolDropDownView()
		: BaseView(IDD_CFGPROP_DROPDOWN)
	{
		mComboView.SetOnSelectionChanged(
			[this](int idx) {
				if ((unsigned)idx < mChoices.size()) {
					mValue = mChoices[idx].mValue;
					OnValueChanged();
				}
			}
		);
	}

	bool GetValue() const override { return mValue; }
	void SetValueImpl(bool value) override {
		if (mValue != value) {
			mValue = value;

			UpdateView();
		}
	}

	void SetDefaultImpl(bool value, bool writeDefault) override {
		mDefaultValue = value;
		mbDefaultValueSet = true;
		mbWriteDefault = writeDefault;
	}

	IATUIConfigBoolDropDownView& AddChoice(bool value, const wchar_t *name) override {
		auto& choice = mChoices.emplace_back();
		choice.mValue = value;
		choice.mName = name;

		if (IsCreated())
			mComboView.AddItem(name);

		return *this;
	}

	void Read(const ATPropertySet& pset) override {
		if (!mTag.empty()) {
			bool v = pset.GetBool(mTag.c_str(), mbDefaultValueSet ? mDefaultValue : mChoices.empty() ? false : mChoices[0].mValue);
			mValue = !v;

			SetValue(v);
		}
	}

	void Write(ATPropertySet& pset) const override {
		if (!mTag.empty() && (!mbDefaultValueSet || mbWriteDefault || mValue != mDefaultValue))
			pset.SetBool(mTag.c_str(), mValue);
	}

private:
	VDUIProxyControl mLabelView;
	VDUIProxyComboBoxControl mComboView;
	bool mValue = false;
	bool mDefaultValue = false;
	bool mbDefaultValueSet = false;
	bool mbWriteDefault = false;

	struct Choice {
		bool mValue;
		VDStringW mName;
	};

	vdvector<Choice> mChoices;

	bool OnLoaded() override {
		AddProxy(&mLabelView, IDC_LABEL);
		AddProxy(&mComboView, IDC_COMBO);
		mResizer.Add(mComboView.GetWindowHandle(), mResizer.kTC);

		for(const Choice& choice : mChoices) {
			mComboView.AddItem(choice.mName.c_str());
		}

		UpdateLabel();
		return false;
	}

	void OnEnable(bool enable) override {
		mComboView.SetEnabled(enable);
	}

	void UpdateView() override {
		int index = -1;

		if (!mChoices.empty()) {
			index = 0;

			auto it = std::find_if(mChoices.begin(), mChoices.end(), [value = mValue](const Choice& choice) { return choice.mValue == value; });

			if (it != mChoices.end())
				index = (int)(it - mChoices.begin());
		}

		mComboView.SetSelection(index);
	}

	void UpdateLabel() override {
		mLabelView.SetCaption(mLabel.c_str());
	}
};

////////////////////////////////////////////////////////////////////////////

class ATUIConfDialogGenericPanel::IntDropDownView final : public BaseView, public IATUIConfigIntDropDownView {
public:
	using BaseView::BaseView;
	using BaseView::SetTag;

	IntDropDownView()
		: BaseView(IDD_CFGPROP_DROPDOWN)
	{
		mComboView.SetOnSelectionChanged(
			[this](int idx) {
				if ((unsigned)idx < mChoices.size()) {
					mValue = mChoices[idx].mValue;
					OnValueChanged();
				}
			}
		);
	}

	sint32 GetValue() const override { return mValue; }
	void SetValueImpl(sint32 value) override {
		if (mValue != value) {
			mValue = value;

			UpdateView();
		}
	}

	void SetDefaultImpl(sint32 value, bool writeDefault) override {
		mDefaultValue = value;
		mbDefaultValueSet = true;
		mbWriteDefault = writeDefault;
	}

	IATUIConfigIntDropDownView& AddChoice(sint32 value, const wchar_t *name) override {
		auto& choice = mChoices.emplace_back();
		choice.mValue = value;
		choice.mName = name;

		if (IsCreated())
			mComboView.AddItem(name);

		return *this;
	}

	void Read(const ATPropertySet& pset) override {
		sint32 v = mValue;

		if (!mTag.empty())
			v = pset.GetInt32(mTag.c_str(), mbDefaultValueSet ? mDefaultValue : mChoices.empty() ? 0 : mChoices[0].mValue);
			
		mValue = ~v;
		SetValue(v);
	}

	void Write(ATPropertySet& pset) const override {
		if (!mTag.empty() && (!mbDefaultValueSet || mbWriteDefault || mValue != mDefaultValue))
			pset.SetInt32(mTag.c_str(), mValue);
	}

private:
	VDUIProxyControl mLabelView;
	VDUIProxyComboBoxControl mComboView;
	sint32 mValue = 0;
	sint32 mDefaultValue = 0;
	bool mbDefaultValueSet = false;
	bool mbWriteDefault = false;

	struct Choice {
		sint32 mValue;
		VDStringW mName;
	};

	vdvector<Choice> mChoices;

	bool OnLoaded() override {
		AddProxy(&mLabelView, IDC_LABEL);
		AddProxy(&mComboView, IDC_COMBO);
		mResizer.Add(mComboView.GetWindowHandle(), mResizer.kTC);

		for(const Choice& choice : mChoices) {
			mComboView.AddItem(choice.mName.c_str());
		}

		UpdateLabel();
		return false;
	}

	void OnEnable(bool enable) override {
		mComboView.SetEnabled(enable);
	}

	void UpdateView() override {
		int index = -1;

		if (!mChoices.empty()) {
			index = 0;

			auto it = std::find_if(mChoices.begin(), mChoices.end(), [value = mValue](const Choice& choice) { return choice.mValue == value; });

			if (it != mChoices.end())
				index = (int)(it - mChoices.begin());
		}

		mComboView.SetSelection(index);
	}

	void UpdateLabel() override {
		mLabelView.SetCaption(mLabel.c_str());
	}
};

////////////////////////////////////////////////////////////////////////////

class ATUIConfDialogGenericPanel::EnumDropDownView final : public BaseView, public IATUIConfigDropDownView {
public:
	using BaseView::BaseView;
	using BaseView::SetTag;

	EnumDropDownView(const ATEnumLookupTable& enumTable)
		: BaseView(IDD_CFGPROP_DROPDOWN)
		, mEnumTable(enumTable)
		, mDefaultValue(enumTable.mDefaultValue)
	{
		mComboView.SetOnSelectionChanged(
			[this](int idx) {
				if ((unsigned)idx < mChoices.size())
					mValue = mChoices[idx].mValue;
			}
		);
	}

	uint32 GetRawValue() const override { return mValue; }
	void SetRawValue(uint32 value) override {
		if (mValue != value) {
			mValue = value;

			UpdateView();
		}
	}

	void SetRawDefault(uint32 value, bool writeDefault) override {
		mDefaultValue = value;
		mbWriteDefault = writeDefault;
	}

	IATUIConfigDropDownView& AddRawChoice(uint32 value, const wchar_t *name) override {
		auto& choice = mChoices.emplace_back();
		choice.mValue = value;
		choice.mName = name;

		if (IsCreated())
			mComboView.AddItem(name);

		return *this;
	}

	void Read(const ATPropertySet& pset) override {
		if (!mTag.empty()) {
			uint32 v = mDefaultValue;

			pset.TryGetEnum(mEnumTable, mTag.c_str(), v);
			mValue = ~v;
			SetRawValue(v);
		}
	}

	void Write(ATPropertySet& pset) const override {
		if (!mTag.empty() && (!mbDefaultValueSet || mbWriteDefault || mValue != mDefaultValue))
			pset.SetEnum(mEnumTable, mTag.c_str(), mValue);
	}

private:
	const ATEnumLookupTable& mEnumTable;
	VDUIProxyControl mLabelView;
	VDUIProxyComboBoxControl mComboView;
	uint32 mValue = 0;
	uint32 mDefaultValue = 0;
	bool mbDefaultValueSet = false;
	bool mbWriteDefault = false;

	struct Choice {
		uint32 mValue;
		VDStringW mName;
	};

	vdvector<Choice> mChoices;

	bool OnLoaded() override {
		AddProxy(&mLabelView, IDC_LABEL);
		AddProxy(&mComboView, IDC_COMBO);
		mResizer.Add(mComboView.GetWindowHandle(), mResizer.kTC);

		for(const Choice& choice : mChoices) {
			mComboView.AddItem(choice.mName.c_str());
		}

		UpdateLabel();
		return false;
	}

	void OnEnable(bool enable) override {
		mComboView.SetEnabled(enable);
	}

	void UpdateView() override {
		int index = -1;

		if (!mChoices.empty()) {
			index = 0;

			auto it = std::find_if(mChoices.begin(), mChoices.end(), [value = mValue](const Choice& choice) { return choice.mValue == value; });

			if (it != mChoices.end())
				index = (int)(it - mChoices.begin());
		}

		mComboView.SetSelection(index);
	}

	void UpdateLabel() override {
		mLabelView.SetCaption(mLabel.c_str());
	}
};

////////////////////////////////////////////////////////////////////////////

class ATUIConfDialogGenericPanel::CustomDropDownView final : public BaseView, public IATUIConfigCustomDropDownView {
public:
	using BaseView::BaseView;
	using BaseView::SetTag;

	CustomDropDownView()
		: BaseView(IDD_CFGPROP_DROPDOWN)
	{
		mComboView.SetOnSelectionChanged(
			[this](int idx) {
				if ((unsigned)idx < mChoices.size())
					mValue = idx;
			}
		);
	}

	uint32 GetValue() const override { return mValue; }
	void SetValueImpl(uint32 value) override {
		if (mValue != value) {
			mValue = value;

			UpdateView();
		}
	}

	void SetDefaultImpl(uint32 value) override {
		mDefaultValue = value;
	}

	IATUIConfigCustomDropDownView& AddChoice(
		const wchar_t *text,
		vdfunction<bool(const ATPropertySet&)> readFn,
		vdfunction<void(ATPropertySet&)> writeFn
	) override {
		auto& choice = mChoices.emplace_back();
		choice.mName = text;
		choice.mpReadFn = std::move(readFn);
		choice.mpWriteFn = std::move(writeFn);

		if (mComboView.IsValid())
			mComboView.AddItem(text);

		return *this;
	}

	void Read(const ATPropertySet& pset) override {
		uint32 v = mDefaultValue;
		uint32 n = (uint32)mChoices.size();

		for(uint32 i=0; i<n; ++i) {
			const auto& ch = mChoices[i];

			if (ch.mpReadFn && ch.mpReadFn(pset)) {
				v = i;
				break;
			}
		}

		mValue = ~v;
		SetValueImpl(v);
	}

	void Write(ATPropertySet& pset) const override {
		if (!mChoices.empty()) {
			uint32 n = mValue;

			if (n >= mChoices.size()) {
				n = mDefaultValue;

				if (n >= mChoices.size())
					n = 0;
			}

			const auto& fn = mChoices[n].mpWriteFn;
			if (fn)
				fn(pset);
		}
	}

private:
	VDUIProxyControl mLabelView;
	VDUIProxyComboBoxControl mComboView;
	uint32 mValue = 0;
	uint32 mDefaultValue = 0;

	struct Choice {
		VDStringW mName;
		vdfunction<bool(const ATPropertySet&)> mpReadFn;
		vdfunction<void(ATPropertySet&)> mpWriteFn;
	};

	vdvector<Choice> mChoices;

	bool OnLoaded() override {
		AddProxy(&mLabelView, IDC_LABEL);
		AddProxy(&mComboView, IDC_COMBO);
		mResizer.Add(mComboView.GetWindowHandle(), mResizer.kTC);

		for(const Choice& choice : mChoices) {
			mComboView.AddItem(choice.mName.c_str());
		}

		UpdateLabel();
		return false;
	}

	void OnEnable(bool enable) override {
		mComboView.SetEnabled(enable);
	}

	void UpdateView() override {
		int index = -1;

		if (mValue < mChoices.size())
			index = (int)mValue;

		mComboView.SetSelection(index);
	}

	void UpdateLabel() override {
		mLabelView.SetCaption(mLabel.c_str());
	}
};

////////////////////////////////////////////////////////////////////////////

ATUIConfDialogGenericPanel::ATUIConfDialogGenericPanel(IATUIConfigController& controller)
	: VDDialogFrameW32(IDD_CFGPROP_GENERIC)
	, mController(controller)
{
}

ATUIConfDialogGenericPanel::~ATUIConfDialogGenericPanel() {
	while(!mViews.empty()) {
		delete mViews.back();
		mViews.pop_back();
	}
}

IATUIConfigStaticTextView& ATUIConfDialogGenericPanel::AddStaticText() {
	return AddViewT<StaticTextView>();
}

IATUIConfigMultilineTextView& ATUIConfDialogGenericPanel::AddMultilineText(bool fullWidth) {
	return AddViewT<MultilineTextView>(false, fullWidth);
}

IATUIConfigMultilineTextView& ATUIConfDialogGenericPanel::AddMultilineLinkableText(bool fullWidth) {
	return AddViewT<MultilineTextView>(true, fullWidth);
}

IATUIConfigCheckboxView& ATUIConfDialogGenericPanel::AddCheckbox() {
	return AddViewT<CheckboxView>();
}

IATUIConfigBoolChoiceView& ATUIConfDialogGenericPanel::AddBoolChoice() {
	return AddViewT<BoolChoiceView>();
}

IATUIConfigPathView& ATUIConfDialogGenericPanel::AddPath() {
	return AddViewT<PathView>();
}

IATUIConfigStringEditView& ATUIConfDialogGenericPanel::AddStringEdit() {
	return AddViewT<StringEditView>();
}

IATUIConfigIntEditView& ATUIConfDialogGenericPanel::AddIntEdit() {
	return AddViewT<IntEditView>();
}

IATUIConfigIntSliderView& ATUIConfDialogGenericPanel::AddIntSlider() {
	return AddViewT<IntSliderView>();
}

IATUIConfigColorView& ATUIConfDialogGenericPanel::AddColor() {
	return AddViewT<ColorView>();
}

IATUIConfigBoolDropDownView& ATUIConfDialogGenericPanel::AddBoolDropDown() {
	return AddViewT<BoolDropDownView>();
}

IATUIConfigIntDropDownView& ATUIConfDialogGenericPanel::AddIntDropDown() {
	return AddViewT<IntDropDownView>();
}

IATUIConfigDropDownView& ATUIConfDialogGenericPanel::AddDropDown(const ATEnumLookupTable& enumTable) {
	return AddViewT<EnumDropDownView>(enumTable);
}

IATUIConfigCustomDropDownView& ATUIConfDialogGenericPanel::AddCustomDropDown() {
	return AddViewT<CustomDropDownView>();
}

void ATUIConfDialogGenericPanel::AddVerticalSpace() {
	mYPaddingDLUs += 4;
}

void ATUIConfDialogGenericPanel::Read(const ATPropertySet& pset) {
	for(BaseView *view : mViews)
		view->Read(pset);

	if (mpCustomRead)
		mpCustomRead(pset);

	UpdateEnables();
}

bool ATUIConfDialogGenericPanel::Write(ATPropertySet& pset) {
	pset.Clear();

	for(BaseView *view : mViews)
		view->Write(pset);

	if (mpCustomWrite) {
		try {
			mpCustomWrite(pset);
		} catch(const ATUIGenericConfigValidationException& ex) {
			ShowError(ex);

			auto *viewToFocus = ex.GetView();

			if (viewToFocus) {
				for(BaseView *view : mViews) {
					if (view == viewToFocus) {
						SetFocusToNextControl(view->GetWindowId());
						break;
					}
				}
			}

			return false;
		} catch(const VDException& ex) {
			ShowError(ex);
			return false;
		}
	}

	return true;
}

void ATUIConfDialogGenericPanel::SetCustomRead(vdfunction<void(const ATPropertySet&)> fn) {
	mpCustomRead = std::move(fn);
}

void ATUIConfDialogGenericPanel::SetCustomWrite(vdfunction<void(ATPropertySet&)> fn) {
	mpCustomWrite = std::move(fn);
}

bool ATUIConfDialogGenericPanel::OnLoaded() {
	mController.BuildDialog(*this);

	UpdateEnables();
	Relayout();

	vdsize32 sz = GetSize();
	sz.h = DLUsToPixelSize(vdsize32(0, mYPosDLUs)).h;
	SetSize(sz);

	return false;
}

void ATUIConfDialogGenericPanel::OnSize() {
	Relayout();

	VDDialogFrameW32::OnSize();
}

void ATUIConfDialogGenericPanel::OnValueChanged() {
	UpdateEnables();
}

void ATUIConfDialogGenericPanel::OnViewHeightChanged(BaseView& resizingView) {
	Relayout();
}

void ATUIConfDialogGenericPanel::UpdateEnables() {
	for(BaseView *view : mViews)
		view->UpdateEnable();
}

void ATUIConfDialogGenericPanel::AddView(BaseView *view) {
	vdautoptr view2(view);

	view->SetViewIndex(*this, (uint32)mViews.size());

	mViews.push_back(nullptr);
	mViews.back() = view2.release();

	if (view->Create(this)) {
		const uint16 id = (uint16)(kBaseId + (mViews.size() - 1));
		view->SetWindowId(id);

		view->SetPaddingTopDLUs(mYPaddingDLUs);
		mYPosDLUs += mYPaddingDLUs;
		mYPaddingDLUs = 0;

		sint32 htDLUs = view->GetHeightDLUs() + 1;

		mResizer.AddWithOffsets(view->GetWindowHandle(), 0, mYPosDLUs, 0, mYPosDLUs + htDLUs, mResizer.kTC, true, true);

		mYPosDLUs += htDLUs;
	}
}

void ATUIConfDialogGenericPanel::Relayout() {
	const sint32 width = GetSize().w;
	sint32 ypos = 0;

	for(BaseView *view : mViews) {
		view->Measure(width);

		ypos += view->GetPaddingTopDLUs();

		sint32 ht = view->GetHeightDLUs() + 1;

		mResizer.SetOffsets(view->GetWindowHandle(), 0, ypos, 0, ypos + ht, mResizer.kTC, true);

		ypos += ht;
	}

	mYPosDLUs = ypos;
}

////////////////////////////////////////////////////////////////////////////

class ATUIConfDialogGeneric final : public VDDialogFrameW32, private IATUIConfigController {
public:
	ATUIConfDialogGeneric(ATPropertySet& pset, const wchar_t *caption, vdfunction<void(IATUIConfigView&)> fn);

private:
	bool OnLoaded() override;
	void OnDataExchange(bool write) override;
	bool OnOK() override;

	void BuildDialog(IATUIConfigView& view) override;

	ATPropertySet& mPropSet;
	vdfunction<void(IATUIConfigView&)> mpConfigurator;
	VDStringW mCaption;

	ATUIConfDialogGenericPanel mPropPanel;
};

ATUIConfDialogGeneric::ATUIConfDialogGeneric(ATPropertySet& pset, const wchar_t *caption, vdfunction<void(IATUIConfigView&)> fn)
	: VDDialogFrameW32(IDD_DEVICE_GENERIC)
	, mPropSet(pset)
	, mpConfigurator(std::move(fn))
	, mCaption(caption)
	, mPropPanel(*this)
{
}

bool ATUIConfDialogGeneric::OnLoaded() {
	mResizer.Add(IDOK, mResizer.kBR);
	mResizer.Add(IDCANCEL, mResizer.kBR);

	mPropPanel.Create(this);

	vdrect32 r = GetControlPos(IDC_STATIC_LAYOUTRECT);

	mPropPanel.SetPosition(r.top_left());
	vdsize32 baseSize = GetSize();
	vdsize32 panelSize = mPropPanel.GetSize();
	SetSize(vdsize32(baseSize.w + panelSize.w - r.width(), baseSize.h + panelSize.h - r.height()));
	SetCurrentSizeAsMinSize();

	mResizer.Add(mPropPanel.GetHandleW32(), mResizer.kMC);

	SetCaption(mCaption.c_str());

	OnDataExchange(false);
	mPropPanel.Focus();
	return true;
}

void ATUIConfDialogGeneric::OnDataExchange(bool write) {
	if (write)
		mPropPanel.Write(mPropSet);
	else
		mPropPanel.Read(mPropSet);
}

bool ATUIConfDialogGeneric::OnOK() {
	return !mPropPanel.Write(mPropSet);
}

void ATUIConfDialogGeneric::BuildDialog(IATUIConfigView& view) {
	mpConfigurator(view);
}

////////////////////////////////////////////////////////////////////////////

bool ATUIShowDialogGenericConfig(VDGUIHandle h, ATPropertySet& pset, const wchar_t *name, vdfunction<void(IATUIConfigView&)> fn) {
	ATUIConfDialogGeneric dlg(pset, name, fn);

	return dlg.ShowDialog(h);
}
