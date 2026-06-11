//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
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

#ifndef f_AT_INPUTMAP_H
#define f_AT_INPUTMAP_H

#include <vd2/system/refcount.h>
#include "inputdefs.h"

class ATInputMap final : public vdrefcounted<IVDRefCount> {
public:
	struct Controller {
		ATInputControllerType mType;
		uint32 mIndex;
	};

	struct Mapping {
		uint32 mInputCode;
		uint32 mControllerId;
		uint32 mCode;
	};

	ATInputMap();
	~ATInputMap();

	const wchar_t *GetName() const;
	void SetName(const wchar_t *name);

	bool IsQuickMap() const { return mbQuickMap; }
	void SetQuickMap(bool q) { mbQuickMap = q; }

	bool UsesPhysicalPort(int portIdx) const;

	void Clear();

	int GetSpecificInputUnit() const {
		return mSpecificInputUnit;
	}

	void SetSpecificInputUnit(int index) {
		mSpecificInputUnit = index;
	}

	uint32 GetControllerCount() const;
	bool HasControllerType(ATInputControllerType type) const;
	bool HasController(ATInputControllerType type, uint32 index) const;
	const Controller& GetController(uint32 i) const;
	uint32 AddController(ATInputControllerType type, uint32 index);
	void AddControllers(std::initializer_list<Controller> controllers);

	uint32 GetMappingCount() const;
	const Mapping& GetMapping(uint32 i) const;
	void AddMapping(uint32 inputCode, uint32 controllerId, uint32 code);
	void AddMappings(std::initializer_list<Mapping> mappings);

	bool Load(VDRegistryKey& key, const char *name);
	void Save(VDRegistryKey& key, const char *name);

protected:
	typedef vdfastvector<Controller> Controllers;
	Controllers mControllers;

	typedef vdfastvector<Mapping> Mappings;
	Mappings mMappings;

	VDStringW mName;
	int mSpecificInputUnit;

	bool mbQuickMap;
};

#endif
