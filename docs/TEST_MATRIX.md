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
| Input movies, TAS, and local streaming | Exact owner-thread frame input, raw initial-state playback, game/settings/build identity, atomic checksummed RLE format, bounded corruption corpus, rerecord-counted timeline edits, monotonic live-input recovery, loopback-only real TCP native A/V, fixed queue/client/backlog limits, diagnostics, and accessible GUI transition gates | `unit.input_movie`, `integration.input_movie`, `unit.streaming_service`, `integration.streaming`, `gui.movie_streaming`, `unit.diagnostics`, `gui.diagnostics` |
| Cloud synchronization | Disabled defaults, secret-free atomic settings, HTTPS/TLS enforcement, real loopback WebDAV verbs and conditional writes, recognized-file scanning, bounded manifests/transfers/queues, two-client uploads/downloads/conflicts, non-destructive deletion healing, atomic conflict copies, active cancellation, accessible GUI, and bidirectional transfer/game-load exclusion | `unit.cloud_sync`, `integration.cloud_webdav`, `gui.cloud_sync`, `unit.persistence`, `unit.diagnostics`, `gui.diagnostics` |
| Online metadata and artwork | Disabled defaults, settings validation/round-trip, approved-license allowlist, exact SHA-256 provider parsing, unlicensed Retronian thumbnail rejection, generic licensed manifests, real loopback TLS/trust/redirect/size behavior, bounded worker/cache, corrupt index/art recovery, SQLite schema migration/preservation/local-art priority, accessible settings and library workflows | `unit.online_metadata`, `integration.online_metadata_https`, `unit.game_library_database`, `unit.game_library_scanner`, `gui.online_metadata`, `gui.game_library`, `gui.settings` |
| RetroAchievements | Disabled defaults, credential-free atomic settings, strict HTTPS allowlist, bounded transport, mocked official-client login/token flow, supported console IDs/logical memory, recognized-game worker lifecycle, authoritative frames, schema-3 progress, offline behavior, accessible GUI, and owner-thread Hardcore restrictions including slowdown rejection and fast-forward permission | `unit.achievements`, `core.lifecycle`, `integration.achievement_worker`, `unit.state_manager`, `unit.state_storage_service`, `gui.achievements` |
| Netplay | HMAC challenge/response, tamper/replay/fuzz rejection, bounded Qt/wire/bridge queues, atomic invalid/start transactions, real localhost TCP peer exchange, two independent core processes, late-input rollback against a generated ROM, runtime-overflow teardown, secret handling, persistent GUI/worker lockouts, post-session checkpoint, disconnect | `unit.netplay_protocol`, `unit.netplay_timeline`, `integration.netplay_transport`, `integration.netplay_worker`, `integration.netplay_end_to_end`, `gui.netplay` |
| Shell and menus | Window visibility, stable menu/action IDs, empty state, action gating, loaded system/region, measured/invalid FPS, bounded/deduplicated startup issues, audio and emulation runtime errors, About, exit | `gui.main_window`, `unit.frame_pacer` |
| Settings center | General/Video/Audio/Input/System/BIOS/Paths/Advanced navigation, live summaries, typed editor routing, loaded-game gating, visible rejection with prior snapshot/menu preservation | `gui.settings`, `gui.main_window`, `gui.input_configuration`, `gui.appearance_accessibility` |
| Portable mode | Explicit CLI selection, executable/macOS-bundle placement, relocation, normal-profile isolation, complete hierarchy/startup/logging, unwritable-root fail-closed behavior, Paths/diagnostics visibility, installed-package startup, bounded transient DMG recovery | `unit.command_line`, `unit.persistence`, `unit.diagnostics`, `gui.settings`, `gui.desktop_portable_startup_smoke`, `gui.desktop_portable_failure_smoke`, `infrastructure.mac_dmg_retry` |
| Navigation and help | Unique action IDs/shortcuts, live configurable hotkeys, embedded User Guide and Keyboard Shortcuts, one-dialog ownership, Escape/Close behavior | `gui.navigation_regression`, `gui.input_configuration` |
| Loading and soft patches | Injected Open/patch dialogs, invalid input errors, one/two-file drag/drop, IPS/BPS/UPS bounds/checksums/reverse UPS, sidecar ambiguity, non-destructive cache and patched core execution, bounded ZIP member browser/extraction, M3U disc navigation, transactional recent-history clear/failure, replace/close, live generated-ROM frame | `unit.game_patch`, `unit.game_file`, `integration.soft_patch`, `integration.archive_playlist`, `gui.game_loading`, `gui.main_window` |
| Emulation controls and speed | Live Pause/Resume, hard/soft reset, configurable exact normal/slow/fast rates, independent hold/toggle composition, focus-safe release, mutually exclusive rewind/slow/fast modes, paused frame advance, canonical worker-state synchronization, rejected-command rollback | `unit.speed_settings`, `unit.frame_pacer`, `core.timing_pacing`, `gui.speed_settings`, `gui.emulation_controls` |
| Run-ahead | Disabled defaults, schema/bounds/corruption, real-core state/video/audio/input equivalence, exact CPU/audio/pad/multitap transient rollback, 1–4-frame depth, recording isolation, fast/slow/rewind suspension, frame advance, Sega CD exclusion, stable allocation, settings transactions and menu gating | `unit.run_ahead_settings`, `integration.run_ahead`, `gui.run_ahead_settings`, `gui.main_window`, `unit.diagnostics`, `gui.diagnostics` |
| Save states | Slots 0–9, schema-1/schema-2 compatibility, schema-3 names/PNG previews/achievement progress, visual browser, save/load/delete/rename, validated import/export, timestamps/frame/size details, malformed presentation/progress rejection, changed execution, deterministic restore, wrong-game rejection, cancellable identity activation | `unit.state_manager`, `unit.state_storage_service`, `gui.save_state_workflow`, `gui.main_window` |
| Session resume | Opt-in/defaults, schema migration, absolute Unicode game/patch persistence, invalid-data rejection, dedicated state save/load/delete, multi-process checkpoint creation/restore, explicit command-line game/patch precedence, patched-content identity, and corruption fallback | `unit.session_settings`, `unit.state_manager`, `unit.state_storage_service`, `gui.session_settings`, `gui.session_resume_application` |
| Video | Native/4:3/stretch, fit/integer scaling, nearest/bilinear, off/on/adaptive synchronization, double/triple buffering, one-frame pending bound and cadence/drop telemetry, overscan, NTSC filter, Game Gear extension, interlace, fullscreen, built-in/custom Libretro shaders, bounded local bezel/alpha-overlay decoding, opacity and explicit apertures, real OpenGL pass-through/composition, Apply/Cancel/defaults | `unit.presentation`, `unit.shader_configuration`, `unit.artwork`, `gui.main_window`, `gui.display_widget`, `gui.libretro_shader_render` |
| Audio | Mute/volume/core controls, live latency/device changes without ring replacement, transactional failure, stopped-output retry, concurrent logical-capacity changes, bounded hot-plug refresh/recovery, Apply/Cancel/defaults | `unit.audio_ring_buffer`, `unit.audio_output`, `gui.main_window` |
| Keyboard input | Defaults, custom maps, focus-safe event filter, snapshots reaching a generated controller-test ROM | `gui.keyboard_input` |
| Controller/input UI | Gameplay/hotkey capture, separate fast-forward and slow-motion hold/toggle defaults and schema migration, duplicate and cross-domain conflicts, persistence, assignments, live specialized ports, Team Player/Master Tap, stable tabs | `core.input_devices`, `unit.input_profile`, `gui.input_configuration` |
| BIOS | Missing/valid/invalid generated firmware, all eight paths propagated into the core, browse seam, validation status, persistence failure and Cancel | `core.firmware_application`, `gui.main_window` |
| Sega CD | Typed change/eject requests, current-disc status, invalid image errors, tray state, bounded CUE syntax/reference preflight, strict M3U/M3U8 ordering/navigation, composite sheet/track/playlist identity, and invalid preflight without mounted-disc mutation | `unit.game_file`, `unit.persistence`, `unit.game_metadata`, `integration.archive_playlist`, `core.sega_cd_workflow`, `gui.main_window` |
| Physical Sega CD media | Native backend discovery contracts, validated raw data/CDDA TOC and CUE, complete content hash and tamper rejection, atomic transient cache, read failure, active/queued cancellation, bounded service lifecycle, real core load/frame/unload, progress/error GUI recovery, typed launch and action gating | `unit.physical_media`, `integration.physical_media`, `gui.physical_media` |
| Game library | Directory add/remove, async scan/cancellation, CUE-track deduplication, search/system filter, favorite, sorting, local art priority, opt-in enriched titles/attribution, launch | `unit.game_library_database`, `unit.game_library_scanner`, `gui.game_library` |
| Game information | Asynchronous local request, bounded parsed fields, failure recovery, and attributed online description/provider/license presentation | `gui.main_window`, `gui.game_library` |
| Screenshots | Native frame capture request, busy/error/success states, directory chooser and settings | `gui.main_window` |
| Lossless recording | Real generated-ROM A/V capture, native PNG/dynamic geometry, stereo WAV/manifest/index, bounded queue/drop accounting, start/stop/replace GUI state, hotkey migration, shutdown finalization | `unit.recording_service`, `integration.recording`, `core.emulation_worker`, `gui.main_window`, `unit.input_profile` |
| Cheats | Valid/invalid code behavior, enable/remove, persistence failure, game-session gating, atomic bounded RetroArch/plain-text import, duplicate suppression, disabled-by-default review, token-isolated work-RAM search, generated typed code, and explicit patch application | `unit.cheats`, `core.cheats`, `gui.cheats`, `core.debug_tools` |
| Per-game settings | Sparse overrides, nested editors, Use Global Settings, persistence failure, game-session gating | `gui.per_game_settings` |
| Appearance/accessibility | Settings-center routing, system/light/dark themes, Apply/OK/Cancel/defaults, live summary refresh, persistence failure, keyboard navigation, high-DPI policy | `gui.appearance_accessibility`, `gui.settings` |
| Localization | Schema migration and persisted choice, exact extraction/catalog completeness, placeholder safety, package-relative loading, English fallback, translated real shell, expansion/layout/wrapping, RTL inheritance, keyboard navigation, and actual-process startup | `unit.appearance_settings`, `unit.localization`, `unit.translation_catalog`, `gui.localization`, `gui.desktop_pseudo_localization_smoke` |
| Diagnostics | Live snapshot refresh, privacy filtering, read-only report, clipboard equality, one-dialog ownership | `gui.diagnostics` |
| Developer tools | Hidden-by-default setting/menu, CPU/RAM/VDP/sound/input/state views, paused edits, typed RAM search/watch, frame-boundary breakpoints, real 68000/Z80 single-instruction steps and bounded execution hooks, trace overflow accounting, atomic symbol import/JSON export, live worker pauses/traces, and active SG-1000/Master System/Game Gear RAM | `unit.debug_analysis`, `core.debug_tools`, `gui.debug_tools`, `gui.debug_tools_live` |
| Executable smoke | `--help`, `--version`, `--patch`, `--portable`, isolated full MainWindow/service startup, portable fail-closed handling, patched restart, corrupt-settings user-visible error dispatch, persistent-path/SQLite creation, event-loop entry, and graceful shutdown | `unit.command_line`, `gui.session_resume_application`, `gui.desktop_help`, `gui.desktop_version`, `gui.desktop_startup_smoke`, `gui.desktop_portable_startup_smoke`, `gui.desktop_portable_failure_smoke`, `gui.desktop_startup_error_smoke` |

