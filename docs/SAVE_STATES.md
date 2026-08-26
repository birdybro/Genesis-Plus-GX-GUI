# Save States

Save states capture the complete emulated machine at one instant. They are convenient
for resuming play, but they do not replace cartridge SRAM or Sega CD backup RAM. Normal
save memory is portable game data and is flushed independently when a game closes;
state files depend on compatible Genesis Plus GX core serialization.

## Using slots

Choose a slot from **Emulation → State Slot**, then use **Save State** (`F5`) or
**Load State** (`F8`). Slots 0–9 may also be selected with `Ctrl+0`–`Ctrl+9`. The menu
shows **Empty**, a local timestamp, or **Invalid** for each slot. Previous/next actions
wrap at the ends. **Delete Selected State** removes either a valid or invalid file.

An operation disables other state and game-lifecycle actions until it finishes. A
successful save, load, or delete is reported in the status bar. A failure opens a
descriptive error and leaves the current game running whenever safe.

## Storage and validation

Files use this platform-data-relative layout:

```text
states/<sanitized-title>-<full-game-sha256>/slot-0.gpgxstate
...
states/<sanitized-title>-<full-game-sha256>/slot-9.gpgxstate
```

For ordinary cartridge and disc files, the identifier is the raw file SHA-256. For a
CUE game, it is a framed SHA-256 over the validated CUE plus every referenced track.
Changing any track therefore selects another state directory even when two sheets have
identical text, while relocating an unchanged sheet/track set preserves its identity.
Save-state activation hashes on its storage thread and checks the service's atomic stop
request between 64 KiB chunks, so a large disc cannot delay shutdown until EOF.

The `.gpgxstate` file begins with a fixed 128-byte little-endian `GPGXST01` envelope.
It stores schema/header lengths, millisecond timestamp, hardware identifier, slot,
frontend frame number, payload length, the full game SHA-256, payload SHA-256, and the
raw core-version signature. The unchanged Genesis Plus GX state payload follows.
Payloads are bounded to 2 MiB and writes use the same atomic transaction mechanism as
save RAM.

Before loading, the frontend validates the regular file, total length, magic, schema,
slot, game identity, hardware, timestamp, payload checksum, and core signature on its
storage thread. Only then is the raw payload submitted to the core-owning emulation
thread. A failed core restore is transactional: the adapter reloads the state that was
active before the rejected candidate.

## Invalid or incompatible states

An **Invalid** slot cannot be loaded. Its tooltip provides a concise reason, such as a
checksum mismatch or wrong-game identity, and it may be deleted from the slot menu.
Do not rename another game's state into a current game's slot directory; the embedded
SHA-256 still prevents it from loading. States made by an incompatible future schema
or core serializer may need the application version that created them. The frontend
does not weaken validation or regenerate reference data to accept a corrupt file.

Raw-state import/export and thumbnails are optional future extensions. The current
wrapper deliberately retains the raw payload intact so those features can be added
without changing the emulator core's serialization algorithm.
