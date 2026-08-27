# Libretro shaders

Genesis Plus GX GUI supports modern Libretro Slang shader presets through the pinned
librashader OpenGL runtime. The application includes one original lightweight CRT
preset and can load user-provided `.slangp` presets. It does not bundle an external
shader pack or download shader files.

## Using a shader

Choose **Video → Shaders → Built-in CRT** for the bundled effect. Its scanline,
aperture-grille, curvature, vignette, and brightness controls are available under
**Video → Shaders → Shader Parameters…** and on the Video page of Settings.

To use an installed Libretro shader pack:

1. Choose **Video → Shaders → Load Libretro Preset…**.
2. Select the pack's `.slangp` file, not an individual `.slang` pass.
3. Adjust any parameters exposed by the preset.

The chosen absolute preset path and parameter values persist globally and may also be
used as a sparse per-game Video override. Preset-relative shader passes, lookup
textures, and includes remain relative to the preset or pack layout, so keep the pack's
directory structure intact when moving it.

## Compatibility

The frontend implements the current Libretro Slang preset pipeline through
librashader 0.12.0. This includes multi-pass presets, lookup textures, frame history,
feedback, and runtime `#pragma parameter` controls supported by that runtime. Shader
frame count and the emulated system's nominal PAL or NTSC rate are supplied each frame.

Legacy `.glslp`/`.glsl` and Cg `.cgp`/`.cg` presets are not accepted. Convert or choose
a Slang version of those presets. A shader written for a non-Libretro engine is not
made compatible merely by changing its filename extension.

Shaders require the accelerated desktop OpenGL renderer and OpenGL 3.3 or newer. If
OpenGL is unavailable, the application keeps rendering the normal unshaded image with
its software path and reports why the selected shader could not run. A missing pass,
invalid preset, compile error, unsupported runtime, or rendering error likewise fails
back to the unshaded frame instead of stopping emulation.

## Rendering behavior

The core's RGB565 frame is uploaded once to a bounded OpenGL texture. librashader owns
the intermediate pass/history resources and renders the final chain into a bounded
viewport-sized texture supplied by the frontend. The existing presentation pass then
draws that texture into the calculated aspect/integer-scale rectangle. Resizing may
reallocate the one output texture but does not enqueue frames or allocate core-sized
objects every frame.

The presentation filter and the preset's own pass filters are separate. The
nearest/bilinear Video option controls ordinary unshaded presentation; a loaded preset
retains the filtering, wrapping, mipmap, and scaling rules declared by its passes.

## Building and licensing

The normal build fetches the checksum-pinned `librashader-v0.12.0` source archive and
builds only its OpenGL C API runtime. Rust 1.88 or newer and Cargo are required. To
build a frontend without Libretro shader support, configure with:

```bash
cmake --preset debug -DGENPLUSGX_ENABLE_LIBRETRO_SHADERS=OFF
```

Packaged builds include the runtime, the built-in preset, and librashader's MPL-2.0
license. librashader's C headers are MIT licensed. User-installed shader presets retain
their authors' licenses; review those terms before redistributing a shader pack.
