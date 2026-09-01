# Upstream Maintenance

The desktop frontend is deliberately outside the authoritative Genesis Plus GX source
tree wherever practical. This keeps synchronization with
`ekeeke/Genesis-Plus-GX` reviewable and prevents GUI concerns from changing emulator
behavior.

## Configure and inspect the upstream remote

Check existing remotes first, then add the canonical project only if it is absent:

```bash
git remote -v
git remote add upstream https://github.com/ekeeke/Genesis-Plus-GX.git
git fetch upstream --tags --prune
```

Do not rename or overwrite a contributor's existing remote without agreement. Confirm
the branch advertised by `git remote show upstream`, then inspect divergence before
merging:

```bash
git log --oneline --left-right --cherry-pick master...upstream/master
git diff --stat master...upstream/master -- core LICENSE.txt Makefile.libretro
```

Review upstream release notes and commits, especially changes to global configuration,
frame/audio buffers, hardware detection, state serialization, CD APIs, file loaders,
input device enums, and third-party licenses.

## Synchronization workflow

Use a dedicated integration branch from the current clean default branch:

```bash
git switch master
git pull --ff-only origin master
git switch -c upstream-sync-YYYY-MM-DD
git merge --no-ff upstream/master
```

A merge commit preserves the published desktop milestone history and makes the imported
upstream boundary explicit. Do not rebase a published shared branch, force-push, or
resolve a conflict by wholesale replacing the frontend branch.

Resolve conflicts by responsibility:

- `core/`, inherited makefiles, `gx/`, `libretro/`, and upstream platform frontends:
  prefer the new upstream implementation unless a documented desktop compatibility
  adaptation is still required.
- `desktop/`, root target-based CMake, desktop tests, packaging, and desktop docs:
  retain the frontend implementation and adapt its narrow core-facing assumptions.
- `LICENSE.txt` and bundled dependency notices: preserve all upstream additions and
  update [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).
- Shared core source manifests in `desktop/core/CMakeLists.txt`: reconcile added,
  removed, or renamed core files explicitly; do not silently omit a new hardware module.
- The desktop-only in-process rollback helpers in `core/system.*` and `core/sound/*`:
  retain their opaque transient-context contract when upstream changes the 68000,
  viewport, filter, or blip-buffer layouts. They do not alter the portable Genesis Plus
  GX save-state format and are exercised by `integration.run_ahead`.

Keep fixes in separate commits after the upstream merge when possible. That makes it
clear whether a line came from upstream or from compatibility work.

## Adapter audit after a sync

Inspect these boundaries before trusting a successful compile:

1. Core globals and `config` layout/initial defaults.
2. `system_init`, `system_reset`, `system_frame`, audio sample production, and shutdown.
3. Bitmap pitch, viewport origin/size, interlace flags, and maximum framebuffer geometry.
4. Controller arrays, active-low button bits, analog coordinate/range semantics, and
   specialized-device identifiers.
5. SRAM, internal BRAM, RAM-cartridge sizes/maps, dirty-state observation, and disc tray
   operations.
6. State serialization size/format and failure semantics.
7. Cartridge/archive/disc formats, CUE parsing, CHD/libchdr API, and BIOS selection.
8. Core option enum/range changes exposed by Video, Audio, System, Input, BIOS, Cheats,
   or per-game settings pages.
9. Libretro shader support is entirely downstream of the copied framebuffer. Updating
   librashader or its C ABI belongs in `cmake/Librashader.cmake` and `desktop/video`;
   it must not be mixed into the inherited libretro frontend or emulator core.

Do not paper over an adapter failure by weakening a deterministic expected result.
Determine whether upstream intentionally corrected behavior, a frontend assumption is
stale, or the sync introduced a regression. Document any intentionally changed golden
hash with the upstream commit and technical reason.

## Required regression gates

At minimum run clean Debug, Release, and sanitizer desktop suites:

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug

cmake --preset release
cmake --build --preset release
ctest --preset release

cmake --preset asan
cmake --build --preset asan
ctest --preset asan
```

Also build and clean the inherited Unix libretro target:

```bash
make -f Makefile.libretro platform=unix -j4
make -f Makefile.libretro clean
```

Pay particular attention to generated-ROM RAM/input assertions, video/audio hashes,
state round trips, SRAM/BRAM unload/reload, Sega CD disc lifecycle, timing rates, the
20,000-frame stability workload, and repeated shutdown/core-lease tests. Run hosted
Windows and both macOS architectures before merging the integration branch.

## Completing the sync

Update the changelog, dependency notices, architecture documentation, fixture/golden
rationale, and any UI text affected by new or removed core capability. Inspect for
accidental binaries, firmware, ROMs, build outputs, and unrelated formatting. Merge the
reviewed integration branch through the normal project process and push without force.

If upstream cannot be incorporated without a significant core redesign, stop and record
the exact conflict rather than silently forking an emulation algorithm. The desktop core
adapter is expected to change more often than the authoritative emulator implementation.
