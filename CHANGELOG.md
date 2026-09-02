# Changelog

All notable desktop-frontend changes are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); this project uses semantic
versioning for desktop packages. Upstream core history remains in Git and the inherited
platform documentation.

## [Unreleased]

### Changed

- The desktop Qt baseline is now 6.8 so the same supported Qt Linguist source-target
  extraction API is used on development and packaged builds.

### Added

- Disabled-by-default HTTPS WebDAV synchronization for cartridge SRAM, Sega CD BRAM,
  RAM-cartridge data, and wrapped state slots, with native secure password storage,
  bounded TLS transfers, content-addressed integrity, ETag-conditional manifests,
  deletion healing, collision-safe conflict copies, automatic safe points, diagnostics,
  and real loopback TLS/WebDAV plus two-client and GUI regressions.

- Disabled-by-default RetroAchievements integration through the official rcheevos
  `rc_client`, including supported-system hashing, achievements, measured progress,
  leaderboards, rich presence, bounded HTTPS transport, native secure token storage,
  offline recovery, schema-3 state progress, and emulation-thread Hardcore enforcement.

- Authenticated direct TCP netplay for one peer, with game/build/settings/BIOS/cheat
  compatibility checks, HMAC-SHA-256 challenge/response and per-packet integrity,
  replay rejection, 0–8 frame input delay, bounded 1–12-frame rollback on the
  emulation thread, deterministic-operation lockouts, diagnostics, and protocol,
  loopback transport, core integration, fuzz, and GUI regressions.

- Native Windows, Linux, and macOS optical-drive support for original mixed-mode Sega
  CD/Mega CD media, using a cancellable bounded worker, raw data/CDDA reads, validated
  atomic BIN/CUE snapshots, transient lifecycle cleanup, and generated legal core/GUI
  regressions without bundling games or firmware.

- Advanced hidden debugger tooling with real paused 68000/Z80 single-instruction
  stepping, opt-in bounded execution traces and drop accounting, atomic local symbol
  import, and versioned atomic JSON export for offline external analysis.

- Qt Linguist localization infrastructure with a schema-migrated System/English
  preference, startup-before-widgets catalog installation, package-relative resource
  discovery, safe English fallback, diagnostics, and a complete compiled expanded
  pseudo-language catalog used by unit, GUI, process, and package regressions on every
  supported platform.

- Explicit `--portable` application-data isolation beside the executable (or beside a
  macOS app bundle), with fail-closed startup, visible mode reporting, relocation-safe
  paths, native-package event-loop verification, and no implicit migration or fallback
  to the normal user profile.

- Bounded, atomic import of emulator-handled RetroArch `.cht` and simple local
  plain-text cheat lists, plus a system-aware live RAM search that creates disabled
  reviewable codes without exposing core globals or implicitly patching memory.
- Local-only bezel and foreground-alpha-overlay presentation with bounded cached
  PNG/JPEG/BMP decoding, opacity, explicit game-aperture insets, quick menus,
  schema-migrated global/per-game settings, privacy-safe diagnostics, and identical
  OpenGL/software composition.
- A 108-case real-OpenGL shader/artwork presentation matrix plus an optional 185-case
  user-owned-ROM option runner that rejects black, hidden, or inverted output.
- Off/on/adaptive vertical-synchronization and double/triple-buffer presentation
  controls with live surface recreation, schema migration, per-game overrides,
  newest-frame-only pending behavior, swap-cadence/drop diagnostics, and a real OpenGL
  six-case regression matrix.
- Optional one-to-four-frame bounded run-ahead for cartridge systems, with exact
  owner-thread rollback context, authoritative audio/input/state preservation,
  recording isolation, deterministic fail-closed verification, Sega CD exclusion,
  persisted settings, runtime diagnostics, and maximum-depth stress coverage.
- Bounded lossless A/V recording and continuous native frame dumps with sequential
  PNGs, stereo PCM WAV, per-frame/manifest metadata, drop instrumentation, atomic
  finalization, configurable hotkey, diagnostics, and real generated-ROM coverage.
