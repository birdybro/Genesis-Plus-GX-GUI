# Test Fixture Provenance

No commercial ROM, proprietary BIOS, copyrighted game asset, or automatically fetched
binary is used by the test suite. Binary fixtures are generated into temporary
directories at test runtime and removed by RAII cleanup.

## Original Libretro Slang pass-through preset

| Field | Value |
| --- | --- |
| Stored filenames | `libretro-pass.slangp`, `libretro-pass.slang` |
| Author | Original fixture authored for Genesis Plus GX GUI |
| Purpose | Prove that the pinned librashader runtime compiles a modern Slang preset and samples the supplied OpenGL texture into a caller-owned output texture |
| Provenance | No copied shader-pack code or assets; dedicated to CC0-1.0 with the other test fixtures |
| Expected behavior | One pass scales a 4×4 red/green/blue/white texture to 64×48; at least one quarter of output RGB pixels must be non-black |

The fixture has no lookup texture, game image, trademark graphic, or golden screenshot.
The same integration test separately runs the original bundled CRT preset through the
real `QOpenGLWidget` and compares its captured output with normal presentation. Its
asymmetric red/green/blue/white quadrants make vertical and horizontal orientation
errors semantic rather than pixel-golden assertions.

## Optional user-owned game acceptance

`genplusgx_external_rom_acceptance_test` accepts a developer-supplied game path at
runtime. That game is not a project fixture: it is never fetched, copied, hashed into a
golden, committed, or required by CI. The caller also supplies an output directory;
game-derived comparison PNGs remain there for local inspection and must not be added to
the repository. Required builds retain only the generated CC0 inputs documented below.

## Generated Genesis RAM marker ROM

| Field | Value |
| --- | --- |
| Stored filename | None; runtime name is `genplusgx-fixture-<unique>.bin` |
| Generator | `tests/utilities/synthetic_rom.cpp` |
| Purpose | Prove real 68000 reset, instruction execution, RAM writes, metadata parsing, and frame execution |
| Size | 64 KiB |
| Provenance | Original fixture authored for this project; generated output is dedicated to CC0-1.0 |
| Expected system | Genesis / Mega Drive (`SYSTEM_MD`) |

The ROM contains a conventional Genesis header and a minimal original 68000 program.
Its vector table directs reset and every exception to address `0x200`. The program:

```text
move.l #$00ff0000,a0
move.l #$13579bdf,(a0)
move.w #$cafe,4(a0)
move.b #$42,6(a0)
configure VDP Mode 5, 320x224, with an original solid-color test image
program PSG channel zero with a deterministic centered square wave
poll controller port A into work RAM and repeat
```

The test runs a real emulated frame and checks the resulting work-RAM values and bounded
program counter. Video tests run the viewport change through a second frame and verify
the tightly packed RGB565 frame hash. Audio tests drain the core after each emulated
frame and verify stereo sample values and their hash. The header checksum is calculated
by the generator. Input tests inject neutral frontend snapshots and verify both mapped
core state and the active-low byte observed by the 68000 program. There is no third-party
program code, game content, artwork, sampled audio, or trademark graphic in the fixture.

## Generated 8-bit Z80 RAM marker ROMs

| Field | Value |
| --- | --- |
| Stored filename | None; runtime extensions are `.sg`, `.sms`, and `.gg` |
| Generator | `makeZ80RamMarkerRom()` in `tests/utilities/synthetic_rom.cpp` |
| Purpose | Execute the same semantic Z80 RAM-write test on SG-1000, Mark III, Master System, and Game Gear |
| Size | 32 KiB |
| Provenance | Original fixture authored for this project; generated output is dedicated to CC0-1.0 |
| Expected systems | `SYSTEM_SG`, `SYSTEM_MARKIII`, `SYSTEM_SMS2`, and `SYSTEM_GG` |

The ROM contains a twelve-byte original Z80 program that disables interrupts, creates
a safe stack, writes marker byte `0x5a` to work RAM at `0xc000`, and loops in bounds.
The test loads it through each real core hardware path, executes a frame, checks the
RAM marker, and verifies native viewport geometry including the 160×144 Game Gear view.

