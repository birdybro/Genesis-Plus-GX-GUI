#!/usr/bin/env python3
"""Create the canonical signed-update manifest from a complete package set."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import re
from pathlib import Path
from urllib.parse import urlsplit


VERSION = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--asset-directory", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--key-id", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--published-at")
    arguments = parser.parse_args()

    if not VERSION.fullmatch(arguments.version):
        parser.error("--version must be canonical major.minor.patch")
    repository = arguments.repository.rstrip("/")
    repository_url = urlsplit(repository)
    repository_parts = [part for part in repository_url.path.split("/") if part]
    try:
        repository_port = repository_url.port
    except ValueError:
        parser.error("--repository must be a canonical HTTPS github.com owner/project URL")
    if (repository_url.scheme != "https" or repository_url.hostname != "github.com" or
            repository_port not in (None, 443) or repository_url.username or
            repository_url.password or repository_url.query or repository_url.fragment or
            len(repository_parts) != 2 or
            repository_url.path != f"/{repository_parts[0]}/{repository_parts[1]}"):
        parser.error("--repository must be a canonical HTTPS github.com owner/project URL")
    if not re.fullmatch(r"[0-9a-f]{16}", arguments.key_id):
        parser.error("--key-id must contain 16 lowercase hexadecimal characters")
    published_at = arguments.published_at
    if not published_at:
        published_at = dt.datetime.now(dt.timezone.utc).isoformat(
            timespec="milliseconds").replace("+00:00", "Z")
    if not re.fullmatch(r"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}Z", published_at):
        parser.error("--published-at must be UTC ISO-8601 with milliseconds")

    prefix = f"Genesis-Plus-GX-GUI-{arguments.version}"
    specifications = [
        ("linux", "x86_64", "tar.gz", f"{prefix}-linux-x86_64.tar.gz"),
        ("windows", "x86_64", "zip", f"{prefix}-windows-x86_64.zip"),
        ("macos", "arm64", "zip", f"{prefix}-macos-arm64.zip"),
        ("macos", "arm64", "dmg", f"{prefix}-macos-arm64.dmg"),
        ("macos", "x86_64", "zip", f"{prefix}-macos-x86_64.zip"),
        ("macos", "x86_64", "dmg", f"{prefix}-macos-x86_64.dmg"),
    ]
    assets = []
    for platform, architecture, file_format, name in specifications:
        path = arguments.asset_directory / name
        if not path.is_file() or path.is_symlink():
            raise SystemExit(f"required regular package is missing: {name}")
        size = path.stat().st_size
        if size < 1 or size > 2 * 1024 * 1024 * 1024:
            raise SystemExit(f"package size is outside the trusted bounds: {name}")
        assets.append({
            "architecture": architecture,
            "fileName": name,
            "format": file_format,
            "platform": platform,
            "sha256": digest(path),
            "size": size,
            "url": f"{repository}/releases/download/v{arguments.version}/{name}",
        })
    manifest = {
        "assets": assets,
        "keyId": arguments.key_id,
        "publishedAt": published_at,
        "releasePage": f"{repository}/releases/tag/v{arguments.version}",
        "schemaVersion": 1,
        "version": arguments.version,
    }
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_bytes(
        (json.dumps(manifest, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
