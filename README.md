# Genesis Plus GX GUI

Genesis Plus GX GUI is a native Qt 6 desktop frontend for the Genesis Plus GX
emulation core. It turns this fork into a standalone application for Windows, Linux,
and macOS while keeping the emulator core isolated and authoritative. It is not an
official upstream Genesis Plus GX project and is not endorsed by Sega.

The application is currently version 0.1.1. It does not include games, Sega firmware,
or box art.

## Screenshots

Release screenshots have not been committed yet. Maintainers can add original captures
under `docs/screenshots/` and link them here; never use commercial game images or scraped
box art. The main window is a conventional menu-driven desktop interface with an
OpenGL-backed emulator display, game/status fields, native dialogs, and keyboard access
to all essential actions.

## Supported systems

- Sega SG-1000 and Mark III
- Sega Master System
- Sega Game Gear
- Sega Genesis / Mega Drive
- Sega CD / Mega CD

The desktop file picker recognizes `.68k`, `.bin`, `.bms`, `.cue`, `.gen`, `.gg`,
`.iso`, `.md`, `.mdx`, `.m3u`, `.m3u8`, `.sg`, `.sgd`, `.smd`, `.sms`, and `.zip`.
CMake builds enable `.chd` by default through the bundled libchdr decoder. ZIP browsing
supports stored or deflated cartridge images; M3U playlists provide ordered local Sega
CD disc sets. A Sega CD game needs a legally obtained, region-appropriate BIOS supplied
by the user.

Cartridge images can be soft-patched at launch with IPS, BPS, or UPS files. Patching is
non-destructive: the core receives a validated cache copy and the source game is never
rewritten.

## Features

- Dedicated emulation thread with bounded video, audio, input, and command exchanges
- Dynamic viewport rendering, aspect/integer scaling, overscan, interlace modes,
  nearest/bilinear filtering, fullscreen, high-DPI support, and native PNG screenshots
- Runtime-selectable off/on/adaptive vertical synchronization and double/triple swap
  buffering with newest-frame-only queuing, cadence/drop instrumentation, and safe
  software fallback
- Built-in adjustable CRT presentation plus modern Libretro `.slangp` shader presets,
  including multi-pass pipelines, lookup textures, history, and runtime parameters
- Local-only bezel and alpha-overlay composition with opacity, explicit game-aperture
  insets, bounded cached decoding, global/per-game settings, and no artwork scraping
- SDL3 stereo audio with live output-device/latency changes, plus hot-pluggable
  controller profiles, deadzones, assignments, capture-based remapping, and specialized
  Genesis Plus GX device choices
- Keyboard play controls plus capture-configurable, conflict-checked emulator hotkeys
- Exact configurable 50–200% normal pacing, 25–75% slow motion, and 200–1600%
  fast forward with focus-safe hold/toggle controls and an always-visible speed status
- Bounded rewind with configurable capture interval/memory budget, focus-safe hold and
  menu-toggle controls, muted backward playback, and deterministic forward resume
- Automatic per-game SRAM, Sega CD BRAM, and RAM-cartridge persistence using atomic files
- Ten metadata-wrapped save-state slots with names, native-frame thumbnails, a visual
  manager, validated import/export, and strict game/hardware identity checks
- Bounded lossless A/V recording with native sequential PNG frame dumps, stereo PCM
  WAV audio, explicit drop metrics, and atomic session finalization
- Opt-in clean-shutdown session checkpoints that safely reopen and resume the last
  running game, while explicit command-line games always take precedence
- BIOS validation, Sega CD disc change/eject, CDDA, CUE/BIN, ISO, and CHD workflows
- Bounded ZIP cartridge browser and traversal-safe M3U/M3U8 multi-disc playlists
- Non-destructive IPS/BPS/UPS soft patching with automatic sidecars, explicit selection,
  checksum validation, patched-content save/state identity, and session resume
- Recent games, searchable asynchronous SQLite library, favorites, play history, and
  user-provided local artwork
- Header metadata and SHA-256 game information, Game Genie/PAR cheats, bounded
  RetroArch/plain-text cheat import, live RAM search, and sparse per-game overrides
- System/light/dark themes, structured rotating logs, and copyable privacy-filtered
  diagnostics
- Startup-safe Qt localization with persisted system/English selection, explicit
  English fallback, and a packaged expanded pseudo-language for cross-platform layout
  and untranslated-string regression testing
- Unified General/Video/Audio/Input/System/BIOS/Paths/Advanced settings center with
  live summaries and direct access to each validated category editor
- Opt-in native debug workspace with live CPU, memory, VDP, sound, input, and validated
  save-state inspection; it remains hidden during normal play unless explicitly enabled
- Headless unit, core, integration, GUI, stress, property, and sanitizer tests

## Installing

Tagged releases produce six versioned packages plus SHA-256 checksum files. Until a
release is authorized, equivalent packages are available as successful workflow
artifacts or can be built from source.

### Windows 10/11 x64

