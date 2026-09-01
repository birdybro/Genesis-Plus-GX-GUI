# Test Matrix

The desktop suite is registered with CTest and uses generated CC0 fixtures and temporary
directories. GUI tests set `QT_QPA_PLATFORM=offscreen`; workflows needing a framebuffer
also set `GENPLUSGX_FORCE_SOFTWARE_VIDEO=1`. No test reads or writes the real application
data directory.

Run the complete GUI regression suite with:

```bash
ctest --preset debug -L gui --output-on-failure
```

## GUI workflow coverage

| Product area | Automated behavior | Primary CTest |
| --- | --- | --- |
| Shell and menus | Window visibility, stable menu/action IDs, empty state, action gating, loaded system/region, measured/invalid FPS, bounded/deduplicated startup issues, audio and emulation runtime errors, About, exit | `gui.main_window`, `unit.frame_pacer` |
| Settings center | General/Video/Audio/Input/System/BIOS/Paths/Advanced navigation, live summaries, typed editor routing, loaded-game gating, visible rejection with prior snapshot/menu preservation | `gui.settings`, `gui.main_window`, `gui.input_configuration`, `gui.appearance_accessibility` |
| Navigation and help | Unique action IDs/shortcuts, live configurable hotkeys, embedded User Guide and Keyboard Shortcuts, one-dialog ownership, Escape/Close behavior | `gui.navigation_regression`, `gui.input_configuration` |
| Loading | Injected Open dialog, invalid input errors, drag/drop, transactional recent-history clear/failure, replace/close, live generated-ROM frame | `gui.game_loading`, `gui.main_window` |
| Emulation controls | Live Pause/Resume, hard/soft reset, independent fast-forward hold/toggle composition, focus-safe hold release, paused frame advance, canonical worker-state synchronization, rejected-command rollback | `gui.emulation_controls` |
| Save states | Slots 0–9, save/load/delete, timestamps, changed execution, deterministic restore, wrong-game rejection, cancellable identity activation | `unit.state_storage_service`, `gui.save_state_workflow`, `gui.main_window` |
| Session resume | Opt-in/defaults, absolute Unicode path persistence, invalid-data rejection, dedicated state save/load/delete, four-process checkpoint creation/restore, explicit command-line precedence, and corruption fallback | `unit.session_settings`, `unit.state_manager`, `unit.state_storage_service`, `gui.session_settings`, `gui.session_resume_application` |
| Video | Native/4:3/stretch, fit/integer scaling, nearest/bilinear, overscan, NTSC filter, Game Gear extension, interlace, fullscreen, built-in/custom Libretro shader selection and parameters, real OpenGL pass-through input sampling, actual CRT widget output, Apply/Cancel/defaults | `unit.shader_configuration`, `gui.main_window`, `gui.display_widget`, `gui.libretro_shader_render` |
| Audio | Mute/volume/core controls, live latency/device changes without ring replacement, transactional failure, stopped-output retry, concurrent logical-capacity changes, bounded hot-plug refresh/recovery, Apply/Cancel/defaults | `unit.audio_ring_buffer`, `unit.audio_output`, `gui.main_window` |
| Keyboard input | Defaults, custom maps, focus-safe event filter, snapshots reaching a generated controller-test ROM | `gui.keyboard_input` |
| Controller/input UI | Gameplay/hotkey capture, separate fast-forward hold/toggle defaults and schema migration, duplicate and cross-domain conflicts, persistence, assignments, live specialized ports, Team Player/Master Tap, stable tabs | `core.input_devices`, `unit.input_profile`, `gui.input_configuration` |
| BIOS | Missing/valid/invalid generated firmware, all eight paths propagated into the core, browse seam, validation status, persistence failure and Cancel | `core.firmware_application`, `gui.main_window` |
| Sega CD | Typed change/eject requests, current-disc status, invalid image errors, tray state, bounded CUE syntax/reference preflight, composite sheet/track identity, and invalid preflight without mounted-disc mutation | `unit.game_file`, `unit.persistence`, `unit.game_metadata`, `core.sega_cd_workflow`, `gui.main_window` |
| Game library | Directory add/remove, async scan/cancellation, CUE-track deduplication, search/system filter, favorite, sorting, local art, launch | `unit.game_library_scanner`, `gui.game_library` |
| Game information | Asynchronous request, bounded parsed metadata fields, failure recovery | `gui.main_window` |
| Screenshots | Native frame capture request, busy/error/success states, directory chooser and settings | `gui.main_window` |
| Cheats | Valid/invalid code behavior, enable/remove, persistence failure, game-session gating | `gui.cheats` |
| Per-game settings | Sparse overrides, nested editors, Use Global Settings, persistence failure, game-session gating | `gui.per_game_settings` |
| Appearance/accessibility | Settings-center routing, system/light/dark themes, Apply/OK/Cancel/defaults, live summary refresh, persistence failure, keyboard navigation, high-DPI policy | `gui.appearance_accessibility`, `gui.settings` |
| Diagnostics | Live snapshot refresh, privacy filtering, read-only report, clipboard equality, one-dialog ownership | `gui.diagnostics` |
| Developer tools | Hidden-by-default setting/menu, CPU/RAM/VDP/sound/input/state views, paused edits, typed RAM search/watch, frame-boundary breakpoints, real 68000/Z80 worker pauses, and active SG-1000/Master System/Game Gear RAM | `unit.debug_analysis`, `core.debug_tools`, `gui.debug_tools`, `gui.debug_tools_live` |
| Executable smoke | `--help`, `--version`, isolated full MainWindow/service startup, corrupt-settings user-visible error dispatch, persistent-path/SQLite creation, event-loop entry, and graceful shutdown | `gui.desktop_help`, `gui.desktop_version`, `gui.desktop_startup_smoke`, `gui.desktop_startup_error_smoke` |

