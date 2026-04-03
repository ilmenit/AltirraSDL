# Shader Effects and Post-Processing

This document covers the implementation of GPU-accelerated display effects
for the SDL3 build, replacing the D3D9 HLSL shader pipeline used on Windows.

Related documents:
- [DISPLAY.md](DISPLAY.md) — base display architecture (texture upload,
  scaling, aspect ratio)
- [UI.md](UI.md) — Dear ImGui integration

## Current State

### Windows (D3D9 Shaders)

The Windows build uses Direct3D 9 shaders for all post-processing. The
shader pipeline lives in `src/VDDisplay/`:

| File | Lines | Purpose |
|------|-------|---------|
| `shaders/screenfx.fxh` | 783 | Screen effects: scanlines, distortion, sharp bilinear, gamma, color correction, dot masks |
| `shaders/bicubic.fxh` | 41 | Bicubic filter kernel |
| `shaders/displayd3d9.fx` | 266 | Technique definitions (30 techniques total; 32 screenfx entry points in `screenfx.fxh`) |
| `source/screenfx.cpp` | 725 | CPU-side lookup texture generation |
| `source/bloom.cpp` | 197 | Bloom V2 parameter computation |
| `source/bicubic.cpp` | 135 | Bicubic filter texture generation |

The effect parameters are carried by three structures:

- `ATArtifactingParams` (in `gtia.h`) — scanline intensity, distortion
  angles, bloom settings, HDR parameters
- `VDVideoDisplayScreenFXInfo` (in `display.h`) — gamma, color correction
  matrix, bloom thresholds, screen mask configuration
- `VDDScreenMaskParams` (in `displaytypes.h`) — mask type (aperture grille,
  dot triad, slot mask), dot pitch, openness

Settings are persisted via `settings.cpp` under `ScreenFX:` registry keys.

The Windows "Adjust Screen Effects" dialog (`uiscreeneffects.cpp`) has four
pages: Main (scanlines, distortion), Bloom, HDR, and Mask.

### SDL3 (Current)

No shader effects. The display uses `SDL_Renderer` with `SDL_Texture` and
only supports `SDL_SCALEMODE_NEAREST` (point) and `SDL_SCALEMODE_LINEAR`
(bilinear). Sharp bilinear and bicubic fall back to bilinear. The "Adjust
Screen Effects" menu item is a disabled placeholder.

## Options Analysis

### Option 1: Stay with SDL_Renderer

`SDL_Renderer` is a high-level 2D API with no custom shader support. It
cannot implement any of the Windows effects. Rejected.

### Option 2: SDL3 GPU API

SDL3 includes a modern GPU API (`SDL_gpu.h`) targeting Vulkan/Metal/D3D12.
It supports custom shaders, render targets, and compute passes.

Advantages:
- Cross-backend (Vulkan on Linux, Metal on macOS, D3D12 on Windows)
- Modern API design

Disadvantages:
- Requires pre-compiled SPIR-V bytecode — no runtime GLSL compilation
- Needs `SDL_shader_tools` or external toolchain to compile shaders at
  build time
- ImGui has no `imgui_impl_sdl_gpu` backend — would need a custom one
  or use a community port
- Relatively new API, less battle-tested for this use case
- Cannot be used alongside `SDL_Renderer` on the same window

### Option 3: OpenGL 3.3 Core (Recommended)

Use SDL3's OpenGL context support (`SDL_WINDOW_OPENGL` +
`SDL_GL_CreateContext`) with GLSL shaders.

Advantages:
- Mature, well-understood API
- GLSL is nearly identical to HLSL — straightforward port of existing
  shaders
- ImGui has a production-quality OpenGL3 backend
  (`imgui_impl_opengl3.cpp`, already vendored)
- Runtime GLSL compilation — no build-time shader toolchain needed
- Enables optional librashader integration for RetroArch shader presets
- Universal Linux/macOS support (GL 3.3 available on Mesa Intel HD 2000+
  from 2011, all AMD GCN, all Nvidia)

