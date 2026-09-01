# Portable Mode

Portable mode keeps frontend configuration, saves, states, screenshots, recordings,
library data, logs, and caches with a particular application copy. It is deliberately
opt-in and does not change or migrate the normal per-user application-data directory.

Start it explicitly:

```text
genesis-plus-gx-gui --portable
genesis-plus-gx-gui --portable --fullscreen game.md
```

On Windows and Linux, the application creates `portable-data` beside the executable.
In the distributed archives the executable is under `bin/`, so the resulting directory
is `bin/portable-data/`. On macOS, data is placed beside
`genesis-plus-gx-gui.app`, never inside the bundle; invoke the bundle executable from a
terminal or configure a launcher to pass `--portable`:

```bash
/path/to/genesis-plus-gx-gui.app/Contents/MacOS/genesis-plus-gx-gui --portable
```

The active mode and resolved directories are shown under **Tools → Settings → Paths**,
in the window title, in structured startup logging, and in copied diagnostics. The
diagnostics report names the mode but continues to redact filesystem paths.

## Isolation and relocation

Portable mode is selected independently on every launch. There is no marker file,
registry value, environment variable, or normal-settings preference that can enable it
implicitly. Starting the same executable without `--portable` continues to use Qt's
standard platform application-data location. Existing standard and portable data are
not merged or copied automatically.

Move the application and its `portable-data` directory together to retain the portable
profile. Do this only while the emulator is closed so atomic settings, save-RAM,
save-state, SQLite, recording, and log transactions have finished. Relative paths and
filesystem-root launch locations are rejected; the process working directory is never
used to select the data root.

The destination must be writable. This commonly means extracting an archive before
launch, copying a macOS app out of a read-only DMG, and avoiding system-owned install
directories. If portable storage cannot initialize, startup fails with a specific
error and does not fall back to the normal user-data directory.

## Directory layout

```text
portable-data/
    config/
    saves/
    states/
    screenshots/
    recordings/
    library/
    logs/
    cache/
```

The contents and atomic-write/identity rules are identical to standard mode. ROMs and
BIOS files are never copied into portable storage automatically; configured firmware
and custom path settings may still refer to external absolute paths.

## Verification

Required tests cover CLI parsing, Linux/Windows-style executable layouts, macOS bundle
placement, relocation, current-directory independence, unsafe roots, complete startup
and shutdown, unwritable-root failure, Settings/diagnostics visibility, and every
installed native package. The package verifier rejects archives that accidentally
pre-create or ship a `portable-data` directory.
