//	AltirraSDL - Tools dialog (split from ui_tools.cpp, Phase 2k)
//
//	Phase 3d note: a 4-way split was planned (convert / import / export /
//	main) but every helper in this file references g_diskExplorer (the
//	~640-line DiskExplorerState struct) and the import/export callbacks
//	are tightly interleaved with the in-place file dialog state.  Promoting
//	the entire DiskExplorerState definition to a header — the only way to
//	let the helpers be moved verbatim — would relocate ~640 lines of
//	struct + member functions into a public-ish header for very little
//	maintainability win.  Like ui/media/videowriter_sdl3.cpp (3e), this
//	file is intentionally left intact.

#include <stdafx.h>
#include <algorithm>
#include <string>
#include <mutex>
#include <thread>
#include <vector>
#include <cstring>
#include <cstdio>
#include <imgui.h>
#include <SDL3/SDL.h>
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
	kDEView_TextGR0,        // Text: wrap to GR.0 screen (38 columns)
	kDEView_Hex,            // Hex dump
	kDEView_Executable,     // Executable
	kDEView_MAC65,          // MAC/65
};

static struct DiskExplorerState {
	vdrefptr<IATDiskImage> pImage;
	IATDiskFS *pFS = nullptr;
	bool readOnly = true;
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
		if (from > to) std::swap(from, to);
		for (int i = from; i <= to && i < (int)selected.size(); ++i)
			selected[i] = true;
		selectedEntry = to;
		lastClickedEntry = to;
	}

	// File viewer
	DiskExplorerViewMode viewMode = kDEView_TextGR0;
	vdfastvector<uint8> viewData;
	VDStringA viewText;
	bool viewValid = false;

	// Path breadcrumb
	VDStringA pathStr;

	// Status
	VDStringA statusMsg;

	~DiskExplorerState() { delete pFS; }

	void Reset() {
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
		pathStr = "/";
		statusMsg.clear();
	}

	void RefreshDirectory() {
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

			const int lineWidth = (viewMode == kDEView_TextGR0) ? 38 : INT_MAX;
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

		default:
			viewText = "(unknown view mode)";
			break;
		}
	}
} g_diskExplorer;

// Thread-safe pending path for disk explorer open (file dialog callback -> main thread)
static std::mutex g_diskExplorerMutex;
static std::string g_diskExplorerPendingOpen;
static std::string g_diskExplorerPendingImport;

static void DiskExplorerOpenCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	std::lock_guard<std::mutex> lock(g_diskExplorerMutex);
	g_diskExplorerPendingOpen = filelist[0];
}

static void DiskExplorerDoOpen(const char *utf8path) {
	try {
		VDStringW wpath = VDTextU8ToW(utf8path, -1);
		g_diskExplorer.Reset();

		IATDiskImage *pImageRaw = nullptr;
		ATLoadDiskImage(wpath.c_str(), &pImageRaw);
		g_diskExplorer.pImage = pImageRaw;
		if (pImageRaw) pImageRaw->Release();

		g_diskExplorer.pFS = ATDiskMountImage(g_diskExplorer.pImage, true);
		if (!g_diskExplorer.pFS) {
			g_diskExplorer.statusMsg = "Unable to detect filesystem on disk image.";
			g_diskExplorer.pImage = nullptr;
			return;
		}
		g_diskExplorer.pFS->SetStrictNameChecking(g_diskExplorer.mbStrictFilenames);
		g_diskExplorer.readOnly = true;
		g_diskExplorer.NavigateTo(ATDiskFSKey::None);
	} catch (const MyError &e) {
		g_diskExplorer.Reset();
		g_diskExplorer.statusMsg.sprintf("Failed to open: %s", e.c_str());
	}
}


// Open disk explorer for a mounted drive's disk image (called from disk context menu).
void ATUIOpenDiskExplorerForDrive(int driveIdx, bool writable, bool autoFlush) {
	ATDiskInterface& di = g_sim.GetDiskInterface(driveIdx);
	IATDiskImage *img = di.GetDiskImage();
	if (!img)
		return;

	g_diskExplorer.Reset();
	g_diskExplorer.pImage = img;
	g_diskExplorer.pDiskInterface = &di;
	g_diskExplorer.mbAutoFlush = autoFlush;

	bool readOnly = !writable;
	g_diskExplorer.pFS = ATDiskMountImage(g_diskExplorer.pImage, readOnly);
	if (!g_diskExplorer.pFS) {
		g_diskExplorer.statusMsg = "Unable to detect filesystem on disk image.";
		g_diskExplorer.pImage = nullptr;
		g_diskExplorer.pDiskInterface = nullptr;
		return;
	}
	g_diskExplorer.pFS->SetStrictNameChecking(g_diskExplorer.mbStrictFilenames);
	g_diskExplorer.readOnly = readOnly;

	// Validate filesystem before allowing writes (matches Windows MountFS)
	if (writable) {
		if (img->IsUpdatable() && g_diskExplorer.pFS->IsReadOnly()) {
			g_diskExplorer.readOnly = true;
			g_diskExplorer.statusMsg = "This disk format is only supported in read-only mode.";
		} else {
			g_diskExplorer.ValidateForWrites();
		}
	}

	g_diskExplorer.NavigateTo(ATDiskFSKey::None);
}

