//	AltirraSDL - Tools menu dialogs
//	Implements Advanced Configuration, and placeholder stubs for dialogs
//	not yet fully implemented.

#include <stdafx.h>
#include <algorithm>
#include <string>
#include <mutex>
#include <imgui.h>
#include <SDL3/SDL.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/error.h>
#include <vd2/system/date.h>
#include <at/atcore/configvar.h>
#include "ui_main.h"
#include "simulator.h"
#include "gtia.h"
#include "constants.h"
#include "firmwaremanager.h"
#include "firmwaredetect.h"
#include "settings.h"
#include "uiaccessors.h"
#include "uitypes.h"

extern ATSimulator g_sim;

// =========================================================================
// Advanced Configuration
// Reference: src/Altirra/source/uiadvancedconfiguration.cpp
// =========================================================================

// Per-variable edit state
struct AdvConfigEditState {
	ATConfigVar *pVar = nullptr;
	char editBuf[256] = {};
	bool editBool = false;
	bool active = false;
};

static AdvConfigEditState g_advEditState;

void ATUIRenderAdvancedConfig(ATUIState &state) {
	ImGui::SetNextWindowSize(ImVec2(650, 500), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Advanced Configuration", &state.showAdvancedConfig, ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		state.showAdvancedConfig = false;
		g_advEditState.active = false;
		ImGui::End();
		return;
	}

	// Gather config vars
	ATConfigVar **vars = nullptr;
	size_t numVars = 0;
	ATGetConfigVars(vars, numVars);

	const VDStringA *uvars = nullptr;
	size_t numUVars = 0;
	ATGetUndefinedConfigVars(uvars, numUVars);

	// Build sorted index
	struct VarEntry {
		const char *name;
		ATConfigVar *pVar;  // nullptr for undefined vars
	};

	std::vector<VarEntry> sorted;
	sorted.reserve(numVars + numUVars);

	for (size_t i = 0; i < numVars; ++i)
		sorted.push_back({ vars[i]->mpVarName, vars[i] });

	for (size_t i = 0; i < numUVars; ++i)
		sorted.push_back({ uvars[i].c_str(), nullptr });

	std::sort(sorted.begin(), sorted.end(),
		[](const VarEntry &a, const VarEntry &b) { return strcmp(a.name, b.name) < 0; });

	// Filter
	static char filterBuf[128] = {};
	ImGui::SetNextItemWidth(-1);
	ImGui::InputTextWithHint("##filter", "Filter variables...", filterBuf, sizeof(filterBuf));

	ImGui::Separator();

	if (ImGui::BeginTable("ConfigVars", 2,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
		ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
		ImVec2(0, ImGui::GetContentRegionAvail().y - 40))) {

		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.55f);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.45f);
		ImGui::TableHeadersRow();

		for (auto &entry : sorted) {
			// Apply filter
			if (filterBuf[0] && !strcasestr(entry.name, filterBuf))
				continue;

			ImGui::TableNextRow();
			ImGui::PushID(entry.name);

			// Name column
			ImGui::TableNextColumn();

			// Bold if overridden
			bool overridden = entry.pVar && entry.pVar->mbOverridden;
			if (overridden) {
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.4f, 1.0f));
			}
			ImGui::TextUnformatted(entry.name);
			if (overridden) {
				ImGui::PopStyleColor();
			}

			// Value column
			ImGui::TableNextColumn();

			if (!entry.pVar) {
				// Undefined variable
				ImGui::TextDisabled("<unknown cvar>");
				if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
					ImGui::OpenPopup("UndefinedCtx");
				if (ImGui::BeginPopup("UndefinedCtx")) {
					if (ImGui::MenuItem("Unset")) {
						ATUnsetUndefinedConfigVar(entry.name);
					}
					ImGui::EndPopup();
				}
			} else if (g_advEditState.active && g_advEditState.pVar == entry.pVar) {
				// Inline editing
				ATConfigVarType type = entry.pVar->GetVarType();

				if (type == ATConfigVarType::Bool) {
					if (ImGui::Checkbox("##editbool", &g_advEditState.editBool)) {
						static_cast<ATConfigVarBool &>(*entry.pVar) = g_advEditState.editBool;
						g_advEditState.active = false;
					}
					if (ImGui::IsKeyPressed(ImGuiKey_Escape))
						g_advEditState.active = false;
				} else if (type == ATConfigVarType::Float) {
					// Drag-to-edit for floats (mirrors Win32 mouse-drag behavior)
					float val;
					sscanf(g_advEditState.editBuf, "%f", &val);
					if (ImGui::DragFloat("##editfloat", &val, 0.001f, 0.0f, 0.0f, "%.4f")) {
						snprintf(g_advEditState.editBuf, sizeof(g_advEditState.editBuf), "%.4f", val);
						static_cast<ATConfigVarFloat &>(*entry.pVar) = val;
					}
					if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_Enter))
						g_advEditState.active = false;
				} else if (type == ATConfigVarType::RGBColor) {
					uint32 rgb = 0;
					sscanf(g_advEditState.editBuf, "%x", &rgb);
					float col[3] = {
						((rgb >> 16) & 0xFF) / 255.0f,
						((rgb >> 8) & 0xFF) / 255.0f,
						(rgb & 0xFF) / 255.0f,
					};
					if (ImGui::ColorEdit3("##editcolor", col, ImGuiColorEditFlags_NoInputs)) {
						uint32 newRgb = ((uint32)(col[0] * 255.0f) << 16)
							| ((uint32)(col[1] * 255.0f) << 8)
							| (uint32)(col[2] * 255.0f);
						snprintf(g_advEditState.editBuf, sizeof(g_advEditState.editBuf), "%06X", newRgb);
						static_cast<ATConfigVarRGBColor &>(*entry.pVar) = newRgb;
					}
					if (ImGui::IsKeyPressed(ImGuiKey_Escape))
						g_advEditState.active = false;
				} else {
					// Int32 or other text-based editing
					ImGui::SetNextItemWidth(-1);
					if (ImGui::InputText("##editval", g_advEditState.editBuf, sizeof(g_advEditState.editBuf),
						ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
						if (entry.pVar->FromString(g_advEditState.editBuf))
							g_advEditState.active = false;
					}
					if (!ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape))
						g_advEditState.active = false;
					// Auto-focus on first frame
					if (ImGui::IsWindowAppearing())
						ImGui::SetKeyboardFocusHere(-1);
				}
			} else {
				// Display mode
				VDStringA valStr = entry.pVar->ToString();

				if (entry.pVar->GetVarType() == ATConfigVarType::RGBColor) {
					// Show color swatch
					uint32 rgb = 0;
					sscanf(valStr.c_str(), "%x", &rgb);
					ImVec4 col(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 1.0f);
					ImGui::ColorButton("##swatch", col, ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoTooltip, ImVec2(16, 16));
					ImGui::SameLine();
				}

				ImGui::TextUnformatted(valStr.c_str());

				// Double-click to edit
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					g_advEditState.pVar = entry.pVar;
					g_advEditState.active = true;
					VDStringA s = entry.pVar->ToString();
					strncpy(g_advEditState.editBuf, s.c_str(), sizeof(g_advEditState.editBuf) - 1);
					g_advEditState.editBuf[sizeof(g_advEditState.editBuf) - 1] = 0;
					if (entry.pVar->GetVarType() == ATConfigVarType::Bool)
						g_advEditState.editBool = static_cast<ATConfigVarBool &>(*entry.pVar);
				}

				// Right-click context menu
				if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
					ImGui::OpenPopup("VarCtx");
				if (ImGui::BeginPopup("VarCtx")) {
					if (ImGui::MenuItem("Edit")) {
						g_advEditState.pVar = entry.pVar;
						g_advEditState.active = true;
						VDStringA s = entry.pVar->ToString();
						strncpy(g_advEditState.editBuf, s.c_str(), sizeof(g_advEditState.editBuf) - 1);
						g_advEditState.editBuf[sizeof(g_advEditState.editBuf) - 1] = 0;
						if (entry.pVar->GetVarType() == ATConfigVarType::Bool)
							g_advEditState.editBool = static_cast<ATConfigVarBool &>(*entry.pVar);
					}
					if (ImGui::MenuItem("Reset to Default")) {
						entry.pVar->Unset();
					}
					ImGui::EndPopup();
				}
			}

			ImGui::PopID();
		}

		ImGui::EndTable();
	}

	// Bottom buttons
	ImGui::Spacing();
	float buttonWidth = 80;
	if (ImGui::Button("Close", ImVec2(buttonWidth, 0))) {
		state.showAdvancedConfig = false;
		g_advEditState.active = false;
	}

	ImGui::End();
}

