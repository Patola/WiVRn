#!/usr/bin/env bash
set -euo pipefail

# Personal build: keep the store and WiVRn Local installations separate.
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo_dir"

export JAVA_HOME="${WIVRN_JAVA_HOME:-/usr/lib/jvm/java-17-openjdk}"
export ANDROID_HOME="${ANDROID_HOME:-$HOME/.local/share/envision/android-sdk}"
key_dir="${WIVRN_SIGNING_DIR:-$HOME/.local/share/envision/android}"
apksigner="$ANDROID_HOME/build-tools/35.0.0/apksigner"
output_dir="$repo_dir/build-quest-diagnostics"

for required in "$JAVA_HOME/bin/java" "$apksigner" "$key_dir/ks.keystore" "$key_dir/ks.password"; do
	if [[ ! -f "$required" ]]; then
		printf 'Missing build prerequisite: %s\n' "$required" >&2
		exit 1
	fi
done

mkdir -p "$output_dir"
cmake -S . -B build-quest-server -G Ninja \
	-DCMAKE_BUILD_TYPE=RelWithDebInfo \
	-DWIVRN_BUILD_CLIENT=OFF -DWIVRN_BUILD_DASHBOARD=OFF \
	-DWIVRN_BUILD_WIVRNCTL=ON -DWIVRN_FEATURE_STEAMVR_LIGHTHOUSE=OFF \
	-DCMAKE_INSTALL_PREFIX="$repo_dir/build-quest-install"
cmake --build build-quest-server -j "${WIVRN_BUILD_JOBS:-8}"

./gradlew assembleRelease -Psuffix=.questdiag \
	'-Pwivrn_app_name=WiVRn Quest Diagnostics' --console=plain
"$apksigner" sign --ks "$key_dir/ks.keystore" \
	--ks-pass "file:$key_dir/ks.password" \
	--out "$output_dir/WiVRn-quest-diagnostics.apk" \
	build/outputs/apk/release/WiVRn-release-unsigned.apk
"$apksigner" verify "$output_dir/WiVRn-quest-diagnostics.apk"

printf '\nServer: %s\nAPK: %s\n' \
	"$repo_dir/build-quest-server/server/wivrn-server" \
	"$output_dir/WiVRn-quest-diagnostics.apk"
