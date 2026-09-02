# Testing

All tests are registered with CTest. They use generated CC0 ROM/firmware/disc fixtures
and temporary directories; proprietary games and Sega BIOS images are neither required
nor downloaded. See `tests/fixtures/README.md` for provenance.

## Standard gates

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug

cmake --preset release
cmake --build --preset release
ctest --preset release
```

## Netplay tests

`unit.netplay_timeline` covers input delay, player ownership, prediction correction,
duplicate/old/future rejection, history pruning, and bridge overflow. The bounded fuzz
corpus and tamper/replay cases live in `unit.netplay_protocol`.
`integration.netplay_transport` creates two real localhost TCP sockets and verifies
mutual authentication, role assignment, authenticated input exchange, wrong-code and
wrong-game rejection, and disconnect. `integration.netplay_worker` executes the legal
synthetic Genesis ROM through atomic reset/start, invalid transactional startup,
prediction, late-input rollback, audio suppression, bounded state history, mutation
lockout, runtime bridge-overflow teardown, post-session checkpoint capture, and clean
stop.
`integration.netplay_end_to_end` launches two child processes so each owns an
independent Genesis Plus GX core, then exercises the complete authenticated socket,
bridge, worker, delayed-input rollback, bounded-metric, disconnect, and shutdown path
in both directions. `gui.netplay` drives the
password field, host/join request, validation, stable object names, deterministic
lockouts, post-connect model refreshes, and disconnect under Qt offscreen.

Run the focused set with:

```bash
ctest --preset debug -R netplay --output-on-failure
```

Useful focused labels include `unit`, `core`, `integration`, `gui`, `persistence`,
`filesystem`, `fuzz`, `timing`, `audio`, `video`, `input`, `lifecycle`, `stress`,
`rewind`, `run-ahead`, `localization`, `packaging`, `release`, and `documentation`:

```bash
ctest --preset debug -L gui --output-on-failure
ctest --preset debug -L stress --output-on-failure
ctest --preset debug -L documentation --output-on-failure
ctest --preset debug -L localization --output-on-failure
```

The documentation gate requires every published guide, checks the release-facing README
sections and dependency inventory, rejects stale milestone language, and resolves local
Markdown links. It never follows external links or requires network access.

Hosted builds enable `GENPLUSGX_WARNINGS_AS_ERRORS`, so a new frontend/test compiler
warning fails the relevant platform job before CTest. This is intentionally not applied
wholesale to inherited core or bundled third-party sources.

`unit.localization` covers supported choices, catalog search paths, successful
installation/replacement, and invalid or unavailable fallback. `unit.translation_catalog`
runs Qt `lupdate` into a temporary directory, compares the complete source/context set
with the complete committed pseudo catalog, rejects missing meta-object contexts,
unfinished or placeholder-damaging translations, and loads the compiled QM.
`gui.localization` constructs the real translated shell and appearance editor, checks
stable object names/data, expanded-layout bounds and wrapping, English fallback,
right-to-left inheritance, and keyboard navigation. The real-process pseudo-language
smoke additionally requires requested/effective/fallback fields in the startup log.
See [LOCALIZATION.md](LOCALIZATION.md).

`unit.command_line` and `unit.persistence` cover explicit portable selection,
executable-relative and macOS-bundle placement, relocation, unsafe roots, and stable
mode reporting. `gui.desktop_portable_startup_smoke` enters the complete event loop in
an isolated derived root and verifies every data directory, SQLite initialization,
mode-tagged logging, and shutdown. `gui.desktop_portable_failure_smoke` blocks that root
with a regular file and requires a descriptive status-2 failure with no fallback.
Release package jobs repeat the positive startup against each actual installed
Linux/Windows/macOS layout before creating artifacts.
`infrastructure.mac_dmg_retry` deterministically verifies bounded recovery from a
transient macOS `hdiutil` resource-busy condition, three-attempt exhaustion, immediate
permanent-error failure, and rejection of a filesystem-root cleanup target.

`unit.rewind_buffer` proves strict byte-cap eviction and backward ordering;
`unit.rewind_settings` covers defaults, atomic persistence, validation, and corruption;
`core.rewind_worker` runs a generated Genesis program forward, rewinds to lower frame
numbers through the real worker/core state path, verifies the audio ring remains empty,
then resumes forward. `gui.rewind_settings`, `gui.emulation_controls`, and
`gui.input_configuration` cover settings transactions, toggle/hold/focus behavior,
hotkey migration, and stable widget identifiers.

`unit.run_ahead_settings` covers disabled defaults, atomic schema-1 persistence, strict
one-through-four-frame validation, bounded malformed input, and cartridge/Sega CD
support classification. `integration.run_ahead` drives the generated Genesis fixture
through exact raw plus transient rollback, compares authoritative state/audio/input
with an independent core baseline, verifies only the final speculative native frame is
displayed/recorded, exercises fast-forward/slow-motion/rewind suspension and return,
directly restores a mutated Team Player handshake, and runs 120 host frames at maximum
depth without allocation growth. The same test
loads generated Sega CD firmware/disc data and requires authoritative-only fallback.
`gui.run_ahead_settings`, `gui.settings`, and `gui.main_window` cover transactions,
persistence failure, Settings Center routing, menu state, and unsupported-hardware
gating.

`unit.speed_settings` covers safe defaults, exact atomic round trips, every domain
boundary, fractional/malformed/future-schema input, and the 32 KiB read cap.
`unit.frame_pacer` proves exact rational deadlines at custom normal, slow, and maximum
fast-forward rates. `core.timing_pacing` measures all three modes through a generated
Genesis program and requires alternate-speed audio to remain empty;
`gui.speed_settings` and `gui.emulation_controls` cover presets, transactions,
accessibility, status, mutual exclusion, and configurable hold/toggle semantics.

`unit.game_file` creates stored and deflated ZIPs and strict M3U playlists in temporary
directories. It verifies bounded member enumeration, exact/CRC-checked cached extraction,
unsafe-name and stale-selection rejection, cache reuse, UTF-8 and line/disc limits,
absolute/URL/traversal/symlink rejection, missing/duplicate discs, and deterministic
parser fuzzing. `integration.archive_playlist` runs extracted Genesis and Master System
members plus an M3U-driven generated Sega CD disc change through the real core adapter.
The GUI suite covers archive selection, cancellation, drag/drop/open source identity,
playlist action gating, and an isolated executable ZIP resume workflow.

`unit.physical_media` uses an injected mixed-mode optical backend to validate table of
contents and CUE generation, raw-sector bounds, the Sega CD signature, exact complete
SHA-256 cache verification, same-size tamper rejection, atomic commit/removal, injected
read failure, monotonic progress, active and queued cancellation, bounded service
events, native backend discovery contracts, and joined shutdown.
`integration.physical_media` imports the same generated raw BIN/CDDA layout, loads it
through `CoreAdapter` with generated non-proprietary firmware, verifies two-track Sega
CD timing, executes a frame, and releases the snapshot only after core shutdown.
`gui.physical_media` drives the stable File action and non-modal selector through
discovery, keyboard-accessible selection, progress, cancellation, failure recovery,
typed launch, status, and cleanup. Required CI covers each native backend's compile and
discovery boundary without requiring user hardware; optional real-drive qualification
is documented in [PHYSICAL_MEDIA.md](PHYSICAL_MEDIA.md).

`unit.game_patch` exercises IPS literals/RLE/growth/truncation, every BPS action,
forward and reverse UPS, all CRC/source mismatch paths, cache reuse/collisions,
sidecar ambiguity, and a fixed-seed 512-case mutation corpus. `integration.soft_patch`
changes an immediate operand in the generated Genesis program, executes the cached ROM
through the real core, verifies the changed RAM marker and distinct SHA-256 identity,
then reloads the unchanged source. GUI tests cover explicit and automatic selection,
two-file drop, visible patch status, disc rejection, and malformed/ambiguous errors.
`gui.session_resume_application` additionally starts the real desktop with `--patch`,
checks an identity-specific checkpoint, and restores the exact game/patch pair in a
second process.

Qt GUI tests use the offscreen platform automatically. Tests requiring frame
presentation force the deterministic software display path. Native widget chrome is not
pixel-compared across platforms; emulator framebuffers and geometry are tested below the
window layer.

`core.debug_tools` loads generated CC0 Genesis, SG-1000, Master System, and Game Gear
fixtures and verifies owner-thread snapshots, the active CPU/RAM backing store, logical
byte order, immutable publication, bounded reads/writes, paused-only mutation, worker
serialization, oversized-command rejection, and a live frame-boundary breakpoint hit.
`unit.debug_analysis` checks endian-aware typed reads, signed values, candidate bounds,
and exact/change filters. `gui.debug_tools` verifies that the developer menu is opt-in
and drives every CPU, memory, VDP, sound, input, search, watch, breakpoint, control, and
state surface using typed responses. `gui.debug_tools_live` wires that GUI to the real
emulation worker and executes both generated 68000 and Z80 programs through actual
breakpoint pause and RAM-inspection workflows. None of these tests reads a commercial
ROM or the user's settings.

`unit.cheats` additionally parses bounded RetroArch-style and simple text lists,
requires atomic rejection of malformed/oversized/invalid-UTF-8/direct-memory-only
input, forces imports disabled, and runs a fixed-seed 2000-input mutation corpus.
`gui.cheats` drives the injected chooser, duplicate suppression, no-implicit-apply
rule, token-isolated live search, signed/value filtering, generated cheat row, and the
subsequent explicit enable/Apply transition. `core.cheats` writes markers into generated
Genesis and Master System work RAM, discovers them through the production debug
snapshot and search model, converts them to Action Replay and Fusion RAM codes, applies
each on the core owner, and verifies the patched value after a frame. `core.debug_tools`
also requires request tokens to survive adapter, queue, and asynchronous breakpoint-hit
paths. It additionally executes exactly one real 68000 and Z80 instruction while
paused, rejects inactive/running steps, records both execute-hook identities, fills the
fixed 4,096-record ring, and verifies exact overwrite accounting.
`unit.debug_analysis` validates the bounded atomic symbol grammar and versioned JSON
trace export. The two debugger GUI tests drive the stable step/trace/symbol/export
controls and collect a live worker trace; closing the workspace must disable and clear
the core hook.

`gui.libretro_shader_render` intentionally uses a real OpenGL context. Linux CI installs
Xvfb and runs the test through XCB/GLX with `GENPLUSGX_REQUIRE_OPENGL_SHADER_TEST=1`, so
failure to create or execute the OpenGL 3.3 shader path is fatal. The test samples a
known input through an original Slang pass and then verifies that the actual display
widget's built-in CRT output is non-black and differs from its unshaded baseline.
It also invokes and verifies the production pre-`QApplication` surface-format setup;
this ordering is required for Qt to composite the accelerated child correctly on
native Wayland.
The widget portion uses an asymmetric quadrant frame and asymmetric top/bottom local
artwork, rejecting a vertical or horizontal orientation change. It executes all 108
combinations of native/4:3/stretch, fit/integer, nearest/bilinear,
off/built-in/custom shader, and off/bezel/overlay artwork presentation, then checks both
endpoints of all five bundled CRT parameters.
Windows and macOS run on their native Qt platforms and use skip code 77 only when the
hosted environment provides no usable desktop OpenGL context.

## Optional real-ROM option acceptance

`genplusgx_external_rom_acceptance_test` is built with shader-enabled desktop tests but
is intentionally not registered as a required CTest: it requires a game legally
provided by the developer. It loads the file through the real `CoreAdapter`, executes
13 core-video, 35 core-audio, 12 emulated-device, 108 accelerated-presentation, and 17
compatible system-reload cases, then reloads it through `EmulationWorker` at normal,
slow-motion, and fast-forward rates. Every mode must publish a non-black frame; host
audio must remain empty outside 100%. Shader images must be closer to the unshaded
upright image than to its vertical mirror. It also creates a temporary IPS record that
changes one Genesis header byte, runs the separately cached result for 120 frames, and
requires a non-black frame without touching the supplied source. Finally, it exercises
all four run-ahead depths with deterministic restoration, non-black future video,
authoritative audio, and bounded rollback storage.

Run it on a native desktop backend with an output directory outside the source tree:

```bash
QT_QPA_PLATFORM=wayland \
  ./build/debug/tests/gui/genplusgx_external_rom_acceptance_test \
  "/path/to/a/user-owned-game.md" /tmp/genplusgx-video-comparisons