Download `Genesis-Plus-GX-GUI-<version>-windows-x86_64.zip`, verify its checksum,
extract the entire archive, and run `bin/genesis-plus-gx-gui.exe`. The portable package
contains the required Qt and SDL runtimes and does not modify the registry. It also
includes Microsoft's official `vc_redist.x64.exe`; run that installer if Windows
reports that the Microsoft Visual C++ runtime is missing.

### Linux x86-64

Download `Genesis-Plus-GX-GUI-<version>-linux-x86_64.tar.gz`, verify and extract it,
then run `bin/genesis-plus-gx-gui`. The archive includes private Qt/SDL application
libraries but relies on the host's standard graphics, window-system, C/C++ runtime, and
libc libraries. It is built against Ubuntu 24.04; a local source build is the fallback
for incompatible distributions.

### macOS Apple Silicon and Intel

Choose the matching `macos-arm64` or `macos-x86_64` ZIP/DMG and copy
`genesis-plus-gx-gui.app` to Applications. Development packages are unsigned, so macOS
may require explicit approval in Privacy & Security. Official distribution signing and
notarization are documented in [PACKAGING.md](docs/PACKAGING.md).

## Quick start

Open a cartridge or disc with **File → Open Game**, drag a supported file onto the
window, launch it from the game library, or pass it on the command line:

```bash
genesis-plus-gx-gui [--fullscreen] [--portable] path/to/game.bin
genesis-plus-gx-gui --patch path/to/translation.bps path/to/game.bin
genesis-plus-gx-gui --help
genesis-plus-gx-gui --version
```

A ZIP containing one compatible cartridge opens directly; a multi-game ZIP presents a
keyboard-accessible member chooser. Opening an M3U starts its first disc and enables the
previous/next playlist-disc commands. Archived Sega CD sets are intentionally not
supported; keep CUE/BIN/CHD disc files beside a relative-path M3U instead.

For a cartridge patch, choose **File → Open Game with Patch…** (`Ctrl+Shift+O`),
drop the game and patch together, or use `--patch`. Ordinary Open automatically applies
one same-stem `.ips`, `.bps`, or `.ups` sidecar; if more than one matches, the emulator
asks you to use the explicit command. ZIP members can be patched explicitly after
selection. Disc images and M3U playlists are not soft-patched.

Use **Input → Controller Configuration** to assign or remap keyboard and SDL controllers.
Default keyboard controls and application hotkeys are listed in
[KEYBOARD_SHORTCUTS.md](docs/KEYBOARD_SHORTCUTS.md).

Choose a normal rate under **Emulation → Emulation Speed**, or open **Speed Settings…**
to configure the normal, slow-motion, and fast-forward percentages. Hold `/` for
momentary slow motion, use `Ctrl+/` to toggle it, hold `Tab` for fast forward, or use
`` ` `` to toggle fast forward. Non-100% pacing intentionally pauses host audio rather
than allowing pitch distortion or buffer drift.

To continue exactly where a clean exit left off, open **Tools → Settings → General
→ Session Settings** and enable automatic session resume. The application writes a
separate validated checkpoint on shutdown and restores it only for the same game and
hardware. Closing a game explicitly clears the pending session marker.

Choose **Video → Shaders → Built-in CRT** for the included scanline/aperture-grille
effect, or **Load Libretro Preset…** for a user-provided modern Slang `.slangp` preset.
See the [Libretro shader guide](docs/LIBRETRO_SHADERS.md) for compatibility and OpenGL
requirements. No third-party shader pack is bundled or downloaded.

Choose **Video → Artwork** for a local bezel behind the game or a transparent PNG
overlay in front. Optional percentage insets define a game aperture explicitly;
artwork never changes core pixels or input geometry. See the
[local artwork guide](docs/ARTWORK_OVERLAYS.md).

Choose **File → Start Lossless A/V Recording…** (`Ctrl+Shift+F12`) to capture native
frames and core audio into a self-contained `.gpgx-recording` directory. The writer is
bounded and asynchronous, so storage cannot block the emulation thread. See the
[recording guide](docs/RECORDING.md) for the PNG/WAV format and limits.

Choose **Emulation → Run-Ahead** to reduce cartridge-game input latency with one to four
bounded speculative frames. It is disabled by default, automatically suspends during
rewind or alternate-speed play, and fails closed if its first continuation is not
deterministic. Sega CD is currently excluded from speculation. See the
[run-ahead guide](docs/RUN_AHEAD.md) for accuracy, performance, and diagnostics details.

For Sega CD, configure your own USA, Europe, and/or Japan firmware under
**Tools → BIOS Settings** before opening a disc. Firmware is validated locally and is
never downloaded by the application. See [BIOS.md](docs/BIOS.md).

The complete operating guide is [USER_GUIDE.md](docs/USER_GUIDE.md). Focused guides
cover [input](docs/INPUT_CONFIGURATION.md), [save states](docs/SAVE_STATES.md),
[the game library](docs/GAME_LIBRARY.md), [cheats](docs/CHEATS.md), and
[appearance/accessibility](docs/APPEARANCE_AND_ACCESSIBILITY.md). Interface language,
fallback behavior, and translation contributions are documented in
[LOCALIZATION.md](docs/LOCALIZATION.md).

## User data and saves

Qt selects the platform application-data root. Typical locations are
`%APPDATA%/Genesis Plus GX GUI` on Windows,
`~/.local/share/Genesis Plus GX GUI` on Linux (subject to `XDG_DATA_HOME`), and
`~/Library/Application Support/Genesis Plus GX GUI` on macOS. Beneath it, the
application creates:

```text
config/       Versioned global and per-game settings
saves/        Cartridge SRAM and Sega CD BRAM/RAM-cartridge data
states/       Per-game save-state slots
screenshots/  Native PNG captures, unless overridden
recordings/   Default lossless A/V capture destination
library/      SQLite game-library index and local metadata
logs/         Rotating JSON Lines frontend logs
```

Single-file games use their raw SHA-256. CUE games use one domain-separated SHA-256
over the validated sheet and every referenced track, so matching sheet text cannot
make different discs share saves, states, cheats, or settings.
Tests always inject temporary roots and never touch this real directory.

Pass `--portable` to keep that same hierarchy in `portable-data` beside the executable
(or beside the `.app` on macOS). Portable mode is explicit for each launch: it never
redirects, copies, or merges normal user data, and startup fails instead of falling back
if the portable location is unwritable. See [PORTABLE_MODE.md](docs/PORTABLE_MODE.md)
for platform launch examples and relocation rules.

## Building from source

The desktop build requires CMake 3.25+, Ninja, a C++20 compiler, Qt 6.8+ with Core,
Gui, Widgets, OpenGLWidgets, Sql, Test, and LinguistTools modules, SDL 3.2+, and
Rust/Cargo 1.88+ for the default Libretro shader runtime. Once CMake can locate the Qt
and SDL config packages:

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Release and combined ASan/UBSan presets are also provided:

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release

cmake --preset asan
cmake --build --preset asan
ctest --preset asan
```

