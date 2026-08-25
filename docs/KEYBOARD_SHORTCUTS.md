# Keyboard Controls and Shortcuts

## Default Player 1 controls

The emulator display accepts the following keyboard controls when it has focus. These
defaults cover both standard Genesis controller layouts; three-button games simply
ignore X, Y, Z, and Mode.

| Emulated control | Default key |
| --- | --- |
| Up / Down / Left / Right | Arrow keys |
| A | Z |
| B | X |
| C | C |
| X | A |
| Y | S |
| Z | D |
| Start | Return |
| Mode | Shift |

Keyboard auto-repeat does not create repeated controller transitions. If opposite
directions are held together, the most recently pressed direction wins; releasing it
restores the still-held direction. Switching away from, hiding, or deactivating the
emulator display releases every held key to prevent stuck input.

Ctrl, Alt, and Command/Meta chords are reserved for application shortcuts and are not
interpreted as gameplay input. The standalone hotkey list will expand alongside the
emulation actions and configurable binding UI in later milestones; the menu always
shows the currently assigned application shortcuts.

## Application shortcuts

| Action | Shortcut |
| --- | --- |
| Open game | Ctrl+O (Command+O on macOS) |
| Close game | Ctrl+W (Command+W on macOS) |
| Fullscreen | Alt+Return |
| Pause/resume | Space |
| Hard reset | Ctrl+R |
| Fast forward | Tab |
| Frame advance | N |
| Save selected state slot | F5 |
| Load selected state slot | F8 |
| Select state slot 0–9 | Ctrl+0 through Ctrl+9 |
| Previous / next state slot | Ctrl+[ / Ctrl+] |
| Delete selected state | Ctrl+Delete |
| Screenshot | F12 |
| Mute | M |

Shortcuts whose runtime service is not yet connected remain disabled; their menu text
still documents the intended binding. Configurable emulator hotkeys are introduced by
the later settings milestone without changing gameplay mappings silently.
