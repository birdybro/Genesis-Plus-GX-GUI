# Test Fixture Provenance

No commercial ROM, proprietary BIOS, copyrighted game asset, or automatically fetched
binary is used by the test suite. Binary fixtures are generated into temporary
directories at test runtime and removed by RAII cleanup.

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

## Generated metadata/property fixtures

`unit.game_metadata` derives cartridge, SMD, Master System, Game Gear, SG-1000, Sega CD,
and CUE/BIN metadata inputs entirely at runtime. Cartridge and disc bytes reuse the
original CC0 generators above; the test creates 8-bit `TMR SEGA` headers, SMD
interleaving, and CUE text itself in a temporary directory. A fixed-seed corpus creates
2,000 bounded arbitrary byte arrays up to 70,000 bytes to exercise truncated header
offsets. No corpus binary or result is stored, downloaded, or automatically promoted
to a golden reference.

`unit.game_library_scanner` writes the generated Genesis ROM and a four-byte original
SG-1000 placeholder into a temporary directory alongside an unsupported text file. It
then performs flat and recursive scans, verifies exact indexed systems and paths, and
removes one temporary generated file to exercise stale-row cleanup. The database test
creates malformed text and structurally incomplete SQLite files solely inside its
temporary directory to verify collision-safe recovery. No fixture survives the test.
