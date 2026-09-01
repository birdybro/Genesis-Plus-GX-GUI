# Final Test Report

This report records the Genesis Plus GX GUI 0.1.1 release-candidate verification,
Linux startup correction, tagged-release audits, and the post-release Libretro shader
debugger, and rewind verification through 2026-08-31 (America/Denver).

## Candidate identity

- Tested implementation: the current commit containing this report; use
  `git log -1 -- docs/FINAL_TEST_REPORT.md` to resolve it without embedding a circular
  SHA
- Exact shader/package implementation baseline:
  `1a16da87911e9cabe2bc33cea5222946f69444d8`
- Exact debugger implementation and hosted evidence baseline:
  `2799b3c88572314ac576af34c3b9fd06c8666467`
- Exact rewind implementation and hosted evidence baseline:
  `e44d4c2e6aeee926603e4318b28641888ada4909`
- Branch: `master`
- Application/package version: `0.1.1`
- Local host: CachyOS Linux x86-64, GCC 16.1.1, CMake 4.4.2, Ninja 1.13.2,
  Qt 6.11.1, and SDL 3.4.14
- Final report/ledger closure: the commit containing this file; use
  `git log -1 -- docs/FINAL_TEST_REPORT.md` to resolve it without embedding a circular
  SHA in that commit

No emulator golden reference changed during final verification. The Linux correction deploys
Qt's XCB EGL/GLX integrations and preflights a usable context/surface before creating
the accelerated child widget, so unavailable OpenGL now preserves the complete shell
through the software renderer. The final hardening pass also made
multi-file CUE identity cover the validated sheet and every referenced track, reused
that identity for metadata, saves, states, cheats, overrides, and library records,
suppressed library rows for owned raw tracks, and made live backup/state/metadata/library
hashing cooperatively cancellable during shutdown. Earlier bounded user-visible runtime
failure and aggregate shutdown-status guarantees remain intact.

The post-release video layer now dynamically loads the pinned librashader 0.12.0
OpenGL runtime, ships an original adjustable CRT preset, and supports modern Libretro
Slang `.slangp` chains without changing core output. The real OpenGL regression found
and fixed an incomplete one-level texture that produced black shader samples. Global
and per-game settings, bounded preset/parameter validation, runtime fallback, PAL/NTSC
uniform timing, packaging, license notices, and GUI controls are included.

A subsequent native Wayland investigation used the user-supplied Phantasy Star IV ROM
in place without copying it into the tree. Before correction, the core ran at 59.8 FPS
and the OpenGL framebuffer regression passed, while an actual KDE Wayland compositor
capture showed a solid-black game area; XCB and forced software captures showed the
expected image. The frontend now establishes its OpenGL 3.3 core default format before
`QApplication`, allowing Qt's backing-store and `QOpenGLWidget` contexts to share.
Actual native Wayland captures now show both normal and built-in CRT output. The real
shader regression verifies that pre-application contract, and post-fix Release, Debug,
and ASan/UBSan suites each pass 77/77.

A subsequent orientation regression used the same user-supplied game in place. The
previous final pass flipped every successful librashader output a second time; an
asymmetric render assertion reproduced the defect before the code changed. The final
pass now presents raw and shader textures with one consistent coordinate convention.
The required real-OpenGL test covers 36 presentation combinations and ten CRT parameter
endpoints. A separate optional native-Wayland acceptance run passed 113 real-game
runtime cases spanning core video, audio, input devices, presentation, and compatible
system reloads, generated 49 temporary comparison PNGs, and found every result upright
both automatically and by contact-sheet review. The actual desktop process separately
loaded Phantasy Star IV with built-in CRT at 59.9 FPS. No ROM or derived image entered
the repository. The resulting code passes 77/77 in Release, Debug, and leak-detecting
ASan/UBSan builds plus 75/75 with shader support disabled. The same 113-case native
Wayland workload passes with ASan/UBSan address and undefined-behavior instrumentation;
the required real-context shader test supplies the leak-detecting GPU gate.