// Open disk explorer for a block device (shows partition list).
void ATUIOpenDiskExplorerForBlockDevice(IATBlockDevice *dev) {
	if (!dev)
		return;

	g_diskExplorer.Reset();
	g_diskExplorer.pBlockDevice = dev;
	g_diskExplorer.readOnly = dev->IsReadOnly();
	g_diskExplorer.RefreshPartitions();
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
	bool strict = g_diskExplorer.mbStrictFilenames;
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
			auto fileKey = g_diskExplorer.pFS->WriteFile(
				g_diskExplorer.currentDir, filename, data, len);
			if (date)
				g_diskExplorer.pFS->SetFileTimestamp(fileKey, *date);
			return;
		} catch (const ATDiskFSException &e) {
			if (e.GetErrorCode() != kATDiskFSError_InvalidFileName &&
				e.GetErrorCode() != kATDiskFSError_FileExists)
				throw;
			if (!g_diskExplorer.mbAdjustFilenames)
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
				if (pos < 0 && g_diskExplorer.mbStrictFilenames) {
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
static ImVec2 g_diskExplorerWinPos = {0, 0};
static ImVec2 g_diskExplorerWinSize = {0, 0};

// Handle a file drop into the Disk Explorer — returns true if the drop was
// consumed (explorer is open, writable, cursor is over window, and filesystem is mounted).
bool ATUIDiskExplorerHandleDrop(const char *utf8path, float dropX, float dropY) {
	if (!g_diskExplorer.pFS || g_diskExplorer.readOnly)
		return false;

	// Check if drop position is within the Disk Explorer window
	if (dropX < g_diskExplorerWinPos.x || dropY < g_diskExplorerWinPos.y
		|| dropX > g_diskExplorerWinPos.x + g_diskExplorerWinSize.x
		|| dropY > g_diskExplorerWinPos.y + g_diskExplorerWinSize.y)
		return false;

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
		g_diskExplorer.OnFSModified();
	} catch (const MyError &e) {
		g_diskExplorer.statusMsg.sprintf("Drop import failed: %s", e.c_str());
	}
	return true;
}

bool ATUIDiskExplorerGetDropRect(ImVec2 &pos, ImVec2 &size) {
	if (!g_diskExplorer.pFS || g_diskExplorer.readOnly
		|| g_diskExplorerWinSize.x <= 0 || g_diskExplorerWinSize.y <= 0)
		return false;
	pos = g_diskExplorerWinPos;
	size = g_diskExplorerWinSize;
	return true;
}

static void DiskExplorerImportCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	std::lock_guard<std::mutex> lock(g_diskExplorerMutex);
	g_diskExplorerPendingImport = filelist[0];
}

static void DiskExplorerDoImport(const char *utf8path) {
	if (!g_diskExplorer.pFS) return;

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
		g_diskExplorer.OnFSModified();
	} catch (const MyError &e) {
		g_diskExplorer.statusMsg.sprintf("Import failed: %s", e.c_str());
	}
}

// Export stores the file key and date info at the time the dialog is opened,
// so it remains valid even if the user navigates to a different directory
// while the save dialog is showing.
struct DiskExportInfo {
	ATDiskFSKey key;
	bool dateValid;
	VDExpandedDate date;
};
static DiskExportInfo g_diskExportInfo;
static std::string g_diskExplorerPendingExport;

static void DiskExplorerExportCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	std::lock_guard<std::mutex> lock(g_diskExplorerMutex);
	g_diskExplorerPendingExport = filelist[0];
}

static void DiskExplorerDoExport(const char *utf8path) {
	if (!g_diskExplorer.pFS) return;

	try {
		vdfastvector<uint8> buf;
		g_diskExplorer.pFS->ReadFile(g_diskExportInfo.key, buf);

		VDStringW wpath = VDTextU8ToW(utf8path, -1);
		VDFile f(wpath.c_str(), nsVDFile::kWrite | nsVDFile::kCreateAlways | nsVDFile::kSequential);
		f.write(buf.data(), (long)buf.size());

		// Preserve file timestamp if available
		if (g_diskExportInfo.dateValid) {
			try {
				f.setCreationTime(VDDateFromLocalDate(g_diskExportInfo.date));
			} catch (...) {
				// Timestamp preservation is best-effort
			}
		}
	} catch (const MyError &e) {
		g_diskExplorer.statusMsg.sprintf("Export failed: %s", e.c_str());
	}
}

// Multi-file export: export all selected files to a folder
struct DiskMultiExportEntry {
	ATDiskFSKey key;
	VDStringA name;
	bool dateValid;
	VDExpandedDate date;
};
static std::vector<DiskMultiExportEntry> g_diskMultiExportEntries;
static std::string g_diskExplorerPendingMultiExport;
static bool g_diskMultiExportAsText = false;

static void DiskExplorerMultiExportCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	std::lock_guard<std::mutex> lock(g_diskExplorerMutex);
	g_diskExplorerPendingMultiExport = filelist[0];
}

static void DiskExplorerDoMultiExport(const char *utf8folder) {
	if (!g_diskExplorer.pFS) return;
	int exported = 0;
	for (auto &me : g_diskMultiExportEntries) {
		try {
			vdfastvector<uint8> buf;
			g_diskExplorer.pFS->ReadFile(me.key, buf);

			if (g_diskMultiExportAsText)
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
			g_diskExplorer.statusMsg.sprintf("Export '%s' failed: %s", me.name.c_str(), e.c_str());
		}
	}
	if (g_diskExplorer.statusMsg.empty() || exported > 0)
		g_diskExplorer.statusMsg.sprintf("Exported %d file(s).", exported);
	g_diskMultiExportEntries.clear();
}

