//	AltirraSDL - Dear ImGui debugger memory viewer pane
//	Hex dump rendering, keyboard/mouse input, context menu.

#include <stdafx.h>
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <at/atcore/address.h>
#include <at/atdebugger/target.h>
#include "ui_dbg_memory.h"
#include "console.h"
#include "debugger.h"

extern SDL_Window *g_pWindow;

// =========================================================================
// Hex dump rendering — interpretation column (text modes)
// =========================================================================

static void RenderTextInterpretation(
	const uint8 *data, uint32 len,
	ATImGuiMemoryPaneImpl::InterpretMode mode,
	sint32 highlightIdx,	// index within row to highlight, or -1
	bool isPrimary)			// true = interp column is active selection
{
	char interpBuf[260];
	for (uint32 i = 0; i < len; ++i) {
		uint8 ch = data[i];

		if (mode == ATImGuiMemoryPaneImpl::InterpretMode::Internal) {
			static constexpr uint8 kXorTab[] = { 0x20, 0x60, 0x40, 0x00 };
			ch ^= kXorTab[(ch >> 5) & 3];
		}

		interpBuf[i] = (ch >= 0x20 && ch < 0x7F) ? (char)ch : '.';
	}
	interpBuf[len] = '\0';

	if (highlightIdx < 0 || (uint32)highlightIdx >= len) {
		// Fast path — no highlight needed
		ImGui::TextUnformatted(interpBuf, interpBuf + len);
	} else {
		// Render with one highlighted character
		if (highlightIdx > 0) {
			ImGui::TextUnformatted(interpBuf, interpBuf + highlightIdx);
			ImGui::SameLine(0, 0);
		}

		// Highlighted char
		{
			char c[2] = { interpBuf[highlightIdx], '\0' };
			ImU32 bg = isPrimary ? IM_COL32(60, 90, 180, 180)
								: IM_COL32(60, 90, 180, 80);
			ImVec2 size = ImGui::CalcTextSize(c);
			ImVec2 pos = ImGui::GetCursorScreenPos();
			ImGui::GetWindowDrawList()->AddRectFilled(
				pos, ImVec2(pos.x + size.x, pos.y + size.y), bg);
			ImGui::TextUnformatted(c);
		}

		if ((uint32)(highlightIdx + 1) < len) {
			ImGui::SameLine(0, 0);
			ImGui::TextUnformatted(interpBuf + highlightIdx + 1,
								   interpBuf + len);
		}
	}
}

// =========================================================================
// Editing display helpers
// =========================================================================

static void RenderEditCursor(sint32 editValue, int editPhase,
							 ATImGuiMemoryPaneImpl::ValueMode mode,
							 bool isWord) {
	char buf[16];

	if (mode == ATImGuiMemoryPaneImpl::ValueMode::HexBytes) {
		if (editPhase == 0)
			snprintf(buf, sizeof(buf), "_%X", editValue & 0x0F);
		else
			snprintf(buf, sizeof(buf), "%X_", (editValue >> 4) & 0x0F);
	} else if (mode == ATImGuiMemoryPaneImpl::ValueMode::HexWords) {
		// Show 4-nibble cursor
		char digits[5];
		snprintf(digits, sizeof(digits), "%04X", editValue & 0xFFFF);
		for (int i = 0; i < 4; i++)
			buf[i] = (i == editPhase) ? '_' : digits[i];
		buf[4] = '\0';
	} else if (mode == ATImGuiMemoryPaneImpl::ValueMode::DecBytes) {
		snprintf(buf, sizeof(buf), "%3d_", editValue & 0xFF);
	} else {
		snprintf(buf, sizeof(buf), "%5d_", editValue & 0xFFFF);
	}

	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
	ImGui::TextUnformatted(buf);
	ImGui::PopStyleColor();
}

// =========================================================================
// RenderHexDump — main entry
// =========================================================================

