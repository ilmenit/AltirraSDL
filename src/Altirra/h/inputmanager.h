//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2009 Avery Lee
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#ifndef f_AT_INPUTMANAGER_H
#define f_AT_INPUTMANAGER_H

#include <map>
#include <set>
#include <vd2/system/VDString.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/vectors.h>
#include <vd2/system/refcount.h>
#include "inputdefs.h"

class ATInputMap;
class ATPokeyEmulator;
class ATPortInputController;
class ATJoystickController;
class ATMouseController;
class ATPaddleController;
class IATJoystickManager;
class ATScheduler;
class VDRegistryKey;
class ATLightPenPort;
class IATDevicePortManager;
class IATDeviceControllerPort;

enum ATInputControllerType : uint32;

class IATInputConsoleCallback {
public:
	virtual void SetConsoleTrigger(uint32 id, bool state) = 0;
};

class IATInputUnitNameSource {
public:
	virtual bool GetInputCodeName(uint32 id, VDStringW& name) const = 0;
};

struct atfixedhash_basenode {
	atfixedhash_basenode *mpNext;
	atfixedhash_basenode *mpPrev;
};

template<typename T>
struct atfixedhash_node : public atfixedhash_basenode {
	T mValue;

	atfixedhash_node(const T& v) : mValue(v) {}
};

template<typename T>
struct athash
{
	size_t operator()(const T& value) const {
		return (size_t)value;
	}
};

template<typename K, typename V, size_t N>
class atfixedhash {
public:
	typedef K key_type;
	typedef V mapped_type;
	typedef typename std::pair<K, V> value_type;
	typedef value_type *pointer;
	typedef const value_type *const_pointer;
	typedef value_type& reference;
	typedef const value_type& const_reference;
	typedef athash<K> hasher;

	typedef pointer iterator;
	typedef const_pointer const_iterator;

	atfixedhash();
	~atfixedhash();

	const_iterator end() const;

	void clear();

	template<class T>
	void get_keys(T& result) const {
		for(const auto& bucket : m.table) {
			for(const auto *p = bucket.mpNext; p != &bucket; p = p->mpNext)
				result.push_back(static_cast<const atfixedhash_node<value_type> *>(p)->mValue.first);
		}
	}

	iterator find(const key_type& k);
	std::pair<pointer, bool> insert(const value_type& v);

protected:
	struct : hasher {
		atfixedhash_basenode table[N];
	} m;
};

template<typename K, typename V, size_t N>
atfixedhash<K,V,N>::atfixedhash() {
	for(int i=0; i<N; ++i) {
		atfixedhash_basenode& n = m.table[i];

		n.mpNext = n.mpPrev = &n;
	}
}

template<typename K, typename V, size_t N>
atfixedhash<K,V,N>::~atfixedhash() {
	clear();
}

template<typename K, typename V, size_t N>
typename atfixedhash<K,V,N>::const_iterator atfixedhash<K,V,N>::end() const {
	return NULL;
}

template<typename K, typename V, size_t N>
void atfixedhash<K,V,N>::clear() {
	for(int i=0; i<N; ++i) {
		atfixedhash_basenode *bucket = &m.table[i];
		atfixedhash_basenode *p = bucket->mpNext;

		if (p != bucket) {
			do {
				atfixedhash_basenode *n = p->mpNext;

				delete static_cast<atfixedhash_node<value_type> *>(p);

				p = n;
			} while(p != bucket);

			bucket->mpPrev = bucket->mpNext = bucket;
		}
	}
}

template<typename K, typename V, size_t N>
typename atfixedhash<K,V,N>::iterator atfixedhash<K,V,N>::find(const key_type& k) {
	size_t bucketIdx = m(k) % N;
	const atfixedhash_basenode *bucket = &m.table[bucketIdx];

	for(atfixedhash_basenode *p = bucket->mpNext; p != bucket; p = p->mpNext) {
		atfixedhash_node<value_type>& n = static_cast<atfixedhash_node<value_type>&>(*p);

		if (n.mValue.first == k)
			return &n.mValue;
	}

	return NULL;
}

template<typename K, typename V, size_t N>
std::pair<typename atfixedhash<K,V,N>::iterator, bool> atfixedhash<K,V,N>::insert(const value_type& v) {
	size_t bucketIdx = m(v.first) % N;
	
	atfixedhash_basenode *bucket = &m.table[bucketIdx];

	for(atfixedhash_basenode *p = bucket->mpNext; p != bucket; p = p->mpNext) {
		atfixedhash_node<value_type>& n = static_cast<atfixedhash_node<value_type>&>(*p);

		if (n.mValue.first == v.first)
			return std::pair<iterator, bool>(&n.mValue, false);
	}

	atfixedhash_node<value_type> *node = new atfixedhash_node<value_type>(v);
	atfixedhash_basenode *last = bucket->mpPrev;
	node->mpPrev = last;
	node->mpNext = bucket;
	last->mpNext = node;
	bucket->mpPrev = node;
	return std::pair<iterator, bool>(&node->mValue, true);
}