## Generated cartridge boot firmware

| Field | Value |
| --- | --- |
| Stored filename | None; all files are generated in the temporary directory |
| Generators | `makeGenesisBootRom()` and `makeZ80BootRom()` |
| Purpose | Prove Genesis, regional Master System, and Game Gear firmware settings reach and activate in the core |
| Sizes | 2 KiB Genesis; 1 KiB Master System/Game Gear |
| Provenance | Original fixture authored for this project; generated output is dedicated to CC0-1.0 |

The Genesis image contains the core-recognized `GENESIS OS` structural marker and an
original self-loop. The Z80 images contain the original marker program plus deterministic
non-uniform padding. They test firmware plumbing only and contain no Sega firmware code.

## Generated Genesis SRAM writer ROM

| Field | Value |
| --- | --- |
| Stored filename | None; runtime extension is `.md` |
| Generator | `makeGenesisSramWriterRom()` in `tests/utilities/synthetic_rom.cpp` |
| Purpose | Exercise the core's real cartridge-SRAM map and live frontend persistence |
| Size | 64 KiB ROM with a declared 64 KiB SRAM region at `0x200000`–`0x20ffff` |
| Provenance | Original fixture authored for this project; generated output is dedicated to CC0-1.0 |
| Expected system | Genesis / Mega Drive (`SYSTEM_MD`) |

The conventional header declares `RA` save memory. Its minimal original 68000 program
loads address `0x200000`, writes byte `0x5a`, and enters a bounded loop. Core tests first
observe erased `0xff` SRAM, execute one frame, then observe that actual mapped write.
Persistence tests generate the image in a temporary directory and verify unload/reload,
corruption rejection, atomic-save failure behavior, and shutdown flushing. No binary is
committed and no third-party program or game content is incorporated.

## Generated Sega CD test BIOS

| Field | Value |
| --- | --- |
| Stored filename | None; runtime extension is `.bin` |
| Generator | `makeSegaCdBios()` in `tests/utilities/synthetic_rom.cpp` |
| Purpose | Exercise regional firmware selection and Sega CD lifecycle without proprietary firmware |
| Size | 128 KiB |
| Provenance | Original fixture authored for this project; generated output is dedicated to CC0-1.0 |
| Expected system | Sega CD / Mega CD (`SYSTEM_MCD`) |

This is not a dump or reimplementation of Sega firmware. Its original vector table
directs the 68000 to a two-byte self-loop, with deterministic non-uniform padding so
the frontend's structural BIOS validation can inspect it. It can initialize and execute
the core for plumbing tests but is not intended to boot any game.

## Generated Sega CD test disc

| Field | Value |
| --- | --- |
| Stored filename | None; runtime extension is `.iso`, or a temporary `.cue` plus `.bin` |
| Generator | `makeSegaCdDiscImage()` in `tests/utilities/synthetic_rom.cpp` |
| Purpose | Exercise disc detection, region selection, CUE/BIN mounting, eject/change, pacing, and BRAM persistence |
| Size | 150 cooked 2,048-byte sectors (300 KiB) |
| Provenance | Original fixture authored for this project; generated output is dedicated to CC0-1.0 |
| Expected system | Sega CD / Mega CD (`SYSTEM_MCD`) |

The first sector contains only the minimum original `SEGADISCSYSTEM` identifier,
test titles, and one generated region marker. The rest is zero-filled. Tests generate
USA and Europe variants and a CUE sheet that references a generated local BIN file.
There is no game program, Sega security program, sampled audio, artwork, or third-party
data. Real-BIOS testing is excluded by default and never creates or fetches firmware.

`unit.game_file` creates all CUE sheets and referenced files inside temporary directories.
Its original text cases cover valid multi-file data/audio layouts, malformed quoting,
track/index ordering, invalid time fields, overlong lines/files, binary controls,
absolute and slash/backslash traversal, missing/empty/directory targets, and a symlink
that resolves outside the CUE directory where the host permits symlink creation. A
fixed-seed 2,000-input bounded byte corpus exercises the pure parser. No fuzz input or
temporary disc material is retained or promoted to a golden file.

