//	AltirraSDL - Tools dialog (split from ui_tools.cpp, Phase 2k)
//
// User-requested document UI extension of the Windows Disk Explorer.
// Helpers use a scoped main-thread document context; dialog results are queued
// by ui_explorer_document.h and drained only within the owning context.

#include <stdafx.h>
#include <algorithm>
#include <cctype>
#include <string>
#include <mutex>
#include <thread>
#include <tuple>
#include <vector>
#include <cstring>
#include <cstdio>
#include <imgui.h>
#include <SDL3/SDL.h>
#include "ui_file_dialog_sdl3.h"
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/error.h>
#include <vd2/system/date.h>
#include <vd2/system/registry.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/configvar.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/media.h>
#include <at/atio/image.h>
#include <at/atio/diskimage.h>
#include <at/atio/cartridgeimage.h>
#include <at/atio/cassetteimage.h>
#include <vd2/Dita/accel.h>
#include "ui_main.h"
#include "ui_explorer_document.h"
#include "accel_sdl3.h"
#include "simulator.h"
#include "gtia.h"
#include "constants.h"
#include "disk.h"
#include "diskinterface.h"
#include "firmwaremanager.h"
#include "firmwaredetect.h"
#include "compatengine.h"
#include "settings.h"
#include "uiaccessors.h"
#include "uikeyboard.h"
#include "uitypes.h"
#include "options.h"
#include "oshelper.h"
#include "ui_atascii.h"
#include "ui_diskexplorer_views.h"
#include "ui_fonts.h"

extern ATSimulator g_sim;

// =========================================================================
// Disk Explorer
// Reference: src/Altirra/source/uidiskexplorer.cpp (2,075 lines Win32)
// =========================================================================

#include <at/atio/diskfs.h>
#include <at/atio/diskimage.h>
#include <at/atio/image.h>
#include <at/atio/partitiontable.h>
#include <at/atio/partitiondiskview.h>
#include <at/atcore/blockdevice.h>
#include "diskinterface.h"

// View modes for the file content viewer
enum DiskExplorerViewMode {
	kDEView_None = -1,
	kDEView_Text = 0,      // Text: no line wrapping
	kDEView_TextWrap,       // Text: wrap to window
	kDEView_TextGR0,        // Text: wrap to a user-selected screen width
	kDEView_Hex,            // Hex dump
	kDEView_Executable,     // Executable
	kDEView_MAC65,          // MAC/65
	kDEView_AtariBasic,     // Atari BASIC tokenized source
	kDEView_SynAssembler,   // Syn assembler tokenized source
	kDEView_6502,           // 6502 disassembly
	kDEView_ASCII,          // 7-bit text interpretation
};

struct DiskExplorerState {
	vdrefptr<IATDiskImage> pImage;
	IATDiskFS *pFS = nullptr;
	bool readOnly = true;
	bool modified = false;
	bool sortDirty = true;
	ATDiskFSKey currentDir = ATDiskFSKey::None;

	// Block device / partition support
	vdrefptr<IATBlockDevice> pBlockDevice;
	vdrefptr<IATDiskImage> pPartitionView;  // holds ATPartitionDiskView while viewing a partition
	vdvector<ATPartitionInfo> partitions;
	int selectedPartition = -1;

	// Drive-mounted image tracking (for flush + emulator notification)
	ATDiskInterface *pDiskInterface = nullptr;
	bool mbAutoFlush = false;

	// Options (persist across sessions within the same run)
	bool mbStrictFilenames = true;
	bool mbAdjustFilenames = true;

	// Directory listing
	struct Entry {
		VDStringA name;
		uint32 sectors = 0;
		uint32 bytes = 0;
		ATDiskFSKey key = ATDiskFSKey::None;
		bool isDir = false;
		bool dateValid = false;
		VDExpandedDate date = {};
	};
	std::vector<Entry> entries;
	int selectedEntry = -1;
	std::vector<bool> selected;   // multi-selection (parallel to entries)
	int lastClickedEntry = -1;    // for Shift+Click range selection

	int GetSelectionCount() const {
		int n = 0;
		for (bool s : selected) if (s) ++n;
		return n;
	}
	void ClearSelection() {
		std::fill(selected.begin(), selected.end(), false);
		selectedEntry = -1;
		lastClickedEntry = -1;
	}
	void SelectSingle(int idx) {
		ClearSelection();
		if (idx >= 0 && idx < (int)selected.size()) {
			selected[idx] = true;
			selectedEntry = idx;
			lastClickedEntry = idx;
		}
	}
	void ToggleSelection(int idx) {
		if (idx >= 0 && idx < (int)selected.size()) {
			selected[idx] = !selected[idx];
			selectedEntry = selected[idx] ? idx : -1;
			lastClickedEntry = idx;
		}
	}
	void SelectRange(int from, int to) {
		if (selected.empty())
			return;

		if (from > to) std::swap(from, to);
		from = std::max(from, 0);
		to = std::min(to, (int)selected.size() - 1);
		if (from > to)
			return;

		for (int i = from; i <= to; ++i)
			selected[i] = true;
		selectedEntry = to;
		lastClickedEntry = to;
	}

	// File viewer
	DiskExplorerViewMode viewMode = kDEView_Hex;
	vdfastvector<uint8> viewData;
	VDStringA viewText;
	bool viewValid = false;

	// Native disk-inspection tools.  These are kept separate from the file
	// viewer so changing a directory or closing the image cannot leave a
	// dangling sector buffer behind.
	bool showImageProperties = false;
	bool showSectorViewer = false;
	bool showBootRecord = false;
	bool showSectorMap = false;
	bool showFileInfo = false;
	ATDiskFSKey fileInfoKey = ATDiskFSKey::None;
	uint32 sectorNumber = 1; // user-facing sector numbers are 1-based
	uint32 loadedSectorNumber = 0;
	vdfastvector<uint8> sectorData;
	VDStringA sectorError;
	ATDiskFSValidationReport validationReport;
	bool validationValid = false; // validation has been run
	bool validationProblems = false;

	// Path breadcrumb
	VDStringA pathStr;

	// Status
	VDStringA statusMsg;

	~DiskExplorerState() { delete pFS; }

	void Reset() {
		modified = false;
		delete pFS; pFS = nullptr;
		pImage = nullptr;
		pBlockDevice = nullptr;
		pPartitionView = nullptr;
		pDiskInterface = nullptr;
		mbAutoFlush = false;
		partitions.clear();
		selectedPartition = -1;
		entries.clear();
		selected.clear();
		selectedEntry = -1;
		lastClickedEntry = -1;
		currentDir = ATDiskFSKey::None;
		viewData.clear();
		viewText.clear();
		viewValid = false;
		showImageProperties = false;
		showSectorViewer = false;
		showBootRecord = false;
		showSectorMap = false;
		showFileInfo = false;
		fileInfoKey = ATDiskFSKey::None;
		sectorNumber = 1;
		loadedSectorNumber = 0;
		sectorData.clear();
		sectorError.clear();
		validationValid = false;
		validationProblems = false;
		pathStr = "/";
		statusMsg.clear();
	}

	IATDiskImage *GetActiveImage() const {
		return pImage ? pImage.get() : pPartitionView.get();
	}

	bool LoadSector(uint32 oneBasedSector) {
		sectorData.clear();
		sectorError.clear();
		loadedSectorNumber = 0;
		IATDiskImage *image = GetActiveImage();
		if (!image) {
			sectorError = "No disk image is open.";
			return false;
		}
		if (!oneBasedSector || oneBasedSector > image->GetVirtualSectorCount()) {
			sectorError = "The selected sector is outside the image.";
			return false;
		}
		try {
			const uint32 sectorSize = image->GetSectorSize(oneBasedSector - 1);
			if (!sectorSize || sectorSize > 1024 * 1024) {
				sectorError = "The image returned an unsupported sector size.";
				return false;
			}
			sectorData.resize(sectorSize);
			const uint32 actual = image->ReadVirtualSector(oneBasedSector - 1, sectorData.data(), sectorSize);
			if (actual != sectorSize) {
				sectorData.clear();
				sectorError.sprintf("Short sector read (%u of %u bytes).", actual, sectorSize);
				return false;
			}
			loadedSectorNumber = oneBasedSector;
			return true;
		} catch (const MyError& e) {
			sectorError.sprintf("Sector read failed: %s", e.c_str());
			return false;
		}
	}

	void RefreshDirectory() {
		sortDirty = true;
		entries.clear();
		selected.clear();
		selectedEntry = -1;
		lastClickedEntry = -1;
		viewValid = false;

		if (!pFS) return;

		// Add ".." entry if not at root, or if at root of a partition (to go back to partition list)
		if (currentDir != ATDiskFSKey::None || pBlockDevice) {
			Entry &up = entries.emplace_back();
			up.name = "..";
			up.isDir = true;
			if (currentDir != ATDiskFSKey::None)
				up.key = pFS->GetParentDirectory(currentDir);
			else
				up.key = ATDiskFSKey::None;
		}

		ATDiskFSEntryInfo einfo;
		ATDiskFSFindHandle h = pFS->FindFirst(currentDir, einfo);
		if (h != ATDiskFSFindHandle::Invalid) {
			do {
				Entry &e = entries.emplace_back();
				e.name = einfo.mFileName;
				e.sectors = einfo.mSectors;
				e.bytes = einfo.mBytes;
				e.key = einfo.mKey;
				e.isDir = einfo.mbIsDirectory;
				e.dateValid = einfo.mbDateValid;
				e.date = einfo.mDate;
			} while (pFS->FindNext(h, einfo));
			pFS->FindEnd(h);
		}

		// Sort: directories first, then alphabetical
		int sortStart = (currentDir != ATDiskFSKey::None || pBlockDevice) ? 1 : 0;
		std::sort(entries.begin() + sortStart, entries.end(),
			[](const Entry &a, const Entry &b) {
				if (a.isDir != b.isDir) return a.isDir > b.isDir;
				return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
			});

		selected.resize(entries.size(), false);

		// Update status — matches Windows format
		ATDiskFSInfo fsinfo;
		pFS->GetInfo(fsinfo);
		statusMsg.sprintf("Mounted %s file system%s. %u block%s (%uKB) free",
			fsinfo.mFSType.c_str(),
			pFS->IsReadOnly() ? " (read-only)" : "",
			fsinfo.mFreeBlocks,
			fsinfo.mFreeBlocks != 1 ? "s" : "",
			(fsinfo.mFreeBlocks * fsinfo.mBlockSize) >> 10);
	}

	void RefreshPartitions() {
		partitions.clear();
		selectedPartition = -1;
		entries.clear();
		selected.clear();
		selectedEntry = -1;
		lastClickedEntry = -1;
		viewValid = false;

		if (!pBlockDevice) return;

		try {
			ATDecodePartitionTable(*pBlockDevice, partitions);
		} catch (const MyError &e) {
			statusMsg.sprintf("Failed to decode partition table: %s", e.c_str());
			return;
		}

		statusMsg = "Mounted block device.";
		pathStr = "/";
	}

	void OpenPartition(int partIdx) {
		if (partIdx < 0 || partIdx >= (int)partitions.size())
			return;

		const ATPartitionInfo &pi = partitions[partIdx];
		vdrefptr<ATPartitionDiskView> pdview(new ATPartitionDiskView(*pBlockDevice, pi));

		// Windows always opens partitions read-only initially (MountFS write=false).
		// The user can toggle Write to enable writes if the partition is updatable.
		vdautoptr<IATDiskFS> fs(ATDiskMountImage(pdview, true));

		if (!fs)
			throw MyError("Unable to detect the file system on the partition.");

		pPartitionView = pdview.get();
		pImage = nullptr;  // not a standalone image
		delete pFS;
		pFS = fs.release();
		pFS->SetStrictNameChecking(mbStrictFilenames);
		readOnly = true;
		currentDir = ATDiskFSKey::None;

		RefreshDirectory();

		VDStringA name = VDTextWToA(pi.mName);
		pathStr.sprintf("/%s/", name.c_str());
	}

	void ReturnToPartitionList() {
		delete pFS;
		pFS = nullptr;
		pImage = nullptr;
		pPartitionView = nullptr;
		entries.clear();
		selected.clear();
		selectedEntry = -1;
		lastClickedEntry = -1;
		viewData.clear();
		viewText.clear();
		viewValid = false;

		RefreshPartitions();
	}

	// Centralized post-modification handler — mirrors Windows OnFSModified().
	// Flushes filesystem, auto-flushes image if configured, notifies emulator.
	void OnFSModified() {
		modified = true;
		try {
			if (pFS)
				pFS->Flush();

			if (mbAutoFlush && pImage)
				pImage->Flush();
		} catch (const MyError &e) {
			statusMsg.sprintf("Flush failed: %s", e.c_str());
		}

		if (pDiskInterface)
			pDiskInterface->OnDiskChanged(true);

		RefreshDirectory();
	}

	// Validate filesystem health before allowing writes — matches Windows ValidateForWrites().
	void ValidateForWrites() {
		if (!pFS) return;

		ATDiskFSValidationReport report;
		if (!pFS->Validate(report)) {
			if (!report.IsSerious() && report.mbBitmapIncorrectLostSectorsOnly) {
				statusMsg = "Warning: allocation bitmap has minor issues (lost sectors only).";
			} else {
				pFS->SetReadOnly(true);
				readOnly = true;
				if (report.mbBrokenFiles || report.mbOpenWriteFiles)
					statusMsg = "Filesystem is damaged -- mounted read-only to prevent further damage.";
				else
					statusMsg = "Allocation bitmap is incorrect -- mounted read-only as a precaution.";
			}
		}
	}

	void NavigateTo(ATDiskFSKey key) {
		currentDir = key;
		RefreshDirectory();

		// Rebuild path string (simplified - just show current level)
		if (key == ATDiskFSKey::None)
			pathStr = "/";
		else {
			ATDiskFSEntryInfo info;
			pFS->GetFileInfo(key, info);
			pathStr.sprintf("/%s/", info.mFileName.c_str());
		}
	}

	void LoadFileView(int entryIdx) {
		viewValid = false;
		viewData.clear();
		viewText.clear();
		selectedEntry = entryIdx;
		if (entryIdx < 0 || entryIdx >= (int)entries.size()) return;

		auto &e = entries[entryIdx];
		if (e.isDir) return;

		try {
			pFS->ReadFile(e.key, viewData);
			FormatView();
			viewValid = true;
		} catch (const MyError &err) {
			viewText.sprintf("Error reading file: %s", err.c_str());
			viewValid = true;
		}
	}

