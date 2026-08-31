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

Useful focused labels include `unit`, `core`, `integration`, `gui`, `persistence`,
`filesystem`, `fuzz`, `timing`, `audio`, `video`, `input`, `lifecycle`, `stress`,
`packaging`, `release`, and `documentation`:

```bash
ctest --preset debug -L gui --output-on-failure
ctest --preset debug -L stress --output-on-failure
ctest --preset debug -L documentation --output-on-failure
```

The documentation gate requires every published guide, checks the release-facing README
sections and dependency inventory, rejects stale milestone language, and resolves local
Markdown links. It never follows external links or requires network access.

Hosted builds enable `GENPLUSGX_WARNINGS_AS_ERRORS`, so a new frontend/test compiler
warning fails the relevant platform job before CTest. This is intentionally not applied
wholesale to inherited core or bundled third-party sources.

Qt GUI tests use the offscreen platform automatically. Tests requiring frame
presentation force the deterministic software display path. Native widget chrome is not
pixel-compared across platforms; emulator framebuffers and geometry are tested below the
window layer.

`core.debug_tools` loads the generated CC0 Genesis fixture and verifies owner-thread
snapshots, logical byte order, immutable publication, bounded reads/writes, paused-only
mutation, and worker serialization. `gui.debug_tools` verifies that the developer menu
is opt-in and drives the real CPU, memory, VDP, sound, input, control, and state surfaces
using typed responses. Neither test reads a commercial ROM or the user's settings.

`gui.libretro_shader_render` intentionally uses a real OpenGL context. Linux CI installs
Xvfb and runs the test through XCB/GLX with `GENPLUSGX_REQUIRE_OPENGL_SHADER_TEST=1`, so
failure to create or execute the OpenGL 3.3 shader path is fatal. The test samples a
known input through an original Slang pass and then verifies that the actual display
widget's built-in CRT output is non-black and differs from its unshaded baseline.
It also invokes and verifies the production pre-`QApplication` surface-format setup;
this ordering is required for Qt to composite the accelerated child correctly on
native Wayland.
The widget portion uses an asymmetric quadrant frame and rejects a vertical or
horizontal orientation change. It executes all 36 combinations of native/4:3/stretch,
fit/integer, nearest/bilinear, and off/built-in/custom shader presentation, then checks
both endpoints of all five bundled CRT parameters.
Windows and macOS run on their native Qt platforms and use skip code 77 only when the
hosted environment provides no usable desktop OpenGL context.

## Optional real-ROM option acceptance

`genplusgx_external_rom_acceptance_test` is built with shader-enabled desktop tests but
is intentionally not registered as a required CTest: it requires a game legally
provided by the developer. It loads the file through the real `CoreAdapter`, executes
13 core-video, 35 core-audio, 12 emulated-device, 36 accelerated-presentation, and 17
compatible system-reload cases, and checks bounded output/lifecycle behavior. Shader
images must be closer to the unshaded upright image than to its vertical mirror.

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
- 10,000 rapidly submitted input snapshots and newest-command coalescing;
- 90 normal paced frames that deliberately fill, but never exceed, the audio ring;
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
