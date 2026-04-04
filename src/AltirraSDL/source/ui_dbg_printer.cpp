//	AltirraSDL - Dear ImGui debugger printer output pane
//	Replaces Win32 ATPrinterOutputWindow (uidbgprinteroutput.cpp).
//	Displays text output from emulated printers.  Supports multiple
//	printer outputs via a dropdown selector, with Clear button.
//	Graphical printer output is displayed as a placeholder for now.

#include <stdafx.h>
#include <string>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/function.h>
#include "ui_debugger.h"
#include "console.h"
#include "debugger.h"
#include "simulator.h"
#include "printeroutput.h"

extern ATSimulator g_sim;

// =========================================================================
// Printer output pane
// =========================================================================

class ATImGuiPrinterOutputPaneImpl final : public ATImGuiDebuggerPane {
public:
	ATImGuiPrinterOutputPaneImpl();
	~ATImGuiPrinterOutputPaneImpl() override;

	bool Render() override;

private:
	void RefreshOutputList();
	void AttachToTextOutput(int index);
	void DetachFromOutput();
	void UpdateTextBuffer();

	// Current output tracking
	int mCurrentOutputIdx = -1;
	ATPrinterOutput *mpCurrentOutput = nullptr;
	size_t mLastTextOffset = 0;
	std::string mTextBuffer;		// UTF-8 converted text for ImGui display
	bool mbNeedsScroll = false;

	// Invalidation callback
	vdfunction<void()> mOnInvalidation;

	// Output list (rebuilt on open and on events)
	struct OutputInfo {
		VDStringA mName;
		bool mbIsGraphical;
		int mIndex;				// index into text or graphical output list
	};
	std::vector<OutputInfo> mOutputList;
	bool mbOutputListDirty = true;

	// Event subscriptions
	vdfunction<void(ATPrinterOutput&)> mOnAddedOutput;
	vdfunction<void(ATPrinterOutput&)> mOnRemovingOutput;
};

ATImGuiPrinterOutputPaneImpl::ATImGuiPrinterOutputPaneImpl()
	: ATImGuiDebuggerPane(kATUIPaneId_PrinterOutput, "Printer Output")
{
	// Subscribe to printer output manager events
	auto& mgr = static_cast<ATPrinterOutputManager&>(g_sim.GetPrinterOutputManager());

	mOnAddedOutput = [this](ATPrinterOutput&) {
		mbOutputListDirty = true;
	};
	mOnRemovingOutput = [this](ATPrinterOutput& output) {
		if (mpCurrentOutput == &output)
			DetachFromOutput();
		mbOutputListDirty = true;
	};

	mgr.OnAddedOutput.Add(&mOnAddedOutput);
	mgr.OnRemovingOutput.Add(&mOnRemovingOutput);

	// Auto-attach to first available output
	RefreshOutputList();
	if (!mOutputList.empty()) {
		for (int i = 0; i < (int)mOutputList.size(); ++i) {
			if (!mOutputList[i].mbIsGraphical) {
				AttachToTextOutput(mOutputList[i].mIndex);
				mCurrentOutputIdx = i;
				break;
			}
		}
	}
}

ATImGuiPrinterOutputPaneImpl::~ATImGuiPrinterOutputPaneImpl() {
	DetachFromOutput();

	auto& mgr = static_cast<ATPrinterOutputManager&>(g_sim.GetPrinterOutputManager());
	mgr.OnAddedOutput.Remove(&mOnAddedOutput);
	mgr.OnRemovingOutput.Remove(&mOnRemovingOutput);
}

void ATImGuiPrinterOutputPaneImpl::RefreshOutputList() {
	mbOutputListDirty = false;
	mOutputList.clear();

	auto& mgr = static_cast<ATPrinterOutputManager&>(g_sim.GetPrinterOutputManager());

	uint32 textCount = mgr.GetOutputCount();
	for (uint32 i = 0; i < textCount; ++i) {
		ATPrinterOutput& out = mgr.GetOutput(i);
		OutputInfo info;
		info.mName = VDTextWToU8(VDStringW(out.GetName()));
		info.mbIsGraphical = false;
		info.mIndex = (int)i;
		mOutputList.push_back(std::move(info));
	}

	uint32 gfxCount = mgr.GetGraphicalOutputCount();
	for (uint32 i = 0; i < gfxCount; ++i) {
		ATPrinterGraphicalOutput& out = mgr.GetGraphicalOutput(i);
		OutputInfo info;
		info.mName = VDTextWToU8(VDStringW(out.GetName()));
		info.mName += " (graphical)";
		info.mbIsGraphical = true;
		info.mIndex = (int)i;
		mOutputList.push_back(std::move(info));
	}
}