The opt-in debugger is hidden for new and migrated configurations and sends every
request through the bounded emulation-owner queue. It exposes immutable CPU, RAM, VDP,
sound, and input snapshots; paused edits; validated states; typed RAM search/watch; and
68000/Z80 frame-boundary breakpoints. A live GUI/worker regression executes generated
Genesis and Master System programs, installs breakpoints through the widgets, observes
real worker pauses, and reads their written RAM. Core coverage additionally executes
SG-1000 and Game Gear programs. That coverage found and fixed the 8-bit Z80 RAM view
initially selecting the inactive Genesis sound-CPU buffer instead of console work RAM.
Native validation subsequently found and fixed Apple Clang and MSVC conversion
warnings, plus an optimized Intel-only crash caused by assuming 16-byte alignment while
reading inherited four-byte-aligned CRAM/VSRAM. The scalar capture fix passed each
formerly crashing optimized-Clang debugger test 50 consecutive times. A complete
successful-run log audit also removed duplicate core-adapter entries from four macOS
GUI-test link commands.

Bounded rewind now captures raw core states only on the emulation-owner thread, evicts
oldest states at a configurable 16–1024 MiB payload cap, restores the frontend frame
counter with the core state, suppresses audio while moving backward, and never expands
the bounded video, audio, command, or event exchanges. Fast-forward is mutually
exclusive. Lifecycle changes, setting changes, disc operations, cheats, debugger
writes, and explicit state restores invalidate incompatible history. A persisted
settings dialog, Backspace hold hotkey with schema migration/conflict checks, menu
toggle, help, and privacy-safe diagnostics complete the user-facing workflow.

Automatic session resume is opt-in and uses a dedicated state-kind marker inside the
existing 128-byte checked envelope. Clean shutdown pauses and captures on the core
owner, waits for atomic state-storage confirmation, and records the absolute game path
only after success. Startup gives explicit command-line input precedence and otherwise
waits for game identity/hardware activation before restoring and starting playback.
Three new tests cover the versioned settings model, GUI editor, and a real four-launch
desktop process workflow; state-manager/service tests also cover the dedicated
checkpoint. Debug, Release, and ASan/UBSan suites each pass 88/88 locally; the
shader-disabled graph passes 86/86. A fresh warning-as-error Clang 22 build passes
88/88 after the first hosted attempt identified and corrected two Apple Clang unused
lambda-capture diagnostics. A simultaneously loaded local matrix exposed and corrected
a test-only rewind-metrics observation race; the command event remains the authoritative
invalidation result. A second hosted attempt compiled past that issue and exposed a
macOS process-test timer that began before asynchronous game/state readiness plus a
duplicate test-only static-library link. The ready-gated test seam and dependency graph
were corrected; cross-platform hosted verification of the corrections is pending.

## Build configurations tested

The primary Debug, Release, and sanitizer configurations were rebuilt against the exact
implementation; the shader-disabled graph was separately reconfigured and rebuilt with
warnings as errors. Newly authored frontend code produced no compiler warning.

| Configuration | Build | CTest | Result |
| --- | --- | --- | --- |
| Debug | C++20, Qt Widgets/OpenGL, SDL3, SQLite, libchdr, librashader | 88/88 | Passed |
| Release | Optimized native x86-64 | 88/88 | Passed |
| ASan + UBSan | Debug instrumentation, leak detection | 88/88 | Passed; no finding |
| Shaders disabled | Warning-gated Debug with `GENPLUSGX_ENABLE_LIBRETRO_SHADERS=OFF` | 86/86 | Passed |
| Clang 22 | Warning-gated Debug | 88/88 | Passed |
| Legacy libretro | `Makefile.libretro`, Unix Release | Build/link/clean | Passed; warning-clean with truncation/qualifier gates |

All three CMake suites include legal generated cartridge, disc, and firmware inputs;
core lifecycle, adapter, persistence, and save-state paths; bounded parser/property
corpora; worker lifecycle; semantic GUI workflows; actual-process startup/shutdown; and
the accelerated 20,000-frame stability test. No suppression was added for project code.

