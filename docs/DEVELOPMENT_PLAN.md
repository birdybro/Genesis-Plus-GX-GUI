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
| 15 Timing/pacing | COMPLETE | NTSC/PAL/CD pacing, FF, pause, frame advance | `desktop/timing`, core timing metadata, worker scheduler | rational/property and live rate/state tests | Monotonic non-busy pacing | `3235274` |
| 16 Keyboard controls | COMPLETE | Excellent default Genesis keyboard mappings | `desktop/input`, display/app integration | mapping, event-filter, focus, worker/core pipeline tests | 3/6-button controls work | `48d2844` |
| 17 Controllers | COMPLETE | SDL3 discovery, hot-plug, assignments, mappings | controller service and source aggregator | virtual devices and injected SDL event tests | Multi-controller lifecycle is safe | `58b908d` |
| 18 Input UI | COMPLETE | Capture, profiles, deadzones, conflicts, advanced devices | profile store, runtime mappings, input dialogs | GUI capture/conflict and persistence tests | Keyboard-accessible remapping works | `87c13b8` |
| 19 Game loading UI | COMPLETE | Open/close, drag/drop, CLI, safe errors | file/dialog services and MainWindow | valid/invalid/drop/CLI GUI integration | Different games load without restart | `568c811` |
| 20 Recent games | COMPLETE | Bounded persistent recents and clear menu | recent model/menu | model migration and GUI action tests | Missing paths handled gracefully | `8615cac` |
| 21 Live SRAM/BRAM | COMPLETE | Connect persistence to core load/unload/exit | adapter/session | cartridge and CD store mapping; live unload/reload tests | Dirty saves flush before teardown | `6e50cac` |
| 22 Save-state GUI | COMPLETE | Slots 0-9, quick actions, timestamps/delete | state UI | GUI and full workflow tests | Wrong-game states never reach core | `c8a3a2a` |
| 23 Video settings | COMPLETE | Expose presentation, overscan, GG, NTSC, and interlaced rendering | settings model/dialog/adapter | schema, propagation, geometry, and GUI tests | Supported settings affect core/display | `d3f5a69` |
| 24 Audio settings | COMPLETE | Levels, mono/filter/LPF/EQ/chip/HQ/device/latency | audio settings UI/adapter | validation/propagation/GUI tests | Supported options affect pipeline/core | `56eeff4` |
| 25 System settings | COMPLETE | Region, VDP, clock and safe system options | system settings UI/adapter | validation and deferred-load tests | Accurate defaults and controlled reinit | `97db779` |
| 26 BIOS manager | COMPLETE | Paths, existence, hashes, detected status | BIOS service/page | missing/valid/invalid generated fixtures | No download/bundled proprietary firmware | `77a562d` |
| 27 Sega CD workflow | COMPLETE | Disc load/change/eject, BIOS, BRAM, CDDA/CHD | CD adapter/UI | synthetic frontend paths; optional external suite | First-class typed CD behavior | `1baef5d` |
| 28 Game information | COMPLETE | Safe header metadata and SHA-256 dialog | metadata parser/dialog | bounded parser/fuzz corpus and GUI test | Metadata never changes core behavior | `c9e0c03` |
| 29 Library database | COMPLETE | SQLite schema, directories, async scanner | `desktop/library` | migration, corruption, recursive scan tests | Scanner cannot freeze GUI | `58d46df` |
| 30 Library UI | COMPLETE | Search/filter/favorite/recent/sort/art/launch | library widgets/models | full GUI workflows | Useful offline local library | `6667162` |
| 31 Screenshots | COMPLETE | Native PNG with collision-safe paths | screenshot service/UI | deterministic PNG and action tests | Atomic image writing and notification | `7f04063` |
| 32 Cheats | COMPLETE | GG/PAR validation, persistence and enable UI | `desktop/cheats`, core host bridge, dialog | parser property, persistence, core and GUI tests | Invalid codes never applied | `7756e2b` |
| 33 Per-game overrides | COMPLETE | Sparse overrides and precedence | settings model/UI/composition | precedence, schema, ordered-load and GUI tests | No file until override exists | `cf3e7ac` |
| 34 Theme/accessibility | COMPLETE | System/light/dark, high-DPI and keyboard access | appearance store/controller/dialog | persistence, palette and navigation GUI tests | Critical controls keyboard accessible | `824adf5` |
| 35 Diagnostics/logging | COMPLETE | Structured logs and copyable safe diagnostics | bounded logger/snapshot/dialog | rotation, redaction, JSONL and GUI copy tests | Useful bounded diagnostics, no secrets | `88313f1` |
| 36 GUI regression | COMPLETE | Cover every significant menu/dialog workflow | `tests/gui`, typed emulation controls, embedded help, test matrix | complete headless GUI suite | Behavior, not construction-only, asserted | `3aa0f41` |
| 37 Sanitizer/stress | COMPLETE | ASan/UBSan, lifecycle and long-frame hardening | resource metrics, accelerated stability workload, testing guide | sanitizers and bounded stability test | No new-code sanitizer defects | `34e709f` |
| 38 Linux CI | COMPLETE | Debug/Release/unit/core/integration/GUI on Ubuntu | GitHub workflow | local action/schema validation and hosted run | Clean Linux matrix definition | `013af97` |
| 39 Windows CI | COMPLETE | MSVC x64 Debug/Release build and tests | GitHub workflow/platform review | action/schema validation and hosted run | Clean Windows matrix definition | `043a2ee` |
| 40 macOS CI | IN PROGRESS | Apple Silicon and Intel Debug/Release build/tests | GitHub workflow/platform review | action/schema validation and hosted run | Clean native macOS matrix | pending |
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

**Commit SHA:** `3235274`

## Milestone 16 detail

**Status:** COMPLETE

**Goal:** Provide responsive, safe Player 1 keyboard controls for Genesis three- and
six-button layouts and deliver their logical state through the existing bounded worker
protocol at frame boundaries.

**Files changed:**

- `CMakeLists.txt`
- `desktop/input/CMakeLists.txt`
- `desktop/input/include/genplusgx/input/keyboard_input.h`
- `desktop/input/src/keyboard_input.cpp`
- `desktop/app/CMakeLists.txt`
- `desktop/app/main.cpp`
- `desktop/video/src/display_widget.cpp`
- `tests/gui/CMakeLists.txt`
- `tests/gui/keyboard_input_test.cpp`
- `docs/ARCHITECTURE.md`
- `docs/DEVELOPMENT_PLAN.md`
- `docs/KEYBOARD_SHORTCUTS.md`

**Tests added:** `gui.keyboard_input` verifies all twelve unique default bindings,
connected neutral state, monotonic change-only sequences, ignored unknown keys,
auto-repeat suppression, last-pressed opposite-direction resolution and restoration,
all three-/six-button controls, Ctrl/Alt/Meta shortcut bypass, event-filter delivery,
focus-loss release, and end-to-end snapshot submission to a real worker. Its generated
ROM workflow confirms both pressed and released sequences are applied at explicit frame
boundaries; the existing semantic core input test continues to verify B+Right as the
expected active-low value in synthetic work RAM.

**Gate evidence:**

- Focused keyboard, core-input, and worker integration tests: passed.
- Keyboard and core-input tests each passed five consecutive Debug executions.
- Debug build and complete CTest: passed (24/24).
- Release build and complete CTest: passed (24/24).
- The first ASan/UBSan run found a destructor-time callback into an expired test sink;
  destruction now disconnects the sink before cleanup, and the complete rerun passed
  (24/24).
- `make -f Makefile.libretro platform=unix -j4`: passed with only the two documented
  inherited qualifier warnings, then cleaned.
- New input service, application integration, and test code compiled under the frontend
  warning policy without warnings.

**Acceptance criteria:** The display is keyboard-focusable. Player 1 defaults use
arrows, Z/X/C, A/S/D, Return, and Shift; no core-specific bit value enters the Qt
service. Input is emitted only when logical state changes, auto-repeat is consumed,
opposite axes have deterministic last-input priority, and every focus/lifecycle exit
clears held keys. Destruction disconnects the callback before internal cleanup so a
sink can never be invoked after its captured state has expired. Modified application
shortcuts bypass gameplay presses. The GUI
submits snapshots only while a game is paused or running, and the worker coalesces and
applies them on its core-owning thread at the next frame boundary.

**Commit SHA:** `48d2844`

## Milestone 17 detail

**Status:** COMPLETE

**Goal:** Discover SDL3-compatible gamepads, handle hot-plug lifecycle, provide a
complete standard Genesis six-button mapping, assign multiple players, and safely merge
controller and keyboard input before the worker boundary.

**Files changed:**

