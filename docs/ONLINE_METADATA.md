# Online Metadata and Artwork

Online enrichment is an optional game-library feature. It is disabled by default and
is never needed to scan, identify, launch, or emulate a game. Enable it under **Tools →
Online Metadata and Artwork…** or through the Advanced page of **Tools → Settings…**.

## Privacy model

The frontend identifies local content with the SHA-256 already computed by the offline
library scanner. It never uploads ROM bytes, file contents, file paths, filenames, save
data, states, firmware, screenshots, or controller data.

The built-in Retronian GameDB provider downloads its static Mega Drive/Genesis hash
index and performs the hash match locally, so even the game SHA-256 is not sent in a
lookup request. It then requests the matched public game ID. The custom **Licensed
Manifest API v1** sends only the system slug and selected game's SHA-256 in an HTTPS
URL. Enabling automatic lookup applies the same rules after a successful library scan.

TLS certificate validation is mandatory. Requests reject embedded credentials and
fragments, use manual redirect handling, time out after 15 seconds, and enforce fixed
response limits. Provider metadata is limited to 512 KiB; a hash index and an artwork
file are each limited to 8 MiB. Network work runs on a bounded background service and
does not call or block the emulator core.

## Providers and licensing

**Retronian GameDB** currently enriches Genesis / Mega Drive entries. Its database is
published under CC BY-SA 4.0. The dialog and game information retain the provider,
creator, license, and exact source URL needed for attribution. Retronian records may
link to separate third-party thumbnail repositories whose image license is not stated
by the database response; the frontend deliberately rejects those image links.

**Licensed Manifest API v1** is intended for a user-operated or independently chosen
service covering any frontend-supported system. The service must return explicit
provider and per-artwork attribution. Accepted identifiers are CC0-1.0, CC-BY-3.0,
CC-BY-4.0, CC-BY-SA-3.0, CC-BY-SA-4.0, CC-BY-NC-SA-3.0,
CC-BY-NC-SA-4.0, and PDM-1.0. Missing, proprietary, unknown, or malformed license data
rejects the record or image; the identifier must reference its matching official
Creative Commons license URL. An invalid asset is never silently applied.

The project does not scrape websites, query commercial game databases, bundle a
metadata dump, or download box art without a declared approved license. Users remain
responsible for complying with a selected provider's current terms and an asset's
attribution/share-alike/noncommercial conditions.

## Library workflow

1. Add and scan a directory in **File → Game Library…**.
2. Select a row and choose **Look Up Metadata**. Automatic lookup can be enabled
   separately, but is off by default.
3. The row uses the enriched preferred title and can show licensed managed artwork.
   **Game Information…** displays release, developer, publisher, genres, description,
   provider, license, creator, and source alongside the core-independent local header.
4. Choose **Clear Online Metadata** to remove the enrichment. A local image selected
   with **Choose Artwork…** always takes priority and is never deleted by this action.

Only a validated exact SHA-256 response can attach to a row. Rescans preserve matching
enrichment. A record for one game cannot attach to another, and manually supplied local
artwork is never replaced by downloaded artwork.

## Cache and offline behavior

Validated records and licensed artwork live under `cache/online-metadata/`, partitioned
by provider type and endpoint. The configurable cache limit is 16–2048 MiB (128 MiB by
default). Records are refreshed after seven days and the Retronian hash index after one
day. If a refresh encounters a transport failure, a previously validated stale record
may be used and is identified as stale in the UI. Corrupt records, mismatched hashes,
invalid images, digest failures, oversized data, and unsupported schemas are rejected.
A corrupt cached Retronian index is replaced from the provider before use.

Cache artwork is application-managed and may be pruned. Clearing enrichment detaches
managed artwork but preserves user-owned images. Deleting the entire online-metadata
cache is safe; the next enabled lookup recreates it. Disabling the feature stops new
requests and leaves ordinary offline library and emulation behavior unchanged.

## Licensed Manifest API v1

For endpoint `https://metadata.example/api`, the frontend requests:

```text
GET https://metadata.example/api/v1/games/<system>/<lowercase-sha256>.json
```

The bounded JSON response has `schemaVersion: 1`, a `provider` object containing
`name`, `homepage`, `creator`, `license_spdx`, and `license_url`, plus a `game` object
containing the exact `sha256`, `title`, and `source_url`. Optional game fields are
`alternate_title`, `description`, `release_date`, `developer`, `publisher`, and a
bounded `genres` array. Optional `artwork` requires an HTTPS `url`, `mime_type`,
creator, source URL, license identifier/URL, and may include a lowercase SHA-256.
PNG and JPEG are accepted after content-type, digest, decoder, dimension, and
16-megapixel checks.

Provider responses should use stable HTTPS URLs and must not require redirects. This
protocol is intentionally small and read-only; the emulator never submits edits or
game files.