// =========================================================================
// Disk Explorer
// Reference: src/Altirra/source/uidiskexplorer.cpp (2,075 lines Win32)
// =========================================================================

#include <at/atio/diskfs.h>
#include <at/atio/diskimage.h>
#include "diskinterface.h"

// View modes for the file content viewer
enum DiskExplorerViewMode {
	kDEView_None = -1,
	kDEView_Text = 0,
	kDEView_TextWrap,
	kDEView_Hex,
	kDEView_Executable,
	kDEView_MAC65,
};

static struct DiskExplorerState {
	vdrefptr<IATDiskImage> pImage;
	IATDiskFS *pFS = nullptr;
	bool readOnly = true;
	ATDiskFSKey currentDir = ATDiskFSKey::None;

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

	// File viewer
	DiskExplorerViewMode viewMode = kDEView_Text;
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
		entries.clear();
		selectedEntry = -1;
		currentDir = ATDiskFSKey::None;
		viewData.clear();
		viewText.clear();
		viewValid = false;
		pathStr = "/";
		statusMsg.clear();
	}

	void RefreshDirectory() {
		entries.clear();
		selectedEntry = -1;
		viewValid = false;

		if (!pFS) return;

		// Add ".." entry if not at root
		if (currentDir != ATDiskFSKey::None) {
			Entry &up = entries.emplace_back();
			up.name = "..";
			up.isDir = true;
			up.key = pFS->GetParentDirectory(currentDir);
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
		std::sort(entries.begin() + (currentDir != ATDiskFSKey::None ? 1 : 0), entries.end(),
			[](const Entry &a, const Entry &b) {
				if (a.isDir != b.isDir) return a.isDir > b.isDir;
				return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
			});

		// Update status
		ATDiskFSInfo fsinfo;
		pFS->GetInfo(fsinfo);
		statusMsg.sprintf("%s | %u free blocks (%u bytes/block) | %d entries",
			fsinfo.mFSType.c_str(), fsinfo.mFreeBlocks, fsinfo.mBlockSize,
			(int)entries.size() - (currentDir != ATDiskFSKey::None ? 1 : 0));
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
		case kDEView_TextWrap: {
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

			viewText.reserve(viewData.size() * 2);
			for (uint8 raw : viewData) {
				if (raw == 0x9B) {
					viewText += '\n';
					continue;
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
		g_diskExplorer.readOnly = true;
		g_diskExplorer.NavigateTo(ATDiskFSKey::None);
	} catch (const MyError &e) {
		g_diskExplorer.Reset();
		g_diskExplorer.statusMsg.sprintf("Failed to open: %s", e.c_str());
	}
}

// Normalize a host filename to 8.3 DOS format.
// Reference: uidiskexplorer.cpp WriteFile() lines 1924-2037
static void NormalizeDOSFilename(const char *src, char *dst, int &nameLen) {
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
				// digits OK
			} else if (c >= 'A' && c <= 'Z') {
				// letters OK
			} else if (c == '_' || c == '@') {
				// allowed in relaxed mode
			} else {
				continue;  // strip invalid chars
			}
			*out++ = c;
			++sectionLen;
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
				memmove(fnbuf + pos + 2, fnbuf + pos + 1, 13 - (pos + 2));
				fnbuf[pos + 1] = '1';
				++nameLen;
			}
			continue;
		}
		break;
	}
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

		// Extract filename from path
		VDStringA filename = VDTextWToA(VDFileSplitPathRightSpan(wpath));

		DiskExplorerWriteFile(filename.c_str(), buf.data(), (uint32)buf.size(), nullptr);
		g_diskExplorer.pFS->Flush();
		g_diskExplorer.RefreshDirectory();
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

