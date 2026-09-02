# Third-Party Notices

This file is an inventory, not a replacement for license texts. Genesis Plus GX GUI
combines an externally installed Qt/SDL desktop layer with third-party code retained in
the upstream emulator source tree. Copyright notices and license terms remain with each
component. The complete inherited notice collection is in [LICENSE.txt](LICENSE.txt),
and bundled libraries also retain their local license files.

## Project and runtime components

| Component | Use | Version/source in this tree | License |
| --- | --- | --- | --- |
| Genesis Plus GX | Authoritative Sega emulator core and modified desktop source tree | Upstream-derived `core/`; copyright Charles MacDonald, Eke-Eke, and contributors | Repository-specific non-commercial license in `LICENSE.txt` |
| [Qt 6](https://doc.qt.io/qt-6/licensing.html) | Core, GUI, Widgets, Network, OpenGLWidgets, SQL/SQLite, Test framework, and Linguist catalog tools | Minimum 6.8; hosted builds use 6.8.3; dynamically deployed desktop modules/plugins | GNU LGPL v3 or GPL v3 open-source editions, or a separately purchased commercial license, as offered by The Qt Company |
| [SDL 3](https://wiki.libsdl.org/SDL3/FrontPage) | Controller discovery/input and host audio | Minimum 3.2; hosted builds use 3.4.14; dynamically deployed | zlib license |
| [librashader](https://github.com/SnowflakePowered/librashader) | Modern Libretro Slang preset parsing, compilation, and OpenGL filter-chain runtime | Source archive `librashader-v0.12.0`, SHA-256 `4bf8cf2489d00848dcabbf2163204093776082da4217d5a5db45e4cbf335cedf`; OpenGL-only C API runtime built and dynamically loaded | Runtime implementation: Mozilla Public License 2.0; C headers: MIT |
| [rcheevos](https://github.com/RetroAchievements/rcheevos) | Official RetroAchievements runtime, game hashing, and `rc_client` service protocol | Source archive `v12.4.0`, SHA-256 `7fb1a43b8edfe727219d054ed868cc985bca54f331f9c2410f818dd3143df5d3`; built statically when achievements are enabled | MIT |
| [QtKeychain](https://github.com/frankosterfeld/qtkeychain) | Native secure storage for the RetroAchievements session token and WebDAV password | Source archive `0.17.0`, SHA-256 `3b85c3929034b0a99da777130c34d99f006fcd3a9d56564159399a33fee0e504`; selected platform backend built statically with plaintext fallback disabled | BSD 3-Clause |
| [Monocypher](https://monocypher.org/) | Ed25519 verification of canonical application-update manifests | Source release `4.0.2`, SHA-256 `38d07179738c0c90677dba3ceb7a7b8496bcfea758ba1a53e803fed30ae0879c`; standard Ed25519 implementation built statically | BSD 2-Clause or CC0 1.0 dual choice; packaged license text retains both alternatives |
| SQLite | Game-library database via Qt's QSQLITE driver | Supplied by the selected Qt deployment | Public domain; Qt driver code retains Qt's license |
| Microsoft Visual C++ Redistributable | Runtime installer for Windows x64 packages | Official `vc_redist.x64.exe` supplied by the Visual Studio 2022 hosted toolchain | Microsoft software license terms distributed with the installer |

Users redistributing binaries must satisfy both the repository's non-commercial terms
and the terms of the exact Qt/SDL packages they redistribute. Official packages keep Qt
and SDL dynamically replaceable and include source-distribution references; a distributor
is responsible for supplying all notices/source offers required by its chosen Qt edition.

## Optional online data services

| Service/data | Use | Distribution and license handling |
| --- | --- | --- |
| [Retronian GameDB](https://gamedb.retronian.com/) | Optional Genesis / Mega Drive library metadata matched by SHA-256 | Retronian publishes its game database under CC BY-SA 4.0. The application downloads data only after user opt-in and retains provider, creator, license, and source attribution. No Retronian database or artwork is bundled in release packages. |

Retronian metadata can contain links to Libretro Thumbnails repositories. Those image
records do not independently declare an asset license in the provider response, so the
application rejects them. A custom Licensed Manifest provider is likewise accepted only
when metadata and each downloaded image identify an approved Creative Commons or
public-domain license and attribution. Provider data is cached for personal frontend
use and is not incorporated into this repository or its packages.

## Bundled emulator dependencies

| Component | Use | Bundled location/version | License |
| --- | --- | --- | --- |
| libchdr | CHD v1-v5 disc-image reader | `core/cd_hw/libchdr`, project version 0.2 | BSD 3-Clause; see `core/cd_hw/libchdr/LICENSE.txt` |
| zlib | DEFLATE decoding used by libchdr and the ZIP cartridge browser | `core/cd_hw/libchdr/deps/zlib-1.3.1` | zlib license |
| MiniZip | Bounded ZIP directory reading and cartridge extraction | `core/cd_hw/libchdr/deps/zlib-1.3.1/contrib/minizip`; Gilles Vollant and contributors | zlib license retained in the bundled sources |
| zstd | Zstandard decoding used by libchdr | `core/cd_hw/libchdr/deps/zstd-1.5.6` | BSD license selected for this distribution; upstream also supplies GPLv2 terms |
| LZMA SDK | LZMA decoding used by libchdr | `core/cd_hw/libchdr/deps/lzma-24.05` | Public domain |
| dr_flac | FLAC decoding used by libchdr | `core/cd_hw/libchdr/include/dr_libs/dr_flac.h`, 0.12.42 | Public-domain or MIT-0 dual choice |
| Tremor | Integer Ogg/Vorbis decoding for CD audio | `core/sound/tremor` | BSD 3-Clause-style Xiph.org license |
| minimp3 | MP3 decoding for CD audio | `core/sound/minimp3` | CC0 1.0 Universal |
| Nuked OPN2 | YM2612/YM3438-compatible FM synthesis option | Genesis Plus GX sound core | GNU LGPL 2.1 or later |
| Genesis Plus GX NTSC filter | Optional analog-video filter implementation | `core/ntsc` | Local terms in `core/ntsc/license.txt` |

The Genesis Plus GX source contains additional processor, sound, platform, and historical
contributions covered by the notices collected in `LICENSE.txt` and their source headers.
Those notices remain authoritative if this summary differs.

## Build and automation tools

CMake, Ninja, platform compilers/SDKs, GitHub Actions, `actionlint`, Qt deployment tools,
and CPack create or validate the software but are not incorporated as application source.
Workflow actions are pinned to immutable revisions in `.github/workflows/` and retain
their own repository licenses. Platform packages also rely on operating-system libraries
that qualify under their own system distribution terms.

## Assets and firmware

The application icon and generated test programs/images are original project material.
Generated binary fixtures are dedicated to CC0 1.0 as documented in
[tests/fixtures/README.md](tests/fixtures/README.md). No commercial game, proprietary
Sega BIOS, scraped artwork, or automatically downloaded unlicensed asset is distributed.
