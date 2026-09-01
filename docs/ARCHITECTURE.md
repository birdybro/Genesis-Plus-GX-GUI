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
desktop/localization/    catalog discovery, installation, and fallback policy
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
For a GUI launch, it loads the versioned appearance preference and installs the selected
`QTranslator` before constructing MainWindow or any settings dialog. The translation
manager searches only application-owned resource directories, keeps one installed
catalog alive for the process lifetime, and falls back atomically to English source
text. A system-language fallback preserves the host locale; an explicit English choice
uses a stable English locale. Language changes are restart-boundary transactions so
existing widgets cannot form a mixed-language object graph.
`desktop/ui` owns native widgets but has no dependency on core headers; later runtime
services are connected through narrow coordinator interfaces rather than by giving the
window direct core access.

`MainWindow` provides the conventional File, Emulation, Video, Audio, Input, Tools, and
Help hierarchy, a central accessible display surface, and separate game/system/region,
FPS, and state-slot status fields. Controls and dialogs have stable Qt `objectName`
values. Modal-looking dialogs use asynchronous `QDialog::open()` so neither production
coordination nor GUI tests require a nested blocking event loop. The empty shell is
tested directly using Qt's offscreen platform. A separate hermetic process test launches
the real executable with a build-local application-data root and dummy audio, requires
MainWindow renderer selection and event-loop entry in the structured log, then verifies
that the production shutdown sequence completed and initialized artifacts stayed inside
that root.

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
reset, frame advance, fast-forward/slow-motion modes, speed configuration,
save/load/delete state, input snapshot,
settings and firmware snapshots, decoded cheat patch lists, disc change/eject, and
shutdown. Commands with
superseding semantics (input and live settings) are coalesced. A failed coalescing
search does not move from the command that is subsequently queued. Lifecycle,
persistence, and disc commands retain ordering. The queue has a fixed capacity and
reports saturation rather than growing without bound.

The GUI keeps each of fast-forward and slow-motion's configurable momentary hold and
toggle latch as distinct states, then sends only each mode's effective logical OR
through the bounded command path. The application-wide Qt event filter consumes the
hold combinations for widgets owned by the main window and forces their release on
focus loss, hide, game close, or configuration replacement. Rewind, slow motion, and
fast forward are mutually exclusive at both the GUI and worker boundaries. The core
continues to receive complete ordinary frames; only the frontend's host deadline mode
changes.

Rewind history follows the same owner-thread rule. After a configured number of forward
frames, the worker captures the core's unchanged raw state payload into a deque whose
aggregate payload bytes cannot exceed the configured limit. Oldest entries are evicted
first. A rewind tick pops the newest state earlier than the current frame, restores it
with its frontend frame number, executes one frame to refresh the framebuffer, publishes
through the existing triple buffer, and writes no host audio. Lifecycle discontinuities
clear history so a state can never cross games, resets, discs, or incompatible settings.

```text
forward frame -> raw state capture -> byte-capped rewind deque
                                          |
rewind command -> earlier state restore <-+ -> video exchange
                                                (audio ring cleared)
```

Run-ahead is a separate forward-only path on that same owner thread. The worker captures
the portable raw state plus an opaque in-process rollback context for transient 68000,
viewport, pause-input, filter, and resampler state. It executes one through four future
frames without publishing their audio or intermediate video, retains only the final
native framebuffer, restores the exact context and latest input snapshot, then executes
one authoritative frame. That authoritative frame supplies host audio, frame count,
rewind history, save data, and recording cadence; the speculative final frame supplies
only the displayed/captured video.

```text
                    +--> speculative frames -> final native video --+
current state/input |                                               |
                    +--> exact rollback -> authoritative frame -----+-> one publish
                                             |                          video + audio
                                             +-> audio / state / history
```

The first speculative and authoritative one-frame continuations must serialize to the
same raw state. A mismatch permanently suspends speculation for that loaded session and
falls back to its already-completed authoritative frame. Fast-forward, slow motion, and
rewind suspend run-ahead without discarding its verified status. Lifecycle or
state-affecting setting changes clear verification. Sega CD reports run-ahead as
unsupported. Standard pad handshake state is part of the transient context; specialized
devices without an exact transient snapshot report the feature as unsupported. Reused
vectors and the fixed native-video scratch buffer bound allocation;
their current/capacity bytes and execution counters are exposed through worker metrics.

The core adapter exposes observation-only capacities for its fixed framebuffer, audio,
save-state, and state-load scratch buffers. Worker metrics likewise expose command/event
queue depths together with configured capacities. The long-running regression samples
these values before and after accelerated execution; instrumentation does not alter core
algorithms, buffer ownership, or scheduling.

The worker emits immutable events containing operation IDs. The coordinator discards
stale completion events after a newer load/unload generation, preventing late UI
updates from a previous game.

Developer inspection uses the same owner-thread boundary. The hidden debug window
submits a typed `debugRequest` into the fixed-capacity command queue; the worker invokes
the C host bridge only between frames and publishes an immutable response event. A
snapshot contains bounded CPU, VDP, sound, input, and RAM copies, so no widget retains a
pointer into Genesis Plus GX globals. Arbitrary memory transfers are separately capped
at 4096 bytes. The worker permits observations while running but rejects register and
memory mutation unless its authoritative state is paused.

