# Packaging

Genesis Plus GX GUI uses the same CMake install graph for local installs, CI artifacts,
and tagged releases. Qt's deployment script copies the required Qt modules, platform
plugins, compiler runtime where applicable, and non-system runtime dependencies such as
SDL3. CPack then archives that staged tree with one version and architecture name.

## Supported outputs

| Host | CPack output | Installed application |
| --- | --- | --- |
| Windows x64 | Portable ZIP | `bin/genesis-plus-gx-gui.exe` with Qt/SDL DLLs |
| Linux x86-64 | Portable `.tar.gz` | `bin/genesis-plus-gx-gui` with private libraries/plugins |
| macOS Apple Silicon | ZIP and unsigned DMG | `genesis-plus-gx-gui.app` |
| macOS Intel | ZIP and unsigned DMG | `genesis-plus-gx-gui.app` |

Artifact names have the form
`Genesis-Plus-GX-GUI-<version>-<platform>-<architecture>`. CPack writes a neighboring
SHA-256 file for every archive or disk image.

## Local package build

Configure and test the Release tree before packaging:

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
cpack --preset release
```

The packages are written to `build/packages/`. The preset uses the platform defaults:
ZIP on Windows, `.tar.gz` on Linux, and both ZIP and DMG on macOS. A single generator can
also be selected directly, for example:

```bash
cpack --config build/release/CPackConfig.cmake -G TGZ -B build/packages
```

To inspect the self-contained install tree before archiving:

```bash
cmake --install build/release --prefix build/package-root
cmake -DPACKAGE_ROOT="$PWD/build/package-root" \
  -DVERIFY_PLATFORM=linux -P cmake/VerifyPackage.cmake
build/package-root/bin/genesis-plus-gx-gui --version
```

Use `windows` or `macos` for `VERIFY_PLATFORM` on those hosts. Verification requires the
application executable, Qt Core runtime, SDL3 runtime, and native Qt platform plugin to
exist in the staged tree.

## Platform notes

Windows packages are portable directories and do not modify the registry. macOS CI
produces unsigned development bundles; users may need to approve them in Privacy &
Security. Official distribution should sign the `.app`, enable hardened runtime, and
notarize the DMG with project-owned Apple credentials. CI never requires those secrets.

The Linux archive targets the Ubuntu version used by CI. It is relocatable and includes
Qt and SDL application libraries, but intentionally relies on base-system graphics,
window-system, C/C++ runtime, and libc libraries for ABI compatibility. Building on the
oldest supported Linux distribution gives the broadest compatibility.

Packages contain no games, firmware, save data, or downloaded artwork.

See `docs/RELEASES.md` for tag validation, the non-publishing workflow rehearsal, final
checksum assembly, and authorized GitHub release publication.
