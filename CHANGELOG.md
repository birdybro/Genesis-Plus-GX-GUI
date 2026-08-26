# Changelog

All notable desktop-frontend changes are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); this project uses semantic
versioning for desktop packages. Upstream core history remains in Git and the inherited
platform documentation.

## [Unreleased]

### Added

- Native Qt 6 Widgets application for Windows x64, Linux x86-64, macOS Apple Silicon,
  and macOS Intel.
- Reusable C++20 adapter around the authoritative Genesis Plus GX core and a dedicated,
  command-driven emulation thread.
- Bounded triple-buffer video exchange, SDL3 audio ring, frame-boundary input snapshots,
  rational PAL/NTSC/CD pacing, fast forward, pause, and frame advance.
- OpenGL/software presentation with dynamic viewport, aspect and integer scaling,
  filtering, overscan, interlace, Game Gear, high-DPI, fullscreen, and screenshots.
- Keyboard/controller profiles, hot-plug, player assignments, capture-based mappings,
  configurable emulator hotkeys, deadzones, conflict validation, and advanced emulated
  device selection.
- Per-game SRAM, Sega CD BRAM/RAM-cartridge persistence and identity-checked save-state
  slots with atomic files.
- Sega CD BIOS validation, CUE/BIN, ISO, CHD, CDDA, disc-change, and eject workflows.
- Recent games, asynchronous SQLite library, metadata/hashes, favorites, play history,
  local artwork, cheats, and sparse per-game overrides.
- Versioned settings, system/light/dark appearance, structured rotating logs, and
  privacy-filtered diagnostics.
- Bounded audio-device hot-plug handling with automatic default-device recovery after
  an explicitly selected playback device disconnects.
- Generated CC0 fixtures and unit, core, integration, GUI, property, stress, and
  ASan/UBSan coverage through CTest.
- Reproducible CMake presets, three-platform CI, CPack distributions, checksum gates,
  and guarded tag-driven release automation.

### Changed

- The repository README now presents the standalone desktop experience while retaining
  the upstream core, legacy libretro, GameCube/Wii, and SDL build paths.
- Desktop saves and settings use platform application-data directories instead of
  current-directory filenames.

### Security

- Bounded metadata/file parsers, validated CUE paths, atomic writes, wrong-game state
  rejection, privacy-filtered diagnostics, and deterministic malformed-input corpora.

[Unreleased]: https://github.com/birdybro/Genesis-Plus-GX-GUI/commits/master
