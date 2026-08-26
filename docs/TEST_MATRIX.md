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
| Shell and menus | Window visibility, stable menu/action IDs, empty state, action gating, status fields, About, exit | `gui.main_window` |
| Navigation and help | Unique action IDs/shortcuts, live configurable hotkeys, embedded User Guide and Keyboard Shortcuts, one-dialog ownership, Escape/Close behavior | `gui.navigation_regression`, `gui.input_configuration` |
| Loading | Injected Open dialog, invalid input errors, drag/drop, recent history, replace/close, live generated-ROM frame | `gui.game_loading` |
| Emulation controls | Live Pause/Resume, hard/soft reset, independent fast-forward hold/toggle composition, focus-safe hold release, paused frame advance, canonical worker-state synchronization, rejected-command rollback | `gui.emulation_controls` |
| Save states | Slots 0–9, save/load/delete, timestamps, changed execution, deterministic restore, wrong-game rejection | `gui.save_state_workflow`, `gui.main_window` |
| Video | Native/4:3/stretch, fit/integer scaling, nearest/bilinear, overscan, NTSC filter, Game Gear extension, interlace, fullscreen, Apply/Cancel/defaults | `gui.main_window`, `gui.display_widget` |
| Audio | Mute, volume, output/latency and core audio controls, bounded hot-plug events, selected-device recovery, Apply/Cancel/defaults | `unit.audio_output`, `gui.main_window` |
| Keyboard input | Defaults, custom maps, focus-safe event filter, snapshots reaching a generated controller-test ROM | `gui.keyboard_input` |
| Controller/input UI | Gameplay/hotkey capture, separate fast-forward hold/toggle defaults and schema migration, duplicate and cross-domain conflicts, persistence, assignments, live specialized ports, Team Player/Master Tap, stable tabs | `core.input_devices`, `unit.input_profile`, `gui.input_configuration` |
| BIOS | Missing/valid/invalid generated firmware, all eight paths propagated into the core, browse seam, validation status, persistence failure and Cancel | `core.firmware_application`, `gui.main_window` |
| Sega CD | Typed change/eject requests, current-disc status, invalid image errors, tray state | `gui.main_window` |
| Game library | Directory add/remove, async scan, search/system filter, favorite, sorting, local art, launch | `gui.game_library` |
| Game information | Asynchronous request, bounded parsed metadata fields, failure recovery | `gui.main_window` |
| Screenshots | Native frame capture request, busy/error/success states, directory chooser and settings | `gui.main_window` |
| Cheats | Valid/invalid code behavior, enable/remove, persistence failure, game-session gating | `gui.cheats` |
| Per-game settings | Sparse overrides, nested editors, Use Global Settings, persistence failure, game-session gating | `gui.per_game_settings` |
| Appearance/accessibility | System/light/dark themes, Apply/OK/Cancel/defaults, persistence failure, keyboard navigation, high-DPI policy | `gui.appearance_accessibility` |
| Diagnostics | Live snapshot refresh, privacy filtering, read-only report, clipboard equality, one-dialog ownership | `gui.diagnostics` |
| CLI smoke | `--help` and `--version` without constructing a window | `gui.desktop_help`, `gui.desktop_version` |

Core system coverage uses original runtime-generated Z80 programs rather than metadata
placeholders. `core.eight_bit_systems` loads and executes SG-1000, forced Mark III,
Master System II, and Game Gear sessions, verifies a semantic work-RAM write, and checks
256×192 or 160×144 native geometry. Genesis and Sega CD retain their independent
generated 68000/disc workflow tests.

The GUI layer deliberately avoids platform-native pixel goldens. Presentation geometry
and emulator pixels are validated independently by deterministic unit/core hashes, while
GUI tests assert semantics and state transitions across platforms.

## Supporting layers

CTest labels allow focused gates: `unit`, `core`, `integration`, `gui`, `persistence`,
`filesystem`, `timing`, `audio`, `input`, `database`, `fuzz`, and `smoke`. A milestone
gate always runs its focused tests and the complete applicable suite. ASan/UBSan runs use
the same deterministic corpus; optional user-supplied Sega CD BIOS tests remain outside
CI and are never counted as required.

The required `core.long_running_stability` test adds accelerated 20,000-frame core
execution, bounded queue/audio/video saturation, and repeated worker lifecycle coverage.
Detailed sanitizer and stress commands are in [TESTING.md](TESTING.md).

`infrastructure.documentation` separately checks the required document manifest,
desktop/version/license feature text, stale milestone wording, and every local Markdown
link. Packaging and release metadata have their own `packaging` and `release` labels.
