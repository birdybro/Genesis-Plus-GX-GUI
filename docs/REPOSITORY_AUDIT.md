# Repository Audit

Audit date: 2026-08-25

Baseline commit: `27426f00aa68f9f358c86919e8a40985326fa05b`

Branch: `master`

Repository: `https://github.com/birdybro/Genesis-Plus-GX-GUI`

## Repository state

The worktree was clean and `master` matched `origin/master` when the audit began. The
configured remotes are:

| Remote | URL | Purpose |
| --- | --- | --- |
| `origin` | `https://github.com/birdybro/Genesis-Plus-GX-GUI.git` | GUI fork |
| `upstream` | `https://github.com/ekeeke/Genesis-Plus-GX.git` | Authoritative emulator upstream |

The baseline contains 977 tracked files. No ROM, BIOS, credential, or local user
configuration was found. The `builds/` directory contains upstream-distributed Wii,
GameCube, and libretro binaries; these predate this desktop project and are not desktop
build outputs.

## Existing build and frontend inventory

There is no root CMake project and no Qt desktop frontend at the baseline. Existing
build entry points are intentionally retained:

| Area | Build entry point | Notes |
| --- | --- | --- |
| libretro | `Makefile.libretro` | Broad platform matrix; best existing source-list reference |
| GameCube | `Makefile.gc`, `Makefile.gc.low-mem` | libogc frontend |
| Wii | `Makefile.wii` | libogc frontend with its own GUI |
| SDL | `sdl/Makefile.sdl1`, `sdl/Makefile.sdl2` | Explicitly documented as a basic debugging frontend |
| GCW0 | `gcw0/Makefile` | Handheld frontend |
| Vita | `psp2/Makefile` | Vita frontend |

The SDL frontend centralizes display allocation, audio callbacks, input polling, pacing,
ROM loading, BIOS loading, save persistence, save states, and shutdown in
`sdl/sdl2/main.c`. It is useful as an integration reference, but is not a suitable base
for GUI expansion. In particular, it writes `game.srm`, `scd.brm`, `cart.brm`, and
`game.gp0` relative to the process working directory.

## Baseline build results

The audit host is Linux x86-64 with CMake 4.4.2, GCC 16.1.1, Ninja 1.13.2,
Qt 6.11.1, SDL2 2.32.70, and SDL3 3.4.14.

### libretro

Command:

```sh
make -f Makefile.libretro platform=unix -j4
```

Result: **pass**. `genesis_plus_gx_libretro.so` linked successfully. GCC reported two
inherited `-Wdiscarded-qualifiers` warnings in `libretro/libretro.c:1274` and
`libretro/libretro.c:1276`. The build was cleaned with the makefile's `clean` target.

### SDL2 debug frontend

Command:

```sh
cd sdl
make -f Makefile.sdl2 -j4
```

Result: **fail**. `sdl/sdl2/main.c:672-680` uses `SDLK_KP9` through `SDLK_KP1`, which
are not SDL2 keycode names in the installed SDL version (`SDLK_KP_9` through
`SDLK_KP_1` are available). This is an existing frontend compatibility defect, not a
core failure. All partial objects and the build directory were removed after recording
the result.

GameCube, Wii, GCW0, and Vita builds require their platform SDKs and were not executable
on the audit host. No baseline automated test suite exists.

## Source boundaries

### Authoritative core

The emulator implementation remains in place under `core/`:

- top-level lifecycle, memory, VDP, ROM loading, and state code;
- `core/m68k/` and `core/z80/` CPU implementations;
- `core/cart_hw/` cartridge hardware, EEPROM, cheats, and SVP;
- `core/cd_hw/` Sega CD hardware and disc readers;
- `core/input_hw/` emulated input devices;
- `core/sound/` audio chips, resampling, Tremor, and minimp3;
- `core/ntsc/` optional NTSC filters.

The core exposes global state and relies on a frontend-provided `osd.h`, configuration
object, archive loader, BIOS paths, framebuffer storage, and error hook. The desktop
adapter must supply these host responsibilities while keeping all core access on one
controlled emulation thread.

`libretro/Makefile.common` is the most complete maintained inventory of core sources and
compile definitions. The desktop CMake target should derive an explicit target-scoped
list from it instead of moving upstream files.

### Existing frontends

