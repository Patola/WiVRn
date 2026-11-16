#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build a separate, temporary Iris AV1 module on the PC. Never contacts a headset.
set -euo pipefail

kernel_release=7.2.0-g344f27110f05
kernel_commit=344f27110f059d0520c9dbbafc503d4bbc6c1f68
kernel_url=${WIVRN_FRAME_KERNEL_SOURCE:-https://github.com/evlaV/deckard-linux-no-oopsies.git}
poc_commit=c2df9372d76a8ffbcf6c987ef980f9acf35d8e53
poc_url=${WIVRN_FRAME_IRIS_POC_SOURCE:-https://github.com/mmis1000/steamframe-iris-av1-poc.git}
patch_sha=d55ac87c73a3fbe4f6c9568ed0cbf04a39b2bdac09b7159b1431a485ae4fbdef
headers_url=https://steamdeck-packages.steamos.cloud/archlinux-deckard-hotfixes/release/0.5.x/linux-72-deckard-headers-7.2.0%2Bg344f27110f05-1-aarch64.pkg.tar.zst
headers_sha=56b3f7a22f4a656bdfcd3ad6adbe88f9b85544650d12aabb69aee30d96ae752c
config_sha=55a25adc47f0692b0aab337cf30b339a6b6ba0a899d30be05b83914b0754d4d0
gcc_url=https://mirrors.edge.kernel.org/pub/tools/crosstool/files/bin/x86_64/15.1.0/x86_64-gcc-15.1.0-nolibc-aarch64-linux.tar.xz
gcc_sha=b36edb14d5fe6942dadeee2146ab278eab76924c0a7746b1e20b3b0afcc9be73

die() { printf '%s\n' "$*" >&2; exit 1; }
if [[ $# != 1 || $1 == --help ]]; then
    printf 'Usage: %s NEW_BUILD_DIRECTORY\n' "$0"
    printf 'Builds Iris AV1 for SteamOS 0.5.9, kernel %s, on an x86-64 Linux PC.\n' "$kernel_release"
    printf 'Does not install, load, copy to, or change anything on a headset.\n'
    exit 0
fi
[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || die 'Requires an x86-64 Linux build host.'
[[ ! -e $1 && $1 != *[[:space:]]* ]] || die 'Choose a new build directory without whitespace.'
for tool in git curl gcc make ld modinfo tar zstd xz sha256sum realpath; do
    command -v "$tool" >/dev/null || die "Missing build tool: $tool"
done
mkdir -p -- "$1"
work=$(realpath -- "$1")
[[ $work != *[[:space:]]* ]] || die 'The resolved build directory contains whitespace.'
exec > >(tee "$work/build.log") 2>&1

fetch() {
    local url=$1 cached=$2 destination=$3 expected=$4
    if [[ -n $cached ]]; then
        cp -- "$cached" "$destination"
    else
        curl --fail --location --connect-timeout 20 --retry 3 --output "$destination" "$url"
    fi
    printf '%s  %s\n' "$expected" "$destination" | sha256sum -c -
}
fetch "$headers_url" "${WIVRN_FRAME_HEADERS_ARCHIVE:-}" "$work/headers.pkg.tar.zst" "$headers_sha"
fetch "$gcc_url" "${WIVRN_FRAME_GCC_ARCHIVE:-}" "$work/gcc.tar.xz" "$gcc_sha"
mkdir "$work/headers" "$work/toolchain" "$work/artifacts"
tar --zstd -xf "$work/headers.pkg.tar.zst" -C "$work/headers"
tar -xJf "$work/gcc.tar.xz" -C "$work/toolchain"
export PATH="$work/toolchain/gcc-15.1.0-nolibc/aarch64-linux/bin:$PATH"

git init -q "$work/poc"
git -C "$work/poc" fetch --depth=1 -- "$poc_url" "$poc_commit"
git -C "$work/poc" checkout -q --detach FETCH_HEAD
[[ $(git -C "$work/poc" rev-parse HEAD) == "$poc_commit" ]] || die 'Unexpected Iris patch revision.'
printf '%s  %s\n' "$patch_sha" "$work/poc/iris-av1.patch" | sha256sum -c -

git init -q "$work/source"
git -C "$work/source" sparse-checkout init --cone
git -C "$work/source" sparse-checkout set drivers/media/platform/qcom/vcodec/iris
git -C "$work/source" fetch --depth=1 -- "$kernel_url" "$kernel_commit"
git -C "$work/source" checkout -q --detach FETCH_HEAD
[[ $(git -C "$work/source" rev-parse HEAD) == "$kernel_commit" ]] || die 'Unexpected kernel source revision.'
git -C "$work/source" apply --check "$work/poc/iris-av1.patch"
git -C "$work/source" apply "$work/poc/iris-av1.patch"

headers="$work/headers/usr/lib/modules/$kernel_release/build"
driver="$work/source/drivers/media/platform/qcom/vcodec/iris"
[[ $(cat "$headers/include/config/kernel.release") == "$kernel_release" ]] || die 'Kernel header release mismatch.'
printf '%s  %s\n' "$config_sha" "$headers/.config" | sha256sum -c -
sha256sum "$headers/.config" "$headers/Module.symvers" > "$work/header-inputs.sha256"

# Valve's headers contain ARM host executables. Rebuild the required tools
# for the PC, retaining the original target configuration and symbol CRCs.
gcc -O2 -I "$headers/scripts/include" "$headers/scripts/basic/fixdep.c" -o "$headers/scripts/basic/fixdep"
gcc -O2 -I "$headers/scripts/include" -I "$headers/scripts/mod" \
    "$headers/scripts/mod/modpost.c" "$headers/scripts/mod/file2alias.c" \
    "$headers/scripts/mod/sumversion.c" "$headers/scripts/mod/symsearch.c" \
    -o "$headers/scripts/mod/modpost"
mkdir -p "$headers/drivers/media/platform/qcom/vcodec/iris"
cp "$driver/trace.h" "$headers/drivers/media/platform/qcom/vcodec/iris/trace.h"
if [[ -f $headers/vmlinux ]]; then mv "$headers/vmlinux" "$headers/vmlinux.reference"; fi
make -j"${WIVRN_BUILD_JOBS:-8}" -C "$headers" ARCH=arm64 CROSS_COMPILE=aarch64-linux- M="$driver" modules
sha256sum -c "$work/header-inputs.sha256"
[[ $(modinfo -F name "$driver/iris.ko") == iris ]] || die 'Unexpected module name.'
vermagic=$(modinfo -F vermagic "$driver/iris.ko")
[[ $vermagic == "$kernel_release SMP preempt mod_unload aarch64" ]] || die 'Unexpected module ABI.'

cp "$driver/iris.ko" "$work/artifacts/iris-av1.ko"
cp "$work/poc/iris-av1.patch" "$work/artifacts/"
printf '%s\n' "$config_sha" > "$work/artifacts/kernel-config.sha256"
printf 'kernel=%s\nsource_commit=%s\npoc_commit=%s\ncompiler=%s\nvermagic=%s\n' \
    "$kernel_release" "$kernel_commit" "$poc_commit" "$(aarch64-linux-gcc -dumpfullversion)" "$vermagic" \
    > "$work/artifacts/build-info.txt"
(
    cd "$work/artifacts"
    sha256sum iris-av1.ko iris-av1.patch kernel-config.sha256 build-info.txt > SHA256SUMS
)
printf '\nBuilt %s/artifacts/iris-av1.ko. No headset was contacted or changed.\n' "$work"