Disadvantages:
- OpenGL is deprecated on macOS (but functional through 4.1)
- Cannot coexist with `SDL_Renderer` on the same window — requires a
  separate fallback path
- Manual GL state management

### Fallback: SDL_Renderer

Keep the existing `SDL_Renderer` code path as a fallback for systems where
OpenGL is unavailable or fails to initialize. This path provides point and
bilinear filtering only, with no shader effects. The backend is selected at
startup.

## Recommended Architecture

### Display Backend Abstraction

```
┌──────────────────────────────────────────────────────────────┐
│                     Display Pipeline                          │
│                                                              │
│  ┌────────────────────────────────────────────────────────┐  │
│  │  Frame Source: GTIA VDPixmap → GL/SDL texture upload   │  │
│  └──────────────────────┬─────────────────────────────────┘  │
│                         │                                    │
│  Post-Processing (mutually exclusive modes):                 │
│  ┌──────────────────┐ ┌──────────────────┐ ┌─────────────┐  │
│  │  Built-in FX     │ │  librashader     │ │  Passthrough │  │
│  │  (GLSL shaders)  │ │  (optional)      │ │             │  │
│  │                  │ │                  │ │  Point or   │  │
│  │  Scanlines       │ │  Any RetroArch   │ │  bilinear   │  │
│  │  Bloom V2        │ │  .slangp/.glslp  │ │  scaling    │  │
│  │  Distortion      │ │  preset          │ │  only       │  │
│  │  Screen masks    │ │                  │ │             │  │
│  │  Sharp bilinear  │ │                  │ │             │  │
│  │  Bicubic         │ │                  │ │             │  │
│  │  Gamma/color     │ │                  │ │             │  │
│  └──────────────────┘ └──────────────────┘ └─────────────┘  │
│                         │                                    │
│  ┌────────────────────────────────────────────────────────┐  │
│  │  Presentation: backbuffer → ImGui overlay → swap       │  │
│  └────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────┘
```

Built-in effects and librashader presets are **mutually exclusive**.
RetroArch presets include their own scanlines, bloom, masks, etc. — layering
our effects on top would double-apply them. When a librashader preset is
active, built-in effects are bypassed.

### Backend Selection at Startup

```cpp
// Try OpenGL first
SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

SDL_Window* window = SDL_CreateWindow("AltirraSDL", w, h,
    SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

if (window) {
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (gl) {
        // OpenGL path — full effects
        ImGui_ImplSDL3_InitForOpenGL(window, gl);
        ImGui_ImplOpenGL3_Init("#version 330 core");
        g_pBackend = new DisplayBackendGL(...);
    }
}

if (!g_pBackend) {
    // Fallback — SDL_Renderer, no effects
    window = SDL_CreateWindow("AltirraSDL", w, h, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
    g_pBackend = new DisplayBackendSDLRenderer(renderer);
}
```

On macOS, request GL 3.2 core with `SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG`
(Apple supports 3.2–4.1 in core profile only).

### IDisplayBackend Interface

```cpp
class IDisplayBackend {
public:
    virtual ~IDisplayBackend() = default;

    // Upload emulator frame pixels to GPU texture
    virtual void UploadFrame(const VDPixmap& px) = 0;

    // Render the frame (with any active effects) to the backbuffer
    virtual void RenderFrame(const SDL_FRect& destRect) = 0;

    // Built-in Altirra effects
    virtual bool SupportsBuiltinEffects() const = 0;
    virtual void SetScreenFX(const VDVideoDisplayScreenFXInfo& fx) = 0;
    virtual void SetFilterMode(ATDisplayFilterMode mode) = 0;
    virtual void SetFilterSharpness(float sharpness) = 0;

    // External shader presets (librashader)
    virtual bool SupportsExternalShaders() const = 0;
    virtual bool LoadShaderPreset(const char* path) = 0;
    virtual void ClearShaderPreset() = 0;

    // ImGui integration — returns the output texture for ImGui to draw
    virtual ImTextureID GetOutputTexture() const = 0;
    virtual int GetTextureWidth() const = 0;
    virtual int GetTextureHeight() const = 0;

    // Cleanup
    virtual void Shutdown() = 0;
};
```

