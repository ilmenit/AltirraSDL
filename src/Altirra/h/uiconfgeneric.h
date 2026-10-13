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

#ifndef f_AT_UICONFGENERIC_H
#define f_AT_UICONFGENERIC_H

#include <vd2/system/refcount.h>
#include <vd2/system/Error.h>

class IATUIConfigPropView {
public:
	// Set the property tag for the view. When set, the view will automatically
	// read and write the corresponding property from/to the property set.
	template<typename T>
	T& SetTag(this T& self, const char *tag) {
		self.SetTagImpl(tag);
		return self;
	}

	// Set an optional label for the view, which is displayed on the left
	// column.
	//
	// As with all displayed strings, & can be prefixed for the underscore
	// Alt+key shortcut.
	template<typename T>
	T& SetLabel(this T& self, const wchar_t *label) {
		self.SetLabelImpl(label);
		return self;
	}

	// Set an optional enable expression for the view. The view is only enabled
	// when the expression evaluates to true. The enable expression is
	// automatically re-evaluated when any view changes.
	template<typename T>
	T& SetEnableExpr(this T& self, vdfunction<bool()> fn) {
		self.SetEnableExprImpl(std::move(fn));
		return self;
	}

	template<typename T>
	T& SetOnChanged(this T& self, vdfunction<void()> fn) {
		self.SetOnChangedImpl(std::move(fn));
		return self;
	}

protected:
	virtual void SetTagImpl(const char *tag) = 0;
	virtual void SetLabelImpl(const wchar_t *label) = 0;
	virtual void SetEnableExprImpl(vdfunction<bool()>&& fn) = 0;
	virtual void SetOnChangedImpl(vdfunction<void()>&& fn) = 0;
};

///////////////////////////////////////////////////////////////////////////

// Interface for a boolean-based view.
class IATUIConfigBoolView : public virtual IATUIConfigPropView {
public:
	// Set the default value for the view. The default value is:
	//	- Assumed when the value is not specified in the property set
	//	- Omitted from the property set unless writeDefault is set
	template<typename T>
	T& SetDefault(this T& self, bool val, bool writeDefault = false) {
		self.SetDefaultImpl(val, writeDefault);
		return self;
	}

	// Set the current value for the view.
	template<typename T>
	T& SetValue(this T& self, bool val) {
		self.SetValueImpl(val);
		return self;
	}

	// Get the current value from the view.
	virtual bool GetValue() const = 0;

protected:
	virtual void SetDefaultImpl(bool val, bool writeDefault) = 0;
	virtual void SetValueImpl(bool v) = 0;
};

// Interface for an integer-based view.
class IATUIConfigIntView : public virtual IATUIConfigPropView  {
public:
	template<typename T>
	T& SetDefault(this T& self, sint32 val, bool writeDefault = false) {
		self.SetDefaultImpl(val, writeDefault);
		return self;
	}

	template<typename T>
	T& SetValue(this T& self, sint32 val) {
		self.SetValueImpl(val);
		return self;
	}

	virtual sint32 GetValue() const = 0;

protected:
	virtual void SetDefaultImpl(sint32 val, bool writeDefault) = 0;
	virtual void SetValueImpl(sint32 v) = 0;
};

// Interface for a string-based view.
class IATUIConfigStringView : public virtual IATUIConfigPropView  {
public:
	template<typename T>
	T& SetValue(this T& self, const wchar_t *s) {
		self.SetValueImpl(s);
		return self;
	}

	template<typename T>
	T& SetDefault(this T& self, const wchar_t *s, bool writeDefault = false) {
		self.SetDefaultImpl(s, writeDefault);
		return self;
	}

	virtual const wchar_t *GetValue() const = 0;

protected:
	virtual void SetValueImpl(const wchar_t *s) = 0;
	virtual void SetDefaultImpl(const wchar_t *s, bool writeDefault) = 0;
};

// Interface for a view representing a 24-bit sRGB color.
class IATUIConfigColorView : public virtual IATUIConfigPropView  {
public:
	template<typename T>
	T& SetValue(this T& self, sint32 color) {
		self.SetValueImpl(color);
		return self;
	}

	template<typename T>
	T& SetFixedPalette(this T& self, vdspan<const uint32> colors) {
		self.SetFixedPaletteImpl(colors);
		return self;
	}

	template<typename T>
	T& SetCustomPaletteKey(this T& self, const char *key) {
		self.SetCustomPaletteKeyImpl(key);
		return self;
	}

	virtual sint32 GetValue() const = 0;

protected:
	virtual void SetValueImpl(sint32 color) = 0;
	virtual void SetFixedPaletteImpl(vdspan<const uint32> colors) = 0;
	virtual void SetCustomPaletteKeyImpl(const char *key) = 0;
};

///////////////////////////////////////////////////////////////////////////

// Interface for a static label view representing a read-only string value.
class IATUIConfigStaticTextView : public virtual IATUIConfigStringView {
};

