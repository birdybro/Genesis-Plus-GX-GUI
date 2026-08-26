# Final Test Report

This report records the Genesis Plus GX GUI 0.1.0 release-candidate verification
performed on 2026-08-26 (America/Denver).

## Candidate identity

- Tested implementation commit: `4d2e7d086d6dd0c0a1adb8aa5efbcd8783dca42f`
- Branch: `master`
- Application/package version: `0.1.0`
- Local host: CachyOS Linux x86-64, GCC 16.1.1, CMake 4.4.2, Ninja 1.13.2,
  Qt 6.11.1, and SDL 3.4.14
- Final report/ledger closure: the commit containing this file; use
  `git log -1 -- docs/FINAL_TEST_REPORT.md` to resolve it without embedding a circular
  SHA in that commit

No golden reference changed during final verification. The final hardening pass made
multi-file CUE identity cover the validated sheet and every referenced track, reused
that identity for metadata, saves, states, cheats, overrides, and library records,
suppressed library rows for owned raw tracks, and made live backup/state/metadata/library
hashing cooperatively cancellable during shutdown. Earlier bounded user-visible runtime
failure and aggregate shutdown-status guarantees remain intact.

## Build configurations tested

Every local configuration was rebuilt with `--clean-first`. Newly authored frontend
code produced no compiler warning.

| Configuration | Build | CTest | Result |
| --- | --- | --- | --- |
| Debug | C++20, Qt Widgets/OpenGL, SDL3, SQLite, libchdr | 74/74 | Passed |
| Release | Optimized native x86-64 | 74/74 | Passed |
| ASan + UBSan | Debug instrumentation, leak detection | 74/74 | Passed; no finding |
| Legacy libretro | `Makefile.libretro`, Unix Release | Build/link/clean | Passed; two inherited `const`-qualifier warnings only |

All three CMake suites include legal generated cartridge, disc, and firmware inputs;
core lifecycle, adapter, persistence, and save-state paths; bounded parser/property
corpora; worker lifecycle; semantic GUI workflows; actual-process startup/shutdown; and
the accelerated 20,000-frame stability test. No suppression was added for project code.

## Test totals

CTest registers 74 distinct tests:

| Named family | Count |
| --- | ---: |
| Infrastructure | 7 |
| Core | 18 |
| Integration | 1 |
| Unit | 30 |
| GUI/smoke | 18 |
| **Total** | **74** |

Tests carry overlapping labels because end-to-end workflows intentionally cross
layers. Label counts are 47 `unit`, 19 `core`, 28 `integration`, and 18 `gui`. Focused
coverage also includes persistence (24), fixtures (22), concurrency (13), settings
(12), input (9), video (6), audio (5), timing (4), release (4), fuzz/property (3), and
packaging (2).

## Operating-system CI matrix

