# Input Movies and TAS Editing

Input movies record controller state at authoritative emulated-frame boundaries. They
are intended for deterministic playback, input debugging, and tool-assisted input
editing; they do not contain a ROM, BIOS, screenshot, audio, or video capture.

## Recording and playback

Load a game, then choose **Tools → TAS and Input Movies → Start Input Movie
Recording…** (`Ctrl+Alt+R`). Choose an absolute `.gpgx-movie` destination. The
emulation worker captures one raw Genesis Plus GX state and then records the complete
eight-port, 12-button/analog input snapshot consumed by each authoritative frame.
Choose the same action again to stop. The completed file is committed atomically so a
failed write does not replace an earlier movie.

Choose **Play Input Movie…** (`Ctrl+Alt+P`) while the matching game is loaded. Playback
restores the movie's initial core state, ignores live gameplay input for the recorded
timeline, stops and pauses exactly after the last frame, and then restores the newest
live input with a monotonic worker-owned sequence. Pause, frame advance, fast-forward,
and slow motion remain available because they change presentation/pacing rather than
the recorded core result.

A movie is accepted only when all three recorded identities match the live session:

- SHA-256 of the exact game content;
- SHA-256 of deterministic system, core video/audio, input-device, BIOS, and enabled
  cheat settings; and
- the exact Genesis Plus GX GUI core Git build identifier.

This strict check prevents a movie from silently desynchronizing against another game,
configuration, BIOS, cheat set, or core revision. It deliberately means that movies
are not promised to play across builds. Netplay and RetroAchievements Hardcore Mode
cannot start a movie, and mutating operations such as reset, state restore, disc change,
cheat replacement, or core-setting replacement are rejected while one is active.
Stop the movie before changing or closing a game. A clean application shutdown
finalizes a non-empty active recording before the emulation worker is joined.

## TAS editor

Choose **Tools → TAS and Input Movies → Edit Input Movie…** with no movie active. The
editor exposes a frame table for all eight emulated ports. Select a frame and port to
edit connection state, Up/Down/Left/Right, A/B/C/X/Y/Z, Mode, Start, and signed analog
X/Y values. You can apply a frame, insert a neutral frame, duplicate or delete a frame,
or truncate the timeline after the selected frame. Every mutation increments the
rerecord counter. Author and plain-text notes are optional and bounded.

The editor works on an owned copy and writes only after **Save**. Cancel leaves the
source untouched. It never invokes the emulator core, guesses input, or regenerates a
failed reference result.

## File and safety contract

`GPGXMOV1` is a versioned little-endian frontend envelope. It preserves the raw core
state payload unchanged, run-length encodes identical consecutive input frames, and
ends with SHA-256 over the complete preceding content. Readers reject a bad digest,
unknown version/flags, invalid UTF-8, unsupported button bits, zero or inconsistent run
lengths, trailing bytes, and every truncated field before playback.

Limits are one million frames, 32 MiB for the initial raw state, 96 MiB for the whole
file, 128 UTF-8 bytes for the author, and 4 KiB for notes. These checks occur before
large allocations. Movies normally live under the platform `recordings/` directory,
but the user may choose another writable location.

`unit.input_movie` covers deterministic encoding, compression, atomic round trips,
identity rejection, editing, and bounded corruption mutations. `integration.input_movie`
records and replays a generated CC0 ROM through the real worker, compares restored raw
states, checks state-changing lockouts and exact timeline completion, and verifies live
input recovery. `gui.movie_streaming` drives the editor and MainWindow transition/error
gates headlessly on every CI platform.
