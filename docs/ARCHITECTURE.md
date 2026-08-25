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

## Desktop process and shell

`desktop/app` is the sole process composition root. It establishes the stable
organization, application, desktop-file, and version identity before constructing the
Qt shell. Command-line help and version reporting finish before the GUI event loop.
`desktop/ui` owns native widgets but has no dependency on core headers; later runtime
services are connected through narrow coordinator interfaces rather than by giving the
window direct core access.

`MainWindow` provides the conventional File, Emulation, Video, Audio, Input, Tools, and
Help hierarchy, a central accessible display surface, and separate game/system/region,
FPS, and state-slot status fields. Controls and dialogs have stable Qt `objectName`
values. Modal-looking dialogs use asynchronous `QDialog::open()` so neither production
coordination nor GUI tests require a nested blocking event loop. The empty shell and its
real executable are tested using Qt's offscreen platform.

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

The implemented `EmulationWorker` owns `CoreAdapter` inside a dedicated `std::thread`.
Its public calls never execute core work: they validate and enqueue operations with
nonzero IDs. The default command and operation-event capacities are each 64. Queue-full
submission returns immediately; the newest pending input command replaces an older one.
Operation events use bounded drop-oldest storage with an observable counter, while frame
completion occupies a separate single replaceable slot, so an unresponsive GUI cannot
create frame-sized event growth. Callers may poll or wait on a condition variable.

The worker state machine is:

```text
Stopped -> Starting -> Idle -> Paused <-> Running
                         ^        |
                         +--------+  (unload)
Any live state -> Stopping -> Stopped
```

A successful load enters `Paused`, ensuring no frame runs before its coordinator is
ready. Invalid transitions produce typed asynchronous failures without mutating state.
Frame waits use interruptible monotonic deadlines; Milestone 15 replaces the temporary
nominal interval with exact region/audio-aware pacing. `stop()` first closes command
admission, wakes all waits, performs adapter shutdown on the owner thread, then joins
synchronously. The destructor invokes the same path, and a stopped worker can be cleanly
started again.

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

At the core boundary, `CoreAdapter` retains the mutable 720x576 RGB565 surface and never
returns its pointer. `videoFrameInfo()` validates the effective viewport, including
negative Game Gear crop offsets, positive overscan, NTSC-filter expansion, and optional
interlaced line doubling. `copyVideoFrame()` copies complete rows into caller-owned
preallocated storage and rejects undersized spans. A core viewport notification is
acknowledged only after that copy succeeds. The later triple-buffer exchange owns those
caller-side slots, so this boundary requires no framebuffer allocation per frame.

The implemented `VideoFrameExchange` owns exactly three fixed 720x576-capable RGB565
surfaces. Its single producer acquires a movable lease to a slot that is neither
published nor being read, lets `CoreAdapter::copyVideoFrame()` write directly into that
surface, then atomically publishes metadata plus a monotonic generation. The GUI marks
the published slot as read while copying into its one preallocated receive surface; the
remaining slots keep the producer nonblocking. If no slot is safe, that presentation
frame is dropped and instrumented rather than queued or allocated.

`DisplayWidget` currently uses Qt's portable backing-store painter to render the tight
RGB565 surface with nearest-neighbor sampling, native pixel aspect, centered aspect fit,
and black letterboxing. It remains stable across logical-size and high-DPI backing-store
changes and is fully testable on Qt's offscreen platform. Milestone 13 adds selectable
geometry/filter policy and the accelerated texture-upload backend while retaining this
deterministic software presentation path for headless tests and renderer fallback.

The application composition root shares the exchange between worker and widget. An
8 ms GUI timer drains the bounded event channel and asks the widget to consume only a
newest-frame event; it does not execute or pace emulation. Shutdown exits the Qt loop,
then stops and joins the worker before stack-owned presentation resources are destroyed.

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

`CoreAdapter::runFrame()` drains `audio_update()` immediately into one fixed-capacity
scratch batch so the core's resamplers cannot accumulate merely because the host is
late. The worker copies that batch as structured stereo frames into
`StereoAudioRingBuffer`. An unconsumed scratch batch is replaced by the newest complete
batch with dropped-frame/batch counters; an undersized copy leaves it pending. The SPSC
ring accepts only whole stereo frames, preserves existing queued audio on overrun, and
reports overrun, underrun, dropped/missing frames, occupancy, and peak occupancy without
locking either real-time endpoint.

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

`InputSnapshot` contains eight logical player states, a monotonic sequence, twelve
Genesis-named digital controls, connection state, and signed analog coordinates. Its
bit layout is deliberately independent from the core's `INPUT_*` constants. The
adapter coalesces equal/newer snapshots, rejects older sequence numbers, and translates
the newest pending snapshot immediately before executing a frame. Logical players are
assigned in order to active core device slots, which correctly maps a normal second pad
to slot four while naturally covering Team Player, Master Tap, and J-Cart slot layouts.
Every unused/disconnected core slot is cleared at the same boundary.

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

`ApplicationPaths::fromPlatform()` resolves the root through Qt's standard application
data location; every test constructs it with an absolute temporary root. Relative roots
are rejected so persistence can never silently fall back to the process working
directory. A per-game directory uses a conservative ASCII title slug plus the complete
lowercase SHA-256 content identifier. Raw cartridge SRAM, CD internal BRAM, and CD RAM
cartridge files retain distinct stable names. Writes use `QSaveFile` with direct-write
fallback disabled, so a failed transaction cannot truncate an existing save; reads are
regular-file checked and size bounded before allocation.

Save-state files use a fixed 128-byte little-endian `GPGXST01` envelope followed by the
unchanged raw Genesis Plus GX payload. The envelope records its schema/header lengths,
millisecond timestamp, hardware, slot, emulated frame number, full game SHA-256, payload
length and SHA-256, plus the raw core version signature. The manager accepts only slots
0-9 and payloads up to 2 MiB, validates the entire envelope before exposing bytes, and
uses the same atomic transaction primitive as RAM persistence. The adapter independently
asks the running core for its exact hardware-specific state size before calling the
core's lengthless `state_load()` API. It keeps the current raw snapshot in reusable
storage and reloads it if the core rejects a candidate, making failed loads transactional.

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