## Generated metadata/property fixtures

`unit.game_metadata` derives cartridge, SMD, Master System, Game Gear, SG-1000, Sega CD,
and CUE/BIN metadata inputs entirely at runtime. Cartridge and disc bytes reuse the
original CC0 generators above; the test creates 8-bit `TMR SEGA` headers, SMD
interleaving, and CUE text itself in a temporary directory. A fixed-seed corpus creates
2,000 bounded arbitrary byte arrays up to 70,000 bytes to exercise truncated header
offsets. No corpus binary or result is stored, downloaded, or automatically promoted
to a golden reference.

`unit.game_library_scanner` writes the generated Genesis ROM, generated Sega CD image,
an original CUE that references that image as `track.bin`, and a four-byte original
SG-1000 placeholder into a temporary directory alongside an unsupported text file. It
then performs flat and recursive scans, verifies exact indexed systems and paths,
requires the CUE but not its duplicate payload row, and removes one generated file to
exercise stale-row cleanup. The database test creates malformed text and structurally
incomplete SQLite files solely inside its temporary directory to verify collision-safe
recovery. No fixture survives the test.

`unit.persistence` and `unit.game_metadata` create temporary CUE/BIN sets. The
persistence cases use identical original two-file sheet text, generated CC0 data-track
bytes, and a tiny original audio-track pattern; one changes only the second track while
another relocates the unchanged files. The metadata cases similarly change one
generated track and relocate another. Together they prove relocation stability,
every-referenced-file identity, shared library/save hashing, and distinct persistence
directories without retaining any binary or digest as an automatically regenerated
golden.

`gui.game_library` creates the same original synthetic Genesis ROM in a temporary game
directory and drives it through the native Add Directory control, real background
scanner, SQLite index, filters, favorite and launch history, and directory removal. A
32×48 solid-color PNG generated by Qt exercises local artwork preview/selection. Both
files are original test output, are never fetched, and are deleted with the temporary
directory.

`unit.screenshot_service` creates a 2×2 RGB565 frame in memory containing exact red,
green, blue, and white pixels. It writes PNG files only beneath a Qt temporary directory
and reads them back to verify conversion and collision behavior. The pixel pattern is
original test data dedicated to CC0-1.0; no image fixture, game artwork, or golden binary
is committed.

`core.cheats` reuses the generated Genesis RAM marker ROM. It installs a word patch in
unused generated ROM padding, verifies the changed host word, clears the patch, and
verifies exact restoration. It also applies and clears a work-RAM patch at an address
the original test program does not touch. `unit.cheats` generates no binary data; its
fixed examples and 10,000-case deterministic bounded text corpus exercise the cheat
decoder and temporary JSON store. No commercial cheat database, game code, or copied
code list is included.

## Generated ZIP and M3U container fixtures

`unit.game_file`, `integration.archive_playlist`, `gui.game_loading`, and
`gui.session_resume_application` create ZIP archives at runtime with the bundled MiniZip
writer. Members contain only the original CC0 Genesis, Master System, or SG-1000 byte
fixtures documented above. Cases use both stored and DEFLATE methods and generate
multi-member, nested-name, traversal-name, unsupported-content, stale-selection,
truncated, and cache-reuse conditions. No archive or extracted member survives its
temporary test directory.

The same tests write original UTF-8 M3U/M3U8 text that references generated Sega CD
disc images in a temporary directory. Valid cases prove declared order, content
enumeration, first-disc loading, and disc changes. Negative cases cover invalid UTF-8,
absolute/drive/URL/parent paths, symlink escape, duplicate or missing discs, excessive
line/disc counts, and a fixed-seed bounded parser corpus. These playlists contain no
commercial filenames, disc data, firmware, or copied metadata; all generated container
fixtures are dedicated to CC0-1.0 with the underlying project-authored fixtures.
