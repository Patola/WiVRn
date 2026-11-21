#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate the same-run bundles before exposing a complete public release."""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import tarfile
from pathlib import Path, PurePosixPath


def stream_sha256(data):
    digest = hashlib.sha256()
    for block in iter(lambda: data.read(1024 * 1024), b""):
        digest.update(block)
    return digest.hexdigest()


def verify_bundle(archive, root, commit):
    with tarfile.open(archive, "r:xz") as bundle:
        source = json.load(bundle.extractfile(f"{root}/SOURCE.json"))
        if source["source_commit"] != commit or source["component"] != "client":
            raise ValueError(f"{archive}: source commit/component mismatch")
        if source["architecture"] != "aarch64":
            raise ValueError(f"{archive}: expected ARM64")
        manifest = bundle.extractfile(f"{root}/SHA256SUMS").read().decode()
        for line in manifest.splitlines():
            expected, name = line.split(maxsplit=1)
            relative = PurePosixPath(name)
            if relative.is_absolute() or ".." in relative.parts:
                raise ValueError(f"{archive}: unsafe manifest path")
            member = bundle.getmember(f"{root}/{relative}")
            if not member.isfile():
                raise ValueError(f"{archive}: expected a regular checksummed file")
            with bundle.extractfile(member) as data:
                actual = stream_sha256(data)
            if actual != expected:
                raise ValueError(f"{archive}: checksum mismatch for {name}")
        if not bundle.getmember(f"{root}/bin/wivrn").mode & 0o111:
            raise ValueError("Client archive lost executable permissions")
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("flatpak", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    commit = subprocess.check_output(["git", "-C", root, "rev-parse", "HEAD"], text=True).strip()
    if os.environ.get("GITHUB_SHA", commit) != commit:
        raise ValueError("Checkout differs from the workflow's source commit")
    allowed_assets = {
        "wivrn-frame-av1-arm64.tar.xz",
        "wivrn-server-x86_64.flatpak",
        "AV1-QUICKSTART.md",
        "BUILD-GUIDE.md",
        "SOURCE.json",
        "SHA256SUMS",
    }
    unexpected = {path.name for path in args.output.iterdir()} - allowed_assets
    if unexpected:
        raise ValueError(f"Unexpected release assets: {', '.join(sorted(unexpected))}")
    client = verify_bundle(
        args.output / "wivrn-frame-av1-arm64.tar.xz", "wivrn-frame-av1-arm64", commit
    )
    if not args.flatpak.is_file() or args.flatpak.stat().st_size == 0:
        raise ValueError("Server Flatpak bundle is missing or empty")
    shutil.copy2(args.flatpak, args.output / "wivrn-server-x86_64.flatpak")
    quickstart = (root / "docs/steam-frame-av1-guide.md").read_text()
    quickstart = quickstart.replace("(steam-frame-av1-building.md)", "(BUILD-GUIDE.md)")
    (args.output / "AV1-QUICKSTART.md").write_text(quickstart)
    shutil.copy2(root / "docs/steam-frame-av1-building.md", args.output / "BUILD-GUIDE.md")
    metadata = {
        "source_commit": commit,
        "source_url": f"https://github.com/Patola/WiVRn/tree/{commit}",
        "workflow_run": os.environ.get("GITHUB_RUN_ID"),
        "server_architecture": "x86_64",
        "flatpak_branch": "av1",
        "client": client,
        "hardware_tested_by_ci": False,
    }
    (args.output / "SOURCE.json").write_text(json.dumps(metadata, indent=2) + "\n")
    lines = []
    for path in sorted(args.output.iterdir()):
        if path.is_file() and path.name != "SHA256SUMS":
            with path.open("rb") as data:
                digest = stream_sha256(data)
            lines.append(f"{digest}  {path.name}\n")
    (args.output / "SHA256SUMS").write_text("".join(lines))
    print(f"Verified release assets from {commit}; ready for publication")


if __name__ == "__main__":
    main()