void ATImGuiPrinterOutputPaneImpl::AttachToTextOutput(int index) {
	DetachFromOutput();

	auto& mgr = static_cast<ATPrinterOutputManager&>(g_sim.GetPrinterOutputManager());
	if (index < 0 || index >= (int)mgr.GetOutputCount())
		return;

	mpCurrentOutput = &mgr.GetOutput(index);
	mLastTextOffset = 0;
	mTextBuffer.clear();

	// Set up invalidation callback
	mOnInvalidation = [this]() {
		// Text was added — will be picked up next frame
	};
	mpCurrentOutput->SetOnInvalidation(mOnInvalidation);

	UpdateTextBuffer();
}

void ATImGuiPrinterOutputPaneImpl::DetachFromOutput() {
	if (mpCurrentOutput) {
		mpCurrentOutput->SetOnInvalidation(vdfunction<void()>());
		mpCurrentOutput = nullptr;
	}
	mCurrentOutputIdx = -1;
}

void ATImGuiPrinterOutputPaneImpl::UpdateTextBuffer() {
	if (!mpCurrentOutput)
		return;

	size_t len = mpCurrentOutput->GetLength();
	if (len > mLastTextOffset) {
		const wchar_t *text = mpCurrentOutput->GetTextPointer(mLastTextOffset);
		size_t newChars = len - mLastTextOffset;

		// Convert wchar_t to UTF-8 for ImGui
		VDStringW wstr(text, newChars);
		VDStringA utf8 = VDTextWToU8(wstr);
		mTextBuffer.append(utf8.c_str(), utf8.size());

		mLastTextOffset = len;
		mbNeedsScroll = true;
	}

	mpCurrentOutput->Revalidate();
}

bool ATImGuiPrinterOutputPaneImpl::Render() {
	bool open = true;

	if (mbFocusRequested) {
		ImGui::SetNextWindowFocus();
		mbFocusRequested = false;
	}

	ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(mTitle.c_str(), &open)) {
		mbHasFocus = false;
		ImGui::End();
		return open;
	}
	mbHasFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

	if (mbOutputListDirty)
		RefreshOutputList();

	// Toolbar: output selector + clear button
	{
		// Output selector dropdown
		const char *currentName = (mCurrentOutputIdx >= 0 && mCurrentOutputIdx < (int)mOutputList.size())
			? mOutputList[mCurrentOutputIdx].mName.c_str()
			: "(none)";

		ImGui::SetNextItemWidth(200);
		if (ImGui::BeginCombo("##output", currentName)) {
			for (int i = 0; i < (int)mOutputList.size(); ++i) {
				bool selected = (i == mCurrentOutputIdx);
				if (ImGui::Selectable(mOutputList[i].mName.c_str(), selected)) {
					if (i != mCurrentOutputIdx) {
						if (!mOutputList[i].mbIsGraphical) {
							AttachToTextOutput(mOutputList[i].mIndex);
							mCurrentOutputIdx = i;
						} else {
							// Graphical output — show placeholder
							DetachFromOutput();
							mCurrentOutputIdx = i;
							mTextBuffer.clear();
						}
					}
				}
			}
			ImGui::EndCombo();
		}

		ImGui::SameLine();
		if (ImGui::Button("Clear")) {
			if (mpCurrentOutput) {
				mpCurrentOutput->Clear();
				mTextBuffer.clear();
				mLastTextOffset = 0;
			}
		}
	}

	ImGui::Separator();

	// Check if current selection is graphical
	bool isGraphical = (mCurrentOutputIdx >= 0 && mCurrentOutputIdx < (int)mOutputList.size()
		&& mOutputList[mCurrentOutputIdx].mbIsGraphical);

	if (isGraphical) {
		ImGui::TextDisabled("(graphical printer output — not yet supported in SDL3 build)");
	} else if (mpCurrentOutput) {
		// Update text buffer from printer output
		UpdateTextBuffer();

		// Text output area
		if (ImGui::BeginChild("PrinterText", ImVec2(0, 0), ImGuiChildFlags_None,
				ImGuiWindowFlags_HorizontalScrollbar)) {
			if (!mTextBuffer.empty())
				ImGui::TextUnformatted(mTextBuffer.c_str(), mTextBuffer.c_str() + mTextBuffer.size());

			if (mbNeedsScroll) {
				ImGui::SetScrollHereY(1.0f);
				mbNeedsScroll = false;
			}
		}
		ImGui::EndChild();
	} else {
		ImGui::TextDisabled("(no printer output available)");
	}

	// Escape → focus Console (matches Windows pattern)
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
			&& !ImGui::GetIO().WantTextInput
			&& ImGui::IsKeyPressed(ImGuiKey_Escape))
		ATUIDebuggerFocusConsole();

	ImGui::End();
	return open;
}

// =========================================================================
// Registration
// =========================================================================

void ATUIDebuggerEnsurePrinterOutputPane() {
	if (!ATUIDebuggerGetPane(kATUIPaneId_PrinterOutput)) {
		auto *pane = new ATImGuiPrinterOutputPaneImpl();
		ATUIDebuggerRegisterPane(pane);
	}
}