- `desktop/core/include/genplusgx/input_snapshot.h`
- `desktop/input/CMakeLists.txt`
- `desktop/input/include/genplusgx/input/controller_input.h`
- `desktop/input/include/genplusgx/input/input_aggregator.h`
- `desktop/input/src/controller_input.cpp`
- `desktop/input/src/input_aggregator.cpp`
- `desktop/app/main.cpp`
- `tests/unit/CMakeLists.txt`
- `tests/unit/controller_input_test.cpp`
- `tests/unit/input_aggregator_test.cpp`
- `docs/ARCHITECTURE.md`
- `docs/INPUT_CONFIGURATION.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `unit.controller_input` uses SDL's official virtual joystick facility
and injected SDL gamepad events to verify startup, bounded event polling, preservation
of unrelated events, discovery, add/remove notification, standardized three-/six-button
mapping, button release, last-input directional priority, analog deadzone and digital
projection, two-player allocation, occupied-slot swapping, unknown-device handling,
thread ownership, teardown, and snapshot publication. `unit.input_aggregator` verifies
source sequence rejection, independent aggregate sequencing, multi-player state,
keyboard/controller merging, analog preservation, cross-device SOCD neutralization, and
complete clearing.

**Gate evidence:**

- Focused controller and aggregator tests passed five consecutive Debug executions.
- Debug build and complete CTest: passed (26/26).
- Release build and complete CTest: passed (26/26).
- ASan/UBSan preset build and complete CTest: passed (26/26).
- `make -f Makefile.libretro platform=unix -j4`: passed with only the two documented
  inherited qualifier warnings, then cleaned.
- New SDL service, aggregation, application composition, and tests compiled under the
  frontend warning policy without warnings.

**Acceptance criteria:** SDL owns controller discovery and standard platform mapping but
never reaches the core. The GUI thread pumps at most 256 gamepad events per tick and
does not consume unrelated SDL events. Eight controller records and their state are
bounded; devices receive the lowest available logical player, may be explicitly swapped,
and close before SDL teardown. The default positional mapping covers all twelve Genesis
six-button controls. Axis values within the configured deadzone are neutral, while
values outside it retain analog precision and drive directions. A frontend-neutral
aggregator prevents keyboard and Player 1 controller snapshots from overwriting each
other and emits one monotonic, change-only stream to the worker.

**Commit SHA:** `58b908d`

## Milestone 18 detail

**Status:** COMPLETE

**Goal:** Deliver an accessible, persistent input configuration workflow with named
profiles, keyboard/controller capture, conflict prevention, axis/deadzone mapping,
player assignment, reset-to-default behavior, and advanced logical device selections.

**Files changed:**

- `CMakeLists.txt`
- `desktop/input/CMakeLists.txt`
- `desktop/input/include/genplusgx/input/controller_input.h`
- `desktop/input/include/genplusgx/input/input_profile.h`
- `desktop/input/include/genplusgx/input/keyboard_input.h`
- `desktop/input/src/controller_input.cpp`
- `desktop/input/src/input_profile.cpp`
- `desktop/input/src/keyboard_input.cpp`
- `desktop/ui/CMakeLists.txt`
- `desktop/ui/include/genplusgx/ui/binding_capture_button.h`
- `desktop/ui/include/genplusgx/ui/input_configuration_dialog.h`
- `desktop/ui/include/genplusgx/ui/main_window.h`
- `desktop/ui/src/binding_capture_button.cpp`
- `desktop/ui/src/input_configuration_dialog.cpp`
- `desktop/ui/src/main_window.cpp`
- `desktop/app/main.cpp`
- `tests/unit/CMakeLists.txt`
- `tests/unit/controller_input_test.cpp`
- `tests/unit/input_profile_test.cpp`
- `tests/gui/CMakeLists.txt`
- `tests/gui/input_configuration_dialog_test.cpp`
- `tests/gui/keyboard_input_test.cpp`
- `docs/ARCHITECTURE.md`
- `docs/DEVELOPMENT_PLAN.md`
- `docs/INPUT_CONFIGURATION.md`

**Tests added:** `unit.input_profile` covers defaults, typed duplicate/hotkey conflicts,
safe reassignment, exact atomic JSON round-trip, duplicate-name rejection, schema 0
migration, missing advanced-field defaults, future-schema rejection, corruption fallback,
and temporary-path isolation. Controller tests now cover live axis remapping, invalid
axis definitions, and consumption of captured buttons; keyboard tests cover runtime
custom mappings and transactional rejection. `gui.input_configuration` drives actual
capture buttons, Escape cancellation, duplicate and hotkey diagnostics, controller
capture, profile add/delete/reset, deadzone and axis/device settings, Apply semantics,
assignment collision handling, hot-plug refresh, and both MainWindow entry points.

**Gate evidence:**

- Focused profile and input-dialog tests passed five consecutive Debug executions.
- Debug build and complete CTest: passed (28/28).
- Release build and complete CTest: passed (28/28).
- ASan/UBSan preset build and complete CTest: passed (28/28).
- `make -f Makefile.libretro platform=unix -j4`: passed with only the two documented
  inherited qualifier warnings, then cleaned.
- New profile, runtime mapping, GUI, composition, and test code compiled under the
  frontend warning policy without warnings.

**Acceptance criteria:** Input state remains bounded and core-neutral. A versioned,
validated model supports up to 32 named profiles with complete default keyboard,
gamepad, and left-stick mappings, configurable deadzone, and eight logical device types.
Profiles persist atomically under the platform application-data root and migrate without
destroying old settings. Capture is keyboard navigable, visibly identifies its waiting
state, supports Escape cancellation, rejects duplicates and emulator-hotkey collisions,
and prevents a captured gamepad press from reaching gameplay. Apply updates the live
keyboard/controller services; Cancel does not. Connected controllers can be assigned to
unique players, and the assignment UI follows hot-plug changes without unsafe callback
reentrancy.

**Commit SHA:** `87c13b8`

## Milestone 19 detail

**Status:** COMPLETE

**Goal:** Connect the application shell to the real emulation worker through safe Open,
Close, drag/drop, and startup-file workflows; provide useful asynchronous load errors;
and support conventional help, version, and fullscreen command-line behavior.

**Files changed:**

- `desktop/core/CMakeLists.txt`
- `desktop/core/include/genplusgx/game_file.h`
- `desktop/core/src/game_file.cpp`
- `desktop/app/CMakeLists.txt`
- `desktop/app/include/genplusgx/app/command_line.h`
- `desktop/app/command_line.cpp`
- `desktop/app/main.cpp`
- `desktop/ui/CMakeLists.txt`
- `desktop/ui/include/genplusgx/ui/dialog_service.h`
- `desktop/ui/include/genplusgx/ui/main_window.h`
- `desktop/ui/src/dialog_service.cpp`
- `desktop/ui/src/main_window.cpp`
- `tests/unit/CMakeLists.txt`
- `tests/unit/game_file_test.cpp`
- `tests/unit/command_line_test.cpp`
- `tests/gui/CMakeLists.txt`
- `tests/gui/game_loading_test.cpp`
- `docs/ARCHITECTURE.md`
- `docs/USER_GUIDE.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `unit.game_file` verifies the compile-time format list, uppercase
extensions, valid generated games, missing/unsupported/overlong paths, and the deliberate
absence of unsupported ZIP claims. `unit.command_line` verifies normal, fullscreen,
short-option, option-terminator, unknown-option, multiple-file, and help behavior.
`gui.game_loading` drives an injected native-dialog seam, cancellation, validation
errors, action state, status text, close dispatch, supported single-file drag/drop,
multiple-file rejection, real generated-ROM load/run/frame presentation, in-process game
replacement, unload, and malformed-image core failure.

**Gate evidence:**

- Debug build and complete CTest: passed (31/31).
- Release build and complete CTest: passed (31/31).
- ASan/UBSan build and complete CTest: passed (31/31).
- Focused load/CLI/filesystem tests passed five consecutive Debug executions.
- Legacy libretro build regression passed with only the two documented inherited
  qualifier warnings, then cleaned.
- New file, command-line, GUI, and composition code compiled under the frontend warning
  policy without warnings.

**Acceptance criteria:** Native selection and drag/drop validate one local regular file
before enqueueing it. The actual Genesis Plus GX worker owns load, replacement, run, and
unload; the GUI only submits bounded commands and reacts to operation IDs. Successful
loads automatically start emulation and publish frames. File-dialog cancellation is a
no-op, invalid preflight selection cannot disturb a running game, failed core loads leave
no half-loaded UI state, and message boxes are asynchronous and injectable. The format
list matches the raw desktop host instead of claiming ZIP or playlist support; CHD is
shown only in CHD-enabled builds. CLI diagnostics return status 2 for invalid syntax and
support one positional game plus `--fullscreen`, `--help`, and `--version`.

**Commit SHA:** `568c811`

## Milestone 20 detail

**Status:** COMPLETE

**Goal:** Maintain a bounded, versioned recent-game history under the platform
application-data root; expose it through a conventional Open Recent menu; handle stale
paths safely; and allow the user to clear history without affecting active emulation.

**Files changed:**

- `desktop/persistence/CMakeLists.txt`
- `desktop/persistence/include/genplusgx/persistence.h`
- `desktop/persistence/include/genplusgx/recent_games.h`
- `desktop/persistence/src/recent_games.cpp`
- `desktop/ui/include/genplusgx/ui/main_window.h`
- `desktop/ui/src/main_window.cpp`
- `desktop/app/main.cpp`
- `tests/unit/CMakeLists.txt`
- `tests/unit/recent_games_test.cpp`
- `tests/gui/game_loading_test.cpp`
- `docs/ARCHITECTURE.md`
- `docs/USER_GUIDE.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `unit.recent_games` verifies insertion ordering, duplicate promotion,
timestamp replacement, a strict 12-entry cap, removal/clear behavior, invalid inputs,
atomic JSON round-trip, missing-file defaults, schema 0 migration, future-schema
rejection, and corruption isolation in a temporary root. `gui.game_loading` now verifies
numbered stable actions, full-path tooltips, missing-entry disabling, launch through the
normal validated load route, operation-time menu disabling, and Clear Recent behavior.

**Gate evidence:**

- Focused recent model/menu tests passed five consecutive Debug executions.
- Debug build and complete CTest: passed (32/32).
- Release build and complete CTest: passed (32/32).
- ASan/UBSan build and complete CTest: passed (32/32).
- Legacy libretro regression passed with only the two documented inherited qualifier
  warnings, then cleaned.
- New model, serialization, menu, application composition, and tests compile under the
  frontend warning policy without warnings.

**Acceptance criteria:** Only a successful core load enters history. Paths are made
absolute once, compared using platform-appropriate case rules, stored newest-first, and
bounded to 12. Duplicate launches update the timestamp and move the existing entry
instead of growing the file. `config/recent-games.json` uses schema 1 and atomic bounded
writes; schema 0 path arrays migrate without silent data loss, while corrupt or future
schemas expose no entries. The menu shows basename labels and full-path tooltips, marks
missing files disabled, cannot dispatch while another lifecycle operation is active,
and clears history without unloading the current game.

**Commit SHA:** `8615cac`

## Milestone 21 detail

**Status:** COMPLETE

**Goal:** Connect cartridge SRAM, Sega CD internal BRAM, and Sega CD RAM-cartridge
memory to content-identified platform storage, loading before the first frame and
atomically flushing before unload, replacement, or shutdown without crossing the
emulation-thread boundary.

**Files changed:**

- `desktop/core/include/genplusgx/backup_memory.h`
- `desktop/core/include/genplusgx/core_adapter.h`
- `desktop/core/include/genplusgx/emulation_worker.h`
- `desktop/core/src/core_adapter.cpp`
- `desktop/core/src/emulation_worker.cpp`
- `desktop/persistence/CMakeLists.txt`
- `desktop/persistence/include/genplusgx/backup_store.h`
- `desktop/persistence/src/backup_store.cpp`
- `desktop/ui/include/genplusgx/ui/main_window.h`
- `desktop/ui/src/main_window.cpp`
- `desktop/app/main.cpp`
- `tests/unit/CMakeLists.txt`
- `tests/unit/backup_store_test.cpp`
- `tests/core/CMakeLists.txt`
- `tests/core/core_backup_memory_test.cpp`
- `tests/core/backup_persistence_test.cpp`
- `tests/utilities/synthetic_rom.h`
- `tests/utilities/synthetic_rom.cpp`
- `tests/fixtures/README.md`
- `docs/ARCHITECTURE.md`
- `docs/USER_GUIDE.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `core.backup_memory` uses an original generated SRAM-enabled Genesis
ROM to verify erased initialization, an actual 68000 write through the core memory map,
bounded copying, exact replacement, hard-reset retention, and wrong-size rejection.
`unit.backup_store` verifies distinct cartridge/internal-CD/RAM-cartridge mapping,
round trips, strict expected sizes, and active-identity lifetime. The full
`core.backup_persistence` workflow loads, executes, atomically unloads, restores before
the first frame, rejects corrupt data, flushes on shutdown, and injects write failures
to verify that unload preserves the active game and shutdown reports data-loss risk.

