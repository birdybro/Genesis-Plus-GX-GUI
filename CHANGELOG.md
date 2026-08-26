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
- Emulation-thread propagation for three-/six-button pads, specialized peripherals,
  Sega Team Player, and Master Tap layouts selected by input profiles.
- Independently configurable fast-forward hold and toggle hotkeys, with focus-safe
  momentary release and migration that preserves existing toggle bindings.
- Unified eight-page Settings center with live category summaries, resolved platform
  data paths, loaded-game gating, and typed routes to transactional category editors.
- Per-game SRAM, Sega CD BRAM/RAM-cartridge persistence and identity-checked save-state
  slots with atomic files.
- Sega CD BIOS validation, CUE/BIN, ISO, CHD, CDDA, disc-change, and eject workflows.
- Recent games, asynchronous SQLite library, metadata/hashes, favorites, play history,
  local artwork, cheats, and sparse per-game overrides.
- Versioned settings, system/light/dark appearance, structured rotating logs, and
  privacy-filtered diagnostics.
- Bounded audio-device hot-plug handling with automatic default-device recovery after
  an explicitly selected playback device disconnects.
- Transactional live audio-device and latency changes that retain the worker-owned ring,
  preserve pause/running state, refresh open device lists, and roll back on failure.
- Live status-bar system/region identity and bounded monotonic measurement of actual
  normal-speed, paused, and fast-forward frame cadence.
- Generated CC0 fixtures and unit, core, integration, GUI, property, stress, and
  ASan/UBSan coverage through CTest.
- A hermetic full-process smoke test that constructs the real desktop shell, starts its
  services, enters the event loop, and verifies graceful shutdown from structured logs.
- Consolidated user-visible startup issue reporting, OpenGL-to-software fallback alerts,
  audio-device recovery errors, and fatal emulation-worker startup diagnostics.
- Transactional runtime video, system, input-profile, controller-assignment, and recent
  history updates that preserve prior state and show worker or persistence rejections.
- Visible, one-shot runtime service failure reporting for emulation, audio, save states,
  metadata, and library history, plus cleanup aggregation that makes incomplete final
  save/service shutdown return a failing process status.
- Runtime-generated Z80 fixtures covering SG-1000, Mark III, Master System, and Game
  Gear execution, plus generated cartridge-firmware activation tests for every BIOS slot.
- Reproducible CMake presets, three-platform CI, CPack distributions, checksum gates,
  and guarded tag-driven release automation.
- Composite Sega CD identities that hash a validated CUE and all referenced track
  content, preventing same-text sheets from sharing saves, states, cheats, overrides,
  or library identifiers while remaining stable after relocation.
- Cooperative 64 KiB identity-hash cancellation for live backup memory, save states,
  metadata, and library workers, plus CUE payload suppression so a library scan does
  not list one disc twice.

### Changed

- The repository README now presents the standalone desktop experience while retaining
  the upstream core, legacy libretro, GameCube/Wii, and SDL build paths.
- Desktop saves and settings use platform application-data directories instead of
  current-directory filenames.
- Linux packaging explicitly uses normalized runtime-dependency matching on CMake 4.4
  and newer, eliminating a policy warning without changing older supported CMake runs.

### Security

- Bounded metadata/file parsers, validated CUE paths, atomic writes, wrong-game state
  rejection, privacy-filtered diagnostics, and deterministic malformed-input corpora.
- CUE sheets are now size/line bounded and structurally preflighted at both game-load
  and disc-swap boundaries; absolute, traversal, missing, empty, and symlink-escaping
  track references are rejected before the inherited core parser opens them.

[Unreleased]: https://github.com/birdybro/Genesis-Plus-GX-GUI/commits/master
