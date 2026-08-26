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
genesis-plus-gx-gui --help
genesis-plus-gx-gui --version
```

Only one startup game is accepted. Unknown options and multiple positional files print
a diagnostic and exit with status 2. Use `--` before a filename beginning with `-`.

## Opening and closing games

Choose **File → Open Game…** (`Ctrl+O` on most platforms), or drop one supported local
file onto the main window. The emulator loads on its dedicated worker thread and begins
running after initialization; the window remains responsive throughout. Choose
**File → Close Game** to stop and unload it. Opening another file replaces the current
game without restarting the application.

Every successfully loaded game moves to the top of **File → Open Recent**. The history
stores at most 12 absolute paths in the platform application-data directory. A missing
file remains listed with a **(Missing)** marker but cannot be launched. Choose **Clear
Recent Games** at the bottom of the submenu to erase the list; doing so does not close
the game currently running.

The desktop host currently opens these files directly:

```text
.68k .bin .bms .cue .gen .gg .iso .md .mdx .sg .sgd .smd .sms
```

CHD appears when the build includes libchdr (the default presets do). ZIP archives and
M3U playlists are not yet accepted by the standalone host and therefore are not shown
in its file picker. A `.bin` file may be a cartridge or part of a disc workflow; the
Genesis Plus GX core makes the final content decision.

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

**Fast Forward** (`Tab`) is a checkable toggle. While enabled, the worker uses its bounded
high-rate pacing path and suppresses host audio so samples cannot accumulate. Turn it off
to return to the loaded system's exact PAL, NTSC, or Sega CD target. **Frame Advance**
(`N`) is available while paused and executes exactly one frame before returning to the
paused state. The status bar reports measured FPS without using a GUI timer to drive
emulation.

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
previous/next navigation, timestamps for existing states, and deletion. The status bar
always shows the selected slot and indicates while an operation is running.

State files live under `states/<title>-<sha256>/slot-N.gpgxstate` in the platform
application-data directory. The frontend preserves the raw Genesis Plus GX state bytes
inside a metadata envelope recording game SHA-256, hardware, slot, timestamp, frame
number, payload length, core signature, and payload checksum. Reads and writes run off
the GUI thread. A state for another game or hardware type, a truncated state, a corrupt
checksum, and an unsupported schema are rejected before the core loader is called.
Invalid entries appear as **Invalid** and can be deleted; they cannot be loaded.

Saving replaces a slot atomically. Loading first validates the complete envelope, then
restores through the emulation thread and reports success in the status bar. Opening or
closing a game is temporarily disabled during an active state operation so its result
cannot cross into another game session. See [SAVE_STATES.md](SAVE_STATES.md) for the
format and recovery details.

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

Choose **Video → Video Settings…** to edit the same controls in one dialog. **Apply**
updates a running game without closing the dialog, **OK**
applies and closes, **Cancel** discards changes not already applied, and **Restore
Defaults** stages the native/fit/nearest presentation with all optional core processing
disabled. Every quick menu item and dialog control reflects the same active snapshot.
Settings are saved atomically as `config/video-settings.json` beneath the platform
application-data directory and restored on the next launch. A malformed or unsupported
settings file is ignored with a diagnostic and safe defaults; it is not silently
destroyed.

The presentation filter controls GPU/Qt scaling. The NTSC filter is separate: it runs
inside Genesis Plus GX and changes the native framebuffer geometry. Overscan and Game
Gear viewport changes likewise come from the core rather than cropping the final
window image. Region and VDP system selection are handled by the separate System
Settings dialog instead of being conflated with display scaling.

## Input configuration

Choose **Input → Controller Configuration…** to edit named keyboard/controller profiles,
capture Genesis three-/six-button bindings, reset mappings, set the SDL analog deadzone,
select advanced emulated devices, and configure emulator hotkeys. Activate a binding and press a key or standardized
controller button; Escape cancels capture. Duplicate bindings and unmodified keys reserved
by emulator actions are rejected before Apply or OK can publish the profile.

The **Hotkeys** tab captures unique keyboard combinations for Open/Close, the library,
pause, hard/soft reset, fullscreen, fast forward, frame advance, save-state slots,
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

## Appearance and accessibility

Choose **Tools → Settings…** (the platform Preferences shortcut) to select **System
default**, **Light**, or **Dark**. System default restores the Qt platform style and
colors captured at application startup. Light and dark use standard Qt Widgets with
high-contrast palettes; they do not replace native controls with a custom skin. Changes
apply to every open application window. The selection is atomically stored in
`config/appearance-settings.json`; unreadable, malformed, or newer unsupported files
fall back to the system theme without being overwritten.

Qt 6 sizes the interface in device-independent units. Fractional desktop scaling and
Retina displays use pass-through scale-factor rounding, while emulated-image integer
scaling remains an independent Video setting. All menus have keyboard mnemonics,
Preferences provides an explicit focus order and label buddy, and important controls
expose stable accessible names through Qt's normal platform accessibility bridge. See
[APPEARANCE_AND_ACCESSIBILITY.md](APPEARANCE_AND_ACCESSIBILITY.md) for keyboard and
assistive-technology details.

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

Mute, volume, and core mixing/chip settings affect a running game immediately at a
frame boundary. Playback device and latency define the SDL stream and bounded ring, so
the dialog marks them as taking effect after restarting. If a configured device is no
longer available, the application logs the condition and safely opens the system
default. SDL audio hot-plug events are drained in bounded batches; if an explicitly
selected device disconnects while running, playback automatically reopens on the
current default device without replacing the saved preference. Settings are atomically
stored as `config/audio-settings.json`; malformed or
unsupported files fail closed to defaults.

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
flushed before a different emulated hardware model is created.

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

## Logs and diagnostics

Choose **Tools → Log and Diagnostics…** to inspect a live support report. It includes
the application version and Git commit, Qt and SDL versions, operating system and CPU
architecture, active renderer and audio device, bounded audio metrics, connected
controller count, loaded game/system/region, BIOS validity and short checksum prefixes,
and structured-logger counters. Reopening the action refreshes the existing dialog.
**Copy Diagnostics** copies exactly the visible privacy-filtered text.

Frontend messages are stored as compact JSON Lines in `logs/frontend.jsonl` beneath the
platform application-data directory. Each line has UTC timestamp, severity, category,
and message. The active file is limited to 1 MiB and keeps up to three rotated backups.
Filesystem paths and credential-like values are redacted before writing; diagnostics
never include ROM/BIOS paths or the log path. Logging covers startup/shutdown, build,
BIOS, renderer, audio, controller, game, state, screenshot, persistence, and rate-limited
timing/audio anomalies without per-frame noise. See
[LOGGING_AND_DIAGNOSTICS.md](LOGGING_AND_DIAGNOSTICS.md) for the schema and support
workflow.

## In-application help

**Help → User Guide** opens a keyboard-accessible getting-started reference without a
browser or network connection. **Help → Keyboard Shortcuts** lists the active default
application shortcuts. **Help → About Genesis Plus GX GUI** shows the frontend version,
Git commit, upstream relationship, and non-commercial license notice; **About Qt** shows
the deployed Qt notice. Source builds are documented in [BUILDING.md](BUILDING.md),
automated gates in [TESTING.md](TESTING.md), and contributor workflow in
[DEVELOPMENT.md](DEVELOPMENT.md).
