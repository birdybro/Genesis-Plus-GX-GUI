# Desktop GUI Development Plan

This is the authoritative milestone ledger. A milestone is complete only after its own
gate and all applicable regression tests pass, the tree is inspected, this ledger is
updated, and the milestone is committed and pushed. Commit SHAs are recorded by the
following milestone because a commit cannot contain its own final SHA.

Status values: `IN PROGRESS`, `PLANNED`, `COMPLETE`, and `BLOCKED`.

## Progress summary

| Milestone | Status | Goal | Files changed | Tests / gate | Acceptance criteria | Commit SHA |
| --- | --- | --- | --- | --- | --- | --- |
| 00 Repository audit | COMPLETE | Establish baseline, boundaries, licenses, architecture, and ledger | `docs/REPOSITORY_AUDIT.md`, `docs/ARCHITECTURE.md`, this ledger | libretro baseline build; SDL2 baseline build attempt; clean-tree inspection | Existing behavior documented; no functional change | `10a9e35` |
| 01 Root CMake | COMPLETE | Modern target-based build, presets, dependency discovery, trivial test | root CMake, `cmake/`, presets, test bootstrap | Debug/Release/ASan configure, build, CTest; libretro regression | Host configure succeeds; legacy builds retained | `99f4608` |
| 02 Core library | COMPLETE | Build core independently of SDL main and GUI | core target manifests, desktop OSD bridge | Debug/Release/ASan core link smoke; libretro regression | Static core compiles without Qt dependency | `df2985a` |
| 03 Synthetic ROM | COMPLETE | Generate legal deterministic test ROM and first headless core test | `tests/fixtures`, core test utilities | generated ROM execution and semantic assertion | Fixture provenance documented; repeatable result | `16b56c9` |
| 04 Core lifecycle | COMPLETE | Adapter init/shutdown/load/unload/reset/frame API | `desktop/core` | lifecycle, invalid transition, repeated load/unload | Deterministic and leak-safe lifecycle | `447bc21` |
| 05 Core video | COMPLETE | Safe framebuffer and dynamic viewport exposure | core/video adapter | viewport, frame content/hash tests | Complete immutable frame snapshots | `8c99cd6` |
| 06 Core audio | COMPLETE | Sample exposure and bounded audio storage | core/audio adapter | deterministic sample and ring-buffer tests | No overflow or unbounded allocation | `ac02374` |
| 07 Core input | COMPLETE | Neutral input snapshot translated at frame boundary | input model/adapter | controller ROM and mapping tests | Snapshot consumed deterministically | `4b5e5b5` |
| 08 Persistence | COMPLETE | Platform paths, safe names, SRAM/BRAM atomic files | `desktop/persistence` | path, collision, corruption, atomic round-trip | No current-directory/user-data leakage in tests | `1e4dc1e` |
| 09 Save states | COMPLETE | Metadata wrapper, slots, validation | state manager | round-trip, corruption, wrong-game, replacement | Raw payload preserved; unsafe states rejected | `0422e36` |
| 10 Qt shell | COMPLETE | QApplication, MainWindow, menus/status/canvas/About | `desktop/app`, `desktop/ui`, resources | offscreen startup and menu semantic tests | Native shell starts headlessly | `0312e53` |
| 11 Emulation worker | COMPLETE | Command queue, worker lifecycle, safe shutdown | worker/coordinator | concurrency, queue bounds, repeated start/stop | No core calls on GUI thread | `0a04aa1` |
| 12 Display widget | COMPLETE | Present synthetic/core frames and handle resize | video widget | integration and shown-frame tests | Stable reusable presentation path | `3ee78c1` |
| 13 Video scaling | COMPLETE | Native, 4:3, stretch, integer and filter modes | video geometry/settings | property/unit and GUI settings tests | Correct letterbox/high-DPI calculations | `71692a3` |
| 14 Audio playback | COMPLETE | SDL3 output, device lifecycle, pause/resume | `desktop/audio`, worker/app composition | dummy-device init, callback accounting, worker transfer | Clean bounded low-latency pipeline | `1fbc570` |
| 15 Timing/pacing | COMPLETE | NTSC/PAL/CD pacing, FF, pause, frame advance | `desktop/timing`, core timing metadata, worker scheduler | rational/property and live rate/state tests | Monotonic non-busy pacing | recorded by milestone 16 |
| 16 Keyboard controls | PLANNED | Excellent default Genesis keyboard mappings | input/UI integration | synthetic key-to-core workflow | 3/6-button controls work | pending |
| 17 Controllers | PLANNED | SDL3 discovery, hot-plug, assignments, mappings | controller service | injected SDL event tests | Multi-controller lifecycle is safe | pending |
| 18 Input UI | PLANNED | Capture, profiles, deadzones, conflicts, advanced devices | settings/input dialogs | GUI capture/conflict and persistence tests | Keyboard-accessible remapping works | pending |
| 19 Game loading UI | PLANNED | Open/close, drag/drop, CLI, safe errors | file/dialog services and MainWindow | valid/invalid/drop/CLI GUI integration | Different games load without restart | pending |
| 20 Recent games | PLANNED | Bounded persistent recents and clear menu | recent model/menu | model migration and GUI action tests | Missing paths handled gracefully | pending |
| 21 Live SRAM/BRAM | PLANNED | Connect persistence to core load/unload/exit | adapter/session | cartridge and CD unload/reload tests | Dirty saves flush before teardown | pending |
| 22 Save-state GUI | PLANNED | Slots 0-9, quick actions, timestamps/delete | state UI | GUI and full workflow tests | Wrong-game states never reach core | pending |
| 23 Video settings | PLANNED | Expose overscan, GG, NTSC, mode/region geometry | video settings UI/adapter | propagation and GUI persistence | Supported settings affect core/display | pending |
| 24 Audio settings | PLANNED | Levels, mono/filter/LPF/EQ/chip/HQ/device/latency | audio settings UI/adapter | validation/propagation/GUI tests | Supported options affect pipeline/core | pending |
| 25 System settings | PLANNED | Region, VDP, clock and safe system options | system settings UI/adapter | validation and reinit tests | Accurate defaults and controlled reinit | pending |
| 26 BIOS manager | PLANNED | Paths, existence, hashes, detected status | BIOS service/page | missing/valid/invalid generated fixtures | No download/bundled proprietary firmware | pending |
| 27 Sega CD workflow | PLANNED | Disc load/change/eject, BIOS, BRAM, CDDA/CHD | CD adapter/UI | synthetic frontend paths; optional external suite | First-class typed CD behavior | pending |
| 28 Game information | PLANNED | Safe header metadata and SHA-256 dialog | metadata parser/dialog | bounded parser/fuzz corpus and GUI test | Metadata never changes core behavior | pending |
| 29 Library database | PLANNED | SQLite schema, directories, async scanner | `desktop/library` | migration, corruption, recursive scan tests | Scanner cannot freeze GUI | pending |
| 30 Library UI | PLANNED | Search/filter/favorite/recent/sort/art/launch | library widgets/models | full GUI workflows | Useful offline local library | pending |
| 31 Screenshots | PLANNED | Native/display PNG with collision-safe paths | screenshot service/UI | deterministic PNG and action tests | Atomic image writing and notification | pending |
| 32 Cheats | PLANNED | GG/PAR validation, persistence and enable UI | `desktop/cheats`, dialog | parser property, persistence, GUI tests | Invalid codes never applied | pending |
| 33 Per-game overrides | PLANNED | Sparse overrides and precedence | settings model/UI | precedence, schema, GUI tests | No file until override exists | pending |
| 34 Theme/accessibility | PLANNED | System/light/dark, high-DPI and keyboard access | theme/accessibility services | persistence and navigation GUI tests | Critical controls keyboard accessible | pending |
| 35 Diagnostics/logging | PLANNED | Structured logs and copyable safe diagnostics | logging/diagnostics dialog | redaction/log creation/GUI tests | Useful bounded diagnostics, no secrets | pending |
| 36 GUI regression | PLANNED | Cover every significant menu/dialog workflow | `tests/gui` and seams | complete headless GUI suite | Behavior, not construction-only, asserted | pending |
| 37 Sanitizer/stress | PLANNED | ASan/UBSan, lifecycle and long-frame hardening | presets/tests | sanitizers and bounded stability test | No new-code sanitizer defects | pending |
| 38 Linux CI | PLANNED | Debug/Release/unit/core/integration/GUI on Ubuntu | GitHub workflow | local action/schema validation | Clean Linux matrix definition | pending |
| 39 Windows CI | PLANNED | MSVC x64 build and tests | GitHub workflow/platform fixes | matrix definition and cross-platform review | Clean Windows matrix definition | pending |
| 40 macOS CI | PLANNED | Apple Silicon and practical Intel build/tests | GitHub workflow/platform fixes | matrix definition and deployment review | Clean macOS matrix definition | pending |
| 41 Packaging | PLANNED | Windows ZIP, macOS app/ZIP or DMG, Linux portable artifact | CPack/deploy scripts | clean install/package smoke checks | Versioned architecture-named artifacts | pending |
| 42 Release automation | PLANNED | Tagged build/test/checksum/release workflow | release workflow | syntax and dry-path validation | No unauthorized tag/release created | pending |
| 43 User documentation | PLANNED | Complete build, test, usage, BIOS, input, save, release docs | README and required docs | link/command/content review | Every shipped UI feature documented | pending |
| 44 Release candidate | PLANNED | Adversarial review, complete clean regressions and report | fixes plus `FINAL_TEST_REPORT.md` | Debug, Release, all tests, sanitizers, packages, docs | Clean tree and all required gates green | pending |