`DisplayBackendSDLRenderer` implements this with `SupportsBuiltinEffects()`
and `SupportsExternalShaders()` returning false. All effect-related methods
are no-ops.

## OpenGL Display Backend

### Window and Context

Replace the current `SDL_CreateRenderer()` path with:

```cpp
SDL_Window* window = SDL_CreateWindow("AltirraSDL", w, h,
    SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
SDL_GLContext gl_context = SDL_GL_CreateContext(window);
SDL_GL_MakeCurrent(window, gl_context);
SDL_GL_SetSwapInterval(1);  // vsync
```

### ImGui Backend Change

The ImGui library build in `CMakeLists.txt` currently compiles:

```cmake
${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
```

For the OpenGL path, also compile:

```cmake
${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
```

Both renderer backends are compiled. At runtime, only one is initialized
based on which display backend was selected. The platform backend
(`imgui_impl_sdl3.cpp`) is shared — it has both `InitForOpenGL()` and
`InitForSDLRenderer()` entry points.

### Texture Upload

Replace `SDL_UpdateTexture` with direct GL texture upload:

```cpp
// One-time: create texture
GLuint emuTexture;
glGenTextures(1, &emuTexture);
glBindTexture(GL_TEXTURE_2D, emuTexture);
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
             GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, nullptr);

// Per-frame: upload pixel data
glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pixelData);
```

`GL_BGRA` + `GL_UNSIGNED_INT_8_8_8_8_REV` matches the XRGB8888 layout that
GTIA already produces. On Mesa this is a fast path with no CPU swizzle.

Pal8 frames are converted to XRGB8888 on the CPU before upload, same as the
current `display_sdl3.cpp` code.

### Fullscreen Quad Rendering

All post-processing uses the same fullscreen-triangle technique (3 vertices,
no VBO):

```glsl
// Vertex shader (shared by all passes)
#version 330 core
out vec2 vUV;
void main() {
    vUV = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(vUV * 2.0 - 1.0, 0.0, 1.0);
}
```

An empty VAO is bound for GL 3.3 core profile compliance.

### Render-to-Texture (FBO Pipeline)

Multi-pass effects (bloom, bicubic) use framebuffer objects:

```cpp
GLuint fbo, fboTex;
glGenFramebuffers(1, &fbo);
glGenTextures(1, &fboTex);

glBindTexture(GL_TEXTURE_2D, fboTex);
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
             GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

glBindFramebuffer(GL_FRAMEBUFFER, fbo);
glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                       GL_TEXTURE_2D, fboTex, 0);
```

FBO textures are resized when the window size changes.

### Per-Frame Rendering Order

```
1. glTexSubImage2D          — upload GTIA frame to emuTexture
2. Post-processing passes   — (see Built-in Effects Pipeline below)
3. glBindFramebuffer(0)     — target backbuffer
4. Draw output quad          — blit processed result to display rect
5. ImGui_ImplOpenGL3_NewFrame / ImGui::NewFrame
6. Build all ImGui UI
7. ImGui::Render / ImGui_ImplOpenGL3_RenderDrawData
8. SDL_GL_SwapWindow
```

Alternatively, step 4 can use `ImGui::GetBackgroundDrawList()->AddImage()`
to let ImGui draw the emulator texture as part of its own render pass.

## Built-in Effects Pipeline

This section describes the port of Altirra's D3D9 HLSL shaders to
OpenGL 3.3 GLSL. The HLSL→GLSL translation is mechanical:

| HLSL | GLSL |
|------|------|
| `half4` / `float4` | `vec4` |
| `tex2D(sampler, uv)` | `texture(sampler, uv)` |
| `lerp(a, b, t)` | `mix(a, b, t)` |
| `saturate(x)` | `clamp(x, 0.0, 1.0)` |
| `cbuffer` | `uniform` block or individual uniforms |
| `SV_Position` | `gl_Position` |
| `: register(t0)` | `glUniform1i(loc, 0)` to bind sampler to texture unit |

