# Per-Game Settings

Per-game settings are optional, local overrides for a content identity. They never
change or duplicate Genesis Plus GX emulation code, and they do not alter global
configuration files.

## Categories and precedence

For each category, resolution is simply:

```text
configured game override -> use it
no game override         -> use current global setting
```

The stored categories are video (including aspect ratio), audio gain/mute and core
mixing, system/hardware/region, input profile name, and BIOS paths. Host audio device
and buffer latency are always inherited globally because they define the process-wide
SDL stream and bounded ring shared with the emulation worker. Cheats already have a
separate per-game manager and are not duplicated here.

Input profiles are referenced by name rather than copied. If a referenced profile is
later removed, loading falls back to the active global profile, reports the condition,
and leaves the override file untouched so the user can repair it deliberately.

## Identity and file format

Files live in the platform application-data directory:

```text
config/per-game-settings/<sanitized-title>-<full-sha256>.json
```

Schema 1 embeds the same full hash and title slug in the document. A path collision or
renamed file therefore cannot silently apply another game's settings. Documents are
bounded to 256 KiB, every nested enum/range/path is validated, and writes use the
frontend's same-directory atomic replacement helper. Future schema versions are
rejected rather than guessed. An empty configuration removes the exact file and does
not create the directory when no override has ever existed.

## Load and failure lifecycle

Game loading begins with a bounded asynchronous metadata request. Once hashing returns,
the coordinator loads and resolves the sparse file and enqueues system, validated
firmware, video-core, and audio-core snapshots before the lifecycle load command.
Genesis Plus GX global state remains owned solely by the emulation thread.

Presentation, gain/mute, core video/audio, and available input mappings can be refreshed
while a game is active. System and BIOS selections are retained by the adapter for the
next initialization, so reopen the game after editing those categories. When a game is
closed, the global effective snapshot is restored. If a replacement cannot flush the
old game's save data, the old game's effective snapshot and editor session are restored
along with it.

Malformed metadata or override content never reaches the core. Metadata failure loads
with globals and disables the per-game editor for that session because no safe identity
exists. File corruption is reported and treated as an empty override without deleting
the evidence.