## Execution policy

- Work continues on `master`, the existing clean development/default branch. A branch
  change is unnecessary unless repository policy changes.
- Each milestone uses one focused commit named `milestone NN: ...`. Closely related
  corrective commits are avoided by fixing the milestone before committing.
- Before a commit, ignored build output is inspected as well as tracked/untracked files.
- A failed required gate is recorded and fixed; it is never committed as a completed
  milestone.
- Pushes target `origin master` without force. Authentication or network failures are
  retried, then recorded while clean local commits continue.
- Tests use generated/public-domain fixtures and injected temporary application roots.
- CI-required Sega CD tests never depend on proprietary BIOS files. External-fixture
  tests are labeled and optional.
- Golden output changes require investigation and an explicit explanation; test code
  never regenerates expected values merely because a comparison failed.

## Milestone 00 detail

**Status:** COMPLETE

**Goal:** Inspect the untouched repository, establish build behavior, define the
core/frontend boundary, identify dependencies and licenses, and select a maintainable
architecture.

**Files changed:**

- `docs/REPOSITORY_AUDIT.md`
- `docs/ARCHITECTURE.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** None; this documentation-only milestone establishes the baseline.

**Gate evidence:**

- `make -f Makefile.libretro platform=unix -j4`: passed, then cleaned.
- `make -f Makefile.sdl2 -j4` from `sdl/`: failed only in the inherited SDL frontend
  on obsolete keypad constants, then cleaned; documented in the audit.
- `git status --short --branch`: clean before documentation edits.
- No functional source or legacy build file is changed.

**Acceptance criteria:** Existing behavior, boundaries, dependencies, licensing
constraints, architecture, risks, and subsequent gates are documented.

**Commit SHA:** `10a9e35`

## Milestone 01 detail

**Status:** COMPLETE

**Goal:** Add a modern root CMake project without replacing legacy platform builds,
discover the selected desktop dependencies, provide reproducible presets, establish
target-scoped frontend warnings/sanitizers, and register the first CTest target.

**Files changed:**

- `CMakeLists.txt`
- `CMakePresets.json`
- `.gitignore`
- `cmake/CompilerWarnings.cmake`
- `cmake/Sanitizers.cmake`
- `cmake/version.h.in`
- `tests/CMakeLists.txt`
- `tests/infrastructure/CMakeLists.txt`
- `tests/infrastructure/infrastructure_test.cpp`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `infrastructure.version` verifies configured version/application/Git
metadata and that new C++ targets compile as C++20.

**Gate evidence:**

- `cmake --preset debug`, build, and CTest: passed (1/1).
- `cmake --preset release`, build, and CTest: passed (1/1).
- `cmake --preset asan`, build, and CTest: passed under ASan/UBSan (1/1).
- `make -f Makefile.libretro platform=unix -j4`: passed with only the two documented
  inherited qualifier warnings, then cleaned.
- Qt 6.11.1 and SDL 3.4.14 were found through CMake config packages.

**Acceptance criteria:** The documented configure/build/test workflow succeeds; Debug,
Release, ASan, and CI presets exist; warnings apply to new code; legacy makefiles and
core sources are unchanged.

**Commit SHA:** `99f4608`

## Milestone 02 detail

**Status:** COMPLETE

**Goal:** Compile the full Genesis Plus GX emulator independently of every existing
frontend, supply a narrowly scoped desktop host ABI, and prove the resulting archive
links without Qt or SDL.

**Files changed:**

- `CMakeLists.txt`
- `desktop/core/CMakeLists.txt`
- `desktop/core/c_api/config.h`
- `desktop/core/c_api/osd.h`
- `desktop/core/c_api/desktop_core_host.h`
- `desktop/core/c_api/desktop_core_host.c`
- `tests/CMakeLists.txt`
- `tests/core/CMakeLists.txt`
- `tests/core/core_link_test.cpp`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `core.library_link` validates the desktop defaults and forces both
system/audio and ROM metadata objects through a final executable link.

**Gate evidence:**

- Debug build and CTest: passed (2/2).
- Release build and CTest: passed (2/2).
- ASan/UBSan preset build and CTest: passed (2/2).
- `make -f Makefile.libretro platform=unix -j4`: passed with the documented inherited
  warnings, then cleaned.
- `git diff -- core`: empty; no authoritative core source was modified.

**Acceptance criteria:** `genplusgx_core` contains the full CPU, VDP, audio, cartridge,
input, Sega CD, Tremor, and optional CHD decoder source set. It exposes no Qt/SDL
dependency. Desktop compile definitions are target-scoped. The CHD target deliberately
uses the decoder-only bundled source manifest because the fork does not vendor zstd's
compression source subset; it never invokes dependency CMake that mutates source files.

**Commit SHA:** `df2985a`

## Milestone 03 detail

**Status:** COMPLETE

**Goal:** Generate an original, redistributable Genesis ROM at test runtime and prove
that the isolated core performs a real reset, executes deterministic 68000 code, parses
its header, completes a frame, and exposes semantic machine state.

**Files changed:**

- `desktop/core/CMakeLists.txt`
- `tests/CMakeLists.txt`
- `tests/core/CMakeLists.txt`
- `tests/core/synthetic_rom_test.cpp`
- `tests/utilities/CMakeLists.txt`
- `tests/utilities/synthetic_rom.h`
- `tests/utilities/synthetic_rom.cpp`
- `tests/fixtures/README.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `core.synthetic_genesis_rom` generates a 64 KiB Genesis image in a
temporary directory, loads it through the real core loader, initializes audio and the
machine, runs a frame, and verifies its system type, normalized domestic title, known
RAM long/word/byte markers, and bounded 68000 program counter.

