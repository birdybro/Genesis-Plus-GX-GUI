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

The left stick uses a default deadzone of 8,000 on SDL's signed 16-bit axis range.
Digital D-pad input takes precedence while held. If opposite D-pad directions are held,
the most recently pressed direction wins and the earlier direction resumes after it is
released.

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

Persistent custom mappings, deadzones, controller profiles, and the accessible binding
capture dialog are introduced by the following input-configuration milestone. Until
then the stable defaults above are active automatically.
