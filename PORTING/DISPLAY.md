# Display and Rendering

## Current Architecture (Windows)

The rendering pipeline on Windows:

```
GTIA/ANTIC emulation
    |  (produces VDPixmap framebuffers)
    v
IVDVideoDisplay          -- clean interface, no Win32 types
    |
    v
IVDVideoDisplayMinidriver -- CONTAMINATED: HWND, HMONITOR, HDC, RECT in signatures
    |
    v
VDD3D9Manager            -- Direct3D 9 device, swap chains, textures
    |
    v
Screen
```

`IVDVideoDisplay` (in `display.h`) is the high-level interface the emulation
calls. Its methods use platform-neutral types (`VDPixmap`, `vdrect32f`,
`uint32`). It is fully portable.

`IVDVideoDisplayMinidriver` (in `displaydrv.h`) is the low-level driver that
`IVDVideoDisplay` delegates to. Its signatures are Win32-specific:

```cpp
bool PreInit(HWND hwnd, HMONITOR hmonitor);
bool Init(HWND hwnd, HMONITOR hmonitor, const VDVideoDisplaySourceInfo& info);
bool Paint(HDC hdc, const RECT& rClient, UpdateMode);
```

This interface cannot be implemented on non-Windows. The SDL3 build bypasses
it entirely.

## SDL3 Architecture

```
GTIA/ANTIC emulation
    |  (produces VDPixmap framebuffers)
    v
VDVideoDisplaySDL3       -- implements IVDVideoDisplay directly
    |                       (no minidriver layer)
    v
SDL_Renderer + SDL_Texture (or SDL_GPU for advanced rendering)
    |
    v
SDL_Window
    |
    v
Screen
```

### VDVideoDisplaySDL3 Class

A new class implementing `IVDVideoDisplay` that uses SDL3 for presentation:

```cpp
class VDVideoDisplaySDL3 : public IVDVideoDisplay {
public:
    VDVideoDisplaySDL3(SDL_Renderer *renderer, int w, int h);

    // IVDVideoDisplay implementation (key methods shown)
    void SetSource(bool bAutoUpdate, const VDPixmap& px, bool interlaced) override;
    void PostBuffer(VDVideoDisplayFrame *frame) override;
    bool RevokeBuffer(bool allowFrameSkip, VDVideoDisplayFrame **ppFrame) override;
    void Update(FieldMode) override;
    void SetFilterMode(FilterMode) override;
    void SetCompositor(IVDDisplayCompositor *) override;
    void SetDestRectF(const vdrect32f *r, uint32 backgroundColor) override;
    void FlushBuffers() override;
    // ... remaining methods (see display.h for full list)

    // SDL3-specific: called by the main loop to draw the current frame
    void Present(SDL_Renderer *renderer);
    SDL_Texture *GetFrameTexture() const { return mpFrameTexture; }

private:
    SDL_Window *mpWindow;
    SDL_Renderer *mpRenderer;
    SDL_Texture *mpFrameTexture = nullptr;  // streaming texture for emulator output

    IVDDisplayCompositor *mpCompositor = nullptr;
    FilterMode mFilterMode = kFilterPoint;
    bool mFrameReady = false;
    uint32 mCurrentFrameNumber = 0;
};
```

### Frame Submission Flow

GTIA uses a **frame queue** protocol, not a simple set-and-forget call. The
key class is `VDVideoDisplayFrame` (defined in `display.h`):

```cpp
class VDVideoDisplayFrame : public vdlist_node, public IVDRefCount {
public:
    VDPixmap    mPixmap {};
    uint32      mFlags = 0;
    uint32      mFrameNumber = 0;
    bool        mbAllowConversion = false;
    IVDVideoDisplayScreenFXEngine *mpScreenFXEngine = nullptr;
    const VDVideoDisplayScreenFXInfo *mpScreenFX = nullptr;
};
```

1. GTIA calls `PostBuffer(VDVideoDisplayFrame*)` to submit a completed frame.
   The frame object contains a `VDPixmap` with raw pixel data (typically
   palettized 8-bit or 32-bit XRGB) plus metadata like the frame number
   and screen FX settings.

2. `VDVideoDisplaySDL3::PostBuffer()` queues the frame by reference-counting
   it into `mPendingFrame`.  It does **not** upload pixels — that happens
   later in `PrepareFrame()`.

3. `PrepareFrame()` (called from the main loop each iteration) uploads
   the pending frame's pixel data to an SDL streaming texture.  It handles:
   - Texture creation/destruction when frame dimensions change
   - Pal8 → XRGB8888 palette conversion
   - Moving consumed frames to `mPrevFrame` so GTIA can reclaim them
     via `RevokeBuffer()`

4. The main loop's `RenderAndPresent()` drives the final rendering:

```cpp
static void RenderAndPresent() {
    SDL_RenderClear(g_pRenderer);

    // Update filter mode on texture if setting changed.
    if (curFilter != s_lastAppliedFilter)
        g_pDisplay->UpdateScaleMode();

    SDL_Texture *emuTex = g_pDisplay->GetTexture();
    if (emuTex) {
        g_displayRect = ComputeDisplayRect();  // scaling/aspect/zoom/pan
        SDL_SetTextureBlendMode(emuTex, SDL_BLENDMODE_NONE);
        SDL_RenderTexture(g_pRenderer, emuTex, nullptr, &g_displayRect);
    }

    ATUIRenderFrame(g_sim, *g_pDisplay, g_pRenderer, g_uiState);
    SDL_RenderPresent(g_pRenderer);
}
```

### Pixel Format Handling