// Import as text: read host file, convert CR/LF -> Atari 0x9B, write to disk
static std::string g_diskExplorerPendingImportText;

static void DiskExplorerImportTextCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	std::lock_guard<std::mutex> lock(g_diskExplorerMutex);
	g_diskExplorerPendingImportText = filelist[0];
}

static void DiskExplorerDoImportText(const char *utf8path) {
	if (!g_diskExplorer.pFS) return;

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
		g_diskExplorer.OnFSModified();
	} catch (const MyError &e) {
		g_diskExplorer.statusMsg.sprintf("Import as text failed: %s", e.c_str());
	}
}

// Export as text: read Atari file, convert 0x9B -> CR/LF, save to host
static std::string g_diskExplorerPendingExportText;
static DiskExportInfo g_diskExportTextInfo;

static void DiskExplorerExportTextCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	std::lock_guard<std::mutex> lock(g_diskExplorerMutex);
	g_diskExplorerPendingExportText = filelist[0];
}

static void DiskExplorerDoExportText(const char *utf8path) {
	if (!g_diskExplorer.pFS) return;

	try {
		vdfastvector<uint8> buf;
		g_diskExplorer.pFS->ReadFile(g_diskExportTextInfo.key, buf);

		// Convert Atari EOL to host CR/LF
		ConvertAtariToHost(buf);

		VDStringW wpath = VDTextU8ToW(utf8path, -1);
		VDFile f(wpath.c_str(), nsVDFile::kWrite | nsVDFile::kCreateAlways | nsVDFile::kSequential);
		f.write(buf.data(), (long)buf.size());

		if (g_diskExportTextInfo.dateValid) {
			try {
				f.setCreationTime(VDDateFromLocalDate(g_diskExportTextInfo.date));
			} catch (...) {
			}
		}
	} catch (const MyError &e) {
		g_diskExplorer.statusMsg.sprintf("Export as text failed: %s", e.c_str());
	}
}

// Partition import: import a disk image file into a partition
static int g_diskExplorerPartitionImportIdx = -1;
static std::string g_diskExplorerPendingPartitionImport;

static void DiskExplorerPartitionImportCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	std::lock_guard<std::mutex> lock(g_diskExplorerMutex);
	g_diskExplorerPendingPartitionImport = filelist[0];
}

static void DiskExplorerDoPartitionImport(const char *utf8path) {
	if (!g_diskExplorer.pBlockDevice) return;
	int idx = g_diskExplorerPartitionImportIdx;
	if (idx < 0 || idx >= (int)g_diskExplorer.partitions.size()) return;

	try {
		const ATPartitionInfo &pi = g_diskExplorer.partitions[idx];
		vdrefptr<ATPartitionDiskView> pdview(new ATPartitionDiskView(*g_diskExplorer.pBlockDevice, pi));

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
			g_diskExplorer.statusMsg = msg;
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

		g_diskExplorer.statusMsg = "Disk image imported to partition successfully.";
	} catch (const MyError &e) {
		g_diskExplorer.statusMsg.sprintf("Partition import failed: %s", e.c_str());
	}
}

// Partition export: export a partition as a disk image file
static int g_diskExplorerPartitionExportIdx = -1;
static std::string g_diskExplorerPendingPartitionExport;

static void DiskExplorerPartitionExportCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	std::lock_guard<std::mutex> lock(g_diskExplorerMutex);
	g_diskExplorerPendingPartitionExport = filelist[0];
}

static void DiskExplorerDoPartitionExport(const char *utf8path) {
	if (!g_diskExplorer.pBlockDevice) return;
	int idx = g_diskExplorerPartitionExportIdx;
	if (idx < 0 || idx >= (int)g_diskExplorer.partitions.size()) return;

	try {
		const ATPartitionInfo &pi = g_diskExplorer.partitions[idx];
		vdrefptr<ATPartitionDiskView> pdview(new ATPartitionDiskView(*g_diskExplorer.pBlockDevice, pi));
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

		g_diskExplorer.statusMsg = "Partition exported as disk image successfully.";
	} catch (const MyError &e) {
		g_diskExplorer.statusMsg.sprintf("Partition export failed: %s", e.c_str());
	}
}

static bool g_diskExplorerSettingsLoaded = false;

static void DiskExplorerLoadSettings() {
	if (g_diskExplorerSettingsLoaded) return;
	g_diskExplorerSettingsLoaded = true;
	VDRegistryAppKey key("Settings", false);
	g_diskExplorer.mbStrictFilenames = key.getBool("Disk Explorer: Strict filenames", g_diskExplorer.mbStrictFilenames);
	g_diskExplorer.mbAdjustFilenames = key.getBool("Disk Explorer: Adjust filenames", g_diskExplorer.mbAdjustFilenames);
	int vm = key.getEnumInt("File Viewer: View mode", 6, (int)kDEView_TextGR0);
	g_diskExplorer.viewMode = (DiskExplorerViewMode)vm;
}

