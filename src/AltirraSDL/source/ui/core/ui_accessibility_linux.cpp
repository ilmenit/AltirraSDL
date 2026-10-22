// Native AT-SPI publication. GIO is loaded from the desktop at runtime so a
// headless install does not acquire a GTK/GLib build or runtime requirement.
// Protocol: https://gitlab.gnome.org/GNOME/at-spi2-core/-/tree/main/xml
#include <stdafx.h>
#include "ui_accessibility.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cwctype>
#include <mutex>
#include <thread>
#include <cstring>
#include <vd2/system/text.h>

namespace {
// Public opaque GIO C ABI. No GObject, toolkit, or emulator object is shared
// with the worker. All bus registration/callbacks belong to its private context.
struct GVariant;
struct GVariantType;
struct GDBusConnection;
struct GDBusMethodInvocation;
struct GDBusNodeInfo;
struct GDBusInterfaceInfo;
struct GMainContext;
struct GAsyncResult;
struct GError;
using MethodCall = void (*)(GDBusConnection *, const char *, const char *,
	const char *, const char *, GVariant *, GDBusMethodInvocation *, void *);
using GetProperty = GVariant *(*)(GDBusConnection *, const char *, const char *,
	const char *, const char *, GError **, void *);
using SetProperty = int (*)(GDBusConnection *, const char *, const char *,
	const char *, const char *, GVariant *, GError **, void *);
struct InterfaceVTable {
	MethodCall method;
	GetProperty get;
	SetProperty set;
	void *padding[8];
};
using AsyncCallback = void (*)(void *, GAsyncResult *, void *);
using SignalCallback = void (*)(GDBusConnection *, const char *, const char *,
	const char *, const char *, GVariant *, void *);

struct Gio {
	SDL_SharedObject *library = nullptr;
	GMainContext *(*contextNew)();
	void (*contextUnref)(GMainContext *);
	void (*contextPush)(GMainContext *);
	void (*contextPop)(GMainContext *);
	int (*contextIteration)(GMainContext *, int);
	GDBusConnection *(*busGet)(int, void *, GError **);
	GDBusConnection *(*connectionNew)(const char *, int, void *, void *, GError **);
	int (*connectionClosed)(GDBusConnection *);
	int (*connectionClose)(GDBusConnection *, void *, GError **);
	const char *(*connectionName)(GDBusConnection *);
	unsigned (*registerObject)(GDBusConnection *, const char *, GDBusInterfaceInfo *,
		const InterfaceVTable *, void *, void (*)(void *), GError **);
	int (*unregisterObject)(GDBusConnection *, unsigned);
	unsigned (*subscribe)(GDBusConnection *, const char *, const char *, const char *,
		const char *, const char *, int, SignalCallback, void *, void (*)(void *));
	void (*unsubscribe)(GDBusConnection *, unsigned);
	GVariant *(*callSync)(GDBusConnection *, const char *, const char *, const char *,
		const char *, GVariant *, const GVariantType *, int, int, void *, GError **);
	void (*call)(GDBusConnection *, const char *, const char *, const char *,
		const char *, GVariant *, const GVariantType *, int, int, void *, AsyncCallback, void *);
	GVariant *(*callFinish)(GDBusConnection *, GAsyncResult *, GError **);
	int (*emit)(GDBusConnection *, const char *, const char *, const char *,
		const char *, GVariant *, GError **);
	void (*returnValue)(GDBusMethodInvocation *, GVariant *);
	void (*returnError)(GDBusMethodInvocation *, const char *, const char *);
	GDBusNodeInfo *(*nodeNew)(const char *, GError **);
	void (*nodeUnref)(GDBusNodeInfo *);
	GDBusInterfaceInfo *(*lookupInterface)(GDBusNodeInfo *, const char *);
	GVariant *(*variantNew)(const char *, ...);
	void (*variantGet)(GVariant *, const char *, ...);
	GVariant *(*variantArray)(const GVariantType *, GVariant *const *, size_t);
	GVariant *(*variantTuple)(GVariant *const *, size_t);
	GVariant *(*variantBox)(GVariant *);
	GVariant *(*variantString)(const char *);
	GVariant *(*variantInt)(int);
	GVariant *(*variantUInt)(unsigned);
	GVariant *(*variantBool)(int);
	GVariant *(*variantChild)(GVariant *, size_t);
	int (*variantGetInt)(GVariant *);
	unsigned (*variantGetUInt)(GVariant *);
	const char *(*variantGetString)(GVariant *, size_t *);
	void (*variantUnref)(GVariant *);
	void (*objectUnref)(void *);
	void (*errorFree)(GError *);

