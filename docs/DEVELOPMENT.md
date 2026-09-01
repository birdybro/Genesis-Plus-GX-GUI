# Development

Genesis Plus GX GUI is organized so desktop work remains testable without turning the
upstream emulator into a UI framework. Start with [ARCHITECTURE.md](ARCHITECTURE.md) for
ownership and data-flow diagrams.

## Source layout

```text
core/                 Authoritative Genesis Plus GX emulator sources
desktop/core/         C host bridge, C++ adapter, worker, commands/events
desktop/video/        Bounded frame exchange, geometry, display widget
desktop/audio/        Bounded SDL3 audio output and metrics
desktop/input/        Neutral mappings, profiles, SDL controller service
desktop/persistence/  Platform roots, save RAM, state wrappers, recents
desktop/settings/     Versioned global and sparse per-game settings
desktop/localization/ Qt catalog selection, discovery, fallback, and status
desktop/library/      Metadata, SQLite store, asynchronous scanner
desktop/ui/           Qt windows, dialogs, models, injectable dialog seams
desktop/app/          Composition root, CLI, application lifecycle
tests/                Unit, core, integration, GUI, fixtures, utilities
cmake/                Warning, sanitizer, package, and release gates
```

Do not move or reformat core files just to match frontend style. Desktop adaptations to
the core-facing ABI belong under `desktop/core/c_api`; unavoidable upstream edits should
be minimal, explained, and regression-tested.

## Threading and ownership

The GUI thread owns Qt widgets and application services. One `EmulationWorker` thread
owns the Genesis Plus GX globals for its entire active lifecycle. Commands cross through
a bounded, synchronized queue; coalescible input/settings commands retain only useful
newest state. Worker events are delivered back to Qt through queued signals.

Video uses three preallocated exchange slots. Audio uses a bounded single-producer/
single-consumer ring. Input is an immutable snapshot consumed at a frame boundary.
Never call core lifecycle/frame/state functions from a widget callback, retain raw frame
pointers across an exchange, block the SDL callback, or create an unbounded per-frame
event queue.

Shutdown is ordered: reject new actions, stop the worker, flush live save memory, stop
audio, join worker/scanner threads, release presentation resources, then destroy the
window. Any new asynchronous service needs an explicit shutdown and repeated-lifecycle
test.

## Code conventions

- New code is C++20 (the narrow core bridge is C99) and builds with target-scoped
  `-Wall -Wextra -Wpedantic` or MSVC `/W4` policy.
- Prefer RAII, value types, `std::filesystem::path`, explicit ownership, and bounded
  containers. Validate integer narrowing, file sizes, offsets, and enum input.
- Keep platform logic in small services rather than scattering preprocessor branches
  through UI code.
- Use translated user-facing Qt strings and stable, descriptive `objectName` values for
  significant widgets and actions.
- Normal recoverable input failures return typed status/results and concise UI errors;
  lower-level details go to bounded structured logging. Do not abort for a bad user file.
- Settings/storage formats have an explicit schema version. Migrate known older schemas,
  reject unknown future schemas, and write atomically.
- Never log every emulated frame or expose private absolute paths in copied diagnostics.

The repository has no mandatory formatter that rewrites upstream code. Match nearby
frontend style and keep builds free of avoidable new warnings.

When adding or changing user-facing text, refresh the committed pseudo catalog and run
the localization label before the full suite. Preserve placeholders and give every
`tr()` call a stable meta-object context; never translate persistence keys, object
names, core values, log fields, or command-line switches. See
[LOCALIZATION.md](LOCALIZATION.md).

## Test design

A behavior should be exercised at the lowest useful layer and through an integration or
GUI workflow when it crosses layers. CTest labels include `unit`, `core`, `integration`,
`gui`, `audio`, `video`, `input`, `timing`, `persistence`, `filesystem`, `database`,
`fuzz`, `lifecycle`, `stress`, `packaging`, and `release`.

Tests must:

- use a temporary application-data root and leave real user files untouched;
- generate original/CC0 game, disc, and firmware bytes or document redistributable
  provenance;
- inject file pickers, message boxes, confirmations, clock/input sources, or output
  devices where native interaction would otherwise be required;
- assert state transitions and output, not merely widget construction;
- use bounded deterministic property corpora rather than unbounded CI fuzz runs; and
- investigate any changed framebuffer/audio hash before updating the expected value.

Qt GUI tests normally run with `QT_QPA_PLATFORM=offscreen`; display workflows force the
software presentation seam where needed. Native window chrome is intentionally not a
cross-platform pixel golden.

Run a focused change and full regression:

```bash
cmake --build --preset debug
ctest --preset debug -L gui --output-on-failure
ctest --preset debug --output-on-failure
```

Ownership, file parsing, settings, and threading changes should also pass the `asan`
preset. See [TESTING.md](TESTING.md) and [TEST_MATRIX.md](TEST_MATRIX.md).

## Adding a setting or action

1. Put the value in the appropriate validated settings value type with a sensible
   default and schema behavior.
2. Add store round-trip, malformed-input, migration, and precedence tests as applicable.
3. Add an adapter/worker command only if the core or emulation pipeline must change.
4. Add the Qt control with a stable object name, keyboard label/focus behavior, and
   Apply/OK/Cancel/Restore Defaults semantics.
5. Test propagation to the owning service, persistence across reopen, failure behavior,
   and game/no-game action gating.
6. Update the user guide, focused guide, shortcut table, and diagnostics if relevant.

Avoid presenting a core option that the adapter silently ignores.

## Version and release changes

The single desktop version is the root `project(... VERSION ...)` in `CMakeLists.txt`.
It configures the executable, About/Diagnostics output, package names, and release tag
gate. A version change must update [CHANGELOG.md](../CHANGELOG.md), pass Debug/Release/
sanitizer suites, build all native packages, and follow [RELEASES.md](RELEASES.md).

Never create or replace a tag/release without authorization. Manual release workflow
runs are non-publishing rehearsals by construction.

## Working with upstream

Core-facing changes require the inherited libretro build in addition to desktop tests.
The safe remote/merge procedure and likely conflict boundaries are in
[UPSTREAM_MAINTENANCE.md](UPSTREAM_MAINTENANCE.md). Keep desktop milestone work in
reviewable commits so an upstream sync can distinguish integration changes from core
changes.
