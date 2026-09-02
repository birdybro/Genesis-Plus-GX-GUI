# RetroAchievements

RetroAchievements support is an optional, disabled-by-default frontend service. It uses
the official `rc_client` API from checksum-pinned rcheevos 12.4.0; achievement logic is
not reimplemented and the Genesis Plus GX emulation algorithms are unchanged. The
application remains fully usable offline and never downloads games or firmware.

## Signing in

Open **Tools → RetroAchievements…**, select **Enable RetroAchievements**, enter your
RetroAchievements username and password, choose the desired mode options, and select
**Sign In**. A loaded supported game is identified automatically after login. The page
shows account scores, the recognized game, rich presence, achievement descriptions,
points, unlock state, and measured progress. Achievement, leaderboard, disconnect, and
reconnect events appear through bounded status notifications.

The password exists only long enough to perform the HTTPS login and is never written to
the settings file. The returned session token is stored through QtKeychain in Windows
Credential Manager, macOS Keychain, or a supported Linux Secret Service/KWallet store.
Plaintext fallback is explicitly disabled. If no secure store is available, sign-in may
succeed for the current process but the application reports that the session could not
be retained. **Sign Out** removes the stored token for that username.

Only the username and these non-secret choices are written atomically to
`config/achievements.json`: enabled, Hardcore, unofficial achievements, Encore, and
notifications. Provider traffic is restricted to HTTPS on port 443 at
`retroachievements.org` or its subdomains. Redirects, TLS errors, responses above
4 MiB, and requests lasting more than 15 seconds fail closed.

## Supported systems and games

The frontend reports the official RetroAchievements console identifier for Genesis /
Mega Drive, Sega CD / Mega CD, Master System / Mark III, Game Gear, and SG-1000. rcheevos
hashes the selected cartridge or disc through bounded file callbacks and asks the
service whether that exact content is recognized. Modified, patched, homebrew, or
different-region content may legitimately have no achievement set. Multi-disc media
changes are reported through the official client API.

No achievement definitions are bundled. Recognition, achievement definitions,
leaderboards, badges, scores, and rich-presence data are provided by RetroAchievements
when the user opts in. Normal emulation continues if the service becomes unavailable;
the client performs its supported idle/reconnect behavior without blocking the GUI.
Netplay remains unavailable while a recognized achievement set is active, in both
Softcore and Hardcore, because rollback would evaluate non-authoritative frames. Disable
achievements for that play session before starting a netplay connection.

## Hardcore Mode

Hardcore Mode becomes active only when support is enabled, the account is authenticated,
and the current game is recognized and loaded by the service. Activating it requests a
core reset and clears incompatible transient state. Enforcement occurs on the emulation
owner thread, not merely by disabling menu actions.

While active, Hardcore blocks save-state capture/restore, automatic resume checkpoints,
rewind, run-ahead, slow motion and below-100% normal speeds, frame advance, cheats,
netplay, debugger inspection/editing, and achievement-progress restoration from a
state. Pause is delegated to the official client's pause policy. Fast-forward and
normal speeds of 100% or greater remain available. Signing out or disabling the service
ends the restriction; it does not restore cleared rewind/run-ahead/debug history.

Frontend schema-3 `.gpgxstate` files can carry a separately bounded rcheevos progress
block alongside the byte-for-byte core payload. Softcore restores validate and return
that block to `rc_client`; schema-1 and schema-2 files remain supported. Hardcore never
captures or restores a state.

## Privacy, security, and troubleshooting

Network use occurs only after the user enables the feature. Credentials and tokens are
excluded from structured logs and diagnostics. The worker/network bridge holds at most
32 requests and 32 responses, the UI event list is bounded, and no network operation
runs on the emulation or GUI event-loop path. The achievement runtime reads emulated
memory only through a console-specific, bounds-checked adapter on the owner thread.

If a game is not recognized, verify that the exact revision is supported on the
RetroAchievements website; do not rename or alter the image to bypass its hash. If the
credential store reports an error on Linux, start a compatible Secret Service or
KWallet provider and sign in again. A TLS, timeout, or server error leaves the game
running offline. This project is an independent frontend integration and is not
officially endorsed by RetroAchievements.

Builds can exclude the integration with
`-DGENPLUSGX_ENABLE_ACHIEVEMENTS=OFF`. Such builds retain the offline emulator and show
the Tools action as unavailable. See [Building](BUILDING.md), [Architecture](ARCHITECTURE.md),
and [Testing](TESTING.md) for dependency, ownership, and automated-gate details.
