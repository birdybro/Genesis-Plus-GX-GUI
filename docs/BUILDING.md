# Building

The supported desktop build uses CMake, Ninja, C++20, Qt 6, and SDL3. It coexists with
the inherited platform makefiles; configuring the root project does not replace or
regenerate those legacy builds.

## Requirements

- CMake 3.25 or newer
- Ninja
- Git (for embedded commit diagnostics; source archives fall back to `unknown`)
- A C++20 compiler: GCC/Clang, Visual Studio 2022, or current Apple Clang
- Qt 6.5 or newer with Core, Gui, Widgets, OpenGLWidgets, Sql, and Test
- SDL 3.2 or newer with its CMake config package
- Platform OpenGL/window-system development files on Linux

Hosted CI and release builds pin Qt 6.8.3 and SDL 3.4.14. A newer compatible local Qt
or SDL is acceptable; the exact detected versions are printed during configure and in
the application's Diagnostics dialog.

CI also configures `GENPLUSGX_WARNINGS_AS_ERRORS=ON`. This applies `/WX` or `-Werror`
only to newly authored desktop/frontend and test targets; inherited emulator and bundled
decoder sources retain a separate, target-local warning policy for upstream mergeability.

## Dependency discovery

Qt and SDL must expose `Qt6Config.cmake` and `SDL3Config.cmake`. If they are outside
normal prefixes, provide a semicolon-separated `CMAKE_PREFIX_PATH`:

```bash
cmake --preset debug \
  -DCMAKE_PREFIX_PATH="/opt/Qt/6.8.3/gcc_64;/opt/SDL3"
```

Alternatively set `Qt6_DIR` and `SDL3_DIR` to the directories containing those config
files. Keep architecture and compiler families consistent: for example, an MSVC build
cannot link a MinGW Qt package, and an arm64 application cannot link x86-64 SDL.

The CHD, zlib, zstd, LZMA, FLAC, Tremor, and minimp3 decoder sources needed by the core
are bundled. CMake does not download dependencies or ROM/BIOS material during configure.

## Linux

Install a current compiler, CMake, Ninja, and the graphics development packages supplied
by the distribution. On Ubuntu 24.04 the native prerequisites used by CI begin with:

```bash
sudo apt-get update
sudo apt-get install ninja-build g++ libgl1-mesa-dev libegl1-mesa-dev
```

Install Qt 6.5+ and SDL3 through their official packages, a suitable distribution
package, or locally built prefixes, then set `CMAKE_PREFIX_PATH` as above. Some stable
Linux distributions do not yet package SDL3; do not substitute SDL2.

## Windows

Install Visual Studio 2022 with Desktop development with C++, CMake, Ninja, the Qt 6
MSVC 2022 x64 package, and SDL3 built/installed for MSVC x64. Use a Developer PowerShell
or command prompt and point CMake at both prefixes:

```powershell
cmake --preset debug `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64;C:/deps/SDL3"
cmake --build --preset debug
ctest --preset debug
```

The CI build adds SDL's DLL directory to `PATH` while testing. A local build must do the
same before launching directly from the build tree, or copy/deploy the runtime through
the install/package target.

## macOS

Install Xcode command-line tools, CMake, Ninja, a host-native Qt 6 package, and host-native
SDL3. Select prefixes for the architecture being built:

```bash
cmake --preset debug \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.8.3/macos;/opt/sdl3"
cmake --build --preset debug
ctest --preset debug
```

CI builds natively on Apple Silicon and Intel runners. Universal binaries are not
currently produced; each package carries one explicit architecture. The macOS package
step creates an unsigned `.app`, ZIP, and DMG.

## Presets and options

The normal workflow is:

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Available configure/build/test presets are `debug`, `release`, `asan`, and `ci`.
`asan` combines AddressSanitizer and UndefinedBehaviorSanitizer and is unsupported by the
Windows preset condition. All presets build into ignored `build/<preset>/` directories.

Useful cache options are:

| Option | Default | Effect |
| --- | --- | --- |
| `BUILD_TESTING` | `ON` through presets | Register unit/core/integration/GUI tests |
| `GENPLUSGX_BUILD_DESKTOP` | `ON` | Build and package the Qt application |
| `GENPLUSGX_ENABLE_SANITIZERS` | `OFF` | Enable ASan and UBSan on supported compilers |
| `GENPLUSGX_ENABLE_CHD` | `ON` | Build bundled libchdr CHD decoding |
| `GENPLUSGX_ENABLE_TREMOR` | `ON` | Build integer Ogg/Vorbis CD-audio decoding |
| `GENPLUSGX_WINDOWS_REDIST` | empty | Official `vc_redist.x64.exe` included in Windows packages |
| `GENPLUSGX_ENABLE_EXTERNAL_FIXTURE_TESTS` | `OFF` | Register user-owned BIOS fixture tests |

Do not enable external fixture tests in public CI. See [BIOS.md](BIOS.md) for their
environment contract.

## Running

The executable is `build/debug/desktop/app/genesis-plus-gx-gui` on single-configuration
Ninja hosts (with `.exe` on Windows). Run `--help` to verify library discovery before
opening a game:

```bash
build/debug/desktop/app/genesis-plus-gx-gui --help
```

For a headless smoke check, set `QT_QPA_PLATFORM=offscreen`; audio tests and hosted CI
also select SDL's dummy audio driver. Normal interactive use should not set either value.

## Installing and packaging

Stage the Release install graph and build native packages with:

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
cmake --install build/release --prefix build/package-root
cpack --preset release
```

Packages and SHA-256 files appear under `build/packages/`. Deployment and platform
contents are described in [PACKAGING.md](PACKAGING.md).

## Troubleshooting

- **Qt6 or SDL3 not found:** inspect the configure output, confirm matching architecture
  and compiler packages, and set `CMAKE_PREFIX_PATH` rather than copying headers/libs.
- **Qt platform plugin cannot initialize:** launch from a staged package or ensure the
  matching Qt plugin directory is discoverable; do not mix Qt installations.
- **SDL runtime missing on Windows:** add the built SDL `bin` directory to `PATH` or use
  `cmake --install`/CPack, which deploys it.
- **No audio device in a headless test:** use `SDL_AUDIODRIVER=dummy`; interactive builds
  report real device failures in the UI and frontend log.
- **OpenGL unavailable in a virtual machine:** the application has a deterministic
  software display path for tests, but interactive acceleration needs a functioning host
  OpenGL stack.

Testing details are in [TESTING.md](TESTING.md). Report new reproducible build problems
with configure output, compiler/Qt/SDL versions, OS/architecture, and the failed command.
