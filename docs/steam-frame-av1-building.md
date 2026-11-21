# Steam Frame AV1: local compilation (alternative)

This guide builds Patola's WiVRn branch on an **x86-64 Linux PC**, copies a
Linux ARM64 client to a Steam Frame, and connects them over the network.
The server uses Vulkan Video AV1 encoding;
the Frame uses its Qualcomm hardware decoder through V4L2/Iris.

Start with the [CI-built package setup guide](https://github.com/Patola/WiVRn/blob/codex/quest-v29-latency/docs/steam-frame-av1-guide.md)
for ready-to-use downloads. Follow the instructions below when you want to
compile the server and ARM64 client locally instead.

There are two separate builds:

| Build | Runs on | Purpose |
| --- | --- | --- |
| WiVRn server and OpenXR runtime | Linux PC, x86-64 | Render VR applications and encode AV1 |
| WiVRn client | Steam Frame, ARM64 | Receive the stream and display VR |

The client bundle includes `bin/`, `lib/` and `share/`. Its `lib/` contains
the OpenXR loader, not a software AV1 decoder. Keep all three directories.

Navigation: [PC server](#5-compile-and-install-the-pc-server),
[ARM64 client](#6-compile-the-arm64-client-on-the-pc),
[Steam shortcut](#9-add-the-client-as-a-non-steam-game-on-the-frame),
[connection](#11-launch-on-the-frame-pair-and-connect).

## 1. Check whether your Frame matches the tested system

SteamOS **0.5.10**, build `20261118.6185655`, kernel
**`7.2.0-g04c7e99cb73a`**, now has native AV1 support through `qcom_iris`.
This client has streamed AV1 successfully with that stock driver. Use an
AV1-capable SteamOS release and keep its stock driver. The custom module is
retired because it can conflict with Steam Link on current SteamOS; this
branch no longer builds, loads or packages it. If you previously loaded our
temporary module, reboot the Frame and stop using the old loader.

You also need a PC GPU and driver that support **Vulkan Video AV1 encoding**.
AV1 decoding support on a GPU does not imply AV1 encoding support. The tested
PC used an RX 7900 XTX with RADV; other drivers must support the required
encode features and queues. The server checks these when a stream starts.

## 2. Set up SSH and note both IP addresses

Use the Frame's desktop terminal to set a password for its `steamos` account
if necessary, then start SSH:

**On the Frame:**

```bash
passwd
sudo systemctl start sshd
ip -br address
```

If SSH is already configured, keep your existing setup. Enabling SSH through
the Frame's settings is also sufficient. No root filesystem unlock is needed.

**On the PC, in a Bash terminal**, substitute your Frame's address:

```bash
export FRAME_HOST=192.168.1.44
export FRAME_USER=steamos
ssh "$FRAME_USER@$FRAME_HOST"
```

**In that SSH session**, run these read-only checks:

```bash
uname -m
uname -r
modinfo -n qcom_iris
v4l2-ctl -d /dev/video-dec0 --list-formats-out
```

Check for architecture `aarch64` and coded format `AV01`. The stock driver
also needs linear NV12 capture for AV1. If AV1 is absent, update SteamOS
before continuing; do not load an old custom module.

Return to the PC terminal:

```bash
exit
ip -br address
```

Note the PC's LAN address too. This guide uses **`192.168.1.10`** for the PC
in the Steam shortcut. Replace it with yours. Connect the PC by Ethernet to
the router and the Frame to a suitable Wi-Fi network. USB is not required.

## 3. Install PC build dependencies

Allow roughly **30 GB of free space** for SDK images, an extracted ARM64 SDK,
dependencies and build files. The client is cross-compiled inside an x86-64
SDK container; no ARM emulator or privileged binfmt container is required.

Use GCC 14 or newer with C++23 support, CMake 3.25 or newer, and Vulkan
headers **1.4.304 or newer**. Older distributions such as a stock Ubuntu 24.04
installation need newer build dependencies before these commands will work.

### Arch Linux

```bash
sudo pacman -S --needed base-devel git cmake ninja pkgconf curl openssh \
    podman xz libcap boost boost-libs systemd-libs vulkan-headers vulkan-icd-loader \
    vulkan-tools glslang spirv-tools ffmpeg libva x264 avahi eigen \
    nlohmann-json cli11 glib2 libnotify libarchive libpng librsvg \
    pipewire libdrm libusb libx11 libxrandr libxcb wayland \
    wayland-protocols libxkbcommon libglvnd sdl2
```

Install the Vulkan driver appropriate for your GPU as well, for example
`vulkan-radeon` for AMD. Do not replace a working GPU driver indiscriminately.

### Recent Ubuntu/Debian with the required compiler and Vulkan headers

```bash
sudo apt update
sudo apt install build-essential git cmake ninja-build pkg-config curl \
    openssh-client podman xz-utils libcap2-bin libsystemd-dev \
    libboost-all-dev libvulkan-dev vulkan-tools glslang-tools spirv-tools \
    libavcodec-dev libavutil-dev libswscale-dev libavfilter-dev \
    libva-dev libx264-dev libavahi-client-dev libavahi-glib-dev \
    avahi-daemon avahi-utils libeigen3-dev nlohmann-json3-dev libcli11-dev \
    libglib2.0-dev libglib2.0-dev-bin libnotify-dev libarchive-dev libpng-dev \
    librsvg2-dev libpipewire-0.3-dev libdrm-dev libudev-dev libusb-1.0-0-dev \
    libx11-dev libx11-xcb-dev libxrandr-dev libxcb-randr0-dev \
    libwayland-dev wayland-protocols libxkbcommon-dev libgl-dev libglx-dev \
    libsdl2-dev
```

Check the tools and Vulkan device:

```bash
c++ --version
cmake --version
podman info
vulkaninfo --summary
vulkaninfo | grep -E 'VK_KHR_video_(queue|encode_queue|encode_av1)'
```

The extension listing is an initial check; the server still needs compatible
features, queue families and codec capabilities. `podman info` must work as
your normal user. Resolve any distribution-specific rootless Podman setup
errors before continuing.

## 4. Clone the custom branch

**On the PC:**

```bash
mkdir -p "$HOME/src"
cd "$HOME/src"
git clone --single-branch --branch codex/quest-v29-latency \
    https://github.com/Patola/WiVRn.git WiVRn-frame-av1
cd WiVRn-frame-av1
export WIVRN_SOURCE="$PWD"
git rev-parse HEAD
```

Build both WiVRn executables from this checkout. Do not mix an old official
client or server with an arbitrary development revision.

Keep this PC terminal open. Subsequent PC commands assume this checkout is
the current directory, unless a step explicitly changes directories. If you
open another terminal, return to it and set `WIVRN_SOURCE="$PWD"` again.

## 5. Compile and install the PC server

This installs into a separate user-owned prefix and leaves distribution
packages and existing Envision prefixes alone:

```bash
cmake --preset server -B build-frame-server \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
    -DCMAKE_INSTALL_PREFIX="$HOME/.local/share/wivrn-av1" \
    -DWIVRN_BUILD_DASHBOARD=OFF \
    -DWIVRN_USE_SYSTEM_BOOST=ON \
    -DWIVRN_USE_SYSTEM_OPENXR=OFF \
    -DWIVRN_USE_VULKAN_ENCODE=ON \
    -DWIVRN_WERROR=OFF

cmake --build build-frame-server --parallel 8
cmake --install build-frame-server
sudo setcap cap_sys_nice=eip \
    "$HOME/.local/share/wivrn-av1/bin/wivrn-server"

"$HOME/.local/share/wivrn-av1/bin/wivrn-server" --version
getcap "$HOME/.local/share/wivrn-av1/bin/wivrn-server"
```

Stop if configuration fails; a failed configure does not produce a usable
`build.ninja`. The system Boost option avoids the bundled Boost.Locale
configuration failure seen with CMake 4.5 on the tested PC.

Build **all targets**, including the matching OpenXR runtime library. Building
only `wivrn-server` after changing revisions can leave a mismatched library
and prevent VR applications from starting. Run `setcap` again after replacing
the installed server executable. Run the server as your normal user.

## 6. Compile the ARM64 client on the PC

Two pinned SDK images are used: ARM64 supplies target headers and libraries;
x86-64 runs the cross-compiler and asset-generation tools. These are the SDK
versions used for the successful Frame build, rather than your PC's native
ARM cross-library versions.

### Extract the ARM64 SDK without executing it

```bash
export WIVRN_FRAME_SDK=registry.gitlab.steamos.cloud/steamrt/steamrt4/sdk/arm64:4.0.20260805.254769
export WIVRN_HOST_SDK=registry.gitlab.steamos.cloud/proton/steamrt4/sdk/x86_64:4.0.20260331.220802-0

podman pull --platform linux/arm64 "$WIVRN_FRAME_SDK"
podman pull --platform linux/amd64 "$WIVRN_HOST_SDK"

(
set -euo pipefail
test ! -e build-frame-sdk-root
mkdir build-frame-sdk-root
wivrn_sdk_container=$(podman create --platform linux/arm64 \
    "$WIVRN_FRAME_SDK" /bin/true)
podman export "$wivrn_sdk_container" | \
    tar --no-same-owner -xf - -C build-frame-sdk-root
podman rm "$wivrn_sdk_container"
)
```

Do this in a new SDK directory; do not overlay it with an older extraction.
Creating and exporting the ARM64 container does not run its ARM programs.

### Run the x86-64 build container

This installs tools **inside the disposable container**, not on the Frame.
Rootless Podman's container root maps to your PC user. The resulting files
remain in your checkout after the container exits.

```bash
podman run --rm --user 0 --security-opt label=disable \
    -v "$WIVRN_SOURCE:/src" \
    -v "$WIVRN_SOURCE/build-frame-sdk-root:/sdk:ro" \
    -e WIVRN_STEAM_FRAME_SYSROOT=/sdk \
    -w /src "$WIVRN_HOST_SDK" bash -lc '
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu librsvg2-bin
curl --fail --location --retry 3 \
    https://github.com/KhronosGroup/KTX-Software/releases/download/v4.4.2/KTX-Software-4.4.2-Linux-x86_64.deb \
    -o /tmp/ktx-host.deb
apt-get install -y /tmp/ktx-host.deb
git config --global --add safe.directory /src

cmake --preset frame -B build-frame-client \
    -DCMAKE_TOOLCHAIN_FILE=/src/cmake/toolchains/SteamFrame-aarch64.cmake \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
    -DWIVRN_COMPRESS_GLB=OFF
cmake --build build-frame-client --parallel 8
DESTDIR=/src/build-frame-client/install cmake --install build-frame-client
'
```

Disabling GLB compression removes the npm/gltf-transform build dependency;
it does not disable video compression. KTX is still needed for asset creation.

Check the output on the PC:

```bash
file build-frame-client/install/usr/bin/wivrn
ls -l build-frame-client/install/usr/lib/libopenxr_loader.so*
test -d build-frame-client/install/usr/share/wivrn/assets
```

`file` should identify an **ARM aarch64** Linux executable. This is not an APK.
Do not try to run it directly on the x86-64 PC.

## 7. Package and transfer the client

Package the complete client tree:

```bash
mkdir -p build-frame-packages
git rev-parse HEAD > build-frame-client/install/usr/WIVRN-SOURCE-REVISION
tar -cJf build-frame-packages/wivrn-frame-client.tar.xz \
    -C build-frame-client/install/usr .
(
    cd build-frame-packages
    sha256sum wivrn-frame-client.tar.xz > SHA256SUMS
)

ssh "$FRAME_USER@$FRAME_HOST" 'mkdir -p "$HOME/mystuff/wivrn-av1-downloads"'
scp build-frame-packages/wivrn-frame-client.tar.xz \
    build-frame-packages/SHA256SUMS \
    "$FRAME_USER@$FRAME_HOST:~/mystuff/wivrn-av1-downloads/"
```

These manifests detect accidental corruption; they are not publisher
signatures.

## 8. Extract the client on the Frame

**From the PC:**

```bash
ssh "$FRAME_USER@$FRAME_HOST"
```

**In the Frame SSH session:**

```bash
(
set -euo pipefail
cd "$HOME/mystuff/wivrn-av1-downloads"
sha256sum --check SHA256SUMS

test ! -e "$HOME/mystuff/wivrn-av1"
mkdir -p "$HOME/mystuff/wivrn-av1"
tar -xJf wivrn-frame-client.tar.xz -C "$HOME/mystuff/wivrn-av1"

file "$HOME/mystuff/wivrn-av1/bin/wivrn"
cat "$HOME/mystuff/wivrn-av1/WIVRN-SOURCE-REVISION"
)
```

If `test ! -e` fails, stop: a previous test directory exists. Close its
client and preserve or rename the directory before extracting a replacement.
Do not merge different builds' `bin/`, `lib/` and `share/` directories. The
separate `wivrn-av1` path lets you keep a regular client alongside this one.

## 9. Add the client as a non-Steam game on the Frame

Use the Steam desktop interface **on the Frame**:

1. Open **Games → Add a Non-Steam Game to My Library**.
2. Select **Browse**, enable an all-files filter if necessary, and choose
   `/home/steamos/mystuff/wivrn-av1/bin/wivrn`.
3. Click **Add Selected Programs**.
4. Open the shortcut's **Properties** and rename it **WiVRn AV1**.
5. Set the target and working directory as follows:

   | Field | Value |
   | --- | --- |
   | Target | `"/home/steamos/mystuff/wivrn-av1/bin/wivrn"` |
   | Start In | `"/home/steamos/mystuff/wivrn-av1/bin"` |

6. Leave forced Proton/Windows compatibility **off**: this is a native ARM64
   Linux application. If a VR-library visibility option is available, enable
   it so the shortcut appears in the headset's VR library.
7. Set **Launch Options** to this one line, replacing `192.168.1.10` with
   **your PC's LAN address**:

   ```text
   WIVRN_LOGLEVEL=debug %command% wivrn://192.168.1.10 > /home/steamos/frame-av1.log 2>&1
   ```

Use your own home path if your Frame account is not `steamos`. The redirection
keeps the test log at a short path; each launch replaces that log. Save it
before another launch if you want to retain a reproduction.

Launch this shortcut locally on the Frame through Steam. Running the binary
from an ordinary SSH shell can lack the OpenXR runtime and Steam launch
environment. Do not launch a PC copy or select Steam Link streaming instead.

Valve documents the basic shortcut procedure in
[Add Non-Steam Games to a Library](https://help.steampowered.com/en/faqs/view/4B8B-9697-2338-40EC).

The client uses the stock SteamOS decoder; no driver swap is needed.

## 10. Configure and start the PC server

**Back in a PC terminal**, create a separate test configuration. This preserves
your normal `config.json`. An existing test configuration is backed up first.

```bash
mkdir -p "$HOME/.config/wivrn"
if test -f "$HOME/.config/wivrn/frame-av1.json"; then
    cp -p "$HOME/.config/wivrn/frame-av1.json" \
        "$HOME/.config/wivrn/frame-av1.json.$(date +%Y%m%d-%H%M%S).bak"
fi
cat > "$HOME/.config/wivrn/frame-av1.json" <<'WIVRN_CONFIG'
{
    "encoder": {
        "encoder": "vulkan",
        "codec": "av1"
    },
    "bit-depth": 8
}
WIVRN_CONFIG
```

The Frame client currently supports **8-bit AV1/NV12**. Do not force
10-bit AV1 for this test. Vulkan AV1 must be selected explicitly.

Stop any other WiVRn server first, including one launched by Envision or a
dashboard. Use that application's normal Stop control. Only one should own
the streaming port. Then start discovery and the new server:

```bash
sudo systemctl start avahi-daemon
"$HOME/.local/share/wivrn-av1/bin/wivrn-server" \
    -f "$HOME/.config/wivrn/frame-av1.json"
```

Leave the server in this terminal so you can see its pairing instructions
and encoder messages. It may wait until the headset connects before starting
the VR runtime and encoder. Allow **TCP and UDP port 9757** from the Frame
through the PC firewall. Automatic discovery also uses mDNS, UDP 5353; the
explicit IP address in the shortcut avoids relying on discovery.

If the server already knows another headset, it may start with new pairing
disabled. Before connecting the Frame for the first time, open a **second PC
terminal** and allow pairing for five minutes:

```bash
"$HOME/.local/share/wivrn-av1/bin/wivrnctl" pair --duration 5
```

This prints the current PIN. Keep the server running in the first terminal.
The command uses the same user's session D-Bus; do not run it with `sudo`.

Do not run the server with `sudo`. It normally manages the user's active
OpenXR runtime for the connection and restores it afterward. The separate
prefix does not prevent this normal runtime-selection behavior.

### Have a VR application ready

WiVRn supplies the runtime and transport; a bare server does not install or
automatically start a VR desktop or game. After connection, start an OpenXR
application on the PC. To select this runtime explicitly for an application:

```bash
XR_RUNTIME_JSON="$HOME/.local/share/wivrn-av1/share/openxr/1/openxr_wivrn.json" \
    /absolute/path/to/your-openxr-application
```

Replace the application path; it is not a command to paste unchanged.
If you already use WayVR, you can add an `application` entry to the test JSON
so it starts on connection, for example:

```json
{
    "encoder": {"encoder": "vulkan", "codec": "av1"},
    "bit-depth": 8,
    "application": ["wayvr"]
}
```

This assumes `wayvr` is installed and on the server's PATH; an absolute path
also works. Restart the test server after editing its configuration.

OpenVR games additionally need an API translator such as xrizer or
OpenComposite. For Steam games, OpenXR also needs to be imported into the
Steam runtime. See the branch's
[OpenVR/Steam notes](https://github.com/Patola/WiVRn/blob/codex/quest-v29-latency/docs/steamvr.md).
These are separate from enabling AV1.

## 11. Launch on the Frame, pair and connect

1. Put on the Frame and start **WiVRn AV1** from its Steam library.
2. Complete initial setup and permissions if prompted.
3. In WiVRn's streaming settings, select **AV1**. Begin at **50 Mbit/s**.
4. Connect to the PC. The shortcut supplies its address; if it does not
   connect automatically, select the server or enter the same LAN address
   in the client UI.
5. On first pairing, follow the PIN instructions shown by the server/client.
   Use the current pairing code, not a code copied from someone else's log.
6. Start your PC VR application or let the configured `application` start it.
7. Verify the image and head/controller response. Once stable, increase the
   bitrate gradually, for example to 100 and then 140 Mbit/s if your network
   sustains it. A high initial bitrate makes diagnosis harder.

The first decoded image can take a little time while the decoder receives
headers, reports its output format and creates capture buffers. Remaining
on a waiting-for-application screen can instead mean no PC VR application
has started; check the server terminal.

## 12. Verify that AV1 hardware decoding is actually in use

The **PC server** should list the selected encoder as something like:

```text
vulkan (av1 8-bit)
```

**In a Frame SSH session:**

```bash
grep -Ei 'Starting WiVRn|V4L2 iris|source change|NV12|refresh rate|failed|exception|error' \
    "$HOME/frame-av1.log"
dmesg -T | tail -n 80
```

Successful logs include:

```text
V4L2 iris decoder source change; configuring DMA-buf CAPTURE queue
V4L2 iris DMA-buf capture: ... NV12 (8-bit) ...
```

Together, an AV1 encoder on the PC, Iris capture on the Frame and a working
image demonstrate the hardware path. Merely seeing `AV01` in a format list
does not test decoding. `dmesg` was readable without `sudo` on the tested
Frame; permissions can differ on other images.

SteamVR defaults native Frame applications to **72 Hz** independently of
the Steam Link streaming refresh-rate setting. With WiVRn running, open
**VR Settings → Video → Per-Application Video Settings**, select the WiVRn
shortcut, and set **Frame Limit / Refresh Rate Override** to **90 Hz** with
no throttling. Reconnect after applying it. See
[Valve's per-application settings documentation](https://partner.steamgames.com/doc/steamhardware/steamframe/vrpreferences).

The tested runtime rejects WiVRn's attempts to change this system-managed
rate with `XR_ERROR_DISPLAY_REFRESH_RATE_UNSUPPORTED_FB`. A stream negotiating
approximately 90 Hz does not establish the panel rate: the compositor was
observed selecting 72 Hz when WiVRn launched. Inspect the latest changes:

```bash
grep -E 'Selected display mode|Prop_DisplayFrequency_Float' \
    "$HOME/.local/share/Steam/logs/vrcompositor.txt" | tail -n 12
```

Use the mode selected after WiVRn launches; the dashboard can run at 90 Hz
before the application starts.

## 13. Later sessions, updates and troubleshooting

- **After a Frame reboot:** launch WiVRn AV1 through Steam. The stock driver
  provides AV1; no module-loading commands are needed.
- **After a SteamOS update:** repeat the read-only decoder checks in step 2
  if AV1 is missing, and completely restart WiVRn to refresh its capabilities.
- **After updating the WiVRn branch:** rebuild the full server/runtime and
  ARM64 client from the same checkout, then transfer the complete client
  tree.
- **`build.ninja` missing:** find the first CMake configure error. Repeat the
  configure command successfully before invoking the build. Do not use
  a shared directory for the native server and ARM client.
- **AV1 is missing in the client:** confirm `AV01` and linear `NV12`, then
  completely quit and relaunch the client. Verify that the Steam
  shortcut points at `wivrn-av1/bin/wivrn` rather than an older installation.
- **Wrong ELF architecture or an OpenXR loader failure:** check that the
  executable is ARM64, its `lib/` is beside `bin/`, and it is launched locally
  by Steam on the Frame. An SSH-only launch is not the normal VR environment.
- **The server is not visible:** confirm only the intended server is running,
  the PC address is correct, and the firewall permits TCP/UDP 9757. Try the
  direct `wivrn://PC_ADDRESS` URI used by the shortcut.
- **Incompatible version:** compare the native startup revisions and check
  which application is actually installed. The same server was verified with
  this Frame client and a matching Quest APK. In Envision, **Build client APK**
  must be followed by **Install locally built APK**; the GitHub release
  installer updates a different package from WiVRn Local.
- **Blank VR view after connection:** start a PC VR application and check its
  OpenXR runtime/translator setup. Verify the server and runtime library were
  built together. An installed WayVR is optional, not bundled by these steps.
- **Severe lag:** lower bitrate and reconnect; inspect missing-shard messages
  and network throughput. The original severe-lag incident was resolved by
  restarting the router. Brief hiccups occurred with multiple codecs; the
  collected logs do not establish one universal cause.

## Sources and tested scope

The ARM64 SDK tag and client build preset come from this branch's
[Steam Frame CI job](https://github.com/Patola/WiVRn/blob/codex/quest-v29-latency/.github/workflows/Build.yml).
Additional implementation details and the November 16 gameplay/Quest tests
are recorded in
[Steam Frame AV1 notes](https://github.com/Patola/WiVRn/blob/codex/quest-v29-latency/docs/steam-frame-av1.md).

The fresh build checks for this guide were performed on the PC. They do not
extend the hardware test results to other kernels, firmware, GPUs or networks.
