# Main Loop and Frontend Entry Point

## Current Architecture (Windows)

The Windows main loop in `src/Altirra/source/main.cpp` is:

```
WinMain()
    → Initialize COM, window classes, create main window
    → g_sim.Init()
    → Enter message loop:
        for(;;) {
            ATUIProcessMessages()     // Dispatch Win32 messages
            g_pATIdle(false)          // Run emulation in idle time
                → Frame timing via MsgWaitForMultipleObjects
                → g_sim.Advance(dropFrame)
                → Update display if frame changed
        }
    → g_sim.Shutdown()
```

Key characteristics:

- **Message-driven**: Emulation runs in the idle callback between Windows
  messages
- **Win32 timing**: Uses `MsgWaitForMultipleObjects` with a waitable timer
  for precise frame pacing
- **Global state**: `g_sim`, `g_pDisplay`, `g_pATIdle` are globals
- **Single-threaded**: Emulation and UI run on the same thread

## SDL3 Main Loop

The SDL3 frontend uses a straightforward frame-based loop:

```cpp
// src/AltirraSDL/source/main_sdl3.cpp

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include "simulator.h"
#include "display_sdl3.h"
#include "input_sdl3.h"
#include "ui_state.h"

int main(int argc, char *argv[]) {
    // Initialize SDL3
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD | SDL_INIT_TIMER);

    SDL_Window *window = SDL_CreateWindow("Altirra",
        800, 600, SDL_WINDOW_RESIZABLE);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, nullptr);

    // Initialize Dear ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    // Initialize emulator
    // Note: sim.Init() calls ATCreateAudioOutput() internally.
    // On the SDL3 build, the factory returns ATAudioOutputSDL3
    // (see AUDIO.md for how this is handled at build time).
    ATSimulator sim;
    sim.Init();

    // Create SDL3 display implementation and connect to GTIA
    VDVideoDisplaySDL3 display(window, renderer);
    sim.GetGTIA().SetVideoOutput(&display);

    // Initialize the native audio device (SDL3 audio stream)
    sim.GetAudioOutput()->InitNativeAudio();

    // Register callback for when a blocked Advance() can retry.
    // This fires when the display consumes a frame, unblocking GTIA.
    bool advanceUnblocked = false;
    sim.SetOnAdvanceUnblocked([&advanceUnblocked]() {
        advanceUnblocked = true;
    });

    // Set up JSON-based configuration (replaces Windows registry)
    // (Done in system library init, see SYSTEM.md)

    // Load ROM/disk from command line
    if (argc > 1) {
        ATMediaLoadContext ctx;
        sim.Load(argv[1], kATMediaWriteMode_VRWSafe, ctx);
    }

    sim.ColdReset();

    // Main loop
    bool running = true;
    uint64 lastFrameTime = SDL_GetPerformanceCounter();
    uint64 perfFreq = SDL_GetPerformanceFrequency();

    while (running) {
        // --- Event Processing ---
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);

            switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;

            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                if (!ImGui::GetIO().WantCaptureKeyboard)
                    HandleKeyEvent(sim, event);
                break;

            case SDL_EVENT_MOUSE_MOTION:
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (!ImGui::GetIO().WantCaptureMouse)
                    HandleMouseEvent(sim, event);
                break;

            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
                HandleGamepadEvent(sim, event);
                break;

            case SDL_EVENT_DROP_FILE:
                HandleFileDrop(sim, event);
                break;
            }
        }

        // --- Emulation Advance ---
        if (sim.IsRunning() && !sim.IsPaused()) {
            advanceUnblocked = false;

            ATSimulator::AdvanceResult ar = sim.Advance(false);

            if (ar == ATSimulator::kAdvanceResult_WaitingForFrame) {
                // GTIA tried to submit a frame via PostBuffer() but the
                // display's frame queue was full. This means a frame was
                // already submitted and is waiting to be presented.
                // The SetOnAdvanceUnblocked callback will fire when the
                // display consumes the frame. We present it now.
            }
        }

        // --- Rendering ---
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        // Render emulator frame
        display.Present(renderer);

        // Render Dear ImGui UI
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        RenderUI(sim, display);

        ImGui::Render();
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

        SDL_RenderPresent(renderer);

        // --- Frame Timing ---
        FramePacing(sim, lastFrameTime, perfFreq);
    }

    // Cleanup
    sim.Shutdown();

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
```

