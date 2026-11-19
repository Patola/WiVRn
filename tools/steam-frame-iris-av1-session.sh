#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# User-run helper for the PC-built Steam Frame Iris module. No persistent install.
set -euo pipefail
readonly iris_release=7.2.0-g344f27110f05
readonly iris_service=iris-driver-rebind.service
readonly iris_state=/run/wivrn-iris-av1

die() { printf 'Error: %s\n' "$*" >&2; exit 1; }
wait_for_decoder() {
    for _ in {1..50}; do
        [[ -e /dev/video-dec0 ]] && return 0
        sleep 0.1
    done
    die 'Decoder node did not reappear. Use restore, or reboot.'
}
check_module() {
    [[ $(uname -m) == aarch64 && $(uname -r) == "$iris_release" ]] || die "Requires aarch64 kernel $iris_release."
    iris_module=$(realpath -- "$1")
    iris_artifacts=$(dirname -- "$iris_module")
    [[ $(basename -- "$iris_module") == iris-av1.ko ]] || die 'Use iris-av1.ko from the complete extracted module bundle.'
    (cd "$iris_artifacts" && sha256sum -c SHA256SUMS)
    [[ $(modinfo -F name "$iris_module") == iris ]] || die 'Unexpected module name.'
    iris_stock=$(modinfo -n iris)
    [[ -f $iris_stock ]] || die 'Installed Iris module not found.'
    [[ $(modinfo -F vermagic "$iris_module") == "$(modinfo -F vermagic "$iris_stock")" ]] || die 'Module ABI mismatch.'
    iris_config=$(gzip -dc /proc/config.gz | sha256sum)
    [[ ${iris_config%% *} == "$(cat "$iris_artifacts/kernel-config.sha256")" ]] || die 'Running kernel configuration mismatch.'
    [[ -r /sys/module/iris/initstate && $(cat /sys/module/iris/initstate) == live ]] || die 'Expected a live stock Iris driver.'
    [[ $(cat /sys/module/iris/refcnt) == 0 ]] || die 'Iris is busy. Close WiVRn and other video/streaming apps.'
    iris_formats=$(v4l2-ctl -d /dev/video-dec0 --list-formats-out)
    [[ $iris_formats != *AV01* ]] || die 'AV1 is already exposed; do not load this workaround again.'
    printf 'Kernel, configuration, module checksum/ABI, and idle-driver checks passed.\n'
}
recover_load_error() {
    iris_rc=$?
    trap - EXIT INT TERM
    if (( iris_rc != 0 )); then
        dmesg > "$iris_state/dmesg-failure.txt" 2>/dev/null || true
        if (( iris_unloaded && !iris_loaded )) && [[ ! -d /sys/module/iris ]]; then
            if modprobe iris; then iris_unloaded=0; fi
        fi
        if (( !iris_loaded && !iris_unloaded && iris_was_active )); then
            systemctl start "$iris_service" || true
        fi
        printf 'Load/check failed; diagnostics: %s. Use restore or reboot before retrying.\n' "$iris_state" >&2
    fi
    exit "$iris_rc"
}
load_module() {
    check_module "$1"
    [[ ! -e $iris_state ]] || die 'A saved session exists. Run restore or reboot first.'
    install -d -m 700 "$iris_state"
    iris_was_active=0
    if systemctl is-active --quiet "$iris_service"; then iris_was_active=1; fi
    printf '%s\n' "$iris_was_active" > "$iris_state/rebind-was-active"
    sha256sum "$iris_stock" > "$iris_state/stock.sha256"
    install -m 600 "$iris_module" "$iris_state/iris-av1.ko"
    iris_expected=$(awk '$2 == "iris-av1.ko" { print $1 }' "$iris_artifacts/SHA256SUMS")
    [[ $iris_expected =~ ^[[:xdigit:]]{64}$ ]] || die 'Missing module checksum.'
    printf '%s  %s\n' "$iris_expected" "$iris_state/iris-av1.ko" | sha256sum -c -
    iris_unloaded=0
    iris_loaded=0
    trap recover_load_error EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    if (( iris_was_active )); then systemctl stop "$iris_service"; fi
    [[ $(cat /sys/module/iris/refcnt) == 0 ]] || die 'Iris became busy; leaving it loaded.'
    dmesg > "$iris_state/dmesg-before.txt"
    rmmod iris
    iris_unloaded=1
    insmod "$iris_state/iris-av1.ko"
    iris_loaded=1
    wait_for_decoder
    [[ $(cat /sys/module/iris/initstate) == live ]] || die 'Patched Iris did not become live.'
    v4l2-ctl -d /dev/video-dec0 --list-formats-out > "$iris_state/formats.txt"
    cat "$iris_state/formats.txt"
    grep -q AV01 "$iris_state/formats.txt" || die 'Patched driver did not expose AV1.'
    v4l2-ctl -d /dev/video-dec0 --set-fmt-video-out=width=1280,height=720,pixelformat=AV01 --list-formats > "$iris_state/av1-capture-formats.txt"
    cat "$iris_state/av1-capture-formats.txt"
    grep -q "'NV12'" "$iris_state/av1-capture-formats.txt" || die 'AV1 linear NV12 is unavailable.'
    sha256sum -c "$iris_state/stock.sha256"
    dmesg > "$iris_state/dmesg-after.txt"
    trap - EXIT INT TERM
    printf 'Temporary Iris AV1 is loaded. Installed module file is unchanged.\n'
    printf 'Restore with: sudo bash %q restore\n' "$0"
}
restore_module() {
    [[ -r $iris_state/stock.sha256 && -r $iris_state/rebind-was-active ]] || die 'No saved session. A reboot restores the installed driver.'
    [[ $(uname -r) == "$iris_release" ]] || die 'Kernel changed. Reboot to restore its installed driver.'
    sha256sum -c "$iris_state/stock.sha256"
    if [[ -d /sys/module/iris ]]; then
        [[ $(cat /sys/module/iris/refcnt) == 0 ]] || die 'Iris is busy. Close WiVRn and other video/streaming apps; do not force unloading.'
        rmmod iris
    fi
    modprobe iris
    wait_for_decoder
    [[ $(cat /sys/module/iris/initstate) == live ]] || die 'Installed Iris did not become live.'
    if [[ $(cat "$iris_state/rebind-was-active") == 1 ]]; then systemctl start "$iris_service"; fi
    sha256sum -c "$iris_state/stock.sha256"
    v4l2-ctl -d /dev/video-dec0 --list-formats-out
    iris_archive="$iris_state.restored.$(date +%s).$$"
    mv -- "$iris_state" "$iris_archive"
    printf 'Installed Iris and the prior rebind-service state restored. Diagnostics: %s\n' "$iris_archive"
}
case ${1:-} in
    check)
        [[ $# == 2 ]] || die 'Usage: bash iris-av1-session.sh check /path/to/iris-av1.ko'
        check_module "$2"
        ;;
    load|restore)
        [[ $EUID == 0 ]] || die 'Use sudo for load/restore.'
        exec 9>/run/wivrn-iris-av1.lock
        flock -n 9 || die 'Another Iris load/restore operation is running.'
        if [[ $1 == load ]]; then
            [[ $# == 2 ]] || die 'Usage: sudo bash iris-av1-session.sh load /path/to/iris-av1.ko'
            load_module "$2"
        else
            [[ $# == 1 ]] || die 'Usage: sudo bash iris-av1-session.sh restore'
            restore_module
        fi
        ;;
    *)
        printf 'Usage: bash %s check MODULE\n       sudo bash %s load MODULE\n       sudo bash %s restore\n' "$0" "$0" "$0"
        exit 1
        ;;
esac