	void FormatView() {
		viewText.clear();
		if (viewData.empty()) return;

		switch (viewMode) {
		case kDEView_Text:
		case kDEView_TextWrap:
		case kDEView_TextGR0: {
			// ATASCII to UTF-8 conversion with full character mapping.
			// Reference: uidiskexplorer.cpp lines 252-356
			static const uint16 kLowTable[32] = {
				0x2665, // 0x00 heart
				0x251C, // 0x01 vertical tee right
				0x2595, // 0x02 vertical bar right
				0x2518, // 0x03 top-left elbow
				0x2524, // 0x04 vertical tee left
				0x2510, // 0x05 bottom-left elbow
				0x2571, // 0x06 forward diagonal
				0x2572, // 0x07 backwards diagonal
				0x25E2, // 0x08 lower right filled triangle
				0x2597, // 0x09 lower right quadrant
				0x25E3, // 0x0A lower left filled triangle
				0x259D, // 0x0B quadrant upper right
				0x2598, // 0x0C quadrant upper left
				0x2594, // 0x0D top quarter
				0x2582, // 0x0E bottom quarter
				0x2596, // 0x0F lower left quadrant
				0x2663, // 0x10 club
				0x250C, // 0x11 lower-right elbow
				0x2500, // 0x12 horizontal bar
				0x253C, // 0x13 four-way
				0x2022, // 0x14 filled circle
				0x2584, // 0x15 lower half
				0x258E, // 0x16 left quarter
				0x252C, // 0x17 horizontal tee down
				0x2534, // 0x18 horizontal tee up
				0x258C, // 0x19 left side
				0x2514, // 0x1A top-right elbow
				0x241B, // 0x1B escape symbol
				0x2191, // 0x1C up arrow
				0x2193, // 0x1D down arrow
				0x2190, // 0x1E left arrow
				0x2192, // 0x1F right arrow
			};

			// Helper to append a Unicode codepoint as UTF-8
			auto appendUTF8 = [&](uint16 cp) {
				if (cp < 0x80) {
					viewText += (char)cp;
				} else if (cp < 0x800) {
					viewText += (char)(0xC0 | (cp >> 6));
					viewText += (char)(0x80 | (cp & 0x3F));
				} else {
					viewText += (char)(0xE0 | (cp >> 12));
					viewText += (char)(0x80 | ((cp >> 6) & 0x3F));
					viewText += (char)(0x80 | (cp & 0x3F));
				}
			};

			const int lineWidth = (viewMode == kDEView_TextGR0) ? ATUIGetTextColumns() : INT_MAX;
			int col = 0;

			viewText.reserve(viewData.size() * 2);
			for (uint8 raw : viewData) {
				if (raw == 0x9B) {
					viewText += '\n';
					col = 0;
					continue;
				}

				// Wrap at column limit for GR.0 mode
				if (col >= lineWidth) {
					viewText += '\n';
					col = 0;
				}

				// Strip inverse video bit — the character shape is the same
				uint8 c = raw & 0x7F;

				if (c < 0x20) {
					appendUTF8(kLowTable[c]);
				} else if (c == 0x60) {
					appendUTF8(0x2666); // black diamond suit
				} else if (c == 0x7B) {
					appendUTF8(0x2660); // spade
				} else if (c == 0x7C) {
					viewText += '|';
				} else if (c == 0x7D) {
					appendUTF8(0x21B0); // curved arrow up-left
				} else if (c == 0x7E) {
					appendUTF8(0x25C0); // left-pointing triangle
				} else if (c == 0x7F) {
					appendUTF8(0x25B6); // right-pointing triangle
				} else {
					viewText += (char)c;
				}
				++col;
			}
			break;
		}

		case kDEView_Hex: {
			const uint8 *data = viewData.data();
			uint32 len = (uint32)viewData.size();
			for (uint32 i = 0; i < len; i += 16) {
				VDStringA line;
				line.sprintf("%06X: ", i);
				for (uint32 j = 0; j < 16; ++j) {
					if (i + j < len)
						line.append_sprintf("%02X ", data[i + j]);
					else
						line += "   ";
					if (j == 7) line += " ";
				}
				line += " |";
				for (uint32 j = 0; j < 16 && i + j < len; ++j) {
					uint8 c = data[i + j];
					line += (c >= 0x20 && c < 0x7F) ? (char)c : '.';
				}
				line += "|\n";
				viewText += line;
			}
			break;
		}

		case kDEView_Executable: {
			const uint8 *data = viewData.data();
			uint32 len = (uint32)viewData.size();
			uint32 pos = 0;

			if (len >= 2 && data[0] == 0xFF && data[1] == 0xFF) {
				pos = 2;
				viewText += "Atari executable file\n\n";
			} else {
				viewText += "Not a standard Atari executable (no $FFFF header)\n\n";
			}

			int segIdx = 0;
			while (pos + 3 < len) {
				// Check for optional $FFFF header between segments
				if (data[pos] == 0xFF && data[pos + 1] == 0xFF)
					pos += 2;
				if (pos + 3 >= len) break;

				uint16 startAddr = data[pos] | (data[pos + 1] << 8);
				uint16 endAddr = data[pos + 2] | (data[pos + 3] << 8);
				pos += 4;

				if (endAddr < startAddr) {
					viewText.append_sprintf("Segment %d: $%04X-$%04X (invalid range)\n", segIdx++, startAddr, endAddr);
					break;
				}

				uint32 segLen = (uint32)(endAddr - startAddr + 1);
				VDStringA line;
				line.sprintf("Segment %d: $%04X-$%04X (%u bytes)", segIdx++, startAddr, endAddr, segLen);

				if (startAddr == 0x02E0 && endAddr == 0x02E1 && pos + 2 <= len) {
					uint16 runAddr = data[pos] | (data[pos + 1] << 8);
					line.append_sprintf("  [RUN $%04X]", runAddr);
				} else if (startAddr == 0x02E2 && endAddr == 0x02E3 && pos + 2 <= len) {
					uint16 initAddr = data[pos] | (data[pos + 1] << 8);
					line.append_sprintf("  [INIT $%04X]", initAddr);
				}

				viewText += line;
				viewText += '\n';
				if (pos + segLen > len) {
					viewText.append_sprintf("  (truncated: %u bytes remaining, segment needs %u)\n",
						len - pos, segLen);
					break;
				}
				pos += segLen;
			}
			break;
		}

		case kDEView_MAC65: {
			// MAC/65 tokenized source decompilation
			// Reference: uidiskexplorer.cpp DecodeMAC65() lines 381-729
			const uint8 *data = viewData.data();
			uint32 len = (uint32)viewData.size();

			if (len < 4 || data[0] != 0xFE || data[1] != 0xFE) {
				viewText = "[Invalid MAC/65 source file]\n";
				break;
			}

			uint32 maxLen = data[2] | (data[3] << 8);
			if (len > maxLen) len = maxLen;
			const uint8 *src = data + 4;
			len -= 4;

			// Statement token table (index 0-95)
			static const char *kStmtTokens[] = {
				"ERROR -", ".IF", ".ELSE", ".ENDIF", ".MACRO", ".ENDM", ".TITLE",
				"", ".PAGE", ".WORD", ".ERROR", ".BYTE", ".SBYTE", ".DBYTE",
				".END", ".OPT", ".TAB", ".INCLUDE", ".DS", ".ORG", ".EQU",
				"BRA", "TRB", "TSB", ".FLOAT", ".CBYTE", ";", ".LOCAL",
				".SET", "*=", "=", ".=",
				"JSR", "JMP", "DEC", "INC", "LDX", "LDY", "STX", "STY",
				"CPX", "CPY", "BIT", "BRK", "CLC", "CLD", "CLI", "CLV",
				"DEX", "DEY", "INX", "INY", "NOP", "PHA", "PHP", "PLA",
				"PLP", "RTI", "RTS", "SEC", "SED", "SEI", "TAX", "TAY",
				"TSX", "TXA", "TXS", "TYA", "BCC", "BCS", "BEQ", "BMI",
				"BNE", "BPL", "BVC", "BVS", "ORA", "AND", "EOR", "ADC",
				"STA", "LDA", "CMP", "SBC", "ASL", "ROL", "LSR", "ROR",
				nullptr, "STZ", "DEA", "INA", "PHX", "PHY", "PLX", "PLY",
			};

			while (len >= 3) {
				uint16 lineNo = src[0] | (src[1] << 8);
				uint32 lineLen = src[2];
				if (lineLen < 3 || lineLen > len) {
					viewText += "[Invalid line header]\n";
					break;
				}

				// Format line number
				if (lineNo == 0) viewText += "0";
				else if (lineNo < 100) viewText.append_sprintf("%02u", lineNo);
				else if (lineNo < 10000) viewText.append_sprintf("%04u", lineNo);
				else viewText.append_sprintf("%06u", lineNo);
				viewText += ' ';

				uint32 offset = 3;
				bool stmt = true;
				bool comment = false;
				int xout = (lineNo == 0 ? 2 : lineNo < 100 ? 3 : lineNo < 10000 ? 5 : 7);

				while (offset < lineLen) {
					uint8 c = src[offset++];

					// Label/identifier token (>= 0x80) or comment continuation
					if (c >= 0x80 || comment) {
						uint32 idLen = comment ? lineLen - offset : c - 0x80;
						if (comment) --offset;
						if (lineLen - offset < idLen) break;
						for (uint32 j = 0; j < idLen; ++j) {
							uint8 ch = src[offset++];
							if (ch >= 0x20 && ch < 0x7F)
								viewText += (char)ch;
							else
								viewText.append_sprintf("<$%02X>", ch);
						}
						xout += idLen;
						continue;
					}

					const char *token = nullptr;

					if (stmt) {
						if (c == 88) { comment = true; continue; }  // comment line

						// Indent to column 10
						while (xout < 9) { viewText += ' '; ++xout; }
						if (viewText.empty() || viewText.back() != ' ') viewText += ' ';

						if (c < 96) token = kStmtTokens[c];
					} else {
						// Expression tokens
						if (c == 5 && lineLen - offset >= 2) {
							uint16 num = src[offset] | (src[offset + 1] << 8);
							offset += 2;
							viewText.append_sprintf("$%04X", num);
							xout += 5;
						} else if (c == 6 && lineLen - offset >= 1) {
							viewText.append_sprintf("$%02X", src[offset++]);
							xout += 3;
						} else if (c == 7 && lineLen - offset >= 2) {
							uint16 num = src[offset] | (src[offset + 1] << 8);
							offset += 2;
							viewText.append_sprintf("%u", num);
							xout += (num >= 10000 ? 5 : num >= 1000 ? 4 : num >= 100 ? 3 : num >= 10 ? 2 : 1);
						} else if (c == 8 && lineLen - offset >= 1) {
							uint8 num = src[offset++];
							viewText.append_sprintf("%u", num);
							xout += (num >= 100 ? 3 : num >= 10 ? 2 : 1);
						} else if (c == 10 && lineLen - offset >= 1) {
							uint8 ch = src[offset++];
							if (ch >= 0x20 && ch < 0x7F) viewText += (char)ch; else viewText.append_sprintf("<$%02X>", ch);
							++xout;
						} else if (c == 59) {
							comment = true; token = "";
						} else {
							// Expression operator tokens
							switch (c) {
								case 11: token = "%$"; break;  case 12: token = "%"; break;
								case 13: token = "*"; break;   case 18: token = "+"; break;
								case 19: token = "-"; break;   case 20: token = "*"; break;
								case 21: token = "/"; break;   case 22: token = "&"; break;
								case 24: token = "="; break;   case 25: token = "<="; break;
								case 26: token = ">="; break;  case 27: token = "<>"; break;
								case 28: token = ">"; break;   case 29: token = "<"; break;
								case 30: token = "-"; break;   case 31: token = "["; break;
								case 32: token = "]"; break;   case 36: token = "!"; break;
								case 37: token = "%"; break;   case 39: token = "\\"; break;
								case 47: token = ".REF"; break; case 48: token = ".DEF"; break;
								case 49: token = ".NOT"; break; case 50: token = ".AND"; break;
								case 51: token = ".OR"; break;  case 52: token = "<"; break;
								case 53: token = ">"; break;   case 54: token = ",X)"; break;
								case 55: token = "),Y"; break;  case 56: token = ",Y"; break;
								case 57: token = ",X"; break;   case 58: token = ")"; break;
								case 61: token = ","; break;    case 62: token = "#"; break;
								case 63: token = "A"; break;    case 64: token = "("; break;
								case 65: token = "\""; break;   case 69: token = "NO"; break;
								case 70: token = "OBJ"; break;  case 71: token = "ERR"; break;
								case 72: token = "EJECT"; break; case 73: token = "LIST"; break;
								case 74: token = "XREF"; break; case 75: token = "MLIST"; break;
								case 76: token = "CLIST"; break; case 77: token = "NUM"; break;
								default: break;
							}
						}
					}

					if (token) {
						viewText += token;
						xout += (int)strlen(token);

						if (comment) {
							while (xout < 21) { viewText += ' '; ++xout; }
							if (!viewText.empty() && viewText.back() != ' ') { viewText += "  "; xout += 2; }
						}
					}

					if (stmt) {
						stmt = false;
						viewText += ' ';
						++xout;
					}
				}

				viewText += '\n';
				src += lineLen;
				len -= lineLen;
			}
			break;
		}

		case kDEView_AtariBasic:
		case kDEView_SynAssembler:
		case kDEView_6502: {
			VDStringA error;
			bool ok = false;
			if (viewMode == kDEView_AtariBasic)
				ok = ATUIDecodeAtariBasic(viewData.data(), viewData.size(), viewText, error);
			else if (viewMode == kDEView_SynAssembler)
				ok = ATUIDecodeSynAssembler(viewData.data(), viewData.size(), viewText, error);
			else
				ok = ATUIDisassemble6502(viewData.data(), viewData.size(), 0, viewText, error);
			if (!ok) {
				viewText = "[Viewer could not decode this file]\n";
				viewText += error;
				viewText += '\n';
			}
			break;
		}

		default:
			viewText = "(unknown view mode)";
			break;
		}
	}
};

struct DiskExportInfo {
	ATDiskFSKey key;
	bool dateValid;
	VDExpandedDate date;
};
struct DiskMultiExportEntry {
	ATDiskFSKey key;
	VDStringA name;
	bool dateValid;
	VDExpandedDate date;
};

struct DiskExplorerDocument : ATUIExplorerDocument {
	DiskExplorerState state;
	bool closeRequested = false;
	bool actionsRequested = false;
	char newDirName[64] = {};
	bool wantRename = false;
	bool wantNewDir = false;
	ATDiskFSKey renameKey = ATDiskFSKey::None;
	char renameBuf[64] = {};

	std::string diskExplorerPendingImport;