**Gate evidence:**

- Focused core/store/GUI persistence checks passed five consecutive Debug executions.
- Debug build and complete CTest: passed (35/35).
- Release build and complete CTest: passed (35/35).
- ASan/UBSan build and complete CTest: passed (35/35) with no findings.
- Legacy libretro regression passed with only the two documented inherited qualifier
  warnings, then cleaned.
- New adapter, worker, persistence, GUI, fixture, and test code compiled under the
  frontend warning policy without warnings.

**Acceptance criteria:** Core backup globals are visible only through owner-thread
adapter copies. Available memory is discovered after system initialization; missing
files receive core-appropriate initialization, including a formatted Sega CD BRAM
header. Existing files must exactly match the core-reported size, so corruption never
reaches a lengthless core API. One content identity owns three stable file types, and
all I/O remains on the emulation thread. Replacement and unload flush before releasing
the old core; a failed atomic write leaves that game loaded and surfaces a GUI error.
Shutdown always releases core and persistence resources but returns the save failure.

**Commit SHA:** `6e50cac`

## Milestone 22 detail

**Status:** COMPLETE

**Goal:** Connect the existing validated state envelope and raw core capture/restore
commands to an asynchronous desktop workflow with quick save/load, slots 0–9,
timestamps, previous/next selection, deletion, useful failures, and strict game/system
identity checks before a payload reaches the core.

**Files changed:**

- `desktop/core/include/genplusgx/emulation_worker.h`
- `desktop/core/src/emulation_worker.cpp`
- `desktop/persistence/CMakeLists.txt`
- `desktop/persistence/include/genplusgx/state_storage_service.h`
- `desktop/persistence/src/state_storage_service.cpp`
- `desktop/ui/include/genplusgx/ui/main_window.h`
- `desktop/ui/src/main_window.cpp`
- `desktop/app/CMakeLists.txt`
- `desktop/app/main.cpp`
- `tests/core/emulation_worker_test.cpp`
- `tests/unit/CMakeLists.txt`
- `tests/unit/state_storage_service_test.cpp`
- `tests/gui/CMakeLists.txt`
- `tests/gui/main_window_test.cpp`
- `tests/gui/save_state_workflow_test.cpp`
- `docs/ARCHITECTURE.md`
- `docs/KEYBOARD_SHORTCUTS.md`
- `docs/SAVE_STATES.md`
- `docs/USER_GUIDE.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `unit.state_storage_service` verifies service lifecycle, bounded
command validation, asynchronous identity creation, ten-slot discovery, save/load
payload round trips, timestamps/frame metadata, stale-generation rejection, corrupt
slot discovery, deletion, deactivation, and restart. `gui.main_window` drives all slot
states and action enablement, selection/navigation, busy gating, sink dispatch,
notifications, and error presentation. `gui.save_state_workflow` runs a generated ROM
through real worker capture, atomic wrapping, execution mutation, validated restore,
raw-state equality, deletion, and a copied wrong-game envelope that remains visibly
invalid and never reaches `state_load()`.

**Gate evidence:**

- Focused state unit/core/GUI/integration checks passed five consecutive Debug
  executions.
- Debug build and complete CTest: passed (37/37).
- Release build and complete CTest: passed (37/37).
- ASan/UBSan build and complete CTest: passed (37/37) with no findings.
- Legacy libretro regression passed with only the two documented inherited qualifier
  warnings, then cleaned.
- New storage service, composition, UI, and tests compiled under the frontend warning
  policy without warnings.

**Acceptance criteria:** The GUI exposes stable slot 0–9 actions, F5/F8 quick actions,
Ctrl+0–9 selection, previous/next navigation, timestamps, invalid-state markers, and
delete. Exactly one operation is in flight and lifecycle actions are gated until it
finishes. Core capture/restore remains on the emulation thread; hashing, bounded reads,
checksum/schema/game/system validation, atomic writes, slot scans, and deletion run on
a separate bounded storage thread. Operation and game-generation IDs prevent stale
results from crossing a replacement. Only a successfully validated raw payload is
submitted to the worker, and corrupt/wrong-game files can be deleted without loading.

**Commit SHA:** `c8a3a2a`

## Milestone 23 detail

**Status:** COMPLETE

**Goal:** Provide one validated, versioned video configuration for display presentation
and supported Genesis Plus GX output controls; apply core changes only on the emulation
thread; and expose synchronized quick actions plus a keyboard-accessible settings
dialog with Apply, Cancel, OK, and Restore Defaults semantics.

**Files changed:**

- `CMakeLists.txt`
- `desktop/core/include/genplusgx/core_adapter.h`
- `desktop/core/include/genplusgx/core_video_settings.h`
- `desktop/core/include/genplusgx/emulation_worker.h`
- `desktop/core/src/core_adapter.cpp`
- `desktop/core/src/emulation_worker.cpp`
- `desktop/settings/CMakeLists.txt`
- `desktop/settings/include/genplusgx/settings/video_settings.h`
- `desktop/settings/src/video_settings.cpp`
- `desktop/ui/CMakeLists.txt`
- `desktop/ui/include/genplusgx/ui/main_window.h`
- `desktop/ui/include/genplusgx/ui/video_settings_dialog.h`
- `desktop/ui/src/main_window.cpp`
- `desktop/ui/src/video_settings_dialog.cpp`
- `desktop/app/CMakeLists.txt`
- `desktop/app/main.cpp`
- `tests/unit/CMakeLists.txt`
- `tests/unit/video_settings_test.cpp`
- `tests/core/core_video_test.cpp`
- `tests/core/emulation_worker_test.cpp`
- `tests/gui/main_window_test.cpp`
- `docs/ARCHITECTURE.md`
- `docs/USER_GUIDE.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `unit.video_settings` verifies missing-file defaults, complete atomic
round-trip, invalid enum rejection, schema 0 migration, malformed JSON isolation, and
future-schema rejection in a temporary root. `core.video_frame` now initializes and
executes every bundled Blargg NTSC preset path, validates live overscan geometry,
round-trips the active core snapshot, rejects invalid values without partial mutation,
and restores native output. `core.emulation_worker` proves that a settings command
crosses the ownership boundary and affects a subsequently published frame.
`gui.main_window` drives all quick actions and verifies dialog initialization,
Apply/Cancel/Restore behavior, synchronized action checks, display policy changes, and
stable object names.

**Gate evidence:**

- Focused video settings/model/core/worker/GUI checks passed five consecutive Debug
  executions.
- Debug build and complete CTest: passed (38/38).
- Release build and complete CTest: passed (38/38).
- ASan/UBSan build and complete CTest: passed (38/38) with no findings.
- Legacy libretro regression passed with only the two documented inherited qualifier
  warnings, then cleaned.
- New settings, adapter, worker, GUI, composition, and test code compiled under the
  frontend warning policy without warnings.

**Acceptance criteria:** `config/video-settings.json` has a strict schema 1 with an
explicit schema 0 migration and atomic bounded writes. Native/4:3/stretch layout,
fit/integer scaling, and nearest/bilinear sampling update the display immediately.
Overscan, Game Gear extended screen, NTSC monochrome/composite/S-Video/RGB filtering,
and single/double-field interlaced rendering are converted to a typed core-neutral
snapshot, coalesced in the bounded worker queue, and applied between frames on the sole
core-owning thread. NTSC table memory is adapter-owned and cannot be enabled with null
filter tables. Runtime core viewport notifications are refreshed without touching
authoritative core source. Invalid, corrupt, or future settings fail closed to safe
defaults.

**Commit SHA:** recorded by milestone 24

## Milestone 24 detail

**Status:** COMPLETE

**Goal:** Expose frontend and Genesis Plus GX audio controls through one strict,
versioned snapshot while keeping real-time host controls callback-safe and every core
chip/mixer mutation on the sole emulation thread.

**Files changed:**

