# Keyboard Controls and Shortcuts

The same default shortcut reference is available in the application through **Help →
Keyboard Shortcuts**. Emulator shortcuts can be changed under **Input → Controller
Configuration… → Hotkeys**; Preferences and Quit retain their platform-standard keys.

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
interpreted as gameplay input. The menu shows the currently assigned application
shortcuts.

## Application shortcuts

| Action | Shortcut |
| --- | --- |
| Open game | Ctrl+O (Command+O on macOS) |
| Close game | Ctrl+W (Command+W on macOS) |
| Game library | Ctrl+L (Command+L on macOS) |
| Fullscreen | Alt+Return |
| Pause/resume | Space |
| Hard reset | Ctrl+R |
| Soft reset | Ctrl+Shift+R |
| Fast forward | Tab |
| Frame advance | N |
| Save selected state slot | F5 |
| Load selected state slot | F8 |
| Select state slot 0–9 | Ctrl+0 through Ctrl+9 |
| Previous / next state slot | Ctrl+[ / Ctrl+] |
| Delete selected state | Ctrl+Delete |
| Screenshot | F12 |
| Mute | M |
| Volume up / down | + / - |
| Preferences | Platform standard shortcut |
| Quit | Platform standard shortcut |

Game-dependent shortcuts stay disabled until a game or first video frame makes the
operation valid. Activate a Hotkeys-tab button and press one keyboard combination;
Escape cancels capture. Every shortcut must be unique. Unmodified shortcuts—and
Shift-only chords where either physical key is a gameplay control—are rejected when
they would shadow a gameplay binding in any profile. Ctrl, Alt, or Command/Meta chords
remain separate from gameplay input. **Restore Defaults** on the Hotkeys tab resets all
emulator shortcuts without changing controller profiles.