### Screen Effects Pass (1 pass)

The main screen effects fragment shader combines multiple features via
boolean parameters. The Windows HLSL build uses `uniform bool`
arguments to a single `FP_ScreenFX()` function, with 32 instantiated
entry points that pass different constant combinations — the HLSL
compiler optimizes away dead branches at compile time. The GLSL port
uses `#define` toggles instead (GLSL lacks the same compile-time
uniform-bool optimization guarantee):

```glsl
#version 330 core

// Compile-time feature toggles
// #define DO_SHARP
// #define DO_SCANLINES
// #define DO_GAMMA
// #define DO_COLOR_CORRECT
// #define DO_SRGB
// #define DO_DOT_MASK

uniform sampler2D uSourceTex;    // emulator frame
uniform sampler2D uGammaTex;     // 256×1 gamma ramp LUT
uniform sampler2D uScanlineTex;  // 1×H scanline mask
uniform sampler2D uMaskTex;      // screen mask (aperture grille / dot triad / slot mask)

uniform vec4 uSharpnessInfo;
uniform vec4 uImageUVSize;
uniform mat3 uColorCorrectMatrix;
```

Features in the fragment shader:

1. **Sharp bilinear**: Snaps to nearest source pixel center, then applies
   a sharpness-controlled blend with bilinear. Controlled by
   `uSharpnessInfo` uniform.

2. **Color correction**: Multiplies linear RGB by a 3×3 color matrix.
   Requires linearization first (sRGB decode or gamma 2.2).

3. **Dot mask**: Multiplies by screen mask texture (aperture grille, dot
   triad, or slot mask pattern).

4. **Gamma correction**: Per-component lookup via 256-entry gamma ramp
   texture.

5. **Scanlines**: Multiplies by scanline mask texture (raised cosine
   pattern).

All features are orthogonal and any combination can be active.

### Bloom V2 (5+ passes)

The bloom effect uses a pyramid downsample/upsample architecture. This is
the most complex effect and requires multiple FBOs at decreasing
resolutions.

**Pass 1 — Gamma conversion:**
Convert sRGB input to linear RGB for bloom processing.

**Pass 2 — Downsample pyramid (repeated for each level, up to 5 levels):**
13-tap bilinear downsample filter. Each level is half the resolution of the
previous. Weights: corners 7/124, edges 16/124, center cross 32/124.

**Pass 3 — Upsample pyramid (repeated for each level):**
4-tap asymmetric upsample kernel (weights 36, 60, 60, 100 from 256).
Blends with the next-coarser level using computed blend factors.

**Pass 4 — Final composition:**
Upsample the coarsest bloom level, blend with 9-tap sampled base image,
apply soft-clip shoulder curve (cubic polynomial), convert back to sRGB.

The bloom parameter computation runs on the CPU (`bloom.cpp`). It produces:
- 6 blend factor pairs (one per upsample pass + final)
- Cubic polynomial coefficients for the shoulder curve
- Base filter weights (corner/side/center taps)

These are uploaded as uniforms each frame (or when parameters change).

### Bicubic Filtering (2 passes)

Separable bicubic: horizontal pass → temp FBO → vertical pass.

Each pass samples 4 taps using a pre-computed filter weight lookup texture
(135 lines in `bicubic.cpp`). The lookup encodes:
- Red: sum of outer tap weights (×4 scale)
- Green: center tap bias (×16 scale)
- Blue: lerp factor between outer taps

The shader reads the lookup texture at the fractional source coordinate to
get tap weights, samples 3 texture positions (using bilinear to get an extra
tap for free), and combines them.

### PAL Artifacting (1 pass)

Samples at two UV offsets (main and chroma-shifted), extracts chroma
difference, blends 50%. Simple single-pass shader.

### Lookup Texture Generation