```text
DebugToolsWindow -> bounded worker command -> CoreAdapter -> C host bridge -> core
       ^                                                            |
       +----------- immutable snapshot/response event <-------------+
```

The window keeps at most one snapshot and one memory read outstanding. Its 250 ms
refresh timer does not drive emulation, queue duplicate work, or allocate per frame.
Closing or hiding the window stops polling; disabling the opt-in setting destroys it.
Save-state buttons deliberately route through the normal asynchronous state manager,
retaining game identity, checksum, atomic-write, and wrong-game protections.

RAM search is pure frontend analysis over the immutable 64 KiB 68000 and 8 KiB Z80
copies. The Z80 copy selects the Genesis sound-CPU RAM for Genesis/Sega CD hardware and
the active work RAM for SG-1000, Master System/Mark III, Game Gear, and Power Base
Converter sessions. Candidate storage is capped at 65,536, watches at 256, displayed
candidates at 1,024, and breakpoint configuration at 64. Frame breakpoints live only
on the worker.
When the list is nonempty, one lightweight C-host call samples both program counters
after a complete frame; a match pauses pacing and emits a typed hit event. There is no
CPU instruction hook, no GUI-side core access, and no normal-play per-frame cost while
the list is empty.

## Per-game settings resolution

```text
selected game path
       |
bounded metadata worker -> SHA-256 GameIdentity -> sparse schema 1 file
                                                    |
global video/audio/system/input/BIOS ---------------+
                                                    |
                                           deterministic precedence
                                                    |
                         system + firmware + video + audio commands
                                                    |
                                           ordered before core load
```

`PerGameSettings` contains optional whole-category snapshots. Absence means inheritance,
not a copied default. `resolvePerGameSettings()` is the sole precedence function and
produces a complete immutable effective snapshot. Host audio device and ring latency
always remain global because the SDL stream and the ring shared with the worker are
process resources; per-game audio covers gain/mute and core sound settings.

The composition root preflights metadata off the GUI thread before submitting a load,
so region, hardware, clock, and BIOS overrides participate in the first core
initialization. The existing coalescing settings commands retain FIFO order with the
lifecycle command. Active global settings and effective game settings are held
separately: quick settings update a category's existing override, or the global layer
when that category inherits. Failed replacement loads reapply the saved previous
effective snapshot. Successful unload returns presentation, audio, input, and deferred
core configuration to globals.

The bounded atomic file embeds the complete SHA-256 and sanitized title slug. Invalid
identity, nested values, paths, sizes, JSON, and future schemas fail closed. Saving an
empty override removes that one exact file, ensuring a title that uses globals does not
accumulate configuration artifacts.

## Cheat data flow

```text
local .cht/.txt ----> bounded system-aware import ----> disabled table rows
                                                        |
RAM snapshot <---- token-routed owner-thread request     |
     |                                                  |
bounded search ----> generated disabled RAM code --------+
                                                        |
Qt table text -> exact system-aware decoder -> atomic per-game JSON
                         |                         |
                         +--> enabled typed patches
                                      |
                           bounded/coalesced command
                                      |
                              emulation thread
                                      |
                    desktop C host patch lifecycle
                            /                 \
               ROM restore/apply       RAM VBlank update
```

Metadata hashing creates the same collision-resistant game identity used by other
persistence services. The actual loaded hardware byte—not an extension guess—selects
Genesis-family versus 8-bit code decoding, so archived or generically named files do
not receive the wrong grammar. Text and JSON never cross the core boundary.

The C host bridge owns at most 150 fixed cheat entries. Genesis ROM words are restored
in reverse before replacement or unload. Master System mapper hooks reapply validated
byte patches after bank changes while retaining the prior bank byte for restoration.
RAM updates use the same VBlank/input callback point as the existing libretro frontend,
including Sega CD program and word RAM. All mutation occurs under `CoreAdapter`'s
thread/lease checks; the GUI cannot call these functions directly.

Cheat-list import accepts only bounded valid UTF-8 and typed code strings for the
active hardware. It is atomic and forces every imported row disabled. RAM search uses
the existing immutable debug snapshot bridge but is available in the normal cheat
manager; opaque client tokens isolate its replies and errors from the opt-in debugger.
Search results are bounded display data and become disabled table rows, never direct
memory writes. Thus both discovery routes rejoin the same validation, persistence, and
owner-thread application boundary shown above.

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
long-run drift from truncating each individual frame duration. The worker releases its
queue mutex, sleeps against the monotonic deadline, then checks commands before running
the frame; no busy loop or Qt timer executes frames. Active-emulation command latency is
therefore normally bounded by one native frame (about 16.7 ms), while paused/idle
command waits remain immediately interruptible. Recoverable host-scheduler delays retain
at most eight frame intervals of schedule debt so the worker can catch up without
long-running A/V drift. A gross stall beyond that bound resynchronizes to one interval
after the current monotonic time instead of running an unbounded catch-up burst.

