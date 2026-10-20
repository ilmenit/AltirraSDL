# SDL_GPU HDR output

HDR uses SDL's extended linear sRGB swapchain (RGBA16 float). The SDL_GPU
backend requests it when the window's current display has OS HDR enabled,
then verifies both composition support and the actual float swapchain
format. Unsupported configurations follow the existing SDR creation path.
OpenGL and the compatible SDL_Renderer backend remain SDR.

Enable hardware accelerated effects in Configure System, then use
**View → Adjust Screen Effects → HDR**. The controls use the existing
Windows `ATArtifactingParams` fields and View profile settings, including
profile inheritance and the normal save/load lifecycle. No independent HDR
preference or duplicated persistence key is introduced.

The emulator brightness uses scRGB's absolute 80-nit reference. SDR/UI
brightness is independent; SDL's system SDR-white scale is accounted for
once. “Use System HDR Brightness” uses the system SDR-white level, matching
the native option's `mbUseSystemSDRAsHDR` meaning. Masks retain their physical
pixel origin. Alternate device video uses the SDR/UI white level. Encoded
texel interpolation preserves SDR filter behavior even on a linear swapchain;
floating-point intermediates retain HDR values through
PAL blending, bicubic filtering and mask application. Standard screenshots
remain SDR XRGB images through SDL's color conversion and tone mapping.

A linear renderer reevaluates HDR availability and system white during
rendering, so moving to an SDR display or disabling OS HDR restores SDR
brightness and core output. If the renderer was created as SDR, enabling OS
HDR or moving to an HDR display requires a restart: replacing only the
swapchain would invalidate SDL's cached backbuffer format and pipelines.
The HDR page explains this requirement and shows the current display/mode.
This implements HDR luminance output; it does not add a wide-gamut monitor
calibration or an HDR screenshot file format.

## Validation

The display regression target checks SDR preservation, calibrated HDR/UI
levels, system-brightness precedence, monitor/effects/option fallback, and
nonfinite input handling. Rebuild its target and run `ctest -R upstream-display`
in the build tree. The bundled shader headers are regenerated with
`source/display/shaders/build-shaders.sh` under `src/AltirraSDL`; this accepts
SDL_shadercross or upstream DXC plus SPIRV-Cross.

The `altirra-display-sampling-test` target executes the production fragment
shader on offscreen GPU textures, without an HDR monitor. It checks SDR,
HDR-disabled linear output, encoded PAL/bicubic intermediates, HDR
bilinear/sharp filtering, nearest filtering and masks. CTest reports a skip
when no GPU is available. On a host with Vulkan, Metal or D3D12:

```sh
ctest --test-dir build/linux-release -R upstream-display-sampling --output-on-failure
```

Physical presentation needs an HDR monitor and OS/compositor support. On
each supported platform/backend combination:

1. Launch on the HDR display with OS HDR enabled. Confirm the dialog reports
   **HDR (extended linear)** and the log confirms a linear HDR swapchain.
2. Toggle Enable HDR and compare a bright Atari screen against the UI.
   Adjust HDR brightness: only the emulated display's brightness should
   change. Adjust SDR brightness: UI brightness should change independently.
   Verify both system-brightness checkboxes and profile/restart restoration.
3. Exercise PAL blending, each filter, bloom, all mask types, scanlines,
   distortion, fullscreen and fractional desktop scaling. Masks must remain
   anchored to physical pixels; dark backgrounds and colors must not acquire
   a washed-out signed-RGB offset.
4. Move the window between HDR and SDR displays, disable/enable OS HDR,
   suspend/resume, and disconnect/reconnect the HDR display. Check capability
   status and brightness, with no stale HDR boost on SDR. A session initially
   launched as SDR must explain that HDR needs a restart.
5. Capture screenshots in both modes and inspect the resulting SDR PNGs.
   Confirm valid dimensions/colors rather than raw half-float data.
6. Start with an SDR display and an unsupported renderer/driver. Confirm the
   normal SDR path still works and HDR controls explain the limitation.

The implementation host has an SDR Linux display. Shader generation and
calibration regressions can be checked there, but actual HDR presentation,
Metal/D3D runtime behavior, monitor switching and suspend/resume on HDR
hardware require the manual validation above.
