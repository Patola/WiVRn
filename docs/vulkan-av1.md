# Personal Vulkan AV1 build

This work is on `codex/quest-v29-latency`. The translation branch (`master`)
and the installed Envision WiVRn prefix are unchanged.

## Source and changes

[WiVRn PR 1087](https://github.com/WiVRn/WiVRn/pull/1087) was merged at
`b6c45786` (nine commits), preserving its history. It adds Vulkan Video AV1,
including 8-bit and 10-bit Main-profile encoding. No client protocol change
is needed. Automatic backend selection retains its existing preferences;
select the Vulkan backend explicitly to use this experimental encoder.

Local additions:

- Check the enabled `videoEncodeAV1` feature as well as the extension and queue.
- Let the driver choose the tile layout. Vulkan explicitly permits a null
  `pTileInfo`; this respects driver limits and removes the patch's single-tile
  4096-pixel width restriction.
- Select a sequence level and tier using resolution, display rate, and peak
  bitrate. Reject requirements above the driver's maximum level. The original
  selector considered decode rate alone and always signaled Main tier.
- Upgrade sequence parameters and send a new key frame when live bitrate or
  refresh-rate increases require a higher level/tier. Keep the existing level
  when lowering settings. Wait for this encoder's pending GPU commands before
  replacing parameters, and retain the appropriate header with each key frame.
- Keep the previous rate-control state in `BeginVideoCoding`, then apply new
  settings with `ControlVideoCoding` inside that scope.
- Use valid reference indices even for reference names not used for prediction;
  prediction still follows WiVRn's acknowledged-frame tracking.
- Correct the default-rate-control quantizer field and GOP remaining-frame
  counts, and fail explicitly if no sequence header is returned.

Relevant specifications: [Vulkan AV1 picture parameters](https://docs.vulkan.org/refpages/latest/refpages/source/VkVideoEncodeAV1PictureInfoKHR.html)
and [AV1 levels and tiers](https://aomediacodec.github.io/av1-spec/#levels).

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

CPU regression tests cover display-rate boundaries, fractional refresh rates,
bitrate tiers, live-upgrade selection, retained levels on decreases, and
rejection above driver limits:

```sh
cmake -S . -B build-quest-server -DWIVRN_BUILD_TEST=ON
cmake --build build-quest-server -j8
./build-quest-server/server/av1-level-test
```

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
configured; regular unpatched servers cannot use the new combination.

## Validation on this system

- Full Linux server/OpenXR-library build succeeds with Vulkan headers 1.4.363.
- CPU AV1 level/tier regression checks pass.
- Envision development build/install, formatting, strict clippy checks, and all
  101 unit tests pass. The new UI hint is included in gettext extraction.
- RX 7900 XTX, RADV/Mesa 26.2.6, Quest 3: the user confirmed smooth wireless
  Vulkan AV1 at 140 Mbit/s with the imported patch. Logs identify three
  10-bit AV1 encoders; the two eye streams were active during WayVR use.
- A 300-frame capture decodes cleanly with FFmpeg/libdav1d after inserting
  temporal delimiters for the standalone OBU demuxer (the network protocol
  supplies frame boundaries separately).

Other GPUs/drivers, active passthrough/alpha streaming, and resolutions above
4096 pixels per stream have not been hardware-tested here. This remains a
personal experimental build, rather than a claim of upstream readiness.