Windows begins the worker thread with a scoped 1 ms Multimedia Timer resolution request
and balances it during thread teardown. This is required because the platform's default
scheduler tick is coarser than the shortest configurable fast-forward deadlines.
macOS converts the remaining steady-clock duration to a native `mach_wait_until`
deadline because hosted libc++ deadline sleeps can be coalesced far beyond a video
interval; its dedicated worker also requests the user-interactive thread QoS class.
Linux uses the standard library sleep directly. These services change only scheduling
resolution; all cadence and drift decisions remain in the platform-independent
`FramePacer`.

Pause removes the active deadline. Resume starts immediately from a fresh origin, and
frame advance runs exactly one frame without activating the pacer. A versioned speed
snapshot selects 50–200% normal, 25–75% slow-motion, and 200–1600% fast-forward pacing.
`FramePacer` multiplies the rational denominator by the exact integer percentage rather
than rounding through floating point, so every mode retains long-run remainder
accumulation. Configuration and mode commands are applied only by the core owner and
reset the next absolute deadline without accumulating old schedule debt.

Core audio batches are always drained, but enter the host ring only when effective speed
is exactly 100%; the composition root also pauses and clears SDL at every other rate.
This deliberately provides silence instead of pitch-shifted playback until a future
time-stretch subsystem exists, while preventing an overrun or steadily growing A/V
backlog. Metrics expose scheduled/late frames, resynchronizations, maximum lateness,
configured/effective target rate, percentage, and fast/slow state.

The composition root samples the worker's monotonic scheduled-frame counter every
500 ms with `FrameRateSampler` and publishes the observed rate to the status bar. The
sampler re-baselines on pause, resume, lifecycle counter reset, or a backward test clock,
so it cannot display a reset spike. This observer only reads metrics; the emulation
thread's absolute deadline remains the sole frame clock. Loaded system/region text comes
from the bounded metadata preflight, with the core's detected Sega CD region taking
precedence.

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
optional librashader Slang pass chain into bounded output texture
        |
optional cached local bezel (before game) / alpha overlay (after game)
        |
presentation pass + aspect/integer-scale/explicit-aperture viewport
        |
