# WiVRn Vulkan AV1 on Steam Frame: setup guide

Use the **CI-built packages** for the standard setup. The workflow builds and
publishes a matching Linux PC server, ARM64 client and patched Iris module.
Follow the installation steps below, then launch and connect.

For local builds, follow [Local compilation (alternative)](steam-frame-av1-building.md).
That guide retains the complete clone, dependency, server, ARM64 client and
module build instructions, plus deployment and restoration.

## Download the CI-built packages

Get a **Steam Frame AV1 build** from
[Patola/WiVRn Releases](https://github.com/Patola/WiVRn/releases).
Download all components from **the same release**, plus `SHA256SUMS`:

| Download | Where it runs |
| --- | --- |
| `wivrn-server-x86_64.flatpak` | Linux PC: server, dashboard and OpenXR runtime |
| `wivrn-frame-av1-arm64.tar.xz` | Steam Frame: native ARM64 client |
| `iris-av1-7.2.0-g344f27110f05-aarch64.tar.xz` | Steam Frame: temporary AV1 decoder module and guarded loader |

The server requires Flatpak with the KDE 6.11 runtime and a GPU/driver with
Vulkan Video AV1 **encoding** support. Flatpak installs the required runtime;
it does not add unsupported GPU capabilities. The Frame workaround uses
**8-bit AV1**, with hardware decoding through Iris and linear NV12 output.

The module is only for **aarch64 kernel `7.2.0-g344f27110f05`** with the matching
configuration fingerprint. The loader verifies both. Do not force it onto
another kernel. If a newer stock driver already supports AV1, reassess the
workaround instead of replacing it. CI builds and checks packages; it does
not load modules or perform hardware streaming tests.

## 1. Verify the downloads on the PC

Put the downloaded assets together in a directory, open a terminal there,
and run:

```bash
sha256sum --check --ignore-missing SHA256SUMS
```

All downloaded files must pass. The release's `SOURCE.json` identifies the
WiVRn commit, workflow run and module requirements. The archives also contain
their own checksums and source records. Only use release assets you trust;
checksums detect corruption but are not publisher signatures.

## 2. Install and configure the PC server

Install Flatpak using your distribution's package manager, then:

```bash
flatpak remote-add --user --if-not-exists flathub \
    https://flathub.org/repo/flathub.flatpakrepo
flatpak install --user ./wivrn-server-x86_64.flatpak
```

This build uses Flatpak branch **`av1`**, so it can coexist with the official
`stable` branch. Always specify `--branch=av1` when launching it. Branches of
the same application share configuration and pairing data; do not assume
the experimental dashboard has an isolated settings directory.

Stop any existing WiVRn server before starting this one. For a separate AV1
test configuration, create the following file on the PC. If this test file
already exists, preserve it before replacing it:

```bash
mkdir -p "$HOME/.var/app/io.github.wivrn.wivrn/config/wivrn"
cat > "$HOME/.var/app/io.github.wivrn.wivrn/config/wivrn/frame-av1.json" <<'EOF'
{
    "encoder": {"encoder": "vulkan", "codec": "av1"},
    "bit-depth": 8
}
EOF
```

Launch the server in a PC terminal:

```bash
flatpak run --branch=av1 --command=sh io.github.wivrn.wivrn \
    -c 'exec wivrn-server -f "$XDG_CONFIG_HOME/wivrn/frame-av1.json"'
```

Leave it open for pairing and encoder messages. Allow TCP/UDP 9757 through
your PC firewall from the Frame. A PC VR application still needs to be started
after connection; these packages do not install a VR desktop such as WayVR.
For graphical server controls instead, launch the AV1 dashboard with:

```bash
flatpak run --branch=av1 io.github.wivrn.wivrn
```

Use one server-launch method at a time. The separate test configuration above
is selected only by the terminal command. To use the dashboard, configure its
encoder settings for Vulkan and AV1 there. Existing OpenVR games also need
an API translator; see the full guide for Steam/OpenXR setup.

## 3. Copy and extract the Frame packages

Enable SSH on the Frame. From the PC download directory, replace the example
address with your Frame's IP:

```bash
scp wivrn-frame-av1-arm64.tar.xz \
    iris-av1-7.2.0-g344f27110f05-aarch64.tar.xz \
    steamos@192.168.1.44:~/
ssh steamos@192.168.1.44
```

In that **Frame SSH session**, check the kernel before proceeding:

```bash
uname -r
v4l2-ctl -d /dev/video-dec0 --list-formats-out
```

If the kernel matches and stock Iris still lacks `AV01`, extract into a fresh
location. These commands deliberately refuse to overlay earlier bundles:

```bash
(
set -euo pipefail
mkdir -p "$HOME/mystuff"
test ! -e "$HOME/mystuff/wivrn-frame-av1-arm64"
test ! -e "$HOME/mystuff/iris-av1-7.2.0-g344f27110f05-aarch64"
tar -xJf "$HOME/wivrn-frame-av1-arm64.tar.xz" -C "$HOME/mystuff"
tar -xJf "$HOME/iris-av1-7.2.0-g344f27110f05-aarch64.tar.xz" -C "$HOME/mystuff"
cd "$HOME/mystuff/wivrn-frame-av1-arm64"
sha256sum --check --quiet SHA256SUMS
cd "$HOME/mystuff/iris-av1-7.2.0-g344f27110f05-aarch64"
sha256sum --check SHA256SUMS
)
```

Keep the client's `bin/`, `lib/` and `share/` together. `lib/` is the bundled
OpenXR loader, not a software AV1 decoder. The tar archive preserves the
executable permissions and loader symlinks.

## 4. Load AV1 temporarily

Close WiVRn and video playback on the Frame first, including Steam Store
autoplay videos: `steamwebhelper` can keep the decoder busy. In the SSH session:

```bash
cd "$HOME/mystuff/iris-av1-7.2.0-g344f27110f05-aarch64"
bash iris-av1-session.sh check "$PWD/iris-av1.ko"
sudo bash iris-av1-session.sh load "$PWD/iris-av1.ko"
```

Do not continue if checks fail. Successful output includes `AV01` and `NV12`,
with the installed module file unchanged. The helper stops the Iris rebind
service for this session if it was active. It does not enable/disable boot
services, overwrite installed drivers, replace firmware or update initramfs.
The runtime module is experimental; a hang can require a physical power cycle.

## 5. Add the Frame client to Steam and connect

On the **Frame's Steam desktop interface**, use **Games → Add a Non-Steam
Game → Browse** and select:

```text
/home/steamos/mystuff/wivrn-frame-av1-arm64/bin/wivrn
```

Name the shortcut **WiVRn AV1** and leave forced Proton compatibility off.
Use `/home/steamos/mystuff/wivrn-frame-av1-arm64/bin` as its working directory.
Set launch options to the following, replacing `192.168.1.10` with your
**PC's** LAN address:

```text
WIVRN_LOGLEVEL=debug %command% wivrn://192.168.1.10 > /home/steamos/frame-av1.log 2>&1
```

Launch locally through Steam on the Frame, rather than from a plain SSH shell.
Select AV1 in WiVRn, start at 50 Mbit/s, and pair with the PIN shown on the PC.
If the server already knows other headsets and pairing is disabled, enable it
from a second **PC** terminal:

```bash
flatpak run --branch=av1 --command=wivrnctl io.github.wivrn.wivrn pair --duration 5
```

Start your PC VR application. Then verify the PC log selects `vulkan (av1
8-bit)` and the Frame log shows `V4L2 iris DMA-buf capture` with `NV12 (8-bit)`:

```bash
grep -Ei 'V4L2 iris|NV12|failed|exception|error' "$HOME/frame-av1.log"
```

Raise bitrate gradually once stable. A disabled 72 Hz selector has been seen
while a stream negotiated about 90 Hz; its value is not a measurement of the
active stream rate. Use SteamOS display settings and the logs for diagnosis.

## 6. Restore the original driver

Close WiVRn and video applications. On the Frame:

```bash
sudo bash "$HOME/mystuff/iris-av1-7.2.0-g344f27110f05-aarch64/iris-av1-session.sh" restore
```

This reloads the installed driver and restores the rebind service's previous
active state. Rebooting also restores the stock setup, because no persistent
driver installation was performed. Do not force-load or force-unload modules.
After a SteamOS/kernel update, repeat the kernel checks before using the patch.

## Alternative: compile locally

For the complete local-compilation instructions, VR application setup and
troubleshooting, read [Local compilation (alternative)](steam-frame-av1-building.md)
or `BUILD-GUIDE.md` in the release. It covers cloning the branch and building
all three components before deployment. Use the current source guide rather
than mixing a prebuilt client with an arbitrary server revision.
