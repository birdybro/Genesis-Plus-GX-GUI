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
Frame waits use interruptible monotonic deadlines derived from the core's current timing
ratio. `stop()` first closes command admission, wakes all waits, performs adapter
shutdown on the owner thread, then joins synchronously. The destructor invokes the same
path, and a stopped worker can be cleanly started again.

## Timing and synchronization

`CoreAdapter::timingInfo()` reads the authoritative values after load: `system_clock`,
`lines_per_frame`, and the upstream constant of 3,420 master cycles per line. The target
ratio is retained as `system_clock / (lines_per_frame * 3420)`—approximately 59.923 Hz
for NTSC and 49.701 Hz for PAL—rather than rounded to 60/50 Hz or to one fixed integer
duration. Sega CD follows the same region-specific VDP frame cadence; the core derives
its sub-CPU cycles per line from that master clock.

`FramePacer` converts the ratio to whole nanoseconds plus a rational remainder and
distributes that remainder across absolute `steady_clock` deadlines. This prevents
long-run drift from truncating each individual frame duration. The worker condition
variable waits until the deadline but remains interruptible by every command and by
shutdown; no busy loop or Qt timer executes frames. A frame more than one full interval
behind resynchronizes to one interval after the current monotonic time instead of
running an unbounded catch-up burst.

Pause removes the active deadline. Resume starts immediately from a fresh origin, and
frame advance runs exactly one frame without activating the pacer. Fast-forward rebuilds
the same rational interval at a bounded 4x rate. Its core audio batch is drained but not
queued because a real-time device cannot consume it at 4x; the composition root pauses
and clears SDL until normal speed resumes. Metrics expose scheduled/late frames,
resynchronizations, maximum lateness, effective target rate, and fast-forward state.

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

`DisplayWidget` selects an accelerated `QOpenGLWidget` canvas on normal window-system
platforms. A persistent RGB565 texture is allocated only for geometry changes and each
new generation uses `glTexSubImage2D`; a small shader draws the texture with selectable
nearest or bilinear sampling. The OpenGL viewport converts the pure logical layout to
device pixels for high-DPI/Retina surfaces. Context, shader, VAO, or texture failure
switches asynchronously to Qt's portable backing-store painter. The same deterministic
software path is selected for offscreen/minimal platforms and by the explicit
`GENPLUSGX_FORCE_SOFTWARE_VIDEO` diagnostic override.

`calculateVideoLayout()` is Qt-independent policy for native pixels, forced 4:3, or
stretch plus fit or integer scale. It validates all dimensions, centers contained output,
uses exact native pixel multiples whenever at least 1x fits, and falls back to aspect-fit
instead of cropping on undersized windows. Both rendering backends consume that one
result, keeping resize and fullscreen behavior consistent.

`VideoSettings` combines those presentation policies with the core-neutral
`CoreVideoSettings` snapshot. The latter contains only settings the desktop core host
actually implements: the four-value overscan mask, Game Gear extended viewport,
single/double-field interlaced rendering, and disabled/monochrome/composite/S-Video/RGB
Blargg NTSC filtering. The adapter owns the large MD/SMS NTSC tables, initializes the
chosen preset before setting the core flag, and refreshes the upstream viewport using
the same notification and horizontal-border policy as the libretro host. No Qt type or
settings-file concern crosses this boundary.

The GUI changes its display policy immediately, then submits the typed core subset as a
worker command. Pending video-settings commands coalesce because only the newest
snapshot matters; a command already being processed remains ordered between frames.
This makes resizing and settings interaction independent of frame execution while
preserving the single core owner.

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

Audio configuration has two explicit ownership domains. `CoreAudioSettings` contains
only Genesis Plus GX mixer, channel, filter, equalizer, sound-chip, and resampling
choices. It travels as a coalescing worker command and is applied between frames by
`CoreAdapter`; switching chip implementations invokes the core's own sound-layer
reinitialization without changing synthesis algorithms. The host snapshot owns device
name, ring latency, master gain, and mute. Device and latency establish resources at
startup, while gain and mute are lock-free atomics consumed by the SDL callback.

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