window / fullscreen / high-DPI surface
```

Slots are allocated for the core's maximum supported surface and reused. A published
frame carries width, height, pitch, crop/overscan viewport, pixel aspect, region,
interlace state, and monotonically increasing frame number. The GUI may skip obsolete
presentation frames but never observes a partially written frame. Native screenshots
copy the `DisplayWidget`'s latest complete presented frame. Starting a new load clears
that presentation snapshot so a request cannot be mislabeled with the next game's name.

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

`PresentationTelemetry` makes the consumer-side bound explicit. A copied generation
replaces any still-pending generation, so the GUI retains exactly zero or one pending
frame and counts the replacement as coalescing. Rendering and actual `frameSwapped`
notifications are counted separately; monotonic swap intervals provide average FPS,
average interval, and worst interval without allocating a sample history. Exchange
publication/copy/skip/drop counters are combined with these GUI-thread metrics in the
diagnostics report and five-second anomaly logging.

`DisplayWidget` selects an accelerated `QOpenGLWidget` canvas on normal window-system
platforms. A persistent RGB565 texture is allocated only for geometry changes and each
new generation uses `glTexSubImage2D`; a small presentation program draws the texture
with selectable nearest or bilinear sampling. Both mutable textures explicitly expose
only mip level zero, so a Libretro pass that binds a mip-capable sampler cannot make the
one-level input incomplete. The OpenGL viewport converts the pure logical layout to
device pixels for high-DPI/Retina surfaces. Before `QApplication` exists, the process
sets the same OpenGL 3.3 core format used by the canvas; this makes Qt's top-level
backing-store compositor and `QOpenGLWidget` contexts shareable, including on native
Wayland. Context, program, VAO, or texture failure
switches asynchronously to Qt's portable backing-store painter. The same deterministic
software path is selected for offscreen/minimal platforms and by the explicit
`GENPLUSGX_FORCE_SOFTWARE_VIDEO` diagnostic override.

The presentation configuration requests OpenGL swap interval 0, 1, or -1 for off, on,
or adaptive synchronization and requests Qt double or triple buffering. The OpenGL
profile is established before `QApplication`; persisted swap policy is refreshed before
the first window, and a live change recreates only the `QOpenGLWidget` surface. The
context's effective interval and buffering are reported independently because Qt, the
window compositor, or the driver may substitute unsupported requests. Surface rebuild
retains the owned framebuffer and shader/settings snapshot. Failure is associated with
the exact canvas generation, preventing a delayed failure callback from deleting a
newer replacement, and falls back to software rendering.

When enabled, `LibretroShaderRuntime` dynamically resolves the pinned librashader C API
and builds one OpenGL chain from the built-in or user-selected `.slangp` preset. It
passes the source texture, frame number, nominal PAL/NTSC rate, original aspect, and
validated parameter overrides to librashader, which owns preset-scoped intermediate,
history, lookup-texture, and multi-pass resources. The frontend supplies one
viewport-sized RGBA8 output texture and presents it with the existing final program;
resize reallocates that output only when physical geometry changes. A configuration
generation rebuilds the chain exactly once. Any load, compile, ABI, or render error is
reported once and bypasses the chain while normal emulation/presentation continues.
The software renderer likewise reports that OpenGL 3.3 is required without hiding the
application shell.

Local artwork is a separate frontend-only composition layer. `ArtworkConfiguration`
validates the off/bezel/overlay mode, 1–100% opacity, persisted path, and four bounded
percentage insets. `ArtworkImage` rejects non-files, inputs over 32 MiB, dimensions over
4096×4096 or 16,777,216 pixels, unsupported decoders, and opaque foreground images. The
GUI thread decodes a successful file once to one RGBA cache; OpenGL uploads it only
when its configuration generation changes, while the software painter reuses the same
`QImage`. No artwork-sized allocation or file read occurs per emulated frame.

Bezel mode draws the cache before the game; overlay mode alpha-composites it afterward.
Both paths use the same upright texture convention. `calculateArtworkVideoLayout()`
applies aspect and integer-scaling policy inside an optional explicitly configured
aperture. Transparent pixels are never inspected to guess geometry, and input/core
coordinates are never changed. A failed image transaction restores the prior settings;
startup failure leaves ordinary unconstrained video active. Diagnostics expose mode,
availability, format, dimensions, and bytes without exposing the local path.

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

Shader selection and parameter values are frontend-only members of `VideoSettings`.
The schema-1 to schema-2 migration adds a disabled shader default; schema 3 adds
synchronized double-buffer presentation defaults without altering prior video behavior.
Schema 4 adds disabled local artwork defaults. Sparse per-game Video overrides carry
the same validated configuration and accept files that predate all three presentation
extensions. Neither shaders nor artwork enter `CoreAdapter` or modify Genesis Plus GX
output algorithms.

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
name, ring latency, master gain, and mute. Gain and mute are lock-free atomics consumed
by the SDL callback. Device and latency changes are transactional live operations: the
old stream remains available until a replacement opens, and the worker keeps the same
shared ring object.

The ring's logical capacity derives from configured latency and is always bounded by a
fixed maximum backing allocation. Live capacity changes set a short reconfiguration
gate, wait for in-flight producer/consumer operations, reset sequence counters, and
publish the new bound without reallocating or replacing the shared object. Producer
writes during the gate are explicitly counted as dropped and callback reads become
instrumented silence. The producer and consumer track underruns, overruns, peak
occupancy, and dropped samples. Normal
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
with a diagnostic. The GUI event pump drains at most 64 audio hot-plug events per tick.
SDL automatically migrates streams opened on the default logical device; removal of an
explicitly selected device destroys its stream and reopens the default while retaining
the shared bounded ring and pause state. The device list and diagnostics then refresh.
An explicit user device change opens the requested stream before retiring the old one;
latency-only changes pause and clear the existing stream. Either path restores the old
configuration if a control step fails, and a stopped output can be retried from the
Audio Settings workflow.
The callback applies bounded integer master gain after filling
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

The profile's separate `CoreInputSettings` snapshot follows the same bounded command
path and is applied only by the core-owning thread. It configures `input.system[]`, exact
three-/six-button pad types, specialized device ports, and live `io_init`/`input_reset`
before subsequent snapshots are consumed. Contiguous 3–8 pad layouts select Team Player
on 16-bit hardware or Master Tap on 8-bit hardware; unconfigured multitap slots are
changed back to `NO_DEVICE`. Pico and Terebi remain subject to the core's hardware/game
detection while retaining explicit device-slot propagation.

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
unmodified application-hotkey collisions. It also rejects gapped devices, non-pad
multitap layouts, and multi-device tablet layouts. `InputProfileStore` serializes bounded JSON
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
persistence failure while retaining the requested live behavior. The same operation
publishes the emulated-device snapshot; per-game profile selection publishes it before
the queued game-load command.

## Persistence and settings

Platform services resolve data locations using Qt standard paths. Tests inject a
temporary root and never touch real user data. Within the application root, the
implemented layout is:

```text
config/video-settings.json
config/audio-settings.json
config/system-settings.json
config/input-profiles.json
config/recent-games.json
config/appearance-settings.json
config/screenshot-settings.json
config/bios.json
config/per-game-settings/<game-id>.json
config/cheats/<game-id>.json
saves/<game-id>/cartridge.srm
saves/<game-id>/scd-internal.brm
saves/<game-id>/scd-cartridge.brm
states/<game-id>/slot-0.gpgxstate ... slot-9.gpgxstate
screenshots/
recordings/<game>-<timestamp>.gpgx-recording/
library/game-library.sqlite3
logs/frontend.jsonl
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
snapshot rather than mutating core globals piecemeal. Video, system, and input changes
cross a result-bearing composition callback before MainWindow publishes the new snapshot.
The composition root stages the worker/runtime change, commits the appropriate global or
per-game store, and rolls runtime state back on commit failure. A rejection keeps the
editor open, restores menu checks, and reaches a concise dialog while the log retains the
low-level detail. Controller assignment uses the same typed rejection path.

Appearance schema 3 stores a closed language preference (`system`, `en`, or the
packaged testing locale `en_XA`) beside the theme. Older schemas migrate to `system`;
unknown values fail validation rather than becoming arbitrary catalog paths. Requested,
effective, and fallback status are copied into diagnostics without exposing a user
path. Translatable presentation text is never used as an object name, settings key,
log field, command value, or core-facing enum.