struct ATInputUnitIdentifier {
	char buf[16];

	bool IsZero() const {
		for(int i=0; i<16; ++i) {
			if (buf[i])
				return false;
		}

		return true;
	}

	void SetZero() {
		memset(buf, 0, sizeof buf);
	}

	bool operator==(const ATInputUnitIdentifier& x) const {
		return !memcmp(buf, x.buf, 16);
	}
};

struct ATInputPointerInfo {
	vdfloat2 mPos {};		// center of touch in [-1,+1]
	float mRadius {};		// radius of touch, or <0 if point because device doesn't do area touches
	bool mbPrimary {};		// true if this is the primary touch driven by main position inputs
	bool mbPressed {};		// true if an active touch, false for hover position
	ATInputPointerCoordinateSpace mCoordSpace {};
};

class ATInputManager {
public:
	ATInputManager();
	~ATInputManager();

	void Init(ATScheduler *fastScheduler, ATScheduler *slowScheduler, ATPokeyEmulator& pokey, IATDevicePortManager& portMgr, ATLightPenPort *lightPen);
	void Shutdown();

	bool Is5200Mode() const { return mb5200Mode; }
	void Set5200Mode(bool is5200);

	void ResetToDefaults();

	IATInputConsoleCallback *GetConsoleCallback() const { return mpCB; }
	void SetConsoleCallback(IATInputConsoleCallback *cb) { mpCB = cb; }

	void Select5200Controller(int index, bool potsEnabled);
	void SelectMultiJoy(int multiIndex);

	void ColdReset();
	void Poll(float dt);

	int GetInputUnitCount() const;
	const wchar_t *GetInputUnitName(int index) const;
	int GetInputUnitIndexById(const ATInputUnitIdentifier& id) const;
	int RegisterInputUnit(const ATInputUnitIdentifier& id, const wchar_t *name, IATInputUnitNameSource *nameSource);
	void UnregisterInputUnit(int unit);

	/// Enables or disables restricted mode. Restricted mode limits triggers
	/// UI triggers only. All other triggers are forced off.
	void SetRestrictedMode(bool restricted);

	/// Redirect port output for all controllers bound to srcPortIdx into
	/// |dstPort| (a caller-owned proxy IATDeviceControllerPort) instead
	/// of the real hardware port. The real port is neutralized while the
	/// redirect is active. Pass nullptr as dstPort to remove the redirect.
	///
	/// Used by netplay: the local player always maps to port 1 in the
	/// user's own config, but on the wire that input may become port 1
	/// or port 2 depending on the peer's role (host vs joiner). Capture
	/// into a buffer, send, then re-apply to the real ports in the
	/// correct order — without also double-driving the local port 1.
	///
	/// The redirect survives RebuildMappings(), so LoadMaps/ActivateInputMap
	/// called while a netplay session is active still get captured.
	void SetPortRedirect(int srcPortIdx, IATDeviceControllerPort *dstPort);

	bool IsInputMapped(int unit, uint32 inputCode) const;
	bool IsMouseMapped() const { return mbMouseMapped; }
	bool IsMouseAbsoluteMode() const { return mbMouseAbsMode; }
	bool IsMouseActiveTarget() const { return mbMouseActiveTarget; }

	void OnButtonChanged(int unit, int id, bool down);
	void OnButtonDown(int unit, int id);
	void OnButtonUp(int unit, int id);
	void OnAxisInput(int unit, int axis, sint32 value, sint32 deadifiedValue);
	void OnMouseMove(int unit, int dx, int dy);
	bool OnMouseWheel(int unit, float delta);
	bool OnMouseHWheel(int unit, float delta);
	void SetMouseBeamPos(int x, int y);
	void SetMousePadPos(int x, int y);
	void SetMouseVirtualStickPos(int x, int y);

	// Returns true if there is a mapping from an absolute (positional) mouse input to a pointer controller.
	// This suggests that primary input pointer visualization should be hidden to avoid overlapping with the
	// mouse pointer.
	bool HasAbsMousePointer() const;

	// Returns true if there is a pointer in non-beam space. This means that the pointer positions are not
	// tied to the screen and so bounds visualizations are useful.
	bool HasNonBeamPointer() const;

	void GetPointers(vdfastvector<ATInputPointerInfo>& pointers) const;

	void ActivateFlag(uint32 id, bool state);
	void ReleaseButtons(uint32 idmin, uint32 idmax);

	void GetNameForInputCode(uint32 code, VDStringW& name) const;
	void GetNameForTargetCode(uint32 code, ATInputControllerType type, VDStringW& name) const;
	bool IsAnalogTrigger(uint32 code, ATInputControllerType type) const;

	uint32 GetInputMapCount() const;
	bool GetInputMapByIndex(uint32 index, ATInputMap **imap) const;
	bool IsInputMapEnabled(ATInputMap *imap) const;
	void AddInputMap(ATInputMap *imap);
	void RemoveInputMap(ATInputMap *imap);
	void RemoveAllInputMaps();
	void ActivateInputMap(ATInputMap *imap, bool enable);
	ATInputMap *CycleQuickMaps();