- `desktop/core/include/genplusgx/core_audio_settings.h`
- `desktop/core/include/genplusgx/core_adapter.h`
- `desktop/core/include/genplusgx/emulation_worker.h`
- `desktop/core/src/core_adapter.cpp`
- `desktop/core/src/emulation_worker.cpp`
- `desktop/audio/include/genplusgx/audio_output.h`
- `desktop/audio/src/audio_output.cpp`
- `desktop/settings/CMakeLists.txt`
- `desktop/settings/include/genplusgx/settings/audio_settings.h`
- `desktop/settings/src/audio_settings.cpp`
- `desktop/ui/CMakeLists.txt`
- `desktop/ui/include/genplusgx/ui/audio_settings_dialog.h`
- `desktop/ui/include/genplusgx/ui/main_window.h`
- `desktop/ui/src/audio_settings_dialog.cpp`
- `desktop/ui/src/main_window.cpp`
- `desktop/app/main.cpp`
- `tests/unit/CMakeLists.txt`
- `tests/unit/audio_output_test.cpp`
- `tests/unit/audio_settings_test.cpp`
- `tests/core/core_audio_test.cpp`
- `tests/core/emulation_worker_test.cpp`
- `tests/gui/main_window_test.cpp`
- `docs/ARCHITECTURE.md`
- `docs/USER_GUIDE.md`
- `docs/KEYBOARD_SHORTCUTS.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `unit.audio_settings` verifies defaults, exact atomic round trip,
range validation, schema 0 migration, malformed JSON isolation, and future-schema
rejection. `unit.audio_output` verifies signed gain, mute, live control bounds, and
dummy-device lifecycle. `core.audio_samples` validates the typed snapshot, rejects
partial invalid mutation, applies settings to a loaded core, and proves deterministic
output changes. `core.emulation_worker` proves the command crosses the ownership
boundary and affects the next audio batch. `gui.main_window` drives mute and volume
quick actions plus dialog initialization, dynamic filter controls, device selection,
Apply, Cancel, and Restore Defaults.

**Gate evidence:**

- Debug build and complete CTest: passed (39/39).
- Focused audio settings/model/output/core/worker/GUI checks passed after correcting
  the fixture assertion to account for deterministic non-PSG startup energy.
- Release build and complete CTest: passed (39/39).
- ASan/UBSan build and complete CTest: passed (39/39) with no findings.
- Legacy libretro regression passed with only the two documented inherited qualifier
  warnings, then cleaned.
- New audio code compiles under the frontend warning policy without warnings.

**Acceptance criteria:** `config/audio-settings.json` uses bounded atomic schema 1
writes and explicit schema 0 migration. Master volume and mute are lock-free atomics
read by the SDL callback. Devices are enumerated through SDL and matched by persisted
name; unavailable devices fall back to the default with a diagnostic. Device and
latency changes are marked restart-required because they define stream/ring ownership.
Mono, filter/low-pass/EQ, PSG/FM/CDDA/PCM levels, MAME/Nuked YM2612 and YM2413 cores,
YM2413 mode, and HQ resampling form a validated core-neutral snapshot. Commands
coalesce in the bounded worker queue and apply between frames. Chip implementation
changes reinitialize only the core sound layer. Authoritative core source is unchanged.

**Commit SHA:** recorded by milestone 25

## Milestone 25 detail

**Status:** COMPLETE

**Goal:** Expose supported hardware, region, VDP standard, master-clock, DTACK lockup,
and 68000 address-error choices without mutating an active machine into a hybrid state.

**Files changed:**

- `desktop/core/include/genplusgx/core_system_settings.h`
- `desktop/core/include/genplusgx/core_adapter.h`
- `desktop/core/include/genplusgx/emulation_worker.h`
- `desktop/core/src/core_adapter.cpp`
- `desktop/core/src/emulation_worker.cpp`
- `desktop/settings/CMakeLists.txt`
- `desktop/settings/include/genplusgx/settings/system_settings.h`
- `desktop/settings/src/system_settings.cpp`
- `desktop/ui/CMakeLists.txt`
- `desktop/ui/include/genplusgx/ui/main_window.h`
- `desktop/ui/include/genplusgx/ui/system_settings_dialog.h`
- `desktop/ui/src/main_window.cpp`
- `desktop/ui/src/system_settings_dialog.cpp`
- `desktop/app/main.cpp`
- `tests/unit/CMakeLists.txt`
- `tests/unit/system_settings_test.cpp`
- `tests/core/CMakeLists.txt`
- `tests/core/core_system_settings_test.cpp`
- `tests/core/emulation_worker_test.cpp`
- `tests/gui/main_window_test.cpp`
- `docs/ARCHITECTURE.md`
- `docs/USER_GUIDE.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `unit.system_settings` verifies defaults, exact atomic round trip,
invalid-enum rejection, schema 0 migration, malformed JSON isolation, and future-schema
rejection. `core.system_settings` verifies invalid snapshots never partially mutate,
PAL VDP timing can be combined with the NTSC master clock, loaded-session changes are
deferred without changing active hardware/timing, forced SG-1000 plus NTSC VDP/PAL
clock apply on reload, and restored automatic detection selects Genesis again.
`core.emulation_worker` verifies the typed command crosses the ownership boundary while
preserving active hardware. `gui.main_window` drives every control and verifies stable
names plus Apply, Cancel, Restore Defaults, sink, and reload notification behavior.

**Gate evidence:**

- Debug build and complete CTest: passed (41/41).
- Focused model/core/worker/GUI checks passed five consecutive Debug executions.
- Release build and complete CTest: passed (41/41).
- ASan/UBSan build and complete CTest: passed (41/41) with no findings.
- Legacy libretro regression passed with only the two documented inherited qualifier
  warnings, then cleaned.
- New system code compiles under the frontend warning policy without warnings.

**Acceptance criteria:** `config/system-settings.json` is a bounded atomic schema 1
file with explicit schema 0 migration. Automatic and forced SG-1000, SG-1000 II,
SG-1000 II + RAM, Mark III, Master System, Master System II, Game Gear, and Genesis
hardware are represented without leaking core constants into UI/persistence code.
Automatic/NTSC-U/PAL Europe/NTSC-J/PAL Japan region, automatic/NTSC/PAL VDP mode, and
automatic/NTSC/PAL master clock remain independent. Accuracy toggles retain hardware-
accurate defaults. A loaded game continues with the exact machine it started with;
the complete desired snapshot is applied before the next load. Commands coalesce in
the bounded worker queue and are never applied from the GUI thread. The dialog states
the reload boundary explicitly. Authoritative core source is unchanged.

**Commit SHA:** recorded by milestone 26

## Milestone 26 detail

**Status:** COMPLETE

**Goal:** Provide a cross-platform, read-only firmware manager that persists every
supported BIOS slot, validates files without depending on proprietary reference data,
and exposes useful status through a native, testable GUI.

**Files changed:**

- `CMakeLists.txt`
- `desktop/platform/CMakeLists.txt`
- `desktop/platform/include/genplusgx/platform/bios_manager.h`
- `desktop/platform/src/bios_manager.cpp`
- `desktop/ui/CMakeLists.txt`
- `desktop/ui/include/genplusgx/ui/bios_settings_dialog.h`
- `desktop/ui/include/genplusgx/ui/main_window.h`
- `desktop/ui/src/bios_settings_dialog.cpp`
- `desktop/ui/src/main_window.cpp`
- `desktop/app/CMakeLists.txt`
- `desktop/app/main.cpp`
- `tests/unit/CMakeLists.txt`
- `tests/unit/bios_manager_test.cpp`
- `tests/gui/main_window_test.cpp`
- `docs/ARCHITECTURE.md`
- `docs/BIOS.md`
- `docs/USER_GUIDE.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `unit.bios_manager` generates non-proprietary byte patterns at runtime
and verifies unconfigured/missing/path-too-long distinctions, slot-specific Genesis,
Master System, Game Gear, and Sega CD size rules, repeated-byte rejection, SHA-256 and
type reporting, exact atomic persistence, schema 0 migration, corrupt/future-schema
isolation, and manager reload. `gui.main_window` opens the full BIOS page, verifies
missing and valid status, drives an injected file picker, inspects the checksum, and
proves Apply, Cancel, Clear, and Restore Defaults semantics.

**Gate evidence:**

- Focused BIOS model and GUI checks passed five consecutive Debug executions.
- Debug build and complete CTest: passed (42/42).
- Release build and complete CTest: passed (42/42).
- ASan/UBSan build and complete CTest: passed (42/42) with no findings.
- Legacy libretro regression passed with only the two documented inherited qualifier
  warnings, then cleaned.
- New platform, GUI, composition, and test code compiled under the frontend warning
  policy without warnings.

**Acceptance criteria:** `config/bios.json` is a bounded atomic schema 1 file with an
explicit schema 0 migration. It retains independent paths for Genesis, Game Gear,
three Master System regions, and three Sega CD/Mega CD regions. Validation performs
bounded whole-file reads, rejects missing/non-file/unreadable/overlong/wrong-sized and
obviously blank repeated-byte images, calculates SHA-256, and reports an expected
region plus detected firmware family. Checksums are informational rather than a brittle
proprietary hash allowlist. The GUI stages choices until Apply/OK, reports save errors,
and contains stable accessible controls. No firmware is bundled, fetched, modified, or
written. The manager itself never crosses the emulation-thread boundary; milestone 27
projects only its validated Sega CD paths into a typed worker command.

**Commit SHA:** `77a562d`

## Milestone 27 detail

**Status:** COMPLETE

**Goal:** Make Sega CD/Mega CD a first-class desktop session with validated regional
BIOS startup, typed disc change/eject operations, CUE/BIN and conditional CHD routing,
live CD audio continuity, and automatic internal/RAM-cartridge backup persistence.

**Files changed:**

- `CMakeLists.txt`
- `desktop/core/CMakeLists.txt`
- `desktop/core/include/genplusgx/bounded_queue.h`
- `desktop/core/include/genplusgx/core_adapter.h`
- `desktop/core/include/genplusgx/core_firmware_settings.h`
- `desktop/core/include/genplusgx/emulation_worker.h`
- `desktop/core/include/genplusgx/game_file.h`
- `desktop/core/src/core_adapter.cpp`
- `desktop/core/src/emulation_worker.cpp`
- `desktop/core/src/game_file.cpp`
- `desktop/ui/include/genplusgx/ui/dialog_service.h`
- `desktop/ui/include/genplusgx/ui/main_window.h`
- `desktop/ui/src/dialog_service.cpp`
- `desktop/ui/src/main_window.cpp`
- `desktop/app/main.cpp`
- `tests/core/CMakeLists.txt`
- `tests/core/sega_cd_workflow_test.cpp`
- `tests/core/sega_cd_external_bios_test.cpp`
- `tests/unit/bounded_queue_test.cpp`
- `tests/unit/game_file_test.cpp`
- `tests/gui/CMakeLists.txt`
- `tests/gui/main_window_test.cpp`
- `tests/utilities/synthetic_rom.h`
- `tests/utilities/synthetic_rom.cpp`
- `tests/fixtures/README.md`
- `docs/ARCHITECTURE.md`
- `docs/BIOS.md`
- `docs/USER_GUIDE.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `core.sega_cd_workflow` generates original CC0 test firmware and USA/
Europe cooked images, proves region-specific missing-BIOS behavior, initializes real
Sega CD core hardware, executes a frame, exercises tray open/close and transactional
malformed/valid CUE-BIN swaps through both adapter and worker, and verifies exact 8 KiB
internal BRAM plus 512 KiB RAM-cartridge files on unload. `unit.game_file` checks the
disc-only format boundary. `unit.bounded_queue` protects path-bearing coalesced commands
from destructive failed searches. `gui.main_window` drives the injected disc chooser,
busy states, typed swap/eject requests, status labels, failure dialog, and cartridge
disablement. An opt-in external test uses `GENPLUSGX_TEST_SEGA_CD_US_BIOS`; it is not
configured or run by default.

