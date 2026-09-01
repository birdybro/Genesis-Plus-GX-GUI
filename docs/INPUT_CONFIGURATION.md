# Input Configuration

The desktop frontend accepts keyboard and SDL3-compatible gamepads. Input is translated
to a controller-neutral snapshot before it reaches the emulation thread, so host device
events never access Genesis Plus GX globals directly.

## Default gamepad layout

SDL's standardized physical positions are used, making the mapping independent of the
letters printed by Xbox, PlayStation, and Nintendo-style controllers.

| Genesis control | Standard gamepad position |
| --- | --- |
| Up / Down / Left / Right | D-pad or left stick |
| A | West face button |
| B | South face button |
| C | East face button |
| X | North face button |
| Y | Left shoulder |
| Z | Right shoulder |
| Start | Start |
| Mode | Back / Select |

The left stick uses a default deadzone of 8,000 on SDL's signed 16-bit axis range. Its
four directions are explicit, configurable axis mappings rather than hidden special
cases. Digital D-pad input takes precedence while held. If opposite D-pad directions
are held, the most recently pressed direction wins and the earlier direction resumes
after it is released.

## Discovery and player assignment

Controllers connected before startup are discovered during service initialization.
Hot-plug add, remove, and mapping-change events are handled while the application runs.
The first controller uses Player 1, the next uses Player 2, and so on through the eight
logical slots supported by the frontend protocol. Reassigning a controller to an
occupied player swaps the two assignments, preserving both devices.

Keyboard and gamepad input are merged, so a Player 1 controller does not disable the
default keyboard controls. If two different devices request opposite directions at the
same time, the merged direction is neutral to avoid sending an impossible directional
state to the core.

## Configuring bindings

Choose **Input → Controller Configuration…**. The Bindings tab shows every Genesis
three-/six-button control and its keyboard and standardized gamepad binding. Activate a
binding button, then press the replacement key or controller button. The capture can be
cancelled with Escape. Captured controller buttons are consumed by the dialog and do
not leak into a running game.

Duplicate physical bindings are rejected with an inline explanation. Unmodified keys
reserved by the configured emulator hotkeys are also rejected, preventing a gameplay mapping from
silently shadowing Pause, Fast Forward, Frame Advance, save/load, screenshot, mute, or
recording/volume shortcuts. Restore Defaults resets the selected profile; Cancel leaves the live
configuration unchanged; Apply and OK validate before publishing changes.

The Hotkeys tab captures a single keyboard combination for Open/Close, library,
pause/reset, fullscreen, separate fast-forward and slow-motion hold/toggle controls, frame advance,
save-state operations and slots, screenshot, mute, and volume. Shortcuts must be unique.
Lossless recording has its own start/stop binding in the same list.
A shortcut that would consume a
gameplay key in any stored profile is rejected, while Ctrl, Alt, and Command/Meta chords
remain available because gameplay input deliberately ignores those modifiers. Apply or
OK changes the live menu shortcuts immediately. Restore Defaults while this tab is
selected resets the hotkeys rather than the active gameplay profile.

The momentary fast-forward binding is handled on press and release. Application/window
deactivation and hiding the main window synthesize a release so acceleration cannot be
left active after a focus transition. It composes with the menu toggle: the effective
state stays enabled until both the held key and the independent toggle latch are off.
Schema-2 profiles keep their existing fast-forward binding as the toggle and receive a
non-conflicting default hold binding during the schema-3 migration.
Schema-4 profiles receive conflict-free `/` hold and `Ctrl+/` toggle bindings during
the schema-5 slow-motion migration. Schema-5 profiles receive `Ctrl+Shift+F12` (or the
first conflict-free fallback) for recording during schema-6 migration. Slow-motion
holds use the same forced release on focus loss as fast forward and rewind.

The Advanced Devices tab configures the analog deadzone, the logical device selected
for each of eight frontend players, and the emulated action produced by each left-stick
axis direction. It includes standard pads, Sega Mouse, light gun, paddle, Sports Pad,
XE-1AP, Pico, Terebi Oekaki, Graphic Board, and Activator choices. Availability remains
subject to the loaded system and the core's port restrictions.

Accepted device changes are converted to a core-neutral device snapshot and sent
through the bounded emulation command queue. They reinitialize port handlers on the
emulation thread, so a running session can switch between compatible pads and
peripherals without any GUI-thread core access. Player devices must be contiguous from
Player 1. Three through eight simultaneous devices must all be pads: Genesis/Sega CD
uses one or two Sega Team Players, while SG-1000/Mark III/Master System/Game Gear uses
one or two Master Taps. Unused positions on a partial multitap are explicitly exposed
as disconnected. Eight-bit systems always receive native two-button pads even when the
shared profile names a Genesis three- or six-button pad.

A generic light gun is assigned to the Menacer-compatible port B on Genesis hardware
and to Light Phaser ports on 8-bit hardware. Pico and Terebi tablet selections must be
the sole device and become functional only with compatible core-detected software or
hardware; the selection still reaches the corresponding core device slot. Invalid
gaps, mixed-peripheral multitaps, and incompatible multi-tablet layouts are rejected
before persistence.

## Profiles and persistence

New Profile copies the current profile under a unique local name. Profiles may be
selected, modified, reset, and deleted (the final profile cannot be deleted). The active
profile is applied immediately after a successful Apply or OK.

Profiles are stored atomically as versioned JSON at
`<application-data>/config/input-profiles.json`. The file contains no hardware secrets.
Schemas 0–4 migrate to the current schema with explicit device, axis, rewind, and
slow-motion hotkey defaults for fields that did not previously exist. A missing file produces defaults; malformed, oversized,
future-schema, or invalid data is rejected without partially applying it.
