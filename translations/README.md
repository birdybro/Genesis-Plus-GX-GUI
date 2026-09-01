# Interface translation catalogs

English source text is built into the application. `genplusgx_en_XA.ts` is a complete
Qt Linguist pseudo-localization catalog used to expose hard-coded English, layout
clipping, placeholder damage, and accelerator problems. It is deliberately accented
and expanded; it is not a natural-language translation.

After changing translatable source text, update the catalog from a configured build:

```bash
cmake --build --preset debug --target genplusgx_update_translations
python3 cmake/GeneratePseudoLocale.py translations/genplusgx_en_XA.ts
cmake --build --preset debug --target genplusgx_release_translations
```

Never edit generated pseudo translations by hand. New community translations should
use the `genplusgx_<locale>.ts` naming convention, preserve placeholders and
accelerators, and be reviewed by a fluent speaker before they are offered as a normal
language choice. The complete runtime, packaging, and contribution contract is in
[`docs/LOCALIZATION.md`](../docs/LOCALIZATION.md).
