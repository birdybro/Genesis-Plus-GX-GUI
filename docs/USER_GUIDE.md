# User Guide

This guide tracks the working standalone desktop frontend as it is built. The complete
end-user guide will expand through the remaining milestones; it does not describe
planned controls as if they already work.

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

## Display and input

Use **Video → Fullscreen** (`Alt+Return`) to enter or leave fullscreen. The Video menu
also provides fit/integer scale, native/4:3/stretch aspect, and nearest/bilinear filter
choices. Keyboard and SDL game-controller defaults and remapping are documented in
[INPUT_CONFIGURATION.md](INPUT_CONFIGURATION.md); shortcuts are listed in
[KEYBOARD_SHORTCUTS.md](KEYBOARD_SHORTCUTS.md).

Features whose menus are visible but remain disabled without a loaded game will be
enabled as their corresponding milestones connect persistence, state, system, and disc
services.
