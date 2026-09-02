# User Guide

This is the complete operating reference for the standalone desktop frontend. It
describes shipped behavior; build, test, packaging, and contributor workflows are linked
from the final section.

## Starting the application

Launch `genesis-plus-gx-gui` from the desktop or a terminal. The current command-line
forms are:

```text
genesis-plus-gx-gui
genesis-plus-gx-gui game.md
genesis-plus-gx-gui --fullscreen game.md
genesis-plus-gx-gui --portable game.md
genesis-plus-gx-gui --patch translation.bps game.md
genesis-plus-gx-gui --help
genesis-plus-gx-gui --version
```

Only one startup game is accepted. Unknown options and multiple positional files print
a diagnostic and exit with status 2. `--patch` requires both an IPS/BPS/UPS file and a
startup game. Use `--` before a filename beginning with `-`.

`--portable` keeps the complete application-data hierarchy beside this application
copy for that launch. It never changes or imports the normal user profile. The active
mode appears in the title, Paths page, log, and diagnostics; an unwritable portable
location stops startup rather than falling back. See [PORTABLE_MODE.md](PORTABLE_MODE.md)
for exact Windows/Linux/macOS placement and relocation guidance.

## Opening and closing games

Choose **File → Open Game…** (`Ctrl+O` on most platforms), or drop one supported local
file onto the main window. The emulator loads on its dedicated worker thread and begins
running after initialization; the window remains responsive throughout. Choose
**File → Close Game** to stop and unload it. Opening another file replaces the current
game without restarting the application.

Choose **File → Open Game with Patch…** (`Ctrl+Shift+O`) to select a cartridge and an
IPS, BPS, or UPS patch together. You may instead drop exactly two local files—the game
and patch in either order—or pass `--patch FILE` on the command line. Ordinary Open,
Recent, library, command-line, and one-file drop launches search beside a direct
cartridge for one same-stem sidecar such as `game.ips` or `game.md.ips`, including
uppercase extensions. Multiple matching sidecars are ambiguous and produce an error
that directs you to explicit selection.

The source file is never modified. The parser caps patches at 64 MiB and cartridge
input/output at the core's 32 MiB limit, validates all record/copy bounds, and verifies
BPS/UPS source, output, and patch CRC-32 values. Exact output is published atomically to
the per-user `cache/patches` directory. The core, save RAM, save states, cheats, and
per-game overrides use the patched content's SHA-256 identity; recents and library
history retain the user-selected source path. The active patch name appears beside the
game in the status bar. Explicitly selected patches survive automatic session resume.
Delete the patch cache only while the application is closed; it is recreated as needed.

Soft patches apply only to cartridge content. A selected ZIP cartridge member can be
patched explicitly, but automatic ZIP sidecars are avoided because the intended member
would be ambiguous. CUE/ISO/CHD disc images and M3U playlists are rejected. A standalone
`.bin` remains eligible because that extension is also used for Genesis cartridges;
oversized disc-track data fails the cartridge size gate.

Every successfully loaded game moves to the top of **File → Open Recent**. The history
stores at most 12 absolute paths in the platform application-data directory. A missing
file remains listed with a **(Missing)** marker but cannot be launched. Choose **Clear
Recent Games** at the bottom of the submenu to erase the list; doing so does not close
the game currently running. If the history file cannot be committed, the existing menu
is retained and a Recent Games Error explains the filesystem failure.

The desktop host accepts these files:

```text
.68k .bin .bms .cue .gen .gg .iso .md .mdx .m3u .m3u8 .sg .sgd .smd .sms .zip
```

CHD appears when the build includes libchdr (the default presets do). ZIP browsing is
limited to stored or deflated cartridge members supported by the core; encrypted
members, unsafe names, excessive compression ratios, members over 32 MiB, and archived
disc workflows are rejected. A one-game ZIP opens automatically. For multiple games,
choose the member in the accessible archive browser. The validated cartridge is cached
beneath the application cache directory; the original ZIP remains the recent/session
source and the cache may be deleted while the application is closed.

M3U and UTF-8 M3U8 playlists describe up to 32 local Sega CD images, one relative path
per non-comment line. Absolute paths, URLs, parent traversal, duplicate discs, missing
files, and paths that escape through a symlink are rejected. The first disc loads at
startup. Use **Emulation → Previous Playlist Disc** (`Ctrl+Shift+PageUp`) or **Next
Playlist Disc** (`Ctrl+Shift+PageDown`) to move through the declared order; ordinary
Change Disc and Eject remain available. Keep a playlist and all referenced CUE/ISO/CHD
files within the playlist directory tree. A `.bin` file may be a cartridge or part of a
disc workflow; the Genesis Plus GX core makes the final content decision.