Platform dependency setup, package creation, and troubleshooting are in
[BUILDING.md](docs/BUILDING.md). Development boundaries and workflow are described in
[DEVELOPMENT.md](docs/DEVELOPMENT.md) and [ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Testing

CTest is the common runner. The complete suite uses generated CC0 cartridge, disc, and
firmware fixtures plus temporary directories; it never downloads commercial ROMs or
proprietary Sega BIOS images. GUI tests run headlessly on all CI platforms.

```bash
ctest --preset debug --output-on-failure
ctest --preset debug -L gui --output-on-failure
ctest --preset asan --output-on-failure
```

See [TESTING.md](docs/TESTING.md), [TEST_MATRIX.md](docs/TEST_MATRIX.md), the
[final release-candidate report](docs/FINAL_TEST_REPORT.md), and the fixture
[provenance record](tests/fixtures/README.md).

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Building](docs/BUILDING.md)
- [Development](docs/DEVELOPMENT.md)
- [User guide](docs/USER_GUIDE.md)
- [Debug tools](docs/DEBUG_TOOLS.md)
- [Libretro shaders](docs/LIBRETRO_SHADERS.md)
- [Local bezels and overlays](docs/ARTWORK_OVERLAYS.md)
- [Display synchronization](docs/DISPLAY_SYNCHRONIZATION.md)
- [Lossless recording](docs/RECORDING.md)
- [Run-ahead](docs/RUN_AHEAD.md)
- [Packaging](docs/PACKAGING.md) and [releases](docs/RELEASES.md)
- [Upstream maintenance](docs/UPSTREAM_MAINTENANCE.md)
- [Development plan and milestone evidence](docs/DEVELOPMENT_PLAN.md)
- [Requirements audit](docs/REQUIREMENTS_AUDIT.md)
- [Final test report](docs/FINAL_TEST_REPORT.md)
- [Changelog](CHANGELOG.md) and [third-party notices](THIRD_PARTY_NOTICES.md)

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md) before submitting changes. Core changes should
be exceptional, narrowly scoped, and backed by deterministic regression tests so future
upstream synchronization remains practical.

## License and upstream relationship

Genesis Plus GX and this modified source tree are distributed under the repository's
specific non-commercial terms in [LICENSE.txt](LICENSE.txt). In particular,
redistributions may not be sold or used in a commercial product or activity, and source
distribution obligations apply. Newly authored desktop/frontend code remains under
those repository terms unless its file explicitly says otherwise. This is not a generic
OSI open-source license; review it before redistributing binaries.

Qt, SDL, librashader, libchdr, zlib, zstd, Tremor, Nuked OPN2, minimp3, LZMA SDK, and
other bundled components retain their own licenses. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
and the complete notices in [LICENSE.txt](LICENSE.txt).

The authoritative emulator project is
[ekeeke/Genesis-Plus-GX](https://github.com/ekeeke/Genesis-Plus-GX). This repository is
a community GUI-enhanced fork/frontend and does not imply official upstream endorsement.