	DiskExportInfo diskExportInfo;
	std::string diskExplorerPendingExport;
	std::vector<DiskMultiExportEntry> diskMultiExportEntries;
	std::string diskExplorerPendingMultiExport;
	bool diskMultiExportAsText = false;
	std::string diskExplorerPendingImportText;
	std::string diskExplorerPendingExportText;
	DiskExportInfo diskExportTextInfo;
	int diskExplorerPartitionImportIdx = -1;
	std::string diskExplorerPendingPartitionImport;
	int diskExplorerPartitionExportIdx = -1;
	std::string diskExplorerPendingPartitionExport;
	bool diskExplorerSettingsLoaded = false;
};
static std::vector<std::shared_ptr<DiskExplorerDocument>> documents;
static DiskExplorerDocument *currentDocument = nullptr;
static ATUIExplorerDocument openRequest;
static std::vector<std::string> pendingDocuments;
static void QueueOpen(void *, const char * const *paths, int) {
	if (paths) for (; *paths; ++paths) pendingDocuments.emplace_back(*paths);
}
// Dialog results are dispatched on the main thread by the document mailbox.

static void DiskExplorerOpenCallback(void *u, const char * const *paths, int f) {
	QueueOpen(u, paths, f);
}

static void DiskExplorerLoadSettings();

static void DiskExplorerDoOpen(const char *utf8path) {
	const std::string source = ATUIExplorerFullPath(utf8path);
	for (auto& doc : documents) {
		if (doc->open && doc->source == source) { doc->focus = true; return; }
	}
	auto document = std::make_shared<DiskExplorerDocument>();
	document->source = source;
	documents.push_back(document);
	ATUIExplorerScope<DiskExplorerDocument> scope(currentDocument, document.get());
	DiskExplorerLoadSettings();
	try {
		VDStringW wpath = VDTextU8ToW(utf8path, -1);
		currentDocument->state.Reset();

		IATDiskImage *pImageRaw = nullptr;
		ATLoadDiskImage(wpath.c_str(), &pImageRaw);
		currentDocument->state.pImage = pImageRaw;
		if (pImageRaw) pImageRaw->Release();

		currentDocument->state.pFS = ATDiskMountImage(currentDocument->state.pImage, true);
		if (!currentDocument->state.pFS) {
			currentDocument->state.statusMsg = "Unable to detect filesystem on disk image.";
			return;
		}
		currentDocument->state.pFS->SetStrictNameChecking(currentDocument->state.mbStrictFilenames);
		currentDocument->state.readOnly = true;
		currentDocument->state.NavigateTo(ATDiskFSKey::None);
	} catch (const MyError &e) {
		currentDocument->state.statusMsg.sprintf("Failed to open: %s", e.c_str());
	}
}

// Open disk explorer for a mounted drive's disk image (called from disk context menu).
void ATUIOpenDiskExplorerForDrive(int driveIdx, bool writable, bool autoFlush) {
	ATDiskInterface& di = g_sim.GetDiskInterface(driveIdx);
	IATDiskImage *img = di.GetDiskImage();
	if (!img)
		return;

	for (const auto& doc : documents) {
		if (doc->open && doc->state.pImage == img) { doc->focus = true; return; }
	}
	auto document = std::make_shared<DiskExplorerDocument>();
	document->source = std::string("D") + std::to_string(driveIdx + 1) + ": " + (di.GetPath() ? VDTextWToU8(di.GetPath(), -1).c_str() : "Mounted disk");
	documents.push_back(document);
	ATUIExplorerScope<DiskExplorerDocument> scope(currentDocument, document.get());
	DiskExplorerLoadSettings();

	currentDocument->state.Reset();
	currentDocument->state.pImage = img;
	currentDocument->state.pDiskInterface = &di;
	currentDocument->state.mbAutoFlush = autoFlush;

	bool readOnly = !writable;
	currentDocument->state.pFS = ATDiskMountImage(currentDocument->state.pImage, readOnly);
	if (!currentDocument->state.pFS) {
		currentDocument->state.statusMsg = "Unable to detect filesystem on disk image.";
		return;
	}
	currentDocument->state.pFS->SetStrictNameChecking(currentDocument->state.mbStrictFilenames);
	currentDocument->state.readOnly = readOnly;

	// Validate filesystem before allowing writes (matches Windows MountFS)
	if (writable) {
		if (img->IsUpdatable() && currentDocument->state.pFS->IsReadOnly()) {
			currentDocument->state.readOnly = true;
			currentDocument->state.statusMsg = "This disk format is only supported in read-only mode.";
		} else {
			currentDocument->state.ValidateForWrites();
		}
	}

	currentDocument->state.NavigateTo(ATDiskFSKey::None);
}

// Open disk explorer for a block device (shows partition list).
void ATUIOpenDiskExplorerForBlockDevice(IATBlockDevice *dev) {
	if (!dev)
		return;

	for (const auto& doc : documents) {
		if (doc->open && doc->state.pBlockDevice == dev) { doc->focus = true; return; }
	}
	auto document = std::make_shared<DiskExplorerDocument>();
	document->source = "Block device";
	documents.push_back(document);
	ATUIExplorerScope<DiskExplorerDocument> scope(currentDocument, document.get());
	DiskExplorerLoadSettings();

	currentDocument->state.Reset();
	currentDocument->state.pBlockDevice = dev;
	currentDocument->state.readOnly = dev->IsReadOnly();
	currentDocument->state.RefreshPartitions();
}

// Convert host line endings (CR/LF, LF, CR) to Atari EOL (0x9B) in place.
// Reference: uidiskexplorer.cpp lines ~1273-1290
static void ConvertHostToAtari(vdfastvector<uint8>& data) {
	const uint8 *src = data.data();
	const uint8 *end = src + data.size();
	uint8 *dst = data.data();

	while (src != end) {
		uint8 c = *src++;
		if (c == 0x0D) {
			if (src != end && *src == 0x0A)
				++src;  // skip LF after CR
			c = 0x9B;
		} else if (c == 0x0A) {
			c = 0x9B;
		}
		*dst++ = c;
	}

	data.resize((uint32)(dst - data.data()));
}

// Convert Atari EOL (0x9B) to host CR/LF. Expands buffer as needed.
// Reference: uidiskexplorer.cpp lines ~1355-1380
static void ConvertAtariToHost(vdfastvector<uint8>& data) {
	// Count 0x9B occurrences to know how much to expand
	uint32 count = 0;
	for (uint8 c : data) {
		if (c == 0x9B)
			++count;
	}
	if (!count) return;

	uint32 oldSize = (uint32)data.size();
	uint32 newSize = oldSize + count;  // each 0x9B becomes two bytes (CR+LF)
	data.resize(newSize);

	// Work backwards to avoid overwriting source
	const uint8 *src = data.data() + oldSize;
	uint8 *dst = data.data() + newSize;
	while (dst != data.data()) {
		uint8 c = *--src;
		if (c == 0x9B) {
			*--dst = 0x0A;
			c = 0x0D;
		}
		*--dst = c;
	}
}

// Normalize a host filename to 8.3 DOS format.
// Reference: uidiskexplorer.cpp WriteFile() lines 1924-2037
static void NormalizeDOSFilename(const char *src, char *dst, int &nameLen) {
	bool strict = currentDocument->state.mbStrictFilenames;
	int sectionLen = 0;
	int sectionLimit = 8;
	bool inExt = false;
	char *out = dst;

	for (const char *s = src; *s; ++s) {
		char c = *s;
		if (c == '.') {
			if (inExt) break;  // only one dot allowed
			inExt = true;
			nameLen = sectionLen;
			sectionLen = 0;
			sectionLimit = 3;
			*out++ = '.';
		} else if (sectionLen < sectionLimit) {
			if (c >= 'a' && c <= 'z')
				c &= 0xDF;  // uppercase

			if (c >= '0' && c <= '9') {
				// In strict mode, first char of name (not ext) must be alpha
				if (!inExt && !sectionLen && strict) {
					*out++ = 'X';
					++sectionLen;
				}
			} else if (c < 'A' || c > 'Z') {
				if (strict || (c != '@' && c != '_'))
					continue;  // strip invalid chars
			}

			if (sectionLen < sectionLimit) {
				*out++ = c;
				++sectionLen;
			}
		}
	}
	if (!inExt)
		nameLen = sectionLen;
	*out = 0;
}

// Write file with auto-rename on collision (up to 100 attempts).
// Mirrors Windows uidiskexplorer.cpp WriteFile() logic.
static void DiskExplorerWriteFile(const char *origFilename, const void *data, uint32 len,
	const VDExpandedDate *date) {
	char fnbuf[13];
	int nameLen = 0;
	const char *filename = origFilename;
	int pass = 0;

	for (;;) {
		try {
			auto fileKey = currentDocument->state.pFS->WriteFile(
				currentDocument->state.currentDir, filename, data, len);
			if (date)
				currentDocument->state.pFS->SetFileTimestamp(fileKey, *date);
			return;
		} catch (const ATDiskFSException &e) {
			if (e.GetErrorCode() != kATDiskFSError_InvalidFileName &&
				e.GetErrorCode() != kATDiskFSError_FileExists)
				throw;
			if (!currentDocument->state.mbAdjustFilenames)
				throw;
			if (++pass >= 100)
				throw;

			if (pass == 1) {
				// First retry: normalize filename to 8.3 DOS format
				NormalizeDOSFilename(origFilename, fnbuf, nameLen);
				filename = fnbuf;
			} else {
				// Subsequent retries: increment numeric suffix
				int pos = nameLen - 1;
				bool incOK = false;
				while (pos >= 0) {
					char c = fnbuf[pos];
					if (c >= '0' && c <= '8') { ++fnbuf[pos]; incOK = true; break; }
					else if (c == '9') { fnbuf[pos] = '0'; --pos; }
					else break;
				}
				if (incOK) continue;

				// No room to increment — try to add another digit
				if (nameLen >= 8) {
					if (pos < 4) throw;
					fnbuf[pos] = '1';
					continue;
				}

				// In strict mode, if pos < 0 (at start), prepend 'X' first
				if (pos < 0 && currentDocument->state.mbStrictFilenames) {
					memmove(fnbuf + 1, fnbuf, 12);
					fnbuf[0] = 'X';
					++nameLen;
				}

				memmove(fnbuf + pos + 2, fnbuf + pos + 1, 13 - (pos + 2));
				fnbuf[pos + 1] = '1';
				++nameLen;
			}
			continue;
		}
		break;
	}
}

// Last known window rect of the Disk Explorer (updated each frame during rendering).

// Handle a file drop into the Disk Explorer — returns true if the drop was
// consumed (the cursor is over the explorer window, or the dropped path is a
// recognized disk image while the explorer is open).  Disk images open in the
// explorer; ordinary files retain the existing import-to-disk behavior.
static bool DiskExplorerPathLooksLikeImage(const char *utf8path) {
	if (!utf8path || !*utf8path)
		return false;

	const char *base = utf8path;
	if (const char *slash = strrchr(base, '/'))
		base = slash + 1;
	if (const char *backslash = strrchr(base, '\\'))
		base = backslash + 1;
	const char *dot = strrchr(base, '.');
	if (!dot || dot == base)
		return false;

	std::string extension(dot);
	for (char& c : extension)
		c = (char)std::tolower((unsigned char)c);
	return extension == ".atr" || extension == ".xfd" || extension == ".dcm"
		|| extension == ".pro" || extension == ".atx" || extension == ".gz"
		|| extension == ".zip" || extension == ".atz";
}

bool ATUIDiskExplorerHandleDrop(const char *utf8path, float dropX, float dropY) {
	if (!utf8path) return false;
	std::shared_ptr<DiskExplorerDocument> target;
	for (const auto& doc : documents) if (doc->Hit(dropX, dropY)) target = doc;
	if (!target) return false;
	if (target->dialogPending) return true;
	ATUIExplorerScope<DiskExplorerDocument> scope(currentDocument, target.get());

	// A disk image dropped onto the explorer must never fall through to the
	// application's global boot handler, even when the currently displayed
	// filesystem is read-only or has not been mounted yet.
	if (DiskExplorerPathLooksLikeImage(utf8path)) {
		DiskExplorerDoOpen(utf8path);
		return true;
	}

	if (!currentDocument->state.pFS || currentDocument->state.readOnly) {
		currentDocument->state.statusMsg = "Drop an Atari disk image here to open it, or use a writable disk to import files.";
		return true;
	}

	try {
		VDStringW wpath = VDTextU8ToW(utf8path, -1);

		VDFile f(wpath.c_str());
		sint64 size = f.size();
		if (size > 16 * 1024 * 1024)
			throw MyError("File too large (max 16MB).");

		vdfastvector<uint8> buf((uint32)size);
		f.read(buf.data(), (long)buf.size());

		const VDDate creationTime = f.getCreationTime();
		f.closeNT();

		VDExpandedDate expDate{};
		const VDExpandedDate *pDate = nullptr;
		if (creationTime != VDDate{}) {
			expDate = VDGetLocalDate(creationTime);
			pDate = &expDate;
		}

		VDStringA filename = VDTextWToA(VDFileSplitPathRightSpan(wpath));
		DiskExplorerWriteFile(filename.c_str(), buf.data(), (uint32)buf.size(), pDate);
		currentDocument->state.OnFSModified();
	} catch (const MyError &e) {
		currentDocument->state.statusMsg.sprintf("Drop import failed: %s", e.c_str());
	}
	return true;
}

bool ATUIDiskExplorerGetDropRect(ImVec2 &pos, ImVec2 &size, float x, float y) {
	for (const auto& doc : documents)
		if (doc->Hit(x, y, &pos, &size)) return true;
	return false;
}

static void DiskExplorerImportCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;

	currentDocument->diskExplorerPendingImport = filelist[0];
}

static void DiskExplorerDoImport(const char *utf8path) {
	if (!currentDocument->state.pFS) return;

	try {
		VDStringW wpath = VDTextU8ToW(utf8path, -1);

		// Read host file
		VDFile f(wpath.c_str());
		sint64 size = f.size();
		if (size > 16 * 1024 * 1024)
			throw MyError("File too large (max 16MB).");

		vdfastvector<uint8> buf((uint32)size);
		f.read(buf.data(), (long)buf.size());

		// Preserve host file creation time (matches Windows uidiskexplorer.cpp)
		const VDDate creationTime = f.getCreationTime();
		f.closeNT();

		VDExpandedDate expDate{};
		const VDExpandedDate *pDate = nullptr;
		if (creationTime != VDDate{}) {
			expDate = VDGetLocalDate(creationTime);
			pDate = &expDate;
		}

		// Extract filename from path
		VDStringA filename = VDTextWToA(VDFileSplitPathRightSpan(wpath));

		DiskExplorerWriteFile(filename.c_str(), buf.data(), (uint32)buf.size(), pDate);
		currentDocument->state.OnFSModified();
	} catch (const MyError &e) {
		currentDocument->state.statusMsg.sprintf("Import failed: %s", e.c_str());
	}
}