`AudioOutput` owns SDL's reference-counted audio-subsystem lease and one
`SDL_OpenAudioDeviceStream` playback stream. Its source format is native-endian stereo
S16 at the same configured rate as `CoreAdapter`; SDL performs any host-device format
conversion. The stream begins paused. Its demand callback reads through a reusable
1,024-frame scratch surface, zero-fills shortages, and submits only the current SDL
request. Callback count, requested, supplied, silent, and failed-submission totals are
atomic. Pause first stops callbacks, then clears SDL and ring backlog, so resume cannot
play stale samples. Device initialization failure is reported but does not prevent the
emulator UI or core worker from running.

Playback devices are enumerated through SDL and persisted by display name rather than
their process-local numeric IDs. A missing configured device falls back to the default
with a diagnostic. The callback applies bounded integer master gain after filling
shortages; mute writes silence while continuing to drain the ring, preventing muted
sessions from building a backlog. Neither operation locks or allocates on the audio
thread.

The composition root creates `AudioOutput` before `EmulationWorker` and passes the
service's shared ring to the worker. Non-frame worker events synchronize host pause with
worker state; the GUI event timer never supplies samples. Exit stops the event pump,
stops and joins the worker, and only then destroys the SDL stream and releases the audio
subsystem.

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

The implemented `KeyboardInput` service installs an event filter on the emulator
display and maps Qt keys to Player 1 without referencing core constants. Its default
layout is arrows, Z/X/C for A/B/C, A/S/D for X/Y/Z, Return for Start, and Shift for
Mode. Physical held keys are tracked separately from the logical snapshot so the most
recent opposite direction wins and the earlier direction resumes when it is released.
Auto-repeat never changes sequence state. Focus loss, window deactivation, hide,
detach, and shutdown release all held keys. Ctrl/Alt/Meta press chords bypass gameplay
mapping for application shortcuts; releases are still observed so a chord cannot leave
a previously held gameplay key stuck. Only changed snapshots receive a new monotonic
sequence and enter the worker's coalescing command path. Destruction disconnects the
snapshot sink before clearing internal state, preventing teardown callbacks into
already-destroyed coordinator captures.

`ControllerInput` initializes only SDL3's gamepad subsystem and runs on the GUI thread,
where SDL requires the event pump. Initialization enumerates already connected standard
gamepads; a bounded pump retrieves only SDL's gamepad event range, leaving unrelated SDL
events available to other platform services. Add/remove/remap, button, and left-stick
events update a fixed eight-player snapshot. Controllers occupy the lowest free player
slot by default, retain that slot across events, and may be reassigned; assigning an
occupied slot swaps the two devices instead of silently disconnecting one. Device
handles close before the SDL subsystem is released, and cross-thread shutdown is
rejected.

The standard SDL layout maps west/south/east to Genesis A/B/C, north/left shoulder/right
shoulder to X/Y/Z, Back to Mode, Start to Start, and the D-pad directly. The left stick
provides both signed analog coordinates and digital directions outside an 8,000-unit
deadzone. Held opposite D-pad directions use last-input priority. `InputAggregator`
combines keyboard and controller sources without inheriting either source's sequence;
its own monotonic sequence is the only one submitted to the worker. Buttons from both
sources remain usable for Player 1, cross-device opposite directions become neutral,
and no unchanged or stale source snapshot is published.

Input configuration is a versioned value model rather than widget state. Each named
profile owns keyboard bindings, SDL-standard button bindings, explicit signed axis
mappings, deadzone, and eight logical device selections. The model rejects duplicate
physical controls, invalid enum values, duplicate profile names, unknown schemas, and
unmodified application-hotkey collisions. `InputProfileStore` serializes bounded JSON
through the persistence layer's atomic writer. Legacy schema 0 is parsed into a current
in-memory value and then rewritten by the composition root; malformed data falls back
to defaults without overwriting the source automatically.

`InputConfigurationDialog` edits a private copy and invokes its configuration sink only
after Apply/OK validation, so Cancel has no side effects. Stable named capture buttons
accept Qt key events or button-down events forwarded by `ControllerInput`; capture
events are consumed before gameplay state changes. The assignment page updates in place
when hot-plug changes the device list. Assignment requests are copied before callbacks,
making a callback-triggered device-list refresh safe. The composition root persists an
accepted configuration, applies bindings/deadzone to live input services, and logs a
persistence failure while retaining the requested live behavior.

