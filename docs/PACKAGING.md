# Packaging

Genesis Plus GX GUI uses the same CMake install graph for local installs, CI artifacts,
and tagged releases. Qt's deployment script copies the required Qt modules, platform
plugins and non-system runtime dependencies such as SDL3. Windows packaging adds
Microsoft's official Visual C++ x64 Redistributable installer. CPack then archives that
staged tree with one version and architecture name.

## Supported outputs

| Host | CPack output | Installed application |
| --- | --- | --- |
| Windows x64 | Portable ZIP | `bin/genesis-plus-gx-gui.exe` with Qt/SDL DLLs and `vc_redist.x64.exe` |
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
exist in the staged tree. Windows verification also requires Microsoft's official
Visual C++ x64 Redistributable installer.

Normal packages must also contain the platform librashader runtime, the original
`genplusgx-crt.slangp`/`.slang` files, and `librashader-MPL-2.0.md`. Verification
treats each as required. Custom user shader packs are never copied into or downloaded
by packaging.

## Platform notes

Windows packages are portable directories and do not modify the registry. The staged
archive includes `vc_redist.x64.exe` for users whose machine lacks the matching runtime;
hosted builds locate it from Visual Studio and pass its path as
`GENPLUSGX_WINDOWS_REDIST`. Local Windows packagers must set that CMake cache path or
the same environment variable before configuring. Qt deployment deliberately skips
unused D3D/DXC shader compilers because the frontend uses Qt Widgets and OpenGL.
macOS CI
produces unsigned development bundles; users may need to approve them in Privacy &
Security. Official distribution should sign the `.app`, enable hardened runtime, and
notarize the DMG with project-owned Apple credentials. CI never requires those secrets.

The Linux archive targets the Ubuntu version used by CI. It is relocatable and includes
Qt and SDL application libraries, but intentionally relies on base-system graphics,
window-system, C/C++ runtime, and libc libraries for ABI compatibility. Building on the
oldest supported Linux distribution gives the broadest compatibility.
The archive includes Qt's XCB EGL and GLX integration plugins. Runtime rendering also
preflights a context and surface before creating the accelerated display, automatically
retaining the complete software-rendered shell if host OpenGL is unavailable.

Packages contain the root license/notices plus the complete user, BIOS, input, save,
build, test, packaging, and maintenance guides under the platform documentation
directory. They contain no games, firmware, save data, or downloaded artwork.

The repository license requires source availability for modified redistribution. Keep
an official binary release attached to its matching immutable tag and the source archive
GitHub exposes for that tag; do not mirror a binary without the corresponding complete
project source and applicable dependency source/license obligations. Qt and SDL remain
dynamically deployed and replaceable, with their exact versions and official source/
license references recorded in `THIRD_PARTY_NOTICES.md`.

See `docs/RELEASES.md` for tag validation, the non-publishing workflow rehearsal, final
checksum assembly, and authorized GitHub release publication.
