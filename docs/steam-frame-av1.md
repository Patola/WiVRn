# Steam Frame AV1 hardware decoding

Start with the [CI-built package setup guide](steam-frame-av1-guide.md) for
downloads, deployment, the Steam shortcut and connection.
For cloning and building the server and ARM64 client yourself, use
[Local compilation (alternative)](steam-frame-av1-building.md).
This page records implementation details and test results.

This work stays on `codex/quest-v29-latency`. The branch uses upstream's
Vulkan AV1 encoder and retains the Frame AV1 decoder and VAAPI reconfiguration
fix. See [the synchronization notes](vulkan-av1.md#source-and-changes).

The local Envision profile uses `WIVRN_USE_SYSTEM_BOOST=ON`. With bundled
Boost, use a revision that includes upstream's `dd472aee` workaround for
the Boost.Locale `source_group` error under CMake 4.5. The earlier configure
failure left an incomplete build directory without `build.ninja`.

## Use the stock SteamOS AV1 driver

WiVRn cannot make an absent kernel codec available by changing its advertised
codec list. The Frame's Iris driver must expose stateful AV1 (`AV01`), and the
client must support that format. Current SteamOS supplies the driver support.

A subsequent beta update installed SteamOS **0.5.10**, build
`20261118.6185655`, with kernel **`7.2.0-g04c7e99cb73a`**. Its signed stock
module is **`qcom_iris`** (`qcom-iris.ko`), rather than the old `iris` module.
It enumerates `AV01`, accepts AV1 format selection, and offers linear NV12
and P010 capture formats. Direct V4L2 decoding of a 10-bit AV1 sample returned
P010 pixels that matched the software decode.

No patched module was loaded on this kernel. The existing custom ARM64
client produced two `V4L2 iris DMA-buf capture` queues with `NV12 (8-bit)`,
and the user confirmed smooth AV1 streaming. The old rebind service was
already inactive because its old-driver path condition no longer matched.

On November 21 the custom kernel module was retired because it can conflict
with Steam Link on current SteamOS. Its builder, temporary load/restore helper,
CI job and release asset were removed. Use the stock driver; if our temporary
module is still loaded, reboot the Frame and stop using the old loader.
Earlier releases may contain the retired module and should not be used for
driver setup. Update older SteamOS installations to obtain native AV1.

The original investigation used
[mmis1000's Iris AV1 proof of concept](https://github.com/mmis1000/steamframe-iris-av1-poc)
on SteamOS 0.5.9. That experimental module is historical; the WiVRn client
and server AV1 implementation remains available.

## Client changes

These affect the Linux V4L2 backend used by the Frame preset. Android and
FFmpeg decoder paths are unchanged.

- Map AV1 to the **stateful** `AV01` FourCC. Older Steam Runtime headers only
  define the different stateless `AV1_FRAME` format, so the FourCC is defined
  locally without depending on newer userspace headers.
- Advertise AV1 only when Iris enumerates it, accepts selection on a temporary
  descriptor, and offers linear NV12 for that codec. Drivers without that
  support continue to advertise the supported HEVC/H.264 choices.
- Parse the AV1 sequence headers on OUTPUT before creating/starting CAPTURE,
  as the existing HEVC path already does.
- After SOURCE_CHANGE, select linear NV12 or P010 based on the decoded bit
  depth. The Vulkan import path uses these linear formats.
- Recycle empty CAPTURE buffers without displaying old pixels or consuming
  the pending frame's metadata.
- Advertise 10-bit AV1 when Iris accepts single-plane P010 after selecting
  AV1. Drivers that offer only linear NV12 continue to negotiate 8-bit.
- Bundle the built OpenXR loader with ARM64 V4L2 installations and use a
  relative library search path. Assets and libraries remain inside the
  extracted client directory.

The client does not load a kernel module, acquire root privileges, or pretend
AV1 is supported on a driver without AV1 capability. Restart it after a
driver change because codec capabilities are cached for the process lifetime.

## ARM64 client build

The native ARM64 CI recipe is still the `frame` preset in
`.github/workflows/Build.yml`. The referenced successful run
`43485525863` used upstream commit `4d782f73`; the subsequent listen-socket and P010
capability fixes merged here do not change that run's wire protocol.

For cross-building on Linux, use an extracted ARM64 Steam Runtime SDK root
with its target libraries, an `aarch64-linux-gnu` GCC/G++ toolchain, and native
CMake/Ninja, pkg-config, glslangValidator, rsvg-convert, gettext and KTX tools:

```sh
export WIVRN_STEAM_FRAME_SYSROOT=/absolute/path/to/arm64-sdk-root
cmake --preset frame -B build-frame-client \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/SteamFrame-aarch64.cmake \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DWIVRN_COMPRESS_GLB=OFF
cmake --build build-frame-client -j8
DESTDIR="$PWD/build-frame-client/install" cmake --install build-frame-client
```

The PC build used ARM64 SDK image
`registry.gitlab.steamos.cloud/steamrt/steamrt4/sdk/arm64:4.0.20260805.254769`
as its sysroot and Debian GCC 14.2.0 cross tools inside an x86-64 Steam
Runtime SDK container. GPU decoding still runs through V4L2; cross compilation
does not substitute a software decoder.

Keep `usr/bin`, `usr/lib` and `usr/share` together when extracting a client
bundle. The executable locates assets relative to itself and its bundled
OpenXR loader via `$ORIGIN/../lib`. It still needs the Frame's OpenXR runtime
and normal Steam launch environment. It is not an Android APK.

Build the server from this synchronized branch too. The merge changes the
wire protocol relative to the older pre-merge custom build; older Quest
clients would also need rebuilding before using the updated server.

## Sharing the server with Quest

The V4L2 AV1 changes do not change the wire format or the Android decoder.
The updated Android APK and Linux server, compiled from `478f0bf5` with their
respective toolchains, both calculate protocol hash `d1e19cf450318ccb`.
The ARM64 Frame client at `519aa4c9` also works with that server; the later
commit only updated documentation.

Building an APK does not install it. In Envision, follow **Build client APK**
with **Install locally built APK** for the selected profile. The separate
GitHub release installer installs another package and does not update
`org.meumeu.wivrn.local`. Check the native client's startup revision when
diagnosing mismatches: the Android package metadata can still say `1.0`.

During testing, the installed Local APK was still `869b0ce6` from November 6,
while the new profile's APK was `478f0bf5`. After verifying matching signing
certificates and updating Local with `adb install -r`, the user confirmed
smooth AV1 streaming from the same Envision server. Application data was
preserved, and no protocol compatibility check was bypassed.

## Validation and limits

- The full Linux server and matching OpenXR library build successfully.
- The ARM64 Release client builds with warnings treated as errors.
- Mock V4L2 capability tests run using the compiled ARM64 client objects under
  QEMU on the PC. They cover stock-driver behavior, AV1 with only compressed
  output, corrected AV01/NV12 support, rejected format selection, 8-bit
  negotiation, preservation of HEVC behavior and descriptor cleanup.

The user tested the patched module and ARM64 client on the Frame on
2026-11-16. They reported smooth detailed gameplay for more than ten minutes,
then another successful run with debug logging. Each run had one brief visual
hiccup, also reported with other codecs. The debug log records two Iris
DMA-buf capture queues using linear `NV12 (8-bit)` and a stream rate of
`90.00001` Hz. There are missing-video-shard messages, but no decoder-worker
failure or kernel Iris crash in the collected logs. These observations do not
establish the cause of the hiccups or provide a measured latency comparison.

The Frame's disabled refresh-rate selector displayed 72 Hz, while the stream
description reported approximately 90 Hz. Requests to change the rate were
rejected with `XR_ERROR_DISPLAY_REFRESH_RATE_UNSUPPORTED_FB`. The selector
uses the runtime's enumerated rate list and is disabled when fewer than two
rates are returned. The negotiated stream rate alone does not establish the
panel's actual refresh rate.

On November 19, compositor logs confirmed an actual switch from about 90 Hz
to 72 Hz when WiVRn launched, despite the Steam Link preferred refresh rate
being about 90 Hz. Valve documents a separate 72 Hz default for native Frame
applications. Set the WiVRn shortcut's **VR Settings → Video → Per-Application
Video Settings → Frame Limit / Refresh Rate Override** to 90 Hz with no
throttling, then reconnect. The compositor log at
`~/.local/share/Steam/logs/vrcompositor.txt` records the selected display mode.
See [Valve's per-application settings documentation](https://partner.steamgames.com/doc/steamhardware/steamframe/vrpreferences).

After the user applied the per-application 90 Hz override and reconnected
on November 20, the compositor remained at approximately 90 Hz and WiVRn's
refresh requests succeeded. The user also confirmed 90 Hz in the client UI.

The updated Quest Local client was also tested against this server. Its log
records three `c2.qti.av1.decoder` MediaCodec instances at 90 Hz, and the user
confirmed smooth streaming without a version mismatch. Frame three-stream
operation and other kernel/firmware versions have not been validated.

Local build products are under `build-frame-av1/`, which is ignored by Git.
Device inspection logs and temporary SDK/build files are not part of the
client bundles.
