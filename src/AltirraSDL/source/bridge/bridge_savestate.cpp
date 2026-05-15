// AltirraBridge - save-state capability (impl)

#include <stdafx.h>

#include "bridge_savestate.h"
#include "bridge_commands_write.h"   // HardwareModeToStr / MemoryModeToStr (via header forward)

#include "simulator.h"
#include "cpu.h"
#include "constants.h"
#include "savestateio.h"             // ATCreateSaveStateSerializer / Deserializer

#include <at/atcore/serialization.h>
#include <at/atcore/media.h>         // kATMediaWriteMode_RO
#include <at/atio/image.h>           // ATImageLoadContext (full definition)

#include <vd2/system/file.h>
#include <vd2/system/zip.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/error.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ATBridge {

// HardwareModeToStr / MemoryModeToStr live inside the anonymous
// namespace of bridge_commands_write.cpp. Rather than expose them,
// the bridge_savestate translation unit has its own tiny mirror -- a
// handful of strings is cheaper than reshuffling the header surface.
namespace {

const char* HwModeStr(ATHardwareMode m) {
	switch (m) {
		case kATHardwareMode_800:    return "800";
		case kATHardwareMode_800XL:  return "800XL";
		case kATHardwareMode_1200XL: return "1200XL";
		case kATHardwareMode_130XE:  return "130XE";
		case kATHardwareMode_XEGS:   return "XEGS";
		case kATHardwareMode_1400XL: return "1400XL";
		case kATHardwareMode_5200:   return "5200";
		default:                     return "unknown";
	}
}

const char* MemModeStr(ATMemoryMode m) {
	switch (m) {
		case kATMemoryMode_8K:         return "8K";
		case kATMemoryMode_16K:        return "16K";
		case kATMemoryMode_24K:        return "24K";
		case kATMemoryMode_32K:        return "32K";
		case kATMemoryMode_40K:        return "40K";
		case kATMemoryMode_48K:        return "48K";
		case kATMemoryMode_52K:        return "52K";
		case kATMemoryMode_64K:        return "64K";
		case kATMemoryMode_128K:       return "128K";
		case kATMemoryMode_256K:       return "256K";
		case kATMemoryMode_320K:       return "320K";
		case kATMemoryMode_320K_Compy: return "320K_Compy";
		case kATMemoryMode_576K:       return "576K";
		case kATMemoryMode_576K_Compy: return "576K_Compy";
		case kATMemoryMode_1088K:      return "1088K";
		default:                       return "unknown";
	}
}

// JSON helpers -- mirror the style in bridge_commands_write.cpp. We
// re-declare here to keep bridge_savestate independently linkable
// without pulling the whole write module into client targets that
// might one day want a smaller surface.
std::string JsonEscape(const std::string& s) {
	std::string out;
	out.reserve(s.size() + 2);
	for (char c : s) {
		switch (c) {
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n";  break;
			case '\r': out += "\\r";  break;
			case '\t': out += "\\t";  break;
			default:
				if ((unsigned char)c < 0x20) {
					char b[8];
					std::snprintf(b, sizeof b, "\\u%04x", (unsigned char)c);
					out += b;
				} else {
					out += c;
				}
				break;
		}
	}
	return out;
}

// Slot store -- module-local. Single-threaded by construction (bridge
// runs on one thread). std::map for sorted-by-name iteration in List.
std::map<std::string, std::pair<std::vector<uint8_t>, StateMetadata>> g_slots;

}  // namespace

// ---------------------------------------------------------------------------
// Metadata helpers.
// ---------------------------------------------------------------------------

void CaptureLiveMetadata(ATSimulator& sim, StateMetadata& m) {
	m.cycle   = sim.GetCpuCycleCounter();
	m.pc      = sim.GetCPU().GetInsnPC();
	m.machine = HwModeStr(sim.GetHardwareMode());
	m.memory  = MemModeStr(sim.GetMemoryMode());
	m.basic   = sim.IsBASICEnabled();
}

std::string FormatMetadataFields(const StateMetadata& m) {
	std::string out;
	out.reserve(128);

	out += "\"size\":";
	out += std::to_string(m.size);
	out += ',';

	out += "\"cycle\":";
	out += std::to_string(m.cycle);
	out += ',';

	{
		char pcbuf[16];
		std::snprintf(pcbuf, sizeof pcbuf, "\"$%04x\"", (unsigned)(m.pc & 0xffff));
		out += "\"pc\":";
		out += pcbuf;
		out += ',';
	}

	out += "\"machine\":\"";
	out += JsonEscape(m.machine);
	out += "\",";

	out += "\"memory\":\"";
	out += JsonEscape(m.memory);
	out += "\",";

	out += "\"basic\":";
	out += (m.basic ? "true" : "false");
	out += ',';

	return out;
}

