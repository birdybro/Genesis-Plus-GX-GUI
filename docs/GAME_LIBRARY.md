# Game Library

The local game library is an offline SQLite index stored at
`library/game-library.sqlite3` beneath the platform application-data root. It contains
paths and metadata only. It does not copy ROMs, contact a metadata service, download
artwork, or initialize Genesis Plus GX while scanning.

The library window and directory controls are connected in the following UI milestone.
This document describes the database and scanner contract already implemented for that
interface.

## Directory model

Each configured directory has a canonical absolute path and an independent recursive
scan flag. Duplicate, parent, and child roots are rejected so one file cannot acquire
ambiguous ownership through overlapping scans. At most 256 roots may be configured.
Removing a root removes its indexed rows through an SQLite foreign-key cascade; it
never removes the games from disk.

Directory walks do not follow symbolic links and use the exact extension set advertised
by the current desktop build. A scan visits at most 100,000 regular files and indexes
metadata in fixed batches of 128. Unsupported files are ignored. Unreadable or changed
supported files are counted as skipped and do not crash the scan.

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
other than the one that initialized it.

At startup, SQLite `quick_check` and required-schema probes run before the index is
used. A malformed database or structurally incomplete current schema is renamed to a
collision-safe `.corrupt-<timestamp>` backup and a fresh schema is created. The backup
is retained for diagnosis and possible manual recovery. A database from a newer schema
version fails closed and is never replaced. SQLite WAL mode, foreign keys, a busy
timeout, and transactional schema creation are enabled on every connection.