Continuous Integration run
[`32937516898`](https://github.com/birdybro/Genesis-Plus-GX-GUI/actions/runs/32937516898)
completed successfully against the exact tested implementation SHA:

| Hosted job | Configuration | Result |
| --- | --- | --- |
| Ubuntu x86-64 | Debug | Passed |
| Ubuntu x86-64 | Release + portable package | Passed |
| Ubuntu x86-64 | ASan + UBSan | Passed |
| Ubuntu x86-64 | Legacy libretro | Passed |
| Windows MSVC x64 | Debug | Passed |
| Windows MSVC x64 | Release + portable ZIP | Passed |
| macOS Apple Silicon | Debug | Passed |
| macOS Apple Silicon | Release + app/ZIP/DMG | Passed |
| macOS Intel x86-64 | Debug | Passed |
| macOS Intel x86-64 | Release + app/ZIP/DMG | Passed |

Each hosted Debug/Release job ran all 74 registered tests. Package jobs staged and
verified native runtimes before uploading artifacts. The completed native packaging
run `32918267812` and non-publishing release rehearsal `32919521105` independently
passed all supported hosts; the rehearsal assembled and reverified checksums but
created neither a tag nor a GitHub release.

## Packaging results

The local Release install was staged without a deployment warning in a fresh temporary
root, verified by `cmake/VerifyPackage.cmake`, and exercised with an installed
`--version` process smoke. The tree contains 45 files and no cartridge, disc, firmware,
SRAM, BRAM, or state payload. CPack produces the versioned
`Genesis-Plus-GX-GUI-0.1.0-linux-x86_64.tar.gz` and a neighboring SHA-256 file. The
closure archive's digest is intentionally not embedded in this packaged report because
doing so would recursively change the archive.

Hosted packaging produces a Windows x86-64 portable ZIP, Linux x86-64 TGZ, macOS arm64
ZIP and unsigned DMG, and macOS x86-64 ZIP and unsigned DMG. Runtime verification
requires the executable, Qt platform/runtime libraries, SDL3, and relevant plugins
before any artifact can upload. Release publication remains guarded by an authorized
matching `v0.1.0` tag and was not performed.

## Final feature checklist

- [x] SG-1000, Mark III, Master System, Game Gear, Genesis/Mega Drive, and Sega CD/Mega
  CD run through the separated desktop adapter and owner-thread emulation worker.
- [x] Dynamic high-DPI OpenGL/software video, aspect/integer scaling, overscan, filters,
  interlace, Game Gear viewport, fullscreen, runtime FPS, and native PNG screenshots.
- [x] Bounded stereo audio, core mixing options, device/latency selection, live
  transactional reconfiguration, instrumentation, pause, and disconnect recovery.
- [x] Keyboard and SDL3 controllers, hot-plug, eight player assignments, button/axis
  capture, deadzones, profiles, specialized devices, multitaps, and configurable
  conflict-checked hotkeys including independent fast-forward hold/toggle.
- [x] Open/replace/close, drag/drop, command line, strict bounded CUE preflight,
  descriptive malformed-file errors, and persistent recent-game history.
- [x] Atomic identity-keyed cartridge SRAM, Sega CD internal BRAM/RAM cartridge,
  automatic load/flush, platform-standard application-data paths, and composite CUE
  identities covering the sheet plus every validated track without path dependence.
- [x] State slots 0-9, quick operations, timestamps, delete, corruption/wrong-game
  rejection, thumbnails, and deterministic restoration.
- [x] All eight supported regional firmware slots, validation, CUE/BIN/ISO/CHD, CDDA,
  disc change/eject, and missing-firmware errors without bundled firmware.
- [x] Versioned global settings and migration, a unified eight-page Preferences center,
  sparse per-game overrides, themes, accessibility, metadata, cheats, diagnostics, and
  privacy-filtered structured logs.
- [x] Recoverable asynchronous SQLite game library with scanning, CUE-owned track
  suppression, search/filter/sort, favorites, play history, launch, and user-provided
  local artwork.
- [x] Visible bounded runtime failures and deterministic shutdown ordering that flushes
  core-owned saves, joins every worker, releases bounded exchanges, and reports cleanup
  failure through logs and process status.
- [x] Windows, Linux, Apple Silicon macOS, and Intel macOS builds; package/checksum and
  guarded release workflows; complete user, developer, architecture, testing, and legal
  documentation.

## Adversarial review

Production frontend, tests, CMake, workflows, and documentation were searched for
`TODO`, `FIXME`, `XXX`, `HACK`, `stub`, `placeholder`, `not implemented`, `abort()`, and
`assert(false)`. Authored-production matches are Qt/API identifiers or normal UI search
placeholder text; test/documentation matches describe generated negative fixtures,
deliberate states, the marker audit itself, or the no-commercial-screenshot policy.
Inherited core, legacy-platform, and vendored-library markers remain outside the desktop
frontend boundary and were not altered merely to cosmetically clear the search. There
are no skipped or disabled CTest/Qt tests. The two `WILL_FAIL` registrations are negative
tests proving malformed and mismatched release tags are rejected. Every production menu
action has a connection and stable object name.

The tree was also checked for unbounded command/video/audio storage, current-directory
save names, workstation paths, proprietary game/firmware files, accidental build
artifacts, secrets, hard-coded platform separators, unhandled actions, stale local
documentation links, and shutdown races. Documentation/package validation passed;
GitHub accepted and executed both workflows on every declared host; the inherited
libretro output was cleaned; ignored output is confined to intentional build/package
directories.

## Known limitations and optional tests

- No commercial ROM, proprietary Sega BIOS, or copyrighted box art is distributed or
  fetched. Real Sega CD boot testing needs a user-supplied regional BIOS and is an
  optional external-fixture suite; CI validates the frontend path with generated legal
  firmware and disc fixtures.
- Controller and audio hot-plug behavior is deterministically tested with injected SDL
  events and the SDL dummy audio driver. Maintainers should still smoke-test target
  hardware and vendor drivers before a public release.
- macOS artifacts are unsigned development builds. Signing, hardened runtime,
  notarization, and any Windows installer signing require project-owned credentials.
- Linux ships a relocatable TGZ rather than an AppImage and intentionally relies on the
  CI distribution's base graphics, window-system, C/C++ runtime, and libc libraries.
- Archive formats are offered only when the authoritative core/build loader supports
  them; this build does not claim ZIP loading. Physical optical drives, netplay,
  achievements, cloud sync, online scraping/downloading, TAS/debugger tooling, and
  streaming remain intentionally outside scope.

These limitations do not leave an advertised control inert and do not weaken the
defined standalone-emulator workflows.