// ---------------------------------------------------------------------------
// Memory-buffer save.
//
// Mirrors ATSimulator::SaveState(path) exactly -- same serializer,
// same zip writer -- but targeting a VDMemoryBufferStream rather
// than a file. Output blob is byte-identical to what SaveState would
// write to disk, so slot blobs can be persisted later (or vice
// versa: a .altstate2 file can be slurped into a slot for fast
// reuse).
// ---------------------------------------------------------------------------

void SaveStateToBuffer(ATSimulator& sim,
                       std::vector<uint8_t>& outBlob,
                       StateMetadata& outMetadata) {
	CaptureLiveMetadata(sim, outMetadata);

	vdrefptr<IATSerializable> snapshot;
	vdrefptr<IATSerializable> snapshotInfo;
	sim.CreateSnapshot(~snapshot, ~snapshotInfo);

	VDMemoryBufferStream mem;
	{
		vdautoptr<IVDZipArchiveWriter> zip(VDCreateZipArchiveWriter(mem));

		{
			vdautoptr<IATSaveStateSerializer> ser(ATCreateSaveStateSerializer(L"savestate.json"));
			ser->Serialize(*zip, *snapshot);
		}
		{
			vdautoptr<IATSaveStateSerializer> ser(ATCreateSaveStateSerializer(L"savestateinfo.json"));
			ser->Serialize(*zip, *snapshotInfo);
		}

		zip->Finalize();
	}

	const auto span = mem.GetBuffer();
	outBlob.assign(span.begin(), span.end());
	outMetadata.size = (uint32_t)outBlob.size();
}

// ---------------------------------------------------------------------------
// Memory-buffer load.
//
// VDMemoryStream is read-only / random-access -- exactly what
// ATSimulator::Load(stream, ...) wants. The pair of empty path
// strings tells the loader "no on-disk origin" -- correct for an
// inline blob; the simulator does not store the origin path
// anywhere observable for savestates.
// ---------------------------------------------------------------------------

bool LoadStateFromBuffer(ATSimulator& sim,
                         const uint8_t* blob,
                         size_t blobSize) {
	if (!blob || blobSize == 0) return false;
	if (blobSize > UINT32_MAX) return false;   // VDMemoryStream uses uint32 length

	// Pass a synthetic filename so the type detector hits the
	// SaveState2 path on extension (and image.cpp's stream wrapper
	// doesn't deref a nullptr filename inside ATVFSWrapStream's
	// VDStringW assignment).
	VDMemoryStream mem(blob, (uint32_t)blobSize);
	ATImageLoadContext ctx{};
	return sim.Load(L"slot.altstate2", L"slot.altstate2",
	                mem, kATMediaWriteMode_RO, &ctx);
}

// ---------------------------------------------------------------------------
// Slot store.
// ---------------------------------------------------------------------------

bool SlotStore(const std::string& name,
               std::vector<uint8_t>&& blob,
               const StateMetadata& metadata) {
	g_slots[name] = std::make_pair(std::move(blob), metadata);
	return true;
}

const std::vector<uint8_t>* SlotFind(const std::string& name) {
	auto it = g_slots.find(name);
	if (it == g_slots.end()) return nullptr;
	return &it->second.first;
}

const StateMetadata* SlotFindMetadata(const std::string& name) {
	auto it = g_slots.find(name);
	if (it == g_slots.end()) return nullptr;
	return &it->second.second;
}

bool SlotDrop(const std::string& name) {
	return g_slots.erase(name) > 0;
}

size_t SlotDropAll() {
	const size_t n = g_slots.size();
	g_slots.clear();
	return n;
}

std::vector<SlotInfo> SlotList() {
	std::vector<SlotInfo> out;
	out.reserve(g_slots.size());
	for (const auto& kv : g_slots) {
		SlotInfo si;
		si.name     = kv.first;
		si.metadata = kv.second.second;
		out.push_back(std::move(si));
	}
	return out;   // std::map already iterates sorted-by-key
}

}  // namespace ATBridge