Seven textures are generated on the CPU and uploaded to GL textures. The
generation code is in `screenfx.cpp` and `bicubic.cpp` — it is
platform-agnostic C++ that only produces `uint32` pixel arrays. It can be
reused directly; only the final upload step changes from D3D9 to OpenGL.

| Texture | Dimensions | Format | Source Function |
|---------|------------|--------|-----------------|
| Gamma ramp | 256×1 | RGBA8 | `VDDisplayCreateGammaRamp()` |
| Scanline mask | 1×dstH | RGBA8 | `VDDisplayCreateScanlineMaskTexture()` |
| Aperture grille | W×1 | RGBA8 | `VDDisplayCreateApertureGrilleTexture()` |
| Slot mask | W×H | RGBA8 | `VDDisplayCreateSlotMaskTexture()` |
| Dot triad mask | W×H | RGBA8 | `VDDisplayCreateTriadDotMaskTexture()` |
| Bicubic filter H | srcW×1 | RGBA8 | `VDDisplayCreateBicubicTexture()` |
| Bicubic filter V | 1×srcH | RGBA8 | `VDDisplayCreateBicubicTexture()` |

Textures are regenerated when relevant parameters change (e.g., window
resize changes scanline mask height, mask type change regenerates mask
texture).

### Distortion (Vertex Shader)

Barrel/pincushion CRT distortion is computed in the vertex shader by
projecting through an ellipsoid model. The `VDDisplayDistortionMapping`
class in `screenfx.cpp` computes the model parameters on the CPU; the
vertex shader applies the transform per-vertex.

For the fullscreen-triangle approach, distortion must be applied in the
fragment shader instead (computing per-pixel UV distortion). This is more
expensive but avoids needing a tessellated mesh. Alternatively, use a
subdivided quad (e.g., 32×32 grid) with per-vertex distortion for better
performance.

## librashader Integration (Optional)