void ATImGuiMemoryPaneImpl::RenderHexDump() {
	if (mViewData.empty())
		return;

	IATDebugger *dbg = ATGetDebugger();
	if (!dbg) return;

	uint32 spaceBase = mViewStart & kATAddressSpaceMask;
	uint32 offset = mViewStart & kATAddressOffsetMask;
	uint32 spaceMask = ATAddressGetSpaceSize(mViewStart) - 1;
	uint32 totalBytes = (uint32)mViewData.size();

	// Bitmap texture for font/graphics modes
	uint32 rows = totalBytes / mColumns;
	if (rows == 0) rows = 1;
	if (IsBitmapMode())
		UpdateBitmapTexture((int)rows);

	// Context menu state (populated during rendering, consumed below)
	uint32 contextAddr = 0;
	bool contextAddrValid = false;

	// Hover tracking: clear hover highlight each frame unless selection is locked
	if (!mbSelectionEnabled)
		mHighlightedAddress.reset();

	// Zero vertical item spacing to prevent gaps from Selectable
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
		ImVec2(ImGui::GetStyle().ItemSpacing.x, 0));

	if (ImGui::BeginChild("MemoryHex", ImVec2(0, 0), ImGuiChildFlags_None,
			ImGuiWindowFlags_HorizontalScrollbar)) {

		// Apply zoom inside the child window so font scale applies here
		if (mZoomFactor != 1.0f)
			ImGui::SetWindowFontScale(mZoomFactor);

		const float charW = ImGui::CalcTextSize("0").x;

		ImGuiListClipper clipper;
		clipper.Begin((int)rows);

		while (clipper.Step()) {
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
				uint32 rowOffset = (uint32)row * mColumns;
				if (rowOffset >= totalBytes) break;

				uint32 rowAddr = spaceBase | ((offset + rowOffset) & spaceMask);
				uint32 bytesInRow = std::min(mColumns, totalBytes - rowOffset);

				// Address label
				VDStringA addrText = dbg->GetAddressText(rowAddr, true);
				ImGui::TextUnformatted(addrText.c_str());
				ImGui::SameLine(0, charW);

				// Determine if highlighted address is in this row
				sint32 hiIdx = -1;	// index within row of highlighted byte
				if (mHighlightedAddress.has_value()) {
					sint32 globalIdx = AddrToIndex(mHighlightedAddress.value());
					if (globalIdx >= (sint32)rowOffset
						&& globalIdx < (sint32)(rowOffset + bytesInRow))
						hiIdx = globalIdx - (sint32)rowOffset;
				}

				// ---- Value cells ----
				bool isByteMode = (mValueMode == ValueMode::HexBytes
								|| mValueMode == ValueMode::DecBytes);
				int step = isByteMode ? 1 : 2;

				for (uint32 col = 0; col + (step - 1) < bytesInRow; col += step) {
					uint32 idx = rowOffset + col;
					uint32 addr = IndexToAddr(idx);
					bool isHi = (hiIdx >= 0 && (sint32)col == (isByteMode ? hiIdx : (hiIdx & ~1)));
					bool isPrimary = isHi && !mbHighlightedData;
					bool editing = (mEditValue >= 0 && isHi && mbSelectionEnabled
								   && !mbHighlightedData);

					// Change detection
					bool changed = (idx < mChanged.size()) && mChanged[idx];
					if (!isByteMode && idx + 1 < mChanged.size())
						changed = changed || mChanged[idx + 1];

					if (changed)
						ImGui::PushStyleColor(ImGuiCol_Text,
							ImVec4(0.3f, 0.5f, 1.0f, 1.0f));

					ImGui::SameLine(0, charW * 0.5f);

					if (editing) {
						RenderEditCursor(mEditValue, mEditPhase,
										 mValueMode, !isByteMode);
					} else {
						// Format value
						char buf[8];
						if (mValueMode == ValueMode::HexBytes)
							snprintf(buf, sizeof(buf), "%02X",
								mViewData[idx]);
						else if (mValueMode == ValueMode::DecBytes)
							snprintf(buf, sizeof(buf), "%3u",
								mViewData[idx]);
						else if (mValueMode == ValueMode::HexWords)
							snprintf(buf, sizeof(buf), "%04X",
								mViewData[idx]
								| ((uint16)mViewData[idx+1] << 8));
						else
							snprintf(buf, sizeof(buf), "%5u",
								mViewData[idx]
								| ((uint16)mViewData[idx+1] << 8));

						// Highlight background
						if (isHi) {
							ImU32 bg = isPrimary
								? IM_COL32(60, 90, 180, 180)
								: IM_COL32(60, 90, 180, 80);
							ImVec2 sz = ImGui::CalcTextSize(buf);
							ImVec2 pos = ImGui::GetCursorScreenPos();
							ImGui::GetWindowDrawList()->AddRectFilled(
								pos,
								ImVec2(pos.x + sz.x, pos.y + sz.y),
								bg);
						}

						// Clickable cell
						ImGui::PushID((int)idx);
						if (ImGui::Selectable(buf, false,
								ImGuiSelectableFlags_None,
								ImGui::CalcTextSize(buf))) {
							mHighlightedAddress = addr;
							mbHighlightedData = false;
							mbSelectionEnabled = true;
							mEditValue = -1;
							mEditPhase = 0;
						}
						if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
							contextAddr = addr;
							contextAddrValid = true;
						}
						if (ImGui::IsItemHovered() && !mbSelectionEnabled) {
							mHighlightedAddress = addr;
							mbHighlightedData = false;
						}
						ImGui::PopID();
					}

					if (changed)
						ImGui::PopStyleColor();
				}

				// ---- Interpretation column ----
				if (mInterpretMode == InterpretMode::Atascii
					|| mInterpretMode == InterpretMode::Internal) {
					ImGui::SameLine(0, charW * 2);
					ImGui::TextUnformatted("|");
					ImGui::SameLine(0, 0);

					// Track position for click detection
					float interpX = ImGui::GetCursorScreenPos().x;
					float interpY = ImGui::GetCursorScreenPos().y;

					RenderTextInterpretation(
						&mViewData[rowOffset], bytesInRow,
						mInterpretMode, hiIdx,
						mbHighlightedData && mbSelectionEnabled);

					ImGui::SameLine(0, 0);
					ImGui::TextUnformatted("|");

					// Click detection in interpretation column
					{
						ImVec2 mouse = ImGui::GetMousePos();
						float lineH = ImGui::GetTextLineHeight();
						if (mouse.x >= interpX
							&& mouse.x < interpX + charW * bytesInRow
							&& mouse.y >= interpY
							&& mouse.y < interpY + lineH) {
							int charIdx = (int)((mouse.x - interpX) / charW);
							if (charIdx >= 0 && (uint32)charIdx < bytesInRow) {
								uint32 clickAddr = IndexToAddr(rowOffset + charIdx);
								if (ImGui::IsMouseClicked(0)) {
									mHighlightedAddress = clickAddr;
									mbHighlightedData = true;
									mbSelectionEnabled = true;
									mEditValue = -1;
									mEditPhase = 0;
								} else if (!mbSelectionEnabled) {
									mHighlightedAddress = clickAddr;
									mbHighlightedData = true;
								}
								if (ImGui::IsMouseClicked(1)) {
									contextAddr = clickAddr;
									contextAddrValid = true;
								}
							}
						}
					}

				} else if (IsBitmapMode() && mpBitmapTexture) {
					ImGui::SameLine(0, charW * 2);

					// Calculate display size
					float lineH = ImGui::GetTextLineHeight();
					float displayW, displayH;
					int rawH;

					if (IsFontMode()) {
						displayW = lineH * (float)(mColumns / 8);
						displayH = lineH;
						rawH = 8;
					} else {
						int rawW = 0;
						switch (mInterpretMode) {
							case InterpretMode::Graphics1Bpp:
								rawW = mColumns * 8; break;
							case InterpretMode::Graphics2Bpp:
								rawW = mColumns * 4; break;
							case InterpretMode::Graphics4Bpp:
								rawW = mColumns * 2; break;
							case InterpretMode::Graphics8Bpp:
								rawW = mColumns; break;
							default: break;
						}
						float pixScale = std::max(lineH * 0.5f, 1.0f);
						displayW = pixScale * rawW;
						displayH = pixScale;
						rawH = 1;
					}

					if (mBitmapTexW > 0 && mBitmapTexH > 0) {
						// Compute actual pixel width for UV
						int contentW = 0;
						switch (mInterpretMode) {
							case InterpretMode::Font1Bpp:
								contentW = mColumns; break;
							case InterpretMode::Font2Bpp:
								contentW = mColumns / 2; break;
							case InterpretMode::Graphics1Bpp:
								contentW = mColumns * 8; break;
							case InterpretMode::Graphics2Bpp:
								contentW = mColumns * 4; break;
							case InterpretMode::Graphics4Bpp:
								contentW = mColumns * 2; break;
							case InterpretMode::Graphics8Bpp:
								contentW = mColumns; break;
							default: break;
						}
						ImVec2 uv0(0, (float)(row * rawH) / mBitmapTexH);
						ImVec2 uv1((float)contentW / mBitmapTexW,
									(float)((row + 1) * rawH) / mBitmapTexH);

						ImGui::Image((ImTextureID)(intptr_t)mpBitmapTexture,
							ImVec2(displayW, displayH), uv0, uv1);
					}
				}

				ImGui::NewLine();
			}
		}

		// ================================================================
		// Keyboard input
		// ================================================================
		bool focused = ImGui::IsWindowFocused();
		bool noTextInput = !ImGui::GetIO().WantTextInput;

		if (mbSelectionEnabled && mHighlightedAddress.has_value()
			&& focused && noTextInput) {

			uint32 hiAddr = mHighlightedAddress.value();
			bool isHexMode = IsHexMode();
			bool isWordVal = IsWordMode() && !mbHighlightedData;

			// ---- Hex digit input ----
			if (isHexMode && !mbHighlightedData) {
				auto tryHexDigit = [&](int digit) {
					if (mEditValue < 0) BeginEdit();
					int maxPhase = isWordVal ? 4 : 2;
					int shift = 4 * ((maxPhase - 1) - mEditPhase);
					mEditValue = (mEditValue & ~(0xF << shift))
								| (digit << shift);
					mEditPhase++;
					if (mEditPhase >= maxPhase) {
						CommitEdit();
						uint32 nextAddr = hiAddr + StepSize();
						mHighlightedAddress = nextAddr;
						EnsureHighlightVisible();
						if (mbNeedsRebuild) RebuildView();
					}
				};

				for (ImGuiKey k = ImGuiKey_0; k <= ImGuiKey_9;
					 k = (ImGuiKey)(k + 1)) {
					if (ImGui::IsKeyPressed(k))
						tryHexDigit(k - ImGuiKey_0);
				}
				for (ImGuiKey k = ImGuiKey_A; k <= ImGuiKey_F;
					 k = (ImGuiKey)(k + 1)) {
					if (ImGui::IsKeyPressed(k))
						tryHexDigit(10 + k - ImGuiKey_A);
				}
			}

			// ---- Decimal digit input ----
			if (!isHexMode && !mbHighlightedData) {
				int maxVal = isWordVal ? 0xFFFF : 0xFF;
				for (ImGuiKey k = ImGuiKey_0; k <= ImGuiKey_9;
					 k = (ImGuiKey)(k + 1)) {
					if (ImGui::IsKeyPressed(k)) {
						if (mEditValue < 0) mEditValue = 0;
						sint32 nv = mEditValue * 10 + (k - ImGuiKey_0);
						if (nv <= maxVal)
							mEditValue = nv;
					}
				}
			}

			// ---- ATASCII / Internal typing in interp column ----
			if (mbHighlightedData
				&& (mInterpretMode == InterpretMode::Atascii
					|| mInterpretMode == InterpretMode::Internal)) {
				ImGuiIO& io = ImGui::GetIO();
				for (int n = 0; n < io.InputQueueCharacters.Size; n++) {
					ImWchar c = io.InputQueueCharacters[n];
					if (c >= 0x20 && c < 0x7F) {
						uint8 byte = (uint8)c;
						if (mInterpretMode == InterpretMode::Internal) {
							// Reverse ATASCII→Internal transform
							switch (byte & 0x60) {
								case 0x20: byte ^= 0x20; break;
								case 0x40: byte ^= 0x60; break;
								case 0x60: break;
							}
						}
						IATDebugTarget *t = mLastState.mpDebugTarget;
						if (t && mHighlightedAddress.has_value()) {
							uint32 curAddr = mHighlightedAddress.value();
							t->WriteByte(curAddr, byte);
							mbNeedsRebuild = true;
							mHighlightedAddress = curAddr + 1;
							EnsureHighlightVisible();
							if (mbNeedsRebuild) RebuildView();
						}
					}
				}
				io.InputQueueCharacters.resize(0);
			}

			// ---- Enter: commit + advance ----
			if (ImGui::IsKeyPressed(ImGuiKey_Enter)
				|| ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
				CommitEdit();
				mHighlightedAddress = hiAddr + StepSize();
				EnsureHighlightVisible();
				if (mbNeedsRebuild) RebuildView();
			}

			// ---- Tab: toggle hex ↔ interp column ----
			if (ImGui::IsKeyPressed(ImGuiKey_Tab)) {
				CommitEdit();
				mbHighlightedData = !mbHighlightedData;
			}

			// ---- Escape: cancel edit or focus console ----
			if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
				if (mEditValue >= 0) {
					mEditValue = -1;
					mEditPhase = 0;
					mbEditCancelledThisFrame = true;
				} else {
					CancelEdit();
					mbEditCancelledThisFrame = true;
				}
			}

			// ---- Arrow keys ----
			bool ctrl = ImGui::GetIO().KeyCtrl;

			if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
				CommitEdit();
				sint32 off = (sint32)(hiAddr & kATAddressOffsetMask);
				if (off >= StepSize()) {
					mHighlightedAddress = (hiAddr & kATAddressSpaceMask)
						| (off - StepSize());
					EnsureHighlightVisible();
				}
			}
			if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
				CommitEdit();
				uint32 off = hiAddr & kATAddressOffsetMask;
				uint32 maxOff = ATAddressGetSpaceSize(hiAddr) - 1;
				if (off + StepSize() <= maxOff) {
					mHighlightedAddress = (hiAddr & kATAddressSpaceMask)
						| (off + StepSize());
					EnsureHighlightVisible();
				}
			}
			if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
				if (ctrl) {
					// Scroll view without moving selection
					uint32 off = mViewStart & kATAddressOffsetMask;
					if (off >= mColumns) {
						mViewStart = (mViewStart & kATAddressSpaceMask)
							| (off - mColumns);
						mbNeedsRebuild = true;
					}
				} else {
					CommitEdit();
					sint32 off = (sint32)(hiAddr & kATAddressOffsetMask);
					if (off >= (sint32)mColumns) {
						mHighlightedAddress = (hiAddr & kATAddressSpaceMask)
							| (off - mColumns);
						EnsureHighlightVisible();
					}
				}
			}
			if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
				uint32 maxOff = ATAddressGetSpaceSize(hiAddr) - 1;
				if (ctrl) {
					uint32 off = mViewStart & kATAddressOffsetMask;
					if (off + mColumns <= maxOff) {
						mViewStart = (mViewStart & kATAddressSpaceMask)
							| (off + mColumns);
						mbNeedsRebuild = true;
					}
				} else {
					CommitEdit();
					uint32 off = hiAddr & kATAddressOffsetMask;
					if (off + mColumns <= maxOff) {
						mHighlightedAddress = (hiAddr & kATAddressSpaceMask)
							| (off + mColumns);
						EnsureHighlightVisible();
					}
				}
			}
		}

		// ---- Escape → focus Console (when no selection) ----
		if (!mbSelectionEnabled && !mbEditCancelledThisFrame
			&& ImGui::IsWindowFocused()
			&& !ImGui::GetIO().WantTextInput
			&& ImGui::IsKeyPressed(ImGuiKey_Escape))
			ATUIDebuggerFocusConsole();

		// ---- Page Up / Down ----
		if (ImGui::IsWindowFocused()) {
			uint32 spBase = mViewStart & kATAddressSpaceMask;
			uint32 off = mViewStart & kATAddressOffsetMask;
			uint32 scrollBytes = mColumns * mVisibleRows;

			if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
				off = (off >= scrollBytes) ? off - scrollBytes : 0;
				mViewStart = spBase | off;
				mbNeedsRebuild = true;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
				uint32 maxOff = ATAddressGetSpaceSize(mViewStart) - 1;
				off = std::min(off + scrollBytes, maxOff);
				mViewStart = spBase | off;
				mbNeedsRebuild = true;
			}
		}

		// ================================================================
		// Mouse wheel: scroll or zoom (Ctrl)
		// ================================================================
		float wheel = ImGui::GetIO().MouseWheel;
		if (wheel != 0 && ImGui::IsWindowHovered()) {
			if (ImGui::GetIO().KeyCtrl) {
				// Ctrl+wheel = zoom
				float nz = std::clamp(
					mZoomFactor * std::pow(2.0f, 0.1f * wheel),
					0.1f, 10.0f);
				if (std::fabs(nz - 1.0f) < 0.05f) nz = 1.0f;
				mZoomFactor = nz;
			} else {
				// Normal wheel = scroll
				sint32 scrollRows = -(sint32)(wheel * 3);
				sint32 newOff = (sint32)(mViewStart & kATAddressOffsetMask)
					+ scrollRows * (sint32)mColumns;
				if (newOff < 0) newOff = 0;
				uint32 maxAddr = ATAddressGetSpaceSize(mViewStart) - 1;
				if ((uint32)newOff > maxAddr)
					newOff = (sint32)maxAddr;
				mViewStart = (mViewStart & kATAddressSpaceMask)
					| (uint32)newOff;
				mbNeedsRebuild = true;
			}
		}

		// ================================================================
		// Context menu
		// ================================================================
		if (contextAddrValid) {
			mContextMenuAddr = contextAddr;
			ImGui::OpenPopup("MemCtx");
		}
		if (ImGui::BeginPopup("MemCtx")) {
			IATDebugger *d = ATGetDebugger();
			if (d) {
				VDStringA addrStr = d->GetAddressText(
					mContextMenuAddr, true, true);
				ImGui::TextDisabled("%s", addrStr.c_str());
				ImGui::Separator();

				// Show Values As submenu
				if (ImGui::BeginMenu("Show Values As")) {
					if (ImGui::MenuItem("Bytes (Hex)",
						nullptr, mValueMode == ValueMode::HexBytes))
						{ mValueMode = ValueMode::HexBytes;
						  SetColumns(mColumns); CancelEdit(); }
					if (ImGui::MenuItem("Bytes (Dec)",
						nullptr, mValueMode == ValueMode::DecBytes))
						{ mValueMode = ValueMode::DecBytes;
						  SetColumns(mColumns); CancelEdit(); }
					if (ImGui::MenuItem("Words (Hex)",
						nullptr, mValueMode == ValueMode::HexWords))
						{ mValueMode = ValueMode::HexWords;
						  SetColumns(mColumns); CancelEdit(); }
					if (ImGui::MenuItem("Words (Dec)",
						nullptr, mValueMode == ValueMode::DecWords))
						{ mValueMode = ValueMode::DecWords;
						  SetColumns(mColumns); CancelEdit(); }
					ImGui::EndMenu();
				}

				// Interpret As submenu
				if (ImGui::BeginMenu("Interpret As")) {
					auto im = [&](const char *l, InterpretMode m) {
						if (ImGui::MenuItem(l, nullptr,
							mInterpretMode == m)) {
							mInterpretMode = m;
							SetColumns(mColumns);
						}
					};
					im("None",               InterpretMode::None);
					im("ATASCII",            InterpretMode::Atascii);
					im("INTERNAL",           InterpretMode::Internal);
					im("1-bpp Font",         InterpretMode::Font1Bpp);
					im("2-bpp Font",         InterpretMode::Font2Bpp);
					im("1-bpp Graphics",     InterpretMode::Graphics1Bpp);
					im("2-bpp Graphics (Wide)", InterpretMode::Graphics2Bpp);
					im("4-bpp Graphics (Wide)", InterpretMode::Graphics4Bpp);
					im("8-bpp Graphics",     InterpretMode::Graphics8Bpp);
					ImGui::EndMenu();
				}

				// Reset Zoom
				if (ImGui::MenuItem("Reset Zoom",
					nullptr, false, mZoomFactor != 1.0f))
					mZoomFactor = 1.0f;

				ImGui::Separator();

				if (ImGui::MenuItem("Toggle Read Breakpoint"))
					d->ToggleAccessBreakpoint(mContextMenuAddr, false);
				if (ImGui::MenuItem("Toggle Write Breakpoint"))
					d->ToggleAccessBreakpoint(mContextMenuAddr, true);

				ImGui::Separator();

				if (ImGui::MenuItem("Add to Watch Window")) {
					VDStringA expr;
					if (IsWordMode())
						expr.sprintf("dw $%04X",
							mContextMenuAddr & 0xFFFF);
					else
						expr.sprintf("db $%04X",
							mContextMenuAddr & 0xFFFF);
					ATUIDebuggerAddToWatch(expr.c_str());
				}
				if (ImGui::MenuItem("Track On-Screen")) {
					int len = IsWordMode() ? 2 : 1;
					if (d->AddWatch(mContextMenuAddr, len) < 0) {
						// All 8 on-screen watch slots are full
					}
				}
			}
			ImGui::EndPopup();
		}
	}
	ImGui::EndChild();
	ImGui::PopStyleVar();	// ItemSpacing
}