	bool Load() {
		if (library)
			return true;
		library = SDL_LoadObject("libgio-2.0.so.0");
		if (!library)
			return false;
#define LOAD(member, name) \
	member = reinterpret_cast<decltype(member)>(SDL_LoadFunction(library, name)); \
	if (!member) { SDL_UnloadObject(library); library = nullptr; return false; }
		LOAD(contextNew, "g_main_context_new");
		LOAD(contextUnref, "g_main_context_unref");
		LOAD(contextPush, "g_main_context_push_thread_default");
		LOAD(contextPop, "g_main_context_pop_thread_default");
		LOAD(contextIteration, "g_main_context_iteration");
		LOAD(busGet, "g_bus_get_sync");
		LOAD(connectionNew, "g_dbus_connection_new_for_address_sync");
		LOAD(connectionClosed, "g_dbus_connection_is_closed");
		LOAD(connectionClose, "g_dbus_connection_close_sync");
		LOAD(connectionName, "g_dbus_connection_get_unique_name");
		LOAD(registerObject, "g_dbus_connection_register_object");
		LOAD(unregisterObject, "g_dbus_connection_unregister_object");
		LOAD(subscribe, "g_dbus_connection_signal_subscribe");
		LOAD(unsubscribe, "g_dbus_connection_signal_unsubscribe");
		LOAD(callSync, "g_dbus_connection_call_sync");
		LOAD(call, "g_dbus_connection_call");
		LOAD(callFinish, "g_dbus_connection_call_finish");
		LOAD(emit, "g_dbus_connection_emit_signal");
		LOAD(returnValue, "g_dbus_method_invocation_return_value");
		LOAD(returnError, "g_dbus_method_invocation_return_dbus_error");
		LOAD(nodeNew, "g_dbus_node_info_new_for_xml");
		LOAD(nodeUnref, "g_dbus_node_info_unref");
		LOAD(lookupInterface, "g_dbus_node_info_lookup_interface");
		LOAD(variantNew, "g_variant_new");
		LOAD(variantGet, "g_variant_get");
		LOAD(variantArray, "g_variant_new_array");
		LOAD(variantTuple, "g_variant_new_tuple");
		LOAD(variantBox, "g_variant_new_variant");
		LOAD(variantString, "g_variant_new_string");
		LOAD(variantInt, "g_variant_new_int32");
		LOAD(variantUInt, "g_variant_new_uint32");
		LOAD(variantBool, "g_variant_new_boolean");
		LOAD(variantChild, "g_variant_get_child_value");
		LOAD(variantGetInt, "g_variant_get_int32");
		LOAD(variantGetUInt, "g_variant_get_uint32");
		LOAD(variantGetString, "g_variant_get_string");
		LOAD(variantUnref, "g_variant_unref");
		LOAD(objectUnref, "g_object_unref");
		LOAD(errorFree, "g_error_free");
#undef LOAD
		// GIO starts process-global service threads; its dynamic library must
		// remain loaded until process exit, including after support is disabled.
		return true;
	}
};

constexpr const char *kRoot = "/org/a11y/atspi/accessible/root";
constexpr const char *kFrame = "/org/a11y/atspi/accessible/window";
constexpr const char *kScreen = "/org/a11y/atspi/accessible/screen";
constexpr const char *kCache = "/org/a11y/atspi/cache";
constexpr const char *kNull = "/org/a11y/atspi/null";
constexpr const char *kAccessible = "org.a11y.atspi.Accessible";
constexpr const char *kApplication = "org.a11y.atspi.Application";
constexpr const char *kText = "org.a11y.atspi.Text";
constexpr const char *kComponent = "org.a11y.atspi.Component";
constexpr const char *kXml =
#include "ui_accessibility_atspi_xml.inl"
;

// Values from at-spi2-core/atspi/atspi-constants.h, not toolkit-specific enums.
constexpr unsigned kRoleApplication = 75, kRoleFrame = 23, kRoleDocumentText = 94;
constexpr unsigned kStateEnabled = 8, kStateFocusable = 11, kStateFocused = 12;
constexpr unsigned kStateMultiline = 17, kStateSensitive = 24;
constexpr unsigned kStateShowing = 25, kStateVisible = 30, kStateReadOnly = 43;

const GVariantType *Type(const char *type) {
	return reinterpret_cast<const GVariantType *>(type);
}

class Publisher {
public:
	void Update(std::shared_ptr<const ATSDLAccessibilitySnapshot> snapshot) {
		std::atomic_store(&mPublished, std::move(snapshot));
		if (!mWorker.joinable()) {
			mStop = false;
			mWorker = std::thread([this] { Run(); });
		}
		mWake.notify_one();
	}
	void Stop() {
		mStop = true;
		mWake.notify_one();
		if (mWorker.joinable())
			mWorker.join();
		std::atomic_store(&mPublished, std::shared_ptr<const ATSDLAccessibilitySnapshot>());
	}
	~Publisher() { Stop(); }

private:
	Gio g;
	std::thread mWorker;
	std::atomic<bool> mStop {false};
	std::mutex mWaitMutex;
	std::condition_variable mWake;
	std::shared_ptr<const ATSDLAccessibilitySnapshot> mPublished, mCurrent;
	GDBusConnection *mConnection = nullptr;
	GDBusNodeInfo *mNode = nullptr;
	GMainContext *mContext = nullptr;
	std::vector<unsigned> mObjects;
	unsigned mOwnerSubscription = 0;
	std::string mName, mDesktopName, mDesktopPath;
	int mId = 0, mCaret = 0, mSelectionStart = 0, mSelectionEnd = 0;
	bool mEmbedded = false, mEmbedding = false;
	std::chrono::steady_clock::time_point mRetry;

