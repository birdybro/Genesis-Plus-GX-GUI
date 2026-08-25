# BIOS and Firmware

Genesis Plus GX GUI never includes or downloads proprietary Sega firmware. Obtain and
dump any required firmware legally, then select it under **Tools → BIOS Settings…**.
The configuration stores paths only; it does not copy or alter the selected files.

## Slots and structural validation

| Slot | Expected file shape | Region shown by the manager |
| --- | --- | --- |
| Genesis / Mega Drive boot ROM | exactly 2 KiB | Worldwide |
| Master System BIOS | 1 KiB-aligned, 1 KiB through 4 MiB | USA, Europe, or Japan |
| Game Gear BIOS | exactly 1 KiB | Worldwide |
| Sega CD / Mega CD BIOS | exactly 128 KiB | USA, Europe, or Japan |

The manager checks that each configured path fits the desktop core host boundary,
exists, names a regular file, can be read completely within its slot's bound, meets the
expected size, and is not made from one repeated byte. It then displays a SHA-256 hash
and a detected firmware family where the image contains a recognizable model marker.

These checks are intentionally structural. A valid status does not authenticate a
dump, guarantee compatibility, or prove its legal provenance. The application does not
ship a list of proprietary firmware hashes, so legitimate regional and hardware
revisions are not rejected merely because a checksum is unfamiliar.

## Sega CD / Mega CD

Select the BIOS matching the console region used by the disc. The USA, Europe, and
Japan settings are independent. Sega CD/Mega CD emulation cannot boot without a usable
regional firmware image. At game load, only paths currently marked valid by the BIOS
manager are sent through the bounded command queue to the emulation thread. The core
detects the disc region before choosing the corresponding path. A missing, moved, or
unreadable file produces a region-specific error and leaves no half-loaded session.

The default automated workflow uses a generated non-proprietary test BIOS and synthetic
disc. An optional real-firmware smoke test can be enabled at configure time with
`-DGENPLUSGX_ENABLE_EXTERNAL_FIXTURE_TESTS=ON`; set
`GENPLUSGX_TEST_SEGA_CD_US_BIOS` to a legally obtained 128 KiB USA BIOS before running
CTest. That path is read locally, never copied into the source/build tree, and never
uploaded by project automation.

## Troubleshooting statuses

- **Not configured:** no path is stored for the slot.
- **Missing:** the stored path no longer exists.
- **Not a file:** the path names a directory or another non-file object.
- **Unreadable:** the file could not be measured or read completely.
- **Path too long:** the native path cannot fit the emulator host interface.
- **Invalid size:** the file shape does not match the selected firmware slot.
- **Invalid content:** the image is an obvious repeated-byte placeholder.
- **Valid:** structural validation passed and the displayed checksum was calculated.

Changing a setting is staged inside the dialog. **Apply** saves without closing,
**OK** saves and closes, and **Cancel** discards changes not already applied. The JSON
configuration uses an atomic replacement transaction, so an interrupted write cannot
partially replace a previously valid configuration.