## Frame Timing

The Atari runs at ~59.92 Hz (NTSC) or ~49.86 Hz (PAL). The main loop must
pace `Advance()` calls to match.

### Strategy: SDL3 Performance Counter

Frame timing uses a `FramePacer` struct with an error accumulator, ported
from the Windows idle handler in `main.cpp`.  Exact frame rates come from
`constants.h` (`kATFrameRate_NTSC`, `kATFrameRate_PAL`, `kATFrameRate_SECAM`).

Three frame rate modes are supported via a `kFramePeriods[3][3]` lookup
table (rows = mode, cols = video standard), matching Windows
`ATUIUpdateSpeedTiming()`:

| Mode | NTSC | PAL | SECAM |
|------|------|-----|-------|
| Hardware | 59.9227 Hz | 49.8607 Hz | 50.0818 Hz |
| Broadcast | 59.94 Hz | 50.00 Hz | 50.00 Hz |
| Integral | 60.00 Hz | 50.00 Hz | 50.00 Hz |

The error accumulator tracks cumulative timing drift in performance counter
ticks (positive = ahead of schedule, need to sleep).  It clamps to ±2
frames to prevent runaway drift after glitches.  `SDL_DelayPrecise()`
provides nanosecond-precision delay, replacing the Windows waitable timer
approach.

### VSync Alternative

If VSync is enabled (`SDL_SetRenderVSync(renderer, 1)`), `SDL_RenderPresent`
blocks until the next vertical blank. In this mode, frame timing is driven
by the display refresh rate. If the display is 60 Hz and the Atari is
~59.92 Hz, there will be occasional frame drops (~1 per 12 seconds) or
we can use adaptive timing that lets the emulation slightly adjust speed to
match the display.

For most users, VSync on a 60 Hz display provides the best experience.
`SDL_DelayPrecise` timing is the fallback for displays with non-standard
refresh rates.

## Keyboard Shortcuts (main event handler)

| Key | Action |
|-----|--------|
| F7 | Quick Load State (in-memory) |
| F8 | Quick Save State (in-memory) |
| F9 | Toggle Pause (via menu) |
| F11 | Warm Reset |
| F12 | Cold Reset |
| Alt+Enter | Toggle Fullscreen |

## Key Differences from Windows Main Loop

| Aspect | Windows | SDL3 |
|--------|---------|------|
| Event dispatch | `TranslateMessage` / `DispatchMessage` | `SDL_PollEvent` |
| Idle processing | `WaitMessage` when nothing to do | Busy loop with frame pacing |
| Frame timing | `MsgWaitForMultipleObjects` + waitable timer | `SDL_DelayPrecise` or VSync |
| UI rendering | Win32 paints via `WM_PAINT` | Dear ImGui immediate-mode in main loop |
| Thread model | Single thread, message pump | Single thread, poll loop |

The fundamental model is the same: single-threaded, one frame of emulation
per iteration, display update after each frame. The difference is how we
wait for the next frame boundary.

## Interaction with ATSimulator

The SDL3 frontend needs to call these `ATSimulator` methods:

### Lifecycle
- `Init()` -- once at startup
- `Shutdown()` -- once at exit
- `ColdReset()` / `WarmReset()` -- on user request
- `Resume()` / `Suspend()` / `Pause()` -- run control

### Per-Frame
- `Advance(bool dropFrame)` -- run one frame of emulation
  - Returns `kAdvanceResult_Running` (still processing), `kAdvanceResult_Stopped`
    (emulation halted), or `kAdvanceResult_WaitingForFrame` (frame queue full,
    display must present before emulation can continue)
- `SetOnAdvanceUnblocked(vdfunction<void()>)` -- registers callback fired when
  a blocked `Advance` can retry (i.e., display consumed a queued frame)
- `GetGTIA().GetPresentedFrameCounter()` -- check if new frame ready