The screenshot directory is stored independently in
`config/screenshot-settings.json`. F12 takes one immutable RGB565 copy on the GUI
thread, then submits it to `ScreenshotService` through a fixed-capacity command queue.
The dedicated worker performs deterministic RGB565-to-RGB32 expansion and PNG encoding
in a same-directory temporary file, then renames to a sanitized timestamp/frame name.
A fixed-capacity event queue reports the final path or concise failure to the GUI. The
emulation thread, core surface, renderer, and settings store are never touched by the
encoder thread, and the application joins the worker explicitly during shutdown.

Continuous lossless capture uses a separate `EmulationCaptureSink` boundary. After a
complete core video/audio batch has been copied on the owner thread, the worker offers
it to `RecordingService`; the sink never calls into the core. Eight maximum-surface
slots are allocated before capture begins. Submission copies into a free slot and
returns immediately, or accounts one dropped A/V frame when the fixed queue is full.
The dedicated writer expands RGB565 into sequential native PNGs, emits stereo PCM WAV
samples plus a JSON Lines frame index, and writes a schema-versioned manifest.

```text
core frame + audio -> emulation-owner capture tap -> preallocated 8-slot queue
                                                        |
                                                        v
                                     recording writer (PNG/WAV/JSONL)
                                                        |
                         .gpgx-recording.partial -> atomic directory rename
```

The queue, per-frame sample count, PNG size, session frame count, and total output bytes
all have hard limits. A dropped slot loses its matching video and audio together. Host
mute/volume and presentation shaders stay downstream and therefore do not alter the
native recording; rewind replaces the otherwise discarded batch with equal-length
silence. GUI start/stop requests and writer completion are asynchronous immutable
events. Game replacement/close initiates finalization, and shutdown joins emulation
before draining and joining the writer, so no capture callback can outlive it.

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
lowercase SHA-256 content identifier. A single-file game keeps the conventional raw-file
SHA-256. A CUE game uses a domain-separated, length-framed stream containing the
validated sheet and each safely resolved track in directive order. Absolute paths are
never part of that digest, so identical relocated content stays stable while any changed
data/audio track selects a different save, state, cheat, and override identity. Raw
cartridge SRAM, CD internal BRAM, and CD RAM cartridge files retain distinct stable
names. Writes use `QSaveFile` with direct-write fallback disabled, so a failed
transaction cannot truncate an existing save; reads are regular-file checked and size
bounded before allocation. The content stream checks cooperative cancellation between
64 KiB chunks. The core worker supplies its atomic stop request while establishing live
backup ownership, so shutdown can interrupt a large disc identity without racing core
globals.

An explicit `--portable` request instead calls
`ApplicationPaths::fromPortableExecutable()`. Windows/Linux roots are the executable's
sibling `portable-data`; a recognized macOS `Contents/MacOS` executable climbs outside
the `.app` first. Resolution is lexical, absolute, independent of the current working
directory, and rejects a filesystem-root placement. Mode selection happens before any
logger, settings store, library connection, worker, or UI is constructed. Initialization
failure is fatal in portable mode, so the composition root cannot mix portable and
platform-standard stores. `ApplicationPaths` carries its mode into the Paths page and
privacy-safe diagnostics without exposing the root there.

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

Schema-2 save-state files use a fixed 176-byte little-endian `GPGXST01` envelope, a
checksummed length-framed presentation block, and the unchanged raw Genesis Plus GX
payload. The envelope records its schema/header lengths, millisecond timestamp,
hardware, slot, emulated frame number, full game SHA-256, payload length/SHA-256,
presentation length/SHA-256, and raw core version signature. The presentation block
holds an optional bounded UTF-8 name and decoded/bounded PNG thumbnail; schema-1
128-byte states remain readable. The manager accepts only slots 0-9, payloads up to
2 MiB, and thumbnails up to 512 KiB/1024×1024, validates the entire envelope before
exposing bytes, and uses the same atomic transaction primitive as RAM persistence. The adapter independently
asks the running core for its exact hardware-specific state size before calling the
core's lengthless `state_load()` API. It keeps the current raw snapshot in reusable
storage and reloads it if the core rejects a candidate, making failed loads transactional.

Save-state UI file work runs on `StateStorageService`, a dedicated bounded worker that
owns `SaveStateManager`. Activating a successful game computes its content identity and
scans slots away from the GUI thread; the service's atomic stop request can cancel that
identity stream between chunks. Each summary is `empty`, fully validated
`available`, or `invalid`; available summaries carry timestamp, frame, size, name, and
thumbnail metadata, while invalid entries retain a safe diagnostic and remain
deletable. The GUI keeps one state operation in flight:

