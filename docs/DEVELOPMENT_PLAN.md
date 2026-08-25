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
| 02 Core library | COMPLETE | Build core independently of SDL main and GUI | core target manifests, desktop OSD bridge | Debug/Release/ASan core link smoke; libretro regression | Static core compiles without Qt dependency | pending |
| 03 Synthetic ROM | PLANNED | Generate legal deterministic test ROM and first headless core test | `tests/fixtures`, core test utilities | generated ROM execution and semantic assertion | Fixture provenance documented; repeatable result | pending |
| 04 Core lifecycle | PLANNED | Adapter init/shutdown/load/unload/reset/frame API | `desktop/core` | lifecycle, invalid transition, repeated load/unload | Deterministic and leak-safe lifecycle | pending |
| 05 Core video | PLANNED | Safe framebuffer and dynamic viewport exposure | core/video adapter | viewport, frame content/hash tests | Complete immutable frame snapshots | pending |
| 06 Core audio | PLANNED | Sample exposure and bounded audio storage | core/audio adapter | deterministic sample and ring-buffer tests | No overflow or unbounded allocation | pending |
| 07 Core input | PLANNED | Neutral input snapshot translated at frame boundary | input model/adapter | controller ROM and mapping tests | Snapshot consumed deterministically | pending |
| 08 Persistence | PLANNED | Platform paths, safe names, SRAM/BRAM atomic files | `desktop/persistence` | path, collision, corruption, atomic round-trip | No current-directory/user-data leakage in tests | pending |
| 09 Save states | PLANNED | Metadata wrapper, slots, validation | state manager | round-trip, corruption, wrong-game, replacement | Raw payload preserved; unsafe states rejected | pending |
| 10 Qt shell | PLANNED | QApplication, MainWindow, menus/status/canvas/About | `desktop/app`, `desktop/ui`, resources | offscreen startup and menu semantic tests | Native shell starts headlessly | pending |
| 11 Emulation worker | PLANNED | Command queue, worker lifecycle, safe shutdown | worker/coordinator | concurrency, queue bounds, repeated start/stop | No core calls on GUI thread | pending |
| 12 Display widget | PLANNED | Present synthetic/core frames and handle resize | video widget | integration and shown-frame tests | Stable reusable texture path | pending |
| 13 Video scaling | PLANNED | Native, 4:3, stretch, integer and filter modes | video geometry/settings | property/unit and GUI settings tests | Correct letterbox/high-DPI calculations | pending |
| 14 Audio playback | PLANNED | SDL3 output, device lifecycle, pause/resume | `desktop/audio` | null-device init and buffer integrity | Clean bounded low-latency pipeline | pending |
| 15 Timing/pacing | PLANNED | NTSC/PAL/CD pacing, FF, pause, frame advance | timing service | rate tolerance and state tests | Monotonic non-busy pacing | pending |
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

**Commit SHA:** pending milestone commit; to be recorded during Milestone 03.
