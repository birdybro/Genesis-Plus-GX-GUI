# Game Information

The desktop frontend reads descriptive game metadata without initializing or changing
the Genesis Plus GX core. With a game loaded, choose **Tools → Game Information…**.
The read runs on a bounded background service so hashing a large image does not block
the window or the emulation thread.

## Displayed fields

Where the selected format provides them, the dialog shows:

- domestic and international titles;
- detected Sega system and region;
- cartridge/disc format, product code, ROM type, and peripheral declaration;
- declared ROM size and actual file size;
- header and computed Genesis checksums;
- cartridge SRAM/media description;
- CUE track count and its local data-track path;
- absolute selected path and SHA-256 content identifier; and
- a concise note when a header is absent or a format delegates metadata to the core.

An emulation load remains authoritative. Metadata detection is informational and does
not override the system, region, mapper, or disc behavior chosen by Genesis Plus GX.

## Format behavior and safety

Genesis/Mega Drive headers, interleaved SMD headers, Master System/Game Gear `TMR SEGA`
headers, SG-1000 extensions, and Sega CD security-area headers are parsed with explicit
bounds checks. CUE sheets are read from a 64 KiB maximum header window, count at most 99
tracks, and inspect only a relative data file contained by the CUE directory. Absolute
and parent-traversing CUE references are not followed by this metadata reader.

The selected file is streamed in 64 KiB chunks for SHA-256 and is never loaded wholesale
for display. A 16 GiB safety ceiling prevents accidentally hashing an unreasonable or
special file. The reader detects a file whose size or modification timestamp changes
during inspection and reports a recoverable error. It never writes to a game image.

CHD identification is available only when CHD loading is part of the desktop build.
Embedded CHD metadata and final image recognition remain the responsibility of the
existing core. CUE metadata describes the selected CUE file; its SHA-256 is not silently
replaced with the hash of a referenced BIN.
