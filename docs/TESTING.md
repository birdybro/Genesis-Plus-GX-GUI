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

Qt GUI tests use the offscreen platform automatically. Tests requiring frame
presentation force the deterministic software display path. Native widget chrome is not
pixel-compared across platforms; emulator framebuffers and geometry are tested below the
window layer.

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
explicit; third-party actions are pinned to immutable commit IDs. Failed CTest jobs
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