```

The output PNGs contain frames from the supplied game. They are for local inspection;
never commit, publish, or upload them without the rights holder's permission. The
program never copies the ROM or firmware into the source/build tree. Required CI uses
the CC0 synthetic fixtures through the same core and display assertions.

LeakSanitizer keeps a narrow symbol-based suppression file for allocations rooted in
the host NVIDIA GL/EGL driver and Qt's system DBus library during this real-context
process.
It does not disable leak checking for the shader test or suppress any project,
librashader, Mesa, or generic allocation frame; all other sanitizer diagnostics remain
fatal. The suppression exists because those process-global host caches are outside the
frontend's ownership and persist after every context/widget has been destroyed.

`gui.desktop_startup_smoke` launches the actual desktop executable rather than a test
facsimile. Its CMake harness creates a configuration-specific root below the build tree,
uses Qt offscreen and SDL dummy audio, and supplies a bounded test-only auto-quit delay.
The process must initialize its directory tree and SQLite library, select a renderer,
enter the event loop, and emit the final clean-shutdown record. The hook is ignored in
normal launches unless the explicit test sentinel is present; malformed, relative, or
overly long-lived test requests fail before services are constructed.

Linux Release packaging additionally launches the staged executable with
`XDG_SESSION_TYPE=wayland` while leaving `QT_QPA_PLATFORM` unset. The XCB-only portable
layout must select its bundled backend before Qt startup, print no missing-Wayland-plugin
diagnostic, and still honor an explicit platform choice; `unit.command_line` exercises
the corresponding XCB-only, bundled-Wayland, and override branches directly.

## Sanitizers

Linux and compatible Clang/GCC hosts can run AddressSanitizer and
UndefinedBehaviorSanitizer together:

```bash
cmake --preset asan
cmake --build --preset asan
ctest --preset asan
```

The preset enables `-fsanitize=address,undefined`, frame pointers, leak detection,
immediate failure, and UBSan stack traces. It runs the same core adapter, generated-ROM,
save-state, persistence/file parsing, integration, GUI, and stability tests as Debug.
Suppressions are intentionally not installed for project code.

## Long-running stability workload

`core.long_running_stability` completes a deterministic accelerated workload without
waiting in real time for its 20,000 direct core frames. It then verifies the actual
threaded scheduler and bounded exchanges with:

- 20 direct load/run/unload cycles;
- 10,000 rapidly submitted input snapshots plus 1,000 speed snapshots and
  newest-command coalescing;
- 90 normal paced frames that deliberately fill, but never exceed, the audio ring;
- 20 quarter-speed frames with host audio suppressed;
- 600 fast-forward frames with host audio suppressed;
- fixed triple-buffer allocation and bounded command/event queue depths;
- 12 worker start/load/frame/unload/stop cycles; and
- a final global core-ownership lease probe.

Core resource metrics capture framebuffer, audio scratch, save-state scratch, and
state-load scratch capacities before the accelerated run and require them to remain
unchanged afterward. ASan leak checking supplies the process-allocation backstop.

Run the workload alone with:

```bash
ctest --preset asan -R '^core\.long_running_stability$'
```

The normal test has a 90-second timeout; current Debug and sanitizer runs are expected to
finish substantially faster on ordinary CI hardware.

## Continuous integration

`.github/workflows/ci.yml` runs on pushes to `master` and `desktop-gui`, and on every
pull request. Ubuntu 24.04 jobs build and test the complete desktop application in Debug
and Release, repeat the full suite under ASan/UBSan, and compile the inherited libretro
target as a compatibility regression. Windows Server 2022 jobs build and test Debug and
Release with the Visual Studio 2022 x64 toolchain. macOS 15 jobs run the same Debug and
Release suites natively on Apple Silicon and Intel runners. Qt 6.8.3 and SDL 3.4.14 are
explicit; Rust 1.88 is pinned for the librashader build; third-party actions are pinned
to immutable commit IDs. Failed CTest jobs
upload `LastTest.log` and CMake configure diagnostics with platform, architecture, and
configuration-specific names.

The hosted jobs use the same essential commands as a local run:

```bash
cmake -S . -B build/ci-Debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DGENPLUSGX_BUILD_DESKTOP=ON
cmake --build build/ci-Debug --parallel 2
QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy \
  ctest --test-dir build/ci-Debug --output-on-failure --no-tests=error --timeout 120
```

CI has least-privilege read-only repository contents permission, cancels obsolete runs
on the same ref, and never enables external proprietary-BIOS fixtures. Windows uses
Qt's MSVC 2022 x64 binaries and a matching source-built SDL library; SDL's DLL directory
is exported for test and application startup. Its emulation worker requests 1 ms timer
resolution only while the thread is alive so sub-default-tick fast-forward deadlines
retain the same bounded pacing semantics as Linux. macOS uses Clang, Ninja, the host's
native Qt package, and a host-native SDL build; neither architecture is cross-compiled.

## Optional external BIOS test

The Sega CD external-fixture test is opt-in and excluded from required CI. Configure
with `GENPLUSGX_ENABLE_EXTERNAL_FIXTURE_TESTS=ON` and provide the documented environment
variables from `docs/BIOS.md`. A user-supplied BIOS is never copied into the source or
build tree.
