# Game Library

The local game library is an offline SQLite index stored at
`library/game-library.sqlite3` beneath the platform application-data root. It contains
paths and metadata only. It does not copy ROMs, contact a metadata service, download
artwork, or initialize Genesis Plus GX while scanning.

Open **File → Game Library…** (`Ctrl+L`) to manage and launch the offline collection.
The window is modeless, so it may remain open while a game runs. Its controls have
stable accessibility names and support normal keyboard focus and table navigation.

## Using the library

Choose **Add…** and select a directory. New roots default to recursive scanning and
begin scanning immediately; select a root to change **Scan subdirectories** or request
**Scan Now** later. **Remove** deletes only the root and its index rows. It never deletes,
moves, or edits a game file. Mutating controls are briefly disabled while a scan owns
the write transaction, while searching, sorting, viewing information, and launching
remain responsive.

The table can be sorted by favorite, title, system, region, last-played time, play
count, or path. The search field matches titles, region, and path without case
sensitivity. System and region selectors compose with **Favorites only**. Double-click
a row or choose **Launch** to use the normal validated game-loading workflow. A launch
is recorded only after the emulation core accepts the game; successful loads from the
regular Open and Recent commands also update an indexed game's play count and
last-played time.

**Add to Favorites** and **Remove from Favorites** persist across rescans. **Game
Information…** opens the already-indexed read-only metadata without rerunning the core.
**Choose Artwork…** associates a local PNG, JPEG, WebP, or BMP path and displays a
bounded preview; **Clear Artwork** removes the association. Artwork is never copied,
uploaded, fetched, or scraped. A moved/deleted image is shown as unavailable without
affecting the game row.

## Directory model

Each configured directory has a canonical absolute path and an independent recursive
scan flag. Duplicate, parent, and child roots are rejected so one file cannot acquire
ambiguous ownership through overlapping scans. At most 256 roots may be configured.
Removing a root removes its indexed rows through an SQLite foreign-key cascade; it
never removes the games from disk.

Directory walks do not follow symbolic links and use the exact extension set advertised
by the current desktop build. A scan visits at most 100,000 regular files and indexes
metadata in fixed batches of 128. Unsupported files are ignored. Unreadable or changed
supported files are counted as skipped and do not crash the scan. Before parsing, the
scanner validates every CUE candidate and records its resolved payload files. A `.bin`,
`.iso`, or other supported payload referenced by a valid CUE is indexed through the CUE
only, preventing one disc from appearing as both a sheet and a raw track; unrelated
standalone files with the same extensions remain normal library games.

ZIP and M3U/M3U8 containers are discoverable and launch through the same source resolver
as Open, drag/drop, and command-line requests. The library indexes each container as one
offline source row; it does not duplicate ZIP members into separate database rows.

## Stored game data

The versioned schema stores each selected path, titles, detected system and region,
format, product and peripheral fields, file/declared sizes, checksums, mapper/media,
Sega CD track details, SHA-256, and last-modified time. It also reserves durable fields
for favorite, last-played, play-count, and user-provided artwork state. Rescanning a
path updates file-owned metadata while preserving those user-owned fields.

Every complete directory scan is a transaction. Rows not observed in that generation
are removed only when enumeration and metadata batching finish successfully. A stop,
filesystem enumeration failure, database failure, or safety-limit failure rolls the
transaction back, preserving the previously completed index.

## Threading and recovery

`GameLibraryScanner` owns its SQLite connection on one worker thread and accepts work
through a fixed-capacity queue. Progress and results return through a bounded event
queue. File hashing and directory enumeration therefore never run on the Qt GUI or
emulation threads. Each `GameLibraryDatabase` connection rejects use from any thread
other than the one that initialized it. Metadata hashing checks the scanner's atomic
cancellation request between 64 KiB chunks, so shutdown does not wait for the rest of a
large image to be read.

At startup, SQLite `quick_check` and required-schema probes run before the index is
used. A malformed database or structurally incomplete current schema is renamed to a
collision-safe `.corrupt-<timestamp>` backup and a fresh schema is created. The backup
is retained for diagnosis and possible manual recovery. A database from a newer schema
version fails closed and is never replaced. SQLite WAL mode, foreign keys, a busy
timeout, and transactional schema creation are enabled on every connection.