## Test totals

The default shader-enabled build registers 88 distinct tests:

| Named family | Count |
| --- | ---: |
| Infrastructure | 8 |
| Core | 20 |
| Integration | 1 |
| Unit | 35 |
| GUI/smoke | 24 |
| **Total** | **88** |

Tests carry overlapping labels because end-to-end workflows intentionally cross
layers. Label counts are 56 `unit`, 21 `core`, 35 `integration`, and 24 `gui`. Focused
coverage also includes persistence (28), fixtures (26), concurrency (16), settings
(16), input (9), video (8), audio (6), timing (4), rewind (4), state (8), release (4),
fuzz/property (3), shader (2), and packaging (3).

`unit.shader_configuration` covers preset modes, path/size bounds, malformed data,
parameter count/name/value validation, real built-in metadata, undeclared overrides,
and declared ranges. `gui.libretro_shader_render` uses a real OpenGL 3.3 context to
sample an original input texture through librashader, then captures the actual
`QOpenGLWidget` and proves the built-in CRT output is non-black and materially differs
from normal presentation. Linux runs that test under Xvfb/GLX. LeakSanitizer suppresses
only process-global allocations rooted in the host NVIDIA GL and DBus libraries; all
project, librashader, Mesa, ASan, and UBSan frames remain unsuppressed and fatal.

Option-domain coverage is also explicit rather than inferred: core tests execute all
13 video values, 35 audio enumerations/endpoints, 12 emulated device types, and 24
system values. UI inventory checks require every corresponding choice, range, and all
30 configurable emulator hotkeys. Existing workflow tests continue to cover host audio
mute/volume/device/latency, keyboard/controller event paths, persistence, states,
library, cheats, BIOS, Sega CD, diagnostics, themes, and clean lifecycle behavior.
Four debugger-labeled tests cover the analysis model, core/worker bridge, semantic GUI,
and live GUI/worker/core workflow; the latter runs both CPU families.

