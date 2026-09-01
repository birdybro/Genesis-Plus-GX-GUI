# Localization

Genesis Plus GX GUI uses Qt Linguist catalogs for interface text. English source text
is always available without an external file. The packaged `en_XA` catalog is an
expanded pseudo-localization used to find untranslated strings, clipped controls, and
layout assumptions; it is deliberately not presented as a natural language.

## Choosing a language

Open **Tools → Settings… → General → Appearance Settings…** and choose **System
language**, **English**, or **Pseudo-localization (layout testing)**. The selection is
stored in `config/appearance-settings.json` and takes effect the next time the
application starts. Restarting avoids a partially translated interface because many
windows and menus are constructed once during startup.

System language examines Qt's ordered UI-language list. English locales use the source
text. A catalog is used only when the application explicitly supports and packages
that locale. If no supported catalog is available, the complete English source UI is
used while the operating system's locale remains active for dates, numbers, layout,
and other locale-sensitive Qt behavior. A missing, corrupt, or un-installable selected
catalog also fails safely to English and is recorded in the structured startup log and
Diagnostics report.

The pseudo-language intentionally expands labels and adds accented characters. It is a
layout test, not a translation quality example. Object names, stored enum values,
keyboard shortcuts, command-line switches, file globs, URLs, and format placeholders
remain language-neutral.

## Catalog locations

Installed catalogs are named `genplusgx_<locale>.qm` and are loaded only from bounded
application-owned locations:

- `translations/` beside a development executable;
- `share/Genesis-Plus-GX-GUI/translations/` in Windows and Linux installs; or
- `Contents/Resources/translations/` inside the installed macOS app.

The application does not search the working directory or download translations. The
package verifier requires the pseudo catalog so every official artifact exercises the
same resource layout.

## Contributing a translation

Qt LinguistTools must be available in the selected Qt installation. Refresh source
messages, regenerate the deterministic pseudo catalog, and compile catalogs with:

```bash
cmake --build --preset debug --target genplusgx_update_translations
python3 cmake/GeneratePseudoLocale.py translations/genplusgx_en_XA.ts
cmake --build --preset debug --target genplusgx_release_translations
```

For a natural-language contribution, copy the extracted message structure to
`translations/genplusgx_<locale>.ts`, translate it with Qt Linguist, add the locale to
the catalog list and validated language choices, and add it to the translation test.
Do not machine-translate a catalog and present it as reviewed. Preserve every `%1`,
`%L1`, `%n`, and rich-text marker exactly, and leave proper names, file patterns, and
shortcut semantics intact.

Every user-visible Qt string belongs in `tr()` or
`QCoreApplication::translate()` with a stable context. Classes that call `tr()` must
have an appropriate Qt meta-object context. Runtime data, log field names, settings
keys, object names, and core-facing values must not be translated.

## Required checks

`unit.translation_catalog` extracts a fresh catalog into a temporary directory and
compares its complete source/context set with the committed catalog. It rejects missing
Qt meta-object contexts, unfinished pseudo messages, altered placeholders, unjustified
identity translations, and an unloadable compiled catalog. `unit.localization` covers
selection, package-relative discovery, invalid/missing catalog fallback, and manager
replacement. `gui.localization` constructs the real translated MainWindow and settings
UI, checks stable automation identifiers, expanded-layout bounds, word wrapping,
English fallback, right-to-left inheritance, and keyboard navigation. The separate
desktop pseudo-language smoke enters the actual application event loop and requires
the structured requested/effective-language record.

Run the focused gate with:

```bash
cmake --build --preset debug
ctest --preset debug -L localization --output-on-failure
```

Then run the complete suite. Native CI repeats the catalog compile and application
smoke on Linux, Windows, Apple Silicon macOS, and Intel macOS.
