// Altirra SDL3 netplay - mid-session savestate capture / apply (impl)

#include <stdafx.h>

#include "netplay_savestate.h"
#include "netplay_disk.h"
#include "netplay_input.h"
#include "protocol.h"
#include "packets.h"
#include "diskinterface.h"
#include <array>
#include <unordered_set>

#include "simulator.h"

#include <vd2/system/file.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/zip.h>
#include <vd2/system/refcount.h>

#include <at/atcore/serialization.h>

#include "gtia.h"
#include "savestateio.h"  // from Altirra/h

#include "netplay_profile.h"

#include <at/atcore/logging.h>

extern ATSimulator g_sim;
extern ATLogChannel g_ATLCNetplay;

namespace ATNetplay {

namespace {
void ValidateArchive(VDZipArchive& zip) {
	uint64 total = 0;
	std::unordered_set<std::string> names;
	for (sint32 i = 0; i < zip.GetFileCount(); ++i) {
		const auto& info = zip.GetFileInfo(i);
		if (info.mUncompressedSize > kMaxSnapshotBytes - total
			|| !names.emplace(info.mRawFileName.c_str()).second)
			throw MyError("Netplay savestate exceeds its decoded size limit or contains duplicate entries.");
		total += info.mUncompressedSize;
	}
}
}

bool CaptureSavestate(std::vector<uint8_t>& out) {
	out.clear();
	try {
		vdrefptr<IATSerializable> snapshot;
		vdrefptr<IATSerializable> snapshotInfo;
		g_sim.CreateSnapshot(~snapshot, ~snapshotInfo);
		if (!snapshot) return false;

		VDMemoryBufferStream mbs;
		VDBufferedWriteStream bws(&mbs, 65536);
		vdautoptr<IVDZipArchiveWriter> zip(VDCreateZipArchiveWriter(bws));
		{
			vdautoptr<IATSaveStateSerializer> ser(
				ATCreateSaveStateSerializer(L"savestate.json"));
			ser->Serialize(*zip, *snapshot);
		}
		// Keep frontend-held inputs alongside core state. Reuse the same
		// field encoder as gameplay packets instead of struct padding.
		const auto inputs = ATNetplayInput::CaptureAppliedState();
		std::array<uint8, 17 + kWireInputPktSize> inputBytes{};
		inputBytes[0] = 2;
		inputBytes[1] = (uint8)inputs.controllerLayout;
		for (unsigned i = 0; i < 8; ++i)
			inputBytes[4 + i] = (uint8)(inputs.rawKeyMask >> (i * 8));
		inputBytes[12] = inputs.consoleMask;
		inputBytes[13] = (uint8)(inputs.selected5200 + 1);
		inputBytes[14] = inputs.pots5200Enabled;
		inputBytes[15] = inputs.shiftHeld;
		inputBytes[16] = inputs.ctrlHeld;
		NetInputPacket inputPacket;
		inputPacket.count = 2;
		inputPacket.inputs[0] = inputs.inputs[0];
		inputPacket.inputs[1] = inputs.inputs[1];
		if (EncodeInputPacket(inputPacket, inputBytes.data() + 17,
			kWireInputPktSize) != kWireInputPktSize) return false;
		zip->BeginFile(L"netplay-input.bin", VDDeflateCompressionLevel::Quick)
			.Write(inputBytes.data(), (sint32)inputBytes.size());
		zip->EndFile();
		for (unsigned i = 0; i < 15; ++i) {
			auto* image = g_sim.GetDiskInterface(i).GetDiskImage();
			if (!CaptureDiskCheckpoint(image).image) continue;
			std::vector<uint8> overlay;
			if (!SerializeDiskOverlay(image, overlay))
				throw MyError("Netplay disk overlay exceeds the resync size limit.");
			wchar_t name[48];
			std::swprintf(name, 48, L"netplay-disk-%u.bin", i);
			auto& stream = zip->BeginFile(name, VDDeflateCompressionLevel::Quick);
			stream.Write(overlay.data(), (sint32)overlay.size());
			zip->EndFile();
		}
		zip->Finalize();
		bws.Flush();

		auto span = mbs.GetBuffer();
		if (span.size() > kMaxSnapshotBytes)
			throw MyError("Netplay savestate exceeds the transfer size limit.");
		VDMemoryStream validationStream(span.data(), (uint32)span.size());
		VDZipArchive validationZip;
		validationZip.Init(&validationStream);
		ValidateArchive(validationZip);
		out.assign(span.begin(), span.end());
		g_ATLCNetplay("resync: captured savestate (%zu bytes)", out.size());
		return !out.empty();
	} catch (const MyError& e) {
		g_ATLCNetplay("resync: capture failed: %s", e.c_str());
		out.clear();
		return false;
	} catch (...) {
		g_ATLCNetplay("resync: capture failed (unknown exception)");
		out.clear();
		return false;
	}
}

bool ApplySavestate(const uint8_t* data, size_t len) {
	if (!data || len == 0 || len > kMaxSnapshotBytes) return false;
	try {
		VDMemoryStream ms(data, (uint32_t)len);
		VDZipArchive zip;
		zip.Init(&ms);
		ValidateArchive(zip);

		ATNetplayInput::AppliedState inputs{};
		const sint32 inputEntry = zip.FindFile("netplay-input.bin");
		if (inputEntry < 0 || zip.GetFileInfo(inputEntry).mUncompressedSize
			!= 17 + kWireInputPktSize)
			throw MyError("Netplay savestate has missing or incompatible input state.");
		std::array<uint8, 17 + kWireInputPktSize> inputBytes{};
		vdautoptr inputStream(zip.OpenDecodedStream(inputEntry));
		inputStream->EnableCRC();
		inputStream->Read(inputBytes.data(), (sint32)inputBytes.size());
		inputStream->VerifyCRC();
		if (inputBytes[0] != 2 || !IsValidControllerLayout((ControllerLayout)inputBytes[1])
			|| inputBytes[2] || inputBytes[3]
			|| inputBytes[12] > 7 || inputBytes[13] > 4
			|| inputBytes[14] > 1 || inputBytes[15] > 1 || inputBytes[16] > 1)
			throw MyError("Netplay savestate contains invalid held input state.");
		inputs.controllerLayout = (ControllerLayout)inputBytes[1];
		for (unsigned i = 0; i < 8; ++i)
			inputs.rawKeyMask |= uint64(inputBytes[4 + i]) << (i * 8);
		inputs.consoleMask = inputBytes[12];
		inputs.selected5200 = int(inputBytes[13]) - 1;
		inputs.pots5200Enabled = inputBytes[14] != 0;
		inputs.shiftHeld = inputBytes[15] != 0;
		inputs.ctrlHeld = inputBytes[16] != 0;
		NetInputPacket inputPacket;
		if (DecodeInputPacket(inputBytes.data() + 17, kWireInputPktSize,
			inputPacket) != DecodeResult::Ok || inputPacket.count != 2)
			throw MyError("Netplay savestate contains invalid frame input state.");
		inputs.inputs[0] = inputPacket.inputs[0];
		inputs.inputs[1] = inputPacket.inputs[1];
		struct DiskRestore {
			vdrefptr<IATDiskImage> image;
			VDStringW path;
			ATMediaWriteMode writeMode{};
			bool persistent = false;
			DiskCheckpoint checkpoint;
		};
		std::array<DiskRestore, 15> disks;
		for (unsigned i = 0; i < disks.size(); ++i) {
			char name[48];
			std::snprintf(name, sizeof name, "netplay-disk-%u.bin", i);
			const sint32 entry = zip.FindFile(name);
			if (entry < 0) continue;
			const auto length = zip.GetFileInfo(entry).mUncompressedSize;
			if (length > kMaxDiskOverlayBytes)
				throw MyError("Netplay disk overlay exceeds the resync size limit.");
			std::vector<uint8> overlay((size_t)length);
			vdautoptr stream(zip.OpenDecodedStream(entry));
			stream->EnableCRC();
			stream->Read(overlay.data(), (sint32)overlay.size());
			stream->VerifyCRC();
			auto& source = g_sim.GetDiskInterface(i);
			auto& restore = disks[i];
			if (!CreateRollbackDisk(source.GetDiskImage(), ~restore.image)
				|| !ParseDiskOverlay(restore.image, overlay.data(), overlay.size(),
					restore.checkpoint))
				throw MyError("Netplay disk overlay is invalid or does not match the mounted image.");
			restore.path = source.GetPath();
			restore.writeMode = source.GetWriteMode();
			restore.persistent = source.IsDiskBacked();
		}

		vdautoptr<IATSaveStateDeserializer> ds(
			ATCreateSaveStateDeserializer(L"savestate.json"));
		vdrefptr<IATSerializable> root;
		ds->Deserialize(zip, ~root);
		if (!root) {
			g_ATLCNetplay("resync: deserialize returned null snapshot");
			return false;
		}
		// All sidecars and snapshot objects have been parsed before any
		// disk version is installed. Restore bytes before drive runtime.
		for (unsigned i = 0; i < disks.size(); ++i) {
			auto& restore = disks[i];
			if (!restore.image) continue;
			auto& disk = g_sim.GetDiskInterface(i);
			if (disk.GetDiskImage() != restore.image.get()) {
				disk.LoadDisk(restore.persistent ? restore.path.c_str() : nullptr,
					restore.path.c_str(), restore.image);
				disk.SetWriteMode(restore.writeMode);
			}
			if (!RestoreDiskCheckpoint(restore.checkpoint)) return false;
		}
		g_sim.GetGTIA().DiscardInFlightFrame();
		ATNetplayInput::RestoreAppliedState(inputs);
		if (!g_sim.ApplySnapshot(*root, nullptr)) {
			g_ATLCNetplay("resync: ApplySnapshot refused the payload");
			return false;
		}

		// Snapshot runtime metadata restores the captured RNG and clocks.
		// Reseeding here would change the future after every rollback/resync.
		g_ATLCNetplay("resync: applied exact savestate (%zu bytes)", len);
		return true;
	} catch (const MyError& e) {
		g_ATLCNetplay("resync: apply failed: %s", e.c_str());
		return false;
	} catch (...) {
		g_ATLCNetplay("resync: apply failed (unknown exception)");
		return false;
	}
}

} // namespace ATNetplay