Core system coverage uses original runtime-generated Z80 programs rather than metadata
placeholders. `core.eight_bit_systems` loads and executes SG-1000, forced Mark III,
Master System II, and Game Gear sessions, verifies a semantic work-RAM write, and checks
256×192 or 160×144 native geometry. Genesis and Sega CD retain their independent
generated 68000/disc workflow tests.

`integration.archive_playlist` creates real stored/deflated ZIPs with MiniZip, validates
and extracts exact members, executes generated Genesis and Master System cartridges,
then resolves a generated M3U and changes between two generated Sega CD images through
the production adapter. GUI coverage drives single- and multi-member archives,
selection cancellation, source/runtime identity, playlist navigation, and a full-process
ZIP command-line/session-resume cycle.

The GUI layer deliberately avoids platform-native pixel goldens. Presentation geometry
and emulator pixels are validated independently by deterministic unit/core hashes, while
GUI tests assert semantics and state transitions across platforms.

`gui.libretro_shader_render` is the focused exception to the normal offscreen software
path. Linux CI runs it under Xvfb with GLX and requires a real OpenGL 3.3 context. It
executes an original one-pass fixture through librashader, reads RGB output, then enables
the bundled CRT preset on the actual `DisplayWidget` and requires a non-black, upright
image that materially differs from the baseline. Its 108-case presentation matrix covers
every aspect, scaling, texture-filter, shader, and artwork mode with asymmetric overlay
orientation checks, followed by minimum/maximum
coverage for all five CRT parameters. It then rebuilds the live widget across all six
sync/buffering requests, verifies effective state is observable, and recaptures an
upright nonblack framebuffer after each rebuild. Windows and macOS use their native Qt platform;
they may return CTest skip code 77 only when the hosted environment cannot create a
desktop OpenGL context.