[librashader](https://github.com/SnowflakePowered/librashader) is a
standalone library that loads and runs RetroArch shader presets. It is
used by ares, and other emulators.

### Why Optional

librashader is licensed under MPL-2.0 (compatible with GPL v2+). It is
written in Rust and distributed as a shared library. The integration uses
runtime dynamic loading (`dlopen`) via the `librashader_ld.h` header — there
is **zero compile-time dependency**. If the shared library is not installed,
shader preset support is simply unavailable.

### Supported Formats

| Format | Files | Status |
|--------|-------|--------|
| **Slang** | `.slang` + `.slangp` presets | Current standard, actively maintained |
| **GLSL** | `.glsl` + `.glslp` presets | Legacy, still supported |
| **Cg** | `.cg` + `.cgp` presets | Deprecated by Nvidia, not supported |

The actively maintained shader collection is
[libretro/slang-shaders](https://github.com/libretro/slang-shaders).
All significant shaders from the older Cg/GLSL era have been ported to
Slang.

### Integration Model

librashader uses a **texture-in, texture-out** model:

```cpp
#define LIBRA_RUNTIME_OPENGL
#include "librashader_ld.h"

// Load library at runtime (once)
libra_instance_t libra = librashader_load_instance();
if (!libra.instance_loaded) {
    // librashader.so not found — feature disabled, all calls are no-ops
}

// Load a preset
libra_shader_preset_t preset = NULL;
libra.preset_create("/path/to/shader.slangp", &preset);

// Create filter chain (compiles all shader passes)
libra_gl_filter_chain_t chain = NULL;
libra.gl_filter_chain_create(&preset,
    (libra_gl_loader_t)SDL_GL_GetProcAddress,
    NULL,  // options
    &chain);

// Per frame: apply shader chain
libra_image_gl_t input  = { emuTexture,    GL_RGBA8, srcW, srcH };
libra_image_gl_t output = { outputTexture, GL_RGBA8, dstW, dstH };
libra.gl_filter_chain_frame(&chain, frameCount++,
    input, output, NULL, NULL, NULL);

// Cleanup
libra.gl_filter_chain_free(&chain);
```

librashader manages its own intermediate FBOs and shader compilation
internally. It modifies GL state during `gl_filter_chain_frame` — the
caller must restore any GL state it cares about afterward (or rely on
ImGui's state save/restore).

### GL Loader

librashader needs a GL function loader. SDL3 provides this:

```cpp
static const void* gl_loader(const char* name) {
    return (const void*)SDL_GL_GetProcAddress(name);
}
```

### Filter Chain Options

```cpp
filter_chain_gl_opt_t opts = {};
opts.version = LIBRASHADER_CURRENT_VERSION;
opts.glsl_version = 330;     // match our GL 3.3 context
opts.use_dsa = false;        // DSA requires GL 4.5
opts.force_no_mipmaps = false;
opts.disable_cache = false;
```

### Build-Time Requirements

Only two header files are needed (vendored in our repo):
- `include/librashader.h` — type definitions and function declarations
- `include/librashader_ld.h` — single-header dynamic loader

No link-time dependency. No Rust toolchain required.

### Runtime Requirements

The user installs `librashader.so` (Linux) or `librashader.dylib` (macOS)
from their package manager or from GitHub releases. If the library is not
present, `librashader_load_instance()` sets `instance_loaded = false` and
all function pointers become no-ops.

### Interaction with Built-in Effects

When a librashader preset is active, built-in effects (scanlines, bloom,
distortion, masks) are disabled. The preset fully controls the visual
pipeline. The user chooses between:

- **Built-in effects** — Altirra's own scanlines/bloom/distortion/masks
- **Shader preset** — any `.slangp` or `.glslp` RetroArch preset
- **None** — clean scaling with selected filter mode

Filter mode (point/bilinear) still applies as the base texture sampling
before any effect chain.

## New File Structure

```
src/AltirraSDL/source/
  display_backend.h              — IDisplayBackend interface definition
  display_gl.h                   — DisplayBackendGL class declaration
  display_gl.cpp                 — GL context management, texture upload,
                                   quad rendering, FBO management,
                                   present/swap
  display_gl_effects.h           — Built-in effect pipeline declaration
  display_gl_effects.cpp         — Multi-pass rendering: screenFX pass,
                                   bloom passes, bicubic passes.
                                   Lookup texture generation (calls
                                   existing screenfx.cpp/bicubic.cpp
                                   functions, uploads to GL textures).
  display_gl_shaders.inl         — Embedded GLSL source strings for all
                                   shaders (vertex, fragment variants)
  display_librashader.h          — librashader wrapper declaration
  display_librashader.cpp        — Runtime dlopen, preset load/unload,
                                   per-frame filter chain application
  display_sdl3.cpp               — Existing SDL_Renderer fallback (kept)
  display_sdl3_impl.h            — Existing display class (kept)
  ui_screeneffects.cpp           — "Adjust Screen Effects" ImGui dialog
                                   (4 pages: Main, Bloom, HDR, Mask)
  ui_shaderpresets.cpp           — Shader preset browser/selector UI

include/ (vendored, optional)
  librashader.h                  — librashader C API types
  librashader_ld.h               — librashader dynamic loader
```

### CMake Changes

```cmake
# Add OpenGL ImGui backend (alongside existing SDLRenderer3 backend)
${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp

# Find OpenGL
find_package(OpenGL REQUIRED)

# Link
target_link_libraries(AltirraSDL PRIVATE OpenGL::GL)
```

The VDDisplay source files `screenfx.cpp`, `bloom.cpp`, and `bicubic.cpp`
must be compiled for the SDL3 build. They contain only platform-agnostic C++
(no D3D9 or Win32 dependencies). Add them to the AltirraSDL (or a shared
library) CMake target. The SIMD code in `screenfx.cpp` (SSSE3 blend
functions) compiles on both GCC and Clang with `-mssse3`.

## Implementation Phases

### Phase 1: OpenGL Display Backend

Switch from `SDL_Renderer` to OpenGL as the primary rendering path.

1. Create `display_backend.h` with `IDisplayBackend` interface
2. Create `display_gl.cpp/.h` — GL context, texture upload, fullscreen
   quad, basic point/bilinear via `glTexParameteri`
3. Wrap existing `display_sdl3.cpp` behind `IDisplayBackend` as
   `DisplayBackendSDLRenderer`
4. Update `main_sdl3.cpp` — backend selection, GL context creation,
   `SDL_GL_SwapWindow` present path
5. Update `CMakeLists.txt` — add `imgui_impl_opengl3.cpp`, `OpenGL::GL`
6. Update `ui_main.cpp` — conditional ImGui init
   (`InitForOpenGL` vs `InitForSDLRenderer`)

**Gate:** Emulator displays frames via OpenGL. ImGui overlays work. Visual
output matches current SDL_Renderer path.

### Phase 2: Sharp Bilinear and Bicubic

Implement the two advanced filter modes that are currently stubbed.

1. Add fullscreen-triangle vertex shader and passthrough fragment shader
2. Implement sharp bilinear fragment shader (sharpness-controlled snap)
3. Port bicubic 2-pass pipeline — compile `bicubic.cpp`, generate lookup
   texture, implement horizontal + vertical fragment shaders with FBO
4. Wire `ATDisplayFilterMode` changes to shader/FBO switching

**Gate:** Sharp bilinear and bicubic produce visually correct output
matching Windows Altirra.

### Phase 3: Screen Effects (Scanlines, Distortion, Masks)

1. Compile `screenfx.cpp` for SDL3 build — lookup texture generation
2. Port `screenfx.fxh` to GLSL — implement the unified fragment shader
   with `#define` toggles for scanlines, gamma, color correction, masks
3. Generate and upload lookup textures (gamma ramp, scanline mask,
   aperture grille, slot mask, dot triad)
4. Implement distortion (fragment-shader UV warp or subdivided quad)
5. Implement "Adjust Screen Effects" ImGui dialog (`ui_screeneffects.cpp`)
   matching the Windows 4-page layout

**Gate:** All screen effects match Windows output. Settings persist via
existing `settings.cpp` `ScreenFX:` keys.

### Phase 4: Bloom V2

1. Compile `bloom.cpp` for SDL3 build — parameter computation
2. Create FBO pyramid (5 levels at decreasing resolutions)
3. Implement downsample shader (13-tap filter)
4. Implement upsample shader (4-tap with blend factors)
5. Implement final composition shader (9-tap base + bloom blend +
   shoulder soft-clip + sRGB conversion)
6. Wire bloom parameters from `ATArtifactingParams` to uniforms

**Gate:** Bloom V2 matches Windows output. Bloom page in Adjust Screen
Effects dialog controls all parameters.

### Phase 5: librashader Integration

1. Vendor `librashader.h` and `librashader_ld.h` headers
2. Implement `display_librashader.cpp` — runtime loading, preset
   management, per-frame filter chain
3. Implement preset browser UI (`ui_shaderpresets.cpp`) — file picker
   for `.slangp`/`.glslp`, parameter display
4. Add View menu entry for shader preset selection
5. Settings persistence for last-used preset path

**Gate:** Loading a RetroArch `.slangp` preset applies the shader chain.
Built-in effects are bypassed when a preset is active. Removing the preset
restores built-in effects.

### Phase 6: HDR (Deferred)

HDR support requires platform-specific display capabilities (HDR10,
scRGB). This is deferred until the base effects pipeline is complete and
tested. The `mbEnableHDR`, `mSDRIntensity`, `mHDRIntensity` fields in
`ATArtifactingParams` are preserved but not actively used.

## Settings Persistence

All screen effect settings are already handled by `settings.cpp` under the
`ScreenFX:` key prefix. The SDL3 build compiles `settings.cpp` and calls
`ATLoadSettings`/`ATSaveSettings`. No additional persistence code is needed
— the existing save/load paths will work once the GTIA artifacting params
and screen mask params are wired to the GL shader uniforms.

## UI: Adjust Screen Effects Dialog

The ImGui dialog replicates the Windows dialog's four-page layout:

**Page 1 — Main:**
- Scanline intensity slider (ticks 1–7, displayed as 12%–87%)
- Distortion X view angle slider (0–180°)
- Distortion Y ratio slider (0–100%)
- Warning text when hardware acceleration is not available
- Clickable link to enable acceleration when currently disabled

Controls are hidden (not just disabled) when hardware support is
unavailable — matching Windows behavior where the distortion sliders
and labels are shown/hidden via `ShowControl()`.

**Page 2 — Bloom:**
- Enable checkbox
- Radius slider (0.1×–10.0× logarithmic, ticks -75 to 75)
- Direct intensity slider (0–200%, ticks 0–200 with ×100 scale)
- Indirect intensity slider (0–200%, ticks 0–200 with ×100 scale)
- Scanline compensation checkbox

**Page 3 — HDR:** (disabled until Phase 6)
- Enable HDR checkbox
- Use system SDR brightness checkbox
- Use system HDR brightness checkbox
- SDR brightness slider (0–400, logarithmic scale, displayed in nits)
- HDR brightness slider (0–400, logarithmic scale, displayed in nits)
- Monitor info section:
  - Display name (from `VDDMonitorInfo`)
  - Current mode (HDR / WCG / SDR)
  - Maximum capability
  - Color gamut coverage (sRGB %, NTSC %, DCI-P3 %)
- Warning/help link for HDR configuration guidance

**Page 4 — Mask:**
- Mask type combo (None, Aperture grille (vertical), Dot mask, Slot mask)
- Dot pitch slider (logarithmic, ticks -60 to 0, displayed in color
  clocks with estimated mm for 12" monitor)
- Openness slider (25–100%)
- Intensity compensation checkbox

A "Reset to Defaults" button on each page restores default values from
`ATArtifactingParams::GetDefault()`.

## UI: Shader Preset Selector

The Windows View menu does not have a shader preset entry — this is a
new feature for the SDL3 build enabled by the librashader integration.
It is placed in the View menu after "Adjust Screen Effects...", matching
the logical grouping of display post-processing options:

```
&View
  ...
  Adjust Colors...                      {Video.AdjustColorsDialog}
  Adjust Screen Effects...              {Video.AdjustScreenEffectsDialog}
  Shader Preset...                      {Video.ShaderPresetDialog}     ← NEW
  ...
```

### Availability Detection

The "Shader Preset..." menu item is only shown when both conditions are
met:
1. The OpenGL display backend is active (not the SDL_Renderer fallback)
2. `librashader_load_instance()` returned `instance_loaded == true`

If either condition is false, the menu item is hidden (not grayed out).

### Preset Browser Dialog

The "Shader Preset..." menu item opens an ImGui dialog with:

- **Current preset** — displays the active preset path (or "(None)")
- **Browse...** button — opens a file dialog filtered for `.slangp` and
  `.glslp` files
- **Clear** button — removes the active preset, restoring built-in
  effects
- **Recent presets** list — last 5 used preset paths, click to load
- **Runtime parameters** — when a preset is loaded, its tunable
  parameters (exposed via `libra.preset_get_runtime_params()`) are
  displayed as sliders/checkboxes for real-time adjustment

### Effect Mode Switching

When a preset is loaded:
- Built-in screen effects (scanlines, bloom, distortion, masks) are
  bypassed — the preset fully owns the visual pipeline
- The "Adjust Screen Effects..." dialog shows an info banner:
  "Screen effects are bypassed while a shader preset is active"
- Filter mode (point/bilinear) still controls base texture sampling
  before the shader chain

When a preset is cleared:
- Built-in effects are restored from the current `ATArtifactingParams`
- The "Adjust Screen Effects..." dialog becomes fully interactive again

### Settings Persistence

The last-used preset path is saved to `settings.ini` under
`ShaderPreset:Path`. On startup, if the path is non-empty and the file
exists, the preset is automatically loaded. If the file is missing
(e.g., deleted or on a different machine), the setting is silently
cleared.
