# Signed application updates

Genesis Plus GX GUI can discover official project releases without trusting unsigned
GitHub metadata. Open **Help → Check for Updates…** for a manual check. Automatic
startup checks are disabled by default; they can be explicitly enabled in the same
window and are attempted at most once every 24 hours, including after a failed check.

The application does not silently install, execute, or replace anything. A successful
check enables **Download Verified Package** for the exact operating system and CPU
architecture. After the package has been downloaded and verified, **Open Download**
hands the file to the operating system so the user remains in control of installation.

## Trust and verification

Every release publishes canonical `update-manifest.json` bytes and a detached
`update-manifest.json.sig`. The application verifies the Ed25519 signature with a
project public key compiled into the executable before parsing any JSON. It then
requires:

- schema version 1 and the expected signing-key identity;
- a canonical `MAJOR.MINOR.PATCH` release and matching project release URL;
- only approved HTTPS GitHub release hosts and a maximum of five redirects;
- one unambiguous platform/architecture/package-format selection;
- safe package filenames, declared sizes from 1 byte through 2 GiB, and lowercase
  SHA-256 digests; and
- a version no older than the highest successfully verified version recorded locally.

Manifest and signature responses have small fixed bounds. Package bytes stream into a
bounded atomic `QSaveFile` under the application cache instead of being accumulated in
memory. A size mismatch, digest mismatch, TLS error, untrusted redirect, cancellation,
or write failure cancels the temporary file; it is never offered to the user.

The pinned production key identifier is `704e04b184a939a4`. The public PEM is retained
in `desktop/resources/update-signing-public-key.pem`; no private release key exists in
the repository or a package. Monocypher 4.0.2 performs application-side Ed25519
verification. This authenticates the project release manifest but is distinct from
Windows Authenticode and Apple application signing/notarization; current development
packages may still display the platform's unsigned-application warning.

## Privacy, storage, and recovery

An update check contacts only the project's public GitHub release URLs. It sends normal
HTTPS request metadata and a generic application user-agent; it sends no game hashes,
ROM or BIOS data, filenames, library contents, credentials, or personal paths.
Configuration is stored atomically in `config/signed-updates.json`. Verified packages
are stored under `cache/updates/` and may be deleted after installation.

If a check fails, continue using the installed version and retry later. Signature,
rollback, size, and hash errors are intentionally fail-closed. Do not work around them
by renaming an unrelated download. Users may instead visit the release page, compare
the published checksum, and report the exact privacy-filtered diagnostics.

## Maintainer signing and key continuity

The release workflow reads the base64-encoded PKCS#8 Ed25519 private PEM only from the
protected `UPDATE_SIGNING_PRIVATE_KEY_B64` repository secret in its final assembly job.
It writes the key to the runner's temporary directory with owner-only permissions,
creates the manifest from the six already verified package artifacts, signs the exact
canonical bytes, verifies the signature with the committed public PEM, checks every
local asset again, and destroys temporary key material. A missing secret is fatal even
during a release rehearsal. Never print, upload, cache, or commit that secret.

Key rotation must preserve an authenticated bridge: first ship a release signed by the
current key whose application trusts both the current and next public keys; only after
that release is broadly available may a later manifest switch to the next key ID. A
suspected private-key compromise requires suspending updater publication, rotating the
repository secret, shipping a reviewed trust update through normal platform download
channels, and documenting the incident. Never replace a published manifest or package
in place; advance the version and publish immutable new assets.
