# Requirements Audit

This matrix traces every numbered section of the standalone-desktop specification to
production code, automated evidence, or an explicit scope decision. It is a closure
index, not a substitute for the detailed architecture, user guides, milestone ledger,
or final test report.

| Section | Status | Implementation and evidence |
| ---: | --- | --- |
| 1. Primary objective | Implemented | The Qt desktop target runs the separated authoritative core for every listed system; see [ARCHITECTURE.md](ARCHITECTURE.md) and the generated-system tests in [TEST_MATRIX.md](TEST_MATRIX.md). |
| 2. Autonomous operation | Complete | The implementation, hardening passes, gates, revisions, and decisions are recorded as independently committed work in [DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md). |
| 3. Git rules | Complete | The milestone ledger records the branch, gates, and commit identities; history was appended without force-push or removal of legacy work. |
| 4. Project architecture | Implemented | C++20, CMake, Qt 6 Widgets/OpenGL, SDL3, SQLite, typed services, and directory ownership are documented in [ARCHITECTURE.md](ARCHITECTURE.md). |
| 5. Core separation | Implemented | `genplusgx_core`, the desktop C bridge, and `CoreAdapter` keep Qt/SDL out of the core target and enforce one process-wide owner context. |
| 6. Emulation thread | Implemented | `EmulationWorker` owns all core access and uses bounded commands/events for lifecycle, settings, input, state, disc, and frame operations. |
| 7. Video | Implemented | `DisplayWidget`, `VideoFrameExchange`, and geometry/settings tests cover dynamic viewports, DPI, resize, aspect, integer scaling, filtering, overscan, Game Gear, PAL/NTSC, interlace, fullscreen, and native capture. |
| 8. Audio | Implemented | SDL3 stereo output, the bounded ring, device/latency reconfiguration, gain/mute, reconnect handling, metrics, and every exposed core mixer/chip option are tested. |
| 9. Timing | Implemented | Rational `steady_clock` pacing covers NTSC, PAL, Sega CD, pause, frame advance, bounded catch-up, configurable 25–1600% slow/normal/fast rates, and focus-safe mode changes without a GUI timer or busy loop. |
| 10. Game loading | Implemented | Native Open, one-file drag/drop, command-line loading, recent history, replacement, typed validation, and conditional CHD are covered. ZIP is not advertised because this build has no archive enumerator. |
| 11. Sega CD | Implemented | Regional BIOS, CUE/BIN/ISO/conditional CHD, CDDA, eject/change, BRAM, RAM cartridge, metadata, and optional external-firmware testing are documented in [BIOS.md](BIOS.md). |
| 12. BIOS manager | Implemented | Eight firmware slots expose path, existence, detected type/region, SHA-256, validation, and local selection without downloading firmware. |
| 13. Save RAM | Implemented | Platform paths, collision-resistant identities, atomic per-game SRAM/internal-BRAM/RAM-cartridge writes, automatic load/flush, and corruption guards are tested. |
| 14. Save states | Implemented | Slots 0–9, save/load/delete, timestamp/frame metadata, shortcuts, wrong-game/core/corruption rejection, atomic replacement, and deterministic restoration are covered; thumbnails and raw import/export remain optional. |
| 15. Input configuration | Implemented | Keyboard and SDL3 hot-plug controllers support eight assignments, remapping, axes, deadzones, profiles/defaults, standard pads, multitaps, and advanced core devices. |
| 16. Hotkeys | Implemented | All requested actions, including independent fast-forward and slow-motion hold/toggle controls plus slots, are capture-configurable and conflict-checked against gameplay bindings. |
| 17. Main GUI | Implemented | Conventional menus/status fields, stable object names, action gating, dialogs, accelerators, and semantic Qt tests are present. |
| 18. Settings dialog | Implemented | The unified General/Video/Audio/Input/System/BIOS/Paths/Advanced center routes to versioned transactional editors with Apply/OK/Cancel/default behavior. |
| 19. Per-game settings | Implemented | Sparse identity-keyed overrides support global inheritance and video/audio/system/input/BIOS precedence without writing empty override files. |
| 20. Game information | Implemented | The bounded background parser displays header titles, system/region, sizes, checksums, path, mapper/media, Sega CD details, and the shared SHA-256 identifier. |
| 21. Game library | Implemented | An asynchronous recoverable SQLite library supports roots, recursion, search, system/region filters, favorites, play history, sorting, local art, information, and launch. Validated CUE payload tracks are suppressed as duplicate rows. |
| 22. Cheats | Implemented | System-aware Genesis and 8-bit Game Genie/Action Replay grammars, enable/disable, descriptions, atomic per-game persistence, validation, and core-thread application are covered. |
| 23. Screenshots | Implemented | Native RGB565 frames are asynchronously written as collision-safe timestamped PNG files to a configurable directory with visible completion/failure. |
| 24. Themes/accessibility | Implemented | System/light/dark themes, pre-application high-DPI policy, native Qt accessibility, labels/buddies, focus order, and keyboard navigation are tested. |
| 25. Command line | Implemented | Positional game loading, `--fullscreen`, `--help`, `--version`, diagnostics, and process exit behavior are unit/smoke tested. |
| 26. Logging/diagnostics | Implemented | Rotating structured logs and a privacy-filtered copyable diagnostics dialog cover versions, OS, renderer, audio, controllers, game/system/region, BIOS, timing, and errors. |
| 27. Legal/licensing | Implemented | Upstream terms are preserved, [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) inventories dependencies, and all tests use generated CC0 inputs without ROM/BIOS/art downloads. |
| 28. Build system | Implemented | Target-based root CMake, presets, preserved legacy builds, pinned CI dependencies, `genplusgx_core`, desktop, tests, install, and CPack targets are present. |
| 29. Compiler gates | Implemented | Frontend targets use GCC/Clang `-Wall -Wextra -Wpedantic` or MSVC warning levels without imposing `-Werror` on inherited core code. |
| 30. Sanitizers | Implemented | The ASan/UBSan preset and Linux hosted job run the complete deterministic suite with leak detection. |
| 31. Testing philosophy | Implemented | CTest layers infrastructure, core, integration, unit, GUI, process smoke, property, stress, and platform gates. |
| 32. Unit tests | Implemented | Configuration, paths/identity, RAM/state, library/metadata, CLI, input/hotkeys, geometry, audio, screenshots, cheats, BIOS, queues, and corruption are covered. |
| 33. Core tests | Implemented | Runtime-generated legal 68000, Z80, SRAM, controller, audio/video, disc, and firmware programs exercise semantic core behavior. |
| 34. Core regression | Implemented | Deterministic framebuffer/audio hashes, memory markers, state round trips, SRAM, firmware activation, and hardware geometry are fixed assertions; no golden is auto-regenerated. |
| 35. GUI tests | Implemented | Qt tests exercise real menu/action, loading, emulation, state, settings, video/audio, input, library, information, BIOS, cheat, diagnostics, and shutdown semantics. |
| 36. Headless GUI | Implemented | Injected dialog seams, Qt offscreen/software rendering, dummy SDL audio, and process hooks make every GUI workflow unattended in CI. |
| 37. Visual regression | Implemented as specified | Cross-platform tests assert presentation semantics and deterministic core pixels rather than brittle native window-chrome goldens. |
| 38. Integration tests | Implemented | Generated games cross adapter, worker, video/audio/input, persistence, state, per-game, and GUI workflow boundaries. |
| 39. Stability test | Implemented | The accelerated 20,000-frame test checks capacities, queue saturation, resource stability, timing metrics, and repeated lifecycle. |
| 40. Fuzz/property tests | Implemented | Fixed-seed bounded corpora exercise CUE, metadata, state, cheat, and configuration boundaries under normal CI. |
| 41. Fixtures | Implemented | [tests/fixtures/README.md](../tests/fixtures/README.md) records generation, purpose, expected behavior, and CC0 provenance. |
| 42. GitHub Actions | Implemented | [ci.yml](../.github/workflows/ci.yml) covers Debug/Release/tests/packages on Ubuntu, Windows, arm64 macOS, Intel macOS, plus Linux sanitizers and legacy libretro. |
| 43. Build artifacts | Implemented | CI emits a Windows portable ZIP, Linux relocatable TGZ, and unsigned macOS ZIP/DMG for both architectures. |
| 44. Packaging | Implemented | One CMake version drives About/executable/package names; install trees are runtime-verified and CPack writes neighboring SHA-256 files. |
| 45. Release automation | Implemented | [release.yml](../.github/workflows/release.yml) validates matching tags, retests, packages, verifies checksums, and publishes only on an authorized tag; dispatch is rehearsal-only. |
| 46. Documentation | Implemented | Every requested document plus focused guides is linked from [README.md](../README.md), locally link-checked, installed, and packaged. |
| 47. Architecture documentation | Implemented | [ARCHITECTURE.md](ARCHITECTURE.md) diagrams command, video, audio, input, persistence, ownership, threading, and shutdown flows. |
| 48. Upstream maintenance | Implemented | [UPSTREAM_MAINTENANCE.md](UPSTREAM_MAINTENANCE.md) documents the remote, safe synchronization, boundary conflicts, and regression gates without moving the upstream tree. |
| 49. Performance | Implemented | Frame/audio/video/command storage is bounded, setting/input commands coalesce, pacing and buffer metrics are exposed, logging is not per-frame, and every large identity stream is cooperatively cancellable. |
| 50. Error handling | Implemented | Normal invalid content, BIOS, persistence/state, audio, rendering, library, and service failures have typed diagnostics plus concise GUI paths. |
| 51. Clean shutdown | Implemented | Producer disconnection, cancellation of live-backup/state/metadata/library hashes, core/save flush, audio/controllers, auxiliary worker joins, display release, and aggregate process status are ordered and tested. |
| 52. Scope exclusions | Observed with later debug-tool amendment | Netplay, achievements, cloud, downloading/scraping, commercial databases, TAS, streaming, instruction-level hooks, external debugger servers, and core rewrites were not introduced. The later explicit request added a bounded native inspection workspace without changing CPU algorithms. |
| 53. Milestones | Complete | Granular milestone records include status, goal, files, tests, acceptance criteria, and commit evidence in [DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md). |
| 54. Definition of done | Satisfied | The product feature checklist is in [FINAL_TEST_REPORT.md](FINAL_TEST_REPORT.md); composite-disc identity, library duplication, and hash-shutdown findings are closed and the exact implementation passed every local and hosted gate. |
| 55. Adversarial review | Repeated | Unfinished markers, disabled/skipped tests, queue growth, action wiring, path assumptions, artifacts, licenses, CI exclusions, runtime propagation, and shutdown were re-audited. |
| 56. Final test report | Complete | [FINAL_TEST_REPORT.md](FINAL_TEST_REPORT.md) records the exact hardened implementation after clean Debug/Release/sanitizer/package gates and its successful ten-job hosted CI run. |
| 57. Final README | Implemented | [README.md](../README.md) covers product identity, systems/features, all platform installs, source builds, input/BIOS/saves/tests, licensing, upstream relationship, and screenshot policy. |
| 58. Final response | Deferred correctly | The concise branch/SHA/commit/platform/test/CI/package/feature/limitation/push report is issued only after the closure gate succeeds. |

## Explicit limitations

The Linux artifact is a relocatable TGZ rather than an AppImage. macOS development
artifacts are unsigned; signing and notarization require project credentials. ZIP is
not claimed by the raw desktop loader. Real proprietary Sega CD firmware and physical
controller/audio hardware remain opt-in/manual smoke inputs; CI uses generated legal
fixtures and injected SDL events. These are capability or credential boundaries, not
inert advertised controls.