## Persistence and settings

Platform services resolve data locations using Qt standard paths. Tests inject a
temporary root and never touch real user data. Within the application root, the planned
layout is:

```text
config/video-settings.json
config/audio-settings.json
config/system-settings.json
config/input-profiles.json
config/recent-games.json
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

The implemented video store uses bounded schema-1 JSON and atomic replacement. Schema
0's legacy flat booleans/mask migrate to named enum values; malformed values and future
schemas return defaults plus a diagnostic without overwriting the source. The
composition root applies presentation values before showing the window, queues the core
snapshot after worker startup, and persists only validated user changes.

System configuration is also a typed core-neutral snapshot. It maps supported hardware,
region, VDP standard, master clock, illegal-access lockup, and address-error policy to
core values only inside `CoreAdapter`. Unlike video and audio presentation controls,
these values define machine initialization. An update while loaded changes only the
adapter's desired snapshot; the active machine, timing, and CPU state are untouched.
The complete snapshot is applied after unloading and immediately before the next ROM
load. This prevents a running session from combining a new clock or console identity
with old VDP/audio/CPU initialization. Its worker command still coalesces and remains
ordered with lifecycle commands.

`ApplicationPaths::fromPlatform()` resolves the root through Qt's standard application
data location; every test constructs it with an absolute temporary root. Relative roots
are rejected so persistence can never silently fall back to the process working
directory. A per-game directory uses a conservative ASCII title slug plus the complete
lowercase SHA-256 content identifier. Raw cartridge SRAM, CD internal BRAM, and CD RAM
cartridge files retain distinct stable names. Writes use `QSaveFile` with direct-write
fallback disabled, so a failed transaction cannot truncate an existing save; reads are
regular-file checked and size bounded before allocation.

Live backup-memory ownership remains inside the core worker:

```text
load command
    -> load and initialize core image
    -> compute content identity
    -> read exact-size SRAM/BRAM files
    -> apply bytes (or core-appropriate erased/formatted defaults)
    -> expose Paused state and permit the first frame

unload / replacement / shutdown
    -> copy available backup regions into one reusable bounded scratch buffer
    -> atomically commit each distinct per-game file
    -> release core image and active persistence identity
