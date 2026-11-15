// Standalone SDL diagnostic: deliberately does not create a renderer.
// Build/run instructions: docs/shader-startup-recovery.md.
#include <SDL3/SDL.h>
#include <cstdio>

int main(int argc, char **argv) {
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		std::fprintf(stderr, "SDL video initialization failed: %s\n", SDL_GetError());
		return 1;
	}
	const SDL_WindowFlags flags = argc > 1 && !SDL_strcmp(argv[1], "--hidden")
		? SDL_WINDOW_HIDDEN : 0;
	const Uint64 started = SDL_GetTicksNS();
	std::fprintf(stderr, "Creating %s window; SDL %d, video=%s\n",
		flags ? "hidden" : "shown", SDL_GetVersion(), SDL_GetCurrentVideoDriver());
	SDL_Window *window = SDL_CreateWindow("SDL window startup probe", 320, 240, flags);
	std::fprintf(stderr, "Window %s after %.1f ms%s%s\n",
		window ? "ready" : "failed", (SDL_GetTicksNS() - started) / 1e6,
		window ? "" : ": ", window ? "" : SDL_GetError());
	const int result = window ? 0 : 1;
	if (window) SDL_DestroyWindow(window);
	SDL_Quit();
	return result;
}
