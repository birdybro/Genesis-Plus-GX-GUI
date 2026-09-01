# Local Bezel and Overlay Artwork

Genesis Plus GX GUI can compose a user-provided image around or over the emulated
frame. This is a presentation-only feature: it does not modify Genesis Plus GX pixels,
timing, saved state, input coordinates, or core accuracy. The application never
downloads artwork and packages never contain game-specific artwork.

## Modes

Open **Video → Artwork** or **Video → Video Settings… → Local bezel and overlay
artwork**. The available modes are:

- **Off** draws only the game and ordinary black letterboxing.
- **Bezel (behind game)** draws the artwork across the display first and the game on
  top. PNG, JPEG, and BMP are accepted.
- **Overlay (in front of game)** draws the game first and alpha-composites artwork on
  top. A PNG with at least one transparent pixel is required so a fully opaque image
  cannot silently hide the game.

Opacity is 1–100%. Artwork is scaled to the complete emulator canvas with smooth
filtering; the independent nearest/bilinear setting still controls the game texture.
The same ordering, alpha behavior, and upright coordinate convention are used by the
OpenGL and Qt software renderers.

## Explicit game aperture

Artwork never guesses a viewport from transparent pixels. Enable **Constrain game
image to an explicit viewport** only when an image contains a designed aperture, then
set left, top, right, and bottom insets as percentages of the canvas. Each inset is
limited to 45%, and opposing insets may total at most 90%. Aspect-ratio and integer-
scaling policy operate inside the remaining rectangle.

Leaving this option disabled preserves normal game geometry. Enabling it changes only
the presentation rectangle; light-gun, mouse, controller, and core input snapshots are
not remapped or mutated.

## Files, limits, and persistence

Select an absolute local `.png`, `.jpg`, `.jpeg`, or `.bmp` path. Files are checked
before decoding and are limited to 32 MiB, 4096×4096, and 16,777,216 decoded pixels.
One converted RGBA image is cached when settings are applied; frames do not reopen the
file or allocate another artwork-sized image. A missing, malformed, unsupported,
oversized, or opaque foreground image is rejected while the previous working
configuration remains active.

The global selection is stored in versioned `config/video-settings.json`; schema 0–3
files migrate with artwork safely disabled. A sparse per-game Video override may carry
a different mode, path, opacity, and aperture. The image itself is never copied into
application data or a package. Moving or deleting it causes a descriptive error and
normal unadorned video remains available.

Diagnostics report only mode, availability, format, dimensions, and byte count. They
do not include the potentially private artwork path.

## Testing

`unit.artwork` covers validation, overflow-safe aperture math, PNG/JPEG alpha rules,
bounded dimensions, malformed input, and the cached pixel format. Offscreen GUI tests
verify software composition, settings transactions, quick actions, failures, and
per-game/global round trips. `gui.libretro_shader_render` uses asymmetric artwork on a
real OpenGL 3.3 surface and executes 108 aspect/scale/filter/shader/artwork combinations,
rejecting black, hidden, or vertically inverted output. The optional external-ROM
runner repeats those 108 presentation cases against a user-owned game and writes its
comparison images only to the caller-selected directory.