**Gate evidence:**

- Focused core, file validation, bounded queue, and GUI tests passed five consecutive
  Debug executions.
- Debug build and complete CTest: passed (43/43).
- Release build and complete CTest: passed (43/43).
- ASan/UBSan build and complete CTest: passed (43/43) with no findings.
- A CHD-disabled build compiled and passed the Sega CD workflow plus disc-extension
  tests, confirming conditional format advertisement.
- The opt-in external-firmware test target configured and compiled; it was not run
  because no proprietary user fixture is present.
- Legacy libretro regression passed with only the two documented inherited qualifier
  warnings, then cleaned.
- New core-adapter, worker, GUI, composition, and test code compiled under the frontend
  warning policy without warnings.

**Acceptance criteria:** BIOS paths reach the core only as an immutable validated
snapshot on the emulation thread, and firmware changes affect the next load. Disc
region and tray/presence/path metadata return on bounded worker events. The native UI
enables change/eject only for active Sega CD sessions and supplies recoverable errors.
ISO and CUE/BIN work through the authoritative core, while CHD is advertised only when
the build includes libchdr. Failed swaps leave a live CD machine with its tray open.
Internal BRAM and RAM-cartridge memory persist under the loaded disc's content identity.
No proprietary BIOS, security program, game data, or fetched fixture enters source,
build output, or CI. Authoritative `core/` files are unchanged.

**Commit SHA:** `1baef5d`

## Milestone 28 detail

**Status:** COMPLETE

**Goal:** Provide read-only, format-aware game metadata and SHA-256 identification on
a bounded background service, then expose it in an accessible native dialog without
initializing, changing, or racing the Genesis Plus GX core.

**Files changed:**

- `CMakeLists.txt`
- `desktop/library/CMakeLists.txt`
- `desktop/library/include/genplusgx/library/game_metadata.h`
- `desktop/library/include/genplusgx/library/game_metadata_service.h`
- `desktop/library/src/game_metadata.cpp`
- `desktop/library/src/game_metadata_service.cpp`
- `desktop/ui/CMakeLists.txt`
- `desktop/ui/include/genplusgx/ui/game_information_dialog.h`
- `desktop/ui/include/genplusgx/ui/main_window.h`
- `desktop/ui/src/game_information_dialog.cpp`
- `desktop/ui/src/main_window.cpp`
- `desktop/app/CMakeLists.txt`
- `desktop/app/main.cpp`
- `tests/unit/CMakeLists.txt`
- `tests/unit/game_metadata_test.cpp`
- `tests/gui/main_window_test.cpp`
- `tests/fixtures/README.md`
- `docs/ARCHITECTURE.md`
- `docs/GAME_INFORMATION.md`
- `docs/USER_GUIDE.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `unit.game_metadata` decodes the generated Genesis header and live
checksum, SRAM declaration, interleaved SMD header, Master System/Game Gear `TMR SEGA`
headers, SG-1000 extension, Sega CD security-area title/region, and safe CUE/BIN track
metadata. It verifies a known streaming SHA-256, traversal rejection, unsupported
input, worker start/request/result/stop/restart semantics, and 2,000 fixed-seed arbitrary
byte arrays up to 70,000 bytes. `gui.main_window` now verifies loaded/no-game/busy action
states, typed asynchronous handoff, all significant populated fields, accessible
read-only controls, close behavior, and injected recoverable errors.

**Gate evidence:**

- Focused metadata model/service and GUI tests passed five consecutive Debug runs.
- Debug build and complete CTest: passed (44/44).
- Release build and complete CTest: passed (44/44).
- ASan/UBSan build and complete CTest: passed (44/44) with no findings.
- A CHD-disabled build compiled and passed metadata/file-format tests, confirming that
  the parser accepts exactly the formats advertised by that build.
- Legacy libretro regression passed with only the two documented inherited qualifier
  warnings, then cleaned.
- New metadata, service, UI, composition, and test code compiled under the frontend
  warning policy without warnings; authoritative `core/` files are unchanged.

**Acceptance criteria:** The parser bounds-checks every header offset and normalizes
only printable text. Whole selected files are streamed in 64 KiB chunks for SHA-256
and Genesis checksum while memory remains bounded to the 64 KiB header window; files
over 16 GiB or changing during inspection fail safely. CUE references may not be
absolute, traverse parents, or resolve through symlinks outside their directory. The
service uses fixed-capacity command/event queues and has deterministic restart and
shutdown. The application matches both operation ID and loaded path before displaying
a result, discarding stale work after game changes. The dialog shows titles, system,
region, product, sizes, checksum, mapper/media, disc tracks, path, and SHA-256 with
stable object names and keyboard accessibility. Metadata remains descriptive and
never calls or overrides the core.

**Commit SHA:** `c9e0c03`

## Milestone 29 detail

**Status:** COMPLETE

**Goal:** Add a recoverable, versioned SQLite index for local game directories and a
bounded asynchronous scanner that reuses the safe milestone 28 metadata reader without
blocking the GUI or exposing a database connection across threads.

**Files changed:**

- `desktop/library/CMakeLists.txt`
- `desktop/library/include/genplusgx/library/game_library_database.h`
- `desktop/library/include/genplusgx/library/game_library_scanner.h`
- `desktop/library/src/game_library_database.cpp`
- `desktop/library/src/game_library_scanner.cpp`
- `desktop/app/main.cpp`
- `tests/unit/CMakeLists.txt`
- `tests/unit/game_library_database_test.cpp`
- `tests/unit/game_library_scanner_test.cpp`
- `tests/fixtures/README.md`
- `docs/ARCHITECTURE.md`
- `docs/GAME_LIBRARY.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `unit.game_library_database` verifies schema creation and persistence,
canonical non-overlapping roots, recursive-mode updates, fixed scan-batch limits,
metadata round trips, transaction rollback, stale deletion, favorite/launch preservation
across rescans, directory cascading, binary and structurally corrupt database recovery,
future-schema rejection, and cross-thread access rejection. `unit.game_library_scanner`
indexes generated Genesis and SG-1000 files, distinguishes flat and recursive scans,
ignores unsupported input, removes stale entries, preserves the last complete index when
a root disappears, restarts cleanly, and surfaces preserved corrupt-database recovery.

**Gate evidence:**

- Focused database/scanner tests passed five consecutive Debug executions.
- Debug build and complete CTest: passed (46/46).
- Release build and complete CTest: passed (46/46).
- ASan/UBSan build and complete CTest: passed (46/46) with no findings.
- Legacy libretro regression passed with only the two documented inherited qualifier
  warnings, then cleaned.
- New database, scanner, application-composition, and test code compiled under the
  frontend warning policy without warnings; authoritative `core/` files are unchanged.

**Acceptance criteria:** Schema 1 stores source directories, complete parsed metadata,
favorites, play counts, last-played timestamps, and optional local artwork paths. Database
startup runs SQLite integrity and structural checks; malformed current databases are
moved to a collision-safe `.corrupt-*` backup with sidecars before an empty index is
created, while unknown future schemas fail closed. Directory roots are canonical,
non-overlapping, and capped at 256. Scans run on their own thread with fixed-capacity
command/event queues, a 100,000-file traversal limit, no symlink following, 128-record
batches, and transactional stale cleanup. Cancellation, incomplete traversal, metadata
failures, and missing roots cannot publish a partial replacement index. The application
owns and shuts down the service explicitly; milestone 30 will connect the model to the
native library window.

**Commit SHA:** `58d46df`

## Milestone 30 detail

**Status:** COMPLETE

**Goal:** Expose the offline index as a useful native library window with directory
management, live scan status, composable search/filtering, sorting, favorites, play
history, local artwork, information, and validated launch handoff.

**Files changed:**

