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

Choose **Emulation → State Slot → Manage Save States…** for the visual browser. It
shows all ten slots with native-frame previews, optional names, timestamps, emulated
frame numbers, payload sizes, and validation status. A name may contain up to 96 UTF-8
bytes. **Save / Replace** records the current native core framebuffer as a PNG preview;
the preview deliberately excludes shaders, bezels, and native window chrome.

**Import…** validates a `.gpgxstate` against the running game's full identity and
hardware before atomically replacing the selected slot. **Export…** validates the slot
before writing a self-contained copy chosen by the user. Import never trusts the slot
number embedded in the source: after validation it rewrites the envelope for the
selected destination while preserving the payload, name, preview, timestamp, and frame
number. A wrong-game, wrong-system, corrupt, oversized, or unsupported file is rejected
without changing the destination slot or reaching the emulator core.

An operation disables other state and game-lifecycle actions until it finishes. A
successful save, load, or delete is reported in the status bar. A failure opens a
descriptive error and leaves the current game running whenever safe.

## Storage and validation

Files use this platform-data-relative layout:

```text
states/<sanitized-title>-<full-game-sha256>/slot-0.gpgxstate
...
states/<sanitized-title>-<full-game-sha256>/slot-9.gpgxstate
states/<sanitized-title>-<full-game-sha256>/resume.gpgxstate
```

For ordinary cartridge and disc files, the identifier is the raw file SHA-256. For a
CUE game, it is a framed SHA-256 over the validated CUE plus every referenced track.
Changing any track therefore selects another state directory even when two sheets have
identical text, while relocating an unchanged sheet/track set preserves its identity.
Save-state activation hashes on its storage thread and checks the service's atomic stop
request between 64 KiB chunks, so a large disc cannot delay shutdown until EOF.

New `.gpgxstate` files use schema 3. They begin with a fixed 176-byte little-endian
`GPGXST01` envelope, followed by a checksummed bounded presentation block, an optional
bounded RetroAchievements progress block, and then the unchanged Genesis Plus GX state
payload. The envelope stores schema/header lengths,
millisecond timestamp, hardware identifier, slot, frontend frame number, payload
length, the full game SHA-256, payload SHA-256, presentation length/SHA-256, and the raw
core-version signature. The presentation block stores length-framed UTF-8 name and PNG
preview bytes. Payloads are bounded to 2 MiB, previews to 512 KiB and 1024×1024 pixels,
and writes use the same atomic transaction mechanism as save RAM. Frontend-generated
previews are at most 256×192.

Existing schema-1 files retain their original 128-byte header, and schema-2 files retain
their presentation data; both remain readable, loadable, exportable, and importable.
Saving or renaming a slot writes schema 3. The schema-3 payload checksum covers both the
separately length-bounded achievement progress and raw state. The core payload format
itself is neither translated nor modified.

Before loading, the frontend validates the regular file, total length, magic, schema,
slot, game identity, hardware, timestamp, payload and presentation checksums, bounded
UTF-8 name, decodable PNG dimensions, and core signature on its storage thread. Only
then is the raw payload submitted to the core-owning emulation thread. A failed core
restore is transactional: the adapter reloads the state that was active before the
rejected candidate.

## Automatic session resume

Automatic resume is opt-in under **Tools → Settings → General → Session
Settings**. During a clean application exit, the composition root pauses the emulation
worker, captures on the core-owning thread, commits `resume.gpgxstate` through the
bounded state-storage service, and only then atomically records the absolute game path
in `config/session-settings.json`. The dedicated checkpoint does not replace slots
0–9.

On the next launch, the stored game is opened only when no game was supplied on the
command line. Emulation remains paused until state-storage activation has recalculated
the game identity and hardware. The checkpoint then passes the same length, signature,
SHA-256, game, system, and core validation as a manual slot before reaching the worker.
After a successful restore normal execution and audio begin. A missing game, missing or
corrupt checkpoint, rejected core restore, or unavailable state service clears the
session marker and starts a normal game session when possible; it never weakens state
validation. Explicitly closing the game clears the marker. A crash retains only the
last checkpoint from an earlier clean exit.

When a cartridge was opened with an IPS, BPS, or UPS patch, the session marker stores
both absolute source and patch paths. Startup reapplies the patch and activates the
checkpoint only after the resulting bytes reproduce the same SHA-256 identity. Manual
slots and the automatic checkpoint therefore cannot silently cross between patched and
unpatched variants of the same source ROM. A missing patch clears the stale resume pair.

## Invalid or incompatible states

An **Invalid** slot cannot be loaded. Its tooltip provides a concise reason, such as a
checksum mismatch or wrong-game identity, and it may be deleted from the slot menu.
Do not rename another game's state into a current game's slot directory; the embedded
SHA-256 still prevents it from loading. States made by an incompatible future schema
or core serializer may need the application version that created them. The frontend
does not weaken validation or regenerate reference data to accept a corrupt file.

Imported and exported files use the frontend envelope rather than a bare core blob so
identity, hardware, checksum, and compatibility errors remain explicit. The raw payload
is still stored intact, which keeps core serialization authoritative.
