# Debug tools

Genesis Plus GX GUI includes an optional native debugger for emulator and homebrew
development. It is hidden by default so ordinary players do not see controls that can
alter a running game's machine state.

## Enabling the workspace

Open **Tools → Settings… → General**, enable **Show developer/debug tools**, then choose
**Tools → Developer Tools → Emulator Debug Workspace…**. The workspace is also assigned
`Ctrl+Shift+D` while the developer submenu is enabled. Disabling the preference closes
the window and removes the submenu. The default and all migrated older configurations
leave it disabled.

Load a game before inspecting state. The workspace refreshes at four samples per second
while visible. Refreshing observes the core between frames and does not pace or pause it.
Use the toolbar to pause, resume, advance one whole frame, hard reset, soft reset, or
request an immediate sample.

## Inspection pages

- **CPU** shows all 68000 data/address registers, PC, SR, USP, and ISP, plus the Z80 main
  and alternate register sets, IX/IY, stack/program counters, interrupt state, halt
  state, and bank. Hexadecimal edits are available only while paused.
- **Memory** reads cartridge ROM, 68000 RAM, Z80 RAM, VRAM, CRAM, VSRAM, cartridge SRAM,
  Sega CD program/word/backup RAM, and Sega CD RAM cartridge where present. Reads and
  paused writes are bounded to 4096 bytes per request. The displayed address includes
  the selected region's logical bus base.
- **VDP** shows the 32 raw register bytes, 64-color palette, decoded tile patterns,
  sprite attribute table, Plane A/Plane B/window maps, and representative horizontal
  and vertical scroll values. VDP register edits require pause.
- **Sound** shows the two raw YM2612/YM3438 register-bank shadows and eight PSG latch
  values. These are inspection views; host output volume/device state remains in the
  normal Audio Settings and diagnostics surfaces.
- **Input** shows the exact logical button mask and analog pair consumed for each of the
  core's eight player slots at the latest frame boundary.
- **States** invokes the normal slots 0–9. Saves remain atomic and loads retain state
  schema, payload checksum, hardware, and game-identity validation.

The VDP explorers decode the latest snapshot rather than asking the core to render a
second frame. Consequently the tools cannot change framebuffer or audio hashes merely
by being open.

## Safety and threading

Genesis Plus GX global state is not thread-safe. No debug widget reads it. Requests use
the same bounded command queue as other emulator operations and execute on the sole
emulation-owner thread between frames. Responses own immutable copied data. The window
allows no more than one snapshot and one memory read to remain outstanding, preventing
refresh delay from becoming queue or memory growth.

The worker, not the checkbox state, is the authority for mutation: memory and register
writes received while running are rejected. A write is range-checked against the
selected logical region and cannot wrap or exceed the transfer cap. Closing a game
invalidates the view and stops requests. Closing/hiding the window stops its timer.

Debug edits can put the emulated software into a nonsensical state. Save first and use
pause before editing. This risk is confined to the emulated session; normal recoverable
errors are shown in the workspace status bar and logged without frame-by-frame noise.

## Current stepping boundary

**Step Frame** advances exactly one complete emulated frame and returns to pause. The
first workspace release does not claim instruction-level stepping or an external IDA
debug server. RAM search/watch and frame-boundary program-counter breakpoints are
tracked as the next debug-analysis milestone and will preserve the same worker-owned
boundary.

## Automated coverage

The generated-ROM `core.debug_tools` regression verifies snapshots, byte order,
transfer limits, edits, rejection while running, and command serialization. The
headless `gui.debug_tools` regression verifies default hiding, every inspection page,
controls, and validated state routing. Run them with:

```bash
ctest --preset debug -R 'core.debug_tools|gui.debug_tools' --output-on-failure
```

No proprietary ROM, BIOS, or ROM-derived image is stored by these tests.
