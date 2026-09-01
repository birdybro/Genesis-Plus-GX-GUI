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

### Importing local lists

Choose **Import…** on the Codes tab to add entries from a local `.cht` or `.txt` file.
The supported `.cht` subset follows RetroArch's emulator-handled fields:
`cheats`, `cheatN_desc`, `cheatN_code`, and `cheatN_enable`. Direct-memory
`cheatN_address`/value records are intentionally not interpreted. Plain-text files use
one code per line, optionally prefixed with `Name | `. Blank lines and lines beginning
with `#` or `;` are ignored.

Imports are limited to 128 KiB, 4096 bytes per line, valid UTF-8, and 150 entries. The
entire file must parse for the active hardware family or nothing is added. Existing
normalized codes are skipped. Imported entries are always disabled—even when a
RetroArch file says `enable = true`—and remain only in the dialog until the user
explicitly enables entries and chooses Apply or OK. This avoids silently trusting a
downloaded code list. The format reference is the
[RetroArch cheat-code guide](https://docs.libretro.com/guides/cheat-codes/); no online
database or download service is used.

### Searching RAM

The **Search RAM** tab requests an immutable work-RAM snapshot from the emulation-owner
thread. Start with an initial snapshot, change a value in the game, select an unsigned
or signed exact/change comparison and optional value, then choose **Filter snapshot**.
Repeat until the candidate set is useful. The table displays at most 1024 candidates,
while the bounded search model keeps the complete applicable RAM range.

Genesis-family searches use aligned, big-endian 16-bit words from the 64 KiB 68000 RAM
and create `FFxxxx:XXXX` Action Replay codes. SG-1000, Mark III, Master System, and Game
Gear searches use bytes from the active 8 KiB work RAM and create `Cxxx:XX` Fusion RAM
codes. **Add selected as cheat** creates a disabled Codes-tab row; it does not patch or
persist anything until explicitly enabled and applied. Search snapshots, failures, and
breakpoint notifications carry separate client tokens so the normal manager cannot
consume or interfere with the hidden developer debugger's traffic.

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