### Configuration
- `SetHardwareMode()`, `SetMemoryMode()`, `SetVideoStandard()`, etc.
- `GetFirmwareManager()`, `GetDeviceManager()` -- device/ROM configuration
- `Load()` -- load disk/cartridge/tape images

### Component Access
- `GetInputManager()` -- for routing input
- `GetGTIA()` -- for connecting display
- `GetAudioOutput()` -- for audio
- `GetDebugger()` -- for debugger (Phase 4)

## Frontend Project Structure

```
src/AltirraSDL/
    source/
        main_sdl3.cpp          -- Entry point, main loop, frame timing
        display_sdl3.cpp       -- VDVideoDisplaySDL3
        display_sdl3_impl.h    -- Display class header
        input_sdl3.cpp         -- SDL3 keyboard/mouse → POKEY/InputManager
        joystick_sdl3.cpp      -- SDL3 gamepad → IATJoystickManager
        ui_main.h              -- ATUIState struct, render declarations
        ui_main.cpp            -- Menu bar (10 menus), About dialog, status overlay
        ui_system.cpp          -- Configure System (20-page paged dialog)
        ui_disk.cpp            -- Disk Drives dialog
        ui_cassette.cpp        -- Cassette Tape Control dialog
    stubs/
        uiaccessors_stubs.cpp  -- ATUIGet/Set* functions, g_kbdOpts, g_ATUIManager
        oshelper_stubs.cpp     -- OS helper stubs
        console_stubs.cpp      -- Console/debugger stubs
        uirender_stubs.cpp     -- UI renderer stubs
        win32_stubs.cpp        -- Win32 API stubs
        device_stubs.cpp       -- Device button/network stubs
src/ATAudio/source/
    audiooutput_sdl3.cpp       -- ATAudioOutputSDL3 (IATAudioOutput factory)
```

## Speed Control and Audio Rate Sync

`UpdatePacerRate()` is called after each presented frame.  It mirrors
`ATUIUpdateSpeedTiming()` from Windows `main.cpp`:

1. Reads frame rate mode (`ATUIGetFrameRateMode()`) and video standard to
   look up `rawSecsPerFrame` from the `kFramePeriods` table.
2. Computes `cyclesPerSecond = kMasterClocks[vstd] * kFramePeriods[0][vstd]
   / rawSecsPerFrame` — this adjusts the audio clock so pitch stays correct
   when the frame rate mode differs from hardware rate.
3. Computes speed factor: `modifier + 1.0`, with 0.5x slow-motion factor
   (matching Windows `g_ATCVEngineSlowmoScale`).  Speed modifier is skipped
   in turbo mode.  Clamped to [0.01, 100.0].
4. Updates `g_pacer` with the speed-adjusted frame rate.
5. Calls `IATAudioOutput::SetCyclesPerSecond(cyclesPerSecond, 1.0/rate)`.

When `ATUIGetTurbo()` or `ATUIGetTurboPulse()` is set, the main loop skips
the frame pacer's `WaitForNextFrame()` call, allowing emulation to run as
fast as the CPU allows.  The stubs wire `ATUISetTurbo` directly to
`g_sim.SetTurboModeEnabled()`.

## Window Focus and Pause-When-Inactive

The main loop tracks `g_winActive` via `SDL_EVENT_WINDOW_FOCUS_GAINED` and
`SDL_EVENT_WINDOW_FOCUS_LOST`.  When `ATUIGetPauseWhenInactive()` is true
and the window is inactive, the loop skips `Advance()` and renders only UI
at 16ms intervals.  This matches the Windows `g_pauseInactive` + `g_winActive`
behavior.

## File Drop

`SDL_EVENT_DROP_FILE` loads the dropped file via `g_sim.Load()` with
`kATMediaWriteMode_VRWSafe` and triggers a cold reset, matching the Windows
drag-and-drop behavior.

## Global State

The Windows build uses globals (`g_sim`, `g_pDisplay`). The SDL3 frontend
does the same for compatibility with shared code that references `g_sim`.
UI functions receive the simulator by reference from `ATUIRenderFrame()`.
