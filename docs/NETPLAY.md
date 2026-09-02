# Netplay

Genesis Plus GX GUI provides opt-in, authenticated two-peer play for a currently
loaded game. The host owns Player 1 and the guest owns Player 2. Netplay is disabled
until the user explicitly opens **Tools → Netplay…** and hosts or joins a session.
There is no discovery, port mapping, relay, account, or background network service.

## Starting a session

1. Both players load byte-identical game content with the same application build.
2. Both players choose the same deterministic core settings, enabled cheats, input
   devices, and BIOS files where firmware is used.
3. The host opens **Tools → Netplay…**, chooses a private session code of at least six
   characters, a TCP port, input delay, and rollback window, then selects **Host
   Session**.
4. The guest enters the host name/address, same port, code, delay, and rollback window,
   then selects **Join Session**.
5. On successful mutual authentication, both emulator workers hard-reset to the same
   initial state through one transactional owner-thread operation. Invalid startup
   leaves the loaded game paused rather than partially enabling the session. Player 1
   is local to the host and Player 2 is local to the guest.

The default TCP port is `55455`. The host may need to allow this application through a
local firewall and manually forward the selected TCP port for play across a NAT. A VPN
that provides direct peer addressing is another option. Genesis Plus GX GUI never
changes router settings automatically.

## Security and privacy

The session code is used in an HMAC-SHA-256 challenge/response exchange. A key derived
from fresh 256-bit host and guest nonces authenticates every input packet. Strictly
increasing packet sequence numbers reject replay, and fixed packet/receive-buffer
limits reject oversized input. The code is cleared after authentication and is never
saved or logged.

Authentication and integrity protection are not traffic encryption. A network observer
may infer connection timing and gameplay traffic volume. Use a trusted network or an
encrypted VPN if traffic privacy is required. Do not reuse a sensitive password as a
session code.

The handshake rejects a peer whose game SHA-256, application/core Git build,
deterministic core settings, active input-device profile, enabled cheats, or validated
BIOS checksums differ. Error messages never disclose the session code.

## Timing and rollback

Local input is frame-stamped with the selected 0–8 frame delay. Missing peer input is
predicted from the last authoritative peer state. If a late input differs, the
emulation thread restores an exact Genesis Plus GX raw/transient rollback snapshot and
re-simulates to the present frame. Corrective frames do not enter the audio device,
video presentation queue, or recording service.

Rollback history is bounded to 1–12 frames and 64 MiB. The input timeline, command
queue, wire parser, and outgoing queue are also bounded. An input outside the window,
an overflow (including the Qt socket read/write queues), conflicting duplicate,
malformed packet, authentication failure, or core
restore failure ends the session rather than continuing in a potentially divergent
state.

## Locked operations

While a session is listening, connecting, authenticating, or active, controls that can
change deterministic state are disabled. This includes pause/reset, speed modes,
rewind, run-ahead, save-state operations, disc changes, cheats, controller profiles,
BIOS, and core video/audio/system settings. Fullscreen, presentation-only display
controls, volume/mute, screenshots, and closing the game remain safe. Loading or
closing a game disconnects netplay first.

## Diagnostics and current limits

**Tools → Log and Diagnostics…** reports connection state, authenticated transport
packet/byte counts, authentication/protocol failures, bounded output depth, predictions,
rollbacks, and rollback-history use. It deliberately omits peer addresses and secrets.

Milestone 94 supports one peer and two players over direct TCP. It does not yet provide
spectators, relay matchmaking, UPnP/NAT-PMP, encrypted transport, synchronized pause,
host migration, runtime state-hash/desync exchange, or mid-session save-state
synchronization. Physical-disc sessions are
possible only after each side has independently loaded identical validated content;
network transfer of games or BIOS files is intentionally unsupported.
M3U playlists and arbitrary pre-session disc swaps are rejected because the selected
disc is not yet part of the wire identity; load the desired CUE or CHD directly.
