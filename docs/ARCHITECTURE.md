# Desktop Architecture

Status: architecture baseline; implementation details are refined with tests at each
milestone.

## Design principles

1. `core/` remains the authoritative emulator and is not moved or rewritten.
2. Exactly one emulation context owns and accesses Genesis Plus GX global state.
3. The GUI never calls core functions directly and never waits on frame execution.
4. All cross-thread flows are bounded, observable, and have explicit shutdown rules.
5. Host policy (paths, dialogs, logging, devices) remains outside the core adapter.
6. Pure calculations and serialization are Qt-independent where practical.
7. New frontend targets use C++20; the inherited core remains C with its established
   compile policy.

## Component overview

```text
             +-------------------+
             | Qt 6 Widgets GUI  |
             +---------+---------+
                       |
               typed commands/events
                       |
             +---------v---------+
             | Frontend services |
             | and coordinator   |
             +---------+---------+
                       |
               bounded command queue
                       |
             +---------v---------+
             | Emulation worker  |
             | (single thread)   |
             +---------+---------+
                       |
                  CoreAdapter
                       |
             +---------v---------+
             | Genesis Plus GX   |
             | core (C library)  |
             +-------------------+
```

The intended target graph is:

```text
genplusgx_core (C static library)
        ^
        |
genplusgx_frontend (C++ adapter and non-UI services)
        ^                       ^
        |                       |
genplusgx_desktop (Qt app)   genplusgx_tests
```

Qt and SDL must never be public dependencies of `genplusgx_core`. Platform-specific
packaging and device implementations depend inward on frontend interfaces, not the
reverse.

## Directory ownership

```text
core/                    upstream-owned emulator sources (kept in place)
desktop/core/            C bridge, CoreAdapter, EmulatorSession, worker protocol
desktop/video/           frame exchange, geometry, presentation widget
desktop/audio/           sample ring, SDL3 output device, A/V instrumentation
desktop/input/           neutral snapshots, mappings, SDL3 controller service
desktop/persistence/     application paths, atomic files, RAM and states
desktop/settings/        versioned global and per-game configuration
desktop/library/         metadata and asynchronous SQLite index
desktop/cheats/          validation, persistence, adapter application
desktop/platform/        platform paths, diagnostics, deployment helpers
desktop/ui/              Qt widgets, models, dialogs, injectable dialog service
desktop/app/             command line and process composition root
tests/                   unit, core, integration, GUI, fixtures, utilities
cmake/                   target helpers and packaging modules
```

## Core adapter

The adapter is the only production component permitted to include the core's broad
`shared.h` surface. Its public C++ API uses owned values such as paths, metadata,
settings snapshots, frame views, and byte spans. A small desktop `osd.h`/C bridge
provides the configuration object, archive and BIOS callbacks, logging hook, and any
other symbols required by the core.

The low-level adapter lifecycle is a state machine:

```text
Uninitialized -> Ready -> Loaded
       ^           ^         |
       |           +---------+
       +---------------------+
```

Invalid transitions return typed errors. Load is transactional: a failed file, BIOS,
or core initialization leaves the adapter in `Ready`, with no half-loaded game exposed
to the GUI. Repeated load/unload and shutdown operations are safe.

`CoreAdapter::initialize()` records its owner thread and acquires a process-wide single
core lease. Every production operation checks that lease and thread before reaching a
Genesis Plus GX function. A process-wide execution mutex prevents simultaneous core
entry and permits the destructor to perform emergency RAII cleanup without racing a
finishing call; normal shutdown is still required on the owner thread. The higher-level
emulation worker adds running, paused, frame-advance, and shutting-down states without
duplicating the adapter's resource lifecycle.

## Threading and command flow

The GUI thread owns all widgets, actions, Qt models, and OpenGL presentation resources.
The emulation thread owns `CoreAdapter`, pacing state, current input snapshot, and the
producer ends of the video/audio buffers. Library scanning and controller discovery use
separate workers only through their service interfaces.

Commands are a finite tagged set: load, unload, start, pause, resume, hard reset, soft
reset, frame advance, fast-forward mode, save/load/delete state, input snapshot,
settings snapshot, disc change/eject, and shutdown. Commands with superseding semantics
(input and live settings) are coalesced. Lifecycle and persistence commands retain
ordering. The queue has a fixed capacity and reports saturation rather than growing
without bound.

The worker emits immutable events containing operation IDs. The coordinator discards
stale completion events after a newer load/unload generation, preventing late UI
updates from a previous game.

## Video data flow

