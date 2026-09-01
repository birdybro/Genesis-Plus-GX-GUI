# Display Synchronization

Genesis Plus GX GUI separates emulation timing from host presentation. The emulation
worker's monotonic PAL/NTSC frame pacer remains the only clock that advances the core.
Vertical synchronization changes how Qt and the graphics driver present an already
completed frame; it never executes, delays, or duplicates a core frame.

## Controls

Open **Video → Synchronization** for quick changes, or use **Video → Video
Settings…**. The settings are also available as a per-game video override.

- **On** is the default and requests swap interval 1. It normally avoids tearing.
- **Off** requests swap interval 0. It can reduce presentation latency but may tear.
- **Adaptive** requests swap interval -1. A supporting driver synchronizes except
  when the application misses refresh cadence; other hosts may substitute a supported
  interval.
- **Double buffer** is the default lower-latency request: one front and one back
  buffer.
- **Triple buffer** requests an additional back buffer for smoother host presentation,
  potentially at the cost of an additional display interval of latency.

Qt, the window-system compositor, and the graphics driver have final authority over
swap interval and buffer count. Changing either setting rebuilds only the OpenGL
presentation surface. The latest complete emulated frame, aspect/filter/shader policy,
and running core session remain intact. If OpenGL cannot be recreated, the application
falls back to the normal Qt software renderer rather than stopping emulation.

## Bounded latency behavior

The core publishes into three preallocated exchange surfaces so producer and consumer
never share a partially written framebuffer. Those surfaces are not a playback queue.
The GUI always replaces an unpainted presentation request with the newest complete
generation, retaining at most one pending frame. It may skip obsolete visual frames
when the GUI or compositor is delayed, while audio and core timing remain authoritative.

This design prevents a slow display from creating an ever-growing queue or A/V drift.
The double/triple-buffer choice applies only to the host swap chain; it does not add a
second frontend frame queue.

## Diagnostics

Choose **Tools → Log/Diagnostics** to inspect:

- requested sync mode and effective OpenGL swap interval;
- requested and effective swap buffering, including host substitution;
- exchange frames published, copied, skipped, and producer drops;
- frames rendered, swapped, coalesced, and duplicate repaint submissions;
- current/maximum pending frames and measured swap cadence.

The structured log warns about producer drops and periodically records new coalescing
activity. Ordinary frame-by-frame events are never logged. Software rendering reports
the requested policy but correctly marks the accelerated swap behavior unavailable.

For lowest latency, start with double buffering and synchronization on. Try sync off
only if tearing is acceptable; use adaptive only when the diagnostics show the driver
accepted its `-1` interval. Triple buffering is useful when compositor cadence is less
stable, but it is not a substitute for run-ahead.