// Interface for a multi-line static text view, which may or may not contain
// links.
class IATUIConfigMultilineTextView : public virtual IATUIConfigStringView {
public:
	virtual void SetOnLinkSelected(vdfunction<void()> fn) = 0;
	virtual void SetOnLinkSelected(vdfunction<void(const wchar_t *url)> fn) = 0;
};

// Interface for a checkbox representing a boolean value.
class IATUIConfigCheckboxView : public virtual IATUIConfigBoolView {
public:
	// Set the display text displayed next to the checkbox.
	virtual IATUIConfigCheckboxView& SetText(const wchar_t *text) = 0;
};

// Interface for a pair of radio buttons representing a boolean value.
class IATUIConfigBoolChoiceView : public virtual IATUIConfigBoolView {
public:
	// Set whether the first (top) choice is for the true or false value.
	virtual IATUIConfigBoolChoiceView& SetFirstChoiceValue(bool value) = 0;

	// Set the display label for the first choice.
	virtual IATUIConfigBoolChoiceView& SetFirstChoiceText(const wchar_t *text) = 0;

	// Set the display label for the second choice.
	virtual IATUIConfigBoolChoiceView& SetSecondChoiceText(const wchar_t *text) = 0;
};

// Interface for a slider representing an integer value.
class IATUIConfigSliderView : public virtual IATUIConfigIntView {
public:
	virtual IATUIConfigSliderView& SetRange(sint32 minVal, sint32 maxVal) = 0;
	virtual IATUIConfigSliderView& SetPage(sint32 pageSize) = 0;
};

// Interface for a browsable file/directory path view.
class IATUIConfigPathView : public virtual IATUIConfigStringView {
public:
	// Set the caption shown on the file/directory browser.
	virtual IATUIConfigPathView& SetBrowseCaption(const wchar_t *caption) = 0;

	// Set the key used to persist the state of the file/directory dialog.
	virtual IATUIConfigPathView& SetBrowseKey(uint32 key) = 0;

	// Set the view to show a save file browser instead of open, not requiring
	// the file to exist.
	virtual IATUIConfigPathView& SetSave() = 0;

	// Set the file type filter and default filename extension.
	virtual IATUIConfigPathView& SetType(const wchar_t *filter, const wchar_t *ext) = 0;

	// Set the file type filter for all images supported by the default image
	// input path.
	virtual IATUIConfigPathView& SetTypeImage() = 0;

	// Set the file type filter for directories only.
	virtual IATUIConfigPathView& SetTypeDirectory() = 0;
};

// Interface for a single line text edit view on a string value.
class IATUIConfigStringEditView : public virtual IATUIConfigStringView {
public:
};

// Interface for a single line text edit view on an integer value.
class IATUIConfigIntEditView : public virtual IATUIConfigIntView {
public:
	// Set the range of allowable values, inclusive.
	virtual IATUIConfigIntEditView& SetRange(sint32 minVal, sint32 maxVal) = 0;
};

// Interface for a slider view on an integer value, with a slider and a value
// label next to it.
class IATUIConfigIntSliderView : public virtual IATUIConfigIntView {
public:
	// Set the range of allowable values, inclusive.
	virtual IATUIConfigIntSliderView& SetRange(sint32 minVal, sint32 maxVal) = 0;

	// Set an optional formatter to convert the raw slider value to a string.
	virtual IATUIConfigIntSliderView& SetFormatter(vdfunction<void(VDStringW&, sint32)> fn) = 0;
};

// Interface for a drop-down corresponding to a boolean value.
class IATUIConfigBoolDropDownView : public virtual IATUIConfigPropView {
public:
	virtual IATUIConfigBoolDropDownView& AddChoice(bool value, const wchar_t *text) = 0;

	template<typename T>
	T& SetValue(this T& self, bool value) {
		self.SetValueImpl(value);
		return self;
	}

	template<typename T>
	T& SetDefault(this T& self, bool value, bool writeDefault = false) {
		self.SetDefaultImpl(value);
		return self;
	}

protected:
	virtual bool GetValue() const = 0;
	virtual void SetValueImpl(bool value) = 0;
	virtual void SetDefaultImpl(bool value, bool writeDefault) = 0;
};

// Interface for a drop-down corresponding to an integer value.
class IATUIConfigIntDropDownView : public virtual IATUIConfigPropView {
public:
	virtual IATUIConfigIntDropDownView& AddChoice(sint32 value, const wchar_t *text) = 0;

	template<typename T>
	T& SetValue(this T& self, sint32 value) {
		self.SetValueImpl(value);
		return self;
	}

	template<typename T>
	T& SetDefault(this T& self, sint32 value, bool writeDefault = false) {
		self.SetDefaultImpl(value, writeDefault);
		return self;
	}

	virtual sint32 GetValue() const = 0;

protected:
	virtual void SetValueImpl(sint32 value) = 0;
	virtual void SetDefaultImpl(sint32 value, bool writeDefault) = 0;
};

// Interface for a drop-down corresponding to an enumerated value.
class IATUIConfigDropDownView : public virtual IATUIConfigPropView {
public:
	template<typename T> requires std::is_enum_v<T>
	IATUIConfigDropDownView& AddChoice(T value, const wchar_t *text) {
		return AddRawChoice((uint32)value, text);
	}
	
