# Cloud Synchronization

Genesis Plus GX GUI can synchronize its per-game save RAM and wrapped save-state files
through a user-provided HTTPS WebDAV account. The feature is opt-in and disabled by
default. Emulation, local saves, and local states continue to work without an account or
network connection.

## Configure an account

1. Close the current game. This prevents a cloud transfer from racing the core's live
   persistence session.
2. Open **Tools → Cloud Synchronization…**.
3. Enable synchronization and enter the HTTPS WebDAV collection URL supplied by your
   provider, your username, and a simple remote directory name. A provider-specific app
   password is preferable to an account's primary password.
4. Select save RAM, save states, or both. At least one category must remain selected.
5. Choose **Remember Password** to put the password in the operating system's native
   credential store, or leave it unsaved and enter it for each manual transfer.
6. Choose **Synchronize Now**.

The application accepts only `https://` endpoints. Credentials embedded in URLs,
redirects, invalid TLS certificates, URL queries/fragments, and ambiguous Basic-auth
usernames are rejected. Passwords never enter `cloud-sync.json`, frontend logs, the
remote manifest, or diagnostic reports. QtKeychain uses Windows Credential Manager,
macOS Keychain, or a Secret Service-compatible Linux store with plaintext fallback
disabled. If no secure store is available, manual one-time passwords still work.

## Selected data

Only these application-owned files can enter the synchronization manifest:

```text
saves/<game-id>/cartridge.srm
saves/<game-id>/scd-internal.brm
saves/<game-id>/scd-cartridge.brm
states/<game-id>/slot-0.gpgxstate ... slot-9.gpgxstate
states/<game-id>/resume.gpgxstate
```

ROMs, disc images, BIOS files, cheats, settings, logs, screenshots, recordings, library
metadata, artwork, and arbitrary files are never selected. Directory symlinks are not
followed. The existing SHA-256 game identity keeps unrelated games in separate paths.

Cloud data is protected in transit by the WebDAV server's validated TLS certificate.
The application does not add client-side encryption to save/state contents, so the
provider can store and inspect those files under its normal storage policy. Use a
provider and account appropriate for that privacy model.

## Conflict and deletion behavior

The remote manifest records each selected path, byte size, and SHA-256. File bodies use
immutable content-addressed objects. Manifest writes use the server's entity tag and an
`If-Match` or create-only precondition, following the lost-update protections described
by [RFC 4918](https://www.rfc-editor.org/rfc/rfc4918) and
[RFC 9110](https://www.rfc-editor.org/rfc/rfc9110). If another client wins the update,
the current synchronization stops and asks you to run it again.

The first synchronization uploads a local-only file and downloads a remote-only file.
If different local and remote versions already exist, or both change after a previous
sync, the local file remains authoritative and unchanged. The remote version is written
atomically to:

```text
cloud/conflicts/<category>/<game-id>/<name>.remote-<UTC>-<hash>.<extension>
```

The results table shows that path. Compare or load the conflict copy deliberately; the
application never guesses which progress to discard. Deletions are also non-destructive:
a local deletion is restored from the remote, and a remote deletion is restored from
the local copy. Delete the entire configured remote directory through the provider only
when intentionally resetting its cloud history, then remove the matching local baseline
under `config/` before creating a new relationship.

Content objects are immutable so a client can never expose a half-written file through
the committed manifest. Older unreferenced revisions are not deleted automatically;
they can consume provider quota. This is an intentional safety tradeoff because generic
WebDAV has no portable transactional garbage-collection primitive.

## Automatic synchronization

**At application startup** synchronizes when no command-line or resume game must load.
When startup immediately loads a game, the operation is deferred until that game closes,
even if **After closing a game** is disabled. **After closing a game** runs after the core
has flushed SRAM/BRAM and released the game. Automatic transfers require a remembered
password. A missing credential or network failure is reported without blocking emulation.

Manual and automatic synchronization are disabled while a game is loading or active.
Game loading, recent launches, drag/drop, and physical-disc launch are disabled while a
transfer owns persistence files. Only one request can run and one can wait in the service
queue; shutdown cancels an active HTTPS request and joins the worker before application
data is released.

## Fixed safety limits

- 4,096 selected files
- 512 MiB represented by the current manifest
- 16 MiB per HTTP transfer
- the stricter native save-RAM or wrapped-state limit for each selected file
- 1 MiB remote/local manifest
- 15-second HTTP request timeout
- one waiting sync command and eight terminal events

Files and manifests are hashed and rechecked before local atomic replacement. A remote
object whose size or SHA-256 does not match its manifest is rejected.

## Troubleshooting

- **No saved password:** unlock or configure the platform credential service, choose
  Remember Password again, or type a one-time password before Synchronize Now.
- **HTTP 401/403:** verify the username, app password, and WebDAV permissions.
- **TLS validation failed:** correct the server certificate or system trust store. The
  application deliberately has no ignore-certificate control.
- **Another client changed the manifest:** wait for the other client to finish and run
  synchronization again.
- **Missing content object or hash mismatch:** preserve local files, inspect the provider
  for corruption, and do not edit `manifest.json` manually.
- **Provider quota grows:** old immutable revisions are safe to remove only as a complete
  remote-directory reset while all clients are stopped; partial object deletion can make
  a manifest unreadable.

The diagnostics dialog reports only enabled/idle/busy state and selected categories.
It intentionally omits the endpoint, username, password, and remote object paths.

## Build-time control

Official builds enable the feature. A distributor can omit QtKeychain and the native
credential integration with:

```bash
cmake --preset release -DGENPLUSGX_ENABLE_CLOUD_SYNC=OFF
```

The menu action remains visibly unavailable in that build. No cloud dependency is added
to the Genesis Plus GX core.