- `desktop/library/include/genplusgx/library/game_library_database.h`
- `desktop/library/src/game_library_database.cpp`
- `desktop/ui/CMakeLists.txt`
- `desktop/ui/include/genplusgx/ui/dialog_service.h`
- `desktop/ui/include/genplusgx/ui/game_library_dialog.h`
- `desktop/ui/include/genplusgx/ui/main_window.h`
- `desktop/ui/src/dialog_service.cpp`
- `desktop/ui/src/game_library_dialog.cpp`
- `desktop/ui/src/main_window.cpp`
- `desktop/app/main.cpp`
- `tests/unit/game_library_database_test.cpp`
- `tests/gui/CMakeLists.txt`
- `tests/gui/game_library_dialog_test.cpp`
- `tests/fixtures/README.md`
- `docs/ARCHITECTURE.md`
- `docs/GAME_LIBRARY.md`
- `docs/USER_GUIDE.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `gui.game_library` verifies the modeless window and stable controls;
case-insensitive search; system, region, and favorite filters; numeric play-count sort;
favorite, launch, information, artwork, recursive, scan, remove, and add callbacks; a
bounded artwork preview; progress/busy states; and recoverable errors. Its integration
workflow adds a temporary directory through the real GUI seam, scans an original
generated Genesis ROM on `GameLibraryScanner`, refreshes the real SQLite model, filters
and favorites the indexed result, records launch history, and removes the root/index
without deleting the ROM. The database test additionally verifies canonical local
artwork assignment, clear, invalid-path rejection, and rescan preservation.

**Gate evidence:**

- Focused database, scanner, and complete GUI library workflows passed five consecutive
  Debug executions.
- Debug build and complete CTest: passed (47/47).
- Release build and complete CTest: passed (47/47).
- ASan/UBSan build and complete CTest: passed (47/47) with no findings.
- Legacy libretro regression passed with only the two documented inherited qualifier
  warnings, then cleaned.
- New Qt model/dialog, coordinator, database extension, and test code compiled under
  the frontend warning policy without warnings; authoritative `core/` files are unchanged.

**Acceptance criteria:** **File → Game Library…** opens a keyboard-accessible modeless
window. Canonical roots can be added, removed from the index, toggled between flat and
recursive traversal, and rescanned with visible progress. Search, system, detected
region, and favorite filters compose without database work; every column sorts through
a proxy model. Favorite, last-played, play-count, and canonical local-artwork paths are
durable and rescan-safe. Artwork is decoded at preview size and is never copied,
downloaded, or scraped. Launches use the normal file-validation/core path and update
history only after a successful core load, including Open/Recent launches for indexed
files. GUI write controls and coordinator mutations reject work during a scan, avoiding
SQLite write contention on the event loop. Deferred history uses a fixed 32-entry
queue. Directory enumeration and hashing remain entirely off the GUI/emulation threads.

**Commit SHA:** `6667162`

## Milestone 31 detail

**Status:** COMPLETE

**Goal:** Capture the core's latest complete native frame as a deterministic PNG on a
bounded background worker, with collision-safe atomic output, a configurable platform
directory, visible completion/failure feedback, and no dependency on window chrome or
GPU readback.

**Files changed:**

- `CMakeLists.txt`
- `desktop/screenshots/CMakeLists.txt`
- `desktop/screenshots/include/genplusgx/screenshots/screenshot_service.h`
- `desktop/screenshots/src/screenshot_service.cpp`
- `desktop/settings/CMakeLists.txt`
- `desktop/settings/include/genplusgx/settings/screenshot_settings.h`
- `desktop/settings/src/screenshot_settings.cpp`
- `desktop/ui/CMakeLists.txt`
- `desktop/ui/include/genplusgx/ui/main_window.h`
- `desktop/ui/include/genplusgx/ui/screenshot_settings_dialog.h`
- `desktop/ui/src/main_window.cpp`
- `desktop/ui/src/screenshot_settings_dialog.cpp`
- `desktop/app/CMakeLists.txt`
- `desktop/app/main.cpp`
- `tests/unit/CMakeLists.txt`
- `tests/unit/screenshot_service_test.cpp`
- `tests/gui/main_window_test.cpp`
- `tests/fixtures/README.md`
- `docs/ARCHITECTURE.md`
- `docs/USER_GUIDE.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `unit.screenshot_service` verifies exact red, green, blue, and white
RGB565 conversion, deterministic native 2×2 PNG dimensions, timestamp/frame filenames,
collision suffixes, temporary-file cleanup, invalid-frame/directory rejection, bounded
worker startup/request/completion/stop/restart, settings defaults, atomic round trip,
schema 0 migration, relative-path rejection, corrupt JSON isolation, and future-schema
rejection. `gui.main_window` publishes a complete frame through the real triple buffer,
captures its immutable snapshot through F12, verifies busy/completion presentation,
drives Browse/Apply/Cancel/OK/Restore Defaults and persistence failure behavior, and
proves that starting another load clears the old frame before capture can recur.

**Gate evidence:**

- Focused screenshot service/application and complete main-window workflows passed five
  consecutive Debug executions.
- Debug build and complete CTest: passed (48/48).
- Release build and complete CTest: passed (48/48).
- ASan/UBSan build and complete CTest: passed (48/48) with no findings.
- Legacy libretro regression passed with only the two documented inherited qualifier
  warnings, then cleaned.
- New screenshot service, settings, GUI, composition, and test code compiles under the
  frontend warning policy without warnings; authoritative `core/` files are unchanged.

**Acceptance criteria:** **File → Screenshot** (`F12`) is enabled only while a loaded
game has a complete presented native frame and no capture is outstanding. It copies
that immutable RGB565 frame once, then a dedicated worker expands it to RGB32 and
encodes PNG without blocking emulation or the event loop. Command/event queues have
fixed capacities; only one GUI request is outstanding. Names contain a sanitized game
stem, millisecond timestamp, and emulated frame number, with bounded numeric collision
suffixing. Encoding occurs in a temporary file in the destination directory and commits
by same-directory rename, leaving no partial final PNG. `config/screenshot-settings.json`
is a bounded atomic schema 1 file with explicit schema 0 migration and an absolute-path
requirement. The modeless settings dialog uses the injectable directory picker, and
completion or failure is visible and logged. Starting a new load clears the previous
presentation frame. The application explicitly starts, drains, disconnects, and joins
the screenshot service during shutdown.

**Commit SHA:** `7f04063`

## Milestone 32 detail

**Status:** COMPLETE

**Goal:** Add a system-aware, user-friendly per-game cheat manager without allowing
unvalidated text, persistence data, or GUI timing to mutate Genesis Plus GX globals.

**Files changed:**

- `CMakeLists.txt`
- `desktop/cheats/`
- `desktop/core/CMakeLists.txt`
- `desktop/core/c_api/desktop_core_cheats.c`
- `desktop/core/c_api/desktop_core_host.{c,h}`
- `desktop/core/c_api/osd.h`
- `desktop/core/include/genplusgx/core_cheat.h`
- `desktop/core/include/genplusgx/core_adapter.h`
- `desktop/core/include/genplusgx/emulation_worker.h`
- `desktop/core/src/core_adapter.cpp`
- `desktop/core/src/emulation_worker.cpp`
- `desktop/ui/include/genplusgx/ui/cheat_manager_dialog.h`
- `desktop/ui/include/genplusgx/ui/main_window.h`
- `desktop/ui/src/cheat_manager_dialog.cpp`
- `desktop/ui/src/main_window.cpp`
- `desktop/app/main.cpp`
- `tests/unit/cheat_manager_test.cpp`
- `tests/core/core_cheat_test.cpp`
- `tests/core/emulation_worker_test.cpp`
- `tests/gui/cheat_manager_dialog_test.cpp`
- `tests/fixtures/README.md`
- `docs/ARCHITECTURE.md`
- `docs/CHEATS.md`
- `docs/USER_GUIDE.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `unit.cheats` checks exact Genesis Game Genie and Action Replay,
Master System/Game Gear Game Genie and Action Replay, Fusion RAM/ROM, case/whitespace
normalization, multi-part codes, invalid alphabet/separators/trailing data, aggregate
limits, 10,000 fixed-seed bounded arbitrary inputs, deterministic re-decoding, atomic
per-game round trips, identity/system mismatch, malformed/future JSON, and invalid-save
rejection. `core.cheats` proves immediate generated-ROM patching, reverse restoration,
VBlank work-RAM application/clear, pre-load rejection, and typed patch/count limits.
`gui.cheats` drives Add/Remove/Enable/Apply, inline validation, persistence failure,
normalization, stable object names, and MainWindow session gating. The worker regression
also applies and clears a typed patch list on the core-owning thread.

**Gate evidence:**

- Focused parser/core/worker/GUI cheat workflows passed.
- Debug build and complete CTest: passed (51/51).
- Release build and complete CTest: passed (51/51).
- ASan/UBSan build and complete CTest: passed (51/51) with no findings.
- Legacy libretro regression passed with only the two documented inherited qualifier
  warnings, then cleaned.
- New C/C++/Qt source compiles under the frontend warning policy without warnings;
  authoritative `core/` sources remain unchanged.

**Acceptance criteria:** **Tools → Cheats…** is enabled only after a loaded game has a
safe SHA-256 identity and detected hardware family. Exact supported code forms are
decoded into at most 150 typed patches before crossing the command queue. Enabled lists
apply only on the emulation thread; Genesis ROM bytes restore on replacement/unload,
8-bit banked ROM cheats follow mapper changes, RAM patches use the established VBlank
hook, and Sega CD RAM classification matches the existing frontend behavior. Schema 1
JSON is bounded, atomic, and keyed by complete game identity/system. Invalid UI or disk
content fails closed without a partial save or core mutation.

**Commit SHA:** `7756e2b`

## Milestone 33 detail

**Status:** COMPLETE

**Goal:** Resolve optional game-specific policy over versioned global settings without
path collisions, unnecessary files, or applying machine settings after initialization.

**Files changed:**

- `CMakeLists.txt`
- `desktop/settings/CMakeLists.txt`
- `desktop/settings/include/genplusgx/settings/per_game_settings.h`
- `desktop/settings/src/per_game_settings.cpp`
- `desktop/ui/CMakeLists.txt`
- `desktop/ui/include/genplusgx/ui/per_game_settings_dialog.h`
- `desktop/ui/include/genplusgx/ui/main_window.h`
- `desktop/ui/src/per_game_settings_dialog.cpp`
- `desktop/ui/src/main_window.cpp`
- `desktop/app/main.cpp`
- `tests/unit/per_game_settings_test.cpp`
- `tests/core/per_game_settings_workflow_test.cpp`
- `tests/gui/per_game_settings_dialog_test.cpp`
- `docs/ARCHITECTURE.md`
- `docs/PER_GAME_SETTINGS.md`
- `docs/USER_GUIDE.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `unit.per_game_settings` verifies sparse category precedence, global
host-audio resource inheritance, invalid nested enums/profile text/relative BIOS paths,
atomic round trips, embedded identity rejection, corrupt JSON, invalid roots, and exact
file removal when the last override is cleared. `integration.per_game_settings` queues
resolved PAL settings immediately before a generated Genesis ROM load, verifies the
worker adopts the PAL cadence, then clears the override and verifies the global NTSC
cadence on reload. `gui.per_game_settings` drives category enablement, nested video
editing, input profile selection, Apply, persistence failure, Use Global Settings,
action gating, and load-transition dialog closure using stable object names.

