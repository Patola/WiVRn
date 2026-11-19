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

KERNEL = "7.2.0-g344f27110f05"
CONFIG_SHA = "55a25adc47f0692b0aab337cf30b339a6b6ba0a899d30be05b83914b0754d4d0"


def stream_sha256(data):
    digest = hashlib.sha256()
    for block in iter(lambda: data.read(1024 * 1024), b""):
        digest.update(block)
    return digest.hexdigest()


def verify_bundle(archive, root, component, commit):
    with tarfile.open(archive, "r:xz") as bundle:
        source = json.load(bundle.extractfile(f"{root}/SOURCE.json"))
        if source["source_commit"] != commit or source["component"] != component:
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
        if component == "client":
            if not bundle.getmember(f"{root}/bin/wivrn").mode & 0o111:
                raise ValueError("Client archive lost executable permissions")
        else:
            config = bundle.extractfile(f"{root}/kernel-config.sha256").read().decode().strip()
            info = bundle.extractfile(f"{root}/build-info.txt").read().decode()
            if config != CONFIG_SHA or f"kernel={KERNEL}\n" not in info:
                raise ValueError("Iris archive does not match the supported kernel/configuration")
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
    client = verify_bundle(
        args.output / "wivrn-frame-av1-arm64.tar.xz", "wivrn-frame-av1-arm64", "client", commit
    )
    iris_name = f"iris-av1-{KERNEL}-aarch64"
    iris = verify_bundle(args.output / f"{iris_name}.tar.xz", iris_name, "iris", commit)
    if not args.flatpak.is_file() or args.flatpak.stat().st_size == 0:
        raise ValueError("Server Flatpak bundle is missing or empty")
    shutil.copy2(args.flatpak, args.output / "wivrn-server-x86_64.flatpak")
    shutil.copy2(root / "docs/steam-frame-av1-downloads.md", args.output / "AV1-QUICKSTART.md")
    shutil.copy2(root / "docs/steam-frame-av1-guide.md", args.output / "BUILD-GUIDE.md")
    metadata = {
        "source_commit": commit,
        "source_url": f"https://github.com/Patola/WiVRn/tree/{commit}",
        "workflow_run": os.environ.get("GITHUB_RUN_ID"),
        "server_architecture": "x86_64",
        "flatpak_branch": "av1",
        "client": client,
        "iris": iris,
        "required_kernel": KERNEL,
        "required_kernel_config_sha256": CONFIG_SHA,
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
