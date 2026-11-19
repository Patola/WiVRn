#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Preserve executable bits and loader symlinks before uploading CI artifacts.
set -euo pipefail
repo_dir=$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
die() { printf '%s\n' "$*" >&2; exit 1; }
[[ $# == 3 && ( $1 == client || $1 == iris ) ]] || die "Usage: $0 client|iris INPUT_DIRECTORY OUTPUT_DIRECTORY"
component=$1
input=$(realpath -- "$2")
mkdir -p -- "$3"
output=$(realpath -- "$3")
[[ $output/ != "$input/"* ]] || die 'Output must be outside the input tree.'

if [[ $component == client ]]; then
    [[ -x $input/bin/wivrn ]] || die 'Client executable is missing or not executable.'
    [[ -f $input/lib/libopenxr_loader.so.1 && -d $input/share/wivrn/assets ]] || die 'Copy the complete installed usr tree, including the OpenXR loader and assets.'
    readelf -h "$input/bin/wivrn" | grep -q 'Machine:.*AArch64' || die 'Expected an ARM64 client.'
    name=wivrn-frame-av1-arm64
else
    (cd "$input" && sha256sum --check SHA256SUMS)
    [[ -f $input/iris-av1-session.sh && -f $input/kernel-config.sha256 ]] || die 'Module loader or configuration fingerprint is missing.'
    [[ $(modinfo -F name "$input/iris-av1.ko") == iris ]] || die 'Expected an Iris module.'
    readelf -h "$input/iris-av1.ko" | grep -q 'Machine:.*AArch64' || die 'Expected an ARM64 module.'
    kernel=$(modinfo -F vermagic "$input/iris-av1.ko")
    kernel=${kernel%% *}
    name="iris-av1-$kernel-aarch64"
fi

stage=$(mktemp -d "$output/.package.XXXXXXXX")
trap 'rm -rf -- "$stage"' EXIT
mkdir "$stage/$name"
cp -a "$input/." "$stage/$name/"
if [[ $component == client ]]; then
    cp "$repo_dir/COPYING" "$stage/$name/COPYING"
    cp "$repo_dir/LICENSE-OFL-1.1" "$stage/$name/LICENSE-OFL-1.1"
    # These permissively licensed dependencies are compiled into the client.
    # Keep their notices next to the binary and the corresponding-source URL.
    deps=${FETCHCONTENT_BASE_DIR:-$repo_dir/cmake-fetch}
    if [[ -d $deps ]]; then
        mkdir -p "$stage/$name/licenses"
        (
            cd "$deps"
            find . -maxdepth 4 -type f \( -iname 'license*' -o -iname 'copying*' -o -iname 'notice*' \) \
                -exec cp --parents -- '{}' "$stage/$name/licenses/" \;
        )
    fi
    cat > "$stage/$name/README.txt" <<'EOF'
WiVRn Steam Frame AV1 client (Linux ARM64).
Keep bin, lib and share together. Add bin/wivrn as a native non-Steam game
on the Frame and launch it there through Steam, without Proton.
The bundled lib/ is the OpenXR loader; AV1 hardware decoding uses Iris.
AV1 requires a matching driver exposing AV01 and linear NV12; this client
does not modify or load drivers. This workaround supports 8-bit AV1.
See AV1-QUICKSTART.md in the release for setup and rollback instructions.
EOF
else
    cp "$repo_dir/COPYING" "$stage/$name/COPYING-LOADER"
fi

python3 - "$repo_dir" "$stage/$name/SOURCE.json" "$component" <<'PY'
import json
import subprocess
import sys
from pathlib import Path

root, destination, component = sys.argv[1:]
commit = subprocess.check_output(["git", "-C", root, "rev-parse", "HEAD"], text=True).strip()
description = subprocess.check_output(["git", "-C", root, "describe", "--tags", "--always"], text=True).strip()
Path(destination).write_text(json.dumps({
    "component": component,
    "architecture": "aarch64",
    "source_commit": commit,
    "source_description": description,
    "source_url": f"https://github.com/Patola/WiVRn/tree/{commit}",
    "hardware_tested_by_ci": False,
}, indent=2) + "\n")
PY
(
    cd "$stage/$name"
    rm -f SHA256SUMS
    find . -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum > "$stage/checksums"
    mv "$stage/checksums" SHA256SUMS
    sha256sum --check --quiet SHA256SUMS
)
tar -cJf "$output/$name.tar.xz" -C "$stage" "$name"
printf '%s\n' "$output/$name.tar.xz"