Exact option-regression CI run
[`33125797973`](https://github.com/birdybro/Genesis-Plus-GX-GUI/actions/runs/33125797973)
passes all ten jobs against commit `5f09458f9e25c08fb87a0fe627f034377b996150`.
All nine CMake jobs register 77 tests: Linux and both macOS architectures pass 77/77;
Windows passes 76 and capability-skips only the documented real-OpenGL test because
the hosted software context is below desktop OpenGL 3.3. Linux ASan/UBSan and the
legacy warning gate pass, and every Release job verifies and uploads its native package.
The full 12,719-line log corpus was downloaded and scanned; it contains no compiler or
linker warning, sanitizer signature, failed test, timeout, runtime error, invalid path,
or packaging failure. Normal Windows pthread feature probes, absent optional Vulkan
headers, and omission of the unused OpenSSL deployment plugin are non-actionable
capability/dependency messages rather than application defects.

### Exact debugger cross-platform verification

Continuous Integration run
[`33421889014`](https://github.com/birdybro/Genesis-Plus-GX-GUI/actions/runs/33421889014)
passes all ten jobs against exact commit
`2799b3c88572314ac576af34c3b9fd06c8666467`. Linux Debug, Release, and ASan/UBSan;
macOS arm64 Debug/Release; and macOS x86-64 Debug/Release each pass 81/81. Windows MSVC
Debug/Release pass every supported test and capability-skip only
`gui.libretro_shader_render` because the hosted software context is below desktop
OpenGL 3.3. The legacy libretro warning gate also builds, links, and cleans.

All 13,564 log lines were inspected. There is no compiler or linker warning, sanitizer
signature, crash, runtime error, failed test, timeout, invalid generated path, or
deployment failure. The only matches requiring classification are normal Windows
pthread feature probes, absent optional Vulkan headers, source text for fail-closed
Visual C++ runtime checks that did not execute, and `windeployqt` intentionally omitting
the unused OpenSSL backend. This final corpus specifically confirms that the former
Intel Release debugger crashes and duplicate-library linker warnings are gone.

### Exact rewind cross-platform verification

Continuous Integration run
[`33465978685`](https://github.com/birdybro/Genesis-Plus-GX-GUI/actions/runs/33465978685)
passes all ten jobs against exact commit
`e44d4c2e6aeee926603e4318b28641888ada4909`. All nine CMake jobs register 85 tests.
Linux Debug/Release/ASan+UBSan and macOS arm64/x86-64 Debug/Release execute and pass
85/85. Windows MSVC Debug/Release pass all supported tests and capability-skip only
`gui.libretro_shader_render` because the hosted software context is below desktop
OpenGL 3.3. The legacy libretro build, link, and clean gate passes.

All 13,848 combined log lines were inspected. No compiler/linker warning, sanitizer
signature, crash, runtime error, timeout, failed test, invalid generated path, or
deployment failure remains. `core.rewind_worker`, `unit.rewind_buffer`,
`unit.rewind_settings`, and `gui.rewind_settings` each pass in all nine CMake jobs.
Normal Windows pthread probes, absent optional Vulkan headers, checkout default-branch
hints, fail-closed script source text, and artifact uploader policy text are benign and
did not execute as failures.

## Operating-system CI matrix

Continuous Integration run
[`33098836359`](https://github.com/birdybro/Genesis-Plus-GX-GUI/actions/runs/33098836359)
completed successfully against the exact shader and native-package implementation
baseline.

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

Each hosted CMake job registered all 77 tests. Linux and both macOS architectures pass
77/77, including real OpenGL/librashader rendering. Windows passes 76 and reports only
the intentional capability skip for that render test because the hosted software
OpenGL context is below desktop 3.3; the application retains normal unshaded video in
that condition. Package jobs staged and verified native runtimes before uploading
artifacts. Full logs were read for all ten jobs; there is no compiler/CMake/linker
warning, sanitizer signature, runtime error, failed test, timeout, invalid generated
path, dependency fallback warning, or deployment failure. The stricter inherited
libretro build also links and cleans without a diagnostic.

## Packaging results

The local Release install was staged without a deployment warning in a fresh temporary
root, verified by `cmake/VerifyPackage.cmake`, and exercised with an installed
`--version` process smoke. The tree contains 55 files, including the matched XCB QPA
and ICU runtimes, XCB EGL and GLX integration plugins, librashader runtime, built-in CRT
preset/source, shader license, and shader guide, with no cartridge, disc, firmware,
SRAM, BRAM, or state payload. Every copied Qt plugin has a package-relative RUNPATH;
the real XCB smoke clears `LD_LIBRARY_PATH`, and loader tracing resolves XCB QPA from
inside the extracted archive.
CPack produces the versioned
`Genesis-Plus-GX-GUI-0.1.1-linux-x86_64.tar.gz` and a neighboring SHA-256 file. The
closure archive's digest is intentionally not embedded in this packaged report because
doing so would recursively change the archive.

Hosted packaging produces a Windows x86-64 portable ZIP, Linux x86-64 TGZ, macOS arm64
ZIP and unsigned DMG, and macOS x86-64 ZIP and unsigned DMG. Runtime verification
requires the executable, Qt platform/runtime libraries, SDL3, relevant plugins, and on
Windows Microsoft's official Visual C++ x64 Redistributable installer before any
artifact can upload. Release publication remains guarded by an authorized matching
version tag.

Exact debugger-evidence run `33421889014` produced four current workflow artifacts:
Linux x86-64 (39,876,276 bytes), Windows x86-64 (52,448,121 bytes), macOS arm64
(63,684,037 bytes), and macOS x86-64 (65,291,661 bytes). These are CI artifacts for
tester consumption, not a newly authorized version tag or GitHub Release.

Exact rewind-evidence run `33465978685` supersedes those tester artifacts with Linux
x86-64 (39,885,530 bytes), Windows x86-64 (52,462,222 bytes), macOS arm64 (63,718,652
bytes), and macOS x86-64 (65,316,304 bytes). Each native Release job verified its
self-contained staged layout and generated neighboring package checksums before the
artifact upload.

All four exact-commit artifact families were downloaded after run `33098836359`. The
six individual SHA-256 manifests verify. Extracted executables identify as Windows
PE32+ x86-64, Linux ELF x86-64, macOS Mach-O arm64, and macOS Mach-O x86-64. Required
Qt platform/SQLite plugins and SDL3 are present; Linux has both XCB GL integrations and
its matching Qt XCB/ICU runtimes; Windows has the official redistributable while
omitting unused DirectX compiler DLLs. Every archive contains the built-in CRT preset,
Slang source, librashader runtime, and MPL-2.0 license. An archive-name audit found no
game, disc image, BIOS, SRAM, BRAM, or state payload.

## Tagged-release log audit

The authorized `v0.1.0` tag triggered Release workflow run
[`33019452922`](https://github.com/birdybro/Genesis-Plus-GX-GUI/actions/runs/33019452922).
Linux x86-64, Windows MSVC x64, macOS arm64, and macOS x86-64 each built, ran all 74
tests with zero failed/skipped/disabled cases, verified their staged runtimes, and
uploaded package artifacts. The Linux ASan/UBSan suite passed 74/74 with no finding,
the legacy libretro regression passed, and the assembly job reverified all six archives
and individual checksums. Final publication alone failed when GitHub's release-asset
endpoint returned HTTP 400 after a five-minute upload attempt for the 17 MiB Linux TGZ.
The verified 136 MiB compressed release candidate remained available as a workflow
artifact; package size and validation were not the cause.

Successful-job logs were audited in full rather than relying on job conclusions.
Windows reported frontend/test C4244, C4324, C4459, and C4127 warnings; both macOS
architectures reported corresponding signedness warnings and duplicate static-library
link entries. Apple Silicon additionally exposed inherited core/common-symbol alignment
and bundled decoder diagnostics. The frontend conversions and shadowing were corrected,
intentional cache-line/MSVC ABI padding was documented and narrowly scoped, redundant
direct links were removed, and Apple core compilation now uses `-fno-common` with
target-local handling for understood inherited diagnostics. CI and Release builds now
enable a warning-as-error option only for authored frontend/test targets. Publication
now creates a draft, uploads and retries each verified asset independently, and promotes
the release only after the complete set succeeds. Because the published `v0.1.0` tag is
immutable, the corrected candidate advances to `v0.1.1` rather than moving that tag.

A subsequent exact warning-gated CI run
[`33021653673`](https://github.com/birdybro/Genesis-Plus-GX-GUI/actions/runs/33021653673)
passed all ten jobs on Linux, Windows, macOS arm64, and macOS x86-64. Reading those logs
and downloading the Windows artifact found one remaining inherited libchdr typedef
warning on both Apple architectures and an incomplete Windows compiler-runtime story:
Qt warned that the Visual Studio location was unavailable, and the ZIP contained
neither `vc_redist.x64.exe` nor compiler runtime DLLs. The final package closure scopes
the understood vendor typedef collision to `genplusgx_chd`, locates and installs the
official redistributable, suppresses only unused D3D/DXC deployment probes, and adds a
synthetic package regression that fails if the redistributable is absent. Hosted
confirmation of those changes is provided by the next audited run.

Package-closure CI run
[`33023291621`](https://github.com/birdybro/Genesis-Plus-GX-GUI/actions/runs/33023291621)
then passed all ten native jobs and every 75-test CMake suite. The Apple typedef and
Windows deployment warnings were absent. Downloaded Windows archive inspection
confirmed its checksum, official redistributable, Qt platform runtime, and SDL3, with
no unused DirectX compiler DLL. The remaining four GCC diagnostics exposed a real
legacy libretro risk: per-game Sega CD backup paths could silently truncate into a
256-byte buffer and collide. The release candidate now formats those paths into a
larger dedicated buffer, rejects overflow explicitly, fixes the two old qualifier
diagnostics, and promotes both warning classes to errors in CI and release builds.
Exact hosted confirmation is run `33024729781`: all ten jobs pass, every CMake job is
75/75, the legacy build is warning-clean, and all six native package checksums verify.

## Linux startup correction verification

The previously staged portable executable was launched on the real KDE
Wayland/XWayland desktop, not under Qt's offscreen test platform. Its captured window
was entirely black below the title bar, while its structured log showed missing GLX
and EGL integration, repeated `QOpenGLWidget` context failures, and a false OpenGL
selection. Running the same executable with the existing software override restored
all seven menus, the empty-game prompt, and the status bar, isolating the failure to
renderer creation.

The corrected installed executable was then exercised twice on that desktop. With its
deployed XCB integrations it initialized OpenGL 4.6 and a full-window capture showed
the File through Help menus, centered open/drop prompt, and status. In a disposable
copy with both integration plugins deliberately removed, context preflight failed
before `QOpenGLWidget` construction, the log selected `Qt software painter`, the normal
shell remained available, and shutdown completed. The package verifier now makes the
first condition mandatory, while renderer preflight protects users from missing or
broken graphics drivers. Debug, Release, and ASan/UBSan each pass 74/74 after the
correction; the sanitizer suite reports no finding.

## Final feature checklist

- [x] SG-1000, Mark III, Master System, Game Gear, Genesis/Mega Drive, and Sega CD/Mega
  CD run through the separated desktop adapter and owner-thread emulation worker.
- [x] Dynamic high-DPI OpenGL/software video, aspect/integer scaling, overscan, filters,
  interlace, Game Gear viewport, fullscreen, runtime FPS, native PNG screenshots,
  adjustable built-in CRT output, and modern Libretro Slang preset chains.
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
- [x] Opt-in automatic clean-shutdown session checkpoint, identity-checked restore,
  command-line precedence, explicit-close clearing, and safe normal-launch fallback.
- [x] Bounded owner-thread rewind with configurable cadence/memory, hold/toggle UI,
  conflict-checked hotkey migration, muted reverse playback, and state invalidation.
- [x] All eight supported regional firmware slots, validation, CUE/BIN/ISO/CHD, CDDA,
  disc change/eject, and missing-firmware errors without bundled firmware.
- [x] Versioned global settings and migration, a unified eight-page Preferences center,
  sparse per-game overrides, themes, accessibility, metadata, cheats, diagnostics, and
  privacy-filtered structured logs.
- [x] Hidden-by-default native debugger with CPU/RAM/VDP/sound/input inspection, paused
  edits, RAM search/watch, validated states, and bounded frame-boundary breakpoints.
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
are no disabled CTest/Qt tests. The hosted Windows software renderer capability-skips
only the real OpenGL 3.3 shader-render test; that same test executes on Linux and both
macOS architectures, and Windows' unsupported-context fallback is covered separately.
The two `WILL_FAIL` registrations are negative tests proving malformed and mismatched
release tags are rejected. Every production menu action has a connection and stable
object name.

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
- The previously supplied external-ROM mount was present but unpopulated during the
  debugger hardening run, so that optional workload was not repeated. The required live
  debugger test uses legal generated 68000 and Z80 programs through the same production
  GUI/worker/core route; earlier real-ROM option acceptance remains recorded above.
- Controller and audio hot-plug behavior is deterministically tested with injected SDL
  events and the SDL dummy audio driver. Maintainers should still smoke-test target
  hardware and vendor drivers before a public release.
- macOS artifacts are unsigned development builds. Signing, hardened runtime,
  notarization, and any Windows installer signing require project-owned credentials.
- Linux ships a relocatable TGZ rather than an AppImage and intentionally relies on the
  CI distribution's base graphics, window-system, C/C++ runtime, and libc libraries.
- Archive formats are offered only when the authoritative core/build loader supports
  them; this build does not claim ZIP loading. Physical optical drives, netplay,
  achievements, cloud sync, online scraping/downloading, instruction-level stepping,
  an external debugger server, TAS tooling, and streaming remain intentionally outside
  scope.

These limitations do not leave an advertised control inert and do not weaken the
defined standalone-emulator workflows.