```text
quick save: GUI -> core-worker capture -> storage-worker atomic wrap/write -> GUI
quick load: GUI -> storage-worker validate/read -> core-worker restore -> GUI
delete:     GUI -> storage-worker remove/refresh -> GUI
rename:     GUI -> storage-worker validate/rewrite/refresh -> GUI
import:     GUI -> storage-worker validate/re-slot/atomic write -> GUI
export:     GUI -> storage-worker validate/atomic copy -> GUI
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
duplicate the emulator's content interpretation. The direct core host accepts `.68k`,
`.bin`, `.bms`, `.cue`, `.gen`, `.gg`, `.iso`, `.md`, `.mdx`, `.sg`, `.sgd`, `.smd`,
and `.sms`; `.chd` is added only when bundled libchdr support is compiled.
`GameLaunchTarget` keeps that runtime path separate from the user-selected source path.
The source resolver adds bounded container and transformation layers:

```text
ZIP source -> validated central directory -> selected cartridge -> cache/runtime path
M3U source -> validated ordered local discs -> first/current disc runtime path
cartridge runtime + IPS/BPS/UPS -> validated immutable patch cache -> core runtime path
```

ZIP enumeration is capped at 4,096 entries and 512 MiB per archive. Only stored or
deflated cartridge members are accepted; names, encryption, compression method, ratio,
uncompressed size, exact byte count, trailing stream status, and CRC are validated
before a collision-safe cache publish. M3U text is capped at 256 KiB, 1,024 bytes per
line, and 32 unique discs. It must be valid UTF-8 and may reference only existing,
validated relative disc paths that remain beneath the playlist directory after
canonicalization. Container parsing never runs the emulator core or dereferences an
untrusted offset directly.

The soft-patch layer is frontend-only. `game_patch.cpp` implements IPS records, all
four BPS action types, and reversible UPS XOR records from their public format
specifications; no emulator-core source is changed. Input patches are capped at 64 MiB,
source/output cartridges at 32 MiB, and every variable integer, relative copy, literal,
RLE run, metadata range, and output range is checked before access. BPS and UPS validate
their source/output/patch CRC-32 fields; IPS has no checksum field. Exact results are
published through a same-directory temporary file to a content-keyed, collision-safe
cache name. `GameLaunchTarget` therefore carries three distinct paths:

```text
sourcePath  = user-selected file retained by recents/library/session
patchPath   = optional user-selected or discovered IPS/BPS/UPS file
runtimePath = exact validated bytes passed to metadata, persistence, and core services
```

This keeps save RAM, states, cheats, and per-game overrides isolated by the patched
SHA-256 without modifying the source. Same-stem sidecar discovery is deterministic and
fails closed when multiple candidates exist. Explicit patches may follow ZIP member
selection; automatic ZIP sidecars and all disc/playlist patching are intentionally
excluded because their target content would be ambiguous.

Native file selection is abstracted behind `DialogService`, enabling production Qt
dialogs and deterministic GUI tests. Open actions, one-game or game-plus-patch drops,
and the command-line game/optional `--patch` pair all converge on
`MainWindow::requestGameLoad()`. The
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
disabled. Add and clear operations first commit a copied model, then publish it to the
menu, so an unwritable history file leaves the prior in-memory list intact and produces
a visible error.

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
downloads, modifies, or copies a firmware file. Structurally valid Genesis, regional
Master System, Game Gear, and Sega CD paths are projected into a
`CoreFirmwareSettings` value and queued to the core-owning thread. Before the next load,
the adapter copies every path into the bounded C host interface, validates and mirrors
the optional Genesis boot image, and enables the core's normal cartridge-firmware handoff.
Changing firmware does not mutate a running machine and takes effect on the next load.

## Sega CD session and disc flow

The adapter stages independent USA, Europe, and Japan firmware paths. Before each game
load it copies them into the desktop host boundary and clears the core's cached CD BIOS
marker. Genesis Plus GX remains authoritative for disc format and region detection,
then selects the detected region's firmware. A missing BIOS is translated into a typed,
region-specific frontend error after the core has unmounted the attempted image.

```text
BIOS manager -- validated regional paths --> bounded firmware command
                                                  |
Open Game / CLI / drop --> load command ----------+--> emulation thread
                                                        |
                                           core disc detection + BIOS load
                                                        |
                                      Paused Sega CD session + disc metadata

Change Disc / Eject --> typed command --> cdd mount/tray state --> result event --> GUI
```

Disc UI is enabled only when a loaded event identifies `SYSTEM_MCD`. Change requests
preflight the build's `.bin`, `.cue`, `.iso`, and conditional `.chd` set, then the core
mounts the complete image on its owner thread. CUE preflight uses a 1 MiB file bound,
the inherited parser's 127-byte line bound, sequential track/index validation, and
canonical directory containment for every referenced file. Absolute, traversal,
missing, empty, directory, and symlink-escaping references never cross the core boundary.
A preflight failure preserves the mounted disc. A later core mount failure leaves the
existing CD session alive, the tray open, audio output cleared, and no stale disc path.
Closing the tray resumes the mounted image through the same status transition used by
the upstream libretro frontend. CUE/BIN, CDDA, and CHD decoding remain upstream core
responsibilities; the frontend adds no alternate sector or audio algorithms.

Internal 8 KiB BRAM and the 512 KiB RAM cartridge are discovered immediately after CD
initialization and use the normal per-content atomic persistence lifecycle. Disc swaps
do not change the game identity or reinitialize backup memory. Tests exercise ISO and
CUE/BIN mounting, unsafe preflight without active-disc mutation, USA/Europe BIOS
selection, eject/close, failed swap recovery, a frame, both BRAM files, clean worker
shutdown, and an opt-in user-firmware smoke path. No proprietary fixture is part of the
default build or CI.

Generated Z80 fixtures independently load and execute through SG-1000, Mark III,
Master System II, and Game Gear hardware. Their semantic work-RAM marker and native
viewport assertions keep support for the 8-bit systems as a runtime regression gate,
not merely an extension-list claim. Generated non-proprietary cartridge boot images
also verify that all optional BIOS slots reach and activate in the authoritative core.

## Read-only game metadata flow

```text
Tools / Game Information
          |
          v