**Gate evidence:**

- Debug build and complete CTest: passed (3/3).
- The synthetic-ROM test passed three consecutive Debug executions.
- Release build and complete CTest: passed (3/3).
- ASan/UBSan preset build and complete CTest: passed (3/3).
- `make -f Makefile.libretro platform=unix -j4`: passed with only the two documented
  inherited qualifier warnings, then cleaned.
- `tests/fixtures/README.md` records generation, behavior, and CC0 provenance; no ROM
  binary is stored in the repository.

**Acceptance criteria:** The runtime-generated image executes deterministic original
68000 instructions and proves behavior through semantic RAM state rather than a
self-regenerated golden file. Temporary files are uniquely named and removed after
each test. Authoritative core headers are treated as external system headers for new
C++ warning policy without weakening warnings on project code.

**Commit SHA:** `16b56c9`

## Milestone 04 detail

**Status:** COMPLETE

**Goal:** Put core initialization, ownership, game load/unload, reset, frame execution,
and shutdown behind a reusable C++ lifecycle boundary that cannot be called from an
arbitrary GUI thread.

**Files changed:**

- `desktop/core/CMakeLists.txt`
- `desktop/core/include/genplusgx/core_adapter.h`
- `desktop/core/src/core_adapter.cpp`
- `tests/core/CMakeLists.txt`
- `tests/core/core_lifecycle_test.cpp`
- `docs/ARCHITECTURE.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `core.lifecycle` verifies uninitialized/ready/loaded transitions,
invalid operations, rejected missing and empty files, one active adapter, repeated
initialization, hard reset semantics, frame counts, hardware/path exposure, owner-thread
enforcement, 25 complete load/frame/unload cycles, ownership transfer after explicit
shutdown, automatic RAII teardown, and audio-rate validation.

**Gate evidence:**

- Debug build and complete CTest: passed (4/4).
- The lifecycle test passed three consecutive Debug executions.
- Release build and complete CTest: passed (4/4).
- ASan/UBSan preset build and complete CTest: passed (4/4).
- `make -f Makefile.libretro platform=unix -j4`: passed with only the two documented
  inherited qualifier warnings, then cleaned.
- New adapter code compiled with the project warning policy without warnings.

**Acceptance criteria:** Exactly one adapter owns the process-global core context. Core
entry is serialized and rejected from non-owner threads. All failed lifecycle calls
return typed errors, failed loads leave a clean ready state, unload/shutdown are
idempotent, core audio/CD/cheat resources are released, and framebuffer/core metadata
cannot outlive ownership.

**Commit SHA:** `447bc21`

## Milestone 05 detail

**Status:** COMPLETE

**Goal:** Expose the core's dynamic RGB565 output without leaking its mutable framebuffer
or allocating a framebuffer-sized object during normal frame publication.

**Files changed:**

- `desktop/core/include/genplusgx/core_adapter.h`
- `desktop/core/src/core_adapter.cpp`
- `tests/core/CMakeLists.txt`
- `tests/core/core_video_test.cpp`
- `tests/utilities/synthetic_rom.cpp`
- `tests/fixtures/README.md`
- `docs/ARCHITECTURE.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `core.video_frame` verifies the reset viewport, maximum reusable surface,
undersized-copy rejection, a real VDP-driven 256x192 to 320x224 viewport transition,
frame numbering, progressive metadata, successful notification acknowledgement,
caller-owned tight RGB565 copies, reset pixel determinism, semantic backdrop color, and
the investigated FNV-1a frame regression hash `0x0cfd2d0b9af92325`.

