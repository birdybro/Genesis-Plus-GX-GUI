# Final Test Report

This report records the Genesis Plus GX GUI 0.1.1 release-candidate verification,
Linux startup correction, tagged-release audits, and the post-release Libretro shader,
debugger, rewind, automatic-session-resume, configurable-speed, archive/playlist,
cartridge soft-patch, enhanced save-state, bounded-recording, and run-ahead verification through 2026-09-01
(America/Denver).

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
- Exact automatic-resume implementation and hosted evidence baseline:
  `3cce4030f83c1ee7c057113eab1a811129c02858`
- Exact configurable-speed implementation and hosted evidence baseline:
  `253c04352a762dfaf2c920fa6b107b6a983f4ecd`
- Exact archive/playlist implementation and hosted evidence baseline:
  `3b828babc13e9a620161a556e1984221358b771d`
- Exact cartridge soft-patch implementation and hosted evidence baseline:
  `da80552964855fe5ab7ec17689526b8cabdb7d8c`
- Exact enhanced save-state implementation and hosted evidence baseline:
  `30ed5d7e378c7107db3be844fc80cfd0c96deec2`
- Exact bounded-recording implementation and hosted evidence baseline:
  `59beedd1f9b1e0a08a2ec0e28f0f9293cd29fe20`
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
were corrected. A third run isolated the remaining macOS `/var` versus `/private/var`
alias: the stored path was absolute and identified the same file, but the test required
lexical equality. The workflow now verifies filesystem identity and carries a Unix
directory-symlink regression. Exact hosted run `33472190327` passes all ten jobs and
every registered CTest at that implementation commit. Its first attempt had one
pre-build GitHub API connection failure while the SDL setup action ran on Intel macOS;
retrying the same job and SHA passed dependency setup, compilation, 88/88 tests,
packaging, and upload.

Configurable emulation speed now uses exact rational owner-thread pacing for persisted
normal 50–200%, slow-motion 25–75%, and fast-forward 200–1600% settings. The UI adds
common normal-speed presets, slow-motion hold/toggle controls, an accessible settings
editor, effective-speed status, and privacy-safe diagnostics. Slow motion, fast
forward, and rewind are mutually exclusive, speed-setting commands coalesce in the
bounded queue, and host audio is intentionally silent outside 100% effective speed so
neither slow nor accelerated playback can accumulate stale audio. The input profile
schema migrates existing users to two conflict-checked slow-motion bindings without
changing their prior mappings. Fresh Debug, Release, ASan/UBSan, and Clang 22 suites
each pass 90/90; the shader-disabled graph passes 88/88; Linux staging/package and the
legacy libretro regression pass. Exact hosted run `33477974149` passes every one of its
ten Linux, Windows, Apple Silicon, and Intel macOS jobs against the implementation SHA;
the complete logs and native artifacts have also passed the audit recorded below.

## Build configurations tested

The primary Debug, Release, and sanitizer configurations were rebuilt against the exact
implementation; the shader-disabled graph was separately reconfigured and rebuilt with
warnings as errors. Newly authored frontend code produced no compiler warning.

| Configuration | Build | CTest | Result |
| --- | --- | --- | --- |
| Debug | C++20, Qt Widgets/OpenGL, SDL3, SQLite, libchdr, librashader | 95/95 | Passed |
| Release | Optimized native x86-64 | 95/95 | Passed |
| ASan + UBSan | Debug instrumentation, leak detection | 95/95 | Passed; no finding |
| Shaders disabled | Warning-gated Debug with `GENPLUSGX_ENABLE_LIBRETRO_SHADERS=OFF` | 93/93 | Passed |
| CHD disabled | Warning-gated Debug with `GENPLUSGX_ENABLE_CHD=OFF` | 95/95 | Passed |
| Clang 22 | Warning-gated Debug | 95/95 | Passed |
| Legacy libretro | `Makefile.libretro`, Unix Release | Build/link/clean | Passed; warning-clean with truncation/qualifier gates |

