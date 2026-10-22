# Custom display effects in AltirraSDL

Configure System → Display Effects offers Effect file, Browse, Reload and Clear.
An effect is applied to the emulator image before the existing PAL/filter/bloom/
mask/HDR pipeline. Debugger panes and UI controls retain their existing rendering.
The selected path uses the existing per-profile `Display: Custom effect path`
setting. Reload reads every file again; Clear restores the normal pipeline.
A load/compile error is shown in this page and retains the previous working effect.

Native Windows Altirra `.cgp` presets contain Cg/HLSL shaders targeting Direct3D.
They cannot run directly on Vulkan, Metal, OpenGL or OpenGL ES. AltirraSDL reports
an explicit conversion error for these files. It does not silently ignore native
preset features. The `dist/extras/customeffects/portable/invert.altfx` package is a
complete port of the Windows invert example, with source and compiled shaders.

## Portable package contract, version 1

An `.altfx` file is UTF-8 JSON. Paths are relative to its directory. Example:

```json
{
  "version": 1,
  "passes": [{
    "glsl": "invert.frag.glsl",
    "spirv": "invert.frag.spv",
    "dxil": "invert.frag.dxil",
    "msl": "invert.frag.msl",
    "filter": "nearest",
    "scale": "source",
    "scale_x": 1,
    "scale_y": 1
  }]
}
```

There are 1–8 sequential passes. Each samples the preceding pass's output (the
first samples the emulator image). `filter` is `linear` (default) or `nearest`.
`scale` is `source` (previous pass dimensions; default) or `viewport` (requested
physical output dimensions). Scale factors default to 1 and must be 0.0625–16.
Dimensions round up, are limited to 8192 per axis and 16 million pixels per pass.
Files must be nonempty, at most 1 MiB each and 8 MiB in total. Unknown properties,
unsupported values, paths escaping the package and missing assets are errors.

Provide GLSL for OpenGL/GLES, SPIR-V for Vulkan, DXIL for Direct3D 12 and MSL for
Metal. A package may target fewer backends; selecting it on another backend is
an explicit error. The SDL_Renderer fallback has no programmable shader API and
reports that SDL_GPU or OpenGL must be selected, followed by a restart.

GLSL files contain the fragment shader **body**, without `#version`. AltirraSDL
prepends desktop GLSL 330 or GLES 300 plus precision declarations. Input UV is
`in vec2 vUV` (0,0 is the top-left image pixel). Declare `uniform sampler2D
sourceTexture`, `uniform vec4 sourceSize, outputSize, frame`, and one fragment
color output. These uniforms may be optimized out. RGB uses the native encoded
color range described below.

SDL_GPU shaders have one sampler and one 48-byte uniform buffer. HLSL bindings
are `t0/s0, space2` and `b0, space3`; fragment inputs are `float4 color:TEXCOORD0`
and `float2 uv:TEXCOORD1`. SPIR-V descriptor sets are 2 for texture/sampler and 3
for uniforms. MSL texture/sampler/buffer indices are all 0. `entrypoint` defaults
to `main`; `msl_entrypoint` defaults to `main0`.

The uniform buffer consists of three `float4` values in order:

| Uniform | Values |
| --- | --- |
| `sourceSize` | input width, input height, 1/width, 1/height |
| `outputSize` | pass width, pass height, 1/width, 1/height |
| `frame` | display draw counter since load, monotonic wall seconds, input sampler decodes sRGB (0 or 1), packed extended RGB (0 or 1) |

Shaders preserve the native encoded RGB range. When `frame.w` is 1, each pass
receives packed extended RGB: unpack with `c * (255.0 / 127.0) - 64.0 / 127.0`,
apply the desired color operation, then repack with `(c * 127.0 + 64.0) / 255.0`.
This retains negative and above-white gamut for the existing built-in effects
instead of clipping it. The flag applies to all passes.

On a linear HDR SDL_GPU renderer, the first
input's sampler automatically decodes sRGB; `frame.z` tells the shader to encode
that sampled RGB before applying an encoded-color operation. Later passes use
float targets without that automatic decode. The sample HLSL shows this handling.
Existing HDR output conversion is applied after custom passes.

## Porting a Windows preset

Translate each native shader pass to this fragment contract, including its
coordinate and color conventions. Map input/viewport scaling and filtering to
pass properties. Re-express uniforms with the documented buffer. Package every
GPU representation you intend to support; use SDL_shadercross or DXC plus
SPIRV-Cross **during effect authoring**, with no runtime compiler dependency:

```sh
dxc -T ps_6_0 -E main -spirv invert.frag.hlsl -Fo invert.frag.spv
dxc -T ps_6_0 -E main invert.frag.hlsl -Fo invert.frag.dxil
spirv-cross invert.frag.spv --msl --msl-version 20100 --output invert.frag.msl
```

The portable GLSL source is authored against the GLSL contract above. Native
feedback/history textures, LUTs, arbitrary additional textures and native Cg
parameter annotations have no version-1 mapping: such effects require an explicit
shader/algorithm port, rather than dropping preset entries. Test all selected
backend representations; a successful compilation alone does not prove that
sampling, color transforms or multi-pass orientation match.