	template<typename T> requires std::is_enum_v<T>
	void SetDefault(T value, bool writeDefault = false) {
		SetRawDefault((uint32)value, writeDefault);
	}

	template<typename T> requires std::is_enum_v<T>
	T GetValue() {
		return (T)GetRawValue();
	}

	template<typename T> requires std::is_enum_v<T>
	void SetValue(T value) {
		SetRawValue((uint32)value);
	}

protected:
	virtual IATUIConfigDropDownView& AddRawChoice(uint32 value, const wchar_t *text) = 0;
	virtual uint32 GetRawValue() const = 0;
	virtual void SetRawValue(uint32 value) = 0;
	virtual void SetRawDefault(uint32 value, bool writeDefault) = 0;
};

// Interface for a drop-down with an arbitrary value mapping using custom
// translations.
class IATUIConfigCustomDropDownView : public virtual IATUIConfigPropView {
public:
	// Add a custom choice. Reads use this choice if the read function returns
	// true; the write pushes equivalent state to the property set.
	virtual IATUIConfigCustomDropDownView& AddChoice(
		const wchar_t *text,
		vdfunction<bool(const ATPropertySet&)> readFn,
		vdfunction<void(ATPropertySet&)> writeFn
	) = 0;

	virtual uint32 GetValue() const = 0;

	template<typename T>
	T& SetValue(this T& self, uint32 value) {
		self.SetValueImpl(value);
		return self;
	}

	template<typename T>
	T& SetDefault(this T& self, uint32 value) {
		self.SetDefaultImpl(value);
		return self;
	}

protected:
	virtual void SetValueImpl(uint32 value) = 0;
	virtual void SetDefaultImpl(uint32 value) = 0;
};

///////////////////////////////////////////////////////////////////////////

// Primary interface to configure a generic device configuration view.
//
// The configuration view is built from the top down. All returned views
// are guaranteed to persist for the lifetime of the dialog and can be
// referenced freely until the modal dialog call returns.
//
class IATUIConfigView {
public:
	virtual IATUIConfigStaticTextView& AddStaticText() = 0;
	virtual IATUIConfigMultilineTextView& AddMultilineText(bool fullWidth) = 0;
	virtual IATUIConfigMultilineTextView& AddMultilineLinkableText(bool fullWidth) = 0;
	virtual IATUIConfigCheckboxView& AddCheckbox() = 0;
	virtual IATUIConfigBoolChoiceView& AddBoolChoice() = 0;
	virtual IATUIConfigPathView& AddPath() = 0;
	virtual IATUIConfigStringEditView& AddStringEdit() = 0;
	virtual IATUIConfigIntEditView& AddIntEdit() = 0;
	virtual IATUIConfigIntSliderView& AddIntSlider() = 0;

	virtual IATUIConfigColorView& AddColor() = 0;
	virtual IATUIConfigBoolDropDownView& AddBoolDropDown() = 0;
	virtual IATUIConfigIntDropDownView& AddIntDropDown() = 0;

	virtual IATUIConfigDropDownView& AddDropDown(const ATEnumLookupTable& enumTable) = 0;

	template<typename T>
	IATUIConfigDropDownView& AddDropDown() {
		return AddDropDown(ATGetEnumLookupTable<T>());
	}

	virtual IATUIConfigCustomDropDownView& AddCustomDropDown() = 0;

	// Add a bit of empty vertical separation space.
	virtual void AddVerticalSpace() = 0;

	// Set a custom read function to decode a property set into the views. This
	// is called in addition to the standard decoding.
	virtual void SetCustomRead(vdfunction<void(const ATPropertySet&)> fn) = 0;

	// Set a custom write function to encode the views into a property set. This
	// is called in addition to the standard encoding. The function can throw
	// a validation exception if an issue is detected,
	virtual void SetCustomWrite(vdfunction<void(ATPropertySet&)> fn) = 0;
};

///////////////////////////////////////////////////////////////////////////

class ATUIGenericConfigValidationException final : public VDException {
public:
	template<typename... Args>
	ATUIGenericConfigValidationException(IATUIConfigPropView *optView, const wchar_t *format, Args&& ...args)
		: mpView(optView)
	{
		constexpr bool validParams = (VDPrintfCompatible<Args> && ...);

		if constexpr(validParams) {
			if constexpr(sizeof...(args) > 0)
				wsetf(format, std::forward<Args>(args)...);
			else
				assign(format);
		} else
			static_assert(!validParams, "Unsupported parameter types passed");
	}

	IATUIConfigPropView *GetView() const {
		return mpView;
	}

private:
	IATUIConfigPropView *mpView = nullptr;
};

///////////////////////////////////////////////////////////////////////////

// Show a generic device configuration dialog with the given parent, property set,
// window caption, and initializer.
bool ATUIShowDialogGenericConfig(VDGUIHandle h, ATPropertySet& pset, const wchar_t *name, vdfunction<void(IATUIConfigView&)> fn);

#endif
