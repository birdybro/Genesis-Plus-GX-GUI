# Physical Sega CD / Mega CD Media

Genesis Plus GX GUI can open an original Sega CD or Mega CD from a local optical
drive on Windows, Linux, and macOS. The application reads the source disc without
modifying it, creates a private raw BIN/CUE snapshot in the application cache, and
passes that validated snapshot through the same emulation-thread and Genesis Plus GX
disc path used for ordinary image files.

## Before opening a disc

Configure a legally obtained BIOS for the disc's region under **Tools → BIOS
Settings…**. The application does not include, download, or copy Sega firmware. An
optical drive capable of raw 2,352-byte CD-sector and CDDA reads is required; a DVD-only
drive or an adapter that exposes only cooked data may not work.

Insert the disc, then choose **File → Open Physical Sega CD Disc…**. Select a drive and
choose **Open Disc**. Discovery and reading run on a dedicated service thread, so the
window and an already running game remain responsive. The progress bar reports raw
sectors completed. **Cancel Import** stops between bounded reads and removes the partial
copy. Do not eject or disconnect the drive until reading finishes.

The importer accepts the conventional Sega CD layout supported by this frontend: one
leading Mode 1 or Mode 2 data track followed by optional CDDA tracks. It validates the
table of contents, track order, disc-size bound, first-sector Sega CD signature, every
read result, generated CUE sheet, byte count, and SHA-256 before asking the core to load
the disc. Audio-first and multi-data-track layouts fail with a descriptive error rather
than being guessed. Genesis Plus GX remains authoritative for emulation, region
detection, BIOS selection, CDDA playback, timing, and backup RAM.

## Snapshot lifetime and privacy

The cache entry is content-addressed beneath `cache/physical-media` in the selected
normal or portable application-data root. A complete CD can require roughly 800 MiB of
free space. A snapshot is committed by an atomic directory rename only after the full
disc succeeds; failed or cancelled imports leave no partial entry. An existing cache
entry is reused only after its exact CUE text, size, and complete SHA-256 are verified.

The active snapshot remains available until the game unloads so the core can seek data
and CDDA tracks. It is removed after a successful close/replacement and during orderly
shutdown. Physical sessions are deliberately omitted from Recent Games, the game
library, and automatic session resume because a transient cache path is not a durable
user-owned disc image. Save RAM and save states still use the imported content identity
and normal atomic persistence paths.

No disc bytes, title data, BIOS data, or drive path are transmitted. The feature has no
network component. Logs identify the selected drive by the operating system's local
display name but do not contain disc sectors.

## Platform notes

- Windows uses the system CD-ROM raw-read and table-of-contents interfaces. Run the
  application as the same user who can access the drive; administrator access should
  not normally be necessary.
- Linux discovers `/dev/cdrom`, `/dev/sr*`, and equivalent canonical optical devices.
  The user must have read permission through the distribution's optical-drive group or
  desktop session policy. Do not run the emulator as root to bypass a permission error.
- macOS discovers mounted CD media through IOKit and reads its raw device. Privacy or
  removable-media policy may require approving access for the application or terminal.

Drive firmware, scratched media, copy-protection outside the normal Sega CD format, and
USB bridges with incomplete raw-audio support can produce a read error. Cleaning or
redumping a legally owned disc with a dedicated preservation tool is the appropriate
fallback; the emulator never silently substitutes zeroes for unreadable sectors.

## Testing and hardware qualification

Required tests use an original generated mixed-mode fixture: 150 synthetic raw data
sectors with a Sega CD marker followed by 75 deterministic CDDA sectors. Unit coverage
checks validation, CUE generation, complete hashing, tamper rejection, atomic cleanup,
read failures, active and queued cancellation, bounded service queues, native discovery
contracts, and shutdown. Integration coverage loads the generated raw snapshot through
the real core with generated non-proprietary firmware and executes a Sega CD frame. GUI
coverage drives discovery, selection, progress, cancellation, error recovery, typed
launch, action gating, and cleanup.

CI cannot place physical hardware in hosted Windows, Linux, and macOS runners. Before a
public release, maintainers should additionally test at least one real mixed-mode Sega
CD on each available target using firmware and media they are legally entitled to use.
Such data must never be committed or uploaded as a CI artifact.
