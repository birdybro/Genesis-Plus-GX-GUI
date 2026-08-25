# Cheats

The desktop frontend exposes Genesis Plus GX's existing cheat behavior through
**Tools → Cheats…**. The action becomes available after the loaded game has been
hashed and assigned a per-game identity. Cheats are never sent to the core as text:
the frontend first validates the complete code and converts it to a bounded typed
patch list on the GUI side, then the emulation worker applies that list on the sole
core-owning thread.

## Supported formats

Genesis / Mega Drive and Sega CD sessions accept:

- Game Genie: `XXXX-XXXX`, using the Genesis Game Genie alphabet
- Action Replay / Pro Action Replay: `XXXXXX:XXXX`

SG-1000, Mark III, Master System, and Game Gear sessions accept:

- Game Genie: `XXX-XXX` or `XXX-XXX-XXX`
- Action Replay: `XXXX-XXXX`
- Fusion RAM: `XXXX:XX`
- Fusion ROM: `XXXXXX:XX`

Use `+` between the parts of a multi-line or multi-address cheat. Leading and trailing
whitespace is removed and hexadecimal text is normalized to uppercase. The parser
requires an exact supported length and separator layout; it rejects unknown Game Genie
characters, partial values, trailing text, empty parts, and more than 150 enabled core
patches. An invalid disabled entry is also rejected so a stored list cannot become
unsafe merely by checking its Enabled box later.

## Using the manager

Choose **Add**, give the cheat a descriptive name, enter the code, and check Enabled
when it should affect the running game. **Apply** validates every row, atomically saves
the complete per-game list, and queues the decoded enabled patches at an emulated frame
boundary. **Cancel** leaves unapplied edits alone. Select a row and choose **Remove** to
delete it from the list, then Apply or OK.

ROM patches are installed when a list is applied and original bytes are restored in
reverse order when the list changes or the game unloads. RAM patches run through the
core's established input/VBlank update point. Master System and Game Gear bank changes
reapply only validated byte patches through the existing mapper update hook. Sega CD
RAM addresses retain the upstream frontend's program-RAM and word-RAM handling.

## Storage and recovery

Lists use schema-versioned JSON under:

```text
config/cheats/<sanitized-title>-<sha256>.json
```

The SHA-256 identity prevents games with the same filename from sharing cheats. Files
embed both the full hash and the decoded system family and are read with a 128 KiB
limit. Writes use the frontend's atomic same-directory replacement helper. A truncated,
malformed, future-schema, wrong-game, wrong-system, or invalid-code file is not applied;
the manager opens with an empty safe list and reports the storage problem. The original
file is not required by or interpreted inside the Genesis Plus GX core.