// Export stores the file key and date info at the time the dialog is opened,
// so it remains valid even if the user navigates to a different directory
// while the save dialog is showing.

static void DiskExplorerExportCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;

	currentDocument->diskExplorerPendingExport = filelist[0];
}

static void DiskExplorerDoExport(const char *utf8path) {
	if (!currentDocument->state.pFS) return;

	try {
		vdfastvector<uint8> buf;
		currentDocument->state.pFS->ReadFile(currentDocument->diskExportInfo.key, buf);

		VDStringW wpath = VDTextU8ToW(utf8path, -1);
		VDFile f(wpath.c_str(), nsVDFile::kWrite | nsVDFile::kCreateAlways | nsVDFile::kSequential);
		f.write(buf.data(), (long)buf.size());

		// Preserve file timestamp if available
		if (currentDocument->diskExportInfo.dateValid) {
			try {
				f.setCreationTime(VDDateFromLocalDate(currentDocument->diskExportInfo.date));
			} catch (...) {
				// Timestamp preservation is best-effort
			}
		}
	} catch (const MyError &e) {
		currentDocument->state.statusMsg.sprintf("Export failed: %s", e.c_str());
	}
}

// Multi-file export: export all selected files to a folder

static void DiskExplorerMultiExportCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;

	currentDocument->diskExplorerPendingMultiExport = filelist[0];
}

static void DiskExplorerDoMultiExport(const char *utf8folder) {
	if (!currentDocument->state.pFS) return;
	int exported = 0;
	for (auto &me : currentDocument->diskMultiExportEntries) {
		try {
			vdfastvector<uint8> buf;
			currentDocument->state.pFS->ReadFile(me.key, buf);

			if (currentDocument->diskMultiExportAsText)
				ConvertAtariToHost(buf);

			VDStringW wfolder = VDTextU8ToW(utf8folder, -1);
			VDStringW wpath = VDMakePath(wfolder.c_str(), VDTextU8ToW(me.name).c_str());
			VDFile f(wpath.c_str(), nsVDFile::kWrite | nsVDFile::kCreateAlways | nsVDFile::kSequential);
			f.write(buf.data(), (long)buf.size());

			if (me.dateValid) {
				try { f.setCreationTime(VDDateFromLocalDate(me.date)); } catch (...) {}
			}
			++exported;
		} catch (const MyError &e) {
			currentDocument->state.statusMsg.sprintf("Export '%s' failed: %s", me.name.c_str(), e.c_str());
		}
	}
	if (currentDocument->state.statusMsg.empty() || exported > 0)
		currentDocument->state.statusMsg.sprintf("Exported %d file(s).", exported);
	currentDocument->diskMultiExportEntries.clear();
}

// Import as text: read host file, convert CR/LF -> Atari 0x9B, write to disk

static void DiskExplorerImportTextCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;

	currentDocument->diskExplorerPendingImportText = filelist[0];
}

static void DiskExplorerDoImportText(const char *utf8path) {
	if (!currentDocument->state.pFS) return;

	try {
		VDStringW wpath = VDTextU8ToW(utf8path, -1);

		VDFile f(wpath.c_str());
		sint64 size = f.size();
		if (size > 16 * 1024 * 1024)
			throw MyError("File too large (max 16MB).");

		vdfastvector<uint8> buf((uint32)size);
		f.read(buf.data(), (long)buf.size());

		// Preserve host file creation time (matches Windows uidiskexplorer.cpp)
		const VDDate creationTime = f.getCreationTime();
		f.closeNT();

		VDExpandedDate expDate{};
		const VDExpandedDate *pDate = nullptr;
		if (creationTime != VDDate{}) {
			expDate = VDGetLocalDate(creationTime);
			pDate = &expDate;
		}

		// Convert host line endings to Atari EOL
		ConvertHostToAtari(buf);

		VDStringA filename = VDTextWToA(VDFileSplitPathRightSpan(wpath));
		DiskExplorerWriteFile(filename.c_str(), buf.data(), (uint32)buf.size(), pDate);
		currentDocument->state.OnFSModified();
	} catch (const MyError &e) {
		currentDocument->state.statusMsg.sprintf("Import as text failed: %s", e.c_str());
	}
}

// Export as text: read Atari file, convert 0x9B -> CR/LF, save to host

static void DiskExplorerExportTextCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;

	currentDocument->diskExplorerPendingExportText = filelist[0];
}

static void DiskExplorerDoExportText(const char *utf8path) {
	if (!currentDocument->state.pFS) return;

	try {
		vdfastvector<uint8> buf;
		currentDocument->state.pFS->ReadFile(currentDocument->diskExportTextInfo.key, buf);

		// Convert Atari EOL to host CR/LF
		ConvertAtariToHost(buf);

		VDStringW wpath = VDTextU8ToW(utf8path, -1);
		VDFile f(wpath.c_str(), nsVDFile::kWrite | nsVDFile::kCreateAlways | nsVDFile::kSequential);
		f.write(buf.data(), (long)buf.size());

		if (currentDocument->diskExportTextInfo.dateValid) {
			try {
				f.setCreationTime(VDDateFromLocalDate(currentDocument->diskExportTextInfo.date));
			} catch (...) {
			}
		}
	} catch (const MyError &e) {
		currentDocument->state.statusMsg.sprintf("Export as text failed: %s", e.c_str());
	}
}

// Partition import: import a disk image file into a partition

static void DiskExplorerPartitionImportCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;

	currentDocument->diskExplorerPendingPartitionImport = filelist[0];
}

static void DiskExplorerDoPartitionImport(const char *utf8path) {
	if (!currentDocument->state.pBlockDevice) return;
	int idx = currentDocument->diskExplorerPartitionImportIdx;
	if (idx < 0 || idx >= (int)currentDocument->state.partitions.size()) return;

	try {
		const ATPartitionInfo &pi = currentDocument->state.partitions[idx];
		vdrefptr<ATPartitionDiskView> pdview(new ATPartitionDiskView(*currentDocument->state.pBlockDevice, pi));

		if (!pdview->IsUpdatable())
			throw MyError("Cannot import disk image as partition is read-only.");

		VDStringW wpath = VDTextU8ToW(utf8path, -1);

		// Load source disk image
		ATImageLoadContext ctx;
		ctx.mLoadType = kATImageType_Disk;
		vdrefptr<IATImage> image0 = ATImageLoadFromFile(wpath.c_str(), &ctx);
		IATDiskImage *imageRaw = vdpoly_cast<IATDiskImage *>(image0.get());
		vdrefptr<IATDiskImage> image(imageRaw);
		if (!image)
			throw MyError("Failed to load disk image.");

		// Validate geometry match
		const uint32 partSectorCount = pdview->GetVirtualSectorCount();
		const uint32 partSectorSize = pdview->GetSectorSize();
		const uint32 imageSectorCount = image->GetVirtualSectorCount();
		const uint32 imageSectorSize = image->GetSectorSize();

		if (imageSectorCount != partSectorCount || imageSectorSize != partSectorSize) {
			VDStringA msg;
			msg.sprintf("Partition and image geometries do not match.\n\n"
				"Partition: %u sectors of %u bytes\n"
				"Image: %u sectors of %u bytes",
				partSectorCount, partSectorSize,
				imageSectorCount, imageSectorSize);
			currentDocument->state.statusMsg = msg;
			return;
		}

		// Copy sectors from image into partition
		uint8 secbuf[512];
		for (uint32 i = 0; i < partSectorCount; ++i) {
			uint32 len = pdview->GetSectorSize(i);
			uint32 actual = image->ReadVirtualSector(i, secbuf, len);
			if (actual < len)
				memset(&secbuf[actual], 0, len - actual);
			pdview->WriteVirtualSector(i, secbuf, len);
		}

		currentDocument->state.statusMsg = "Disk image imported to partition successfully.";
	} catch (const MyError &e) {
		currentDocument->state.statusMsg.sprintf("Partition import failed: %s", e.c_str());
	}
}

// Partition export: export a partition as a disk image file

static void DiskExplorerPartitionExportCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;

	currentDocument->diskExplorerPendingPartitionExport = filelist[0];
}

static void DiskExplorerDoPartitionExport(const char *utf8path) {
	if (!currentDocument->state.pBlockDevice) return;
	int idx = currentDocument->diskExplorerPartitionExportIdx;
	if (idx < 0 || idx >= (int)currentDocument->state.partitions.size()) return;

	try {
		const ATPartitionInfo &pi = currentDocument->state.partitions[idx];
		vdrefptr<ATPartitionDiskView> pdview(new ATPartitionDiskView(*currentDocument->state.pBlockDevice, pi));
		vdrefptr<IATDiskImage> newImage;

		ATCreateDiskImage(pdview->GetGeometry(), ~newImage);

		uint8 secbuf[512];
		const uint32 n = pdview->GetVirtualSectorCount();

		for (uint32 i = 0; i < n; ++i) {
			const uint32 sz = pdview->GetSectorSize(i);
			pdview->ReadVirtualSector(i, secbuf, sz);
			newImage->WriteVirtualSector(i, secbuf, sz);
		}

		VDStringW wpath = VDTextU8ToW(utf8path, -1);
		newImage->Save(wpath.c_str(), kATDiskImageFormat_ATR);

		currentDocument->state.statusMsg = "Partition exported as disk image successfully.";
	} catch (const MyError &e) {
		currentDocument->state.statusMsg.sprintf("Partition export failed: %s", e.c_str());
	}
}

static void DiskExplorerLoadSettings() {
	if (currentDocument->diskExplorerSettingsLoaded) return;
	currentDocument->diskExplorerSettingsLoaded = true;
	VDRegistryAppKey key("Settings", false);
	currentDocument->state.mbStrictFilenames = key.getBool("Disk Explorer: Strict filenames", currentDocument->state.mbStrictFilenames);
	currentDocument->state.mbAdjustFilenames = key.getBool("Disk Explorer: Adjust filenames", currentDocument->state.mbAdjustFilenames);
	int vm = key.getEnumInt("File Viewer: View mode", (int)kDEView_ASCII + 1, (int)kDEView_Hex);
	if (vm < (int)kDEView_Text || vm > (int)kDEView_ASCII)
		vm = (int)kDEView_TextGR0;
	currentDocument->state.viewMode = (DiskExplorerViewMode)vm;
}

static void DiskExplorerSaveSettings() {
	VDRegistryAppKey key("Settings", true);
	key.setBool("Disk Explorer: Strict filenames", currentDocument->state.mbStrictFilenames);
	key.setBool("Disk Explorer: Adjust filenames", currentDocument->state.mbAdjustFilenames);
	key.setInt("File Viewer: View mode", (int)currentDocument->state.viewMode);
}

static const char *DiskExplorerFormatName(ATDiskImageFormat format) {
	switch (format) {
	case kATDiskImageFormat_ATR: return "ATR";
	case kATDiskImageFormat_XFD: return "XFD";
	case kATDiskImageFormat_P2: return "PRO (P2)";
	case kATDiskImageFormat_P3: return "PRO (P3)";
	case kATDiskImageFormat_ATX: return "ATX";
	case kATDiskImageFormat_DCM: return "DCM";
	default: return "memory / virtual";
	}
}

static void DiskExplorerRenderHex(const uint8 *data, size_t len) {
	if (!data || !len) {
		ImGui::TextDisabled("No data.");
		return;
	}
	ImGui::BeginChild("HexData", ImVec2(0, 0), ImGuiChildFlags_Borders,
		ImGuiWindowFlags_HorizontalScrollbar);
	ATUIRenderHexDump(data, len);
	ImGui::EndChild();
}

static bool DiskExplorerReadBootRecord(IATDiskImage *image, vdfastvector<uint8>& data, VDStringA& error) {
	data.clear();
	error.clear();
	if (!image) { error = "No disk image is open."; return false; }
	const uint32 count = std::min(image->GetBootSectorCount(), image->GetVirtualSectorCount());
	if (!count) { error = "The image has no boot sectors."; return false; }
	for (uint32 i = 0; i < count; ++i) {
		const uint32 size = image->GetSectorSize(i);
		if (!size || data.size() > 1024 * 1024 - size) {
			error = "The boot record is too large to display.";
			return false;
		}
		const size_t oldSize = data.size();
		data.resize(oldSize + size);
		try {
			if (image->ReadVirtualSector(i, data.data() + oldSize, size) != size) {
				error.sprintf("Short read in boot sector %u.", i + 1);
				data.clear();
				return false;
			}
		} catch (const MyError& e) {
			error.sprintf("Boot record read failed: %s", e.c_str());
			data.clear();
			return false;
		}
	}
	return true;
}

// DOS 1/2 and MyDOS store the allocation bitmap at VTOC offset 10.  This is
// the same layout used by ATDiskFSDOS2, including the DOS 2.5 extension.  We
// intentionally report unknown for other filesystems instead of presenting a
// misleading allocation map.
static bool DiskExplorerBuildDOS2Map(IATDiskImage *image, std::vector<uint8>& map, VDStringA& note) {
	map.clear();
	note.clear();
	if (!image || image->GetVirtualSectorCount() <= 359) return false;
	try {
		const uint32 sectorSize = image->GetSectorSize(359);
		if (sectorSize != 128 && sectorSize != 256) return false;
		std::vector<uint8> vtoc(sectorSize);
		if (image->ReadVirtualSector(359, vtoc.data(), sectorSize) != sectorSize || vtoc[0] == 0 || vtoc[0] > 35)
			return false;
		const uint32 pages = vtoc[0] >= 2 ? vtoc[0] - 2 : 0;
		std::vector<uint8> bitmap(pages ? 256 * pages : sectorSize, 0);
		memcpy(bitmap.data(), vtoc.data(), sectorSize);
		const uint32 extra = pages ? ((sectorSize > 128 ? pages : pages * 2) - 1) : 0;
		for (uint32 i = 0; i < extra; ++i) {
			const uint32 vsec = 358 - i;
			if (image->ReadVirtualSector(vsec, bitmap.data() + sectorSize * (i + 1), sectorSize) != sectorSize)
				return false;
		}
		const uint32 count = image->GetVirtualSectorCount();
		if (count == 1040 && sectorSize == 128 && vtoc[0] == 2) {
			std::vector<uint8> extension(128);
			if (image->ReadVirtualSector(1023, extension.data(), 128) != 128) return false;
			bitmap.resize(256, 0);
			memcpy(bitmap.data() + 100, extension.data() + 84, 38);
			note = "Atari DOS 2.5 allocation bitmap";
		} else {
			note = vtoc[0] == 1 ? "Atari DOS 1.x allocation bitmap" : "DOS 2.x / MyDOS allocation bitmap";
		}
		map.resize(count, 2); // 2 = unknown / outside bitmap
		for (uint32 sector = 1; sector <= count; ++sector) {
			const uint32 bit = sector;
			const uint32 byteIndex = 10 + (bit >> 3);
			if (byteIndex >= bitmap.size()) break;
			map[sector - 1] = (bitmap[byteIndex] & (0x80 >> (bit & 7))) ? 1 : 0;
		}
		return true;
	} catch (const MyError&) {
		return false;
	}
}

