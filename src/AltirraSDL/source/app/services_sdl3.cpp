// SDL3 port of the non-dialog helpers from src/Dita/source/services.cpp.
//
// Windows Altirra stores the last-used directory for each file dialog in
// a per-key map (keyed by FourCC) that is persisted to the registry under
// "Saved filespecs".  The SDL3 frontend re-uses the exact same API and
// key values so settings.ini stays interoperable between the two builds.
//
// This file intentionally mirrors the relevant code in services.cpp
// verbatim — only the Win32 common-dialog code is omitted.

#include <map>
#include <string.h>

#include <vd2/system/filesys.h>
#include <vd2/system/registry.h>
#include <vd2/system/strutil.h>
#include <vd2/system/thread.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/VDString.h>
#include <vd2/Dita/services.h>

namespace {
	// MAX_PATH is a Win32 constant; use the same value Windows uses so the
	// on-disk format is bit-compatible with settings.ini written by the
	// Windows build.
	constexpr size_t kMaxPath = 260;

	struct FilespecEntry {
		wchar_t szFile[kMaxPath];
	};

	typedef std::map<long, FilespecEntry> tFilespecMap;

	tFilespecMap *g_pFilespecMap;
	VDCriticalSection g_csFilespecMap;
}

void VDInitFilespecSystem() {
	if (!g_pFilespecMap) {
		static vdautoptr<tFilespecMap> spFilespecMap(new tFilespecMap);
		g_pFilespecMap = spFilespecMap;
	}
}

void VDSaveFilespecSystemData() {
	vdsynchronized(g_csFilespecMap) {
		if (g_pFilespecMap) {
			VDRegistryAppKey key("Saved filespecs");

			for(tFilespecMap::const_iterator it(g_pFilespecMap->begin()), itEnd(g_pFilespecMap->end()); it!=itEnd; ++it) {
				long id = it->first;
				const FilespecEntry& fse = it->second;
				char buf[16];

				sprintf(buf, "%08x", (unsigned)id);

				key.setString(buf, fse.szFile);
			}
		}
	}
}

void VDLoadFilespecSystemData() {
	vdsynchronized(g_csFilespecMap) {
		VDInitFilespecSystem();

		if (g_pFilespecMap) {
			VDRegistryAppKey key("Saved filespecs", false);
			VDRegistryValueIterator it(key);

			VDStringW value;
			while(const char *s = it.Next()) {
				unsigned long specKey = strtoul(s, NULL, 16);
				if (key.getString(s, value))
					VDSetLastLoadSavePath(specKey, value.c_str());
			}
		}
	}
}

void VDClearFilespecSystemData() {
	vdsynchronized(g_csFilespecMap) {
		if (g_pFilespecMap) {
			g_pFilespecMap->clear();
		}

		VDRegistryAppKey key("Saved filespecs");

		VDRegistryValueIterator it(key);
		vdvector<VDStringA> values;

		while(const char *s = it.Next())
			values.emplace_back(s);

		for(const VDStringA& value : values)
			key.removeValue(value.c_str());
	}
}

void VDSetLastLoadSavePath(long nKey, const wchar_t *path) {
	vdsynchronized(g_csFilespecMap) {
		VDInitFilespecSystem();

		tFilespecMap::iterator it = g_pFilespecMap->find(nKey);

		if (it == g_pFilespecMap->end()) {
			std::pair<tFilespecMap::iterator, bool> r = g_pFilespecMap->insert(tFilespecMap::value_type(nKey, FilespecEntry()));

			if (!r.second)
				return;

			it = r.first;
		}

		FilespecEntry& fsent = (*it).second;

		wcsncpyz(fsent.szFile, path, sizeof fsent.szFile / sizeof fsent.szFile[0]);
	}
}

const VDStringW VDGetLastLoadSavePath(long nKey) {
	VDStringW result;

	vdsynchronized(g_csFilespecMap) {
		VDInitFilespecSystem();

		tFilespecMap::iterator it = g_pFilespecMap->find(nKey);

		if (it != g_pFilespecMap->end()) {
			FilespecEntry& fsent = (*it).second;

			result = fsent.szFile;
		}
	}

	return result;
}

void VDSetLastLoadSaveFileName(long nKey, const wchar_t *fileName) {
	vdsynchronized(g_csFilespecMap) {
		VDInitFilespecSystem();

		tFilespecMap::iterator it = g_pFilespecMap->find(nKey);

		if (it == g_pFilespecMap->end()) {
			std::pair<tFilespecMap::iterator, bool> r = g_pFilespecMap->insert(tFilespecMap::value_type(nKey, FilespecEntry()));

			if (!r.second)
				return;

			it = r.first;
		}

		FilespecEntry& fsent = (*it).second;

		VDStringW newPath(VDMakePath(VDFileSplitPathLeft(VDStringW(fsent.szFile)).c_str(), fileName));

		wcsncpyz(fsent.szFile, newPath.c_str(), sizeof fsent.szFile / sizeof fsent.szFile[0]);
	}
}