Core system coverage uses original runtime-generated Z80 programs rather than metadata
placeholders. `core.eight_bit_systems` loads and executes SG-1000, forced Mark III,
Master System II, and Game Gear sessions, verifies a semantic work-RAM write, and checks
256×192 or 160×144 native geometry. Genesis and Sega CD retain their independent
generated 68000/disc workflow tests.

The GUI layer deliberately avoids platform-native pixel goldens. Presentation geometry
and emulator pixels are validated independently by deterministic unit/core hashes, while
GUI tests assert semantics and state transitions across platforms.

`gui.libretro_shader_render` is the focused exception to the normal offscreen software
path. Linux CI runs it under Xvfb with GLX and requires a real OpenGL 3.3 context. It
executes an original one-pass fixture through librashader, reads RGB output, then enables
the bundled CRT preset on the actual `DisplayWidget` and requires a non-black, upright
image that materially differs from the baseline. Its 36-case presentation matrix covers
every aspect, scaling, texture-filter, and shader mode, followed by minimum/maximum
coverage for all five CRT parameters. Windows and macOS use their native Qt platform;
they may return CTest skip code 77 only when the hosted environment cannot create a
desktop OpenGL context.

Core option-domain regressions explicitly execute 13 video choices, 35 audio
enumeration/range-endpoint choices, all 12 exposed emulated input devices, and all 24
system enumeration/toggle values. The generated 8-bit fixtures additionally execute
every compatible hardware override and both Game Gear viewport modes. GUI inventory
assertions require the corresponding combo counts/ranges plus all 30 emulator hotkeys.
The optional external-ROM runner described in [TESTING.md](TESTING.md) repeats 113
compatible runtime cases on a user-owned game and emits local comparison images.

## Supporting layers

CTest labels allow focused gates: `unit`, `core`, `integration`, `gui`, `persistence`,
`filesystem`, `security`, `parser`, `timing`, `audio`, `input`, `database`, `fuzz`, and
`smoke`. A milestone
gate always runs its focused tests and the complete applicable suite. ASan/UBSan runs use
the same deterministic corpus; optional user-supplied Sega CD BIOS tests remain outside
CI and are never counted as required.

The required `core.long_running_stability` test adds accelerated 20,000-frame core
execution, bounded queue/audio/video saturation, and repeated worker lifecycle coverage.
Detailed sanitizer and stress commands are in [TESTING.md](TESTING.md).

`unit.persistence` and `unit.game_metadata` also prove that the raw SHA-256 for ordinary
games remains backward compatible, identical CUE text cannot collide when a track
changes, relocating an unchanged sheet/track set preserves identity, every consumer
derives the same digest, and a cancellation request stops the 64 KiB stream. The scanner
test requires a valid CUE row while its otherwise-loadable `.bin` payload is absent.
`unit.backup_store` and `core.backup_persistence` prove cancellation leaves no active
save identity and that stopping the emulation worker interrupts an in-progress identity
consumer rather than waiting for it to finish.

`infrastructure.documentation` separately checks the required document manifest,
desktop/version/license feature text, stale milestone wording, and every local Markdown
link. Packaging and release metadata have their own `packaging` and `release` labels.
`unit.shutdown_report` verifies that clean exit remains successful, cleanup failures are
retained and summarized, and a pre-existing application failure is never masked. The
real-process smoke test independently requires the structured clean-shutdown record.