static void DiskExplorerBeginToolWindow(const char *title, bool *open, ImVec2 size) {
	ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
}

static void DiskExplorerRenderInspectionWindows() {
	IATDiskImage *image = currentDocument->state.GetActiveImage();
	if (!image) return;

	if (currentDocument->state.showImageProperties) {
		DiskExplorerBeginToolWindow("Disk Image Properties", &currentDocument->state.showImageProperties, ImVec2(520, 430));
		if (ImGui::Begin(currentDocument->ToolTitle("Disk Image Properties").c_str(), &currentDocument->state.showImageProperties, ImGuiWindowFlags_NoSavedSettings)) {
			const ATDiskGeometryInfo geom = image->GetGeometry();
			ImGui::Text("Format: %s", DiskExplorerFormatName(image->GetImageFormat()));
			ImGui::Text("Virtual sectors: %u", image->GetVirtualSectorCount());
			ImGui::Text("Physical sectors: %u", image->GetPhysicalSectorCount());
			ImGui::Text("Geometry: %u tracks, %u side(s), %u sectors/track",
				(unsigned)geom.mTrackCount, (unsigned)geom.mSideCount, (unsigned)geom.mSectorsPerTrack);
			ImGui::Text("Sector size: %u bytes (boot: %u)", image->GetSectorSize(), image->GetBootSectorCount());
			ImGui::Text("Access: %s%s%s", image->IsUpdatable() ? "updatable" : "read-only",
				image->IsDynamic() ? ", dynamic" : "", image->IsDirty() ? ", dirty" : "");
			if (currentDocument->state.pDiskInterface && currentDocument->state.pDiskInterface->GetPath())
				ImGui::TextWrapped("Path: %s", VDTextWToA(currentDocument->state.pDiskInterface->GetPath()).c_str());
			if (currentDocument->state.pFS) {
				ATDiskFSInfo info;
				currentDocument->state.pFS->GetInfo(info);
				ImGui::Separator();
				ImGui::Text("Filesystem: %s", info.mFSType.c_str());
				ImGui::Text("Free: %u blocks (%u bytes each)", info.mFreeBlocks, info.mBlockSize);
				if (ImGui::Button("Validate filesystem")) {
					currentDocument->state.validationValid = true;
					currentDocument->state.validationProblems = !currentDocument->state.pFS->Validate(currentDocument->state.validationReport);
				}
				if (currentDocument->state.validationValid) {
					if (!currentDocument->state.validationReport.IsSerious() && currentDocument->state.validationReport.mbBitmapIncorrectLostSectorsOnly)
						ImGui::TextColored(ImVec4(1, .8f, .2f, 1), "Validation: minor lost-sector bitmap issue.");
					else if (currentDocument->state.validationReport.IsSerious())
						ImGui::TextColored(ImVec4(1, .3f, .3f, 1), "Validation: serious filesystem problems found.");
					else
						ImGui::TextColored(ImVec4(.3f, 1, .3f, 1), "Validation: no problems found.");
				}
			}
		}
		ImGui::End();
	}

	if (currentDocument->state.showSectorViewer) {
		DiskExplorerBeginToolWindow("Sector Viewer", &currentDocument->state.showSectorViewer, ImVec2(760, 560));
		if (ImGui::Begin(currentDocument->ToolTitle("Sector Viewer").c_str(), &currentDocument->state.showSectorViewer, ImGuiWindowFlags_NoSavedSettings)) {
			const uint32 count = image->GetVirtualSectorCount();
			ImGui::SetNextItemWidth(120);
			if (ImGui::InputScalar("Sector (1-based)", ImGuiDataType_U32, &currentDocument->state.sectorNumber)) {
				currentDocument->state.sectorNumber = std::max<uint32>(1, std::min(count, currentDocument->state.sectorNumber));
				currentDocument->state.LoadSector(currentDocument->state.sectorNumber);
			}
			ImGui::SameLine();
			if (ImGui::Button("Previous") && currentDocument->state.sectorNumber > 1) {
				--currentDocument->state.sectorNumber;
				currentDocument->state.LoadSector(currentDocument->state.sectorNumber);
			}
			ImGui::SameLine();
			if (ImGui::Button("Next") && currentDocument->state.sectorNumber < count) {
				++currentDocument->state.sectorNumber;
				currentDocument->state.LoadSector(currentDocument->state.sectorNumber);
			}
			if (currentDocument->state.loadedSectorNumber != currentDocument->state.sectorNumber)
				currentDocument->state.LoadSector(currentDocument->state.sectorNumber);
			if (!currentDocument->state.sectorError.empty())
				ImGui::TextColored(ImVec4(1, .35f, .35f, 1), "%s", currentDocument->state.sectorError.c_str());
			if (currentDocument->state.loadedSectorNumber) {
				ImGui::Text("%u bytes", (unsigned)currentDocument->state.sectorData.size());
				if (ImGui::BeginTabBar("SectorViews")) {
					if (ImGui::BeginTabItem("Hex")) { DiskExplorerRenderHex(currentDocument->state.sectorData.data(), currentDocument->state.sectorData.size()); ImGui::EndTabItem(); }
					if (ImGui::BeginTabItem("ATASCII")) { ImGui::TextDisabled("High-bit bytes are rendered as inverse video; $9B is Atari EOL."); ATUIRenderTextColumnControls("SectorATASCIIColumns"); ImGui::BeginChild("SectorATASCII", ImVec2(0, 0), ImGuiChildFlags_Borders); ATUIRenderATASCII(currentDocument->state.sectorData.data(), currentDocument->state.sectorData.size(), ATUIGetTextColumns()); ImGui::EndChild(); ImGui::EndTabItem(); }
					if (ImGui::BeginTabItem("ASCII")) { ImGui::TextDisabled("Inverse-video bits are stripped; $9B is shown as a line break."); ATUIRenderTextColumnControls("SectorASCIIColumns"); ImGui::BeginChild("SectorASCII", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar); ATUIRenderASCII(currentDocument->state.sectorData.data(), currentDocument->state.sectorData.size(), ATUIGetTextColumns()); ImGui::EndChild(); ImGui::EndTabItem(); }
					ImGui::EndTabBar();
				}
			}
		}
		ImGui::End();
	}

	if (currentDocument->state.showBootRecord) {
		DiskExplorerBeginToolWindow("Boot Record", &currentDocument->state.showBootRecord, ImVec2(760, 560));
		if (ImGui::Begin(currentDocument->ToolTitle("Boot Record").c_str(), &currentDocument->state.showBootRecord, ImGuiWindowFlags_NoSavedSettings)) {
			vdfastvector<uint8> boot;
			VDStringA error;
			if (DiskExplorerReadBootRecord(image, boot, error)) {
				ImGui::Text("Boot sectors: %u (%u bytes)", image->GetBootSectorCount(), (unsigned)boot.size());
				if (ImGui::BeginTabBar("BootViews")) {
					if (ImGui::BeginTabItem("Hex")) { DiskExplorerRenderHex(boot.data(), boot.size()); ImGui::EndTabItem(); }
					if (ImGui::BeginTabItem("6502")) {
						VDStringA disasm, disasmError;
						ATUIDisassemble6502(boot.data(), boot.size(), 0, disasm, disasmError);
						ImGui::BeginChild("BootDisassembly", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
						ImGui::PushFont(ATUIGetFontMono());
						ImGui::TextUnformatted(disasm.empty() ? disasmError.c_str() : disasm.c_str());
						ImGui::PopFont();
						ImGui::EndChild();
						ImGui::EndTabItem();
					}
					if (ImGui::BeginTabItem("ATASCII")) { ImGui::TextDisabled("High-bit bytes are rendered as inverse video; $9B is Atari EOL."); ATUIRenderTextColumnControls("BootATASCIIColumns"); ImGui::BeginChild("BootATASCII", ImVec2(0, 0), ImGuiChildFlags_Borders); ATUIRenderATASCII(boot.data(), boot.size(), ATUIGetTextColumns()); ImGui::EndChild(); ImGui::EndTabItem(); }
					if (ImGui::BeginTabItem("ASCII")) { ImGui::TextDisabled("Inverse-video bits are stripped; $9B is shown as a line break."); ATUIRenderTextColumnControls("BootASCIIColumns"); ImGui::BeginChild("BootASCII", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar); ATUIRenderASCII(boot.data(), boot.size(), ATUIGetTextColumns()); ImGui::EndChild(); ImGui::EndTabItem(); }
					ImGui::EndTabBar();
				}
			} else ImGui::TextColored(ImVec4(1, .35f, .35f, 1), "%s", error.c_str());
		}
		ImGui::End();
	}

	if (currentDocument->state.showSectorMap) {
		DiskExplorerBeginToolWindow("Sector Map", &currentDocument->state.showSectorMap, ImVec2(760, 560));
		if (ImGui::Begin(currentDocument->ToolTitle("Sector Map").c_str(), &currentDocument->state.showSectorMap, ImGuiWindowFlags_NoSavedSettings)) {
			std::vector<uint8> map;
			VDStringA note;
			if (DiskExplorerBuildDOS2Map(image, map, note)) {
				ImGui::TextUnformatted(note.c_str());
				ImGui::TextDisabled("Green = free, blue = allocated, gray = unavailable");
				if (ImGui::BeginChild("SectorMapGrid", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
					if (ImGui::BeginTable("SectorMapTable", 16, ImGuiTableFlags_SizingFixedFit)) {
						for (uint32 i = 0; i < map.size(); ++i) {
							ImGui::TableNextColumn();
							ImGui::PushID((int)i);
							const ImVec4 color = map[i] == 1 ? ImVec4(.25f, .75f, .25f, 1) : map[i] == 0 ? ImVec4(.25f, .45f, .85f, 1) : ImVec4(.35f, .35f, .35f, 1);
							ImGui::ColorButton("##sector", color, ImGuiColorEditFlags_NoTooltip, ImVec2(18, 18));
							if (ImGui::IsItemHovered()) { ImGui::BeginTooltip(); ImGui::Text("Sector %u: %s", i + 1, map[i] == 1 ? "free" : map[i] == 0 ? "allocated" : "unknown"); ImGui::EndTooltip(); }
							ImGui::PopID();
						}
						ImGui::EndTable();
					}
					ImGui::EndChild();
				}
			} else {
				ImGui::TextWrapped("An allocation bitmap is not available for this filesystem. Physical/virtual sector details are available in Image Properties.");
			}
		}
		ImGui::End();
	}

	if (currentDocument->state.showFileInfo && currentDocument->state.pFS) {
		DiskExplorerBeginToolWindow("File Information", &currentDocument->state.showFileInfo, ImVec2(420, 260));
		if (ImGui::Begin(currentDocument->ToolTitle("File Information").c_str(), &currentDocument->state.showFileInfo, ImGuiWindowFlags_NoSavedSettings)) {
			ATDiskFSEntryInfo info;
			try {
				currentDocument->state.pFS->GetFileInfo(currentDocument->state.fileInfoKey, info);
				ImGui::Text("Name: %s", info.mFileName.c_str());
				ImGui::Text("Type: %s", info.mbIsDirectory ? "directory" : "file");
				ImGui::Text("Sectors: %u", info.mSectors);
				ImGui::Text("Bytes: %u", info.mBytes);
				if (info.mbDateValid) ImGui::Text("Date: %02u/%02u/%04u %02u:%02u:%02u", info.mDate.mMonth, info.mDate.mDay, info.mDate.mYear, info.mDate.mHour, info.mDate.mMinute, info.mDate.mSecond);
			} catch (const MyError& e) { ImGui::TextColored(ImVec4(1, .35f, .35f, 1), "%s", e.c_str()); }
		}
		ImGui::End();
	}
}

static void DiskExplorerSort(ImGuiTableSortSpecs *specs) {
	auto& state = currentDocument->state;
	if (!specs || (!specs->SpecsDirty && !state.sortDirty)) return;
	state.sortDirty = false;
	specs->SpecsDirty = false;
	if (specs->SpecsCount == 0) return;
	const auto spec = specs->Specs[0];
	std::vector<int> order(state.entries.size());
	for (size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
	std::stable_sort(order.begin(), order.end(), [&](int ai, int bi) {
		const auto& a = state.entries[ai]; const auto& b = state.entries[bi];
		if (a.name == ".." || b.name == "..") return a.name == ".." && b.name != "..";
		if (a.isDir != b.isDir) return a.isDir;
		int cmp = 0;
		auto compare = [](const auto& x, const auto& y) { return x < y ? -1 : x > y ? 1 : 0; };
		if (spec.ColumnIndex == 1) cmp = compare(a.sectors, b.sectors);
		else if (spec.ColumnIndex == 2) cmp = compare(a.bytes, b.bytes);
		else if (spec.ColumnIndex == 3) {
			auto date = [](const auto& e) { return std::make_tuple(e.dateValid, e.date.mYear,
				e.date.mMonth, e.date.mDay, e.date.mHour, e.date.mMinute, e.date.mSecond); };
			cmp = compare(date(a), date(b));
		}
		if (!cmp) cmp = a.name.comparei(b.name.c_str());
		return spec.SortDirection == ImGuiSortDirection_Descending ? cmp > 0 : cmp < 0;
	});
	auto entries = state.entries;
	auto selected = state.selected;
	const int active = state.selectedEntry, anchor = state.lastClickedEntry;
	for (size_t i = 0; i < order.size(); ++i) {
		state.entries[i] = std::move(entries[order[i]]);
		state.selected[i] = selected[order[i]];
		if (order[i] == active) state.selectedEntry = (int)i;
		if (order[i] == anchor) state.lastClickedEntry = (int)i;
	}
}

static bool DiskExplorerNeedsSave(const DiskExplorerDocument& doc) {
	return doc.state.modified && doc.state.pImage && !doc.state.pDiskInterface && !doc.state.pBlockDevice;
}
static void DiskExplorerSaveImageCallback(void *, const char * const *paths, int filter) {
	if (!paths || !paths[0]) { currentDocument->closeRequested = false; return; }
	try {
		// Filter determines the format explicitly; the canonical saver rejects
		// conversions that cannot preserve this image's sector characteristics.
		static const ATDiskImageFormat formats[] = {kATDiskImageFormat_ATR,
			kATDiskImageFormat_XFD, kATDiskImageFormat_ATX, kATDiskImageFormat_DCM,
			kATDiskImageFormat_P2, kATDiskImageFormat_P3};
		if (filter < 0 || filter >= 6) filter = 0;
		if (currentDocument->state.pFS) currentDocument->state.pFS->Flush();
		currentDocument->state.pImage->Save(VDTextU8ToW(paths[0], -1).c_str(), formats[filter]);
		currentDocument->source = ATUIExplorerFullPath(paths[0]);
		currentDocument->state.modified = false;
		currentDocument->state.statusMsg = "Saved disk image.";
		if (currentDocument->closeRequested) currentDocument->open = false;
	} catch (const MyError& e) {
		currentDocument->state.statusMsg.sprintf("Save failed: %s", e.c_str());
		currentDocument->closeRequested = false;
	}
}
static void DiskExplorerSaveAs(SDL_Window *window) {
	static const SDL_DialogFileFilter filters[] = {{"ATR disk image", "atr"},
		{"XFD disk image", "xfd"}, {"ATX disk image", "atx"}, {"DCM disk image", "dcm"},
		{"PRO type 2 disk image", "pro"}, {"PRO type 3 disk image", "pro"}};
	ATUIExplorerFileDialog(*currentDocument, true, 'disk', DiskExplorerSaveImageCallback,
		nullptr, window, filters, 6);
}
static void DiskExplorerClosePrompt(SDL_Window *window) {
	auto& doc = *currentDocument;
	if (!doc.open && DiskExplorerNeedsSave(doc)) { doc.open = true; doc.closeRequested = true; }
	if (!doc.closeRequested || doc.dialogPending) return;
	const std::string title = doc.ToolTitle("Save disk changes?");
	ImGui::OpenPopup(title.c_str());
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(.5f, .5f));
	if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::TextUnformatted("Save the modified disk image before closing?");
		ImGui::TextUnformatted(doc.source.c_str());
		if (ImGui::Button("Save as...")) { ImGui::CloseCurrentPopup(); DiskExplorerSaveAs(window); }
		ImGui::SameLine();
		if (ImGui::Button("Discard changes")) {
			doc.state.modified = false; doc.open = false; doc.closeRequested = false; ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) { doc.closeRequested = false; ImGui::CloseCurrentPopup(); }
		ImGui::EndPopup();
	}
}
static bool DiskExplorerSelectedIsXEX(VDStringA *error = nullptr) {
	const auto& state = currentDocument->state;
	if (state.GetSelectionCount() != 1 || !state.viewValid) {
		if (error) *error = "Select one file to run.";
		return false;
	}
	std::vector<ATUIXEXSegmentInfo> segments;
	VDStringA parseError;
	const bool valid = ATUIParseXEX(state.viewData.data(), state.viewData.size(),
		segments, parseError);
	if (error) *error = valid ? VDStringA() : parseError;
	return valid;
}
static std::string DiskExplorerSelectedOrigin() {
	const auto& state = currentDocument->state;
	return currentDocument->source + " > " + state.pathStr.c_str()
		+ state.entries[state.selectedEntry].name.c_str();
}
static void DiskExplorerRunSelectedXEX() {
	const auto& state = currentDocument->state;
	if (!DiskExplorerSelectedIsXEX() || state.selectedEntry < 0) return;
	const std::string origin = DiskExplorerSelectedOrigin();
	ATUIBootProgramData(origin.c_str(), state.viewData.data(), state.viewData.size());
	currentDocument->state.statusMsg.sprintf("Running %s from the disk image.",
		state.entries[state.selectedEntry].name.c_str());
}
static void DiskExplorerOpenSelectedXEX() {
	const auto& state = currentDocument->state;
	if (!DiskExplorerSelectedIsXEX() || state.selectedEntry < 0) return;
	const std::string origin = DiskExplorerSelectedOrigin();
	ATUIOpenXEXExplorerData(origin.c_str(), state.viewData.data(), state.viewData.size());
}

static void RenderDocument(ATSimulator &sim, ATUIState &state, SDL_Window *window) {
	DiskExplorerLoadSettings();

	// Process pending file dialog results on main thread
	{
		std::string importPath, importTextPath;
		{

			importPath.swap(currentDocument->diskExplorerPendingImport);
			importTextPath.swap(currentDocument->diskExplorerPendingImportText);
		}
		if (!importPath.empty())
			DiskExplorerDoImport(importPath.c_str());
		if (!importTextPath.empty())
			DiskExplorerDoImportText(importTextPath.c_str());

		std::string exportPath, exportTextPath, partImportPath, partExportPath;
		{

			exportPath.swap(currentDocument->diskExplorerPendingExport);
			exportTextPath.swap(currentDocument->diskExplorerPendingExportText);
			partImportPath.swap(currentDocument->diskExplorerPendingPartitionImport);
			partExportPath.swap(currentDocument->diskExplorerPendingPartitionExport);
		}
		if (!exportPath.empty())
			DiskExplorerDoExport(exportPath.c_str());
		if (!exportTextPath.empty())
			DiskExplorerDoExportText(exportTextPath.c_str());
		if (!partImportPath.empty())
			DiskExplorerDoPartitionImport(partImportPath.c_str());
		if (!partExportPath.empty())
			DiskExplorerDoPartitionExport(partExportPath.c_str());

		std::string multiExportPath;
		{

			multiExportPath.swap(currentDocument->diskExplorerPendingMultiExport);
		}
		if (!multiExportPath.empty())
			DiskExplorerDoMultiExport(multiExportPath.c_str());
	}

	currentDocument->Prepare();
	if (!ImGui::Begin(currentDocument->Title("Disk Explorer").c_str(), &currentDocument->open, ImGuiWindowFlags_MenuBar)) {
		currentDocument->Track();

		ImGui::End();
		DiskExplorerRenderInspectionWindows();
		return;
	}

	// Save window rect for drop hit-testing
	currentDocument->Track();

	if (ATUICheckEscClose()) {
		DiskExplorerSaveSettings();

		currentDocument->open = false;
		ImGui::End();
		return;
	}

	if (!currentDocument->open) {
		DiskExplorerSaveSettings();

		ImGui::End();
		return;
	}

	ATUIExplorerSource(currentDocument->source);
	if (DiskExplorerNeedsSave(*currentDocument)) ImGui::TextUnformatted("Modified image - changes are not saved to disk.");
	// Menu bar
	if (ImGui::BeginMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("Open another...")) {
				static const SDL_DialogFileFilter kFilters[] = {
					{ "Disk Images", "atr;xfd;dcm;pro;atx;gz;zip;atz" },
					{ "All Files", "*" },
				};
				ATUIExplorerFileDialog(*currentDocument, false, 'disk', DiskExplorerOpenCallback, nullptr, window, kFilters, 2, false);
			}

			if (ImGui::MenuItem("Save disk image as...", nullptr, false,
				currentDocument->state.pImage && !currentDocument->state.pDiskInterface))
				DiskExplorerSaveAs(window);
			// Open from mounted drives
			if (ImGui::BeginMenu("Open Mounted Drive")) {
				bool anyMounted = false;
				for (int d = 0; d < 15; ++d) {
					ATDiskInterface &di = sim.GetDiskInterface(d);
					IATDiskImage *dimg = di.GetDiskImage();
					if (dimg) {
						anyMounted = true;
						VDStringA label;
						label.sprintf("D%d:", d + 1);
						if (ImGui::MenuItem(label.c_str())) {
							ATUIOpenDiskExplorerForDrive(d, false, false);
						}
					}
				}
				if (!anyMounted)
					ImGui::TextDisabled("No disk images mounted");
				ImGui::EndMenu();
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Close")) {
				DiskExplorerSaveSettings();
				currentDocument->open = false;
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("View")) {
			auto setMode = [](DiskExplorerViewMode m) {
				currentDocument->state.viewMode = m;
				currentDocument->state.FormatView();
				currentDocument->state.viewValid = !currentDocument->state.viewData.empty();
			};
			if (ImGui::MenuItem("ATASCII: no line wrapping", nullptr, currentDocument->state.viewMode == kDEView_Text))
				setMode(kDEView_Text);
			if (ImGui::MenuItem("ATASCII: wrap to window", nullptr, currentDocument->state.viewMode == kDEView_TextWrap))
				setMode(kDEView_TextWrap);
			VDStringA atasciiColumnsLabel;
			atasciiColumnsLabel.sprintf("ATASCII: %d columns", ATUIGetTextColumns());
			if (ImGui::MenuItem(atasciiColumnsLabel.c_str(), nullptr, currentDocument->state.viewMode == kDEView_TextGR0))
				setMode(kDEView_TextGR0);
			if (ImGui::MenuItem("ASCII (7-bit)", nullptr, currentDocument->state.viewMode == kDEView_ASCII))
				setMode(kDEView_ASCII);
			if (ImGui::MenuItem("Hex dump", nullptr, currentDocument->state.viewMode == kDEView_Hex))
				setMode(kDEView_Hex);
			if (ImGui::MenuItem("Executable", nullptr, currentDocument->state.viewMode == kDEView_Executable))
				setMode(kDEView_Executable);
			if (ImGui::MenuItem("MAC/65", nullptr, currentDocument->state.viewMode == kDEView_MAC65))
				setMode(kDEView_MAC65);
			if (ImGui::MenuItem("Atari BASIC", nullptr, currentDocument->state.viewMode == kDEView_AtariBasic))
				setMode(kDEView_AtariBasic);
			if (ImGui::MenuItem("Syn assembler", nullptr, currentDocument->state.viewMode == kDEView_SynAssembler))
				setMode(kDEView_SynAssembler);
			if (ImGui::MenuItem("6502 disassembly", nullptr, currentDocument->state.viewMode == kDEView_6502))
				setMode(kDEView_6502);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Disk")) {
			const bool haveImage = currentDocument->state.GetActiveImage() != nullptr;
			ImGui::BeginDisabled(!haveImage);
			if (ImGui::MenuItem("View Sectors...")) {
				currentDocument->state.showSectorViewer = true;
				currentDocument->state.sectorNumber = 1;
				currentDocument->state.loadedSectorNumber = 0;
			}
			if (ImGui::MenuItem("Show Boot Record")) currentDocument->state.showBootRecord = true;
			if (ImGui::MenuItem("Show Image Properties")) currentDocument->state.showImageProperties = true;
			if (ImGui::MenuItem("Show Sector Map")) currentDocument->state.showSectorMap = true;
			ImGui::EndDisabled();
			ImGui::Separator();
			ImGui::BeginDisabled(!currentDocument->state.pFS);
			if (ImGui::MenuItem("Validate File System")) {
				currentDocument->state.validationValid = true;
				currentDocument->state.validationProblems = !currentDocument->state.pFS->Validate(currentDocument->state.validationReport);
				currentDocument->state.statusMsg = currentDocument->state.validationProblems ? "Filesystem validation found problems." : "Filesystem validation passed.";
			}
			ImGui::EndDisabled();
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Options")) {
			if (ImGui::BeginMenu("Filename Checking")) {
				if (ImGui::MenuItem("Strict", nullptr, currentDocument->state.mbStrictFilenames)) {
					currentDocument->state.mbStrictFilenames = true;
					if (currentDocument->state.pFS)
						currentDocument->state.pFS->SetStrictNameChecking(true);
				}
				if (ImGui::MenuItem("Relaxed", nullptr, !currentDocument->state.mbStrictFilenames)) {
					currentDocument->state.mbStrictFilenames = false;
					if (currentDocument->state.pFS)
						currentDocument->state.pFS->SetStrictNameChecking(false);
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Adjust Conflicting Filenames", nullptr, currentDocument->state.mbAdjustFilenames))
					currentDocument->state.mbAdjustFilenames = !currentDocument->state.mbAdjustFilenames;
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}

	// Partition list view: when a block device is loaded but no filesystem is mounted
	if (!currentDocument->state.pFS && currentDocument->state.pBlockDevice) {
		ImGui::Text("Block Device - %d partition(s)", (int)currentDocument->state.partitions.size());

		if (!currentDocument->state.statusMsg.empty())
			ImGui::TextDisabled("%s", currentDocument->state.statusMsg.c_str());

		ImGui::Separator();

		if (ImGui::BeginTable("PartitionList", 4,
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
			ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
			ImVec2(0, 0))) {

			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.40f);
			ImGui::TableSetupColumn("Sectors", ImGuiTableColumnFlags_WidthStretch, 0.20f);
			ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 0.20f);
			ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthStretch, 0.20f);
			ImGui::TableHeadersRow();

			for (int i = 0; i < (int)currentDocument->state.partitions.size(); ++i) {
				const ATPartitionInfo &pi = currentDocument->state.partitions[i];
				ImGui::TableNextRow();
				ImGui::PushID(i);

				ImGui::TableNextColumn();
				VDStringA name = VDTextWToA(pi.mName);
				bool selected = (currentDocument->state.selectedPartition == i);
				if (ImGui::Selectable(name.c_str(), selected,
					ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)
				|| ImGui::GetCurrentContext()->NavJustMovedToId == ImGui::GetItemID()) {
					currentDocument->state.selectedPartition = i;

					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
						try {
							currentDocument->state.OpenPartition(i);
						} catch (const MyError &e) {
							currentDocument->state.statusMsg.sprintf("Open partition failed: %s", e.c_str());
						}
						ImGui::PopID();
						ImGui::EndTable();
						ImGui::End();
						return;
					}
				}

				// Partition context menu
				if (ImGui::BeginPopupContextItem()) {
					if (ImGui::MenuItem("Open")) {
						int openIdx = i;
						ImGui::EndPopup();
						ImGui::PopID();
						ImGui::EndTable();
						try {
							currentDocument->state.OpenPartition(openIdx);
						} catch (const MyError &e) {
							currentDocument->state.statusMsg.sprintf("Open partition failed: %s", e.c_str());
						}
						ImGui::End();
						return;
					}

					ImGui::Separator();

					bool writable = !currentDocument->state.pBlockDevice->IsReadOnly() && !pi.mbWriteProtected;
					ImGui::BeginDisabled(!writable);
					if (ImGui::MenuItem("Import Disk Image...")) {
						currentDocument->diskExplorerPartitionImportIdx = i;
						static const SDL_DialogFileFilter kFilters[] = {
							{ "Disk Images", "atr;xfd;dcm;pro;atx;gz;zip;atz" },
							{ "All Files", "*" },
						};
						ATUIExplorerFileDialog(*currentDocument, false, 'disk', DiskExplorerPartitionImportCallback, nullptr, window, kFilters, 2, false);
					}
					ImGui::EndDisabled();

					if (ImGui::MenuItem("Export Disk Image...")) {
						currentDocument->diskExplorerPartitionExportIdx = i;
						static const SDL_DialogFileFilter kFilters[] = {
							{ "Atari Disk Image", "atr" },
							{ "All Files", "*" },
						};
						ATUIExplorerFileDialog(*currentDocument, true, 'disk', DiskExplorerPartitionExportCallback, nullptr, window, kFilters, 2);
					}

					ImGui::EndPopup();
				}

				ImGui::TableNextColumn();
				ImGui::Text("%u", pi.mBlockCount);

				ImGui::TableNextColumn();
				ImGui::Text("%u", pi.mSectorCount * pi.mSectorSize);

				ImGui::TableNextColumn();
				// Date column — empty for partitions (matches Windows)

				ImGui::PopID();
			}

			ImGui::EndTable();
		}

		ImGui::End();
		return;
	}

	if (!currentDocument->state.pFS) {
		ImGui::TextWrapped("No recognized filesystem. Use the Disk menu to inspect sectors, the boot record, and image properties.");
		ImGui::TextDisabled("You can also drop an ATR, XFD, ATX, DCM, PRO, ZIP, GZ, or ATZ image onto this window.");
		if (!currentDocument->state.statusMsg.empty())
			ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", currentDocument->state.statusMsg.c_str());
		ImGui::End();
		DiskExplorerRenderInspectionWindows();
		return;
	}

	// Toolbar
	ImGui::Text("Path: %s", currentDocument->state.pathStr.c_str());

	bool canWrite = !currentDocument->state.readOnly;
	ImGui::BeginDisabled(!canWrite);
	if (ImGui::SmallButton("Import...")) {
		static const SDL_DialogFileFilter kFilters[] = { { "All Files", "*" } };
		ATUIExplorerFileDialog(*currentDocument, false, 'dexp', DiskExplorerImportCallback, nullptr, window, kFilters, 1, false);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();

	// Count selected exportable files for the toolbar Export button
	int toolbarSelFiles = 0;
	for (int j = 0; j < (int)currentDocument->state.entries.size(); ++j) {
		if (j < (int)currentDocument->state.selected.size() && currentDocument->state.selected[j]
			&& !currentDocument->state.entries[j].isDir
			&& currentDocument->state.entries[j].name != ".."
			&& currentDocument->state.entries[j].key != ATDiskFSKey::None)
			++toolbarSelFiles;
	}
	ImGui::BeginDisabled(toolbarSelFiles == 0);
	if (ImGui::SmallButton("Export selected...") && toolbarSelFiles > 0) {
		if (toolbarSelFiles > 1) {
			currentDocument->diskMultiExportEntries.clear();
			currentDocument->diskMultiExportAsText = false;
			for (int j = 0; j < (int)currentDocument->state.entries.size(); ++j) {
				if (j < (int)currentDocument->state.selected.size() && currentDocument->state.selected[j]
					&& !currentDocument->state.entries[j].isDir
					&& currentDocument->state.entries[j].name != ".."
					&& currentDocument->state.entries[j].key != ATDiskFSKey::None) {
					auto &ej = currentDocument->state.entries[j];
					currentDocument->diskMultiExportEntries.push_back({ej.key, ej.name, ej.dateValid, ej.date});
				}
			}
			ATUIExplorerFolderDialog(*currentDocument, 'dexp',
				DiskExplorerMultiExportCallback, nullptr, window);
		} else {
			// Single file export
			for (int j = 0; j < (int)currentDocument->state.entries.size(); ++j) {
				if (j < (int)currentDocument->state.selected.size() && currentDocument->state.selected[j]
					&& !currentDocument->state.entries[j].isDir) {
					auto &ej = currentDocument->state.entries[j];
					currentDocument->diskExportInfo.key = ej.key;
					currentDocument->diskExportInfo.dateValid = ej.dateValid;
					currentDocument->diskExportInfo.date = ej.date;
					break;
				}
			}
			static const SDL_DialogFileFilter kFilters[] = { { "All Files", "*" } };
			ATUIExplorerFileDialog(*currentDocument, true, 'dexp', DiskExplorerExportCallback, nullptr, window, kFilters, 1);
		}
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::BeginDisabled(!canWrite);
	if (ImGui::SmallButton("New folder..."))
		ImGui::OpenPopup("NewDirPopup");
	ImGui::EndDisabled();

	// New Directory popup
	if (ImGui::BeginPopup("NewDirPopup")) {
		if (!currentDocument->state.pFS) {
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		} else {

			ImGui::Text("Directory name:");
			ImGui::SetNextItemWidth(200);
			bool submitted = ImGui::InputText("##dirname", currentDocument->newDirName, sizeof(currentDocument->newDirName),
				ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SameLine();
			if ((ImGui::Button("OK") || submitted) && currentDocument->newDirName[0]) {
				try {
					currentDocument->state.pFS->CreateDir(currentDocument->state.currentDir, currentDocument->newDirName);
					currentDocument->state.OnFSModified();
					currentDocument->newDirName[0] = 0;
					ImGui::CloseCurrentPopup();
				} catch (const MyError &err) {
					currentDocument->state.statusMsg.sprintf("Create dir failed: %s", err.c_str());
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel")) {
				currentDocument->newDirName[0] = 0;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	ImGui::SameLine();
	bool rwToggle = !currentDocument->state.readOnly;
	if (ImGui::Checkbox("Write", &rwToggle)) {
		currentDocument->state.readOnly = !rwToggle;
		// Remount with new read/write mode — use pImage for standalone images,
		// pPartitionView for partition filesystems
		IATDiskImage *mountImage = currentDocument->state.pImage
			? currentDocument->state.pImage.get()
			: currentDocument->state.pPartitionView.get();
		if (mountImage) {
			try {
				delete currentDocument->state.pFS;
				currentDocument->state.pFS = nullptr;
				currentDocument->state.pFS = ATDiskMountImage(mountImage, currentDocument->state.readOnly);
				if (currentDocument->state.pFS)
					currentDocument->state.pFS->SetStrictNameChecking(currentDocument->state.mbStrictFilenames);

				// Check if writes are actually possible — matches Windows MountFS logic
				if (!currentDocument->state.readOnly && currentDocument->state.pFS) {
					if (mountImage->IsUpdatable() && currentDocument->state.pFS->IsReadOnly()) {
						// Image format is updatable but FS can only be mounted read-only
						currentDocument->state.readOnly = true;
						currentDocument->state.statusMsg = "This disk format is only supported in read-only mode.";
					} else {
						currentDocument->state.ValidateForWrites();
					}
				}

				currentDocument->state.RefreshDirectory();
			} catch (const MyError &e) {
				currentDocument->state.statusMsg.sprintf("Remount failed: %s", e.c_str());
			}
		}
	}

	ImGui::Separator();

	ImGui::BeginDisabled(currentDocument->state.GetSelectionCount() == 0);
	if (ImGui::Button("Selection actions...")) currentDocument->actionsRequested = true;
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Select all")) {
		for (size_t i = 0; i < currentDocument->state.entries.size(); ++i)
			currentDocument->state.selected[i] = currentDocument->state.entries[i].name != "..";
		if (!currentDocument->state.entries.empty()) currentDocument->state.selectedEntry = 0;
	}
	ImGui::SameLine();
	VDStringA runError;
	const bool canRunXEX = DiskExplorerSelectedIsXEX(&runError);
	ImGui::BeginDisabled(!canRunXEX);
	if (ImGui::Button("Run")) DiskExplorerRunSelectedXEX();
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("%s", canRunXEX ?
			"Load and run this executable directly from the disk image." : runError.c_str());
	ImGui::SameLine();
	ImGui::BeginDisabled(!canRunXEX);
	if (ImGui::Button("Open in XEX Explorer")) DiskExplorerOpenSelectedXEX();
	ImGui::EndDisabled();
	// Split: file list (top) and viewer (bottom)
	const float splitHeight = ImGui::GetContentRegionAvail().y;
	float listH = ATUIExplorerListHeight(*currentDocument);

	// File list
	if (ImGui::BeginTable("FileList", 4,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
		ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingStretchProp,
		ImVec2(0, listH))) {

		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch, 0.40f);
		ImGui::TableSetupColumn("Sectors", ImGuiTableColumnFlags_WidthStretch, 0.15f);
		ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 0.20f);
		ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthStretch, 0.25f);
		ImGui::TableHeadersRow();
		DiskExplorerSort(ImGui::TableGetSortSpecs());

		for (int i = 0; i < (int)currentDocument->state.entries.size(); ++i) {
			auto &e = currentDocument->state.entries[i];
			ImGui::TableNextRow();
			ImGui::PushID(i);

			// Name
			ImGui::TableNextColumn();
			bool isSel = (i < (int)currentDocument->state.selected.size()) && currentDocument->state.selected[i];
			VDStringA label;
			if (e.isDir)
				label.sprintf("[%s]", e.name.c_str());
			else
				label = e.name;

			if (ImGui::Selectable(label.c_str(), isSel,
				ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)
				|| ImGui::GetCurrentContext()->NavJustMovedToId == ImGui::GetItemID()) {
				ImGuiIO &io = ImGui::GetIO();
				if (io.KeyCtrl || io.KeySuper) {
					currentDocument->state.ToggleSelection(i);
				} else if (io.KeyShift && currentDocument->state.lastClickedEntry >= 0) {
					const int rangeAnchor = currentDocument->state.lastClickedEntry;
					currentDocument->state.ClearSelection();
					currentDocument->state.SelectRange(rangeAnchor, i);
					currentDocument->state.lastClickedEntry = rangeAnchor;
				} else {
					currentDocument->state.SelectSingle(i);
				}
				if (!e.isDir)
					currentDocument->state.LoadFileView(i);

				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !e.isDir &&
					DiskExplorerSelectedIsXEX()) {
					DiskExplorerRunSelectedXEX();
				} else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && e.isDir) {
					if (e.name == ".." && currentDocument->state.currentDir == ATDiskFSKey::None
						&& currentDocument->state.pBlockDevice) {
						// At root of a partition — return to partition list
						ImGui::PopID();
						currentDocument->state.ReturnToPartitionList();
						break;
					}
					// Capture key before NavigateTo invalidates entries
					ATDiskFSKey navKey = (e.name == "..") ?
						currentDocument->state.pFS->GetParentDirectory(currentDocument->state.currentDir) : e.key;
					ImGui::PopID();
					currentDocument->state.NavigateTo(navKey);
					break;  // entries invalidated
				}
			}

			// Context menu
			bool deleted = false;
			if (currentDocument->actionsRequested && i == currentDocument->state.selectedEntry) {
				ImGui::OpenPopup("ItemActions"); currentDocument->actionsRequested = false;
			}
			if (ImGui::BeginPopupContextItem("ItemActions")) {
				if (!currentDocument->state.selected[i]) {
					currentDocument->state.SelectSingle(i);
					if (!e.isDir) currentDocument->state.LoadFileView(i);
				}
				// Compute selection state — matches Windows enable/disable logic
				// (uidiskexplorer.cpp lines 1596-1629)
				bool anyFiles = false, anyDirs = false, anySpecials = false;
				int selCount = 0;
				for (int j = 0; j < (int)currentDocument->state.entries.size(); ++j) {
					if (j < (int)currentDocument->state.selected.size() && currentDocument->state.selected[j]) {
						auto &ej = currentDocument->state.entries[j];
						if (ej.name == ".." || ej.key == ATDiskFSKey::None)
							anySpecials = true;
						else if (ej.isDir)
							anyDirs = true;
						else
							anyFiles = true;
						++selCount;
					}
				}
				const bool anyItemsSelected = !anySpecials && selCount > 0;
				const bool singleItemSelected = !anySpecials && selCount == 1;
				const bool singleFileSelected = !anyDirs && !anySpecials && singleItemSelected;
				const bool writable = !currentDocument->state.readOnly;

				// Context menu order matches Windows IDR_DISK_EXPLORER_CONTEXT_MENU:
				// View | sep | New Folder, Rename, Delete | sep | Import, Import Text | sep | Export, Export Text

				if (ImGui::MenuItem("Run", nullptr, false, DiskExplorerSelectedIsXEX()))
					DiskExplorerRunSelectedXEX();
				if (ImGui::MenuItem("Open in XEX Explorer", nullptr, false, DiskExplorerSelectedIsXEX()))
					DiskExplorerOpenSelectedXEX();
				if (ImGui::MenuItem("Copy details", nullptr, false, anyItemsSelected)) {
					VDStringA details;
					for (size_t j = 0; j < currentDocument->state.entries.size(); ++j) {
						if (!currentDocument->state.selected[j]) continue;
						const auto& entry = currentDocument->state.entries[j];
						details.append_sprintf("%s\t%u bytes\t%u sectors\n", entry.name.c_str(), entry.bytes, entry.sectors);
					}
					ImGui::SetClipboardText(details.c_str());
				}
				// View — only enabled for single file selection (matches Windows)
				ImGui::BeginDisabled(!singleFileSelected);
				if (ImGui::MenuItem("View")) {
					currentDocument->state.selectedEntry = i;
					currentDocument->state.LoadFileView(i);
				}
				ImGui::EndDisabled();
				ImGui::BeginDisabled(!singleItemSelected);
				if (ImGui::MenuItem("File Information") && singleItemSelected) {
					currentDocument->state.fileInfoKey = e.key;
					currentDocument->state.showFileInfo = true;
				}
				ImGui::EndDisabled();

				ImGui::Separator();

				// New Folder / Rename / Delete
				ImGui::BeginDisabled(!writable);
				if (ImGui::MenuItem("New Folder..."))
					currentDocument->wantNewDir = true;
				ImGui::EndDisabled();

				ImGui::BeginDisabled(!singleItemSelected || !writable);
				if (ImGui::MenuItem("Rename...") && singleItemSelected && writable) {
					// Find the single selected non-special entry
					for (int j = 0; j < (int)currentDocument->state.entries.size(); ++j) {
						if (j < (int)currentDocument->state.selected.size() && currentDocument->state.selected[j]
							&& currentDocument->state.entries[j].name != ".."
							&& currentDocument->state.entries[j].key != ATDiskFSKey::None) {
							currentDocument->renameKey = currentDocument->state.entries[j].key;
							strncpy(currentDocument->renameBuf, currentDocument->state.entries[j].name.c_str(), sizeof(currentDocument->renameBuf) - 1);
							currentDocument->renameBuf[sizeof(currentDocument->renameBuf) - 1] = 0;
							currentDocument->wantRename = true;
							break;
						}
					}
				}
				ImGui::EndDisabled();

				ImGui::BeginDisabled(!anyItemsSelected || !writable);
				if (ImGui::MenuItem("Delete") && anyItemsSelected && writable && currentDocument->state.pFS) {
					// Delete all selected non-special entries
					for (int j = (int)currentDocument->state.entries.size() - 1; j >= 0; --j) {
						if (j < (int)currentDocument->state.selected.size() && currentDocument->state.selected[j]
							&& j < (int)currentDocument->state.entries.size()) {
							auto &ej = currentDocument->state.entries[j];
							if (ej.name != ".." && ej.key != ATDiskFSKey::None) {
								try {
									currentDocument->state.pFS->DeleteFile(ej.key);
								} catch (const MyError &err) {
									currentDocument->state.statusMsg.sprintf("Delete '%s' failed: %s", ej.name.c_str(), err.c_str());
								}
							}
						}
					}
					currentDocument->state.OnFSModified();
					deleted = true;
				}
				ImGui::EndDisabled();

				ImGui::Separator();

				// Import File / Import File as Text
				ImGui::BeginDisabled(!writable);
				if (ImGui::MenuItem("Import File...")) {
					static const SDL_DialogFileFilter kFilters[] = { { "All Files", "*" } };
					ATUIExplorerFileDialog(*currentDocument, false, 'dexp', DiskExplorerImportCallback, nullptr, window, kFilters, 1, false);
				}
				if (ImGui::MenuItem("Import File as Text...")) {
					static const SDL_DialogFileFilter kFilters[] = { { "All Files", "*" } };
					ATUIExplorerFileDialog(*currentDocument, false, 'dexp', DiskExplorerImportTextCallback, nullptr, window, kFilters, 1, false);
				}
				ImGui::EndDisabled();

				ImGui::Separator();

				// Export File / Export File as Text — disabled if dirs in selection (matches Windows)
				{
					// Count selected exportable files
					int selFileCount = 0;
					for (int j = 0; j < (int)currentDocument->state.entries.size(); ++j) {
						if (j < (int)currentDocument->state.selected.size() && currentDocument->state.selected[j]
							&& !currentDocument->state.entries[j].isDir
							&& currentDocument->state.entries[j].name != ".."
							&& currentDocument->state.entries[j].key != ATDiskFSKey::None)
							++selFileCount;
					}
					bool canExport = !anyDirs && anyItemsSelected;

					ImGui::BeginDisabled(!canExport);
					if (ImGui::MenuItem("Export File...") && canExport) {
						if (selFileCount > 1) {
							currentDocument->diskMultiExportEntries.clear();
							currentDocument->diskMultiExportAsText = false;
							for (int j = 0; j < (int)currentDocument->state.entries.size(); ++j) {
								if (j < (int)currentDocument->state.selected.size() && currentDocument->state.selected[j]
									&& !currentDocument->state.entries[j].isDir
									&& currentDocument->state.entries[j].name != ".."
									&& currentDocument->state.entries[j].key != ATDiskFSKey::None) {
									auto &ej = currentDocument->state.entries[j];
									currentDocument->diskMultiExportEntries.push_back({ej.key, ej.name, ej.dateValid, ej.date});
								}
							}
							ATUIExplorerFolderDialog(*currentDocument, 'dexp',
								DiskExplorerMultiExportCallback, nullptr,
								window);
						} else {
							currentDocument->diskExportInfo.key = e.key;
							currentDocument->diskExportInfo.dateValid = e.dateValid;
							currentDocument->diskExportInfo.date = e.date;
							static const SDL_DialogFileFilter kFilters[] = { { "All Files", "*" } };
							ATUIExplorerFileDialog(*currentDocument, true, 'dexp', DiskExplorerExportCallback, nullptr, window, kFilters, 1);
						}
					}
					if (ImGui::MenuItem("Export File as Text...") && canExport) {
						if (selFileCount > 1) {
							currentDocument->diskMultiExportEntries.clear();
							currentDocument->diskMultiExportAsText = true;
							for (int j = 0; j < (int)currentDocument->state.entries.size(); ++j) {
								if (j < (int)currentDocument->state.selected.size() && currentDocument->state.selected[j]
									&& !currentDocument->state.entries[j].isDir
									&& currentDocument->state.entries[j].name != ".."
									&& currentDocument->state.entries[j].key != ATDiskFSKey::None) {
									auto &ej = currentDocument->state.entries[j];
									currentDocument->diskMultiExportEntries.push_back({ej.key, ej.name, ej.dateValid, ej.date});
								}
							}
							ATUIExplorerFolderDialog(*currentDocument, 'dexp',
								DiskExplorerMultiExportCallback, nullptr,
								window);
						} else {
							currentDocument->diskExportTextInfo.key = e.key;
							currentDocument->diskExportTextInfo.dateValid = e.dateValid;
							currentDocument->diskExportTextInfo.date = e.date;
							static const SDL_DialogFileFilter kFilters[] = { { "All Files", "*" } };
							ATUIExplorerFileDialog(*currentDocument, true, 'dexp', DiskExplorerExportTextCallback, nullptr, window, kFilters, 1);
						}
					}
					ImGui::EndDisabled();
				}
				ImGui::EndPopup();
			}
			if (deleted) {
				ImGui::PopID();
				break;  // entries invalidated by OnFSModified, restart next frame
			}

			// Sectors
			ImGui::TableNextColumn();
			if (!e.isDir)
				ATUIExplorerNumber("%u", e.sectors);

			// Size
			ImGui::TableNextColumn();
			if (!e.isDir)
				ATUIExplorerNumber("%u", e.bytes);

			// Date
			ImGui::TableNextColumn();
			if (e.dateValid)
				ImGui::Text("%02u/%02u/%02u %02u:%02u:%02u",
					e.date.mMonth, e.date.mDay, e.date.mYear % 100,
					e.date.mHour, e.date.mMinute, e.date.mSecond);

			ImGui::PopID();
		}

		ImGui::EndTable();
	}

	// New directory popup -- currentDocument->wantNewDir is set in the context menu above
	if (currentDocument->wantNewDir) {
		ImGui::OpenPopup("NewDirPopup");
		currentDocument->wantNewDir = false;
	}

	// Rename popup — currentDocument->wantRename is set in the context menu above
	if (currentDocument->wantRename) {
		ImGui::OpenPopup("RenamePopup");
		currentDocument->wantRename = false;
	}
	if (ImGui::BeginPopup("RenamePopup")) {
		if (!currentDocument->state.pFS) {
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		} else {
			ImGui::Text("New name:");
			ImGui::SetNextItemWidth(200);
			bool submitted = ImGui::InputText("##rename", currentDocument->renameBuf, sizeof(currentDocument->renameBuf),
				ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SameLine();
			if ((ImGui::Button("OK") || submitted) && currentDocument->renameBuf[0] &&
				currentDocument->renameKey != ATDiskFSKey::None) {
				try {
					currentDocument->state.pFS->RenameFile(currentDocument->renameKey, currentDocument->renameBuf);
					currentDocument->state.OnFSModified();
					ImGui::CloseCurrentPopup();
				} catch (const MyError &err) {
					currentDocument->state.statusMsg.sprintf("Rename failed: %s", err.c_str());
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}

	// File viewer

	ATUIExplorerSplitter(*currentDocument, splitHeight);

	static const char *kViewModeNames[] = { "ATASCII", "ATASCII (wrap)", "ATASCII (columns)", "Hex dump", "Executable", "MAC/65", "Atari BASIC", "Syn assembler", "6502 disassembly", "ASCII (7-bit)" };
	if (currentDocument->state.selectedEntry >= 0 &&
		currentDocument->state.selectedEntry < (int)currentDocument->state.entries.size() &&
		!currentDocument->state.entries[currentDocument->state.selectedEntry].isDir) {
		ImGui::Text("%s   %u bytes   %d selected",
			currentDocument->state.entries[currentDocument->state.selectedEntry].name.c_str(),
			(unsigned)currentDocument->state.viewData.size(), currentDocument->state.GetSelectionCount());
	}
	ImGui::Text("Preview");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(210);
	const int currentViewMode = std::max(0, std::min((int)kDEView_ASCII, (int)currentDocument->state.viewMode));
	VDStringA currentViewLabel;
	if (currentViewMode == (int)kDEView_TextGR0)
		currentViewLabel.sprintf("ATASCII (%d columns)", ATUIGetTextColumns());
	else if (currentViewMode == (int)kDEView_ASCII)
		currentViewLabel.sprintf("ASCII (%d columns)", ATUIGetTextColumns());
	else
		currentViewLabel = kViewModeNames[currentViewMode];
	if (ImGui::BeginCombo("##DiskPreviewMode", currentViewLabel.c_str())) {
		for (int i = 0; i <= (int)kDEView_ASCII; ++i) {
			const bool selected = i == currentViewMode;
			VDStringA itemLabel;
			if (i == (int)kDEView_TextGR0)
				itemLabel.sprintf("ATASCII (%d columns)", ATUIGetTextColumns());
			else
				itemLabel = kViewModeNames[i];
			if (ImGui::Selectable(itemLabel.c_str(), selected)) {
				currentDocument->state.viewMode = (DiskExplorerViewMode)i;
				currentDocument->state.FormatView();
				currentDocument->state.viewValid = !currentDocument->state.viewData.empty();
			}
			if (selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	if (currentDocument->state.viewValid && (currentDocument->state.viewMode == kDEView_Text ||
		currentDocument->state.viewMode == kDEView_TextWrap || currentDocument->state.viewMode == kDEView_TextGR0)) {
		ImGui::SameLine();
		ImGui::TextDisabled("ATASCII: high-bit bytes are inverse video");
	}
	if (currentDocument->state.viewValid && (currentDocument->state.viewMode == kDEView_TextGR0 ||
		currentDocument->state.viewMode == kDEView_ASCII)) {
		ATUIRenderTextColumnControls("DiskFilePreviewColumns");
	}

	if (!currentDocument->state.statusMsg.empty())
		ImGui::TextWrapped("%s", currentDocument->state.statusMsg.c_str());

	if (currentDocument->state.viewValid) {
		const bool isATASCIIView = currentDocument->state.viewMode == kDEView_Text
			|| currentDocument->state.viewMode == kDEView_TextWrap
			|| currentDocument->state.viewMode == kDEView_TextGR0;
		const bool isASCIIView = currentDocument->state.viewMode == kDEView_ASCII;
		bool wrap = (currentDocument->state.viewMode == kDEView_TextWrap);
		ImGui::BeginChild("FileView", ImVec2(0, 0), ImGuiChildFlags_Borders,
			wrap ? 0 : ImGuiWindowFlags_HorizontalScrollbar);
		if (isATASCIIView) {
			int columns = 0;
			if (currentDocument->state.viewMode == kDEView_TextGR0)
				columns = ATUIGetTextColumns();
			else if (wrap)
				columns = std::max(1, (int)(ImGui::GetContentRegionAvail().x / 16.0f));

			ATUIRenderATASCII(currentDocument->state.viewData.data(),
				currentDocument->state.viewData.size(), columns);
		} else if (currentDocument->state.viewMode == kDEView_Hex) {
			ATUIRenderHexDump(currentDocument->state.viewData.data(), currentDocument->state.viewData.size(), 0);
		} else if (isASCIIView) {
			ATUIRenderASCII(currentDocument->state.viewData.data(), currentDocument->state.viewData.size(),
				ATUIGetTextColumns());
		} else {
			ImGui::PushFont(ATUIGetFontMono());
			if (wrap)
				ImGui::TextWrapped("%s", currentDocument->state.viewText.c_str());
			else
				ImGui::TextUnformatted(currentDocument->state.viewText.c_str());
			ImGui::PopFont();
		}
		ImGui::EndChild();
	} else {
		ImGui::BeginChild("FileView", ImVec2(0, 0), ImGuiChildFlags_Borders);
		ImGui::TextDisabled("Select a file to view its contents.");
		ImGui::EndChild();
	}

	ImGui::End();
	DiskExplorerRenderInspectionWindows();
}

void ATUIRequestDiskExplorer(SDL_Window *window) {
	static const SDL_DialogFileFilter filters[] = { { "Disk images", "atr;xfd;dcm;pro;atx;gz;zip;atz" }, { "All files", "*" } };
	ATUIExplorerFileDialog(openRequest, false, 'disk', QueueOpen, nullptr, window, filters, 2, true);
}

void ATUIRenderDiskExplorer(ATSimulator &sim, ATUIState &state, SDL_Window *window) {
	openRequest.Drain();
	std::vector<std::string> paths;
	paths.swap(pendingDocuments);
	for (const auto& path : paths) DiskExplorerDoOpen(path.c_str());
	// Snapshot: opening another document while drawing must not invalidate iteration.
	const auto snapshot = documents;
	for (const auto& doc : snapshot) {
		if (!doc->open) continue;
		ATUIExplorerScope<DiskExplorerDocument> scope(currentDocument, doc.get());
		// Keep an opened image inspectable after its drive is ejected/replaced,
		// but never send changes to the replacement disk in that drive.
		if (doc->state.pDiskInterface && doc->state.pDiskInterface->GetDiskImage() != doc->state.pImage) {
			doc->state.pDiskInterface = nullptr;
			doc->state.mbAutoFlush = false;
			doc->state.modified = doc->state.pImage && doc->state.pImage->IsDirty();
			doc->source += " (detached from drive)";
		}
		doc->Drain();
		if (!doc->open) continue;
		ImGui::BeginDisabled(doc->dialogPending);
		RenderDocument(sim, state, window);
		ImGui::EndDisabled();
		if (doc->requestClose) { doc->requestClose = false; doc->open = false; }
		DiskExplorerClosePrompt(window);
	}
	documents.erase(std::remove_if(documents.begin(), documents.end(),
		[](const auto& doc) { return !doc->open; }), documents.end());
	state.showDiskExplorer = !documents.empty() || openRequest.dialogPending;
}

bool ATUIDiskExplorerHasUnsavedChanges() {
	for (const auto& doc : documents) if (DiskExplorerNeedsSave(*doc)) return true;
	return false;
}

void ATUIOpenDiskExplorerFile(const char *path) {
	if (path && *path) pendingDocuments.emplace_back(path);
}

void ATUICloseDiskExplorers() {
	for (const auto& doc : documents) doc->requestClose = true;
}
void ATUIShutdownDiskExplorers() {
	for (const auto& doc : documents) {
		ATUIExplorerScope<DiskExplorerDocument> scope(currentDocument, doc.get());
		DiskExplorerSaveSettings();
	}
	documents.clear();
}
