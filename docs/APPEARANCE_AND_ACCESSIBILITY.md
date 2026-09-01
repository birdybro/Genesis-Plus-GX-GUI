# Appearance and Accessibility

Genesis Plus GX GUI uses ordinary Qt 6 Widgets so operating-system keyboard,
accessibility, font, and display-scaling behavior remains available. It does not use a
custom skinned control toolkit.

## Themes

Open **Tools → Settings… → General → Appearance Settings…** and choose one of:

- **System default** restores the platform style and palette captured when the
  application started.
- **Light** uses Qt Fusion controls with a high-contrast light palette.
- **Dark** uses Qt Fusion controls with a high-contrast dark palette.

Apply changes every open application window. Cancel discards changes that were not
already applied. Restore Defaults stages System default. If saving fails, the dialog
stays open and exposes the error as visible text; an unsupported or corrupt settings
file falls back to System default without destroying that file.

## Interface language

The same dialog offers **System language**, **English**, and the deliberately expanded
**Pseudo-localization (layout testing)** catalog. A language change is persisted by
Apply/OK but takes effect after restart so startup-created menus and windows never form
a partially translated interface. If a selected catalog is absent or invalid, the
application keeps a complete English UI and reports the fallback through logging and
Diagnostics. System selection preserves the operating-system locale even when English
text is used as the fallback.

The pseudo-language is shipped to test expansion, wrapping, focus order, and hard-coded
English; it is not a natural-language translation. See [LOCALIZATION.md](LOCALIZATION.md)
for catalog paths and contribution rules.

The schema-3 setting is stored atomically in
`config/appearance-settings.json` beneath the platform application-data directory.
Schema 1 and 2 files migrate to System language without losing the existing theme.

## Keyboard access

The Settings window exposes a keyboard-navigable category list for General, Video,
Audio, Input, System, BIOS, Paths, and Advanced, and every category action is an
ordinary accessible Qt button. Menu titles and significant dialog labels include
mnemonics. On platforms that show
mnemonic underlines only while Alt is held, press Alt first. In Appearance Settings,
`Alt+A` focuses Application theme and `Alt+L` focuses Interface language. Tab visits
both choices, Restore Defaults, Apply, OK, and Cancel in a stable order. Enter activates
the focused/default button and Escape closes the dialog without applying staged changes.

Input-binding capture, game-library search and tables, BIOS paths, game information,
and standard dialog buttons remain in the normal Qt focus chain. Emulator gameplay
keys are suppressed while text entry or binding capture owns focus so configuration can
be completed safely from the keyboard.

## Assistive technology

Important interactive controls provide accessible names, descriptions, or associated
label buddies in addition to visible text. Status and validation messages are visible
labels, not color-only indicators. Qt publishes these objects through the native
accessibility bridge used by Windows UI Automation, macOS Accessibility, and Linux
AT-SPI when the corresponding platform service is available.

## High-DPI displays

Qt 6 high-DPI support is active and the process requests pass-through fractional scale
factors before creating `QApplication`. Widget sizes and fonts therefore follow the
desktop's device-independent scaling, including Retina displays. Emulator image
scaling is separate: use Video Settings to choose fit or integer pixels, aspect ratio,
and nearest or bilinear filtering.
