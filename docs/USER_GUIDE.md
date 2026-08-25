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

## Display and input

Use **Video → Fullscreen** (`Alt+Return`) to enter or leave fullscreen. The Video menu
also provides fit/integer scale, native/4:3/stretch aspect, and nearest/bilinear filter
choices. Keyboard and SDL game-controller defaults and remapping are documented in
[INPUT_CONFIGURATION.md](INPUT_CONFIGURATION.md); shortcuts are listed in
[KEYBOARD_SHORTCUTS.md](KEYBOARD_SHORTCUTS.md).

Features whose menus are visible but remain disabled without a loaded game will be
enabled as their corresponding milestones connect persistence, state, system, and disc
services.
