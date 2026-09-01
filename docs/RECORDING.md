# Lossless A/V Recording and Frame Dumps

Genesis Plus GX GUI can record the core's native video and audio without changing
emulation or depending on a platform codec installation. With a game running, choose
**File → Start Lossless A/V Recording…** or press `Ctrl+Shift+F12`, then choose an
output directory. Use the same action to stop. The shortcut can be changed under
**Input → Controller Configuration… → Hotkeys**.

## Output format

Each capture is a self-contained directory named
`<game>-<UTC timestamp>.gpgx-recording`. It contains:

```text
manifest.json
frames.jsonl
audio.wav
frames/
    frame-000000000.png
    frame-000000001.png
    ...
```

The PNG files are the complete native RGB image delivered by the core. They exclude
window chrome, letterboxing, scaling, presentation filtering, overlays, and CRT or
Libretro shaders. Dynamic viewport sizes, PAL/NTSC output, Game Gear geometry, and
interlaced field flags are preserved per frame in `frames.jsonl`. `audio.wav` is
little-endian stereo 16-bit PCM at the active core sample rate. `manifest.json` records
the schema and format versions, content-derived game ID, nominal frame rate, sample
rate, captured/dropped counts, byte count, and completion reason.

This directory is also the continuous frame-dump format. Image tools can consume the
PNGs directly. A user-installed encoder such as FFmpeg can combine the sequence and
WAV afterward; the application neither bundles nor invokes an external encoder.

## Timing and sound behavior

Capture occurs after a complete core frame on the emulation-owner thread. It records
core audio before host mute and master-volume scaling, so muting speakers does not mute
the file. Core mixer, filter, and sound-chip options do affect the file. Fast-forward
and slow-motion change how quickly capture is produced but not the emulated-time frame
or sample cadence in the output. Pause produces no frames. Rewind frames carry silence
with the matching core sample count, keeping the streams aligned with intentionally
muted backward playback.

Replacing or closing a game first disables capture and requests finalization, then
submits the lifecycle operation. Already accepted slots own their A/V copies, so the
asynchronous writer may finish after the core unloads without crossing game identities.
New file operations are disabled while a recording starts or finalizes. Application
shutdown stops emulation first, drains accepted frames, finalizes the WAV/metadata, and
joins the writer before audio and the remaining services are released.

## Bounded operation and recovery

The emulator thread copies into eight preallocated capture slots and never waits for
PNG encoding or filesystem I/O. A full queue drops the whole A/V frame and increments
an explicit counter; it never grows or blocks emulation. The hard limits are 108,000
accepted frames (about 30 minutes at 60 Hz), 8 GiB per session, 4 MiB per encoded PNG,
and 4,096 stereo samples per submitted frame. Reaching a limit finalizes or reports the
recording rather than overrunning storage or memory.

While active, output uses the `.gpgx-recording.partial` suffix. Individual PNGs and the
manifest use atomic file commits. A successful finish closes and flushes the WAV and
index, writes a complete manifest, then atomically renames the directory to
`.gpgx-recording`. A write or finalization failure leaves the explicitly marked partial
directory for inspection and reports the path in the log; it is never presented as a
complete capture.

The default chooser location is the platform application-data `recordings` directory.
The selected destination applies to that recording only. Diagnostics report active
state, bounded queue occupancy, written/dropped frames, and output bytes without
including game or filesystem paths.