Core option-domain regressions explicitly execute 13 video choices, 35 audio
enumeration/range-endpoint choices, all 12 exposed emulated input devices, and all 24
system enumeration/toggle values. The generated 8-bit fixtures additionally execute
every compatible hardware override and both Game Gear viewport modes. GUI inventory
assertions require the corresponding combo counts/ranges plus all 33 emulator hotkeys.
The optional external-ROM runner described in [TESTING.md](TESTING.md) repeats 185
option cases plus three speed-mode workflows on a user-owned game and emits local
comparison images. It additionally exercises all four run-ahead depths through the real
worker/core path.

## Supporting layers

Signed update coverage is split across `unit.signed_updates`,
`integration.signed_update_https`, `gui.signed_updates`, and
`infrastructure.signed_update_manifest`. Together they cover the Ed25519 trust root,
strict manifest parser, highest-seen rollback state, real TLS/redirect/bounds behavior,
streamed atomic package verification, user-controlled handoff, release tooling, and
tamper rejection without contacting GitHub or using a production private key.

CTest labels allow focused gates: `unit`, `core`, `integration`, `gui`, `persistence`,
`filesystem`, `security`, `parser`, `timing`, `audio`, `input`, `database`, `fuzz`, and
`smoke`. A milestone
gate always runs its focused tests and the complete applicable suite. ASan/UBSan runs use
the same deterministic corpus; optional user-supplied Sega CD BIOS tests remain outside
CI and are never counted as required.

The required `core.long_running_stability` test adds accelerated 20,000-frame core
execution, input/speed-command coalescing, normal/slow/fast bounded queue/audio/video
saturation, and repeated worker lifecycle coverage.
`integration.run_ahead` separately executes 120 maximum-depth host frames (480
speculative core frames) after transition coverage and requires rollback vector
capacity, audio occupancy, video publication, recording cadence, and determinism
counters to remain bounded.
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