`libretro/`, `gx/`, `sdl/`, `gcw0/`, and `psp2/` each provide their own `osd.h` and
host integration. They are peers of the new `desktop/` tree and must not become
dependencies of the Qt GUI. Select utility behavior may be adapted behind desktop-owned
interfaces when its license and invariants are preserved.

### Planned desktop boundary

New code will live under `desktop/` and will be split into core adapter, emulation
coordination, video, audio, input, persistence, library, settings, platform services,
and Qt UI targets. Tests will live under `tests/`. See `ARCHITECTURE.md` for ownership
and data flow.

## Core capabilities and compile-time options

The existing core supports SG-1000, Mark III/Master System, Game Gear, Genesis/Mega
Drive, and Sega/Mega CD. The current maintained desktop-relevant build configuration
uses RGB565 rendering and enables:

- a 32 MiB maximum cartridge buffer;
- CHD through bundled libchdr, LZMA, zlib, and zstd decoder sources;
- Ogg/Vorbis CD audio through bundled Tremor;
- Nuked YM2612/YM3438 and OPLL alternatives;
- Sega CD sub-CPU address-error handling;
- little-endian optimizations on supported hosts.

Raw/interleaved cartridge formats and ZIP/GZip loading are implemented by the current
frontends around `core/loadrom.c`. Sega CD handling includes CUE/BIN, ISO with supported
audio tracks, and CHD when `USE_LIBCHDR` is enabled. Format claims in the desktop UI
will be tied to the actual enabled build rather than inferred from filename alone.

## Dependency and licensing inventory

The repository-wide `LICENSE.txt` applies a non-commercial source-distribution license
to Genesis Plus GX and separately records several bundled component terms. This license
must remain authoritative for the combined emulator distribution; the desktop project
must not imply upstream endorsement or commercial permission.

| Component | Location / acquisition | License observation |
| --- | --- | --- |
| Genesis Plus GX | repository `core/` and existing frontends | Project-specific non-commercial license in `LICENSE.txt` |
| Qt 6 | system/deployment dependency | LGPL/GPL or commercial terms; deployment must satisfy the selected Qt terms |
| SDL3 | system/deployment dependency | zlib license |
| libchdr | `core/cd_hw/libchdr` | BSD-3-Clause, with separately licensed dependencies |
| zlib 1.3.1 | bundled under libchdr | zlib license |
| zstd 1.5.6 | bundled under libchdr | BSD-3-Clause |
| LZMA SDK 24.05 | bundled under libchdr | public-domain notice in bundled license |
| Tremor | `core/sound/tremor` | BSD-style Xiph license recorded in `LICENSE.txt` |
| minimp3 | `core/sound/minimp3` | CC0-1.0 |
| Nuked OPN2/OPLL cores | `core/sound` | LGPL-2.1-or-later notices recorded in source/project license |
| Blargg NTSC filters | `core/ntsc` | LGPL-2.1-or-later |

`THIRD_PARTY_NOTICES.md` will consolidate exact notices before distribution. SQLite is
available through Qt SQL without adding an ORM. No online metadata, firmware, ROM, or
artwork dependency is planned.

## Risks and controls

| Risk | Control |
| --- | --- |
| Core globals are not thread-safe | One adapter instance owned exclusively by the emulation thread |
| Frontend callbacks are implicit through `osd.h` | A desktop-owned C bridge with a narrow C++ adapter surface |
| Untrusted archive/disc/state inputs | Validate lengths and offsets at desktop boundaries; add bounded fuzz/property tests |
| Save/state writes can be interrupted | Per-game IDs, platform data paths, temporary-file plus atomic replacement |
| GUI events can outpace emulation | Bounded command, video, and audio structures; coalesce replaceable commands |
| Upstream merges can be noisy | Do not relocate core files; isolate adaptations and maintain regression hashes |
| Native GUI tests can be flaky | Injectable dialog services, stable object names, offscreen/Xvfb CI seams |
| Optional BIOS is proprietary | Never bundle or download it; validate user-selected files; optional external tests only |
| Cross-platform deployment licenses differ | Record every packaged library and its notice; keep package recipes explicit |

## Milestone 00 acceptance evidence

- Repository state and remotes were inspected before modification.
- Existing frontend/core boundaries and dependencies are recorded above.
- Baseline libretro success and SDL2 failure are reproducible and documented.
- `ARCHITECTURE.md` records the implementation architecture and ownership rules.
- `DEVELOPMENT_PLAN.md` tracks every requested milestone and its completion evidence.
- This milestone changes documentation only and does not alter emulator behavior.