**Gate evidence:**

- Debug build and complete CTest: passed (5/5).
- The video-frame test passed three consecutive Debug executions.
- Release build and complete CTest: passed (5/5).
- ASan/UBSan preset build and complete CTest: passed (5/5).
- `make -f Makefile.libretro platform=unix -j4`: passed with only the two documented
  inherited qualifier warnings, then cleaned.
- The locked hash represents 320x224 pixels of RGB565 `0xEF7D`, the core's expansion of
  the Genesis VDP's maximum native 3-bit channel intensity; it is not a blank frame.

**Acceptance criteria:** Mutable core video memory never crosses the public boundary.
Metadata validates native crop/overscan, NTSC expansion, and interlaced-height cases.
Copying requires bounded caller-owned storage, produces a complete immutable snapshot,
and consumes the viewport-change notification only after success. The generated legal
ROM drives a real VDP geometry and pixel-output regression.

**Commit SHA:** `8c99cd6`

## Milestone 06 detail

**Status:** COMPLETE

**Goal:** Drain deterministic stereo samples from every emulated frame into a bounded
adapter batch and provide a lock-free SPSC ring suitable for the later SDL3 host audio
device.

**Files changed:**

- `CMakeLists.txt`
- `desktop/core/CMakeLists.txt`
- `desktop/core/include/genplusgx/audio_frame.h`
- `desktop/core/include/genplusgx/core_adapter.h`
- `desktop/core/src/core_adapter.cpp`
- `desktop/audio/CMakeLists.txt`
- `desktop/audio/include/genplusgx/audio_ring_buffer.h`
- `desktop/audio/src/audio_ring_buffer.cpp`
- `tests/CMakeLists.txt`
- `tests/core/CMakeLists.txt`
- `tests/core/core_audio_test.cpp`
- `tests/unit/CMakeLists.txt`
- `tests/unit/audio_ring_buffer_test.cpp`
- `tests/utilities/synthetic_rom.cpp`
- `tests/fixtures/README.md`
- `docs/ARCHITECTURE.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `core.audio_samples` verifies no pre-frame audio, format/count/frame
metadata, undersized-copy retention, non-silent centered PSG output, consume-on-success,
hard-reset sample equality, the investigated stereo FNV-1a regression hash
`0x3fdb01d7287ae391`, and bounded pending-batch overwrite instrumentation.
`unit.audio_ring_buffer` verifies construction validation, capacity/occupancy, stereo
order, wraparound, preserve-oldest overrun behavior, partial underrun behavior, metrics,
clear/reset, and a 20,000-frame concurrent SPSC transfer without corruption or drops.

**Gate evidence:**

- Debug build and complete CTest: passed (7/7).
- Core audio and ring-buffer tests each passed three consecutive Debug executions.
- Release build and complete CTest: passed (7/7).
- ASan/UBSan preset build and complete CTest: passed (7/7).
- `make -f Makefile.libretro platform=unix -j4`: passed with only the two documented
  inherited qualifier warnings, then cleaned.
- New core-adapter and audio-ring code compiled with the project warning policy without
  warnings.

**Acceptance criteria:** `runFrame()` always drains the core into fixed 4,096-frame
scratch storage. Copies use whole typed stereo frames, preserve pending data when the
destination is too small, and expose dropped-batch counters. The SPSC host ring has a
fixed construction-time capacity, never allocates in producer/consumer calls, uses
acquire/release publication, and instruments every bounded loss/short read. The legal
generated ROM produces stable real PSG samples rather than a silence-only hash.

**Commit SHA:** `ac02374`

## Milestone 07 detail

**Status:** COMPLETE

**Goal:** Define a keyboard/controller-independent player snapshot and translate only
the newest valid state into Genesis Plus GX device slots at the start of an emulated
frame.

**Files changed:**

- `desktop/core/c_api/desktop_core_host.c`
- `desktop/core/include/genplusgx/input_snapshot.h`
- `desktop/core/include/genplusgx/core_adapter.h`
- `desktop/core/src/core_adapter.cpp`
- `tests/core/CMakeLists.txt`
- `tests/core/core_input_test.cpp`
- `tests/unit/CMakeLists.txt`
- `tests/unit/input_snapshot_test.cpp`
- `tests/utilities/synthetic_rom.cpp`
- `tests/fixtures/README.md`
- `docs/ARCHITECTURE.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `unit.input_snapshot` verifies twelve unique logical buttons,
composition/membership, eight-player capacity, and neutral disconnected defaults.
`core.input_snapshot` verifies two default gamepads, queue-versus-frame-boundary
semantics, released input, all twelve exact core mappings, signed analog values,
logical player-two routing to core slot four, disconnected clearing, applied sequences,
stale-sequence rejection, and newest-state coalescing. The generated 68000 loop also
proves active-low port bytes `0x7f` (released) and `0x67` (B+Right).