```

`CoreAdapter` maps the authoritative core regions without exporting their addresses:
64 KiB cartridge SRAM when enabled, the 8 KiB Sega CD internal BRAM, and the configured
Sega CD RAM-cartridge area. Sega CD BRAM defaults use the same format structure expected
by the core. Persisted files must equal the currently reported region size; truncated,
oversized, or invalidly formatted data fails the load before any emulated frame. The
worker performs hashing and file I/O on its owner thread, so GUI timing cannot race a
save. An atomic-write failure blocks unload or replacement and preserves the active
paused/running core. Final shutdown still tears resources down, releases the identity,
and returns a failed status so the loss cannot be silent.

Save-state files use a fixed 128-byte little-endian `GPGXST01` envelope followed by the
unchanged raw Genesis Plus GX payload. The envelope records its schema/header lengths,
millisecond timestamp, hardware, slot, emulated frame number, full game SHA-256, payload
length and SHA-256, plus the raw core version signature. The manager accepts only slots
0-9 and payloads up to 2 MiB, validates the entire envelope before exposing bytes, and
uses the same atomic transaction primitive as RAM persistence. The adapter independently
asks the running core for its exact hardware-specific state size before calling the
core's lengthless `state_load()` API. It keeps the current raw snapshot in reusable
storage and reloads it if the core rejects a candidate, making failed loads transactional.

Save-state UI file work runs on `StateStorageService`, a dedicated bounded worker that
owns `SaveStateManager`. Activating a successful game computes its content identity and
scans slots away from the GUI thread. Each summary is `empty`, fully validated
`available`, or `invalid`; available summaries carry timestamp and emulated frame
metadata, while invalid entries retain a safe diagnostic and remain deletable. The GUI
keeps one state operation in flight:

```text
quick save: GUI -> core-worker capture -> storage-worker atomic wrap/write -> GUI
quick load: GUI -> storage-worker validate/read -> core-worker restore -> GUI
delete:     GUI -> storage-worker remove/refresh -> GUI
```

Both worker channels use operation IDs. The storage channel also requires a monotonic
game-generation ID, so a result queued for a previous image cannot restore into a
replacement. Open/close/recent and state actions are disabled during an operation.
Raw payloads never pass from disk to `CoreAdapter::loadRawState()` until magic, schema,
length, SHA-256, game identity, hardware, slot, and core signature checks all succeed.
The storage queue drains accepted file operations during shutdown; the core worker is
still joined before the storage and audio services are destroyed.

## Game loading and library

The implemented preflight service checks that a selected path uses a format enabled in
the desktop build, is representable by the core's 255-byte host ABI, exists, is a
regular file, and can be opened for reading. It does not inspect untrusted offsets or
duplicate the emulator's content interpretation. The direct host currently accepts
`.68k`, `.bin`, `.bms`, `.cue`, `.gen`, `.gg`, `.iso`, `.md`, `.mdx`, `.sg`, `.sgd`,
`.smd`, and `.sms`; `.chd` is added only when bundled libchdr support is compiled. ZIP
and M3U are intentionally not advertised because archive/playlist enumeration is not
yet present in this desktop host.

Native file selection is abstracted behind `DialogService`, enabling production Qt
dialogs and deterministic GUI tests. Open actions, one-local-file drops, and the single
command-line positional argument all converge on `MainWindow::requestGameLoad()`. The
composition root assigns operation IDs and submits load/unload commands. A successful
load enters worker `Paused`, updates UI identity, applies current input, then submits
`Start`; frame execution never happens on the GUI thread. A different game can replace
the active one without process restart. Invalid preflight choices preserve the running
game, while a core rejection returns the window to the no-game state because the
transaction has already released the previous core image.

Successful load completion also updates a separate `RecentGamesModel`; validation
failures never enter history. The model normalizes paths to absolute form, coalesces
duplicates using platform case rules, retains timestamps, and caps storage at 12
entries. `RecentGamesStore` persists schema 1 JSON through the common bounded atomic
writer at `config/recent-games.json` and explicitly migrates the legacy schema 0 path
array. Invalid/future data falls back to an empty in-memory list without rewriting the
source. Open Recent entries retain full-path tooltips; stale paths are visible but
disabled, and a clear operation only changes history.

## Firmware management

Firmware selection is a platform service rather than an emulator-core or widget
responsibility. `BiosConfigurationStore` owns the bounded schema and atomic
`config/bios.json` transaction. `BiosManager` owns the active path snapshot and an
independently refreshed validation result for Genesis, Game Gear, regional Master
System, and regional Sega CD/Mega CD slots. The Qt page only stages a copy and delegates
native file selection through an injectable callback.

```text
native file picker / test seam
            |
            v
    BIOS settings dialog -- Apply --> BiosManager
            ^                           |
            |              bounded read + SHA-256 + atomic JSON
            +----------- validation snapshot
```

Validation never executes firmware or initializes the core. It checks path capacity,
existence, regular-file status, the size shape accepted by the corresponding upstream
loader, bounded readability, and obviously blank repeated-byte content. SHA-256 and
detected family are identification aids, not a proprietary hash allowlist. This permits
legitimate revisions and user dumps without claiming authenticity. The manager never
downloads, modifies, or copies a firmware file. Milestone 27 consumes only this typed
configuration when it connects Sega CD startup to the core-owning emulation thread.

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

1. stop the GUI event pump, detach input callbacks, and disable new commands;
2. stop controller input and close its SDL handles;
3. wake the emulation worker, stop frame execution, and atomically flush available
   SRAM/BRAM on the core-owning thread;
4. shut down the core, release the active persistence identity, publish final status,
   and join the emulation thread even when a save failed;
5. stop and close the SDL3 audio stream/device;
6. release OpenGL resources while their context is current;
7. persist remaining frontend settings/library state and destroy the GUI.

Each stage has tests for no-game, running, paused, audio-disabled, dirty-save, and
fullscreen variants. No detached thread is permitted.

## Upstream maintenance boundary

Core source paths remain unchanged. Core compile definitions are centralized in one
CMake target and adaptations are confined to desktop bridge files whenever possible.
Any unavoidable core patch must be minimal, separately documented, and covered by core
regression tests. Existing makefiles/frontends remain independently buildable.
