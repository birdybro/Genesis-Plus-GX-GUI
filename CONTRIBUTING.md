# Contributing

Thank you for improving Genesis Plus GX GUI. Contributions should preserve emulator
accuracy, the desktop/core boundary, and the repository's ability to incorporate future
Genesis Plus GX updates.

## Before contributing

Read [LICENSE.txt](LICENSE.txt). This repository uses the Genesis Plus GX
non-commercial license; it is not licensed for commercial products or activity. By
submitting a contribution, you represent that you have the right to provide it under
the repository's applicable terms.

Do not submit commercial ROMs, proprietary Sega BIOS files, copyrighted game assets,
scraped box art, credentials, or generated user data. Test binaries must be original,
public-domain, or explicitly redistributable and documented in
[tests/fixtures/README.md](tests/fixtures/README.md).

## Development setup

Follow [BUILDING.md](docs/BUILDING.md), then verify a clean Debug build:

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The project uses C++20, target-scoped warnings, Qt 6 Widgets, SDL3, Qt Test, and CTest.
Architecture and module ownership are documented in
[ARCHITECTURE.md](docs/ARCHITECTURE.md); day-to-day conventions are in
[DEVELOPMENT.md](docs/DEVELOPMENT.md).

## Change guidelines

- Prefer changes under `desktop/` for frontend behavior. Do not rewrite, rename, or move
  upstream core files for appearance alone.
- Treat the Genesis Plus GX global core as single-owner and non-thread-safe. GUI code
  must communicate with it through the worker command/event boundary.
- Keep frame, sample, event, log, and command storage bounded. Avoid framebuffer-sized
  allocation or frame-by-frame logging in steady state.
- Give significant Qt widgets/actions stable, unique `objectName` values and use
  injectable dialog/message seams so workflows remain headlessly testable.
- Keep settings schemas versioned, validate untrusted paths and sizes before use, use
  atomic persistence, and never write tests to the real application-data root.
- Add or update documentation with the code. A visible action or setting is incomplete
  until its behavior and failure cases are documented.
- Investigate deterministic golden changes. Never update a framebuffer/audio hash merely
  because the current test failed.

Changes to authoritative `core/` algorithms require a clear accuracy justification,
focused regression coverage, and an explanation of their relationship to upstream.
Consult [UPSTREAM_MAINTENANCE.md](docs/UPSTREAM_MAINTENANCE.md) first.

## Tests and commits

Run the focused labels for your change and all applicable regression tests. Examples:

```bash
ctest --preset debug -L unit --output-on-failure
ctest --preset debug -L gui --output-on-failure
ctest --preset debug --output-on-failure
```

Linux contributors should also run the `asan` preset for new ownership, parsing,
threading, or persistence code. Legacy core-facing changes should build the inherited
libretro target as described in [TESTING.md](docs/TESTING.md).

Keep commits reviewable and do not include `build/`, packages, user settings, ROMs, or
firmware. Do not rewrite published history or force-push shared branches.

## Pull requests

A useful pull request explains:

- the user-visible result and design decision;
- affected core/frontend/thread ownership boundaries;
- tests added and exact gates run;
- supported platforms exercised;
- license/provenance for any dependency or fixture; and
- known limitations or follow-up work.

CI must pass on Ubuntu, Windows, and macOS. Tests requiring user-owned Sega CD firmware
remain optional external-fixture tests and must not become a required CI dependency.