**Gate evidence:**

- Debug build and complete CTest: passed (9/9).
- Core input and logical-snapshot tests each passed three consecutive Debug executions.
- Release build and complete CTest: passed (9/9).
- ASan/UBSan preset build and complete CTest: passed (9/9).
- `make -f Makefile.libretro platform=unix -j4`: passed with only the two documented
  inherited qualifier warnings, then cleaned.
- New C/C++ host, model, adapter, generator, and test code compiled without warnings.

**Acceptance criteria:** Frontend snapshots contain no core constants. They cannot
mutate input globals between frames, and an older command cannot replace a newer one.
At the boundary, players map in order to active core devices, all unused/disconnected
slots are zeroed, digital and analog state is translated, and the emulated program
observes the result through the real Genesis controller I/O path.

**Commit SHA:** `4b5e5b5`

## Milestone 08 detail

**Status:** COMPLETE

**Goal:** Replace current-directory save conventions with absolute platform application
paths, collision-resistant per-game identities, bounded reads, and atomic cartridge/CD
RAM files.

**Files changed:**

- `CMakeLists.txt`
- `desktop/persistence/CMakeLists.txt`
- `desktop/persistence/include/genplusgx/persistence.h`
- `desktop/persistence/src/persistence.cpp`
- `tests/unit/CMakeLists.txt`
- `tests/unit/persistence_test.cpp`
- `docs/ARCHITECTURE.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `unit.persistence` uses only `QTemporaryDir` and verifies hierarchy
creation, separator/traversal/device-name/length sanitization, deterministic SHA-256,
changed-content identity, full-hash directory collision avoidance, cartridge SRAM, CD
internal BRAM, CD RAM cartridge, missing-file behavior, atomic replacement, rejected
oversized writes preserving prior data, bounded-load corruption rejection, non-file
corruption, invalid/traversal identity rejection, blocked roots, and rejection of
current-directory-relative roots.

**Gate evidence:**

- Debug build and complete CTest: passed (10/10).
- Persistence tests passed three consecutive Debug executions.
- Release build and complete CTest: passed (10/10).
- ASan/UBSan preset build and complete CTest: passed (10/10).
- `make -f Makefile.libretro platform=unix -j4`: passed with only the two documented
  inherited qualifier warnings, then cleaned.
- New persistence code compiled under the frontend warning policy without warnings.

**Acceptance criteria:** Production roots come from Qt's platform standard and must be
absolute; tests cannot touch real application data. Filenames are portable and cannot
traverse. Per-game directories include the full SHA-256. All three RAM forms use stable
distinct names, at most 8 MiB is accepted, reads validate before allocating, and Qt
atomic transactions never fall back to truncating direct writes.

**Commit SHA:** `1e4dc1e`

## Milestone 09 detail

**Status:** COMPLETE

**Goal:** Preserve Genesis Plus GX's raw snapshot payload while adding atomic slots,
portable frontend metadata, content identity, bounded parsing, and safe core-load gates.

**Files changed:**

- `desktop/core/include/genplusgx/core_adapter.h`
- `desktop/core/src/core_adapter.cpp`
- `desktop/persistence/CMakeLists.txt`
- `desktop/persistence/include/genplusgx/persistence.h`
- `desktop/persistence/include/genplusgx/state_manager.h`
- `desktop/persistence/src/persistence.cpp`
- `desktop/persistence/src/state_manager.cpp`
- `tests/core/CMakeLists.txt`
- `tests/core/core_state_test.cpp`
- `tests/unit/CMakeLists.txt`
- `tests/unit/state_manager_test.cpp`
- `docs/ARCHITECTURE.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `unit.state_manager` verifies slots 0-9, fixed metadata, raw-payload
preservation, atomic replacement, missing/delete behavior, invalid slot and signature,
bounded payloads, wrong-game and wrong-system rejection, payload checksums, unsupported
schemas, and truncated envelopes. `core.state_round_trip` saves a real generated-ROM
machine state, proves RAM/register/program-counter restoration, rejects a truncated raw
state before the lengthless core loader, transactionally rolls back a core-rejected
snapshot, and proves deterministic execution after two restores of the same payload.