bounded metadata request queue --> metadata thread --> streaming file reader
          ^                                               |
          |                                 bounded header parser + SHA-256
          +-------- bounded result queue <----------------+
                              |
                       Qt information dialog
```

`GameMetadataService` owns one non-core worker thread and fixed-capacity command/event
queues. It streams files in 64 KiB chunks, retains at most the 64 KiB header window,
and sends owned results back to the GUI event pump. The shared content stream produces
the exact persistence identity: raw SHA-256 for one file or the framed CUE-plus-tracks
digest. It checks an atomic stop request between chunks, allowing service and scanner
shutdown to interrupt a large ROM, CHD, or multi-track disc hash. The composition root
matches both operation ID and path to the still-visible loaded game; stale results after
a load or close are discarded. The parser contains no core calls and therefore cannot
mutate or race Genesis Plus GX globals. Genesis, SMD, 8-bit Sega, raw/cooked Sega CD,
CUE, and conditional CHD paths remain descriptive only. CUE references are constrained
to safe relative paths before hashing or the small data-header read.

## Game-library database and scanner

```text
configured canonical roots --> bounded scan command --> scanner thread
                                                        |
                       metadata parser <--- safe directory walk
                                                        |
                                      batches of at most 128 records
                                                        v
Qt library model <-------- query connection ------ SQLite WAL database
                                                        |
                                      generation transaction / stale cleanup
```

The scanner owns its `GameLibraryDatabase` connection on the scanner thread. A
connection records its initializing thread and rejects cross-thread use. Recursive and
flat walks skip permission-denied entries, never follow symlinks, and stop at a fixed
100,000-file ceiling. The bounded candidate pass validates CUE sheets first and records
their canonical referenced tracks; supported track payloads are then suppressed as
duplicate rows while unrelated standalone `.bin`/`.iso` files remain eligible. Metadata
reaches SQLite in bounded batches while one generation transaction ensures that
cancelled, incomplete, or failed scans cannot delete the last complete index. Rescans
update file-owned fields but preserve favorite, play-count, last-played, and
local-artwork state.

The schema is versioned with SQLite `user_version`, enables foreign keys and WAL, and
validates both `quick_check` and required tables/columns at startup. Corrupt or
structurally incomplete current databases are preserved under a collision-safe
`.corrupt-<timestamp>` name before a clean rebuild. Future schema versions fail closed
without modification. Configured roots are canonical, non-overlapping, and bounded.
No network access is part of scanning or artwork handling.

The modeless Qt library dialog consumes immutable directory/game snapshots from the GUI
thread's read connection and filters/sorts them through a proxy model. UI mutations are
typed callbacks into the application coordinator. They are disabled (and independently
rejected by the coordinator) while the scanner has an outstanding write transaction,
so SQLite's busy timeout cannot stall the event loop. Scanner events refresh the model
only after commit. A successful core load records play history; loads that coincide with
a scan are retained in a small bounded-by-user-actions deferred list and committed once
the scan finishes. Local artwork is decoded to preview dimensions and its path is the
only artwork data stored in SQLite.

## Error handling and diagnostics

Frontend APIs return structured errors with a stable category, operation, safe user
message, and diagnostic detail. Expected invalid user input never relies on assertions.
Logging records lifecycle transitions, devices, BIOS status, persistence operations,
and bounded timing/audio anomalies without frame-by-frame noise or secrets.

Dialogs are routed through an interface so GUI tests can replace native file and
message boxes. Important widgets and actions have stable `objectName` values.

Startup constructs independent services defensively and collects recoverable failures
with a short subsystem label. Once MainWindow is ready, empty and duplicate messages are
removed and at most 12 details plus an overflow summary are shown in one asynchronous
Startup Issues dialog; unaffected services remain available. The same composition path
is process-tested with deliberately malformed settings. OpenGL initialization reports
through a display-owned callback before switching to the software painter, failed SDL
default-device recovery produces an immediate audio dialog, and failure to start the
emulation worker is fatal and visibly reported after auxiliary workers are stopped.
Full diagnostic detail remains in the structured log when logging is available.

After startup, accepted asynchronous work remains correlated by operation ID and active
game generation. A stopped metadata service resolves any waiting load, cheat, or game
information request with a visible error instead of leaving a busy latch set. A failed
save-state service disables state actions. Library-history writes and scanner failure,
audio device control, rejected worker commands, and an unexpected emulation-worker stop
all reach bounded user-visible error paths; repeated frame/input failures are collapsed
to one emulation-service report per game session. An unexpected core-worker stop is
fatal because the single controlled core context cannot be reconstructed safely from
the GUI thread.

Normal exit first disconnects GUI producers, synchronously stops the emulation worker
after an optional automatic-resume transaction. That transaction pauses the worker,
captures raw state on its owner thread, writes the dedicated identity-bound checkpoint
through the state-storage thread, and atomically records the absolute game path only
after storage confirmation. Startup gives an explicit command-line game precedence;
otherwise it holds the loaded core paused until identity activation validates and
restores that checkpoint. Any failure clears the stale marker and starts normally.
The regular shutdown then stops the emulation worker so dirty backup memory is flushed
and the core is released, drains and joins lossless recording, then stops audio,
controllers, state storage, metadata, screenshots, and the library scanner. The display
releases its frame exchange last. `ShutdownReport` retains every service failure, keeps
an existing nonzero application result, upgrades an otherwise-success result when any
cleanup fails, and emits the aggregate before structured logging is removed.

The composition root installs `FrontendLogger` after platform application directories
exist and removes it after the final service shutdown record. The Qt message handler
redacts before serializing one compact JSON object per line; its mutex covers rotation
and the stream only, and it retains fixed byte/file bounds. It forwards to Qt's previous
handler so development stderr behavior is preserved. No emulation frame emits a log.

```text
Qt/frontend events --> redact --> JSONL encoder --> 1 MiB active log
                                                    |
                                                    +--> .1 --> .2 --> .3

