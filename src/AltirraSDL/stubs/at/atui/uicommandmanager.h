//	Altirra SDL3 build - stub for <at/atui/uicommandmanager.h>
//	Provides the full header content without Win32 dependencies.
//	The real header has no Win32 deps but lives in ATNativeUI's include
//	tree which isn't on the SDL3 include path.

#ifndef f_AT_ATUI_UICOMMANDMANAGER_H
#define f_AT_ATUI_UICOMMANDMANAGER_H

#include <vd2/system/linearalloc.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/vdstl_hashmap.h>
#include <vd2/system/vdstl_vectorview.h>
#include <at/atcore/notifylist.h>

struct VDAccelToCommandEntry;
class VDStringW;
struct ATUICommand;
class ATUICommandManager;

enum ATUICmdState {
	kATUICmdState_None,
	kATUICmdState_Checked,
	kATUICmdState_RadioChecked
};

struct ATUICommandBaseOptions {
	bool mbQuiet = false;
	vdfunction<void(const ATUICommand *cmd, vdspan<const VDStringW> args)> mpOnCommandExecuted;
};

struct ATUICommandOptions final : public ATUICommandBaseOptions {
	vdfastvector<VDStringSpanW> mInArgs;
};

class ATUICommandContext final : public ATUICommandBaseOptions, public vdrefcount {
public:
	bool GetArg(size_t index, VDStringW& s) const;
	void SetArg(size_t index, const wchar_t *s);
	void MarkCompleted(bool succeeded);

private:
	friend class ATUICommandManager;

	ATUICommandManager *mpParent = nullptr;
	const ATUICommand *mpCommand = nullptr;
	bool mbRecordArgs = false;
	bool mbCommandCompleted = false;
	bool mbCommandCancelled = false;
	vdvector<VDStringW> mInArgs;
	vdvector<VDStringW> mOutArgs;
};

typedef void (*ATUICmdExecuteFnBase)();
typedef void (*ATUICmdExecuteFnCtx)(ATUICommandContext&);
typedef bool (*ATUICmdTestFn)();
typedef ATUICmdState (*ATUICmdStateFn)();
typedef void (*ATUICmdFormatFn)(VDStringW&);

struct ATUICommandExecuteFn {
	ATUICommandExecuteFn() = default;

	template<typename T> requires std::is_convertible_v<T, ATUICmdExecuteFnBase>
	constexpr ATUICommandExecuteFn(T&& fn) : mpBase(fn), mbHasCtx(false) {}

	template<typename T> requires std::is_convertible_v<T, ATUICmdExecuteFnCtx>
	constexpr ATUICommandExecuteFn(T&& fn) : mpCtx(fn), mbHasCtx(true) {}

	void operator()(ATUICommandContext& ctx) const {
		if (mbHasCtx)
			mpCtx(ctx);
		else
			mpBase();
	}

	union {
		ATUICmdExecuteFnBase mpBase {};
		ATUICmdExecuteFnCtx mpCtx;
	};

	bool mbHasCtx = false;
};

struct ATUICommand {
	const char *mpName;
	ATUICommandExecuteFn mpExecuteFn;
	ATUICmdTestFn mpTestFn;
	ATUICmdStateFn mpStateFn;
	ATUICmdFormatFn mpFormatFn;
};

class ATUICommandManager {
	ATUICommandManager(const ATUICommandManager&);
	ATUICommandManager& operator=(const ATUICommandManager&);
public:
	ATUICommandManager();
	~ATUICommandManager();

	void RegisterCommand(const ATUICommand *cmd);
	void RegisterCommands(const ATUICommand *cmd, size_t n);

	const ATUICommand *GetCommand(const char *str) const;
	bool ExecuteCommand(const char *str, const ATUICommandOptions& ctx = ATUICommandOptions());
	bool ExecuteCommand(const ATUICommand& cmd, const ATUICommandOptions& ctx = ATUICommandOptions());
	bool ExecuteCommandNT(const char *str, const ATUICommandOptions& ctx = ATUICommandOptions()) noexcept;
	bool ExecuteCommandNT(const ATUICommand& cmd, const ATUICommandOptions& ctx = ATUICommandOptions()) noexcept;

	void ListCommands(vdfastvector<VDAccelToCommandEntry>& commands) const;

private:
	friend class ATUICommandContext;

	struct Node {
		Node *mpNext;
		uint32 mHash;
		const ATUICommand *mpCmd;
	};

	VDLinearAllocator mAllocator;

	vdrefptr<ATUICommandContext> mpCommandContext;

	enum { kHashTableSize = 257 };
	Node *mpHashTable[kHashTableSize];
};

#endif