**Gate evidence:**

- Debug build and complete CTest: passed (12/12).
- State manager and core state tests each passed three consecutive Debug executions.
- Release build and complete CTest: passed (12/12).
- ASan/UBSan preset build and complete CTest: passed (12/12).
- `make -f Makefile.libretro platform=unix -j4`: passed with only the two documented
  inherited qualifier warnings, then cleaned.
- New adapter, serialization, persistence, and test code compiled under the frontend
  warning policy without warnings.

**Acceptance criteria:** Every wrapper is a bounded 128-byte little-endian header plus
the byte-for-byte raw core payload. Its schema, slot, hardware, timestamp, frame number,
full game SHA-256, payload SHA-256, payload length, and core signature are validated
before core entry. Slots replace atomically and a failed core load restores the prior
machine state. Raw loads must exactly match the active hardware's core-generated state
size, preventing truncated buffers from reaching `state_load()`.

**Commit SHA:** `0422e36`

## Milestone 10 detail

**Status:** COMPLETE

**Goal:** Establish the installable Qt Widgets process and a conventional, accessible
main-window shell whose stable identities support behavior-driven headless tests.

**Files changed:**

- `CMakeLists.txt`
- `desktop/app/CMakeLists.txt`
- `desktop/app/main.cpp`
- `desktop/ui/CMakeLists.txt`
- `desktop/ui/include/genplusgx/ui/about_dialog.h`
- `desktop/ui/include/genplusgx/ui/main_window.h`
- `desktop/ui/src/about_dialog.cpp`
- `desktop/ui/src/main_window.cpp`
- `tests/CMakeLists.txt`
- `tests/gui/CMakeLists.txt`
- `tests/gui/main_window_test.cpp`
- `docs/ARCHITECTURE.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `gui.main_window` constructs and shows the real shell on Qt's offscreen
platform, verifies its title/size/drop/accessibility contract, central canvas, all seven
top-level menus and six structural submenus, stable action identifiers, no-game enabled
state, checkable actions, five status fields, asynchronous About dialog build/license
identity, and functional Exit action. `gui.desktop_help` and `gui.desktop_version` run
the packaged executable entry point and verify its command-line identity without
entering the event loop.

**Gate evidence:**

- Debug build and complete CTest: passed (15/15).
- Main-window GUI test passed three consecutive offscreen executions.
- Release build and complete CTest: passed (15/15).
- ASan/UBSan preset build and complete CTest: passed (15/15).
- `make -f Makefile.libretro platform=unix -j4`: passed with only the two documented
  inherited qualifier warnings, then cleaned.
- New app, UI, and GUI-test code compiled under the frontend warning policy without
  warnings.

**Acceptance criteria:** `genesis-plus-gx-gui` starts as a native Qt Widgets application
with platform identity/version metadata, a black accessible display surface, complete
desktop menu hierarchy, informative empty-state status bar, and nonblocking About dialog.
Every significant shell object has a stable `objectName`; tests require no human dialogs
or real display server. The install target is a GUI executable/app bundle as appropriate
for the host platform.

**Commit SHA:** `0312e53`

## Milestone 11 detail

**Status:** COMPLETE

**Goal:** Move all active core lifecycle and frame execution behind a restartable worker
with bounded command/event transport, explicit state transitions, and synchronous clean
shutdown.

**Files changed:**

- `desktop/core/CMakeLists.txt`
- `desktop/core/include/genplusgx/bounded_queue.h`
- `desktop/core/include/genplusgx/core_adapter.h`
- `desktop/core/include/genplusgx/emulation_worker.h`
- `desktop/core/src/core_adapter.cpp`
- `desktop/core/src/emulation_worker.cpp`
- `tests/core/CMakeLists.txt`
- `tests/core/emulation_worker_test.cpp`
- `tests/unit/CMakeLists.txt`
- `tests/unit/bounded_queue_test.cpp`
- `docs/ARCHITECTURE.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `unit.bounded_queue` verifies rejected zero capacity, fixed-depth
backpressure, FIFO order, newest-match coalescing, bounded drop-oldest reporting, pop,
and clear. `core.emulation_worker` proves the core owner differs from its caller; checks
stopped/idle/paused/running transitions and typed asynchronous failures; exercises real
load, run, pause, resume, frame advance, fast-forward, input boundary application, hard
and soft reset, raw state capture/restore, unload, corrupt replacement-load recovery,
frame-event coalescing, synchronous stop, ten complete worker restarts, RAII shutdown,
and final release of the global core lease.

**Gate evidence:**

- Debug build and complete CTest: passed (17/17).
- The complete worker workflow passed ten consecutive Debug executions.
- Release build and complete CTest: passed (17/17).
- ASan/UBSan preset build and complete CTest: passed (17/17).
- `make -f Makefile.libretro platform=unix -j4`: passed with only the two documented
  inherited qualifier warnings, then cleaned.
- New protocol, worker, queue, and test code compiled under the frontend warning policy
  without warnings.

**Acceptance criteria:** Only the worker thread constructs and owns `CoreAdapter`.
Commands carry nonzero operation IDs through a fixed-capacity FIFO; pending input updates
coalesce and queue saturation returns synchronously. Operation events remain bounded and
report drops, while frame completion has exactly one replaceable newest slot. Running
uses interruptible condition-variable deadlines instead of GUI timers or busy waiting.
Stop rejects new commands, interrupts waits, shuts down the core on its owner thread,
joins synchronously, and is idempotent. A failed game replacement cannot leave the
worker claiming a loaded/running state.

