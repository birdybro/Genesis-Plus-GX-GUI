# Local A/V Streaming Output

Genesis Plus GX GUI can expose the authoritative native video frame and corresponding
stereo audio batch to a local capture client without making the GUI or emulation thread
wait for networking.

Choose **Tools → Local A/V Streaming Output…**, select a port from 1024–65535 and a
one-to-four-client limit, then choose **Start streaming**. The server binds only to the
IPv4 loopback address `127.0.0.1`; it is never reachable directly from another machine.
The dialog reports connected clients, queue occupancy, frames/bytes sent, dropped
frames, and clients removed for excessive backlog. Stop the server before giving the
port to another program.

This is a native-frame transport, not an Internet broadcast service or compressed
media container. A compatible local program can convert it to a codec/container or
feed broadcasting software. Use **File → Start Lossless A/V Recording…** when a normal
on-disk frame/WAV capture is preferable.

## `GPGX-AV/1` protocol

The TCP peer first receives the 10 ASCII bytes `GPGX-AV/1\n`. Each following packet is
little-endian and has this layout:

| Offset | Size | Meaning |
| ---: | ---: | --- |
| 0 | 4 | ASCII `GXF1` |
| 4 | 4 | Bytes after this eight-byte prefix |
| 8 | 8 | Monotonic emulated frame number |
| 16 | 4 | Native video width |
| 20 | 4 | Native video height |
| 24 | 4 | Flags: bit 0 interlaced, bit 1 odd field |
| 28 | 4 | Audio sample rate in Hz |
| 32 | 4 | Stereo audio-frame count |
| 36 | 4 | RGB565 pixel count |
| 40 | variable | Packed little-endian RGB565 pixels, then interleaved signed little-endian S16 left/right audio |

TCP is a byte stream: clients must accumulate the complete eight-byte prefix and the
declared remaining length instead of assuming one socket read equals one frame. Width,
height, interlace flags, pixel count, and audio count can change between frames. A
client should reject impossible lengths before allocating and should not assume 320×224
or a fixed PAL/NTSC audio batch.

## Bounded behavior and security

Capture delivery uses the same emulation-thread fan-out as lossless recording. The
network service receives an owned slot from a fixed queue (four frames by default),
encodes on its own thread into a reused packet buffer, and drops a new frame when no
slot is free. It never blocks core execution and never grows a frame queue without
bound. Each socket is disconnected if its Qt pending-write backlog exceeds 8 MiB. A
maximum of four peers is enforced.

There is intentionally no authentication because remote interfaces are prohibited and
the listener uses `QHostAddress::LocalHost`, not wildcard or LAN addresses. Any local
process running as the user can connect, so do not treat the stream as a confidentiality
boundary. No game path, title, ROM bytes, save data, BIOS data, input, or credential is
sent—only native output pixels, the matching audio batch, geometry, and counters.

`unit.streaming_service` uses real loopback sockets to cover validation, greeting,
packet delivery, port collision, queue bounds, fan-out, and restart/stop behavior.
`integration.streaming` runs a generated CC0 Genesis ROM through the real emulation
worker and verifies a complete native video/audio packet at a real TCP client.
`gui.movie_streaming` covers the accessible configuration, pending start/stop state,
metrics, and backend availability gates.