static void DiskExplorerSaveSettings() {
	VDRegistryAppKey key("Settings", true);
	key.setBool("Disk Explorer: Strict filenames", g_diskExplorer.mbStrictFilenames);
	key.setBool("Disk Explorer: Adjust filenames", g_diskExplorer.mbAdjustFilenames);
	key.setInt("File Viewer: View mode", (int)g_diskExplorer.viewMode);
}

void ATUIRenderDiskExplorer(ATSimulator &sim, ATUIState &state, SDL_Window *window) {
	DiskExplorerLoadSettings();

	// Process pending file dialog results on main thread
	{
		std::string openPath, importPath, importTextPath;
		{
			std::lock_guard<std::mutex> lock(g_diskExplorerMutex);
			openPath.swap(g_diskExplorerPendingOpen);
			importPath.swap(g_diskExplorerPendingImport);
			importTextPath.swap(g_diskExplorerPendingImportText);
		}
		if (!openPath.empty())
			DiskExplorerDoOpen(openPath.c_str());
		if (!importPath.empty())
			DiskExplorerDoImport(importPath.c_str());
		if (!importTextPath.empty())
			DiskExplorerDoImportText(importTextPath.c_str());

		std::string exportPath, exportTextPath, partImportPath, partExportPath;
		{
			std::lock_guard<std::mutex> lock(g_diskExplorerMutex);
			exportPath.swap(g_diskExplorerPendingExport);
			exportTextPath.swap(g_diskExplorerPendingExportText);
			partImportPath.swap(g_diskExplorerPendingPartitionImport);
			partExportPath.swap(g_diskExplorerPendingPartitionExport);
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
			std::lock_guard<std::mutex> lock(g_diskExplorerMutex);
			multiExportPath.swap(g_diskExplorerPendingMultiExport);
		}
		if (!multiExportPath.empty())
			DiskExplorerDoMultiExport(multiExportPath.c_str());
	}

	ImGui::SetNextWindowSize(ImVec2(850, 620), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (!ImGui::Begin("Disk Explorer", &state.showDiskExplorer, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar)) {
		g_diskExplorerWinPos = ImGui::GetWindowPos();
		g_diskExplorerWinSize = ImGui::GetWindowSize();
		ImGui::End();
		return;
	}

	// Save window rect for drop hit-testing
	g_diskExplorerWinPos = ImGui::GetWindowPos();
	g_diskExplorerWinSize = ImGui::GetWindowSize();

	if (ATUICheckEscClose()) {
		DiskExplorerSaveSettings();
		g_diskExplorer.Reset();
		g_diskExplorerWinSize = {0, 0};
		state.showDiskExplorer = false;
		ImGui::End();
		return;
	}

	if (!state.showDiskExplorer) {
		DiskExplorerSaveSettings();
		g_diskExplorer.Reset();
		g_diskExplorerWinSize = {0, 0};
		ImGui::End();
		return;
	}

	// Menu bar
	if (ImGui::BeginMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("Open Disk Image...")) {
				static const SDL_DialogFileFilter kFilters[] = {
					{ "Disk Images", "atr;xfd;dcm;pro;atx;gz;zip;atz" },
					{ "All Files", "*" },
				};
				SDL_ShowOpenFileDialog(DiskExplorerOpenCallback, nullptr, window, kFilters, 2, nullptr, false);
			}
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
							g_diskExplorer.Reset();
							g_diskExplorer.pImage = dimg;
							g_diskExplorer.pDiskInterface = &di;
							g_diskExplorer.pFS = ATDiskMountImage(dimg, true);
							if (!g_diskExplorer.pFS) {
								g_diskExplorer.statusMsg = "Unable to detect filesystem.";
								g_diskExplorer.pImage = nullptr;
								g_diskExplorer.pDiskInterface = nullptr;
							} else {
								g_diskExplorer.pFS->SetStrictNameChecking(g_diskExplorer.mbStrictFilenames);
								g_diskExplorer.readOnly = true;
								g_diskExplorer.NavigateTo(ATDiskFSKey::None);
							}
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
				g_diskExplorer.Reset();
				state.showDiskExplorer = false;
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("View")) {
			auto setMode = [](DiskExplorerViewMode m) {
				g_diskExplorer.viewMode = m;
				g_diskExplorer.FormatView();
				g_diskExplorer.viewValid = !g_diskExplorer.viewData.empty();
			};
			if (ImGui::MenuItem("Text: no line wrapping", nullptr, g_diskExplorer.viewMode == kDEView_Text))
				setMode(kDEView_Text);
			if (ImGui::MenuItem("Text: wrap to window", nullptr, g_diskExplorer.viewMode == kDEView_TextWrap))
				setMode(kDEView_TextWrap);
			if (ImGui::MenuItem("Text: wrap to GR.0 screen (38 columns)", nullptr, g_diskExplorer.viewMode == kDEView_TextGR0))
				setMode(kDEView_TextGR0);
			if (ImGui::MenuItem("Hex dump", nullptr, g_diskExplorer.viewMode == kDEView_Hex))
				setMode(kDEView_Hex);
			if (ImGui::MenuItem("Executable", nullptr, g_diskExplorer.viewMode == kDEView_Executable))
				setMode(kDEView_Executable);
			if (ImGui::MenuItem("MAC/65", nullptr, g_diskExplorer.viewMode == kDEView_MAC65))
				setMode(kDEView_MAC65);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Options")) {
			if (ImGui::BeginMenu("Filename Checking")) {
				if (ImGui::MenuItem("Strict", nullptr, g_diskExplorer.mbStrictFilenames)) {
					g_diskExplorer.mbStrictFilenames = true;
					if (g_diskExplorer.pFS)
						g_diskExplorer.pFS->SetStrictNameChecking(true);
				}
				if (ImGui::MenuItem("Relaxed", nullptr, !g_diskExplorer.mbStrictFilenames)) {
					g_diskExplorer.mbStrictFilenames = false;
					if (g_diskExplorer.pFS)
						g_diskExplorer.pFS->SetStrictNameChecking(false);
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Adjust Conflicting Filenames", nullptr, g_diskExplorer.mbAdjustFilenames))
					g_diskExplorer.mbAdjustFilenames = !g_diskExplorer.mbAdjustFilenames;
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}

	// Partition list view: when a block device is loaded but no filesystem is mounted
	if (!g_diskExplorer.pFS && g_diskExplorer.pBlockDevice) {
		ImGui::Text("Block Device - %d partition(s)", (int)g_diskExplorer.partitions.size());

		if (!g_diskExplorer.statusMsg.empty())
			ImGui::TextDisabled("%s", g_diskExplorer.statusMsg.c_str());

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

			for (int i = 0; i < (int)g_diskExplorer.partitions.size(); ++i) {
				const ATPartitionInfo &pi = g_diskExplorer.partitions[i];
				ImGui::TableNextRow();
				ImGui::PushID(i);

				ImGui::TableNextColumn();
				VDStringA name = VDTextWToA(pi.mName);
				bool selected = (g_diskExplorer.selectedPartition == i);
				if (ImGui::Selectable(name.c_str(), selected,
					ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
					g_diskExplorer.selectedPartition = i;

					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
						try {
							g_diskExplorer.OpenPartition(i);
						} catch (const MyError &e) {
							g_diskExplorer.statusMsg.sprintf("Open partition failed: %s", e.c_str());
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
							g_diskExplorer.OpenPartition(openIdx);
						} catch (const MyError &e) {
							g_diskExplorer.statusMsg.sprintf("Open partition failed: %s", e.c_str());
						}
						ImGui::End();
						return;
					}

					ImGui::Separator();

					bool writable = !g_diskExplorer.pBlockDevice->IsReadOnly() && !pi.mbWriteProtected;
					ImGui::BeginDisabled(!writable);
					if (ImGui::MenuItem("Import Disk Image...")) {
						g_diskExplorerPartitionImportIdx = i;
						static const SDL_DialogFileFilter kFilters[] = {
							{ "Disk Images", "atr;xfd;dcm;pro;atx;gz;zip;atz" },
							{ "All Files", "*" },
						};
						SDL_ShowOpenFileDialog(DiskExplorerPartitionImportCallback, nullptr, window, kFilters, 2, nullptr, false);
					}
					ImGui::EndDisabled();

					if (ImGui::MenuItem("Export Disk Image...")) {
						g_diskExplorerPartitionExportIdx = i;
						static const SDL_DialogFileFilter kFilters[] = {
							{ "Atari Disk Image", "atr" },
							{ "All Files", "*" },
						};
						SDL_ShowSaveFileDialog(DiskExplorerPartitionExportCallback, nullptr, window, kFilters, 2, nullptr);
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

	if (!g_diskExplorer.pFS) {
		ImGui::TextWrapped("No disk image loaded. Use File > Open Disk Image to browse a disk.");
		if (!g_diskExplorer.statusMsg.empty())
			ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", g_diskExplorer.statusMsg.c_str());
		ImGui::End();
		return;
	}

	// Toolbar
	ImGui::Text("Path: %s", g_diskExplorer.pathStr.c_str());
	ImGui::SameLine(ImGui::GetContentRegionAvail().x - 200);

	bool canWrite = !g_diskExplorer.readOnly;
	ImGui::BeginDisabled(!canWrite);
	if (ImGui::SmallButton("Import...")) {
		static const SDL_DialogFileFilter kFilters[] = { { "All Files", "*" } };
		SDL_ShowOpenFileDialog(DiskExplorerImportCallback, nullptr, window, kFilters, 1, nullptr, false);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();

	// Count selected exportable files for the toolbar Export button
	int toolbarSelFiles = 0;
	for (int j = 0; j < (int)g_diskExplorer.entries.size(); ++j) {
		if (j < (int)g_diskExplorer.selected.size() && g_diskExplorer.selected[j]
			&& !g_diskExplorer.entries[j].isDir
			&& g_diskExplorer.entries[j].name != ".."
			&& g_diskExplorer.entries[j].key != ATDiskFSKey::None)
			++toolbarSelFiles;
	}
	ImGui::BeginDisabled(toolbarSelFiles == 0);
	if (ImGui::SmallButton("Export...") && toolbarSelFiles > 0) {
		if (toolbarSelFiles > 1) {
			g_diskMultiExportEntries.clear();
			g_diskMultiExportAsText = false;
			for (int j = 0; j < (int)g_diskExplorer.entries.size(); ++j) {
				if (j < (int)g_diskExplorer.selected.size() && g_diskExplorer.selected[j]
					&& !g_diskExplorer.entries[j].isDir
					&& g_diskExplorer.entries[j].name != ".."
					&& g_diskExplorer.entries[j].key != ATDiskFSKey::None) {
					auto &ej = g_diskExplorer.entries[j];
					g_diskMultiExportEntries.push_back({ej.key, ej.name, ej.dateValid, ej.date});
				}
			}
			SDL_ShowOpenFolderDialog(DiskExplorerMultiExportCallback, nullptr, window, nullptr, false);
		} else {
			// Single file export
			for (int j = 0; j < (int)g_diskExplorer.entries.size(); ++j) {
				if (j < (int)g_diskExplorer.selected.size() && g_diskExplorer.selected[j]
					&& !g_diskExplorer.entries[j].isDir) {
					auto &ej = g_diskExplorer.entries[j];
					g_diskExportInfo.key = ej.key;
					g_diskExportInfo.dateValid = ej.dateValid;
					g_diskExportInfo.date = ej.date;
					break;
				}
			}
			static const SDL_DialogFileFilter kFilters[] = { { "All Files", "*" } };
			SDL_ShowSaveFileDialog(DiskExplorerExportCallback, nullptr, window, kFilters, 1, nullptr);
		}
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::BeginDisabled(!canWrite);
	if (ImGui::SmallButton("New Dir..."))
		ImGui::OpenPopup("NewDirPopup");
	ImGui::EndDisabled();

	// New Directory popup
	if (ImGui::BeginPopup("NewDirPopup")) {
		if (!g_diskExplorer.pFS) {
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		} else {
			static char newDirName[64] = {};
			ImGui::Text("Directory name:");
			ImGui::SetNextItemWidth(200);
			bool submitted = ImGui::InputText("##dirname", newDirName, sizeof(newDirName),
				ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SameLine();
			if ((ImGui::Button("OK") || submitted) && newDirName[0]) {
				try {
					g_diskExplorer.pFS->CreateDir(g_diskExplorer.currentDir, newDirName);
					g_diskExplorer.OnFSModified();
					newDirName[0] = 0;
					ImGui::CloseCurrentPopup();
				} catch (const MyError &err) {
					g_diskExplorer.statusMsg.sprintf("Create dir failed: %s", err.c_str());
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel")) {
				newDirName[0] = 0;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	ImGui::SameLine();
	bool rwToggle = !g_diskExplorer.readOnly;
	if (ImGui::Checkbox("Write", &rwToggle)) {
		g_diskExplorer.readOnly = !rwToggle;
		// Remount with new read/write mode — use pImage for standalone images,
		// pPartitionView for partition filesystems
		IATDiskImage *mountImage = g_diskExplorer.pImage
			? g_diskExplorer.pImage.get()
			: g_diskExplorer.pPartitionView.get();
		if (mountImage) {
			try {
				delete g_diskExplorer.pFS;
				g_diskExplorer.pFS = ATDiskMountImage(mountImage, g_diskExplorer.readOnly);
				if (g_diskExplorer.pFS)
					g_diskExplorer.pFS->SetStrictNameChecking(g_diskExplorer.mbStrictFilenames);

				// Check if writes are actually possible — matches Windows MountFS logic
				if (!g_diskExplorer.readOnly && g_diskExplorer.pFS) {
					if (mountImage->IsUpdatable() && g_diskExplorer.pFS->IsReadOnly()) {
						// Image format is updatable but FS can only be mounted read-only
						g_diskExplorer.readOnly = true;
						g_diskExplorer.statusMsg = "This disk format is only supported in read-only mode.";
					} else {
						g_diskExplorer.ValidateForWrites();
					}
				}

				g_diskExplorer.RefreshDirectory();
			} catch (const MyError &e) {
				g_diskExplorer.statusMsg.sprintf("Remount failed: %s", e.c_str());
			}
		}
	}

	ImGui::Separator();

	// Split: file list (top) and viewer (bottom)
	float listH = ImGui::GetContentRegionAvail().y * 0.55f;

	static bool s_wantRename = false;
	static bool s_wantNewDir = false;
	static ATDiskFSKey s_renameKey = ATDiskFSKey::None;
	static char s_renameBuf[64] = {};

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

		for (int i = 0; i < (int)g_diskExplorer.entries.size(); ++i) {
			auto &e = g_diskExplorer.entries[i];
			ImGui::TableNextRow();
			ImGui::PushID(i);

			// Name
			ImGui::TableNextColumn();
			bool isSel = (i < (int)g_diskExplorer.selected.size()) && g_diskExplorer.selected[i];
			VDStringA label;
			if (e.isDir)
				label.sprintf("[%s]", e.name.c_str());
			else
				label = e.name;

			if (ImGui::Selectable(label.c_str(), isSel,
				ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
				ImGuiIO &io = ImGui::GetIO();
				if (io.KeyCtrl) {
					g_diskExplorer.ToggleSelection(i);
				} else if (io.KeyShift && g_diskExplorer.lastClickedEntry >= 0) {
					g_diskExplorer.ClearSelection();
					g_diskExplorer.SelectRange(g_diskExplorer.lastClickedEntry, i);
				} else {
					g_diskExplorer.SelectSingle(i);
				}
				if (!e.isDir)
					g_diskExplorer.LoadFileView(i);

				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && e.isDir) {
					if (e.name == ".." && g_diskExplorer.currentDir == ATDiskFSKey::None
						&& g_diskExplorer.pBlockDevice) {
						// At root of a partition — return to partition list
						ImGui::PopID();
						g_diskExplorer.ReturnToPartitionList();
						break;
					}
					// Capture key before NavigateTo invalidates entries
					ATDiskFSKey navKey = (e.name == "..") ?
						g_diskExplorer.pFS->GetParentDirectory(g_diskExplorer.currentDir) : e.key;
					ImGui::PopID();
					g_diskExplorer.NavigateTo(navKey);
					break;  // entries invalidated
				}
			}

			// Context menu
			bool deleted = false;
			if (ImGui::BeginPopupContextItem()) {
				// Compute selection state — matches Windows enable/disable logic
				// (uidiskexplorer.cpp lines 1596-1629)
				bool anyFiles = false, anyDirs = false, anySpecials = false;
				int selCount = 0;
				for (int j = 0; j < (int)g_diskExplorer.entries.size(); ++j) {
					if (j < (int)g_diskExplorer.selected.size() && g_diskExplorer.selected[j]) {
						auto &ej = g_diskExplorer.entries[j];
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
				const bool writable = !g_diskExplorer.readOnly;

				// Context menu order matches Windows IDR_DISK_EXPLORER_CONTEXT_MENU:
				// View | sep | New Folder, Rename, Delete | sep | Import, Import Text | sep | Export, Export Text

				// View — only enabled for single file selection (matches Windows)
				ImGui::BeginDisabled(!singleFileSelected);
				if (ImGui::MenuItem("View")) {
					g_diskExplorer.selectedEntry = i;
					g_diskExplorer.LoadFileView(i);
				}
				ImGui::EndDisabled();

				ImGui::Separator();

				// New Folder / Rename / Delete
				ImGui::BeginDisabled(!writable);
				if (ImGui::MenuItem("New Folder..."))
					s_wantNewDir = true;
				ImGui::EndDisabled();

				ImGui::BeginDisabled(!singleItemSelected || !writable);
				if (ImGui::MenuItem("Rename...") && singleItemSelected && writable) {
					// Find the single selected non-special entry
					for (int j = 0; j < (int)g_diskExplorer.entries.size(); ++j) {
						if (j < (int)g_diskExplorer.selected.size() && g_diskExplorer.selected[j]
							&& g_diskExplorer.entries[j].name != ".."
							&& g_diskExplorer.entries[j].key != ATDiskFSKey::None) {
							s_renameKey = g_diskExplorer.entries[j].key;
							strncpy(s_renameBuf, g_diskExplorer.entries[j].name.c_str(), sizeof(s_renameBuf) - 1);
							s_renameBuf[sizeof(s_renameBuf) - 1] = 0;
							s_wantRename = true;
							break;
						}
					}
				}
				ImGui::EndDisabled();

				ImGui::BeginDisabled(!anyItemsSelected || !writable);
				if (ImGui::MenuItem("Delete") && anyItemsSelected && writable && g_diskExplorer.pFS) {
					// Delete all selected non-special entries
					for (int j = (int)g_diskExplorer.entries.size() - 1; j >= 0; --j) {
						if (j < (int)g_diskExplorer.selected.size() && g_diskExplorer.selected[j]
							&& j < (int)g_diskExplorer.entries.size()) {
							auto &ej = g_diskExplorer.entries[j];
							if (ej.name != ".." && ej.key != ATDiskFSKey::None) {
								try {
									g_diskExplorer.pFS->DeleteFile(ej.key);
								} catch (const MyError &err) {
									g_diskExplorer.statusMsg.sprintf("Delete '%s' failed: %s", ej.name.c_str(), err.c_str());
								}
							}
						}
					}
					g_diskExplorer.OnFSModified();
					deleted = true;
				}
				ImGui::EndDisabled();

				ImGui::Separator();

				// Import File / Import File as Text
				ImGui::BeginDisabled(!writable);
				if (ImGui::MenuItem("Import File...")) {
					static const SDL_DialogFileFilter kFilters[] = { { "All Files", "*" } };
					SDL_ShowOpenFileDialog(DiskExplorerImportCallback, nullptr, window, kFilters, 1, nullptr, false);
				}
				if (ImGui::MenuItem("Import File as Text...")) {
					static const SDL_DialogFileFilter kFilters[] = { { "All Files", "*" } };
					SDL_ShowOpenFileDialog(DiskExplorerImportTextCallback, nullptr, window, kFilters, 1, nullptr, false);
				}
				ImGui::EndDisabled();

				ImGui::Separator();

				// Export File / Export File as Text — disabled if dirs in selection (matches Windows)
				{
					// Count selected exportable files
					int selFileCount = 0;
					for (int j = 0; j < (int)g_diskExplorer.entries.size(); ++j) {
						if (j < (int)g_diskExplorer.selected.size() && g_diskExplorer.selected[j]
							&& !g_diskExplorer.entries[j].isDir
							&& g_diskExplorer.entries[j].name != ".."
							&& g_diskExplorer.entries[j].key != ATDiskFSKey::None)
							++selFileCount;
					}
					bool canExport = !anyDirs && anyItemsSelected;

					ImGui::BeginDisabled(!canExport);
					if (ImGui::MenuItem("Export File...") && canExport) {
						if (selFileCount > 1) {
							g_diskMultiExportEntries.clear();
							g_diskMultiExportAsText = false;
							for (int j = 0; j < (int)g_diskExplorer.entries.size(); ++j) {
								if (j < (int)g_diskExplorer.selected.size() && g_diskExplorer.selected[j]
									&& !g_diskExplorer.entries[j].isDir
									&& g_diskExplorer.entries[j].name != ".."
									&& g_diskExplorer.entries[j].key != ATDiskFSKey::None) {
									auto &ej = g_diskExplorer.entries[j];
									g_diskMultiExportEntries.push_back({ej.key, ej.name, ej.dateValid, ej.date});
								}
							}
							SDL_ShowOpenFolderDialog(DiskExplorerMultiExportCallback, nullptr, window, nullptr, false);
						} else {
							g_diskExportInfo.key = e.key;
							g_diskExportInfo.dateValid = e.dateValid;
							g_diskExportInfo.date = e.date;
							static const SDL_DialogFileFilter kFilters[] = { { "All Files", "*" } };
							SDL_ShowSaveFileDialog(DiskExplorerExportCallback, nullptr, window, kFilters, 1, nullptr);
						}
					}
					if (ImGui::MenuItem("Export File as Text...") && canExport) {
						if (selFileCount > 1) {
							g_diskMultiExportEntries.clear();
							g_diskMultiExportAsText = true;
							for (int j = 0; j < (int)g_diskExplorer.entries.size(); ++j) {
								if (j < (int)g_diskExplorer.selected.size() && g_diskExplorer.selected[j]
									&& !g_diskExplorer.entries[j].isDir
									&& g_diskExplorer.entries[j].name != ".."
									&& g_diskExplorer.entries[j].key != ATDiskFSKey::None) {
									auto &ej = g_diskExplorer.entries[j];
									g_diskMultiExportEntries.push_back({ej.key, ej.name, ej.dateValid, ej.date});
								}
							}
							SDL_ShowOpenFolderDialog(DiskExplorerMultiExportCallback, nullptr, window, nullptr, false);
						} else {
							g_diskExportTextInfo.key = e.key;
							g_diskExportTextInfo.dateValid = e.dateValid;
							g_diskExportTextInfo.date = e.date;
							static const SDL_DialogFileFilter kFilters[] = { { "All Files", "*" } };
							SDL_ShowSaveFileDialog(DiskExplorerExportTextCallback, nullptr, window, kFilters, 1, nullptr);
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
				ImGui::Text("%u", e.sectors);

			// Size
			ImGui::TableNextColumn();
			if (!e.isDir)
				ImGui::Text("%u", e.bytes);

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

	// New directory popup -- s_wantNewDir is set in the context menu above
	if (s_wantNewDir) {
		ImGui::OpenPopup("NewDirPopup");
		s_wantNewDir = false;
	}

	// Rename popup — s_wantRename is set in the context menu above
	if (s_wantRename) {
		ImGui::OpenPopup("RenamePopup");
		s_wantRename = false;
	}
	if (ImGui::BeginPopup("RenamePopup")) {
		if (!g_diskExplorer.pFS) {
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		} else {
			ImGui::Text("New name:");
			ImGui::SetNextItemWidth(200);
			bool submitted = ImGui::InputText("##rename", s_renameBuf, sizeof(s_renameBuf),
				ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SameLine();
			if ((ImGui::Button("OK") || submitted) && s_renameBuf[0] &&
				s_renameKey != ATDiskFSKey::None) {
				try {
					g_diskExplorer.pFS->RenameFile(s_renameKey, s_renameBuf);
					g_diskExplorer.OnFSModified();
					ImGui::CloseCurrentPopup();
				} catch (const MyError &err) {
					g_diskExplorer.statusMsg.sprintf("Rename failed: %s", err.c_str());
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}

	// File viewer
	ImGui::Separator();

	static const char *kViewModeNames[] = { "Text", "Text (wrap)", "GR.0 (38 col)", "Hex dump", "Executable", "MAC/65" };
	ImGui::Text("View: %s", kViewModeNames[g_diskExplorer.viewMode]);
	ImGui::SameLine();
	if (g_diskExplorer.viewValid)
		ImGui::Text("(%u bytes)", (uint32)g_diskExplorer.viewData.size());

	if (g_diskExplorer.viewValid) {
		bool wrap = (g_diskExplorer.viewMode == kDEView_TextWrap);
		ImGui::BeginChild("FileView", ImVec2(0, 0), ImGuiChildFlags_Borders,
			wrap ? 0 : ImGuiWindowFlags_HorizontalScrollbar);
		if (wrap)
			ImGui::TextWrapped("%s", g_diskExplorer.viewText.c_str());
		else
			ImGui::TextUnformatted(g_diskExplorer.viewText.c_str());
		ImGui::EndChild();
	} else {
		ImGui::BeginChild("FileView", ImVec2(0, 0), ImGuiChildFlags_Borders);
		ImGui::TextDisabled("Select a file to view its contents.");
		ImGui::EndChild();
	}

	// Status bar
	if (!g_diskExplorer.statusMsg.empty()) {
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeightWithSpacing());
		ImGui::TextDisabled("%s", g_diskExplorer.statusMsg.c_str());
	}

	ImGui::End();
}

