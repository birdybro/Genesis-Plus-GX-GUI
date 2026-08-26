# Final Test Report

This report records the Genesis Plus GX GUI 0.1.0 release-candidate verification
performed on 2026-08-25 (America/Denver).

## Candidate identity

- Tested implementation commit: `d84f7eb575e36f4bb6dd5c270c40b351dbd7b047`
- Branch: `master`
- Application/package version: `0.1.0`
- Local host: CachyOS Linux x86-64, GCC 16.1.1, CMake 4.4.2, Ninja 1.13.2,
  Qt 6.11.1, and SDL 3.4.14
- Final report/ledger closure: the commit containing this file; use
  `git log -1 -- docs/FINAL_TEST_REPORT.md` to resolve it without a circular SHA
  embedded in that commit

No golden reference was changed during final verification. The final adversarial pass
found fixed emulator shortcuts and incomplete selected-audio-device recovery as two
substantive acceptance gaps. The candidate adds schema-migrated configurable hotkeys,
cross-domain conflict checks, live action updates, bounded audio hot-plug polling, and
automatic fallback to the default output after an explicitly selected device is
removed.

## Build configurations tested

Every local configuration was cleaned before reconfiguration and compilation. Newly
authored frontend code produced no compiler warnings.

| Configuration | Build | CTest | Result |
| --- | --- | --- | --- |
| Debug | C++20, Qt Widgets/OpenGL, SDL3, SQLite, libchdr | 67/67 | Passed |
| Release | Optimized native x86-64 | 67/67 | Passed |
| ASan + UBSan | Debug instrumentation, leak detection | 67/67 | Passed; no sanitizer findings |
| Legacy libretro | `Makefile.libretro`, Unix Release | Build/link/clean | Passed; two inherited `const`-qualifier warnings only |

The sanitizer suite includes generated cartridge/disc/firmware inputs, core adapter and
save-state paths, bounded parser/property corpora, worker lifecycle, GUI workflows, and
the accelerated 20,000-frame stability test. No suppression was added for project code.

## Test totals

CTest registers 67 distinct tests:

| Named family | Count |
| --- | ---: |
| Infrastructure | 7 |
| Core | 15 |
| Integration | 1 |
| Unit | 29 |
| GUI/smoke | 15 |
| **Total** | **67** |

Tests carry overlapping cross-cutting labels because a core or GUI workflow can also be
an integration or unit-level gate. Label counts are 45 `unit`, 16 `core`, 23
`integration`, and 15 `gui`. Other focused coverage includes persistence (24), fixtures
(19), concurrency (13), settings (10), input (8), video (6), audio (5), timing (4),
release (4), fuzz/property (2), and packaging (2).

## Operating-system CI matrix

The candidate is exercised by Continuous Integration run `32922820386`:

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

Run `32922820386` completed with all ten jobs successful against the exact candidate
SHA. Each hosted Debug/Release job ran the complete 67-test suite; package jobs also
staged and verified native runtimes before uploading artifacts.

The previously completed native packaging run `32918267812` and non-publishing release
rehearsal `32919521105` independently passed all supported hosts. The rehearsal assembled
and reverified every platform checksum, uploaded a release-candidate artifact, skipped
the publication step, and created neither a tag nor a GitHub release.

## Packaging results

The local Release install was staged in a new temporary root and passed runtime
verification plus an installed `--version` smoke test. The installed tree contained 43
files before the final report was added and no cartridge, disc, BIOS, SRAM, BRAM, or
state-file extensions. That pre-report CPack pass produced:

- `Genesis-Plus-GX-GUI-0.1.0-linux-x86_64.tar.gz` — 17,050,452 bytes
- SHA-256: `7e3b742f7da027d91b36c7591124ff4320ef81259dc16566e3218c6eacb48622`

A second install/package/checksum pass includes this report and is the milestone-closure
artifact. Its generated neighboring checksum is deliberately not embedded here because
changing this packaged report would recursively change that archive digest.

Hosted packaging produces the versioned Windows x86-64 portable ZIP, Linux x86-64 TGZ,
macOS arm64 ZIP and unsigned DMG, and macOS x86-64 ZIP and unsigned DMG. Runtime
verification requires the executable, Qt platform/runtime libraries, SDL3, and relevant
native plugins before any artifact can upload. Release publication remains guarded by
an authorized matching `v0.1.0` tag and was not performed.

## Final feature checklist

- [x] SG-1000, Mark III, Master System, Game Gear, Genesis/Mega Drive, and Sega CD/Mega
  CD sessions run through the separated desktop adapter and emulation thread.
- [x] Dynamic video, high-DPI OpenGL/software presentation, aspect/integer scaling,
  overscan, filters, interlace, fullscreen, and native PNG screenshots.
- [x] Bounded stereo audio, mute/volume/core mixing, selectable device/latency,
  underrun/overrun metrics, pause behavior, and device-removal recovery.
- [x] Keyboard and SDL3 controllers, hot-plug, eight assignments, button/axis capture,
  deadzones, profiles, specialized core device selection, and configurable hotkeys.
- [x] Open/replace/close, drag/drop, command line, malformed-file errors, and bounded
  recent-game history for every format supported by this build.
- [x] Atomic identity-keyed cartridge SRAM, Sega CD internal BRAM/RAM cartridge,
  automatic load/flush, and safe platform application-data paths.
- [x] Slots 0–9, quick save/load, timestamps, delete, corruption/wrong-game rejection,
  thumbnails, and deterministic restore.
- [x] Regional BIOS validation, CUE/BIN/ISO/CHD workflows, CDDA, disc change/eject, and
  user-facing missing-firmware errors without bundled or downloaded firmware.
- [x] Versioned global settings and migrations, sparse per-game overrides, themes,
  accessibility, game information, cheats, and privacy-filtered diagnostics/logging.
- [x] Asynchronous recoverable SQLite game library with scanning, search/filter/sort,
  favorites, play history, launch, and user-provided local art.
- [x] Windows, Linux, Apple Silicon macOS, and Intel macOS builds; package/checksum and
  guarded release workflows; complete user/developer/legal documentation.

## Adversarial review

Production frontend, tests, CMake, workflows, and documentation were searched for
`TODO`, `FIXME`, `XXX`, `HACK`, `stub`, `placeholder`, `not implemented`, `abort()`, and
`assert(false)`. Remaining `placeholder` matches describe generated negative fixtures,
release-asset test files, or the intentional no-commercial-screenshot policy; none is an
unfinished production path. There are no skipped or disabled CTest/Qt tests. The two
`WILL_FAIL` registrations are negative tests proving malformed and mismatched release
tags are rejected. Every production menu action has a connection and stable object
name. Per-frame command, video, and audio paths use bounded storage.

The tree was also checked for current-directory save names, workstation paths,
proprietary game/firmware files, build artifacts, secrets, unhandled action entries,
stale documentation links, and workflow syntax. Documentation validation resolved all
local links, actionlint 1.7.7 reported no workflow findings, the inherited libretro
output was cleaned, and ignored output is confined to `build/`.

## Known limitations and optional tests

- No commercial ROM, proprietary Sega BIOS, or copyrighted box art is distributed or
  fetched. Real Sega CD boot testing needs a user-supplied regional BIOS and is an
  optional external-fixture suite; CI validates the frontend path with generated legal
  firmware/disc fixtures.
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
