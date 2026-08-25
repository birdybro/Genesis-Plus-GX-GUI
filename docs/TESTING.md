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
`filesystem`, `fuzz`, `timing`, `audio`, `video`, `input`, `lifecycle`, and `stress`:

```bash
ctest --preset debug -L gui --output-on-failure
ctest --preset debug -L stress --output-on-failure
```

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

## Optional external BIOS test

The Sega CD external-fixture test is opt-in and excluded from required CI. Configure
with `GENPLUSGX_ENABLE_EXTERNAL_FIXTURE_TESTS=ON` and provide the documented environment
variables from `docs/BIOS.md`. A user-supplied BIOS is never copied into the source or
build tree.
