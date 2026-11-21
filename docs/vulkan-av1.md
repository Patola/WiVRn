# Vulkan AV1 in this fork

For Steam Frame, start with the [CI-built package setup guide](steam-frame-av1-guide.md).
Use [Local compilation (alternative)](steam-frame-av1-building.md) for the
complete server and ARM64 client build instructions. Use SteamOS's stock
AV1-capable driver; the custom kernel module is retired.

This work is on `codex/quest-v29-latency`. The translation branch (`master`)
and the installed Envision WiVRn prefix are unchanged.

## Source and changes

Upstream merged Vulkan AV1 in
[`04837ba2`](https://github.com/WiVRn/WiVRn/commit/04837ba215b90fae63fa49d4166f54cd364e536b).
This branch now merges that upstream revision and uses its Vulkan encoder,
including the shared H.264/H.265 encoder interfaces and capability probing.
The previous local AV1 changes, level/tier helper and its standalone tests
were removed in favor of upstream's implementation.

Vulkan AV1 supports 8-bit and 10-bit Main-profile encoding. Select Vulkan
explicitly: automatic backend selection keeps upstream's existing preferences.
The upstream implementation currently uses a single tile and rejects AV1
streams wider than 4096 pixels per encoder stream. The former local support
for wider streams and custom level/tier updates is no longer part of this
branch. Use the capabilities of the current upstream implementation when
choosing stream layouts and settings.

The Frame's V4L2 AV1 decoder support, bundled ARM64 OpenXR loader, VAAPI live
reconfiguration fix, Boost.Locale build fix and server/client release workflow
remain because these changes are not in the synchronized upstream revision.
No wire-protocol change is introduced by this synchronization.

## Build and configuration

The existing helper builds the whole server and its matching OpenXR library:

```sh
./tools/build-quest-diagnostics.sh
```

Use this encoder setting in a separate configuration file (copy the other
settings, such as the `wayvr` application, from the usual configuration):

```json
"encoder": {"encoder": "vulkan", "codec": "av1"}
```

Start it after stopping any existing server:

```sh
./build-quest-server/server/wivrn-server -f /path/to/test-config.json
```

The test configuration used here is
`build-quest-diagnostics/vulkan-av1/config.json`. The regular
`~/.config/wivrn/config.json` still selects VAAPI AV1.

For validation on this machine, use
`VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` and
`NO_ENABLE_TIMELINE_SEMAPHORE=1`. The latter disables an installed implicit
Monado layer that otherwise injects a second timeline-semaphore feature
structure and produces an unrelated device-creation validation error.
It is only needed for clean validation; the system configuration was not changed.

Always build all targets after changing commits, so the server and OpenXR
library have matching version tags (otherwise WayVR can fail to start).

## Envision

The Envision changes are on `codex/vulkan-av1` in
`/home/patola/workspace/kimi/envision-wivrn`, commit `a066efc`.
They allow AV1 in the Vulkan codec selector and explain its requirements.
Existing Auto selections and presets are retained. No additional build-system
option is needed: WiVRn's `WIVRN_USE_VULKAN_ENCODE` already defaults to `ON`.

The development installation is at:

```sh
/home/patola/workspace/kimi/envision-wivrn/build/localprefix/bin/envision
```

To build this source through Envision, use a separate profile and set:

| Profile field | Value |
| --- | --- |
| XR Service Repo | `/dados/git/WiVRn` |
| XR Service Branch | `codex/quest-v29-latency` |

Envision can clone the local Git repository, so no push is required. Build
that profile and select Vulkan + AV1 for the desired encoder streams in its
WiVRn settings. The settings apply to whichever WiVRn server is being
configured. Current upstream WiVRn also supports this encoder combination.

## Validation after synchronizing to upstream `04837ba2`

- The full Linux server and matching OpenXR runtime build with bundled
  Boost, CMake 4.5.1 and warnings treated as errors.
- The headless GPU test on the RX 7900 XTX completes 30 frames for each of
  H.264 8-bit, HEVC 8/10-bit and AV1 8/10-bit at 1280×1344. Each configuration
  changes total bitrate 50 → 140 → 50 Mbit/s and nominal refresh rate
  80 → 120 → 80 Hz. The Khronos validation layer reports no validation errors.
- A 4608×1024 test is rejected with upstream's single-tile width-limit error,
  as expected. The wider-stream support in the historical results below
  does not apply to this encoder.
- The Vulkan encoder sources and capability probing match the synchronized
  upstream revision. The Frame decoder, VAAPI fixes, Boost build patch and
  module-free release packaging are unchanged by the merge.

These checks cover compilation and GPU command submission/completion. They
do not establish decoded image quality or replace a new headset streaming
test. Logs and the temporary GPU harness are in the ignored
`build-frame-av1/upstream-sync/` directory.

## Historical validation before the upstream encoder switch

The following results describe the previous local encoder implementation.
They are retained as history, not as validation of the newly adopted upstream
encoder. In particular, its wider-stream and custom level/tier behavior no
longer applies.

- Full Linux server/OpenXR-library build succeeds with Vulkan headers 1.4.363.
- CPU AV1 level/tier regression checks pass.
- The final encoder code also passes a headless GPU test with the Khronos
  validation layer: 30 synthetic frames per configuration, H.264 8-bit,
  H.265 8/10-bit, and AV1 8/10-bit at 1280×1344. It changes total bitrate
  50 → 140 → 50 Mbit/s and nominal refresh rate 80 → 120 → 80 Hz. Logs
  confirm AV1 level and tier upgrades; no validation errors occur.
- The same GPU test passes AV1 8/10-bit at 4608×1024, covering the former
  single-tile width limit. This checks submission and completion, rather
  than decoded image quality. Its temporary harness and logs are under
  `build-quest-diagnostics/vulkan-av1/gpu-smoke*`.
- Envision development build/install, formatting, strict clippy checks, and all
  101 unit tests pass. The new UI hint is included in gettext extraction.
- RX 7900 XTX, RADV/Mesa 26.2.6, Quest 3: the user confirmed smooth wireless
  Vulkan AV1 at 140 Mbit/s with the imported patch. Logs identify three
  10-bit AV1 encoders; the two eye streams were active during WayVR use.
- A 300-frame capture decodes cleanly with FFmpeg/libdav1d after inserting
  temporal delimiters for the standalone OBU demuxer (the network protocol
  supplies frame boundaries separately).
- The final build was then tested on the headset starting at 50 Mbit/s and
  increasing live to 140 without reconnecting. The user confirmed that it
  remained smooth. The captures show level 5.0, Main tier at 50, upgraded to
  High tier at 140. Both eyes decode cleanly before and after the change:
  four samples of 300 frames, 1200 frames total, with no decoder warnings.
  No Vulkan validation errors were reported in this session.

The test server and its log collectors were stopped after validation, leaving
port 9757 available for the regular Envision server. Large temporary raw
captures were removed; bounded samples and diagnostic logs remain in the
ignored build directory.

Other GPUs/drivers, active passthrough/alpha streaming, and headset display
of streams above 4096 pixels wide have not been tested here. This remains a
personal experimental build, rather than a claim of upstream readiness.