	GVariant *Tuple(std::initializer_list<GVariant *> items) {
		return g.variantTuple(items.begin(), items.size());
	}
	GVariant *Empty(const char *type) { return g.variantArray(Type(type), nullptr, 0); }
	GVariant *Ref(const char *path, const char *name = nullptr) {
		return g.variantNew("(so)", name ? name : mName.c_str(), path);
	}
	GVariant *Interfaces(const char *path) {
		std::vector<GVariant *> values {g.variantString(kAccessible)};
		if (!strcmp(path, kRoot))
			values.push_back(g.variantString(kApplication));
		else {
			values.push_back(g.variantString(kComponent));
			if (!strcmp(path, kScreen))
				values.push_back(g.variantString(kText));
		}
		return g.variantArray(Type("s"), values.data(), values.size());
	}
	GVariant *States(const char *path) {
		uint64_t bits = (1ull << kStateEnabled) | (1ull << kStateSensitive)
			| (1ull << kStateShowing) | (1ull << kStateVisible);
		if (!strcmp(path, kScreen)) {
			bits |= (1ull << kStateFocusable) | (1ull << kStateMultiline)
				| (1ull << kStateReadOnly);
			if (mCurrent->focused)
				bits |= 1ull << kStateFocused;
		}
		if (!strcmp(path, kFrame) && mCurrent->focused)
			bits |= 1ull << 1; // ATSPI_STATE_ACTIVE
		GVariant *parts[] {g.variantUInt((unsigned)bits), g.variantUInt((unsigned)(bits >> 32))};
		return g.variantArray(Type("u"), parts, 2);
	}
	GVariant *Parent(const char *path) {
		if (!strcmp(path, kRoot))
			return Ref(mDesktopPath.empty() ? kNull : mDesktopPath.c_str(), mDesktopName.c_str());
		return Ref(!strcmp(path, kFrame) ? kRoot : kFrame);
	}
	const char *Name(const char *path) {
		return !strcmp(path, kRoot) ? "AltirraSDL" : !strcmp(path, kFrame) ? "Altirra" : "Screen";
	}
	unsigned Role(const char *path) {
		return !strcmp(path, kRoot) ? kRoleApplication : !strcmp(path, kFrame) ? kRoleFrame : kRoleDocumentText;
	}
	const char *RoleName(const char *path) {
		return !strcmp(path, kRoot) ? "application" : !strcmp(path, kFrame) ? "frame" : "document text";
	}
	int Count() const { return (int)mCurrent->text.size(); }
	int ArgInt(GVariant *args, size_t index) {
		auto *v = g.variantChild(args, index);
		int value = g.variantGetInt(v);
		g.variantUnref(v);
		return value;
	}
	unsigned ArgUInt(GVariant *args, size_t index) {
		auto *v = g.variantChild(args, index);
		unsigned value = g.variantGetUInt(v);
		g.variantUnref(v);
		return value;
	}
	GVariant *TextString(int start, int end) {
		start = std::clamp(start, 0, Count());
		end = end < 0 ? Count() : std::clamp(end, start, Count());
		const std::wstring text = mCurrent->text.substr(start, end - start);
		const auto utf8 = VDTextWToU8(text.c_str(), (int)text.size());
		return g.variantString(utf8.c_str());
	}
	void Reply(GDBusMethodInvocation *invocation, GVariant *value) {
		g.returnValue(invocation, Tuple({value}));
	}
	void Event(const char *path, const char *interface, const char *name,
		const char *detail, int first, int second, GVariant *value) {
		g.emit(mConnection, nullptr, path, interface, name,
			Tuple({g.variantString(detail), g.variantInt(first), g.variantInt(second),
				g.variantBox(value), Empty("{sv}")}), nullptr);
	}
	void ObjectEvent(const char *name, const char *detail, int first, int second, GVariant *value) {
		Event(kScreen, "org.a11y.atspi.Event.Object", name, detail, first, second, value);
	}
	void SendChanges(const std::shared_ptr<const ATSDLAccessibilitySnapshot>& next) {
		const auto old = mCurrent;
		mCurrent = next;
		mCaret = std::min(mCaret, Count());
		mSelectionStart = std::min(mSelectionStart, Count());
		mSelectionEnd = std::min(mSelectionEnd, Count());
		if (!mEmbedded)
			return;
		if (!old || old->text != next->text) {
			if (old && !old->text.empty()) {
				const auto utf8 = VDTextWToU8(old->text.c_str(), (int)old->text.size());
				ObjectEvent("TextChanged", "delete", 0, (int)old->text.size(), g.variantString(utf8.c_str()));
			}
			if (!next->text.empty())
				ObjectEvent("TextChanged", "insert", 0, Count(), TextString(0, -1));
		}
		if (!old || old->focused != next->focused) {
			Event(kFrame, "org.a11y.atspi.Event.Window", next->focused ? "Activate" : "Deactivate",
				"", 0, 0, g.variantString("Altirra"));
			ObjectEvent("StateChanged", "focused", next->focused, 0, g.variantInt(0));
		}
	}
	std::pair<int, int> TextRange(int offset, unsigned unit, bool legacy = false) const {
		const auto& text = mCurrent->text;
		offset = std::clamp(offset, 0, Count());
		if (offset == Count())
			return {offset, offset};
		if (unit == 0)
			return {offset, offset + 1};
		if (legacy) {
			// Legacy boundaries describe intervals between consecutive starts
			// or consecutive ends. They intentionally include intervening
			// whitespace; WORD_END is not the same interval as WORD_START.
			std::vector<int> boundaries {0};
			const auto space = [&](int i) { return std::iswspace(text[i]) != 0; };
			const auto terminal = [&](int i) {
				return text[i] == L'.' || text[i] == L'!' || text[i] == L'?' || text[i] == L'\n';
			};
			for (int i = 1; i < Count(); ++i) {
				bool boundary = false;
				if (unit == 1) boundary = !space(i) && space(i - 1);
				else if (unit == 2) boundary = space(i) && !space(i - 1);
				else if (unit == 3) {
					if (!space(i)) {
						int before = i - 1;
						while (before >= 0 && space(before) && text[before] != L'\n') --before;
						boundary = before >= 0 && terminal(before);
					}
				} else if (unit == 4) boundary = terminal(i - 1);
				else if (unit == 5) boundary = text[i - 1] == L'\n';
				else if (unit == 6) boundary = text[i] == L'\n';
				if (boundary) boundaries.push_back(i);
			}
			boundaries.push_back(Count());
			auto next = std::upper_bound(boundaries.begin(), boundaries.end(), offset);
			return {*(next - 1), *next};
		}
		// AT-SPI granularity WORD=1, SENTENCE=2, LINE=3, PARAGRAPH=4.
		// Legacy boundary WORD_START/END=1/2, SENTENCE_START/END=3/4,
		// LINE_START/END=5/6. Preserve newline as the line terminator.
		const bool word = legacy ? unit <= 2 : unit == 1;
		const bool sentence = legacy ? unit <= 4 : unit == 2;
		int first = offset, last = offset;
		if (word) {
			const bool space = std::iswspace(text[offset]) != 0;
			while (first > 0 && (std::iswspace(text[first - 1]) != 0) == space)
				--first;
			while (last < Count() && (std::iswspace(text[last]) != 0) == space)
				++last;
		} else if (sentence) {
			const auto terminal = [](wchar_t c) { return c == L'.' || c == L'!' || c == L'?' || c == L'\n'; };
			while (first > 0 && !terminal(text[first - 1]))
				--first;
			while (last < Count() && !terminal(text[last]))
				++last;
			if (last < Count()) ++last;
		} else {
			while (first > 0 && text[first - 1] != L'\n') --first;
			while (last < Count() && text[last] != L'\n') ++last;
			if (last < Count()) ++last;
		}
		return {first, last};
	}
	struct Rect { int x, y, w, h; };
	Rect Bounds(int start, int end, unsigned coord) const {
		if (start < 0 || end < start || end > Count())
			return {0, 0, 0, 0};
		float left = 0, top = 0, right = 0, bottom = 0;
		bool found = false;
		for (const auto& line : mCurrent->lines) {
			const bool caret = start == end;
			if (caret ? (start < line.offset || start > line.offset + line.length)
				: (start >= line.offset + line.length || end <= line.offset))
				continue;
			const float cell = line.length ? line.width / line.length : 0;
			const float x1 = line.x + std::clamp(start - line.offset, 0, line.length) * cell;
			const float x2 = line.x + std::clamp(end - line.offset, 0, line.length) * cell;
			if (!found) {
				left = x1; right = x2; top = line.y; bottom = line.y + line.height;
				found = true;
			} else {
				left = std::min(left, x1); right = std::max(right, x2);
				top = std::min(top, line.y); bottom = std::max(bottom, line.y + line.height);
			}
			if (caret)
				break;
		}
		// ATSPI_COORD_TYPE_SCREEN=0, WINDOW=1, PARENT=2. The screen
		// document's parent is the frame, whose origin is the window origin.
		const int dx = coord == 0 ? mCurrent->windowX : 0;
		const int dy = coord == 0 ? mCurrent->windowY : 0;
		return {(int)left + dx, (int)top + dy, (int)(right - left), (int)(bottom - top)};
	}
	int OffsetAt(int x, int y, unsigned coord) const {
		double px = x, py = y;
		if (coord == 0) { px -= mCurrent->windowX; py -= mCurrent->windowY; }
		for (const auto& line : mCurrent->lines) {
			if (py < line.y || py >= line.y + line.height)
				continue;
			if (!line.length || line.width <= 0)
				return line.offset;
			const double column = (px - line.x) * line.length / line.width;
			return line.offset + (int)std::clamp(column, 0.0, (double)line.length);
		}
		return -1;
	}
	void AccessibleMethod(const char *path, const char *method, GVariant *args, GDBusMethodInvocation *inv) {
		const bool root = !strcmp(path, kRoot), screen = !strcmp(path, kScreen);
		if (!strcmp(method, "GetChildAtIndex")) {
			Reply(inv, !screen && ArgInt(args, 0) == 0 ? Ref(root ? kFrame : kScreen) : Ref(kNull, ""));
		} else if (!strcmp(method, "GetChildren")) {
			GVariant *child = !screen ? Ref(root ? kFrame : kScreen) : nullptr;
			Reply(inv, g.variantArray(Type("(so)"), &child, child ? 1 : 0));
		} else if (!strcmp(method, "GetIndexInParent")) Reply(inv, g.variantInt(root ? -1 : 0));
		else if (!strcmp(method, "GetRelationSet")) Reply(inv, Empty("(ua(so))"));
		else if (!strcmp(method, "GetRole")) Reply(inv, g.variantUInt(Role(path)));
		else if (!strcmp(method, "GetRoleName") || !strcmp(method, "GetLocalizedRoleName")) Reply(inv, g.variantString(RoleName(path)));
		else if (!strcmp(method, "GetState")) Reply(inv, States(path));
		else if (!strcmp(method, "GetAttributes")) {
			GVariant *attribute = g.variantNew("{ss}", "toolkit", "AltirraSDL");
			Reply(inv, g.variantArray(Type("{ss}"), &attribute, 1));
		} else if (!strcmp(method, "GetApplication")) Reply(inv, Ref(kRoot));
		else if (!strcmp(method, "GetInterfaces")) Reply(inv, Interfaces(path));
	}
	void TextMethod(const char *method, GVariant *args, GDBusMethodInvocation *inv) {
		if (!strcmp(method, "GetText")) {
			Reply(inv, TextString(ArgInt(args, 0), ArgInt(args, 1)));
		} else if (!strcmp(method, "GetStringAtOffset") || !strcmp(method, "GetTextAtOffset")
			|| !strcmp(method, "GetTextBeforeOffset") || !strcmp(method, "GetTextAfterOffset")) {
			const int offset = ArgInt(args, 0);
			const unsigned unit = ArgUInt(args, 1);
			auto range = TextRange(offset, unit, strcmp(method, "GetStringAtOffset") != 0);
			if (!strcmp(method, "GetTextBeforeOffset"))
				range = range.first ? TextRange(range.first - 1, unit, true) : std::pair<int,int>{0,0};
			if (!strcmp(method, "GetTextAfterOffset"))
				range = TextRange(range.second, unit, true);
			g.returnValue(inv, Tuple({TextString(range.first, range.second), g.variantInt(range.first), g.variantInt(range.second)}));
		} else if (!strcmp(method, "GetCharacterAtOffset")) {
			int offset = ArgInt(args, 0);
			Reply(inv, g.variantInt(offset >= 0 && offset < Count() ? (int)mCurrent->text[offset] : -1));
		} else if (!strcmp(method, "GetAttributeValue")) Reply(inv, g.variantString(""));
		else if (!strcmp(method, "GetDefaultAttributes") || !strcmp(method, "GetDefaultAttributeSet")) Reply(inv, Empty("{ss}"));
		else if (!strcmp(method, "GetAttributes") || !strcmp(method, "GetAttributeRun"))
			g.returnValue(inv, Tuple({Empty("{ss}"), g.variantInt(0), g.variantInt(Count())}));
		else if (!strcmp(method, "GetNSelections")) Reply(inv, g.variantInt(mSelectionStart != mSelectionEnd ? 1 : 0));
		else if (!strcmp(method, "GetSelection")) {
			const bool valid = ArgInt(args, 0) == 0 && mSelectionStart != mSelectionEnd;
			g.returnValue(inv, g.variantNew("(ii)", valid ? mSelectionStart : -1, valid ? mSelectionEnd : -1));
		} else if (!strcmp(method, "SetCaretOffset")) {
			int offset = ArgInt(args, 0);
			bool valid = offset >= 0 && offset <= Count();
			if (valid) {
				mCaret = offset;
				ObjectEvent("TextCaretMoved", "", offset, 0, g.variantInt(0));
			}
			Reply(inv, g.variantBool(valid));
		} else if (!strcmp(method, "AddSelection") || !strcmp(method, "SetSelection")) {
			const bool add = !strcmp(method, "AddSelection");
			int start = ArgInt(args, add ? 0 : 1), end = ArgInt(args, add ? 1 : 2);
			bool valid = start >= 0 && end >= start && end <= Count()
				&& (add ? mSelectionStart == mSelectionEnd : ArgInt(args, 0) == 0);
			if (valid) {
				mSelectionStart = start; mSelectionEnd = end;
				ObjectEvent("TextSelectionChanged", "", 0, 0, g.variantInt(0));
			}
			Reply(inv, g.variantBool(valid));
		} else if (!strcmp(method, "RemoveSelection")) {
			bool valid = ArgInt(args, 0) == 0 && mSelectionStart != mSelectionEnd;
			if (valid) {
				mSelectionStart = mSelectionEnd = 0;
				ObjectEvent("TextSelectionChanged", "", 0, 0, g.variantInt(0));
			}
			Reply(inv, g.variantBool(valid));
		} else if (!strcmp(method, "GetCharacterExtents") || !strcmp(method, "GetRangeExtents")) {
			bool single = !strcmp(method, "GetCharacterExtents");
			int start = ArgInt(args, 0);
			int end = single ? (start >= 0 && start < Count() ? start + 1 : -1) : ArgInt(args, 1);
			auto rect = Bounds(start, end, ArgUInt(args, single ? 1 : 2));
			g.returnValue(inv, g.variantNew("(iiii)", rect.x, rect.y, rect.w, rect.h));
		} else if (!strcmp(method, "GetOffsetAtPoint"))
			Reply(inv, g.variantInt(OffsetAt(ArgInt(args, 0), ArgInt(args, 1), ArgUInt(args, 2))));
		else if (!strcmp(method, "GetBoundedRanges")) {
			int x = ArgInt(args, 0), y = ArgInt(args, 1), w = ArgInt(args, 2), h = ArgInt(args, 3);
			unsigned coord = ArgUInt(args, 4);
			unsigned xClip = ArgUInt(args, 5), yClip = ArgUInt(args, 6);
			std::vector<GVariant *> ranges;
			for (const auto& line : mCurrent->lines) {
				int first = -1, last = -1;
				for (int column = 0; column < line.length && w > 0 && h > 0; ++column) {
					auto rect = Bounds(line.offset + column, line.offset + column + 1, coord);
					bool included = rect.x < (double)x + w && (double)rect.x + rect.w > x
						&& rect.y < (double)y + h && (double)rect.y + rect.h > y;
					if ((xClip & 1) && rect.x < x) included = false;
					if ((xClip & 2) && (double)rect.x + rect.w > (double)x + w) included = false;
					if ((yClip & 1) && rect.y < y) included = false;
					if ((yClip & 2) && (double)rect.y + rect.h > (double)y + h) included = false;
					if (included) {
						if (first < 0) first = line.offset + column;
						last = line.offset + column + 1;
					}
				}
				if (first >= 0)
					ranges.push_back(Tuple({g.variantInt(first), g.variantInt(last),
						TextString(first, last), g.variantBox(g.variantInt(0))}));
			}
			Reply(inv, g.variantArray(Type("(iisv)"), ranges.data(), ranges.size()));
		} else if (!strcmp(method, "ScrollSubstringTo") || !strcmp(method, "ScrollSubstringToPoint"))
			Reply(inv, g.variantBool(false)); // emulator pixels are not a scrollable document
	}
	void ComponentMethod(const char *path, const char *method, GVariant *args, GDBusMethodInvocation *inv) {
		auto rectangle = [&](unsigned coord) {
			const auto& s = *mCurrent;
			const bool frame = !strcmp(path, kFrame);
			return Rect{(frame ? 0 : (int)s.x) + (coord == 0 ? s.windowX : 0),
				(frame ? 0 : (int)s.y) + (coord == 0 ? s.windowY : 0),
				frame ? s.windowWidth : (int)s.width, frame ? s.windowHeight : (int)s.height};
		};
		if (!strcmp(method, "Contains") || !strcmp(method, "GetAccessibleAtPoint")) {
			int x = ArgInt(args, 0), y = ArgInt(args, 1);
			auto r = rectangle(ArgUInt(args, 2));
			bool inside = x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
			if (!strcmp(method, "Contains")) Reply(inv, g.variantBool(inside));
			else Reply(inv, inside ? Ref(!strcmp(path, kFrame) ? kScreen : path) : Ref(kNull, ""));
		} else if (!strcmp(method, "GetExtents")) {
			auto r = rectangle(ArgUInt(args, 0));
			Reply(inv, g.variantNew("(iiii)", r.x, r.y, r.w, r.h));
		} else if (!strcmp(method, "GetPosition")) {
			auto r = rectangle(ArgUInt(args, 0));
			g.returnValue(inv, g.variantNew("(ii)", r.x, r.y));
		} else if (!strcmp(method, "GetSize")) {
			auto r = rectangle(1);
			g.returnValue(inv, g.variantNew("(ii)", r.w, r.h));
		} else if (!strcmp(method, "GetLayer")) Reply(inv, g.variantUInt(3)); // ATSPI_LAYER_WIDGET
		else if (!strcmp(method, "GetMDIZOrder")) Reply(inv, g.variantNew("n", 0));
		else if (!strcmp(method, "GetAlpha")) Reply(inv, g.variantNew("d", 1.0));
		else if (!strcmp(method, "GrabFocus")) Reply(inv, g.variantBool(mCurrent->focused));
		else Reply(inv, g.variantBool(false)); // no cross-thread focus, resize, or scroll mutation
	}
	GVariant *CacheItem(const char *path) {
		return Tuple({Ref(path), Ref(kRoot), Parent(path), g.variantInt(!strcmp(path, kRoot) ? -1 : 0),
			g.variantInt(!strcmp(path, kScreen) ? 0 : 1), Interfaces(path),
			g.variantString(Name(path)), g.variantUInt(Role(path)),
			g.variantString(""), States(path)});
	}
	void Method(const char *path, const char *interface, const char *method,
		GVariant *args, GDBusMethodInvocation *inv) {
		if (!strcmp(interface, kAccessible)) AccessibleMethod(path, method, args, inv);
		else if (!strcmp(interface, kText)) TextMethod(method, args, inv);
		else if (!strcmp(interface, kComponent)) ComponentMethod(path, method, args, inv);
		else if (!strcmp(interface, kApplication)) {
			Reply(inv, g.variantString(!strcmp(method, "GetLocale") ? "C.UTF-8" : ""));
		} else if (!strcmp(interface, "org.a11y.atspi.Cache")) {
			GVariant *items[] {CacheItem(kRoot), CacheItem(kFrame), CacheItem(kScreen)};
			Reply(inv, g.variantArray(Type("((so)(so)(so)iiassusau)"), items, 3));
		} else g.returnError(inv, "org.freedesktop.DBus.Error.UnknownMethod", "Unsupported accessibility method");
	}
	GVariant *Property(const char *path, const char *interface, const char *property) {
		if (!strcmp(interface, kAccessible)) {
			if (!strcmp(property, "Name")) return g.variantString(Name(path));
			if (!strcmp(property, "Description")) return g.variantString(!strcmp(path, kScreen) ? "Emulated Atari text display" : "");
			if (!strcmp(property, "Parent")) return Parent(path);
			if (!strcmp(property, "ChildCount")) return g.variantInt(!strcmp(path, kScreen) ? 0 : 1);
			if (!strcmp(property, "Locale")) return g.variantString("C.UTF-8");
			if (!strcmp(property, "AccessibleId")) return g.variantString(path);
			if (!strcmp(property, "HelpText")) return g.variantString("");
		} else if (!strcmp(interface, kText)) {
			if (!strcmp(property, "CharacterCount")) return g.variantInt(Count());
			if (!strcmp(property, "CaretOffset")) return g.variantInt(mCaret);
		} else if (!strcmp(interface, kApplication)) {
			if (!strcmp(property, "Id")) return g.variantInt(mId);
			if (!strcmp(property, "ToolkitName")) return g.variantString("AltirraSDL");
			if (!strcmp(property, "ToolkitVersion")) return g.variantString("1");
			if (!strcmp(property, "Version")) return g.variantString("1");
			if (!strcmp(property, "AtspiVersion")) return g.variantString("2.1");
			if (!strcmp(property, "InterfaceVersion")) return g.variantUInt(1);
		}
		if (!strcmp(property, "version")) return g.variantUInt(1);
		return nullptr;
	}
	static void OnMethod(GDBusConnection *, const char *, const char *path,
		const char *interface, const char *method, GVariant *args,
		GDBusMethodInvocation *inv, void *data) {
		static_cast<Publisher *>(data)->Method(path, interface, method, args, inv);
	}
	static GVariant *OnGet(GDBusConnection *, const char *, const char *path,
		const char *interface, const char *property, GError **, void *data) {
		return static_cast<Publisher *>(data)->Property(path, interface, property);
	}
	static int OnSet(GDBusConnection *, const char *, const char *,
		const char *interface, const char *property, GVariant *value, GError **, void *data) {
		auto& self = *static_cast<Publisher *>(data);
		if (!strcmp(interface, kApplication) && !strcmp(property, "Id")) {
			self.mId = self.g.variantGetInt(value);
			return true;
		}
		return false;
	}
	static void OnEmbedded(void *connection, GAsyncResult *result, void *data) {
		auto& self = *static_cast<Publisher *>(data);
		GError *error = nullptr;
		auto *reply = self.g.callFinish(static_cast<GDBusConnection *>(connection), result, &error);
		self.mEmbedding = false;
		if (reply) {
			const char *name, *path;
			self.g.variantGet(reply, "((&s&o))", &name, &path);
			self.mDesktopName = name;
			self.mDesktopPath = path;
			self.g.variantUnref(reply);
			self.mEmbedded = true;
			ATSDLAccessibilityPublishStatus(ATSDLAccessibilityState::Active,
				"AT-SPI screen reader support is active.");
			auto snapshot = self.mCurrent;
			self.mCurrent.reset();
			self.SendChanges(snapshot);
		}
		if (error) {
			self.g.errorFree(error);
			ATSDLAccessibilityPublishStatus(ATSDLAccessibilityState::Connecting,
				"Waiting for the AT-SPI accessibility registry; retrying automatically.");
		}
	}
	static void OnOwnerChanged(GDBusConnection *, const char *, const char *,
		const char *, const char *, GVariant *, void *data) {
		auto& self = *static_cast<Publisher *>(data);
		self.mEmbedded = false;
		self.mDesktopName.clear();
		self.mDesktopPath.clear();
		self.mRetry = {};
		ATSDLAccessibilityPublishStatus(ATSDLAccessibilityState::Connecting,
			"The accessibility registry changed; reconnecting...");
	}
	void Embed() {
		mEmbedding = true;
		g.call(mConnection, "org.a11y.atspi.Registry", kRoot, "org.a11y.atspi.Socket", "Embed",
			Tuple({Ref(kRoot)}), nullptr, 0, 1000, nullptr, OnEmbedded, this);
	}
	bool Connect() {
		ATSDLAccessibilityPublishStatus(ATSDLAccessibilityState::Connecting,
			"Waiting for the desktop accessibility bus. Install or enable the system AT-SPI services; retrying automatically.");
		GError *error = nullptr;
		auto *session = g.busGet(2, nullptr, &error); // G_BUS_TYPE_SESSION
		if (!session) { if (error) g.errorFree(error); return false; }
		auto *reply = g.callSync(session, "org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus",
			"GetAddress", nullptr, nullptr, 0, 1000, nullptr, &error);
		g.objectUnref(session);
		if (!reply) { if (error) g.errorFree(error); return false; }
		const char *address;
		g.variantGet(reply, "(&s)", &address);
		// G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | MESSAGE_BUS_CONNECTION
		mConnection = g.connectionNew(address, 9, nullptr, nullptr, &error);
		g.variantUnref(reply);
		if (!mConnection) { if (error) g.errorFree(error); return false; }
		mName = g.connectionName(mConnection);
		static const InterfaceVTable vtable {OnMethod, OnGet, OnSet, {}};
		const auto add = [&](const char *path, const char *interface) {
			unsigned id = g.registerObject(mConnection, path, g.lookupInterface(mNode, interface),
				&vtable, this, nullptr, nullptr);
			if (id) mObjects.push_back(id);
			return id != 0;
		};
		bool ok = add(kRoot, kAccessible) && add(kRoot, kApplication)
			&& add(kFrame, kAccessible) && add(kFrame, kComponent)
			&& add(kScreen, kAccessible) && add(kScreen, kComponent)
			&& add(kScreen, kText) && add(kCache, "org.a11y.atspi.Cache");
		if (!ok) { Disconnect(); return false; }
		mOwnerSubscription = g.subscribe(mConnection, "org.freedesktop.DBus",
			"org.freedesktop.DBus", "NameOwnerChanged", "/org/freedesktop/DBus",
			"org.a11y.atspi.Registry", 0, OnOwnerChanged, this, nullptr);
		Embed();
		return true;
	}
	void Disconnect() {
		if (!mConnection)
			return;
		if (mEmbedded && !g.connectionClosed(mConnection))
			g.call(mConnection, "org.a11y.atspi.Registry", kRoot, "org.a11y.atspi.Socket", "Unembed",
				Tuple({Ref(kRoot)}), nullptr, 0, 500, nullptr, nullptr, nullptr);
		for (auto id : mObjects) g.unregisterObject(mConnection, id);
		mObjects.clear();
		if (mOwnerSubscription) g.unsubscribe(mConnection, mOwnerSubscription);
		mOwnerSubscription = 0;
		// Finish outstanding Embed callbacks before destroying their context/data.
		g.connectionClose(mConnection, nullptr, nullptr);
		while (mEmbedding) g.contextIteration(mContext, true);
		g.objectUnref(mConnection);
		mConnection = nullptr;
		mEmbedded = false;
		if (!mStop)
			ATSDLAccessibilityPublishStatus(ATSDLAccessibilityState::Connecting,
				"The accessibility bus disconnected; reconnecting...");
		mDesktopName.clear(); mDesktopPath.clear();
	}
	void Run() {
		if (!g.Load()) {
			ATSDLAccessibilityPublishStatus(ATSDLAccessibilityState::Unavailable,
				"The desktop GIO runtime (libgio-2.0.so.0) is unavailable. Install it, then disable and enable screen reader support again.");
			return;
		}
		mContext = g.contextNew();
		g.contextPush(mContext);
		mNode = g.nodeNew(kXml, nullptr);
		if (!mNode)
			ATSDLAccessibilityPublishStatus(ATSDLAccessibilityState::Unavailable,
				"The desktop GIO runtime could not initialize the AT-SPI interface.");
		mCurrent = std::atomic_load(&mPublished);
		mRetry = {};
		while (!mStop && mNode && mCurrent) {
			auto next = std::atomic_load(&mPublished);
			if (next != mCurrent)
				SendChanges(next);
			const auto now = std::chrono::steady_clock::now();
			if (mConnection && g.connectionClosed(mConnection)) Disconnect();
			if ((!mConnection || (!mEmbedded && !mEmbedding)) && now >= mRetry) {
				mRetry = now + std::chrono::seconds(5);
				if (!mConnection) Connect();
				else Embed();
			}
			while (g.contextIteration(mContext, false)) {}
			std::unique_lock lock(mWaitMutex);
			mWake.wait_for(lock, std::chrono::milliseconds(10));
		}
		Disconnect();
		if (mNode) g.nodeUnref(mNode);
		mNode = nullptr;
		g.contextPop(mContext);
		g.contextUnref(mContext);
		mContext = nullptr;
		mCurrent.reset();
	}
};

Publisher g_publisher;
}

void ATSDLAccessibilityPlatformUpdate(SDL_Window *,
	std::shared_ptr<const ATSDLAccessibilitySnapshot> snapshot) {
	g_publisher.Update(std::move(snapshot));
}

void ATSDLAccessibilityPlatformShutdown() {
	g_publisher.Stop();
}
