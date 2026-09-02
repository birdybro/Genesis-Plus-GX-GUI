#!/usr/bin/env python3
"""Verify manifest canonical form, signature, trust identity, and local assets."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path
from urllib.parse import urlsplit


VERSION = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")
TIMESTAMP = re.compile(
    r"^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}Z$")


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def fail(message: str) -> None:
    raise SystemExit(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--signature", type=Path, required=True)
    parser.add_argument("--public-key", type=Path, required=True)
    parser.add_argument("--asset-directory", type=Path, required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--key-id", required=True)
    arguments = parser.parse_args()

    raw = arguments.manifest.read_bytes()
    try:
        parsed = json.loads(raw)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        fail(f"manifest JSON is invalid: {error}")
    canonical = (json.dumps(parsed, sort_keys=True, separators=(",", ":")) + "\n").encode()
    if raw != canonical:
        fail("manifest is not canonical compact sorted-key JSON")
    signature_file = arguments.signature.read_bytes()
    if len(signature_file) != 89 or not signature_file.endswith(b"\n"):
        fail("signature file must be one canonical base64 line")
    try:
        signature = base64.b64decode(signature_file[:-1], validate=True)
    except ValueError as error:
        fail(f"signature base64 is invalid: {error}")
    if len(signature) != 64:
        fail("Ed25519 signature must contain exactly 64 bytes")
    with tempfile.NamedTemporaryFile() as signature_file:
        signature_file.write(signature)
        signature_file.flush()
        verified = subprocess.run([
            "openssl", "pkeyutl", "-verify", "-rawin", "-pubin",
            "-inkey", str(arguments.public_key), "-in", str(arguments.manifest),
            "-sigfile", signature_file.name,
        ], check=False, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if verified.returncode != 0:
        fail("manifest Ed25519 signature verification failed: " + verified.stdout.strip())

    if set(parsed) != {"assets", "keyId", "publishedAt", "releasePage",
                       "schemaVersion", "version"}:
        fail("manifest root schema is invalid")
    if (type(parsed["schemaVersion"]) is not int or parsed["schemaVersion"] != 1 or
            parsed["keyId"] != arguments.key_id or
            not isinstance(parsed["version"], str) or
            not VERSION.fullmatch(parsed["version"]) or
            not isinstance(parsed["publishedAt"], str) or
            not TIMESTAMP.fullmatch(parsed["publishedAt"])):
        fail("manifest schema or key identity is invalid")
    repository = arguments.repository.rstrip("/")
    repository_url = urlsplit(repository)
    repository_parts = [part for part in repository_url.path.split("/") if part]
    try:
        repository_port = repository_url.port
    except ValueError:
        fail("repository is not a canonical HTTPS github.com owner/project URL")
    if (repository_url.scheme != "https" or repository_url.hostname != "github.com" or
            repository_port not in (None, 443) or repository_url.username or
            repository_url.password or repository_url.query or repository_url.fragment or
            len(repository_parts) != 2 or
            repository_url.path != f"/{repository_parts[0]}/{repository_parts[1]}"):
        fail("repository is not a canonical HTTPS github.com owner/project URL")
    version = parsed["version"]
    if parsed["releasePage"] != f"{repository}/releases/tag/v{version}":
        fail("release page is not bound to the trusted repository/version")
    if not isinstance(parsed["assets"], list) or len(parsed["assets"]) != 6:
        fail("manifest must contain the six cross-platform release assets")
    prefix = f"Genesis-Plus-GX-GUI-{version}"
    expected = {
        ("linux", "x86_64", "tar.gz"): f"{prefix}-linux-x86_64.tar.gz",
        ("windows", "x86_64", "zip"): f"{prefix}-windows-x86_64.zip",
        ("macos", "arm64", "zip"): f"{prefix}-macos-arm64.zip",
        ("macos", "arm64", "dmg"): f"{prefix}-macos-arm64.dmg",
        ("macos", "x86_64", "zip"): f"{prefix}-macos-x86_64.zip",
        ("macos", "x86_64", "dmg"): f"{prefix}-macos-x86_64.dmg",
    }
    seen = set()
    for asset in parsed["assets"]:
        if set(asset) != {"architecture", "fileName", "format", "platform",
                          "sha256", "size", "url"}:
            fail("manifest asset schema is invalid")
        identity = (asset["platform"], asset["architecture"], asset["format"])
        if identity in seen or identity not in expected:
            fail("manifest contains an unexpected or duplicate platform asset")
        seen.add(identity)
        name = asset["fileName"]
        if not isinstance(name, str) or name != expected[identity] or Path(name).name != name:
            fail("manifest asset filename is unsafe")
        if (type(asset["size"]) is not int or asset["size"] < 1 or
                asset["size"] > 2 * 1024 * 1024 * 1024 or
                not isinstance(asset["sha256"], str) or
                not re.fullmatch(r"[0-9a-f]{64}", asset["sha256"])):
            fail(f"manifest asset size or digest is invalid: {name}")
        path = arguments.asset_directory / name
        if not path.is_file() or path.is_symlink():
            fail(f"manifest asset is missing: {name}")
        if path.stat().st_size != asset["size"] or digest(path) != asset["sha256"]:
            fail(f"manifest asset size or digest mismatch: {name}")
        if asset["url"] != f"{repository}/releases/download/v{version}/{name}":
            fail(f"manifest asset URL is not trusted: {name}")
    if set(expected) != seen:
        fail("manifest does not contain the exact cross-platform package set")
    print(f"Verified signed update manifest for {version} with {len(seen)} assets")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