**Gate evidence:**

- Focused model, ordered-load integration, dialog, MainWindow, and game-loading tests
  passed.
- Debug build and complete CTest: passed (54/54).
- Release build and complete CTest: passed (54/54).
- ASan/UBSan build and complete CTest: passed (54/54) with no findings.
- Legacy libretro regression passed with only the two documented inherited qualifier
  warnings, then cleaned.
- New C++/Qt code compiles under the frontend warning policy without warnings;
  authoritative `core/` sources remain unchanged.

**Acceptance criteria:** A bounded schema 1 file stores only explicitly enabled video,
audio, system/region, input-profile, and BIOS categories beneath a full SHA-256 game
identity. Empty configurations delete the exact per-game file. One pure resolver gives
overrides precedence and otherwise inherits current globals; SDL device and ring
latency intentionally remain global. Metadata preflight occurs on its worker, and all
effective core snapshots are ordered ahead of load so machine settings affect the first
frame. Live-safe categories apply immediately. Deleted profiles, corrupt data, queue
failure, failed replacement, unload, and shutdown return to a deterministic safe state.

**Commit SHA:** `cf3e7ac`

## Milestone 34 detail

**Status:** COMPLETE

**Goal:** Provide persistent system, light, and dark presentation without custom widget
skins, retain Qt's platform accessibility behavior, and make the complete preferences
workflow usable without a pointer at fractional and Retina display scales.

**Files changed:**

- `desktop/settings/CMakeLists.txt`
- `desktop/settings/include/genplusgx/settings/appearance_settings.h`
- `desktop/settings/src/appearance_settings.cpp`
- `desktop/ui/CMakeLists.txt`
- `desktop/ui/include/genplusgx/ui/appearance_settings_dialog.h`
- `desktop/ui/include/genplusgx/ui/main_window.h`
- `desktop/ui/include/genplusgx/ui/theme_controller.h`
- `desktop/ui/src/appearance_settings_dialog.cpp`
- `desktop/ui/src/main_window.cpp`
- `desktop/ui/src/theme_controller.cpp`
- `desktop/app/main.cpp`
- `tests/unit/appearance_settings_test.cpp`
- `tests/gui/appearance_accessibility_test.cpp`
- `tests/gui/main_window_test.cpp`
- `docs/APPEARANCE_AND_ACCESSIBILITY.md`
- `docs/ARCHITECTURE.md`
- `docs/USER_GUIDE.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `unit.appearance_settings` verifies system defaults, exact atomic
round trips, invalid enum rejection, schema-zero migration, invalid text, future schema,
and corrupt JSON. `gui.appearance_accessibility` verifies deterministic light/dark
palette application, restoration of the captured platform palette, pass-through
high-DPI rounding, label/buddy and accessible metadata, mnemonic focus, explicit tab
order, Apply/Restore Defaults, visible persistence failure, and the MainWindow
Preferences workflow. The existing MainWindow suite now independently drives the
dedicated Video Settings action.

**Gate evidence:**

- Focused appearance persistence, controller/dialog, keyboard-navigation, and
  MainWindow tests passed.
- Debug build and complete CTest: passed (56/56).
- Release build and complete CTest: passed (56/56).
- ASan/UBSan build and complete CTest: passed (56/56) with no findings.
- Legacy libretro regression passed with only the two documented inherited qualifier
  warnings, then cleaned.
- New C++/Qt code compiles under the frontend warning policy without warnings;
  authoritative `core/` sources remain unchanged.

**Acceptance criteria:** **Tools → Settings…** opens a modeless native Appearance
dialog with stable automation names, a mnemonic-associated theme selector, explicit
keyboard focus order, accessible names/descriptions, and transactional Apply/OK/Cancel
behavior. System mode restores the startup platform style and palette; light/dark use
Qt Fusion plus centralized high-contrast palettes rather than fragile per-widget skins.
Qt 6 receives pass-through scale-factor rounding before `QApplication` construction,
so native widgets remain device-independent while the emulator display retains its
separate pixel/integer-scaling policy. Schema 1 persistence is bounded and atomic;
malformed or unsupported settings fail closed to the system theme.

**Commit SHA:** `824adf5`

## Milestone 35 detail

**Status:** COMPLETE

**Goal:** Retain actionable frontend lifecycle and anomaly evidence in bounded
machine-readable logs, and expose a live support report that is useful without leaking
personal filesystem or credential data.

**Files changed:**

- `CMakeLists.txt`
- `desktop/diagnostics/CMakeLists.txt`
- `desktop/diagnostics/include/genplusgx/diagnostics/diagnostics.h`
- `desktop/diagnostics/src/diagnostics.cpp`
- `desktop/app/CMakeLists.txt`
- `desktop/app/main.cpp`
- `desktop/ui/CMakeLists.txt`
- `desktop/ui/include/genplusgx/ui/diagnostics_dialog.h`
- `desktop/ui/include/genplusgx/ui/main_window.h`
- `desktop/ui/src/diagnostics_dialog.cpp`
- `desktop/ui/src/main_window.cpp`
- `desktop/video/src/display_widget.cpp`
- `tests/unit/diagnostics_test.cpp`
- `tests/gui/diagnostics_dialog_test.cpp`
- `tests/unit/CMakeLists.txt`
- `tests/gui/CMakeLists.txt`
- `docs/LOGGING_AND_DIAGNOSTICS.md`
- `docs/ARCHITECTURE.md`
- `docs/USER_GUIDE.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `unit.diagnostics` verifies home and arbitrary absolute-path removal,
credential-key redaction, safe report formatting, build/Qt/SDL identity, invalid logger
configuration, installed Qt-handler capture, valid JSON Lines, strict byte ceilings,
three-file rotation, retained redaction, and metrics. `gui.diagnostics` drives the real
Tools action, live provider refresh, read-only accessible report, runtime/audio/BIOS
content, privacy filtering, one-dialog ownership, and clipboard equality.

**Gate evidence:**

- Focused logger, formatter, dialog, renderer, and MainWindow tests passed.
- Debug build and complete CTest: passed (58/58).
- Release build and complete CTest: passed (58/58).
- ASan/UBSan build and complete CTest: passed (58/58).
- Legacy libretro regression: passed; the two pre-existing `extract_name`
  const-qualification warnings remain confined to `libretro/libretro.c`.
- New C++/Qt code compiles under the frontend warning policy without warnings;
  authoritative `core/` sources remain unchanged.

**Acceptance criteria:** The process-wide Qt handler writes compact UTF-8 JSON Lines to
`logs/frontend.jsonl`, flushes each event, rotates at 1 MiB through three backups, and
drops rather than grows when rotation/writing fails. Every record has UTC timestamp,
severity, category, and message; home/absolute paths and password/token/secret/
authorization/API-key values are redacted before disk. Startup/build, BIOS status,
renderer, audio, controllers, game load/unload, save states, screenshots, persistence
failures, clean shutdown, and five-second rate-limited timing/audio anomalies are
covered without frame logging. **Tools → Log and Diagnostics…** refreshes a read-only,
keyboard-accessible snapshot containing version/commit, Qt/SDL/OS/architecture,
renderer, audio metrics/device, controller count, loaded title/system/region, safe BIOS
states/hash prefixes, and logger counters. Copy uses the exact filtered report and never
includes ROM, BIOS, log, or home-directory paths.

**Commit SHA:** recorded by milestone 36

## Milestone 36 detail

**Status:** COMPLETE

**Goal:** Exercise every significant desktop menu/dialog workflow headlessly, close
remaining action wiring gaps discovered by the inventory, and make the GUI coverage
matrix explicit and repeatable.

**Files changed:**