All three CMake suites include legal generated cartridge, disc, and firmware inputs;
core lifecycle, adapter, persistence, and save-state paths; bounded parser/property
corpora; worker lifecycle; semantic GUI workflows; actual-process startup/shutdown; and
the accelerated 20,000-frame stability test. The two recording-labeled tests cover the
bounded writer in isolation and through the real core/worker path. No suppression was
added for project code.

## Test totals

The default shader-enabled build registers 95 distinct tests:

| Named family | Count |
| --- | ---: |
| Infrastructure | 8 |
| Core | 20 |
| Integration | 4 |
| Unit | 38 |
| GUI/smoke | 25 |
| **Total** | **95** |

Tests carry overlapping labels because end-to-end workflows intentionally cross
layers. Label counts are 60 `unit`, 24 `core`, 39 `integration`, and 25 `gui`. Focused
coverage also includes persistence (31), fixtures (28), concurrency (16), settings
(18), input (9), video (8), audio (6), timing (6), rewind (4), state (8), release (4),
fuzz/property (4), shader (2), recording (2), and packaging (3).

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
33 configurable emulator hotkeys. Existing workflow tests continue to cover host audio
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

### Exact automatic-resume cross-platform verification

Continuous Integration run
[`33472190327`](https://github.com/birdybro/Genesis-Plus-GX-GUI/actions/runs/33472190327)
passes all ten jobs against exact commit
`3cce4030f83c1ee7c057113eab1a811129c02858`. All nine CMake jobs register and complete
88/88 tests. Linux Debug/Release/ASan+UBSan and macOS arm64/x86-64 Debug/Release
execute the complete supported suite. Windows MSVC Debug/Release retain only the
documented in-test real-OpenGL capability skip on their software renderer. The legacy
libretro warning gate builds, links, and cleans.

The first attempt's Intel macOS Release job could not connect to `api.github.com` while
the SDL setup action queried its releases, before project configuration or compilation.
The failed-job retry used the identical SHA and completed SDL setup, compilation,
88/88 tests, package verification, ZIP/DMG creation, and upload. The final combined
13,950-line log corpus contains no compiler/linker warning, sanitizer signature, crash,
runtime error, timeout, failed test, invalid generated path, or deployment failure.
Normal checkout hints and Windows pthread feature probes are non-actionable.

### Exact configurable-speed cross-platform verification

Continuous Integration run
[`33477974149`](https://github.com/birdybro/Genesis-Plus-GX-GUI/actions/runs/33477974149)
passes all ten jobs against exact implementation commit
`253c04352a762dfaf2c920fa6b107b6a983f4ecd`. All nine CMake jobs register and complete
90/90 tests. Linux Debug/Release/ASan+UBSan and macOS arm64/x86-64 Debug/Release run the
complete suite. Windows MSVC Debug/Release capability-skip only the documented real
OpenGL 3.3 shader test on the hosted software context while completing every other
test. The legacy libretro target builds, links, and cleans.

All 14,067 log lines (1,874,662 bytes) were inspected. There is no compiler/linker
warning, sanitizer signature, crash, runtime error, timeout, failed test, invalid
generated path, or deployment failure. Normal checkout default-branch hints, Windows
pthread feature probes, absent optional Vulkan headers, and omission of the unused
OpenSSL Qt backend are non-actionable. In particular, the new speed settings, timing,
worker, GUI, migration, and long-running boundedness regressions pass on every host.

## Operating-system CI matrix

Continuous Integration run
[`33499366503`](https://github.com/birdybro/Genesis-Plus-GX-GUI/actions/runs/33499366503)
completed successfully against the exact enhanced save-state implementation and current
native-package baseline.

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

Each hosted CMake job registered all 93 tests. Linux and both macOS architectures pass
93/93, including real OpenGL/librashader rendering. Windows passes 92 and reports only
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

Exact automatic-resume run `33472190327` supersedes those tester artifacts with Linux
x86-64 (39,909,784 bytes), Windows x86-64 (52,482,706 bytes), macOS arm64 (63,748,492
bytes), and macOS x86-64 (65,357,924 bytes). The six contained package SHA-256 files
verify; ZIP and TGZ integrity checks pass; both DMGs identify as Apple DMG version 4.
The Linux TGZ has 86 entries, Windows ZIP 75, and each macOS app ZIP 123. Required Qt,
SDL3, librashader, platform-plugin, CRT-preset, license, and Windows redistributable
payloads are present, with no ROM, disc, BIOS, save/state, secret-key, or environment
payload.

Exact configurable-speed run `33477974149` supersedes those tester artifacts with Linux
x86-64 (39,929,410 bytes), Windows x86-64 (52,502,878 bytes), macOS arm64 (63,777,323
bytes), and macOS x86-64 (65,382,346 bytes). All six contained SHA-256 manifests verify;
TGZ and ZIP integrity checks pass; both DMGs identify as Apple DMG version 4. Extracted
executables identify as the four expected native architectures. The Linux archive has
86 entries, Windows has 75, and each macOS app ZIP has 123. Required Qt, SDL3,
librashader, CRT, license/documentation, and Windows redistributable payloads are
present, with no ROM, disc, BIOS, save/state, private-key, or environment payload.

Exact enhanced save-state run `33499366503` supersedes those tester artifacts with
Linux x86-64 (40,082,683 bytes), Windows x86-64 (52,595,396 bytes), macOS arm64
(63,987,624 bytes), and macOS x86_64 (65,603,781 bytes). All six contained SHA-256
manifests verify; TGZ, ZIP, and DMG structural checks pass. The Linux archive has 86
entries, Windows has 75, and each macOS app ZIP has 123. Extracted executables identify
as the four advertised native architectures; all 22 Linux ELF objects resolve; packaged
help, version, and real XCB event-loop smokes pass. Required Qt, SDL3, SQLite,
librashader, CRT, license/documentation, and Windows redistributable resources are
present, with no ROM, disc, BIOS, save/state, credential, or private-key payload.

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

## Archive and multi-disc playlist verification

Milestone 82 adds a separately linked, symbol-isolated MiniZip/zlib reader for bounded
ZIP cartridge discovery and exact cached extraction, plus strict UTF-8 M3U/M3U8 Sega CD
playlist resolution. Debug and optimized Release each pass 91/91 tests. ASan/UBSan
passes 91/91 without a finding; a fresh shader-disabled graph passes all 89 applicable
tests; and a fresh Clang 22 warning-as-error graph passes 91/91 with no authored-code
diagnostic. The new integration test executes extracted Genesis and Master System
members and changes between generated Sega CD discs declared by an M3U. GUI and
full-process tests cover selection, cancellation, source/runtime identity, disc action
gating, command-line loading, and session resume.

A fresh CHD-disabled graph also passes 91/91, proving that ZIP browsing remains usable
when the optional CHD decoder is excluded and that its prefixed zlib copy is independent.

The staged Linux installation passes package verification. Its 0.1.1 x86-64 TGZ has a
valid SHA-256 manifest, valid gzip/tar structure, 86 entries, and the expected binary,
Qt/SDL/librashader runtime, CRT assets, documentation, and notices. The inherited Unix
libretro target also builds, links, and cleans.

Exact implementation run
[`33483677263`](https://github.com/birdybro/Genesis-Plus-GX-GUI/actions/runs/33483677263)
for commit `3b828babc13e9a620161a556e1984221358b771d` passes all ten hosted jobs. Each of
the nine native configurations registers 91 tests and reports zero failures; Windows
capability-skips only the established real OpenGL shader test on its software context.
The complete 14,255-line, 1,902,125-byte log has no authored warning, linker failure,
sanitizer finding, runtime error, timeout, or test failure. Expected Windows pthread
and optional Vulkan-header configure probes are non-actionable.

The four downloaded artifacts are Linux x86-64 (40,017,572 bytes), Windows x86-64
(52,551,513 bytes), macOS arm64 (63,869,318 bytes), and macOS x86_64 (65,518,404
bytes). All six package checksum manifests and every ZIP/gzip/tar integrity check pass;
the DMGs and four application binaries identify with their advertised formats and
architectures. Extracted Linux runtime dependencies resolve, its `--version` and
`--help` smokes pass, required runtime/assets/notices are present, and the complete
payload scan finds no ROM, BIOS, save, state, test-fixture, credential, or private-key
content.

## Cartridge soft-patch verification

Milestone 83 adds bounded, checksum-validating IPS, BPS, and UPS cartridge soft
patching. The original game is never modified: a collision-safe per-user cache contains
the exact patched runtime bytes, while recent-game and library records retain the
source path. Metadata, SHA-256 game identity, saves, states, cheats, per-game settings,
and automatic session resume all derive from the patched bytes. Users can choose a
game/patch pair, pass `--patch`, drop the two files together, or use one unambiguous
same-name sidecar. Disc images and playlists are rejected because cartridge patch
formats do not describe multi-file disc layouts.

Complete warning-as-error Debug, optimized Release, ASan/UBSan, fresh Clang 22, and
CHD-disabled suites each pass 93/93; the shader-disabled graph passes all 91 applicable
tests. The sanitizer run has no finding. Unit coverage exercises IPS literals/RLE/
growth/truncation, every BPS command and checksum, bidirectional/resizing UPS, malformed
offsets and varints, fixed-seed mutation input, sidecar ambiguity, immutable sources,
and cache collision/reuse. The core workflow changes a generated Genesis program's
68000 immediate through IPS and proves changed RAM plus independent identity. GUI and
real-process coverage proves explicit/automatic/drop/CLI launch, errors, status,
versioned session migration, checkpoint creation, and exact patched restart.

The staged Linux installation passes package verification, dependency and CLI smokes;
CPack emits the expected 0.1.1 x86-64 TGZ/checksum; and the inherited Unix libretro
target builds, links, identifies as x86-64 ELF, and cleans. The supplied NAS ROM path
was not mounted during this verification, so the optional header-only IPS acceptance
case could not run against it. No degraded RAID mount was attempted.

The first exact hosted attempt, run `33489629492`, correctly failed Apple Silicon and
Windows instead of hiding a platform defect: their case-insensitive filesystems made
`.ips` and the uppercase `.IPS` discovery probe name the same file, but lexical
deduplication reported them as two patches. Discovery now deduplicates by filesystem
identity. A same-inode hard-link regression reproduces the alias on case-sensitive
hosts and also covers native case-insensitive spelling. Windows additionally exposed a
test-only issue where the schema-1 migration fixture concatenated an unescaped
backslash path into JSON; the fixture now uses Qt's JSON writer. Corrected Debug,
Release, and ASan/UBSan suites each pass 93/93 again.

Exact corrective run
[`33490884217`](https://github.com/birdybro/Genesis-Plus-GX-GUI/actions/runs/33490884217)
passes all ten jobs and every registered test on Linux, Windows, Apple Silicon, and
Intel macOS. All six downloaded package checksums and archive integrity checks pass;
the four applications identify as the expected ELF x86-64, PE32+ x86-64, Mach-O arm64,
and Mach-O x86-64 architectures. Required Qt, SDL3, librashader, shader, documentation,
license, SQLite, and Windows redistributable payloads are present. Linux dependencies,
`--help`, and `--version` resolve from the extracted archive, and no ROM, disc, BIOS,
save/state, fixture, credential, or private key is included.

The complete 14,413-line, 1,921,484-byte log audit found one actionable warning in all
four macOS builds: `integration.soft_patch` linked `genplusgx_game_files` both directly
and transitively. Its target now relies on the adapter/persistence dependency graph and
the generated link line contains one correctly ordered copy. The downloaded Linux
archive also emitted a non-fatal missing-Wayland-plugin probe before falling back to
its intentionally bundled XCB backend on a Wayland/XWayland desktop. Package startup
now selects XCB before `QApplication` only for the relocatable XCB-only layout, while
preserving explicit overrides and native Wayland source builds. The unit test covers
XCB-only, native-Wayland, and explicit-override cases; CI and release package smokes
now simulate Wayland auto-selection and reject the old diagnostic. Post-correction GCC
Debug/Release/ASan, Clang 22, and CHD-disabled suites pass 93/93, the shader-off suite
passes 91/91, the legacy target builds/links/cleans, and a fresh Linux stage passes
automatic backend, dependency, package-layout, TGZ, and checksum checks.

Final exact run
[`33494058639`](https://github.com/birdybro/Genesis-Plus-GX-GUI/actions/runs/33494058639)
passes all ten jobs against `da80552964855fe5ab7ec17689526b8cabdb7d8c`.
All nine desktop/sanitizer configurations pass 93/93 tests; Windows capability-skips
only the established real-OpenGL shader test, and the inherited Linux libretro target
builds and cleans. The complete 14,385-line, 1,918,607-byte log is free of authored
compiler/linker warnings, sanitizer findings, runtime errors, timeouts, and failures.
The duplicate-library and missing-platform-plugin findings from the preceding audit do
not recur.

The four downloaded artifacts are Linux x86-64 (40,052,846 bytes), Windows x86-64
(52,577,028 bytes), macOS arm64 (63,924,570 bytes), and macOS x86_64 (65,553,913
bytes). All six package checksum manifests, every ZIP/gzip/tar integrity check, and
both DMG format checks pass. The applications and librashader runtimes identify with
their advertised architectures. Extracted payloads contain the expected Qt, SDL3,
SQLite, librashader, CRT, documentation, notices, and Windows redistributable files;
the scan finds no ROM, disc, BIOS, save/state, fixture, credential, or private key.
All 22 Linux ELF objects resolve their dependencies, `--help` and `--version` run from
the archive, and a simulated Wayland-session plugin trace directly loads bundled XCB
without the former missing-Wayland diagnostic.

## Enhanced save-state management verification

Milestone 84 upgrades the checked frontend state envelope to schema 2 while preserving
the raw Genesis Plus GX payload and schema-1 compatibility. Optional UTF-8 names and
native-frame PNG previews have strict byte/dimension caps, their own SHA-256 integrity
field, and real image decoding before acceptance. Manual import validates game identity,
hardware, envelope lengths, presentation metadata, payload signature, and both hashes
before atomically replacing the selected slot. Export validates first; rename atomically
rewraps the unchanged core state, timestamp, frame, and preview.

The non-modal, keyboard-accessible ten-slot browser exposes previews, names, timestamps,
frame and payload sizes, invalid-state diagnostics, Save/Replace, Load, Import, Export,
Rename, and Delete through stable object names and injectable native dialog seams. All
disk work remains on the bounded state-storage service, core capture/restore remains on
the emulation owner thread, and the final review fixed and regression-tested the browser's
busy-state propagation so repeated clicks cannot queue overlapping work.

Warning-as-error Debug, optimized Release, leak-detecting ASan/UBSan, fresh Clang 22,
and CHD-disabled builds pass 93/93; the shader-disabled graph passes 91/91. Tests cover
schema-2 round trips, schema-1 compatibility, names/previews, corrupt and oversized PNGs,
invalid UTF-8/control characters, deterministic mutations across every envelope region,
wrong-game protection without destination changes, async rename/import/export, every
browser action, dialog injection, and a generated-ROM capture/export/rename/import/load
workflow that restores the exact core payload. A fresh Linux stage/package, legacy
libretro build/link/clean, and real packaged XCB event-loop smoke also pass.

Exact run
[`33499366503`](https://github.com/birdybro/Genesis-Plus-GX-GUI/actions/runs/33499366503)
passes all ten jobs against `30ed5d7e378c7107db3be844fc80cfd0c96deec2`.
Each of the nine native configurations passes 93/93, including hosted ASan/UBSan;
Windows capability-skips only the established real-OpenGL shader test. The full
14,426-line, 1,923,154-byte log contains no authored compiler/linker warning, sanitizer
finding, runtime error, timeout, failure, or unexpected skip. All four downloaded
artifact families, six checksum manifests, archives, application architectures, Linux
dependencies, required runtime/legal resources, and prohibited-payload scans pass.

## Bounded recording verification

Milestone 85 adds a capture tap after each completed core frame without allowing the
GUI or writer to access core globals. The emulation owner copies native tightly packed
RGB565 video and matching stereo samples into one of eight preallocated slots; a
dedicated writer emits sequential PNG frames, PCM WAV audio, a JSONL frame index, and
a final manifest. Rewind frames retain cadence with silence. Queue, frame, per-image,
per-frame-audio, and total-output limits are hard bounded and surfaced in diagnostics.
An exclusive `.partial` directory becomes visible as a completed recording only after
all accepted frames drain and final metadata is durable.

The recording service unit test covers exact pixel conversion, dynamic geometry, WAV
headers/sample counts, JSON metadata, collisions, saturation/drop accounting, invalid
geometry and rates, restart, shutdown draining, and 24 immediate start/stop races. The
integration test executes a generated legal Genesis program through the real core,
worker capture tap, and writer, then validates all four recorded frames and output
files. Worker, GUI, process, settings, migration, and diagnostics regressions also cover
owner-thread delivery, disabled capture, native-dialog injection, transition gating,
close-game finalization, the standard recordings directory, and the configurable
hotkey.

Local warning-as-error Debug, optimized Release, ASan/UBSan, Clang 22, and CHD-disabled
graphs pass 95/95; the shader-disabled graph passes 93/93. The staged Linux package,
legacy libretro build/link/clean, dependencies, CLI, and real XCB/OpenGL event-loop
smokes pass. Exact hosted run
[`33506923671`](https://github.com/birdybro/Genesis-Plus-GX-GUI/actions/runs/33506923671)
passes all ten jobs against `59beedd1f9b1e0a08a2ec0e28f0f9293cd29fe20`.
All nine native configurations pass every supported test and both recording tests;
Windows capability-skips only the established OpenGL 3.3 shader-render test.

The complete 14,523-line, 1,937,071-byte hosted log has no authored compiler/linker
warning, sanitizer finding, runtime error, crash, timeout, failure, or unexpected skip.
The downloaded packages are Linux TGZ 40,130,666 bytes, Windows ZIP 52,627,291 bytes,
macOS arm64 ZIP/DMG 32,049,001/31,999,649 bytes, and macOS x86_64 ZIP/DMG
32,869,328/32,790,054 bytes. All six checksum manifests and both DMG structural checks
pass. Executables and librashader libraries match their advertised architectures;
required Qt, SDL3, SQLite, shaders, documentation, notices, and redistributable files
are present; all 22 Linux ELF dependencies resolve; and no prohibited game, firmware,
save/state, fixture, credential, or private-key payload is present.

## Run-ahead verification

Milestone 86 adds optional, disabled-by-default one-through-four-frame run-ahead for
cartridge systems. The emulation owner thread captures the portable raw state plus a
separate same-process transient context, executes bounded speculative frames, retains
only the final native framebuffer, rolls back exactly, reapplies the latest input, and
executes one authoritative frame. Only that authoritative frame contributes audio,
state, persistence, rewind history, frame count, and recording cadence. Sega CD and
specialized peripheral protocols fail safe to authoritative-only operation.

The transient context includes the viewport, pause line, full 68000 execution context,
sound filters/counters, every active blip buffer, standard pad/Master Tap/Four Way Play
state, and Team Player state. The pre-commit adversarial review caught the initially
omitted private Team Player handshake counter; a direct save/mutate/restore regression
now proves it is preserved. The first speculative continuation is compared with its
authoritative counterpart and any mismatch disables run-ahead for that session while
publishing the canonical frame.

Warning-as-error Debug, optimized Release, leak-detecting ASan/UBSan, fresh Clang 22,
and CHD-disabled builds pass 98/98 tests; the shader-disabled graph passes 96/96. The
integration suite proves independent baseline state/audio/input, future video, host
audio FIFO retention, recording isolation, pad and multitap rollback, incompatible-mode
suspension, Sega CD exclusion, and stable allocation through 120 maximum-depth host
frames. The strict legacy libretro build/link/clean and a fresh staged Linux package
also pass. All 22 staged ELF dependency graphs resolve; installed CLI and real
XCB/OpenGL event-loop smokes pass; the package contains no prohibited runtime payload;
and its TGZ checksum verifies. Exact hosted native logs and artifacts remain pending
until the implementation commit is pushed; Milestone 86 cannot close before that audit.

## Final feature checklist

- [x] SG-1000, Mark III, Master System, Game Gear, Genesis/Mega Drive, and Sega CD/Mega
  CD run through the separated desktop adapter and owner-thread emulation worker.
- [x] Dynamic high-DPI OpenGL/software video, aspect/integer scaling, overscan, filters,
  interlace, Game Gear viewport, fullscreen, runtime FPS, native PNG screenshots,
  adjustable built-in CRT output, and modern Libretro Slang preset chains.
- [x] Bounded stereo audio, core mixing options, device/latency selection, live
  transactional reconfiguration, instrumentation, pause, and disconnect recovery.
- [x] Exact configurable normal, slow-motion, and fast-forward pacing; mutually
  exclusive run modes; conflict-checked hold/toggle bindings; bounded silent audio
  behavior away from 100%; live status and diagnostics.
- [x] Keyboard and SDL3 controllers, hot-plug, eight player assignments, button/axis
  capture, deadzones, profiles, specialized devices, multitaps, and configurable
  conflict-checked hotkeys including independent fast-forward hold/toggle.
- [x] Open/replace/close, drag/drop, command line, strict bounded CUE preflight,
  bounded ZIP cartridge browsing, strict M3U multi-disc playlists, descriptive
  malformed-file errors, and persistent source-aware recent-game history.
- [x] Non-destructive IPS/BPS/UPS cartridge soft patching through explicit GUI/CLI,
  two-file drop, and unambiguous sidecars with checksummed patched-content identity.
- [x] Atomic identity-keyed cartridge SRAM, Sega CD internal BRAM/RAM cartridge,
  automatic load/flush, platform-standard application-data paths, and composite CUE
  identities covering the sheet plus every validated track without path dependence.
- [x] State slots 0-9, quick operations, names, native-frame previews, a complete
  browser, validated manual import/export, timestamps, delete, schema migration,
  corruption/wrong-game rejection, and deterministic restoration.
- [x] Bounded lossless native-frame recording with stereo PCM audio, deterministic
  PNG/JSON frame dumps, collision-safe output directories, drop instrumentation,
  lifecycle-safe draining, and a configurable hotkey.
- [x] Opt-in automatic clean-shutdown session checkpoint, identity-checked restore,
  command-line precedence, explicit-close clearing, and safe normal-launch fallback.
- [x] Bounded owner-thread rewind with configurable cadence/memory, hold/toggle UI,
  conflict-checked hotkey migration, muted reverse playback, and state invalidation.
- [x] Optional bounded one-through-four-frame cartridge run-ahead with exact transient
  rollback, authoritative audio/input/state, pad/multitap support, fail-closed
  determinism verification, mode suspension, settings, status, and diagnostics.
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
- The previously supplied external-ROM mount was unmounted/empty during the
  configurable-speed, soft-patch, enhanced save-state, and recording runs, so its speed
  workflows, temporary header-only IPS case, optional real-game state browser smoke,
  and optional real-game recording smoke were not executed locally. The required suites
  use legal generated 68000 and Z80 programs through the same production GUI/worker/core route;
  the earlier 113-case Phantasy Star IV option acceptance remains recorded above. No
  degraded RAID assembly or mount was attempted.
- Controller and audio hot-plug behavior is deterministically tested with injected SDL
  events and the SDL dummy audio driver. Maintainers should still smoke-test target
  hardware and vendor drivers before a public release.
- macOS artifacts are unsigned development builds. Signing, hardened runtime,
  notarization, and any Windows installer signing require project-owned credentials.
- Linux ships a relocatable TGZ rather than an AppImage and intentionally relies on the
  CI distribution's base graphics, window-system, C/C++ runtime, and libc libraries.
- ZIP support is intentionally limited to stored/deflated cartridge members; archived
  Sega CD workflows and formats other than ZIP remain unsupported. Automatic soft
  patch discovery is intentionally limited to direct cartridge files; ZIP members can
  use an explicitly chosen patch, while disc images/playlists reject cartridge patch
  formats. Physical optical drives, netplay,
  achievements, cloud sync, online scraping/downloading, instruction-level stepping,
  an external debugger server, TAS tooling, and streaming remain intentionally outside
  scope.

These limitations do not leave an advertised control inert and do not weaken the
defined standalone-emulator workflows.