**Commit SHA:** `0a04aa1`

## Milestone 12 detail

**Status:** COMPLETE

**Goal:** Carry each complete core RGB565 frame through bounded reusable storage and
present it in the native Qt shell with stable nearest-neighbor resize behavior.

**Files changed:**

- `CMakeLists.txt`
- `desktop/app/CMakeLists.txt`
- `desktop/app/main.cpp`
- `desktop/core/CMakeLists.txt`
- `desktop/core/include/genplusgx/emulation_worker.h`
- `desktop/core/src/emulation_worker.cpp`
- `desktop/ui/CMakeLists.txt`
- `desktop/ui/include/genplusgx/ui/main_window.h`
- `desktop/ui/src/main_window.cpp`
- `desktop/video/CMakeLists.txt`
- `desktop/video/include/genplusgx/video/display_widget.h`
- `desktop/video/include/genplusgx/video/frame_exchange.h`
- `desktop/video/src/display_widget.cpp`
- `desktop/video/src/frame_exchange.cpp`
- `tests/core/emulation_worker_test.cpp`
- `tests/gui/CMakeLists.txt`
- `tests/gui/display_widget_test.cpp`
- `tests/unit/CMakeLists.txt`
- `tests/unit/frame_exchange_test.cpp`
- `docs/ARCHITECTURE.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `unit.frame_exchange` verifies construction bounds, exactly three fixed
pixel surfaces, empty/undersized errors, producer lease publication/cancellation,
complete pixel/metadata copies, newest-frame selection, skipped-frame metrics, invalid
geometry, and clear. The worker integration now hashes a real 320x224 generated-ROM
frame after it crosses the exchange and matches `0x0cfd2d0b9af92325`.
`gui.display_widget` publishes a controlled red/green RGB565 frame, presents it through
the production widget on Qt offscreen, asserts metadata/pixels and empty-state behavior,
renders the widget to an image, and semantically verifies nearest boundaries, aspect
fit, black borders, and stable portrait/landscape resize.

**Gate evidence:**

- Debug build and complete CTest: passed (19/19).
- Exchange, worker video integration, and display tests each passed five consecutive
  Debug executions.
- Release build and complete CTest: passed (19/19).
- ASan/UBSan preset build and complete CTest: passed (19/19).
- `make -f Makefile.libretro platform=unix -j4`: passed with only the two documented
  inherited qualifier warnings, then cleaned.
- New frame exchange, display, integration, and test code compiled under the frontend
  warning policy without warnings.

**Acceptance criteria:** The worker copies directly from `CoreAdapter` into a leased
exchange slot, with no framebuffer-sized per-frame allocation or intermediate worker
copy. A producer never overwrites the published slot or a slot being read. Three slots,
one coalesced frame event, explicit producer-drop/skipped-frame instrumentation, and a
preallocated GUI receive surface bound memory. The main process starts the worker,
polls only its bounded events from an 8 ms GUI timer, repaints the newest frame, and
stops/joins the worker after the Qt event loop; the timer never drives emulation.

**Commit SHA:** `3ee78c1`

## Milestone 13 detail

**Status:** COMPLETE

**Goal:** Provide mathematically bounded scaling policies, selectable filtering, real
fullscreen behavior, and a persistent GPU texture path with deterministic fallback.

**Files changed:**

- `desktop/video/CMakeLists.txt`
- `desktop/video/include/genplusgx/video/display_widget.h`
- `desktop/video/include/genplusgx/video/video_geometry.h`
- `desktop/video/src/display_widget.cpp`
- `desktop/video/src/video_geometry.cpp`
- `desktop/ui/src/main_window.cpp`
- `tests/gui/display_widget_test.cpp`
- `tests/gui/main_window_test.cpp`
- `tests/unit/CMakeLists.txt`
- `tests/unit/video_geometry_test.cpp`
- `docs/ARCHITECTURE.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `unit.video_geometry` checks invalid dimensions, exact native/4:3/
stretch/whole-integer results, fit fallback below 1x, centering, and 1,280 combinations
of source geometry, destination geometry, aspect, and scale policy for containment and
integer invariants. GUI tests exercise every live menu group, exclusive defaults,
nearest/bilinear propagation, fit/integer/native/4:3/stretch layout updates, semantic
render boundaries in landscape and portrait, offscreen software selection, and checked
fullscreen entry/exit.

**Gate evidence:**

- Debug build and complete CTest: passed (20/20).
- Geometry, display, and main-window policy tests each passed five consecutive Debug
  executions.
- Release build and complete CTest: passed (20/20).
- ASan/UBSan preset build and complete CTest: passed (20/20).
- `make -f Makefile.libretro platform=unix -j4`: passed with only the two documented
  inherited qualifier warnings, then cleaned.
- New geometry, OpenGL, menu, and test code compiled under the frontend warning policy
  without warnings.

**Acceptance criteria:** Pure geometry always returns a contained centered rectangle,
preserves native or forced 4:3 aspect, fills only in explicit stretch mode, and uses
whole native pixel multiples whenever at least 1x fits. Production window-system
platforms use `QOpenGLWidget`, a reusable RGB565 texture, and subimage uploads; nearest
and bilinear select hardware texture sampling. Shader/context/VAO/texture failure
asynchronously falls back to the same deterministic software renderer used by offscreen
tests and by `GENPLUSGX_FORCE_SOFTWARE_VIDEO`. Viewports convert logical coordinates to
device pixels for high-DPI/Retina surfaces. No GUI timer performs scaling or emulation.

