# Releases

Version tags drive the release pipeline. The project version has one authoritative
source: the `VERSION` field in the root `CMakeLists.txt`. A release tag must match that
value exactly as `vMAJOR.MINOR.PATCH`; the workflow rejects every mismatch before
installing dependencies or building packages.

## Safe release rehearsal

The Release workflow supports an explicitly non-publishing manual run:

```bash
gh workflow run release.yml --ref master -f release_tag=v0.1.0
```

This path performs the same version validation, native Release builds, complete tests,
Linux sanitizer suite, legacy libretro regression, package creation, runtime smoke
checks, checksum validation, and final asset assembly as a tagged release. It uploads a
`release-candidate-v0.1.0` workflow artifact but cannot execute the GitHub release step.

## Publishing a release

Before creating a tag:

1. Update the root CMake project version and `CHANGELOG.md`.
2. Complete the release-candidate checklist in `docs/FINAL_TEST_REPORT.md`.
3. Confirm the default branch and hosted CI are green and the worktree is clean.
4. Obtain project authorization to publish the intended version.
5. Create and push an annotated `vMAJOR.MINOR.PATCH` tag without rewriting any existing
   tag.

The tag push starts `.github/workflows/release.yml`. Publishing is structurally gated on
all package, sanitizer, and legacy jobs. The final job downloads the four native artifact
sets, requires all six platform archives and their individual CPack SHA-256 files,
recomputes every digest, writes `SHA256SUMS.txt`, and only then creates the GitHub release.

The workflow uses the scoped GitHub token with `contents: write` only in the final job.
It requires no repository signing, notarization, firmware, ROM, controller, or hardware
secrets. No release is created by ordinary branch pushes, pull requests, or manual
rehearsals.

## Published outputs

A complete version publishes:

- `Genesis-Plus-GX-GUI-<version>-windows-x86_64.zip`
- `Genesis-Plus-GX-GUI-<version>-linux-x86_64.tar.gz`
- `Genesis-Plus-GX-GUI-<version>-macos-arm64.zip`
- `Genesis-Plus-GX-GUI-<version>-macos-arm64.dmg`
- `Genesis-Plus-GX-GUI-<version>-macos-x86_64.zip`
- `Genesis-Plus-GX-GUI-<version>-macos-x86_64.dmg`
- one neighboring `.sha256` file per package and aggregate `SHA256SUMS.txt`

Verify a downloaded package on Linux with:

```bash
sha256sum -c Genesis-Plus-GX-GUI-0.1.0-linux-x86_64.tar.gz.sha256
```

Use `shasum -a 256 -c` on macOS. PowerShell users can compare
`Get-FileHash -Algorithm SHA256` with the corresponding checksum file.

macOS development packages are unsigned. Windows packages are portable ZIP files rather
than signed installers. See `docs/PACKAGING.md` for the signing, notarization, and
platform deployment model. Do not replace a published tag or release asset silently; fix
the issue, advance the version, and publish a new tag.
