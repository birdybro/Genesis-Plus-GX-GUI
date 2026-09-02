#!/usr/bin/env python3
"""Exercise canonical release-manifest creation and public verification tooling."""

from __future__ import annotations

import base64
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def run(command: list[str], expected: int = 0) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, check=False)
    if (result.returncode == expected):
        return result
    raise AssertionError(
        f"command returned {result.returncode}, expected {expected}: "
        f"{' '.join(command)}\n{result.stdout}")


def main() -> int:
    source = Path(sys.argv[1]).resolve()
    create = source / "cmake" / "CreateUpdateManifest.py"
    verify = source / "cmake" / "VerifyUpdateManifest.py"
    with tempfile.TemporaryDirectory(prefix="genplusgx-update-manifest-") as temporary:
        root = Path(temporary)
        assets = root / "assets"
        assets.mkdir()
        version = "1.2.3"
        prefix = f"Genesis-Plus-GX-GUI-{version}"
        names = [
            f"{prefix}-linux-x86_64.tar.gz",
            f"{prefix}-windows-x86_64.zip",
            f"{prefix}-macos-arm64.zip",
            f"{prefix}-macos-arm64.dmg",
            f"{prefix}-macos-x86_64.zip",
            f"{prefix}-macos-x86_64.dmg",
        ]
        for name in names:
            (assets / name).write_bytes(f"generated fixture: {name}\n".encode())
        manifest = assets / "update-manifest.json"
        run([sys.executable, str(create), "--asset-directory", str(assets),
             "--version", version, "--repository", "https://github.com/test/project",
             "--key-id", "0123456789abcdef", "--published-at",
             "2026-09-02T18:00:00.000Z", "--output", str(manifest)])
        raw = manifest.read_bytes()
        parsed = json.loads(raw)
        assert raw == (json.dumps(parsed, sort_keys=True, separators=(",", ":")) +
                       "\n").encode()
        assert len(parsed["assets"]) == 6
        for asset in parsed["assets"]:
            path = assets / asset["fileName"]
            assert asset["size"] == path.stat().st_size
            assert asset["sha256"] == hashlib.sha256(path.read_bytes()).hexdigest()
        invalid_repository = subprocess.run(
            [sys.executable, str(create), "--asset-directory", str(assets),
             "--version", version,
             "--repository", "https://github.com.example/test/project",
             "--key-id", "0123456789abcdef", "--output", str(root / "invalid.json")],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            check=False)
        assert invalid_repository.returncode != 0 and "canonical HTTPS" in invalid_repository.stdout
        malformed_port = subprocess.run(
            [sys.executable, str(create), "--asset-directory", str(assets),
             "--version", version,
             "--repository", "https://github.com:not-a-port/test/project",
             "--key-id", "0123456789abcdef", "--output", str(root / "invalid-port.json")],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            check=False)
        assert malformed_port.returncode != 0 and "canonical HTTPS" in malformed_port.stdout

        openssl = shutil.which("openssl")
        if openssl is None:
            print("OpenSSL CLI unavailable; canonical generation passed and C++ Ed25519 tests cover verification")
            return 0
        pkeyutl_help = subprocess.run(
            [openssl, "pkeyutl", "-help"], text=True, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, check=False)
        if "-rawin" not in pkeyutl_help.stdout:
            print("OpenSSL CLI lacks Ed25519 raw-input support; canonical generation passed and C++ Ed25519 tests cover verification")
            return 0
        # RFC 8032 section 7.1 vector 1. The deterministic private seed is public
        # test material, is created only in this temporary directory, and is not
        # the project's production release key.
        seed = bytes.fromhex(
            "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60")
        private_der = bytes.fromhex("302e020100300506032b657004220420") + seed
        public_der = bytes.fromhex(
            "302a300506032b6570032100" +
            "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a")
        private_key = root / "test-private.der"
        public_key = root / "test-public.der"
        raw_signature = root / "signature.raw"
        signature = assets / "update-manifest.json.sig"
        private_key.write_bytes(private_der)
        public_key.write_bytes(public_der)
        run([openssl, "pkeyutl", "-sign", "-rawin", "-keyform", "DER",
             "-inkey", str(private_key), "-in", str(manifest),
             "-out", str(raw_signature)])
        signature.write_bytes(base64.b64encode(raw_signature.read_bytes()) + b"\n")
        run([sys.executable, str(verify), "--manifest", str(manifest),
             "--signature", str(signature), "--public-key", str(public_key),
             "--asset-directory", str(assets),
             "--repository", "https://github.com/test/project",
             "--key-id", "0123456789abcdef"])
        malformed_verify_port = subprocess.run(
            [sys.executable, str(verify), "--manifest", str(manifest),
             "--signature", str(signature), "--public-key", str(public_key),
             "--asset-directory", str(assets),
             "--repository", "https://github.com:not-a-port/test/project",
             "--key-id", "0123456789abcdef"],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            check=False)
        assert (malformed_verify_port.returncode != 0 and
                "canonical HTTPS" in malformed_verify_port.stdout)
        signature.write_bytes(signature.read_bytes() + b"\n")
        noncanonical_signature = subprocess.run(
            [sys.executable, str(verify), "--manifest", str(manifest),
             "--signature", str(signature), "--public-key", str(public_key),
             "--asset-directory", str(assets),
             "--repository", "https://github.com/test/project",
             "--key-id", "0123456789abcdef"],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            check=False)
        assert noncanonical_signature.returncode != 0 and "canonical base64" in noncanonical_signature.stdout
        signature.write_bytes(signature.read_bytes()[:-1])
        (assets / names[0]).write_bytes(b"tampered")
        failed = subprocess.run(
            [sys.executable, str(verify), "--manifest", str(manifest),
             "--signature", str(signature), "--public-key", str(public_key),
             "--asset-directory", str(assets),
             "--repository", "https://github.com/test/project",
             "--key-id", "0123456789abcdef"],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            check=False)
        assert failed.returncode != 0 and "mismatch" in failed.stdout
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