GTIA outputs frames in several formats depending on configuration. The
primary format is `nsVDPixmap::kPixFormat_XRGB8888`. SDL3 textures support
this directly as `SDL_PIXELFORMAT_XRGB8888`.

For PAL blending and other effects, GTIA may output in different formats.
Handle the common ones:

| VDPixmap Format | SDL3 Pixel Format |
|----------------|-------------------|
| `kPixFormat_XRGB8888` | `SDL_PIXELFORMAT_XRGB8888` |
| `kPixFormat_RGB565` | `SDL_PIXELFORMAT_RGB565` |
| `kPixFormat_Pal8` | Requires manual palette conversion |

For paletted output, convert to XRGB8888 before uploading to the texture
(the palette is available from GTIA). This avoids SDL3 palette surface
complexity.

### Scaling and Aspect Ratio

Atari display is not square-pixel. NTSC pixels have an aspect ratio of
approximately 12:7. The display must handle:

- **Aspect ratio correction**: Scale the 336-pixel-wide output to the correct
  visual width
- **Integer scaling**: Optional mode that scales to the nearest integer
  multiple for pixel-sharp display
- **Stretch modes**: Fit, fill, and custom destination rectangle

`ComputeDisplayRect()` in `main_sdl3.cpp` implements all scaling logic,
ported from `ATDisplayPane::ResizeDisplay()` in `uidisplay.cpp`.  It reads
`ATUIGetDisplayStretchMode()`, GTIA's `GetRawFrameFormat()` /
`GetFrameSize()` / `GetPixelAspectRatio()`, plus zoom/pan settings.  All 5
stretch modes are supported: Unconstrained, PreserveAspectRatio,
SquarePixels, Integral, and IntegralPreserveAspectRatio.  Integer scaling
uses `floor(zoom * 1.0001)` for rounding-error tolerance.  The resulting
`SDL_FRect` is pixel-snapped and passed to `SDL_RenderTexture()`.

The `SetDestRect()` / `SetDestRectF()` / `SetPixelSharpness()` overrides
on `VDVideoDisplaySDL3` remain stubs — scaling is handled entirely by the
main loop's `ComputeDisplayRect()` + `SDL_RenderTexture()` path.

### Screen Effects

The Windows build supports scanline simulation, bloom, distortion, screen
masks, sharp bilinear, bicubic, gamma/color correction, and HDR via D3D9
HLSL shaders. The SDL3 build implements these using OpenGL 3.3 GLSL shaders.

See [SHADERS.md](SHADERS.md) for the full architecture, implementation
phases, and optional librashader integration for RetroArch shader presets.

The `SetScreenFX()` method on `IVDVideoDisplay` carries the effect
configuration. The `ATArtifactingParams` and `VDDScreenMaskParams` structures
hold all tunable parameters, persisted via `settings.cpp` under `ScreenFX:`
registry keys.

### Compositor Integration

The `IVDDisplayCompositor` interface is clean (no Win32 types). It allows
ATUI widgets (status bar, indicators) to overlay on the video output. The
compositor calls `Composite(IVDDisplayRenderer&, ...)` to draw overlays.

For the SDL3 build, two options:

**Option A: Keep ATUI compositor.** Implement `IVDDisplayRenderer` for SDL3
(draw rectangles, blit textures, render text using SDL3 renderer). This
preserves the existing on-screen display exactly.

**Option B: Replace with Dear ImGui overlays.** Render status information as
ImGui widgets on top of the emulator frame. Simpler, and consistent with the
rest of the SDL3 UI.

Recommended: **Option B** for the SDL3 build. The ATUI compositor is tightly
integrated with the VDDisplay rendering pipeline, and reimplementing
`IVDDisplayRenderer` for SDL3 is significant work for marginal benefit when
Dear ImGui is already available.

### VSync

SDL3 renderer supports vsync via `SDL_RENDERER_PRESENTVSYNC` property. The
emulator already has frame timing logic (the scheduler runs at the Atari's
native frame rate). The SDL3 main loop should:

1. Run `Advance()` to produce a frame
2. Call `SDL_RenderPresent()` (which blocks if vsync is enabled)
3. Measure actual frame time to detect if we're falling behind

Do not use the Windows-style `MsgWaitForMultipleObjects` timing approach.
SDL3's `SDL_DelayPrecise()` combined with vsync provides equivalent timing
quality.

### Fullscreen

SDL3 supports fullscreen via `SDL_SetWindowFullscreen()`. Map the existing
`SetFullScreen()` calls to this. SDL3 handles mode switching, DPI changes,
and multi-monitor scenarios.

## Summary of New Files

| File | Purpose |
|------|---------|
| `src/AltirraSDL/source/display_sdl3.cpp` | `VDVideoDisplaySDL3` implementing `IVDVideoDisplay` |
| `src/AltirraSDL/source/display_sdl3_impl.h` | Header for the above (in source/ dir) |

## Interface Dependency

The SDL3 display implementation depends only on:

- `IVDVideoDisplay` (from `vd2/VDDisplay/display.h` -- clean)
- `VDPixmap` (from `vd2/Kasumi/pixmap.h` -- clean, just a struct)
- SDL3 headers

It does **not** depend on `displaydrv.h`, `direct3d.h`, or any Win32 headers.

**Important:** The VDDisplay *headers* (`display.h`, `compositor.h`,
`renderer.h`) are used for interface definitions. The VDDisplay *source
files* (D3D9 renderers, GDI renderers, display manager -- 15+ Win32-
dependent files) are **not compiled** for the SDL3 build. The SDL3 frontend
provides its own `IVDVideoDisplay` implementation instead.
