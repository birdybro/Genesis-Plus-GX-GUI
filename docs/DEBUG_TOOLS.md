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
request an immediate sample. While paused, **Step 68000** and **Step Z80** execute one
instruction through the selected real CPU engine; the inactive 68000 is rejected on
8-bit systems.

## Inspection pages

- **CPU** shows all 68000 data/address registers, PC, SR, USP, and ISP, plus the Z80 main
  and alternate register sets, IX/IY, stack/program counters, interrupt state, halt
  state, and bank. Hexadecimal edits are available only while paused.
- **Memory** reads cartridge ROM, 68000 RAM, Z80 RAM, VRAM, CRAM, VSRAM, and the raw VDP
  register file. On SG-1000, Mark III/Master System, and Game Gear software, Z80 RAM is
  the active console work RAM rather than the Genesis sound-CPU buffer. Reads and
  paused writes are bounded to 4096 bytes per request. The displayed address includes
  the selected region's logical bus base. A newly loaded game selects its active CPU
  and RAM automatically; inactive 68000 RAM access on an 8-bit system is rejected
  instead of aliasing work RAM with the wrong byte order. Persistent SRAM and Sega CD
  backup-memory files remain protected by their normal persistence tools.
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

## RAM analysis

The **Analysis → RAM Search** page scans 68000 or Z80 RAM as overlapping 8-, 16-, or
32-bit values. 68000 values use logical big-endian byte order; Z80 values use
little-endian order. Signed and unsigned modes support equal, not-equal, changed,
unchanged, increased, decreased, greater-than, and less-than filters. **New Search**
captures a baseline and each **Filter** compares the latest immutable snapshot with the
previous retained baseline. A search has at most 65,536 candidates, and the table shows
at most the first 1,024 while preserving the exact total.

The **RAM Watch** page maintains at most 256 typed addresses and reports their current
value and whether it changed since the prior visible snapshot. Watches read copied RAM;
they never add memory callbacks to the core. Searches and watches are cleared when the
game closes or changes so an address cannot silently cross game identities.

## Program-counter breakpoints

The **Breakpoints** page accepts up to 64 unique 68000 or Z80 addresses. The list is
installed transactionally on the emulation worker. When any breakpoint is active, that
worker reads only the two program counters after each completed frame. A match pauses
emulation, clears pending host audio, reports the CPU/address, and switches the debugger
to the breakpoint page. Resume re-arms the breakpoint.

These are explicitly frame-boundary breakpoints: they are useful for stable main loops,
idle loops, and frame handlers but do not stop midway through an instruction. This
design keeps breakpoint decisions outside the authoritative CPU algorithms. **Step
Frame** advances a complete synchronized video frame; the two CPU step actions instead
advance only the selected CPU and are intended for paused code inspection.

## Instruction trace, symbols, and external analysis

**Analysis → Trace & Symbols** can record 68000 execution, Z80 execution, or both. The
desktop build enables only the inherited execute-hook boundary; memory read/write hooks
remain absent from normal builds. A null hook is the default until tracing is explicitly
enabled. The core-side circular buffer holds 4,096 records, overwrites the oldest record
on overflow, and reports the exact dropped count. The GUI retains at most 4,096 records
and renders only the newest 1,024 rows, so leaving trace enabled cannot create an
unbounded core, command, event, or widget allocation. Closing/changing a game disables
and clears tracing.

Symbol import is local and atomic. Files are limited to 1 MiB and 65,536 records using
one of these whitespace-separated forms:

```text
m68k 000200 ResetEntry
z80 $0038 IrqVector
0x000250 ControllerPoll
```

An omitted CPU means 68000. Addresses are hexadecimal, names are bounded printable
tokens, and `#` or `;` starts a comment. Any malformed record rejects the complete new
file and leaves the prior table active. Symbols annotate exact trace addresses only;
they never write to the game or core.

**Export JSON…** writes the retained trace atomically using versioned schema 1. Each
entry contains its monotonic sequence, CPU, hexadecimal address, master-cycle counter,
and an exact symbol when available; the top level includes the dropped-record total.
This file-only integration is suitable for scripts and offline analysis without opening
a debugger socket, accepting commands from the network, or granting another process
access to emulation memory. A GDB/IDA server is deliberately not claimed.

## Safety and threading

Genesis Plus GX global state is not thread-safe. No debug widget reads it. Requests use
the same bounded command queue as other emulator operations and execute on the sole
emulation-owner thread between frames. Responses own immutable copied data. The window
allows no more than one snapshot, one memory read, and one trace drain to remain
outstanding, preventing refresh delay from becoming queue or memory growth.

The worker, not the checkbox state, is the authority for mutation: memory and register
writes received while running are rejected. A write is range-checked against the
selected logical region and cannot wrap or exceed the transfer cap. Closing a game
invalidates the view and stops requests. Closing/hiding the window stops its timer.

Debug edits can put the emulated software into a nonsensical state. Save first and use
pause before editing. This risk is confined to the emulated session; normal recoverable
errors are shown in the workspace status bar and logged without frame-by-frame noise.

## Automated coverage

The generated-ROM `core.debug_tools` regression verifies Genesis, SG-1000, Master
System, and Game Gear snapshots, active CPU/RAM selection, byte order, transfer limits,
real single-instruction execution, bounded 68000/Z80 hooks, trace overflow accounting,
edits, rejection while running, command serialization, and a real pause-on-breakpoint
workflow. `unit.debug_analysis` verifies typed/endian reads, RAM filter semantics,
atomic bounded symbol parsing, CPU-address identity, and versioned JSON output.
It also mutates the symbol format through a fixed-seed 512-case bounded corpus and
requires every rejected input to preserve the prior active table.
The headless `gui.debug_tools` regression verifies default hiding, every inspection
page, searches, watches, breakpoints, step/trace controls, symbol annotation, atomic
export, and validated state routing.
`gui.debug_tools_live` connects the actual workspace to the actual worker and proves
both 68000 and Z80 generated programs pause at configured breakpoints, expose their
written RAM, and route a live execution trace. Run them with:

```bash
ctest --preset debug -L debug --output-on-failure
```

No proprietary ROM, BIOS, or ROM-derived image is stored by these tests.
