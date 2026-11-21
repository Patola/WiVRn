# WiVRn Vulkan AV1 on Steam Frame: setup guide

Use the **CI-built packages** for the standard setup. The workflow builds and
publishes a matching Linux PC server and ARM64 client.
Follow the installation steps below, then launch and connect.

**Use SteamOS's stock AV1-capable driver.** SteamOS **0.5.10**, build
`20261118.6185655`, kernel **`7.2.0-g04c7e99cb73a`**, was tested with native
`qcom_iris`: it exposes `AV01` and linear NV12, and this client streamed AV1
successfully with the stock driver. The custom kernel module is retired:
it can conflict with Steam Link on current SteamOS. Older releases may still
contain it; do not load it. If you previously loaded our temporary module,
reboot the Frame to return to the stock driver and stop using its old loader.
Update older SteamOS installations to a version with native AV1 support.

For local builds, follow [Local compilation (alternative)](steam-frame-av1-building.md).
That guide retains the complete clone, dependency, server and ARM64 client
build instructions, plus deployment.

## Download the CI-built packages

Get a **Steam Frame AV1 build** from
[Patola/WiVRn Releases](https://github.com/Patola/WiVRn/releases).
Download the server and client from **the same release**, plus `SHA256SUMS`.

| Download | Where it runs |
| --- | --- |
| `wivrn-server-x86_64.flatpak` | Linux PC: server, dashboard and OpenXR runtime |
| `wivrn-frame-av1-arm64.tar.xz` | Steam Frame: native ARM64 client |

The server requires Flatpak with the KDE 6.11 runtime and a GPU/driver with
Vulkan Video AV1 **encoding** support. Flatpak installs the required runtime;
it does not add unsupported GPU capabilities. The Frame client uses
**8-bit AV1**, with hardware decoding through Iris and linear NV12 output.

The stock Frame driver must expose `AV01` and linear NV12 output. CI builds
and checks the server/client packages; it does not perform hardware streaming
tests.

## 1. Verify the downloads on the PC

Put the downloaded assets together in a directory, open a terminal there,
and run:

```bash
sha256sum --check --ignore-missing SHA256SUMS
```

All downloaded files must pass. The release's `SOURCE.json` identifies the
WiVRn commit and workflow run. The client archive also contains
its own checksums and source records. Only use release assets you trust;
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

## 3. Copy and extract the Frame client

Enable SSH on the Frame. From the PC download directory, replace the example
address with your Frame's IP:

```bash
scp wivrn-frame-av1-arm64.tar.xz steamos@192.168.1.44:~/
```

Open an SSH session to check the driver and extract the client:

```bash
ssh steamos@192.168.1.44
```

In that **Frame SSH session**, check the stock decoder before proceeding:

```bash
uname -r
v4l2-ctl -d /dev/video-dec0 --list-formats-out
```

Extract the client into a fresh location. These commands deliberately refuse
to overlay an earlier bundle:

```bash
(
set -euo pipefail
mkdir -p "$HOME/mystuff"
test ! -e "$HOME/mystuff/wivrn-frame-av1-arm64"
tar -xJf "$HOME/wivrn-frame-av1-arm64.tar.xz" -C "$HOME/mystuff"
cd "$HOME/mystuff/wivrn-frame-av1-arm64"
sha256sum --check --quiet SHA256SUMS
)
```

Keep the client's `bin/`, `lib/` and `share/` together. `lib/` is the bundled
OpenXR loader, not a software AV1 decoder. The tar archive preserves the
executable permissions and loader symlinks.

## 4. Add the Frame client to Steam and connect

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

Raise bitrate gradually once stable.

### Set the Frame's per-application refresh rate

SteamVR defaults native Frame applications to **72 Hz**, independently of
the Steam Link streaming refresh-rate setting. With WiVRn running, open
**VR Settings → Video → Per-Application Video Settings**, select the WiVRn
shortcut, and set **Frame Limit / Refresh Rate Override** to **90 Hz** with
no throttling. Reconnect WiVRn after applying the override.
See [Valve's per-application settings documentation](https://partner.steamgames.com/doc/steamhardware/steamframe/vrpreferences).

On the tested runtime WiVRn cannot directly change this system-managed rate;
requests can fail with `XR_ERROR_DISPLAY_REFRESH_RATE_UNSUPPORTED_FB`.
A negotiated stream rate of about 90 Hz alone does not prove the panel runs
at 90 Hz: the compositor was observed switching to 72 Hz on WiVRn launch.
Check the latest display-mode changes on the Frame:

```bash
grep -E 'Selected display mode|Prop_DisplayFrequency_Float' \
    "$HOME/.local/share/Steam/logs/vrcompositor.txt" | tail -n 12
```

Look for the mode selected after launching WiVRn, rather than a 90 Hz entry
from the idle dashboard before launch.

## Alternative: compile locally

For the complete local-compilation instructions, VR application setup and
troubleshooting, read [Local compilation (alternative)](steam-frame-av1-building.md)
or `BUILD-GUIDE.md` in the release. It covers cloning the branch and building
the server and client before deployment. Use the current source guide rather
than mixing a prebuilt client with an arbitrary server revision.