```text
core RGB565 framebuffer
        |
  frame-boundary viewport snapshot
        |
bounded triple-buffer exchange (producer publishes newest complete slot)
        |
GUI render request (coalesced)
        |
QOpenGLWidget texture upload
        |
nearest/bilinear shader + aspect/integer-scale viewport
        |
window / fullscreen / high-DPI surface
```

Slots are allocated for the core's maximum supported surface and reused. A published
frame carries width, height, pitch, crop/overscan viewport, pixel aspect, region,
interlace state, and monotonically increasing frame number. The GUI may skip obsolete
presentation frames but never observes a partially written frame. Native screenshots
copy a published complete frame; displayed screenshots are captured explicitly from
the presentation path.

## Audio data flow

```text
core audio_update() stereo S16 samples
        |
bounded single-producer/single-consumer ring
        |
SDL3 audio stream/device callback
        |
selected host output device
```

The ring capacity derives from configured latency and is always bounded. The producer
and consumer track underruns, overruns, peak occupancy, and dropped samples. Normal
pacing uses audio occupancy plus a monotonic frame deadline without low-resolution GUI
timers. Pausing stops production and silences/pauses the host stream. Fast-forward may
mute or drain samples according to settings, but cannot accumulate backlog.

## Input data flow

```text
Qt keyboard events + SDL3 controller events
        |
profile mapping, deadzone, assignment, hotkey conflict policy
        |
immutable logical InputSnapshot
        |
atomic/coalesced worker command
        |
core input globals updated once at frame boundary
```

Host controls are represented independently from Genesis Plus GX device constants.
Device adapters translate the logical snapshot only on the emulation thread. Hotkeys
are evaluated before gameplay mappings with explicit conflict diagnostics. Controller
hot-plug changes assignments through a stable device identity where SDL provides one.

## Persistence and settings

Platform services resolve data locations using Qt standard paths. Tests inject a
temporary root and never touch real user data. Within the application root, the planned
layout is:

```text
config/settings.json
config/games/<game-id>.json
saves/<game-id>/cartridge.srm
saves/<game-id>/scd-internal.brm
saves/<game-id>/scd-cartridge.brm
states/<game-id>/slot-0.gpgxstate ... slot-9.gpgxstate
screenshots/
library/library.sqlite3
logs/frontend.log
```

`game-id` is a content-derived cryptographic identifier with sanitized title used only
as a human-readable prefix where helpful. Persistence uses bounded reads, temporary
files in the destination directory, flush/close validation, then atomic replacement
where the platform permits. State wrappers contain a magic value, schema version,
game ID, system, core compatibility metadata, timestamp, payload length, checksum, and
the unchanged raw core state payload. Wrong-game states are rejected before core access.

Settings have a versioned schema with migrations. Global settings are loaded first;
an existing per-game override is applied only for fields explicitly marked overridden.
Defaults and validation are pure, testable logic. UI Apply sends a complete validated
snapshot rather than mutating core globals piecemeal.

## Game loading and library

The file loader normalizes a path, checks that it is a bounded regular file (or a valid
disc manifest), identifies the format from content plus extension, and delegates the
actual emulation interpretation to the core. Archive enumeration validates filename
lengths and uncompressed sizes. Unsupported formats and missing BIOS files produce
typed errors with concise user messages and detailed logs.

The library scanner runs outside the GUI thread, parses bounded metadata without
initializing the emulator, and submits database batches. SQLite operations use
transactions, schema migrations, integrity checks, and recoverable rebuild behavior.
No network access is part of scanning or artwork handling.

## Error handling and diagnostics

Frontend APIs return structured errors with a stable category, operation, safe user
message, and diagnostic detail. Expected invalid user input never relies on assertions.
Logging records lifecycle transitions, devices, BIOS status, persistence operations,
and bounded timing/audio anomalies without frame-by-frame noise or secrets.

Dialogs are routed through an interface so GUI tests can replace native file and
message boxes. Important widgets and actions have stable `objectName` values.

## Shutdown order

Shutdown is an explicit, idempotent workflow:

1. disable new UI commands and enqueue worker shutdown;
2. stop frame execution and flush dirty SRAM/BRAM on the emulation thread;
3. stop and close the SDL3 audio stream/device;
4. publish final operation results and terminate the emulation thread;
5. stop controller/library workers and join them;
6. release OpenGL resources while their context is current;
7. persist frontend settings/library state and destroy the GUI.

Each stage has tests for no-game, running, paused, audio-disabled, dirty-save, and
fullscreen variants. No detached thread is permitted.

## Upstream maintenance boundary

Core source paths remain unchanged. Core compile definitions are centralized in one
CMake target and adaptations are confined to desktop bridge files whenever possible.
Any unavoidable core patch must be minimal, separately documented, and covered by core
regression tests. Existing makefiles/frontends remain independently buildable.
