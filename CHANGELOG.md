# Changelog

All notable desktop-frontend changes are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); this project uses semantic
versioning for desktop packages. Upstream core history remains in Git and the inherited
platform documentation.

## [Unreleased]

### Added

- An opt-in, hidden-by-default developer workspace for live 68000/Z80 registers,
  bounded memory reads and paused writes, VDP registers/palette/tiles/sprites/planes/
  scroll data, YM2612/PSG state, logical input snapshots, run controls, and validated
  save-state slot operations.
- A thread-owned debug protocol that publishes immutable snapshots between frames and
  rejects mutation while emulation is running, keeping Genesis Plus GX globals out of
  the GUI thread.
- Adjustable built-in CRT rendering and modern Libretro Slang `.slangp` compatibility
  through a checksum-pinned librashader 0.12.0 OpenGL runtime, including multi-pass
  chains, lookup textures, frame history/feedback, preset parameters, and per-game
  overrides.
- Video-menu and Settings controls for selecting presets and editing declared shader
  parameters, with versioned persistence and relative shader-pack resource handling.
- Real OpenGL/librashader and `QOpenGLWidget` regression coverage that verifies input
  sampling, non-black output, upright orientation across all 36 presentation
  combinations, and both endpoints of every built-in CRT parameter under Xvfb.
- An opt-in external-ROM acceptance runner that exercises complete video, audio,
  emulated-input-device, and compatible system-option matrices against a user-owned
  game while writing comparison PNGs only to a caller-selected directory.

### Fixed

- Built-in and custom Libretro shader output is no longer vertically inverted by a
  redundant second texture-coordinate flip in the final presentation pass.
- Dynamic controller-assignment and shader-parameter forms now remove complete rows,
  eliminating invalid-layout diagnostics during normal dialog refreshes.
- Native Wayland sessions now configure the OpenGL 3.3 core surface format before
  constructing `QApplication`, keeping Qt's backing-store compositor context
  compatible with the emulator `QOpenGLWidget` instead of displaying a black game
  area while emulation continues normally.
- Emulator and shader output textures now explicitly constrain their mip level range,
  preventing a mip-capable Libretro sampler from producing a black frame when the
  source contains only level zero.
- Shader timing uniforms use the emulated PAL/NTSC nominal frame rate, and shader load,
  compile, or rendering failures release the failed chain and retain the normal
  unshaded display without retrying it every frame.
- The macOS OpenGL loader now resolves framework-exported core functions when Qt's
  extension resolver does not expose them, allowing librashader to initialize on both
  Apple Silicon and Intel hosts.
- Linux packages explicitly stage the imported Qt and SDL runtime targets before
  dependency closure, eliminating CMake fallback-directory deployment warnings.
- Windows deployment normalizes the Visual C++ Redistributable path before generating
  install scripts, eliminating invalid backslash-escape diagnostics.
- Linux deployment locates Qt's XCB QPA runtime without importing its private CMake
  component, and stages Qt's version-matched ICU companions, keeping source
  configuration independent of private Qt/XCB headers while making the archive
  independent of the build host's ICU version.
- Linux deployment rewrites copied Qt plugin RUNPATHs for the packaged directory depth
  and tests the archive through the real XCB backend, preventing an incompatible host
  `libQt6XcbQpa` from being selected before the application window is created.

## [0.1.1] - 2026-08-26

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
- A CI-only warning-as-error gate for newly authored desktop/frontend and test targets
  on GCC, Clang, Apple Clang, and MSVC.

### Fixed

- The legacy libretro frontend now rejects overlong Sega CD BRAM and RAM-cartridge
  paths instead of silently truncating them, and its CI regression is warning-clean.
- Windows release archives include Microsoft's official Visual C++ x64 Redistributable
  installer, and package verification rejects incomplete archives.
- Windows deployment does not probe for unused DirectX shader compilers, and the final
  inherited libchdr typedef warning is scoped out on Apple Clang.

### Changed

- The repository README now presents the standalone desktop experience while retaining
  the upstream core, legacy libretro, GameCube/Wii, and SDL build paths.
- Desktop saves and settings use platform application-data directories instead of
  current-directory filenames.
- Linux packaging explicitly uses normalized runtime-dependency matching on CMake 4.4
  and newer, eliminating a policy warning without changing older supported CMake runs.

### Fixed

- Linux portable packages now include Qt's XCB EGL/GLX integration plugins. The video
  widget also preflights an OpenGL context and offscreen surface before it can select
  accelerated rendering, preserving the menu bar, empty-game prompt, and status bar by
  falling back to Qt software rendering when OpenGL is unavailable.
- Cross-platform Release builds are free of the frontend signedness, shadowing,
  intentional-alignment, and duplicate-static-library warnings found during the first
  tagged release log audit.
- GitHub release publication uploads each verified asset independently with bounded
  retries and keeps the release in draft state until every archive and checksum is
  present, preventing a transient upload failure from publishing a partial release.

### Security

- Bounded metadata/file parsers, validated CUE paths, atomic writes, wrong-game state
  rejection, privacy-filtered diagnostics, and deterministic malformed-input corpora.
- CUE sheets are now size/line bounded and structurally preflighted at both game-load
  and disc-swap boundaries; absolute, traversal, missing, empty, and symlink-escaping
  track references are rejected before the inherited core parser opens them.

[Unreleased]: https://github.com/birdybro/Genesis-Plus-GX-GUI/commits/master
[0.1.1]: https://github.com/birdybro/Genesis-Plus-GX-GUI/releases/tag/v0.1.1