- A visual save-state manager with native-frame PNG previews, optional per-slot names,
  frame/size/timestamp details, and identity-validated import/export. Schema-3 state
  envelopes remain backward-compatible with existing schema-1/schema-2 files, keep all
  presentation data outside the unchanged Genesis Plus GX payload, and optionally
  carry separately bounded RetroAchievements progress.
- Non-destructive IPS, BPS, and bidirectional UPS cartridge soft patching with strict
  bounds and CRC validation, automatic same-stem sidecars, explicit file/CLI/two-file
  drop selection, collision-safe caching, patched-content save/state/settings identity,
  visible status/logging, and automatic session-resume preservation.

- Bounded ZIP browsing for stored/deflated cartridge images, with deterministic member
  selection, safe cached extraction, CRC/size/ratio validation, source-aware recents,
  and command-line, drag/drop, session-resume, core, integration, and GUI coverage.
- Strict local M3U/M3U8 Sega CD playlists with traversal/symlink escape rejection,
  ordered previous/next disc controls, composite content identity, and generated
  multi-disc workflow tests.
- Configurable exact-rational emulation pacing with 50–200% normal speed, 25–75%
  slow motion, 200–1600% fast forward, normal-speed presets, a persistent accessible
  editor, live status/diagnostics, mutually exclusive rewind/slow/fast modes, and
  audio suppression whenever effective speed is not 100%.
- Independently configurable slow-motion hold and toggle hotkeys with conflict-safe
  input-schema migration, focus-loss release, and real worker/core pacing regressions.
- Opt-in automatic session resume with a dedicated identity-checked shutdown
  checkpoint, explicit command-line precedence, corruption-safe fallback to normal
  emulation, and transactional settings/GUI controls.
- Bounded in-memory rewind on the emulation thread, with a configurable 16–1024 MiB
  budget and 1–60-frame capture interval, a focus-safe Backspace hold hotkey, a menu
  toggle, audio suppression, diagnostics, persisted settings, and state-history tests.
- An opt-in, hidden-by-default developer workspace for live 68000/Z80 registers,
  bounded memory reads and paused writes, VDP registers/palette/tiles/sprites/planes/
  scroll data, YM2612/PSG state, logical input snapshots, run controls, and validated
  save-state slot operations.
- A thread-owned debug protocol that publishes immutable snapshots between frames and
  rejects mutation while emulation is running, keeping Genesis Plus GX globals out of
  the GUI thread.
- Bounded 8/16/32-bit signed or unsigned RAM search and watch tools, plus worker-owned
  68000/Z80 frame-boundary program-counter breakpoints that pause on a live match
  without enabling an always-on instruction hook.
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

- Netplay's two-process regression now waits for remote-frame proof before releasing
  its intentionally delayed input and coordinates both verified peers before teardown,
  eliminating a scheduler-dependent false pass/failure on macOS Debug. Orderly remote
  socket closure is also reported once as a peer departure instead of as a connection
  failure followed by a duplicate disconnect.
- Cross-platform release gates retain the complete 108-case software-OpenGL matrix
  with a realistic bounded timeout, and macOS DMG creation retries only the transient
  `hdiutil` resource-busy condition with scoped staging cleanup.
- Relocatable Linux packages now select their bundled XCB backend before Qt startup
  when no platform override is present, avoiding a missing-Wayland-plugin diagnostic
  on Wayland/XWayland desktops while preserving native Wayland source builds.
- The soft-patch core workflow no longer supplies the game-file static library twice,
  eliminating the duplicate-library linker warning found in all hosted macOS logs.
- Debug snapshots now use scalar byte assembly for inherited CRAM/VSRAM storage,
  preventing optimized Intel macOS builds from issuing a falsely aligned SIMD read and
  crashing the CPU/VDP inspection workflows.
- Debugger presentation and C-bridge conversions are explicit across Apple Clang and
  MSVC, and GUI integration tests no longer link the core adapter twice on macOS.
- Z80 RAM inspection, search, watch, and paused writes now target the active work RAM
  for SG-1000, Master System/Mark III, Game Gear, and Power Base Converter sessions
  instead of the inactive Genesis sound-CPU buffer. New sessions select their active
  CPU/RAM automatically, and inactive 68000 RAM access fails closed.
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
