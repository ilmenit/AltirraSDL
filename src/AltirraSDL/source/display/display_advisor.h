// SDL3 extension: optional, reversible graphics-performance suggestions.
#pragma once
#include "uitypes.h"

struct SDL_Window;
class IDisplayBackend;
namespace ATDisplayAdvisor {
void Update();
void Observe(SDL_Window *window, IDisplayBackend *backend,
	double workMs, double presentMs, double budgetMs, bool eligible);
void RenderUI(bool gaming);
void Cancel();
bool Active();
bool TrialActive();
bool NeedsObservation();
bool EffectiveEffects(bool stored);
ATDisplayFilterMode EffectiveFilter(ATDisplayFilterMode stored);
// Explicit automation only; feeds the same policy without delaying the UI.
struct Diagnostics { double work, present, allowance; unsigned warmup, slow; bool eligible; };
Diagnostics GetDiagnostics();
void InjectTest(const char *scenario);
}
