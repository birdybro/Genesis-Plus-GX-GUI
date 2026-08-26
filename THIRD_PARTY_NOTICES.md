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
| [Qt 6](https://doc.qt.io/qt-6/licensing.html) | Core, GUI, Widgets, OpenGLWidgets, SQL/SQLite, and Test framework | Minimum 6.5; hosted builds use 6.8.3; dynamically deployed desktop modules/plugins | GNU LGPL v3 or GPL v3 open-source editions, or a separately purchased commercial license, as offered by The Qt Company |
| [SDL 3](https://wiki.libsdl.org/SDL3/FrontPage) | Controller discovery/input and host audio | Minimum 3.2; hosted builds use 3.4.14; dynamically deployed | zlib license |
| SQLite | Game-library database via Qt's QSQLITE driver | Supplied by the selected Qt deployment | Public domain; Qt driver code retains Qt's license |

Users redistributing binaries must satisfy both the repository's non-commercial terms
and the terms of the exact Qt/SDL packages they redistribute. Official packages keep Qt
and SDL dynamically replaceable and include source-distribution references; a distributor
is responsible for supplying all notices/source offers required by its chosen Qt edition.

## Bundled emulator dependencies

| Component | Use | Bundled location/version | License |
| --- | --- | --- | --- |
| libchdr | CHD v1-v5 disc-image reader | `core/cd_hw/libchdr`, project version 0.2 | BSD 3-Clause; see `core/cd_hw/libchdr/LICENSE.txt` |
| zlib | DEFLATE decoding used by libchdr | `core/cd_hw/libchdr/deps/zlib-1.3.1` | zlib license |
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
Sega BIOS, scraped artwork, or automatically downloaded copyrighted asset is distributed.
