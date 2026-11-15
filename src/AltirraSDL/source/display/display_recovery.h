// SDL3 extension: graphics recovery has no native Windows counterpart.
#pragma once
#include "display_backend.h"

namespace ATDisplayRecovery {
// Main-thread only. These calls never replace live graphics resources.
void Begin(ATDisplayBackendPreference& preference, bool explicitOverride, bool gaming);
void BeforeWindow();
void AfterWindow(bool created);
void Created(DisplayBackendType actual);
void BeforeFrame(bool hasTexture);
void AfterFrame(bool healthy);
void RenderUI(bool gaming);
void Suspend();
bool Active();
bool ContextLost();
void InjectTest(const char *event);
const char *Notice();
const char *ActualRenderer();
}
