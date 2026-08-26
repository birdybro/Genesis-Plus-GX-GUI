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
volume shortcuts. Restore Defaults resets the selected profile; Cancel leaves the live
configuration unchanged; Apply and OK validate before publishing changes.

The Hotkeys tab captures a single keyboard combination for Open/Close, library,
pause/reset, fullscreen, fast forward, frame advance, save-state operations and slots,
screenshot, mute, and volume. Shortcuts must be unique. A shortcut that would consume a
gameplay key in any stored profile is rejected, while Ctrl, Alt, and Command/Meta chords
remain available because gameplay input deliberately ignores those modifiers. Apply or
OK changes the live menu shortcuts immediately. Restore Defaults while this tab is
selected resets the hotkeys rather than the active gameplay profile.

The Advanced Devices tab configures the analog deadzone, the logical device selected
for each of eight frontend players, and the emulated action produced by each left-stick
axis direction. It includes standard pads, Sega Mouse, light gun, paddle, Sports Pad,
XE-1AP, Pico, Terebi Oekaki, Graphic Board, and Activator choices. Availability remains
subject to the loaded system and the core's port restrictions.

## Profiles and persistence

New Profile copies the current profile under a unique local name. Profiles may be
selected, modified, reset, and deleted (the final profile cannot be deleted). The active
profile is applied immediately after a successful Apply or OK.

Profiles are stored atomically as versioned JSON at
`<application-data>/config/input-profiles.json`. The file contains no hardware secrets.
Schema 0 and 1 data is migrated to schema 2 with explicit device, axis, and hotkey
defaults for fields that did not previously exist. A missing file produces defaults; malformed, oversized,
future-schema, or invalid data is rejected without partially applying it.