	uint32 GetPresetInputMapCount() const;
	bool GetPresetInputMapByIndex(uint32 index, ATInputMap **imap) const;

	bool LoadMaps(VDRegistryKey& key);
	void LoadSelections(VDRegistryKey& key, ATInputControllerType defaultControllerType);
	void SaveMaps(VDRegistryKey& key);
	void SaveSelections(VDRegistryKey& key);

protected:
	struct Mapping;
	struct Trigger;
	struct PresetMapDef;

	void RebuildMappings();
	void ActivateMappings(uint32 id, bool state);
	void ActivateAnalogMappings(uint32 id, int ds, int dsdead);
	bool ActivateImpulseMappings(uint32 id, int ds);
	void ClearTriggers();
	void SetTrigger(Mapping& mapping, bool state);
	void Update5200Controller();
	uint32 GetPresetMapDefCount() const;
	const PresetMapDef *GetPresetMapDef(uint32 index) const;
	void InitPresetMap(const PresetMapDef& def, ATInputMap **ppMap) const;
	void InitPresetMaps();
	bool IsTriggerRestricted(const Trigger& trigger) const;

	ATScheduler *mpSlowScheduler = nullptr;
	ATScheduler *mpFastScheduler = nullptr;
	ATPokeyEmulator *mpPokey = nullptr;
	ATLightPenPort *mpLightPen = nullptr;
	IATDevicePortManager *mpPortMgr = nullptr;
	IATInputConsoleCallback *mpCB = nullptr;
	// Default-initialised because the ctor does not list it and no header
	// default existed historically.  Previously the flag held indeterminate
	// heap bytes until the first SetRestrictedMode() call.  The Win32 build
	// happened to mask this because uivideodisplaywindow.cpp always calls
	// SetRestrictedMode(false) during display-window creation, which runs
	// before any key press.  The SDL3 frontend has no equivalent call site,
	// so the garbage byte leaked into IsTriggerRestricted() and — when
	// non-zero — suppressed every joystick direction and fire press in
	// ActivateMappings(), producing the "arrows-as-joystick does nothing
	// after fresh config" report on Linux Mint.  Keep the default here so
	// the field is safe regardless of which frontend instantiates us.
	bool mbRestrictedMode = false;
	// Netplay port-capture state: when mPortRedirectSrc >= 0,
	// controllers bound to that port index are redirected to
	// mpPortRedirectDst. Re-applied after every RebuildMappings.
	int mPortRedirectSrc = -1;
	IATDeviceControllerPort *mpPortRedirectDst = nullptr;
	int m5200ControllerIndex;
	bool mb5200PotsEnabled;
	bool mb5200Mode;
	bool mbMouseAbsMode;
	bool mbMouseMapped;
	bool mbMouseActiveTarget;

	// True if there is a mapping from a mouse input to a controller with a pointer in absolute mode.
	bool mbMouseAbsMappedToPointer = false;

	// True if one of the controllers has a pointer in normalized coordinates (thus not directly tied to
	// screen positions).
	bool mbControllerHasNonBeamPointer = false;

	uint32 mMouseAvgQueue[4];
	int mMouseAvgIndex;
	float mMouseWheelAccum = 0;
	float mMouseHWheelAccum = 0;

	uint8 mMultiMask = 0xFF;

	typedef atfixedhash<int, uint32, 64> Buttons;
	Buttons mButtons;

	struct Mapping {
		uint32 mTriggerIdx;
		uint32 mFlagIndex1;
		uint32 mFlagIndex2;
		bool mbFlagValue1;
		bool mbFlagValue2;
		bool mbMotionActive;
		bool mbTriggerActivated;
		uint8 mAutoCounter;
		uint8 mAutoPeriod;
		uint8 mAutoValue;
		float mMotionSpeed;
		float mMotionAccel;
		float mMotionDrag;
	};

	typedef vdfastvector<bool> Flags;
	Flags mFlags;

	typedef std::multimap<uint32, Mapping> Mappings;
	Mappings mMappings;

	struct Trigger {
		uint32 mId;
		uint32 mCount;
		ATPortInputController *mpController;
	};

	typedef vdfastvector<Trigger> Triggers;
	Triggers mTriggers;

	typedef std::map<ATInputMap *, bool> InputMaps;
	InputMaps mInputMaps;

	struct ControllerInfo {
		ATPortInputController *mpInputController;
		bool mbBoundToMouseAbs;
	};

	typedef vdfastvector<ControllerInfo> InputControllers;
	InputControllers mInputControllers;

	uint32	mAllocatedUnits;
	ATInputUnitIdentifier mUnitIds[32];
	VDStringW	mUnitNames[32];
	IATInputUnitNameSource *mpUnitNameSources[32];

	static const PresetMapDef kPresetMapDefs[];
};

#endif	// f_AT_INPUTMANAGER_H