- `desktop/app/main.cpp`
- `desktop/ui/CMakeLists.txt`
- `desktop/ui/include/genplusgx/ui/help_dialog.h`
- `desktop/ui/include/genplusgx/ui/main_window.h`
- `desktop/ui/src/help_dialog.cpp`
- `desktop/ui/src/main_window.cpp`
- `tests/gui/CMakeLists.txt`
- `tests/gui/emulation_controls_test.cpp`
- `tests/gui/navigation_regression_test.cpp`
- `docs/TEST_MATRIX.md`
- `docs/USER_GUIDE.md`
- `docs/KEYBOARD_SHORTCUTS.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `gui.emulation_controls` drives Pause/Resume, hard and soft reset,
fast-forward enable/disable, and single-frame advance through real MainWindow actions,
a bounded emulation worker, and a generated Genesis ROM. It verifies canonical worker
state is reflected back into action check/enabled state and rejected submissions roll
back optimistic UI state. `gui.navigation_regression` enforces unique stable object names
and non-conflicting shortcuts for every production action, and exercises both embedded
Help dialogs, accessibility text, one-dialog ownership, close buttons, and Escape.

**Gate evidence:**

- Focused emulation-control, navigation, MainWindow, loading, and state tests passed.
- Complete headless GUI label suite: passed (15/15).
- Debug build and complete CTest: passed (60/60).
- Release build and complete CTest: passed (60/60).
- ASan/UBSan build and complete CTest: passed (60/60). The first sanitizer run
  exposed a test-only raw-pointer dereference after `WA_DeleteOnClose`; the assertion
  now uses `QPointer`, and both the focused and complete sanitizer reruns are clean.
- Legacy libretro regression: passed; the two pre-existing `extract_name`
  const-qualification warnings remain confined to `libretro/libretro.c`.

**Acceptance criteria:** Every significant menu and dialog has behavioral coverage in
`docs/TEST_MATRIX.md`. The previously inert Pause/Resume, Reset, Soft Reset, Fast Forward,
Frame Advance, User Guide, and Keyboard Shortcuts actions are connected. Emulation
commands cross a typed callback into the worker queue; action state is synchronized from
completed worker events and fails closed if a submission is rejected. Help content is
offline, read-only, modal, keyboard closable, accessible, and bounded to one instance per
topic. The platform-independent GUI suite validates behavior rather than widget
construction or native-window pixel goldens.

**Commit SHA:** recorded by milestone 37

## Milestone 37 detail

**Status:** COMPLETE

**Goal:** Put sustained emulation, bounded-buffer behavior, command storms, and repeated
core/worker lifecycle transitions under the same AddressSanitizer and
UndefinedBehaviorSanitizer gate as the functional suite.

**Files changed:**

- `desktop/core/include/genplusgx/core_adapter.h`
- `desktop/core/include/genplusgx/emulation_worker.h`
- `desktop/core/src/core_adapter.cpp`
- `desktop/core/src/emulation_worker.cpp`
- `tests/core/CMakeLists.txt`
- `tests/core/long_running_stability_test.cpp`
- `docs/TESTING.md`
- `docs/TEST_MATRIX.md`
- `docs/ARCHITECTURE.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** `core.long_running_stability` runs 20,000 direct unpaced core frames,
checks every audio batch and periodic video snapshots, asserts fixed scratch capacities,
performs 20 direct reload cycles, saturates the threaded worker with 10,000 coalescible
input updates, runs 90 normal and 600 fast-forward scheduled frames, verifies bounded
audio/video/command/event storage, performs 12 full worker reload/shutdown cycles, and
finishes with a core-lease probe.

**Gate evidence:**

- Focused Debug stability workload: passed in 8.28 seconds.
- Focused ASan/UBSan stability workload: passed in 8.85 seconds with leak detection.
- Debug build and complete CTest: passed (61/61); stability workload 8.48 seconds.
- Release build and complete CTest: passed (61/61); stability workload 5.49 seconds.
- ASan/UBSan build and complete CTest: passed (61/61); stability workload 8.66
  seconds with leak detection and immediate ASan/UBSan failure enabled.
- Legacy libretro regression: passed; the two pre-existing `extract_name`
  const-qualification warnings remain confined to `libretro/libretro.c`.

**Acceptance criteria:** The accelerated test represents more than five minutes of NTSC
emulation without making CI wait in real time. Core scratch capacities are identical
before and after steady-state execution. Queue depths never exceed reported capacities,
newest-input coalescing is exercised, the audio ring reports controlled overruns without
growing, video remains a fixed triple buffer, fast-forward emits no unplayable host audio,
and repeated shutdown releases the process-wide core lease. The combined sanitizer
preset retains leak detection and has no project suppression file.

**Commit SHA:** `34e709f`

## Milestone 38 detail

**Status:** COMPLETE

**Goal:** Run the complete Linux desktop regression suite in clean, reproducible hosted
Debug, Release, and sanitizer environments while retaining a legacy build compatibility
gate.

**Files changed:**

- `.github/workflows/ci.yml`
- `docs/TESTING.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** No product test is duplicated in YAML. The workflow executes all CTest
tests, including unit, core, integration, GUI, fuzz/property, lifecycle, timing, and
stress labels, in each CMake configuration.

**Gate evidence:**

- `actionlint` 1.7.12: passed with no findings.
- Clean CI-preset configure and 314-step build: passed without frontend warnings.
- Complete local CI CTest: passed (61/61), including all 15 headless GUI tests and
  the 20,000-frame stability workload.
- Action references and selected Qt/SDL releases were resolved against their upstream
  repositories; every action is pinned to an immutable full commit ID.
- The first hosted run is verified after this workflow-bearing commit is pushed and its
  result is recorded by milestone 39.

**Acceptance criteria:** Ubuntu 24.04 checks out with read-only permissions, installs
pinned Qt 6.8.3 and SDL 3.4.14 toolchains, builds and tests Debug and Release, repeats
the suite with ASan/UBSan leak detection, builds the inherited Unix libretro target,
uploads useful failure logs, and does not enable proprietary external fixtures.

**Commit SHA:** `013af97`

## Milestone 39 detail

**Status:** COMPLETE

**Goal:** Prove the desktop frontend and complete automated suite build and run as
native 64-bit Windows applications using Microsoft's supported compiler toolchain.

**Files changed:**

- `.github/workflows/ci.yml`
- `desktop/core/CMakeLists.txt`
- `desktop/core/c_api/config.h`
- `desktop/core/src/emulation_worker.cpp`
- `desktop/timing/CMakeLists.txt`
- `desktop/timing/include/genplusgx/timing/host_timer_resolution.h`
- `desktop/timing/src/host_timer_resolution.cpp`
- `tests/core/emulation_worker_test.cpp`
- `tests/unit/per_game_settings_test.cpp`
- `docs/ARCHITECTURE.md`
- `docs/TESTING.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** No product test is duplicated. The Windows Server 2022 matrix executes
all 61 CTest registrations in both Debug and Release using Visual Studio 2022 x64.

**Gate evidence:**

- Milestone 38 hosted run `32908213185`: all four Linux jobs passed; Debug, Release,
  ASan/UBSan, and inherited libretro coverage are green.
- Windows path conversions, temporary-directory use, core compile definitions, dynamic
  library search paths, and multi-configuration CTest invocation were reviewed.
- Hosted run `32908722488` reached the native MSVC build and exposed the upstream
  GNU-style `__inline__` token. A target-local MSVC definition maps it to `__inline`
  without editing authoritative core sources; the same change also scopes CRT warning
  compatibility to the desktop host bridge.
- Corrective run `32909305432` compiled the full core, then exposed a C/C++ ABI boundary
  hidden by ELF symbol behavior: the frontend `config` global was declared before an
  outer C-linkage block. Its frontend-owned header now gives the variable and reset
  function explicit C linkage on every compiler.
- Run `32909748818` built both Windows configurations and passed all 15 GUI tests. Its
  remaining failures identified two test portability defects—a POSIX-only absolute BIOS
  path and a fixed 20 ms frame-replacement sleep—and a real coarse-timer pacing issue at
  the 4.17 ms fast-forward interval.
- Test BIOS paths now derive from a native temporary directory, frame coalescing waits on
  its observable replacement metric, and the Windows worker owns a balanced 1 ms timer
  resolution request. No core algorithm or target rate was relaxed.
- Local Debug, Release, and ASan/UBSan builds and complete CTest suites pass (61/61 in
  each configuration); `actionlint` 1.7.7 reports no findings.
- Hosted run `32910596677` passed all six jobs. Both MSVC x64 configurations built the
  full desktop application and passed 61/61 tests, including 15/15 GUI tests, timing,
  state, persistence, generated-ROM, Sega CD, and long-running stability coverage. The
  Linux Debug, Release, ASan/UBSan, and inherited libretro jobs also remained green.

**Acceptance criteria:** Windows Server 2022 obtains Qt 6.8.3 for MSVC 2022 x64, builds
matching SDL 3.4.14 libraries, compiles the entire application with `/W4`, and runs the
complete Debug and Release suite headlessly. SDL runtime libraries are on `PATH`, failed
runs retain only bounded build/test diagnostics, and no proprietary fixtures are used.

**Commit SHA:** `043a2ee`

## Milestone 40 detail

**Status:** IN PROGRESS

**Goal:** Build and test the complete desktop application natively with Clang on current
macOS 15 Apple Silicon and Intel hosts in both Debug and Release configurations.

**Files changed:**

- `.github/workflows/ci.yml`
- `docs/TESTING.md`
- `docs/DEVELOPMENT_PLAN.md`

**Tests added:** No product test is duplicated. Four native macOS jobs each execute all
61 CTest registrations, including the complete headless GUI, core, integration,
filesystem, timing, generated-fixture, and stability suites.

**Gate evidence:**

- GitHub's current runner-image inventory was checked before selecting explicit
  `macos-15` arm64 and `macos-15-intel` x86-64 labels; the deprecated macOS 14 image and
  moving `macos-latest` alias are not used.
- Qt 6.8.3 is installed as the host-native package, SDL 3.4.14 is built natively for each
  runner, and all third-party actions retain immutable commit pins.
- Hosted run `32911157563` configured both Apple Silicon variants, then exposed a
  libchdr-only feature-test conflict: strict `_POSIX_C_SOURCE` hides BSD integer aliases
  required by Apple's `sysctl` headers. That Linux-only definition is now excluded on
  Apple without changing vendored sources. The same Clang build identified and corrected
  a safe-size-to-iterator-difference conversion in the frontend video exchange.
- Corrective run `32911618611` built both Apple Silicon configurations and passed 59/61
  tests before showing libc++ condition-variable deadline oversleep in the two real-time
  pacing workloads. Active pacing now releases the queue lock, uses the standard
  monotonic `sleep_until`, and checks commands before each frame. It remains non-busy
  with command latency bounded to one native frame; paused and idle waits stay directly
  interruptible.
- Run `32911961059` confirmed that the standard sleep itself is heavily coalesced on the
  hosted Apple Silicon worker. The platform timing service now converts the remaining
  steady-clock duration into an absolute Mach deadline and repeats interrupted waits;
  a portable unit check guards against early return and unbounded oversleep.
- Local Debug, Release, and ASan/UBSan suites remain green (61/61 each) after the shared
  timing portability fix. Static workflow validation and hosted verification are
  pending this workflow-bearing commit.

**Acceptance criteria:** Apple Silicon and Intel runners configure with Clang/Ninja,
build the full `.app` target and every test, execute Debug and Release headlessly, and
retain bounded failure diagnostics without proprietary fixtures or signing credentials.

**Commit SHA:** recorded by milestone 41