**Commit SHA:** `71692a3`

## Milestone 14 detail

**Status:** COMPLETE

**Goal:** Play the core's stereo S16 output through a lifecycle-safe SDL3 host stream
without coupling device callbacks to either Qt or the Genesis Plus GX global state.

**Files changed:**

- `CMakeLists.txt`
- `desktop/audio/CMakeLists.txt`
- `desktop/audio/include/genplusgx/audio_output.h`
- `desktop/audio/src/audio_output.cpp`
- `desktop/core/CMakeLists.txt`
- `desktop/core/include/genplusgx/emulation_worker.h`
- `desktop/core/src/emulation_worker.cpp`
- `desktop/app/CMakeLists.txt`
- `desktop/app/main.cpp`
- `tests/core/emulation_worker_test.cpp`
- `tests/unit/CMakeLists.txt`
- `tests/unit/audio_output_test.cpp`
- `docs/ARCHITECTURE.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `unit.audio_output` runs SDL3's real device-stream lifecycle against
the deterministic dummy backend. It verifies configuration bounds, latency-derived
ring capacity, typed pre-init/repeated-init failures, initial paused state, device
identity, callback-driven consumption, exact supplied/silence accounting, submission
instrumentation, repeated pause/resume, queue clearing, and idempotent shutdown. The
worker test now requires generated core audio to reach its shared bounded ring.

**Gate evidence:**

- Debug build and complete CTest: passed (21/21).
- Audio output, ring, and worker integration tests: passed.
- Audio output, ring, and worker integration tests each passed five consecutive Debug
  executions.
- Release build and complete CTest: passed (21/21).
- ASan/UBSan preset build and complete CTest: passed (21/21).
- `make -f Makefile.libretro platform=unix -j4`: passed with only the two documented
  inherited qualifier warnings, then cleaned.
- New audio service, integration, and test code compiled under the frontend warning
  policy without warnings.

**Acceptance criteria:** The application initializes SDL3 audio independently and
continues with a warning if no device is available. The emulation thread drains each
complete core batch into a shared SPSC ring using one fixed 4,096-frame transfer
surface. The SDL callback uses one fixed 1,024-frame scratch surface, reads only whole
stereo frames, supplies silence on underrun, and adds only SDL's current demand to its
stream. Default 80 ms latency produces a bounded 3,840-frame ring. Device streams begin
paused, track callback/ring underruns and overruns, clear stale data on pause, and close
before the audio subsystem. Application shutdown stops and joins emulation before
closing audio.

**Commit SHA:** `1fbc570`

## Milestone 15 detail

**Status:** COMPLETE

**Goal:** Schedule frames from the core's authoritative rational NTSC/PAL cadence with
interruptible monotonic waits, bounded late-frame recovery, pause/frame-advance
semantics, and a controlled fast-forward rate.

**Files changed:**

- `CMakeLists.txt`
- `desktop/timing/CMakeLists.txt`
- `desktop/timing/include/genplusgx/timing/frame_pacer.h`
- `desktop/timing/src/frame_pacer.cpp`
- `desktop/core/CMakeLists.txt`
- `desktop/core/include/genplusgx/core_adapter.h`
- `desktop/core/include/genplusgx/emulation_worker.h`
- `desktop/core/src/core_adapter.cpp`
- `desktop/core/src/emulation_worker.cpp`
- `tests/core/CMakeLists.txt`
- `tests/core/core_lifecycle_test.cpp`
- `tests/core/timing_pacing_test.cpp`
- `tests/unit/CMakeLists.txt`
- `tests/unit/frame_pacer_test.cpp`
- `docs/ARCHITECTURE.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `unit.frame_pacer` validates input/overflow bounds, exact NTSC and PAL
rates, 600-frame nanosecond remainder accumulation, pause, resume, a rational 4x mode,
late-frame instrumentation, single-interval catch-up limits, and metric reset. Core
lifecycle tests verify generated NTSC and PAL headers expose the expected master clocks,
line counts, and cadence. `core.timing_pacing` measures live worker behavior: 30 normal
frames remain within rate tolerance, pause schedules none, frame advance does not restart
continuous pacing, and 40 fast frames materially accelerate without an unbounded loop.

**Gate evidence:**

- Initial focused tests and five consecutive unit/live timing runs: passed.
- Debug build and complete CTest: passed (23/23).
- Release build and complete CTest: passed (23/23).
- ASan/UBSan preset build and complete CTest: passed (23/23).
- `make -f Makefile.libretro platform=unix -j4`: passed with only the two documented
  inherited qualifier warnings, then cleaned.
- New scheduler, integration, and test code compiled under the frontend warning policy
  without warnings.

**Acceptance criteria:** `CoreAdapter` reports the same master clock, lines-per-frame,
and 3,420 master cycles per line used by the upstream libretro timing calculation. The
frontend retains the ratio rather than rounding it to a millisecond/microsecond timer.
`FramePacer` distributes fractional nanoseconds across deadlines, waits through the
worker's interruptible condition variable, permits at most a bounded catch-up before
resynchronizing, and exposes target rate, late frames, maximum lateness, and resyncs.
Pause removes the deadline, resume begins immediately from a fresh monotonic origin,
frame advance executes without enabling the scheduler, and fast-forward is exactly 4x.
Fast-forward still drains the core batch but does not enqueue impossible real-time host
audio; the application pauses and clears the SDL stream until normal speed resumes.
Sega CD automatically uses its loaded region's VDP cadence while its core sub-clock
remains synchronized by Genesis Plus GX.

**Commit SHA:** recorded by milestone 16