safe runtime providers --> DiagnosticsSnapshot --> formatter --> read-only dialog
                                                               --> clipboard
```

The diagnostics provider runs only on the GUI thread when the dialog is opened or
refreshed. It samples lock-free/bounded audio and logger counters, current controller
inventory, display backend, metadata-derived title/system/region, and BIOS validation
states. It never receives content paths. The formatter applies the same path and
credential redaction as a defense in depth before display or clipboard transfer.

## Appearance, DPI, and accessibility

`AppearanceSettingsStore` is a bounded, versioned JSON boundary under the application
config directory. The composition root loads it before constructing any top-level
window. `ThemeController` owns the one process-wide policy: system mode restores the
platform style/palette captured at startup, while explicit light and dark modes select
Qt Fusion and a centralized palette. Widgets do not retain theme state and no theme
operation crosses into the emulation worker or core.

```text
appearance-settings.json --> validated snapshot --> ThemeController --> QApplication
                                     ^                       |
                                     |                       v
                         Preferences callback <--- all open Qt widgets
```

The high-DPI rounding policy is set before `QApplication` construction. Qt therefore
keeps widget geometry, fonts, keyboard focus, accessibility objects, and platform
screen-reader integration in device-independent coordinates. `DisplayWidget` continues
to calculate emulated pixels from its physical render target and the independent
fit/integer-scale policy. This prevents UI scaling from changing emulator geometry.

`SettingsDialog` is the single Preferences entry point and an observation/navigation
layer rather than another persistence owner. It renders eight live category summaries
from immutable copies of the MainWindow snapshots, including the resolved
`ApplicationPaths`, then dispatches typed actions back to MainWindow:

```text
Settings center (General / Video / Audio / Input / System / BIOS / Paths / Advanced)
          |                         ^
          | typed page action       | refreshed live snapshot
          v                         |
existing validated category dialog + category-specific persistence callback
```

The category dialogs retain Apply/OK/Cancel/Restore Defaults semantics and their
existing stores. Failed video, system, and input operations keep the editor open and do
not publish the staged snapshot; other category stores return the same typed failure to
their existing dialog. This keeps a failed write isolated instead of creating a second
cross-file transaction format in the settings center.

## Shutdown order

Shutdown is an explicit, idempotent workflow:

1. stop the GUI event pump, disconnect window/input producers and renderer sinks, and
   disable new commands;
2. wake the emulation worker, stop frame execution, atomically flush available
   SRAM/BRAM on the core-owning thread, shut down the core, and join the worker even
   when a save failed;
3. stop and close the SDL3 audio stream/device;
4. stop controller input and close its SDL handles on the GUI/owner thread;
5. request cooperative cancellation of any in-flight backup, state, metadata, or
   library identity hash, then stop and join the state, metadata, screenshot, and
   library-scanner workers, clearing any pending UI operation rather than leaving it
   busy;
6. release the bounded display frame exchange after its producer has joined;
7. aggregate every cleanup failure without replacing a prior nonzero application exit,
   emit the final structured log, and shut down logging;
8. return the aggregate process status while Qt destroys the window and graphics
   resources.

Each stage has tests for no-game, running, paused, audio-disabled, dirty-save, and
fullscreen variants. `ShutdownReport` normalizes cleanup errors and upgrades an
otherwise successful exit if final persistence or service teardown failed. No detached
thread is permitted.

## Upstream maintenance boundary

Core source paths remain unchanged. Core compile definitions are centralized in one
CMake target and adaptations are confined to desktop bridge files whenever possible.
Any unavoidable core patch must be minimal, separately documented, and covered by core
regression tests. Existing makefiles/frontends remain independently buildable.