Missing files, directories, unsupported extensions, unreadable files, overlong paths,
and core-rejected images produce a concise error dialog instead of crashing. Selecting
an invalid file while a game is running leaves that game alone. Proprietary ROMs and
Sega CD BIOS files are never included with the application.

Choose **File → Exit** or the platform window-close control to quit. If a game is active,
the application first stops accepting new work, flushes save memory, stops and joins the
emulation thread, stops audio, finishes storage/scanner workers, and then closes the GUI.

## Emulation controls

**Emulation → Pause** (`Space`) stops frame production and host audio without blocking
the window; trigger it again to resume. **Reset** (`Ctrl+R`) performs the console's hard
reset path, while **Soft Reset** uses the supported reset-button behavior. Neither action
unloads the image or disc, and both are disabled until a game is ready.

Hold `Tab` for momentary fast-forward, or use **Emulation → Fast Forward Toggle** (`` ` ``)
to latch it. The two controls compose safely, and releasing the hold—or moving focus away
from the application—cannot cancel an independently enabled toggle. While active, the
worker uses its bounded high-rate pacing path and suppresses host audio so samples cannot
accumulate. Hold `/` for momentary slow motion or use **Emulation → Slow Motion Toggle**
(`Ctrl+/`) to latch it with the same focus-safe behavior. Slow motion, fast forward, and
rewind are mutually exclusive.

**Emulation → Emulation Speed** selects a 50%, 75%, 100%, 125%, 150%, or 200% normal
speed preset. **Speed Settings…**, also available from **Settings → Advanced**, accepts
an exact 50–200% normal rate, 25–75% slow-motion rate, and 200–1600% fast-forward rate.
These values persist in versioned settings and affect only frontend pacing: the core
still executes complete frames with its authoritative PAL, NTSC, or Sega CD clocks and
algorithms. Host audio plays only at an effective 100%; other rates deliberately pause
and clear playback rather than adding pitch distortion, unbounded buffering, or A/V
drift. The status bar distinguishes `Speed`, `Slow`, and `Fast` and reports the current
percentage.

**Frame Advance**
(`N`) is available while paused and executes exactly one frame before returning to the
paused state. The status bar reports measured FPS without using a GUI timer to drive
emulation. It observes the worker's completed-frame counter twice per second, displays
one decimal place, falls to `0.0 FPS` while paused, and reflects the faster cadence during
fast-forward. The adjacent System and Region fields describe the loaded image; missing
header data is shown explicitly as Unknown instead of leaving a stale prior-game value.

Rewind begins recording in-memory states as soon as a game loads. Hold Backspace for
momentary rewind or select **Emulation → Rewind Toggle** for latched backward playback.
The action becomes available after the first earlier state exists. Audio is cleared and
muted while moving backward, then normal PAL/NTSC/Sega CD pacing resumes on release.
**Emulation → Rewind Settings…** (also available from **Settings → Advanced**) controls
whether history is recorded, the 1–60-frame capture interval, and a strict 16–1024 MiB
payload cap. Defaults are enabled, every six frames, and 128 MiB (roughly 12 seconds for
one-MiB states at 60 Hz). Loading/unloading, reset, state restore, disc change, and
state-affecting core setting changes clear incompatible history.

## Sega CD and Mega CD discs

Configure the USA, Europe, and/or Japan BIOS you legally provide under **Tools → BIOS
Settings…** before opening a disc from **File → Open Game…**. The emulator detects the
disc's console region and selects that region's configured BIOS. If the required file
is absent or cannot be loaded, the error identifies the region and the emulator returns
to its no-game state safely.

The direct disc workflow accepts cooked ISO and CUE/BIN images. CHD is included when
the build enables bundled libchdr (the standard presets do). After a Sega CD game has
loaded, **Emulation → Change Disc…** selects a replacement disc and **Eject Disc** opens
the virtual tray; the same checkable action becomes **Close Disc Tray** while open.
Those actions remain disabled for cartridge games. A failed replacement leaves the
tray open with no partially mounted image, allowing another selection or closing the
application normally. CDDA tracks described by a supported image are played by the
existing Genesis Plus GX CD subsystem through the normal bounded audio pipeline.

## Save memory

Cartridge SRAM is loaded automatically before the first emulated frame and saved
automatically when a game is closed, replaced, or the application exits. Sega CD
internal backup RAM and RAM-cartridge memory use the same lifecycle when those regions
are active. Each game is identified by a SHA-256 hash of its content, so games with the
same filename cannot overwrite one another.

The platform application-data directory contains separate files under
`saves/<title>-<sha256>/`:

```text
cartridge.srm
scd-internal.brm
scd-cartridge.brm
```

Writes use an atomic replacement transaction. If a save cannot be committed while
closing or replacing a game, the application reports the error and leaves that game
loaded so the user can correct the storage problem and retry. A truncated or
wrong-sized save is rejected before emulation begins rather than partially copied into
the core. The application never reads or writes `game.srm`, `scd.brm`, or other save
files relative to its current working directory.

## Game information

With a game loaded, choose **Tools → Game Information…** to inspect its titles, system,
region, format, product code, declared and actual sizes, header/computed checksums,
mapper or disc details, path, and SHA-256 identifier. CUE sheets also show their track
count and local data-track path where safely available. The read happens on a bounded
background worker and does not pause or reinitialize emulation.

Metadata is informational: Genesis Plus GX remains authoritative for actual hardware,
region, mapper, and disc detection when loading. A missing or malformed header is shown
as a note rather than guessed into core settings. See
[GAME_INFORMATION.md](GAME_INFORMATION.md) for format and safety details.

## Game library

Choose **File → Game Library…** (`Ctrl+L`) to build an entirely local collection. Use
**Add…** to select a root; it is indexed on a worker thread, so hashing and recursive
directory walking do not freeze the main window or emulation. Select a root to toggle
subdirectory scanning, rescan it, or remove it from the index. Removing a root never
deletes the files it contains.

Search title/path text, filter by system or detected region, show favorites only, and
sort any table column. The table includes last-played and play-count state. Launch by
double-clicking a row or pressing **Launch**; the count changes only after a successful
core load. Favorites and local artwork paths survive rescans. **Game Information…**
shows the indexed header and hash, while **Choose Artwork…** displays a bounded local
preview. The application never contacts an artwork or metadata service. See
[GAME_LIBRARY.md](GAME_LIBRARY.md) for storage, recovery, and scan safety details.

## Save states

Use **Emulation → Save State** (`F5`) and **Load State** (`F8`) with the selected slot.
The **State Slot** submenu offers slots 0–9, `Ctrl+0` through `Ctrl+9` selection,
previous/next navigation, timestamps/names for existing states, and deletion. Choose
**Manage Save States…** in the same submenu for a keyboard-accessible ten-slot browser
with native-frame previews, optional names, timestamps, frame numbers, payload sizes,
and validation status. **Import…** and **Export…** use self-contained `.gpgxstate`
files; imports must match the loaded game's content identity and hardware before they
can replace the selected slot. The status bar always shows the selected slot and
indicates while an operation is running.

State files live under `states/<title>-<sha256>/slot-N.gpgxstate` in the platform
application-data directory. The frontend preserves the raw Genesis Plus GX state bytes
inside a metadata envelope recording game SHA-256, hardware, slot, timestamp, frame
number, payload length, core signature, payload checksum, bounded UTF-8 name, and a
checksummed PNG preview. Reads and writes run off the GUI thread. A state for another
game or hardware type, a truncated state, a corrupt payload/preview, and an unsupported
schema are rejected before the core loader is called. Existing schema-1 states remain
readable. Invalid entries appear as **Invalid** and can be deleted; they cannot be
loaded or exported.

Saving replaces a slot atomically. Loading first validates the complete envelope, then
restores through the emulation thread and reports success in the status bar. Opening or
closing a game is temporarily disabled during an active state operation so its result
cannot cross into another game session. See [SAVE_STATES.md](SAVE_STATES.md) for the
format and recovery details.

### Resume the last session on launch

Open **Tools → Settings → General → Session Settings** to opt into automatic
session resume. When the application exits cleanly with a game running, it pauses at a
frame boundary and atomically writes a dedicated checkpoint. The next launch reopens
that game, validates its full identity and hardware, restores the checkpoint, and then
starts playback. A path passed on the command line always wins. Choosing **File →
Close Game** clears the pending resume marker.

Automatic resume is disabled by default. It does not overwrite slots 0–9 and it is not
a substitute for SRAM or Sega CD backup RAM. If the game moved, the checkpoint is
missing/corrupt, or the core rejects it, the frontend clears the stale marker and falls
back to a normal launch instead of weakening validation or repeatedly failing at
startup.

## Screenshots

Press `F12` or choose **File → Screenshot** while a game has produced a visible frame.
The command captures the latest complete native emulator image—not the desktop window,
menus, scaling borders, or native window chrome—and writes a PNG without pausing
emulation. The status bar confirms the final path; an encoding or directory error is
shown without closing the running game. The action stays disabled until the first frame
of a game and while a previous capture is being written.

Choose **File → Screenshot Settings…** to select an absolute output directory. Apply,
OK, Cancel, and Restore Defaults use the same staged behavior as the other settings
dialogs. The default is the platform application-data `screenshots` directory, and the
choice is atomically stored in `config/screenshot-settings.json`.

Names combine a sanitized game filename, millisecond timestamp, and emulated frame
number. If that exact name already exists, the application adds a numeric suffix and
never overwrites it. PNG data is first completed in a temporary file in the destination
directory, so an interrupted encode cannot leave a partially named capture.

## Run-ahead latency reduction

For cartridge games, choose **Emulation → Run-Ahead** to display a future frame while
the saved machine advances by only one frame. **Run-Ahead Settings…** selects one to
four speculative frames. The feature is disabled by default and additional frames cost
additional CPU time.

Fast-forward, slow motion, and rewind suspend speculation until normal forward play
returns. Pause performs no work; Frame Advance applies one complete configured
run-ahead step. Sega CD remains authoritative-only because its disc and CDDA path has
not passed the same exact rollback gate. If the first speculative continuation differs
from the authoritative continuation, the application disables run-ahead for that game
session and continues normal emulation. Specialized mouse/light-gun/analog/tablet
devices similarly use authoritative-only input; standard pads and pad-only multitaps
are supported. See [RUN_AHEAD.md](RUN_AHEAD.md) for the exact
state/audio/video flow and diagnostic counters.

## Lossless A/V recording and frame dumps

Choose **File → Start Lossless A/V Recording…** (`Ctrl+Shift+F12`) while a game is
running, select a destination directory, then use the same action to stop. Recording
creates one `.gpgx-recording` directory containing sequential native PNG frames, a
stereo 16-bit PCM `audio.wav`, a per-frame JSON Lines index, and a final JSON manifest.
It captures the unscaled core image, so presentation filters, shaders, borders, and
window chrome are intentionally excluded. The status bar reports start/finalization,
the final path, written frames, and any explicitly counted drops.

An eight-slot preallocated queue separates the emulation thread from PNG and filesystem
work. When storage cannot keep up, the complete video/audio frame is dropped and
counted instead of blocking emulation or growing memory. Sessions are capped at 108,000
accepted frames and 8 GiB. Active output has a `.partial` suffix and is renamed only
after the audio header, index, and manifest flush successfully. Closing or replacing a
game automatically requests finalization, and application shutdown drains accepted
frames before releasing the service.

The default chooser location is the platform application-data `recordings` directory,
but the selected destination applies only to that session. Host mute and master volume
do not change recorded core audio; core sound settings do. Fast/slow host pacing keeps
the file's normal emulated-time cadence, pause adds nothing, and rewind records aligned
silence. See [RECORDING.md](RECORDING.md) for the format, hard limits, recovery behavior,
and post-processing guidance.

## Cheats

With a game loaded, choose **Tools → Cheats…** to manage its local cheat list. Add a
name and code, choose whether it is enabled, then use Apply or OK. Genesis / Mega Drive
and Sega CD games accept Genesis Game Genie and Action Replay / Pro Action Replay
codes. SG-1000, Mark III, Master System, and Game Gear games accept their Game Genie
and Action Replay forms plus Fusion RAM/ROM codes. Join multi-address codes with `+`.

Every row is validated before the list is saved or applied. Unsupported alphabets,
wrong separators, partial/trailing data, invalid disabled rows, and lists above the
150-patch core limit remain visible as an inline error and never reach emulated memory.
Enabled changes take effect through the emulation worker without pausing the GUI.

Use **Import…** on the Codes tab for a bounded local RetroArch `.cht` file containing
emulator-handled code fields, or a `.txt` file containing `Name | Code` or code-only
lines. An import is all-or-nothing, skips codes already in the table, and always adds
entries disabled. Review and explicitly enable an entry before applying it.

The **Search RAM** tab can narrow live work-RAM values by exact, changed/unchanged,
increased, or decreased comparisons. Begin with **New search**, change the in-game
value, and use **Filter snapshot** as often as needed. Signed or unsigned byte/word
interpretation is selectable. Adding a selected result creates a disabled cheat row;
the generated Genesis word or 8-bit byte code still requires explicit enable and Apply.
Pause first when a stable snapshot is important. Search requests run on the emulation
thread and do not grant the GUI direct access to core memory.

Lists are stored atomically in `config/cheats/` using the game's complete SHA-256
identity. Loading another game clears the live patch list before its own stored list is
resolved. Corrupt, future-schema, wrong-game, and wrong-system files fail closed to an
empty list. See [CHEATS.md](CHEATS.md) for exact formats and lifecycle details.

## Per-game settings

After a game has loaded and its SHA-256 identity is ready, choose **Tools → Per-Game
Settings…**. Check only the categories that should differ for that title, then edit the
video, audio, system/region, input-profile, or BIOS override. Unchecked categories use
the current global value; they are not copied into the game file and continue to follow
later global changes. **Use Global Settings** clears every category, and Apply removes
the game's override file entirely.

Video presentation and core output, master volume/mute, core audio mixing, and an
available input profile can change while the game is running. Machine identity, region,
VDP/master-clock, and BIOS changes take effect when that game is reopened because they
participate in core initialization. Host audio device and buffer latency remain global
process settings; the per-game audio editor disables those two resource controls.

Before every load, the metadata worker hashes the selected content. The coordinator
loads `config/per-game-settings/<title>-<sha256>.json`, resolves each category over the
global snapshots, and orders the resulting system, firmware, video, and audio commands
ahead of the core load. A deleted input profile falls back to the active global profile
with a warning. If replacing a game fails before the old game unloads, its previous
effective settings and editor session are restored. Corrupt, oversized, future-schema,
or wrong-identity files are reported and ignored without modifying them. See
[PER_GAME_SETTINGS.md](PER_GAME_SETTINGS.md) for the precedence and storage contract.

## Display and video

Use **Video → Fullscreen** (`Alt+Return`) to enter or leave fullscreen. The Video menu
also provides fit/integer scale, native/4:3/stretch aspect, and nearest/bilinear texture
filter choices. **Overscan** can show no border, top/bottom, left/right, or every border.
The bundled NTSC filter offers monochrome, composite, S-Video, and RGB presets in
addition to disabled. Interlaced games may use the core's single-field or double-field
output, and Game Gear software can opt into the extended 256×192 viewport.

**Video → Synchronization** selects off, on, or adaptive vertical synchronization and
double- or triple-buffered host presentation. On plus double buffering is the default.
Off can reduce latency at the cost of tearing; adaptive requires graphics-driver
support; triple buffering can smooth host cadence at the cost of another display
interval. Applying a change rebuilds the OpenGL presentation surface without restarting
the game. The GUI still keeps only the newest complete frame, so no option creates an
unbounded queue or becomes a second clock. The diagnostics report both requested and
effective behavior because a compositor or driver may substitute unsupported requests.
See [DISPLAY_SYNCHRONIZATION.md](DISPLAY_SYNCHRONIZATION.md) for the exact contract and
troubleshooting guidance.

Choose **Video → Video Settings…** to edit the same controls in one dialog. **Apply**
updates a running game without closing the dialog, **OK**
applies and closes, **Cancel** discards changes not already applied, and **Restore
Defaults** stages native/fit/nearest, synchronized double-buffered presentation with all
optional core processing disabled. Every quick menu item and dialog control reflects
the same active snapshot.
Settings are saved atomically as `config/video-settings.json` beneath the platform
application-data directory and restored on the next launch. A malformed or unsupported
settings file is ignored with a diagnostic and safe defaults; it is not silently
destroyed. If a runtime command or settings write fails, the previous display policy and
menu checks remain active, an error dialog explains the rejection, and **OK** leaves the
editor open for correction or retry.

The presentation filter controls GPU/Qt scaling. The NTSC filter is separate: it runs
inside Genesis Plus GX and changes the native framebuffer geometry. Overscan and Game
Gear viewport changes likewise come from the core rather than cropping the final
window image. Region and VDP system selection are handled by the separate System
Settings dialog instead of being conflated with display scaling.

### CRT and Libretro shaders

Choose **Video → Shaders → Built-in CRT** to enable the bundled adjustable scanline,
aperture-grille, curvature, vignette, and brightness effect. Choose **Shader
Parameters…** to edit the preset's declared controls. **Off** returns to normal
presentation.

**Load Libretro Preset…** accepts a user-provided modern Slang `.slangp` preset. The
frontend uses librashader for multi-pass chains, lookup textures, history/feedback, and
declared runtime parameters. Keep a downloaded shader pack's relative files together;
the application stores the selected absolute preset path but does not copy or download
its referenced files. Legacy `.glslp` and Cg presets are not supported.

Shaders require OpenGL 3.3 or newer. A missing file, invalid preset, compilation error,
or unavailable OpenGL renderer produces a descriptive error and leaves the normal
unshaded image active. The selection is available in global Video settings and as a
per-game Video override. See [LIBRETRO_SHADERS.md](LIBRETRO_SHADERS.md) for the exact
compatibility and packaging contract.

The accelerated renderer works on native X11 and Wayland sessions. If a graphics
driver cannot provide the requested context, the frontend automatically retains the
normal software-rendered picture; shaders are unavailable only for that session.

### Local bezel and overlay artwork

Use **Video → Artwork** to choose a local image and display it as a bezel behind the
game or as a foreground overlay. Bezel mode accepts PNG, JPEG, and BMP. Overlay mode
requires a PNG alpha channel so an opaque file cannot hide gameplay. The Artwork page
in Video Settings also provides 1–100% opacity and optional left/top/right/bottom
percentage insets for an explicitly designed game aperture.

Artwork is frontend-only and never changes core pixels, timing, input snapshots, save
states, or screenshots of the native emulated image. With aperture constraints off,
game geometry is identical to artwork-off presentation. The image is bounded, decoded
once when applied, and cached for both OpenGL and software drawing. If it later goes
missing or becomes invalid, the application reports the problem and retains normal
video or the previous working configuration.

Selections may be global or part of a per-game Video override. The application stores
only the absolute local path and never downloads, scrapes, packages, or copies artwork.
See [ARTWORK_OVERLAYS.md](ARTWORK_OVERLAYS.md) for supported formats, limits, aperture
semantics, privacy, and troubleshooting.

## Input configuration

Choose **Input → Controller Configuration…** to edit named keyboard/controller profiles,
capture Genesis three-/six-button bindings, reset mappings, set the SDL analog deadzone,
select advanced emulated devices, and configure emulator hotkeys. Activate a binding and press a key or standardized
controller button; Escape cancels capture. Duplicate bindings and unmodified keys reserved
by emulator actions are rejected before Apply or OK can publish the profile.

The **Hotkeys** tab captures unique keyboard combinations for Open/Close, the library,
pause, hard/soft reset, fullscreen, fast forward, slow motion, frame advance, save-state slots,
screenshots, mute, and volume. Apply updates the live menu shortcuts immediately;
Restore Defaults on this tab resets only emulator shortcuts. See
[KEYBOARD_SHORTCUTS.md](KEYBOARD_SHORTCUTS.md) for the complete default table.

Choose **Input → Player Assignments…** to open the assignments page directly. Controllers
are discovered at startup and hot-plugged while the application runs. Assigning a device
to an occupied player swaps the assignments, and removing a controller releases its
active state. The keyboard remains available for Player 1. Profiles are versioned and
written atomically, and Restore Defaults never changes a running session until changes
are accepted. Specialized core devices—including Sega Mouse, light gun, paddle, Sports
Pad, XE-1AP, Pico, Terebi Oekaki, Graphic Board, and Activator—are available through the
advanced page subject to the loaded system and port restrictions. See
[INPUT_CONFIGURATION.md](INPUT_CONFIGURATION.md) for mapping semantics and defaults.
Profile edits are published only after the runtime accepts the complete profile and its
atomic file commit succeeds. A failure restores the prior keyboard/controller/core
mapping, keeps the editor open, and reports the cause. A controller assignment rejected
after hot-unplug is likewise shown instead of being accepted silently.

## Appearance and accessibility

Choose **Tools → Settings…** (the platform Preferences shortcut) to open the unified
settings center. Its stable categories are **General**, **Video**, **Audio**, **Input**,
**System**, **BIOS**, **Paths**, and **Advanced**. Each page summarizes the live values
and opens the corresponding complete editor; the Paths page also shows the active
standard/portable mode and resolved directories for configuration, saves, states, screenshots, recordings,
library data, and logs. Existing category menu entries remain direct shortcuts to those
same editors.

From General, open **Appearance Settings…** to select **System default**, **Light**, or
**Dark**. System default restores the Qt platform style and
colors captured at application startup. Light and dark use standard Qt Widgets with
high-contrast palettes; they do not replace native controls with a custom skin. Changes
apply to every open application window. The selection is atomically stored in
`config/appearance-settings.json`; unreadable, malformed, or newer unsupported files
fall back to the system theme without being overwritten.

Appearance Settings also selects **System language**, **English**, or the deliberately
expanded **Pseudo-localization (layout testing)** catalog. Language changes take effect
after restart so startup-created windows never mix languages. Unsupported, missing, or
invalid catalogs fall back to complete English source text; the operating-system locale
is retained under System language. The current choice, effective language, and fallback
state appear in Diagnostics. See [LOCALIZATION.md](LOCALIZATION.md).

The settings center is a navigation and status surface, so transactional **Apply**,
**OK**, **Cancel**, and **Restore Defaults** stay in each typed editor. This prevents a
failure in one persistence domain from partially applying unrelated categories.
Per-game overrides are enabled on Advanced only while a game is loaded; diagnostics
remain available at all times.

Qt 6 sizes the interface in device-independent units. Fractional desktop scaling and
Retina displays use pass-through scale-factor rounding, while emulated-image integer
scaling remains an independent Video setting. All menus have keyboard mnemonics,
Preferences provides an explicit focus order and label buddy, and important controls
expose stable accessible names through Qt's normal platform accessibility bridge. See
[APPEARANCE_AND_ACCESSIBILITY.md](APPEARANCE_AND_ACCESSIBILITY.md) for keyboard and
assistive-technology details and [LOCALIZATION.md](LOCALIZATION.md) for translation
behavior and contribution requirements.

## Audio settings

Use **Audio → Mute** (`M`) or the volume up/down actions for immediate output control.
These controls affect host playback only: mute continues consuming samples so audio
cannot accumulate, and master volume scales the final stereo stream without changing
the emulated sound chips.

Choose **Audio → Audio Settings…** to select the playback device, buffer latency,
stereo/mono output, PSG and FM levels, Sega CD CDDA/PCM levels, filtering, low-pass
strength, three-band equalizer gains, YM2612/YM3438 and YM2413/OPLL implementations,
Master System FM detection, and high-quality FM/PSG resampling. Relevant low-pass or EQ
controls are enabled only for the selected filter mode. Apply, OK, Cancel, and Restore
Defaults follow the same staging behavior as Video Settings.

Mute, volume, core mixing/chip settings, playback device, and latency all affect a
running game immediately. A device or latency change briefly pauses the SDL stream,
clears stale samples, and reconfigures the logical capacity of the same worker-owned
ring; it does not stop the emulation thread or require an application restart. Failure
to open a newly selected device leaves the previous stream, capacity, and pause state
active and produces an error dialog. If a configured device is no longer available at
startup, the application includes the condition in its Startup Issues dialog and safely
opens the system default. SDL
audio hot-plug events are drained in bounded batches and refresh an already-open Audio
Settings device list; if an explicitly
selected device disconnects while running, playback automatically reopens on the
current default device without replacing the saved preference. If that recovery also
fails, an Audio Output Unavailable dialog explains that playback is disabled while
emulation remains usable. Settings are atomically stored as
`config/audio-settings.json`; malformed or unsupported files fail closed to defaults
and appear in the consolidated startup report.

## System settings

Choose **Tools → System Settings…** to override console hardware, region, VDP video
standard, or master clock. Hardware choices include automatic detection, SG-1000 and
both SG-1000 II variants, Mark III, Master System and Master System II, Game Gear, and
Mega Drive/Genesis. Region, VDP, and master clock are independent advanced controls;
leaving each on **Automatic** preserves the core's normal content detection.

The Accuracy section retains real-hardware lockups for illegal accesses and 68000
address-error exceptions by default. Disable these only for software known to depend
on non-hardware behavior. All system choices are saved atomically in
`config/system-settings.json`.

System choices define machine initialization, so applying them never rewrites a game
that is already running. The dialog and status bar state that the complete snapshot
takes effect on the next game load. Closing and reopening the game is sufficient; the
application itself does not need to restart. This boundary also ensures save data is
flushed before a different emulated hardware model is created. A worker or persistence
failure keeps the prior system snapshot, leaves the dialog open, and presents a System
Settings Error.

## BIOS settings

Choose **Tools → BIOS Settings…** to configure firmware for Genesis/Mega Drive,
Game Gear, regional Master System models, and USA/Europe/Japan Sega CD or Mega CD.
Each entry shows the configured path, expected region, validation status, detected
firmware family and size, and a SHA-256 checksum. **Clear** stages removal of one path;
**Restore Defaults** stages clearing every path. Apply, OK, and Cancel follow the same
staging behavior as the other settings dialogs.

The application checks that a selection exists, is a normal readable file, has the
shape accepted by that core loader, fits the core host path boundary, and is not an
obviously empty repeated-byte image. A **Valid** status means those structural checks
passed. The checksum is informational and does not certify that a dump is authentic or
correct for a particular game revision.

Configuration is written atomically to `config/bios.json` beneath the platform
application-data directory. Firmware files themselves remain wherever you put them;
the application does not copy, modify, download, or bundle Sega firmware. Sega CD/Mega
CD software requires a suitable user-supplied regional BIOS. See [BIOS.md](BIOS.md) for
sizes, privacy, optional external-fixture testing, and troubleshooting.

Keyboard and SDL game-controller defaults and remapping are documented in
[INPUT_CONFIGURATION.md](INPUT_CONFIGURATION.md); shortcuts are listed in
[KEYBOARD_SHORTCUTS.md](KEYBOARD_SHORTCUTS.md).

## Debug tools

The emulator debugger is deliberately absent from the normal menus. To opt in, open
**Tools → Settings… → General**, enable **Show developer/debug tools**, and apply the
change. A **Tools → Developer Tools → Emulator Debug Workspace…** command then opens a
separate native window. Disabling the setting closes that window and hides the submenu
again; the choice is versioned and persists between launches.

The workspace samples CPU, memory, VDP, sound, and logical input state between emulated
frames. It can pause/resume, advance one whole frame, reset, inspect bounded memory,
search and watch typed RAM values, pause on 68000/Z80 frame-boundary program counters,
step either real CPU while paused, collect bounded 68000/Z80 instruction traces, load
local symbols, atomically export versioned trace JSON, and use the normal
game-identity-checked state slots. On the 8-bit consoles, the Z80
RAM view follows the active console work RAM. Memory or register edits require pause
and are rejected again by the emulation worker if UI state is stale. This is a developer
facility: editing live state can crash the emulated program, but it must not race the
core or bypass frontend file validation. See [DEBUG_TOOLS.md](DEBUG_TOOLS.md) for all
views and safety boundaries.

## Logs and diagnostics

Choose **Tools → Log and Diagnostics…** to inspect a live support report. It includes
the application version and Git commit, Qt and SDL versions, operating system and CPU
architecture, active renderer and audio device, bounded audio metrics, connected
controller count, loaded game/system/region, BIOS validity and short checksum prefixes,
lossless-recording queue/drop/output metrics, and structured-logger counters. Reopening
the action refreshes the existing dialog.
**Copy Diagnostics** copies exactly the visible privacy-filtered text.

Frontend messages are stored as compact JSON Lines in `logs/frontend.jsonl` beneath the
platform application-data directory. Each line has UTC timestamp, severity, category,
and message. The active file is limited to 1 MiB and keeps up to three rotated backups.
Filesystem paths and credential-like values are redacted before writing; diagnostics
never include ROM/BIOS paths or the log path. Logging covers startup/shutdown, build,
BIOS, renderer, audio, controller, game, state, screenshot, recording, persistence, and rate-limited
timing/audio anomalies without per-frame noise. See
[LOGGING_AND_DIAGNOSTICS.md](LOGGING_AND_DIAGNOSTICS.md) for the schema and support
workflow.

Recoverable background failures are also shown at the point of use. A stopped metadata
or save-state service clears the affected busy state and disables only that workflow;
library-history and audio-control failures identify their subsystem. Repeated failures
from the same emulation session are consolidated. If the emulation worker itself stops,
the application reports the failure and closes because continuing would expose a stale
loaded-game state. Failure to flush final save data or stop a service is recorded as an
incomplete shutdown and returns a nonzero process status for launchers and automation.

## In-application help

**Help → User Guide** opens a keyboard-accessible getting-started reference without a
browser or network connection. **Help → Keyboard Shortcuts** lists the active default
application shortcuts. **Help → About Genesis Plus GX GUI** shows the frontend version,
Git commit, upstream relationship, and non-commercial license notice; **About Qt** shows
the deployed Qt notice. Source builds are documented in [BUILDING.md](BUILDING.md),
automated gates in [TESTING.md](TESTING.md), and contributor workflow in
[DEVELOPMENT.md](DEVELOPMENT.md).
