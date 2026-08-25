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
