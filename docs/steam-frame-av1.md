# Steam Frame AV1 hardware decoding

This work stays on `codex/quest-v29-latency`. Upstream `master` was merged
through `47651c00`, including the Frame controller, battery, startup and
codec-specific 10-bit detection changes.
The existing Vulkan AV1 server encoder and VAAPI reconfiguration fix are retained.
The translation branch and installed Envision profiles were not changed.

## Two components are required

WiVRn cannot make an absent kernel codec available by changing its advertised
codec list. The Frame's Iris driver must expose stateful AV1 (`AV01`), and the
client must support that format. This follows the separate kernel workaround
described in [SteamOS report 2954](https://github.com/ValveSoftware/SteamOS/issues/2954)
and the [Iris AV1 proof of concept](https://github.com/mmis1000/steamframe-iris-av1-poc).

The headset was inspected over SSH **read-only**, using system query tools.
No files were copied to it, no WiVRn client was installed or launched, and no
service or module was changed.
The artifacts described below were built and staged on the PC only.

Observed device state on 2026-11-16:

| Item | Value |
| --- | --- |
| SteamOS | 0.5.9, build `20261113.6134720` |
| Kernel | `7.2.0-g344f27110f05`, aarch64 |
| Decoder | `/dev/video-dec0` → `/dev/video22`, Iris |
| Advertised coded formats | H264, HEVC, VP90; no AV01 |
| Firmware | Existing `qcom/vpu/vpu33_4v.mbn` |

## Client changes

These affect the Linux V4L2 backend used by the Frame preset. Android and
FFmpeg decoder paths are unchanged.

- Map AV1 to the **stateful** `AV01` FourCC. Older Steam Runtime headers only
  define the different stateless `AV1_FRAME` format, so the FourCC is defined
  locally without depending on newer userspace headers.
- Advertise AV1 only when Iris enumerates it, accepts selection on a temporary
  descriptor, and offers linear NV12 for that codec. Stock kernels continue
  to advertise the supported HEVC/H.264 choices.
- Parse the AV1 sequence headers on OUTPUT before creating/starting CAPTURE,
  as the existing HEVC path already does.
- Request linear NV12 after SOURCE_CHANGE. The patched driver defaults to
  Qualcomm's compressed Q08C, which this Vulkan import path cannot use.
- Recycle empty CAPTURE buffers without displaying old pixels or consuming
  the pending frame's metadata.
- Advertise **8-bit AV1 only**. The workaround offers NV12 and compressed
  TP10C rather than the linear P010 needed by the current import path.
  Server capability negotiation therefore selects 8-bit for the Frame even
  if the server normally uses 10-bit with another headset.
- Bundle the built OpenXR loader with ARM64 V4L2 installations and use a
  relative library search path. Assets and libraries remain inside the
  extracted client directory.

The client does not load a kernel module, acquire root privileges, or pretend
AV1 is supported on the unmodified driver. Restart it after a driver change
because codec capabilities are cached for the process lifetime.

## Matching kernel module

The proof of concept originally targets kernel 6.18. It must not be loaded
into this Frame's 7.2 kernel. The local builder instead uses:

| Input | Pinned value |
| --- | --- |
| Kernel source | `evlaV/deckard-linux-no-oopsies`, `344f27110f059d0520c9dbbafc503d4bbc6c1f68` |
| Iris patch | `mmis1000/steamframe-iris-av1-poc`, `c2df9372d76a8ffbcf6c987ef980f9acf35d8e53` |
| Header package | `linux-72-deckard-headers-7.2.0+g344f27110f05-1-aarch64.pkg.tar.zst` |
| Header archive SHA-256 | `56b3f7a22f4a656bdfcd3ad6adbe88f9b85544650d12aabb69aee30d96ae752c` |
| Running/header config SHA-256 | `55a25adc47f0692b0aab337cf30b339a6b6ba0a899d30be05b83914b0754d4d0` |

The pinned patch applies unchanged to this source revision. It includes the
AV1 codec/buffer handling, IRQ initialization correction and corrected NV12
format mask. Its original kernel-source copyright and GPL-2.0-only attribution
are retained; the patch is fetched separately rather than relicensed as WiVRn.

Build on an x86-64 Linux PC, using a new output directory:

```sh
./tools/build-steam-frame-iris-av1.sh build-frame-iris-7.2
```

The script downloads and verifies the header and cross-compiler archives,
fetches pinned source revisions, and builds only Iris. It preserves the
package's configuration and symbol CRCs, and checks them again after building.
The resulting module has vermagic:

```text
7.2.0-g344f27110f05 SMP preempt mod_unload aarch64
```

The cross compiler is GCC 15.1.0; Valve's kernel was built with GCC 15.2.1.
Kbuild reports this difference. The output is unsigned, and optional BTF
generation is skipped; neither changes the installed kernel or its firmware.
Matching configuration/vermagic and a successful build do **not** establish
that loading the module is safe or that streaming works on this newer kernel.

Optional cached inputs are `WIVRN_FRAME_HEADERS_ARCHIVE`,
`WIVRN_FRAME_GCC_ARCHIVE`, `WIVRN_FRAME_KERNEL_SOURCE` and
`WIVRN_FRAME_IRIS_POC_SOURCE`. Revisions and archive checksums are still checked.
`WIVRN_BUILD_JOBS` controls parallelism.

No loader or automatic system modification is added to the client. Activation
would require a separate, explicitly authorized deployment and a temporary
driver swap. That step was not performed under the read-only instruction.

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

## Validation and limits

- The full Linux server and matching OpenXR library build successfully.
- The ARM64 Release client builds with warnings treated as errors.
- Mock V4L2 capability tests run using the compiled ARM64 client objects under
  QEMU on the PC. They cover stock-driver behavior, AV1 with only compressed
  output, corrected AV01/NV12 support, rejected format selection, 8-bit
  negotiation, preservation of HEVC behavior and descriptor cleanup.
- The separate module builds with the pinned 7.2 headers. Header configuration
  matches the read-only device observation, configuration/symbol CRC files
  remain unchanged, and module name/architecture/vermagic checks pass.
- ShellCheck and Bash syntax checks pass for the module builder.

End-to-end AV1 hardware decoding, decoded image correctness, latency, and
three concurrent streams have **not** been tested on the Frame. Loading a
module or starting a decoder would change device state, so those tests were
excluded by the read-only constraint. The existing driver still lacks AV1.

Local build products are under `build-frame-av1/`, which is ignored by Git.
Device inspection logs and temporary SDK/build files are not part of the
client/module bundles.
