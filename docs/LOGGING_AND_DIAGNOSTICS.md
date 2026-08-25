# Logging and Diagnostics

## Structured frontend logs

The desktop application installs a Qt message handler after its platform data
directories are initialized. Records are UTF-8 JSON Lines with this shape:

```json
{"category":"default","level":"information","message":"Game unloaded.","timestamp":"2026-08-25T12:34:56.789Z"}
```

The field set is stable: UTC `timestamp`, severity `level`, Qt logging `category`, and
the redacted `message`. Ordering reflects writes serialized at the frontend logger.
The active file is `logs/frontend.jsonl` beneath the application-data root. It is
limited to 1 MiB and rotates through `.1`, `.2`, and `.3`; a record is dropped if it
cannot fit or rotation/write fails, and the dropped counter appears in Diagnostics.
There is no frame-by-frame logging.

Before serialization, the logger replaces the current home directory and other
absolute paths. Values associated with password, token, secret, authorization, or API
key labels are replaced as well. The previous Qt handler still receives each message,
so console diagnostics remain available in development builds.

## Diagnostics report

Open **Tools → Log and Diagnostics…**. The snapshot includes:

- application version, Git commit, Qt version, and compiled SDL version;
- operating-system description and CPU architecture;
- active OpenGL/software renderer and audio device;
- audio occupancy/capacity plus underrun/overrun counters;
- controller count and loaded game title, system, and region;
- each BIOS slot's validation state and a short SHA-256 prefix only when valid;
- logger active/written/dropped/redacted/rotation counters.

The report deliberately excludes ROM paths, BIOS paths, the log path, save paths,
controller identifiers, and configuration contents. A final formatter pass redacts
unexpected absolute paths and credential-like text. **Copy Diagnostics** copies exactly
the read-only text shown in the dialog.

## Troubleshooting workflow

Reproduce the issue, open Diagnostics, and copy the report. If detailed event order is
needed, close the application cleanly and provide only the relevant JSONL records after
reviewing them. Rotation means older sessions may be in `.1` through `.3`. Do not attach
ROMs, BIOS files, save files, or state files to a public issue.