void ATUIRenderDiskExplorer(ATSimulator &sim, ATUIState &state, SDL_Window *window) {
	// Process pending file dialog results on main thread
	{
		std::string openPath, importPath;
		{
			std::lock_guard<std::mutex> lock(g_diskExplorerMutex);
			openPath.swap(g_diskExplorerPendingOpen);
			importPath.swap(g_diskExplorerPendingImport);
		}
		if (!openPath.empty())
			DiskExplorerDoOpen(openPath.c_str());
		if (!importPath.empty())
			DiskExplorerDoImport(importPath.c_str());

		std::string exportPath;
		{
			std::lock_guard<std::mutex> lock(g_diskExplorerMutex);
			exportPath.swap(g_diskExplorerPendingExport);
		}
		if (!exportPath.empty())
			DiskExplorerDoExport(exportPath.c_str());
	}

	ImGui::SetNextWindowSize(ImVec2(850, 620), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (!ImGui::Begin("Disk Explorer", &state.showDiskExplorer, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		g_diskExplorer.Reset();
		state.showDiskExplorer = false;
		ImGui::End();
		return;
	}

	if (!state.showDiskExplorer) {
		g_diskExplorer.Reset();
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
					IATDiskImage *dimg = sim.GetDiskInterface(d).GetDiskImage();
					if (dimg) {
						anyMounted = true;
						VDStringA label;
						label.sprintf("D%d:", d + 1);
						if (ImGui::MenuItem(label.c_str())) {
							g_diskExplorer.Reset();
							g_diskExplorer.pImage = dimg;
							g_diskExplorer.pFS = ATDiskMountImage(dimg, true);
							if (!g_diskExplorer.pFS) {
								g_diskExplorer.statusMsg = "Unable to detect filesystem.";
								g_diskExplorer.pImage = nullptr;
							} else {
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
				g_diskExplorer.Reset();
				state.showDiskExplorer = false;
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("View")) {
			if (ImGui::MenuItem("Text", nullptr, g_diskExplorer.viewMode == kDEView_Text))
				{ g_diskExplorer.viewMode = kDEView_Text; g_diskExplorer.FormatView(); g_diskExplorer.viewValid = !g_diskExplorer.viewData.empty(); }
			if (ImGui::MenuItem("Text (wrap)", nullptr, g_diskExplorer.viewMode == kDEView_TextWrap))
				{ g_diskExplorer.viewMode = kDEView_TextWrap; g_diskExplorer.FormatView(); g_diskExplorer.viewValid = !g_diskExplorer.viewData.empty(); }
			if (ImGui::MenuItem("Hex Dump", nullptr, g_diskExplorer.viewMode == kDEView_Hex))
				{ g_diskExplorer.viewMode = kDEView_Hex; g_diskExplorer.FormatView(); g_diskExplorer.viewValid = !g_diskExplorer.viewData.empty(); }
			if (ImGui::MenuItem("Executable", nullptr, g_diskExplorer.viewMode == kDEView_Executable))
				{ g_diskExplorer.viewMode = kDEView_Executable; g_diskExplorer.FormatView(); g_diskExplorer.viewValid = !g_diskExplorer.viewData.empty(); }
			if (ImGui::MenuItem("MAC/65", nullptr, g_diskExplorer.viewMode == kDEView_MAC65))
				{ g_diskExplorer.viewMode = kDEView_MAC65; g_diskExplorer.FormatView(); g_diskExplorer.viewValid = !g_diskExplorer.viewData.empty(); }
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
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

	bool hasSelection = g_diskExplorer.selectedEntry >= 0 &&
		g_diskExplorer.selectedEntry < (int)g_diskExplorer.entries.size() &&
		!g_diskExplorer.entries[g_diskExplorer.selectedEntry].isDir;
	ImGui::BeginDisabled(!hasSelection);
	if (ImGui::SmallButton("Export...") && hasSelection) {
		auto &e = g_diskExplorer.entries[g_diskExplorer.selectedEntry];
		g_diskExportInfo.key = e.key;
		g_diskExportInfo.dateValid = e.dateValid;
		g_diskExportInfo.date = e.date;
		static const SDL_DialogFileFilter kFilters[] = { { "All Files", "*" } };
		SDL_ShowSaveFileDialog(DiskExplorerExportCallback, nullptr, window, kFilters, 1, nullptr);
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::BeginDisabled(!canWrite);
	if (ImGui::SmallButton("New Dir..."))
		ImGui::OpenPopup("NewDirPopup");
	ImGui::EndDisabled();

	// New Directory popup
	if (ImGui::BeginPopup("NewDirPopup")) {
		static char newDirName[64] = {};
		ImGui::Text("Directory name:");
		ImGui::SetNextItemWidth(200);
		bool submitted = ImGui::InputText("##dirname", newDirName, sizeof(newDirName),
			ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SameLine();
		if ((ImGui::Button("OK") || submitted) && newDirName[0]) {
			try {
				g_diskExplorer.pFS->CreateDir(g_diskExplorer.currentDir, newDirName);
				g_diskExplorer.pFS->Flush();
				g_diskExplorer.RefreshDirectory();
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

	ImGui::SameLine();
	bool rwToggle = !g_diskExplorer.readOnly;
	if (ImGui::Checkbox("Write", &rwToggle)) {
		g_diskExplorer.readOnly = !rwToggle;
		// Remount with new read/write mode
		if (g_diskExplorer.pImage) {
			try {
				delete g_diskExplorer.pFS;
				g_diskExplorer.pFS = ATDiskMountImage(g_diskExplorer.pImage, g_diskExplorer.readOnly);

				// Validate filesystem before allowing writes
				if (!g_diskExplorer.readOnly && g_diskExplorer.pFS) {
					ATDiskFSValidationReport report;
					g_diskExplorer.pFS->Validate(report);
					if (report.IsSerious()) {
						g_diskExplorer.pFS->SetReadOnly(true);
						g_diskExplorer.readOnly = true;
						g_diskExplorer.statusMsg = "Filesystem has serious corruption — forced to read-only.";
					} else if (report.mbBitmapIncorrect) {
						g_diskExplorer.statusMsg = "Warning: filesystem bitmap has minor issues.";
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
	static ATDiskFSKey s_renameKey = ATDiskFSKey::None;
	static char s_renameBuf[64] = {};

	// File list
	if (ImGui::BeginTable("FileList", 4,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
		ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingStretchProp,
		ImVec2(0, listH))) {

		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch, 0.40f);
		ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 0.20f);
		ImGui::TableSetupColumn("Sectors", ImGuiTableColumnFlags_WidthStretch, 0.15f);
		ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthStretch, 0.25f);
		ImGui::TableHeadersRow();

		for (int i = 0; i < (int)g_diskExplorer.entries.size(); ++i) {
			auto &e = g_diskExplorer.entries[i];
			ImGui::TableNextRow();
			ImGui::PushID(i);

			// Name
			ImGui::TableNextColumn();
			bool selected = (g_diskExplorer.selectedEntry == i);
			VDStringA label;
			if (e.isDir)
				label.sprintf("[%s]", e.name.c_str());
			else
				label = e.name;

			if (ImGui::Selectable(label.c_str(), selected,
				ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
				g_diskExplorer.selectedEntry = i;
				if (!e.isDir)
					g_diskExplorer.LoadFileView(i);

				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && e.isDir) {
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
				if (!e.isDir && ImGui::MenuItem("Export...")) {
					g_diskExportInfo.key = e.key;
					g_diskExportInfo.dateValid = e.dateValid;
					g_diskExportInfo.date = e.date;
					static const SDL_DialogFileFilter kFilters[] = { { "All Files", "*" } };
					SDL_ShowSaveFileDialog(DiskExplorerExportCallback, nullptr, window, kFilters, 1, nullptr);
				}
				if (!e.isDir && !g_diskExplorer.readOnly && ImGui::MenuItem("Delete")) {
					try {
						g_diskExplorer.pFS->DeleteFile(e.key);
						g_diskExplorer.pFS->Flush();
						deleted = true;
					} catch (const MyError &err) {
						g_diskExplorer.statusMsg.sprintf("Delete failed: %s", err.c_str());
					}
				}
				if (!g_diskExplorer.readOnly && e.name != ".." && ImGui::MenuItem("Rename...")) {
					s_renameKey = e.key;
					strncpy(s_renameBuf, e.name.c_str(), sizeof(s_renameBuf) - 1);
					s_renameBuf[sizeof(s_renameBuf) - 1] = 0;
					s_wantRename = true;
				}
				if (e.isDir && e.name != ".." && !g_diskExplorer.readOnly && ImGui::MenuItem("Delete Directory")) {
					try {
						g_diskExplorer.pFS->DeleteFile(e.key);
						g_diskExplorer.pFS->Flush();
						deleted = true;
					} catch (const MyError &err) {
						g_diskExplorer.statusMsg.sprintf("Delete failed: %s", err.c_str());
					}
				}
				ImGui::EndPopup();
			}
			if (deleted) {
				ImGui::PopID();
				g_diskExplorer.RefreshDirectory();
				break;  // entries invalidated, restart next frame
			}

			// Size
			ImGui::TableNextColumn();
			if (!e.isDir)
				ImGui::Text("%u", e.bytes);

			// Sectors
			ImGui::TableNextColumn();
			if (!e.isDir)
				ImGui::Text("%u", e.sectors);

			// Date
			ImGui::TableNextColumn();
			if (e.dateValid)
				ImGui::Text("%04u-%02u-%02u %02u:%02u",
					e.date.mYear, e.date.mMonth, e.date.mDay,
					e.date.mHour, e.date.mMinute);

			ImGui::PopID();
		}

		ImGui::EndTable();
	}

	// Rename popup — s_wantRename is set in the context menu above
	if (s_wantRename) {
		ImGui::OpenPopup("RenamePopup");
		s_wantRename = false;
	}
	if (ImGui::BeginPopup("RenamePopup")) {
		ImGui::Text("New name:");
		ImGui::SetNextItemWidth(200);
		bool submitted = ImGui::InputText("##rename", s_renameBuf, sizeof(s_renameBuf),
			ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SameLine();
		if ((ImGui::Button("OK") || submitted) && s_renameBuf[0] &&
			s_renameKey != ATDiskFSKey::None) {
			try {
				g_diskExplorer.pFS->RenameFile(s_renameKey, s_renameBuf);
				g_diskExplorer.pFS->Flush();
				g_diskExplorer.RefreshDirectory();
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

	// File viewer
	ImGui::Separator();

	static const char *kViewModeNames[] = { "Text", "Text (wrap)", "Hex Dump", "Executable", "MAC/65" };
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

// =========================================================================
// First Time Setup Wizard
// Reference: src/Altirra/source/uisetupwizard.cpp
// =========================================================================

static struct SetupWizardState {
	int page = 0;
	bool wentPastFirst = false;
	bool firmwareScanned = false;
	int scanFound = 0;
	int scanExisting = 0;
	VDStringA scanMessage;

	// Thread-safe: path stored by callback, processed on main thread
	std::mutex scanMutex;
	std::string pendingScanPath;

	void Reset() {
		page = 0;
		wentPastFirst = false;
		firmwareScanned = false;
		scanFound = 0;
		scanExisting = 0;
		scanMessage.clear();
		// mutex and pendingScanPath don't need reset
	}
} g_setupWiz;

// Firmware scan logic reimplemented from uifirmwarescan.cpp
static void ATUIDoFirmwareScan(const char *utf8path) {
	ATFirmwareManager &fwmgr = *g_sim.GetFirmwareManager();
	VDStringW path = VDTextU8ToW(utf8path, -1);
	VDStringW pattern = VDMakePath(path.c_str(), L"*.*");

	VDDirectoryIterator it(pattern.c_str());
	vdvector<VDStringW> candidates;

	while (it.Next()) {
		if (it.GetAttributes() & (kVDFileAttr_System | kVDFileAttr_Hidden))
			continue;
		if (it.IsDirectory())
			continue;
		if (!ATFirmwareAutodetectCheckSize(it.GetSize()))
			continue;
		candidates.push_back(it.GetFullPath());
	}

	ATFirmwareInfo info;
	vdvector<ATFirmwareInfo> detected;

	for (auto &fullPath : candidates) {
		try {
			VDFile f(fullPath.c_str());
			sint64 size = f.size();
			if (!ATFirmwareAutodetectCheckSize(size))
				continue;

			uint32 size32 = (uint32)size;
			vdblock<char> buf(size32);
			f.read(buf.data(), (long)buf.size());

			ATSpecificFirmwareType specificType;
			sint32 knownIdx = -1;
			if (ATFirmwareAutodetect(buf.data(), size32, info, specificType, knownIdx) == ATFirmwareDetection::SpecificImage) {
				ATFirmwareInfo &info2 = detected.push_back();
				info2 = std::move(info);
				info2.mId = ATGetFirmwareIdFromPath(fullPath.c_str());
				info2.mPath = fullPath;

				if (specificType != kATSpecificFirmwareType_None && !fwmgr.GetSpecificFirmware(specificType))
					fwmgr.SetSpecificFirmware(specificType, info2.mId);
			}
		} catch (const MyError &) {
		}
	}

	size_t existing = 0;
	for (auto &det : detected) {
		ATFirmwareInfo info2;
		if (fwmgr.GetFirmwareInfo(det.mId, info2)) {
			++existing;
			continue;
		}
		fwmgr.AddFirmware(det);
	}

	g_setupWiz.scanFound = (int)detected.size();
	g_setupWiz.scanExisting = (int)existing;
	g_setupWiz.firmwareScanned = true;
	g_setupWiz.scanMessage.sprintf("Firmware images recognized: %d (%d already present)",
		(int)detected.size(), (int)existing);
}

// File dialog callback — may run on background thread, so just store the path.
// The actual scan runs on the main thread in ATUIRenderSetupWizard().
static void FirmwareScanCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0])
		return;
	std::lock_guard<std::mutex> lock(g_setupWiz.scanMutex);
	g_setupWiz.pendingScanPath = filelist[0];
}

static int GetWizPrevPage(int page) {
	switch (page) {
		case 0:  return -1;
		case 10: return 0;
		case 11: return 10;
		case 20: return 11;
		case 21: return 20;
		case 30: return g_sim.GetHardwareMode() == kATHardwareMode_5200 ? 20 : 21;
		case 40: return 30;
		case 41: return 30;
		default: return 0;
	}
}

static int GetWizNextPage(int page) {
	switch (page) {
		case 0:  return 10;
		case 10: return 11;
		case 11: return 20;
		case 20: return g_sim.GetHardwareMode() == kATHardwareMode_5200 ? 30 : 21;
		case 21: return 30;
		case 30: return g_sim.GetHardwareMode() == kATHardwareMode_5200 ? 41 : 40;
		default: return -1;
	}
}

void ATUIRenderSetupWizard(ATSimulator &sim, ATUIState &state, SDL_Window *window) {
	// Process pending firmware scan on main thread (callback may have run on background thread)
	{
		std::string scanPath;
		{
			std::lock_guard<std::mutex> lock(g_setupWiz.scanMutex);
			scanPath.swap(g_setupWiz.pendingScanPath);
		}
		if (!scanPath.empty())
			ATUIDoFirmwareScan(scanPath.c_str());
	}

	ImGui::SetNextWindowSize(ImVec2(620, 480), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	bool open = state.showSetupWizard;
	if (!ImGui::Begin("First Time Setup", &open,
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
		if (!open) {
			if (g_setupWiz.wentPastFirst) {
				sim.LoadROMs();
				sim.ColdReset();
			}
			g_setupWiz.Reset();
			state.showSetupWizard = false;
		}
		ImGui::End();
		return;
	}

	if (!open || ATUICheckEscClose()) {
		if (g_setupWiz.wentPastFirst) {
			sim.LoadROMs();
			sim.ColdReset();
		}
		g_setupWiz.Reset();
		state.showSetupWizard = false;
		ImGui::End();
		return;
	}

	float sidebarW = 140;

	// Left sidebar: step list
	{
		ImGui::BeginChild("WizSteps", ImVec2(sidebarW, -40), ImGuiChildFlags_Borders);

		static const struct { int pageMin; int pageMax; const char *label; } kSteps[] = {
			{ 0, 9, "Welcome" },
			{ 10, 19, "Setup firmware" },
			{ 20, 29, "Select system" },
			{ 30, 39, "Experience" },
			{ 40, 49, "Finish" },
		};

		for (auto &step : kSteps) {
			bool active = (g_setupWiz.page >= step.pageMin && g_setupWiz.page <= step.pageMax);
			if (active) {
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.4f, 1.0f));
				ImGui::Bullet();
				ImGui::SameLine();
				ImGui::TextUnformatted(step.label);
				ImGui::PopStyleColor();
			} else {
				ImGui::TextUnformatted(step.label);
			}
		}

		ImGui::EndChild();
	}

	ImGui::SameLine();

	// Right content area
	{
		ImGui::BeginChild("WizContent", ImVec2(0, -40));

		switch (g_setupWiz.page) {
		case 0: // Welcome
			ImGui::TextWrapped(
				"Welcome to Altirra!\n\n"
				"This wizard will help you configure the emulator for the first time. "
				"To begin, click Next.\n\n"
				"If you would like to skip the setup process, click Close to exit this "
				"wizard and start the emulator. All of the settings here can also be set "
				"up manually. You can also repeat the first time setup process via the "
				"Tools menu at any time."
			);
			break;

		case 10: { // Firmware
			ImGui::TextWrapped(
				"Altirra has internal replacements for all standard ROMs. However, "
				"if you have original ROM images, you can set these up now for better "
				"compatibility.\n\n"
				"If you do not have ROM images or do not want to set them up now, just "
				"click Next."
			);
			ImGui::Spacing();

			// Firmware status table
			ATFirmwareManager &fwm = *sim.GetFirmwareManager();
			static const struct { ATFirmwareType type; const char *name; } kFirmware[] = {
				{ kATFirmwareType_Kernel800_OSB, "800 OS (OS-B)" },
				{ kATFirmwareType_KernelXL,      "XL/XE OS" },
				{ kATFirmwareType_Basic,          "BASIC" },
				{ kATFirmwareType_Kernel5200,     "5200 OS" },
			};

			if (ImGui::BeginTable("FWStatus", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
				ImGui::TableSetupColumn("ROM Image");
				ImGui::TableSetupColumn("Status");
				ImGui::TableHeadersRow();

				for (auto &fw : kFirmware) {
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(fw.name);
					ImGui::TableNextColumn();
					uint64 fwid = fwm.GetCompatibleFirmware(fw.type);
					bool present = (fwid && fwid >= kATFirmwareId_Custom);
					if (present) {
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
						ImGui::TextUnformatted("OK");
						ImGui::PopStyleColor();
					} else {
						ImGui::TextDisabled("Not found");
					}
				}
				ImGui::EndTable();
			}

			ImGui::Spacing();
			if (ImGui::Button("Scan for firmware..."))
				SDL_ShowOpenFolderDialog(FirmwareScanCallback, nullptr, window, nullptr, false);

			if (!g_setupWiz.scanMessage.empty()) {
				ImGui::SameLine();
				ImGui::TextUnformatted(g_setupWiz.scanMessage.c_str());
			}
			break;
		}

		case 11: // Post-firmware
			ImGui::TextWrapped(
				"ROM image setup is complete.\n\n"
				"If you want to set up more firmware ROM images in the future, this can "
				"be done through the menu option System > Firmware Images."
			);
			break;

		case 20: { // Select system
			ImGui::TextWrapped(
				"Select the type of system to emulate. This can be changed later from "
				"the System menu."
			);
			ImGui::Spacing();

			bool is5200 = (sim.GetHardwareMode() == kATHardwareMode_5200);
			bool isComputer = !is5200;

			if (ImGui::RadioButton("Computer (XL/XE)", isComputer) && !isComputer) {
				uint32 profileId = ATGetDefaultProfileId(kATDefaultProfile_XL);
				ATSettingsSwitchProfile(profileId);
			}
			if (ImGui::RadioButton("Atari 5200", is5200) && !is5200) {
				uint32 profileId = ATGetDefaultProfileId(kATDefaultProfile_5200);
				ATSettingsSwitchProfile(profileId);
			}
			break;
		}

		case 21: { // Video standard (skipped for 5200)
			ImGui::TextWrapped(
				"Select the video standard. NTSC (60Hz) is the North American standard. "
				"PAL (50Hz) is the European standard.\n\n"
				"This affects timing and color palette. Most software is designed for NTSC."
			);
			ImGui::Spacing();

			bool isNTSC = (sim.GetVideoStandard() == kATVideoStandard_NTSC
				|| sim.GetVideoStandard() == kATVideoStandard_PAL60);

			if (ImGui::RadioButton("NTSC (60 Hz)", isNTSC) && !isNTSC)
				ATSetVideoStandard(kATVideoStandard_NTSC);
			if (ImGui::RadioButton("PAL (50 Hz)", !isNTSC) && isNTSC)
				ATSetVideoStandard(kATVideoStandard_PAL);
			break;
		}

		case 30: { // Experience level
			ImGui::TextWrapped(
				"Select the emulation experience level.\n\n"
				"Authentic mode enables hardware artifacting, accurate disk timing, and "
				"drive sounds for a more realistic experience.\n\n"
				"Convenient mode enables SIO patches for fast loading and disables "
				"hardware artifacts for a cleaner experience."
			);
			ImGui::Spacing();

			bool isAuthentic = (sim.GetGTIA().GetArtifactingMode() != ATArtifactMode::None);

			if (ImGui::RadioButton("Authentic", isAuthentic) && !isAuthentic) {
				sim.GetGTIA().SetArtifactingMode(ATArtifactMode::AutoHi);
				sim.SetCassetteSIOPatchEnabled(false);
				sim.SetDiskSIOPatchEnabled(false);
				sim.SetDiskAccurateTimingEnabled(true);
				ATUISetDriveSoundsEnabled(true);
				ATUISetDisplayFilterMode(kATDisplayFilterMode_Bilinear);
			}
			if (ImGui::RadioButton("Convenient", !isAuthentic) && isAuthentic) {
				ATUISetDriveSoundsEnabled(false);
				sim.SetCassetteSIOPatchEnabled(true);
				sim.SetDiskSIOPatchEnabled(true);
				sim.SetDiskAccurateTimingEnabled(false);
				sim.GetGTIA().SetArtifactingMode(ATArtifactMode::None);
				ATUISetDisplayFilterMode(kATDisplayFilterMode_SharpBilinear);
				ATUISetViewFilterSharpness(+1);
			}
			break;
		}

		case 40: // Finish (computer)
			ImGui::TextWrapped(
				"Setup is now complete.\n\n"
				"Click Finish to exit and power up the emulated computer. You can then "
				"use the File > Boot Image... menu option to boot a disk, cartridge, or "
				"cassette tape image, or start a program.\n\n"
				"If you want to repeat this process in the future, the setup wizard can "
				"be restarted via the Tools menu."
			);
			break;

		case 41: // Finish (5200)
			ImGui::TextWrapped(
				"Setup is now complete.\n\n"
				"Click Finish to exit and power up the emulated console. The 5200 needs "
				"a cartridge to work, so select File > Boot Image... to attach and start "
				"a cartridge image.\n\n"
				"You will probably want to check your controller settings. The default "
				"setup binds F2-F4, the digit key row, arrow keys, and Ctrl/Shift to "
				"joystick 1. Alternate bindings can be selected from the Input menu or "
				"new ones can be defined in Input > Input Mappings.\n\n"
				"If you want to repeat this process in the future, choose Tools > First "
				"Time Setup... from the menu."
			);
			break;
		}

		ImGui::EndChild();
	}

	// Bottom buttons
	ImGui::Separator();
	int prevPage = GetWizPrevPage(g_setupWiz.page);
	int nextPage = GetWizNextPage(g_setupWiz.page);
	bool canPrev = (prevPage >= 0);
	bool canNext = (nextPage >= 0);

	ImGui::BeginDisabled(!canPrev);
	if (ImGui::Button("< Prev"))
		g_setupWiz.page = prevPage;
	ImGui::EndDisabled();

	ImGui::SameLine();

	if (canNext) {
		if (ImGui::Button("Next >")) {
			g_setupWiz.wentPastFirst = true;
			g_setupWiz.page = nextPage;
		}
	} else {
		if (ImGui::Button("Finish")) {
			if (g_setupWiz.wentPastFirst) {
				sim.LoadROMs();
				sim.ColdReset();
			}
			g_setupWiz.Reset();
			state.showSetupWizard = false;
		}
	}

	ImGui::SameLine();
	if (ImGui::Button("Close")) {
		if (g_setupWiz.wentPastFirst) {
			sim.LoadROMs();
			sim.ColdReset();
		}
		g_setupWiz.Reset();
		state.showSetupWizard = false;
	}

	ImGui::End();
}

// =========================================================================
// Keyboard Shortcuts
// Reference: src/ATNativeUI/source/acceleditdialog.cpp
//
// The SDL3 build uses hardcoded shortcuts in main_sdl3.cpp and
// menu labels in ui_main.cpp. This dialog displays the current
// bindings. A full rebinding system would require refactoring the
// shortcut dispatch into a data-driven table (future work).
// =========================================================================

struct ShortcutEntry {
	const char *command;
	const char *keys;
	const char *context;
};

static const ShortcutEntry kShortcuts[] = {
	// File
	{ "Boot Image",              "Ctrl+O",      "Global" },

	// Emulation control
	{ "Warm Reset",              "F5",           "Global" },
	{ "Cold Reset",              "Shift+F5",     "Global" },
	{ "Pause / Resume",          "F9",           "Global" },

	// State
	{ "Quick Load State",        "F7",           "Global" },
	{ "Quick Save State",        "F8",           "Global" },

	// View
	{ "Toggle Fullscreen",       "Alt+Enter",    "Global" },

	// Mouse
	{ "Release Mouse Capture",   "Right Alt",    "Global" },

	// Console switches (emulator input)
	{ "Start (Console)",         "F2",           "Emulator" },
	{ "Select (Console)",        "F3",           "Emulator" },
	{ "Option (Console)",        "F4",           "Emulator" },
};

void ATUIRenderKeyboardShortcuts(ATUIState &state) {
	ImGui::SetNextWindowSize(ImVec2(550, 420), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Keyboard Shortcuts", &state.showKeyboardShortcuts, ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		state.showKeyboardShortcuts = false;
		ImGui::End();
		return;
	}

	// Filter
	static char filterBuf[128] = {};
	ImGui::SetNextItemWidth(200);
	ImGui::InputTextWithHint("##filter", "Filter...", filterBuf, sizeof(filterBuf));
	ImGui::SameLine();
	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Shortcut customization will be available in a future update.\n"
			"Currently showing the default key bindings.");

	ImGui::Separator();

	if (ImGui::BeginTable("Shortcuts", 3,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
		ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
		ImVec2(0, ImGui::GetContentRegionAvail().y - 35))) {

		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthStretch, 0.45f);
		ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch, 0.30f);
		ImGui::TableSetupColumn("Context", ImGuiTableColumnFlags_WidthStretch, 0.25f);
		ImGui::TableHeadersRow();

		for (const auto &s : kShortcuts) {
			if (filterBuf[0] && !strcasestr(s.command, filterBuf) && !strcasestr(s.keys, filterBuf))
				continue;

			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(s.command);
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(s.keys);
			ImGui::TableNextColumn();
			ImGui::TextDisabled("%s", s.context);
		}

		ImGui::EndTable();
	}

	ImGui::Spacing();
	if (ImGui::Button("Close", ImVec2(80, 0)))
		state.showKeyboardShortcuts = false;

	ImGui::End();
}

// =========================================================================
// Compatibility Database
// Reference: src/Altirra/source/uicompatdb.cpp (1,154 lines Win32)
// =========================================================================

#include <at/atio/image.h>
#include <at/atio/blobimage.h>
#include "compatedb.h"
#include "compatengine.h"
#include "compatdb.h"
#include "uicompat.h"
#include "cartridge.h"
#include "cassette.h"
#include "disk.h"
#include "diskinterface.h"
#include "hleprogramloader.h"

// Local reimplementation of uicompatdb.cpp helper (excluded by ui* regex)
static void ATCompatAddSourcedRulesForImage(vdvector<ATCompatEDBSourcedAliasRule> &sourcedRules, IATImage *image, const wchar_t *sourceName) {
	if (!image) return;
	vdfastvector<ATCompatMarker> markers;
	ATCompatGetMarkersForImage(markers, image);
	for (const ATCompatMarker &marker : markers) {
		ATCompatEDBSourcedAliasRule &rule = sourcedRules.push_back();
		rule.mRule = marker;
		rule.mSource = sourceName;
	}
}

static struct CompatDBState {
	ATCompatEDB edb;
	VDStringW path;
	VDStringW compilePath;
	bool modified = false;
	bool needsCompile = false;
	bool isExternalDb = false;
	bool initialized = false;

	// Display state
	vdfastvector<ATCompatEDBTitle *> displayedTitles;
	int selectedTitle = -1;
	int selectedAlias = -1;
	int selectedTag = -1;
	char searchBuf[128] = {};
	char titleNameBuf[256] = {};

	// Tag picker popup
	bool showTagPicker = false;
	ATCompatKnownTag lastSelectedTag = kATCompatKnownTag_None;

	// Close confirmation state
	bool wantClose = false;
	bool showDiscardConfirm = false;
	bool showCompileConfirm = false;

	void Reset() {
		edb = ATCompatEDB();
		path.clear();
		compilePath.clear();
		modified = false;
		needsCompile = false;
		isExternalDb = false;
		initialized = false;
		displayedTitles.clear();
		selectedTitle = -1;
		selectedAlias = -1;
		selectedTag = -1;
		searchBuf[0] = 0;
		titleNameBuf[0] = 0;
		showTagPicker = false;
	}

	void RefreshTitles() {
		displayedTitles.clear();
		selectedAlias = -1;
		selectedTag = -1;

		VDStringW searchLower;
		if (searchBuf[0]) {
			searchLower = VDTextU8ToW(searchBuf, -1);
			for (wchar_t &c : searchLower) c = towlower(c);
		}

		VDStringW tmp;
		for (auto *title : edb.mTitleTable) {
			if (!searchLower.empty()) {
				tmp = title->mName;
				for (wchar_t &c : tmp) c = towlower(c);
				if (!wcsstr(tmp.c_str(), searchLower.c_str()))
					continue;
			}
			displayedTitles.push_back(title);
		}

		std::sort(displayedTitles.begin(), displayedTitles.end(),
			[](const ATCompatEDBTitle *a, const ATCompatEDBTitle *b) {
				return a->mName.comparei(b->mName) < 0;
			});

		if (selectedTitle >= (int)displayedTitles.size())
			selectedTitle = displayedTitles.empty() ? -1 : (int)displayedTitles.size() - 1;
	}

	void SetModified() {
		modified = true;
		needsCompile = true;
	}

	VDStringA GetCaption() const {
		VDStringA s;
		if (modified) s += "*";
		if (path.empty())
			s += "New file";
		else {
			VDStringW name = VDFileSplitPathRightSpan(path);
			s += VDTextWToU8(name);
		}
		s += " - Compatibility Database";
		return s;
	}
} g_compatDB;

// Thread-safe pending paths for compat DB file operations
static std::mutex g_compatDBFileMutex;
static std::string g_compatDBPendingLoad;
static std::string g_compatDBPendingSave;
static std::string g_compatDBPendingCompile;

static void CompatDBLoadCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	std::lock_guard<std::mutex> lock(g_compatDBFileMutex);
	g_compatDBPendingLoad = filelist[0];
}

static void CompatDBSaveCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	std::lock_guard<std::mutex> lock(g_compatDBFileMutex);
	g_compatDBPendingSave = filelist[0];
}

static void CompatDBCompileCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	std::lock_guard<std::mutex> lock(g_compatDBFileMutex);
	g_compatDBPendingCompile = filelist[0];
}

static void CompatDBProcessPending() {
	std::string loadPath, savePath, compilePath;
	{
		std::lock_guard<std::mutex> lock(g_compatDBFileMutex);
		loadPath.swap(g_compatDBPendingLoad);
		savePath.swap(g_compatDBPendingSave);
		compilePath.swap(g_compatDBPendingCompile);
	}

	if (!loadPath.empty()) {
		try {
			VDStringW wpath = VDTextU8ToW(loadPath.c_str(), -1);
			ATCompatEDB tempEDB;
			ATLoadCompatEDB(wpath.c_str(), tempEDB);
			for (auto &entry : tempEDB.mTagTable) {
				auto knownTag = ATCompatGetKnownTagByKey(entry.second.mKey.c_str());
				if (knownTag)
					entry.second.mDisplayName = ATUICompatGetKnownTagDisplayName(knownTag);
			}
			g_compatDB.edb = std::move(tempEDB);
			g_compatDB.path = wpath;
			g_compatDB.compilePath.clear();
			g_compatDB.modified = false;
			g_compatDB.isExternalDb = false;
			g_compatDB.needsCompile = false;
			g_compatDB.RefreshTitles();
		} catch (const MyError &e) {
			fprintf(stderr, "[AltirraSDL] CompatDB load failed: %s\n", e.c_str());
		}
	}

	if (!savePath.empty()) {
		try {
			VDStringW wpath = VDTextU8ToW(savePath.c_str(), -1);
			ATSaveCompatEDB(wpath.c_str(), g_compatDB.edb);
			g_compatDB.path = wpath;
			g_compatDB.modified = false;
		} catch (const MyError &e) {
			fprintf(stderr, "[AltirraSDL] CompatDB save failed: %s\n", e.c_str());
		}
	}

	if (!compilePath.empty()) {
		try {
			VDStringW wpath = VDTextU8ToW(compilePath.c_str(), -1);
			vdblock<char> buf;
			ATCompileCompatEDB(buf, g_compatDB.edb);
			VDFile f(wpath.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
			f.write(buf.data(), (long)buf.size());
			f.close();
			g_compatDB.compilePath = wpath;
			g_compatDB.needsCompile = false;
			ATCompatReloadExtDatabase();
		} catch (const MyError &e) {
			fprintf(stderr, "[AltirraSDL] CompatDB compile failed: %s\n", e.c_str());
		}
	}
}

void ATUIRenderCompatDB(ATSimulator &sim, ATUIState &state) {
	// Process pending file dialog results on main thread
	CompatDBProcessPending();

	if (!g_compatDB.initialized) {
		g_compatDB.initialized = true;

		// Auto-load external DB if present
		if (ATCompatIsExtDatabaseLoaded()) {
			const VDStringW &dbPath = ATCompatGetExtDatabasePath();
			if (!dbPath.empty()) {
				try {
					VDStringW srcPath = VDFileSplitExtLeft(dbPath) + L".atcompatdb";
					ATLoadCompatEDB(srcPath.c_str(), g_compatDB.edb);
					g_compatDB.path = srcPath;
					g_compatDB.compilePath = dbPath;
					g_compatDB.isExternalDb = true;

					for (auto &entry : g_compatDB.edb.mTagTable) {
						auto knownTag = ATCompatGetKnownTagByKey(entry.second.mKey.c_str());
						if (knownTag)
							entry.second.mDisplayName = ATUICompatGetKnownTagDisplayName(knownTag);
					}
				} catch (const MyError &) {
				}
			}
		}
		g_compatDB.RefreshTitles();
	}

	VDStringA caption = g_compatDB.GetCaption();

	ImGui::SetNextWindowSize(ImVec2(750, 550), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	bool open = state.showCompatDB;
	if (!ImGui::Begin(caption.c_str(), &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar)) {
		// Window collapsed — if user clicked X, just flag for close (popup will show next frame when uncollapsed)
		if (!open) {
			if (!g_compatDB.modified && !(g_compatDB.isExternalDb && g_compatDB.needsCompile)) {
				g_compatDB.Reset();
				state.showCompatDB = false;
			} else {
				g_compatDB.wantClose = true;
			}
		}
		ImGui::End();
		return;
	}

	// Handle deferred close request (from collapsed state or ESC/X)
	if (g_compatDB.wantClose || !open || ATUICheckEscClose()) {
		g_compatDB.wantClose = false;
		if (g_compatDB.modified) {
			g_compatDB.showDiscardConfirm = true;
		} else if (g_compatDB.isExternalDb && g_compatDB.needsCompile) {
			g_compatDB.showCompileConfirm = true;
		} else {
			g_compatDB.Reset();
			state.showCompatDB = false;
			ImGui::End();
			return;
		}
	}

	// Menu bar
	if (ImGui::BeginMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("New")) {
				if (!g_compatDB.modified) {
					g_compatDB.edb = ATCompatEDB();
					g_compatDB.path.clear();
					g_compatDB.compilePath.clear();
					g_compatDB.modified = false;
					g_compatDB.needsCompile = false;
					g_compatDB.isExternalDb = false;
					g_compatDB.RefreshTitles();
				} else {
					g_compatDB.showDiscardConfirm = true;
				}
			}
			if (ImGui::MenuItem("Load...")) {
				static const SDL_DialogFileFilter kFilters[] = {
					{ "Altirra CompatDB", "atcompatdb" },
					{ "All Files", "*" },
				};
				SDL_ShowOpenFileDialog(CompatDBLoadCallback, nullptr, nullptr, kFilters, 2, nullptr, false);
			}
			if (ImGui::MenuItem("Save")) {
				if (g_compatDB.path.empty()) {
					static const SDL_DialogFileFilter kFilters[] = {
						{ "Altirra CompatDB", "atcompatdb" },
						{ "All Files", "*" },
					};
					SDL_ShowSaveFileDialog(CompatDBSaveCallback, nullptr, nullptr, kFilters, 2, nullptr);
				} else {
					try {
						ATSaveCompatEDB(g_compatDB.path.c_str(), g_compatDB.edb);
						g_compatDB.modified = false;
					} catch (const MyError &e) {
						fprintf(stderr, "[AltirraSDL] Save failed: %s\n", e.c_str());
					}
				}
			}
			if (ImGui::MenuItem("Save As...")) {
				static const SDL_DialogFileFilter kFilters[] = {
					{ "Altirra CompatDB", "atcompatdb" },
					{ "All Files", "*" },
				};
				SDL_ShowSaveFileDialog(CompatDBSaveCallback, nullptr, nullptr, kFilters, 2, nullptr);
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Compile...")) {
				if (!g_compatDB.compilePath.empty()) {
					try {
						vdblock<char> buf;
						ATCompileCompatEDB(buf, g_compatDB.edb);
						VDFile f(g_compatDB.compilePath.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
						f.write(buf.data(), (long)buf.size());
						f.close();
						g_compatDB.needsCompile = false;
						ATCompatReloadExtDatabase();
					} catch (const MyError &e) {
						fprintf(stderr, "[AltirraSDL] Compile failed: %s\n", e.c_str());
					}
				} else {
					static const SDL_DialogFileFilter kFilters[] = {
						{ "Compat Engine", "atcpengine" },
						{ "All Files", "*" },
					};
					SDL_ShowSaveFileDialog(CompatDBCompileCallback, nullptr, nullptr, kFilters, 2, nullptr);
				}
			}
			if (ImGui::MenuItem("Compile To...")) {
				static const SDL_DialogFileFilter kFilters[] = {
					{ "Compat Engine", "atcpengine" },
					{ "All Files", "*" },
				};
				SDL_ShowSaveFileDialog(CompatDBCompileCallback, nullptr, nullptr, kFilters, 2, nullptr);
			}
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}

	// Left pane: title list
	float leftW = ImGui::GetContentRegionAvail().x * 0.4f;

	ImGui::BeginChild("TitlePane", ImVec2(leftW, 0));

	ImGui::Text("Titles");
	ImGui::SetNextItemWidth(-1);
	bool searchChanged = ImGui::InputTextWithHint("##search", "Quick search...", g_compatDB.searchBuf, sizeof(g_compatDB.searchBuf));
	if (searchChanged)
		g_compatDB.RefreshTitles();

	// Title list
	if (ImGui::BeginListBox("##titles", ImVec2(-1, ImGui::GetContentRegionAvail().y - 30))) {
		for (int i = 0; i < (int)g_compatDB.displayedTitles.size(); ++i) {
			VDStringA name = VDTextWToU8(g_compatDB.displayedTitles[i]->mName);
			bool selected = (g_compatDB.selectedTitle == i);
			if (ImGui::Selectable(name.c_str(), selected)) {
				g_compatDB.selectedTitle = i;
				g_compatDB.selectedAlias = -1;
				g_compatDB.selectedTag = -1;
			}
		}
		ImGui::EndListBox();
	}

	// Add/Delete title buttons
	if (ImGui::Button("Add Title")) {
		auto *newTitle = g_compatDB.edb.mTitleTable.Create();
		newTitle->mName = L"(New Title)";
		g_compatDB.SetModified();
		g_compatDB.RefreshTitles();
		// Select the new title
		for (int i = 0; i < (int)g_compatDB.displayedTitles.size(); ++i) {
			if (g_compatDB.displayedTitles[i] == newTitle) {
				g_compatDB.selectedTitle = i;
				break;
			}
		}
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(g_compatDB.selectedTitle < 0);
	if (ImGui::Button("Delete Title") && g_compatDB.selectedTitle >= 0) {
		auto *title = g_compatDB.displayedTitles[g_compatDB.selectedTitle];
		g_compatDB.edb.mTitleTable.Destroy(title->mId);
		g_compatDB.SetModified();
		g_compatDB.RefreshTitles();
	}
	ImGui::EndDisabled();

	ImGui::EndChild();

	ImGui::SameLine();

	// Right pane: aliases and tags for selected title
	ImGui::BeginChild("DetailPane", ImVec2(0, 0));

	ATCompatEDBTitle *selTitle = nullptr;
	if (g_compatDB.selectedTitle >= 0 && g_compatDB.selectedTitle < (int)g_compatDB.displayedTitles.size())
		selTitle = g_compatDB.displayedTitles[g_compatDB.selectedTitle];

	static ATCompatEDBTitle *s_lastEditTitle = nullptr;

	if (selTitle) {
		// Title name editing — only sync buffer when selection changes
		if (selTitle != s_lastEditTitle) {
			s_lastEditTitle = selTitle;
			VDStringA nameU8 = VDTextWToU8(selTitle->mName);
			strncpy(g_compatDB.titleNameBuf, nameU8.c_str(), sizeof(g_compatDB.titleNameBuf) - 1);
			g_compatDB.titleNameBuf[sizeof(g_compatDB.titleNameBuf) - 1] = 0;
		}
		ImGui::Text("Title:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1);
		if (ImGui::InputText("##titlename", g_compatDB.titleNameBuf, sizeof(g_compatDB.titleNameBuf),
			ImGuiInputTextFlags_EnterReturnsTrue)) {
			uint32 titleId = selTitle->mId;
			selTitle->mName = VDTextU8ToW(g_compatDB.titleNameBuf, -1);
			g_compatDB.SetModified();
			g_compatDB.RefreshTitles();
			// Re-select the renamed title by ID (it may have moved due to re-sort)
			for (int i = 0; i < (int)g_compatDB.displayedTitles.size(); ++i) {
				if (g_compatDB.displayedTitles[i]->mId == titleId) {
					g_compatDB.selectedTitle = i;
					break;
				}
			}
			s_lastEditTitle = nullptr;  // force re-sync after refresh
		}

		ImGui::Spacing();

		// Aliases section
		ImGui::Text("Aliases (%d):", (int)selTitle->mAliases.size());

		float halfH = (ImGui::GetContentRegionAvail().y - 30) * 0.5f;
		if (ImGui::BeginListBox("##aliases", ImVec2(-1, halfH))) {
			for (int i = 0; i < (int)selTitle->mAliases.size(); ++i) {
				VDStringW displayStr;
				for (const auto &rule : selTitle->mAliases[i].mRules) {
					if (!displayStr.empty()) displayStr += L", ";
					displayStr += rule.ToDisplayString();
				}
				VDStringA u8 = VDTextWToU8(displayStr);
				bool selected = (g_compatDB.selectedAlias == i);
				ImGui::PushID(i);
				if (ImGui::Selectable(u8.c_str(), selected))
					g_compatDB.selectedAlias = i;
				ImGui::PopID();
			}
			ImGui::EndListBox();
		}

		ImGui::BeginDisabled(g_compatDB.selectedAlias < 0);
		if (ImGui::Button("Edit Alias...") && g_compatDB.selectedAlias >= 0 &&
			g_compatDB.selectedAlias < (int)selTitle->mAliases.size()) {
			ImGui::OpenPopup("Edit Alias");
		}
		ImGui::SameLine();
		if (ImGui::Button("Delete Alias") && g_compatDB.selectedAlias >= 0 &&
			g_compatDB.selectedAlias < (int)selTitle->mAliases.size()) {
			selTitle->mAliases.erase(selTitle->mAliases.begin() + g_compatDB.selectedAlias);
			g_compatDB.SetModified();
			g_compatDB.selectedAlias = -1;
		}
		ImGui::EndDisabled();

		ImGui::SameLine();

		// Add alias from currently loaded media
		if (ImGui::Button("Add From Loaded Media")) {
			vdvector<ATCompatEDBSourcedAliasRule> srcRules;
			VDStringW sourceName;

			for (int i = 0; i < 15; ++i) {
				auto *pImage = sim.GetDiskInterface(i).GetDiskImage();
				if (pImage) {
					sourceName.sprintf(L"D%u:", i + 1);
					ATCompatAddSourcedRulesForImage(srcRules, pImage, sourceName.c_str());
				}
			}
			for (int i = 0; i < 2; ++i) {
				auto *cart = sim.GetCartridge(i);
				if (cart)
					ATCompatAddSourcedRulesForImage(srcRules, cart->GetImage(), L"Cart");
			}
			ATCompatAddSourcedRulesForImage(srcRules, sim.GetCassette().GetImage(), L"Tape");
			auto *pl = sim.GetProgramLoader();
			if (pl)
				ATCompatAddSourcedRulesForImage(srcRules, pl->GetCurrentImage(), L"Exe");

			if (!srcRules.empty()) {
				ATCompatEDBAlias alias;
				for (auto &sr : srcRules)
					alias.mRules.push_back(sr.mRule);
				selTitle->mAliases.push_back_as(std::move(alias));
				g_compatDB.SetModified();
			}
		}

		// Edit Alias popup
		// Edit Alias popup statics (must be outside inner scope)
		static vdvector<ATCompatEDBSourcedAliasRule> s_availRules;
		static bool s_availLoaded = false;

		if (ImGui::BeginPopupModal("Edit Alias", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
			if (g_compatDB.selectedAlias >= 0 &&
				g_compatDB.selectedAlias < (int)selTitle->mAliases.size()) {
				auto &alias = selTitle->mAliases[g_compatDB.selectedAlias];

				ImGui::Text("Rules in this alias:");

				// Available rules from loaded media
				if (!s_availLoaded) {
					s_availRules.clear();
					VDStringW sourceName;
					for (int d = 0; d < 15; ++d) {
						auto *pImage = sim.GetDiskInterface(d).GetDiskImage();
						if (pImage) {
							sourceName.sprintf(L"D%u:", d + 1);
							ATCompatAddSourcedRulesForImage(s_availRules, pImage, sourceName.c_str());
						}
					}
					for (int c = 0; c < 2; ++c) {
						auto *cart = sim.GetCartridge(c);
						if (cart)
							ATCompatAddSourcedRulesForImage(s_availRules, cart->GetImage(), L"Cart");
					}
					ATCompatAddSourcedRulesForImage(s_availRules, sim.GetCassette().GetImage(), L"Tape");
					auto *pl = sim.GetProgramLoader();
					if (pl)
						ATCompatAddSourcedRulesForImage(s_availRules, pl->GetCurrentImage(), L"Exe");
					s_availLoaded = true;
				}

				// Current rules
				static int s_activeRuleSel = -1;
				ImGui::Text("Active rules:");
				if (ImGui::BeginListBox("##activerules", ImVec2(400, 120))) {
					for (int r = 0; r < (int)alias.mRules.size(); ++r) {
						VDStringA u8 = VDTextWToU8(alias.mRules[r].ToDisplayString());
						ImGui::PushID(r);
						if (ImGui::Selectable(u8.c_str(), s_activeRuleSel == r))
							s_activeRuleSel = r;
						ImGui::PopID();
					}
					ImGui::EndListBox();
				}

				if (ImGui::Button("Remove Selected Rule") && s_activeRuleSel >= 0 &&
					s_activeRuleSel < (int)alias.mRules.size()) {
					alias.mRules.erase(alias.mRules.begin() + s_activeRuleSel);
					g_compatDB.SetModified();
					s_activeRuleSel = -1;
				}

				ImGui::Spacing();
				ImGui::Text("Available rules from loaded media:");
				if (ImGui::BeginListBox("##availrules", ImVec2(400, 120))) {
					for (int r = 0; r < (int)s_availRules.size(); ++r) {
						VDStringA u8 = VDTextWToU8(s_availRules[r].ToDisplayString());
						ImGui::PushID(r + 1000);
						if (ImGui::Selectable(u8.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
							if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
								// Add to alias if not already present
								bool found = false;
								for (auto &existing : alias.mRules) {
									if (existing == s_availRules[r].mRule) { found = true; break; }
								}
								if (!found) {
									alias.mRules.push_back(s_availRules[r].mRule);
									g_compatDB.SetModified();
								}
							}
						}
						ImGui::PopID();
					}
					ImGui::EndListBox();
				}

				ImGui::TextDisabled("Double-click to add a rule to the alias.");
			}

			ImGui::Spacing();
			if (ImGui::Button("Close", ImVec2(80, 0))) {
				s_availLoaded = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		ImGui::Spacing();

		// Tags section
		ImGui::Text("Tags (%d):", (int)selTitle->mTags.size());

		if (ImGui::BeginListBox("##tags", ImVec2(-1, ImGui::GetContentRegionAvail().y - 30))) {
			for (int i = 0; i < (int)selTitle->mTags.size(); ++i) {
				VDStringW displayName;
				auto knownTag = ATCompatGetKnownTagByKey(selTitle->mTags[i].c_str());
				if (knownTag)
					displayName = ATUICompatGetKnownTagDisplayName(knownTag);
				else
					displayName.sprintf(L"[%hs]", selTitle->mTags[i].c_str());

				VDStringA u8 = VDTextWToU8(displayName);
				bool selected = (g_compatDB.selectedTag == i);
				ImGui::PushID(i + 10000);
				if (ImGui::Selectable(u8.c_str(), selected))
					g_compatDB.selectedTag = i;
				ImGui::PopID();
			}
			ImGui::EndListBox();
		}

		if (ImGui::Button("Add Tag"))
			g_compatDB.showTagPicker = true;

		ImGui::SameLine();
		ImGui::BeginDisabled(g_compatDB.selectedTag < 0);
		if (ImGui::Button("Delete Tag") && g_compatDB.selectedTag >= 0 &&
			g_compatDB.selectedTag < (int)selTitle->mTags.size()) {
			selTitle->mTags.erase(selTitle->mTags.begin() + g_compatDB.selectedTag);
			g_compatDB.SetModified();
			g_compatDB.selectedTag = -1;
		}
		ImGui::EndDisabled();

	} else {
		ImGui::TextDisabled("Select a title to view details.");
	}

	ImGui::EndChild();

	// Tag picker popup
	if (g_compatDB.showTagPicker) {
		ImGui::OpenPopup("Select Tag");
		g_compatDB.showTagPicker = false;
	}

	if (ImGui::BeginPopupModal("Select Tag", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		static char tagFilter[64] = {};
		ImGui::SetNextItemWidth(300);
		ImGui::InputTextWithHint("##tagfilter", "Filter tags...", tagFilter, sizeof(tagFilter));

		if (ImGui::BeginListBox("##taglist", ImVec2(300, 300))) {
			for (int i = 1; i < kATCompatKnownTagCount; ++i) {
				ATCompatKnownTag tag = (ATCompatKnownTag)i;
				const wchar_t *wname = ATUICompatGetKnownTagDisplayName(tag);
				VDStringA name = VDTextWToU8(VDStringW(wname));

				if (tagFilter[0] && !strcasestr(name.c_str(), tagFilter))
					continue;

				if (ImGui::Selectable(name.c_str(), tag == g_compatDB.lastSelectedTag, ImGuiSelectableFlags_AllowDoubleClick)) {
					g_compatDB.lastSelectedTag = tag;
					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && selTitle) {
						const char *key = ATCompatGetKeyForKnownTag(tag);

						// Ensure tag exists in EDB
						auto r = g_compatDB.edb.mTagTable.insert_as(key);
						if (r.second) {
							r.first->second.mKey = key;
							r.first->second.mDisplayName = wname;
						}

						// Add to title if not already present
						VDStringA keyStr(key);
						if (std::find(selTitle->mTags.begin(), selTitle->mTags.end(), keyStr) == selTitle->mTags.end()) {
							selTitle->mTags.push_back_as(keyStr);
							g_compatDB.SetModified();
						}

						tagFilter[0] = 0;
						ImGui::CloseCurrentPopup();
					}
				}
			}
			ImGui::EndListBox();
		}

		if (ImGui::Button("Cancel")) {
			tagFilter[0] = 0;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	// Unsaved changes confirmation popup
	if (g_compatDB.showDiscardConfirm) {
		ImGui::OpenPopup("Unsaved Changes");
		g_compatDB.showDiscardConfirm = false;
	}
	if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::TextUnformatted("Unsaved changes will be lost. Are you sure?");
		ImGui::Spacing();
		if (ImGui::Button("Discard", ImVec2(120, 0))) {
			ImGui::CloseCurrentPopup();
			g_compatDB.Reset();
			state.showCompatDB = false;
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120, 0)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	// Compile-before-exit confirmation for external databases
	if (g_compatDB.showCompileConfirm) {
		ImGui::OpenPopup("Compile Before Exit?");
		g_compatDB.showCompileConfirm = false;
	}
	if (ImGui::BeginPopupModal("Compile Before Exit?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::TextWrapped("The external compatibility database still needs to be compiled before changes will take effect.\nCompile it before exiting?");
		ImGui::Spacing();
		if (ImGui::Button("Compile & Close", ImVec2(140, 0))) {
			ImGui::CloseCurrentPopup();
			if (!g_compatDB.compilePath.empty()) {
				try {
					vdblock<char> buf;
					ATCompileCompatEDB(buf, g_compatDB.edb);
					VDFile f(g_compatDB.compilePath.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
					f.write(buf.data(), (long)buf.size());
					f.close();
					ATCompatReloadExtDatabase();
				} catch (const MyError &e) {
					fprintf(stderr, "[AltirraSDL] Compile failed: %s\n", e.c_str());
				}
			}
			g_compatDB.Reset();
			state.showCompatDB = false;
		}
		ImGui::SameLine();
		if (ImGui::Button("Close Without Compile", ImVec2(180, 0))) {
			ImGui::CloseCurrentPopup();
			g_compatDB.Reset();
			state.showCompatDB = false;
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(80, 0)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	ImGui::End();
}
