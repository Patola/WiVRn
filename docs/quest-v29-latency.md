# Quest 3 v2.9 investigation and personal build

This branch starts at `869b0ce6` (WiVRn `26.9-108-g869b0ce6`). Translation
files and the `master` branch are unchanged. Nothing is installed into `/usr`
or the Envision checkout by the build helper.

## Findings on 6 November 2026

Quest 3 reports Android 14 / API 34 and firmware fingerprint:

```text
oculus/eureka/eureka:14/UP1A.231005.007.A1/52641960034500520:user/abl_signing_keys:release,amss_signing_keys:release,release-keys
```

The installed store client and `/usr/bin/wivrn-server` both report WiVRn 26.9.
Both the old and new servers use the existing VAAPI / AV1 10-bit configuration.

| Test | Result |
| --- | --- |
| Installed 26.9, Wi-Fi with UDP video, initialized at 140 Mbit/s | Severe lag; repeated missing video shards and IDR requests/timeouts |
| Installed 26.9, Wi-Fi with TCP-only video | Severe lag; TCP send queue reached about 4.5 MB |
| Installed 26.9, USB with TCP-only video | Smooth, using the same AV1 hardware decoders |
| Source build, initialized at 50 Mbit/s, Wi-Fi lock disabled | Smooth |
| Same source build, initialized at 50 Mbit/s, normal Wi-Fi lock | Smooth |
| Same session, requested bitrate later changed to 140 Mbit/s | Reported smooth, but the requested bitrate did not prove the encoder rate changed |
| Source build restarted with AV1 initialized at 140 Mbit/s | Severe lag again |
| User's Envision server, launched directly without Envision environment, initialized at 140 Mbit/s | Severe lag |
| Same unmodified Envision server restarted with AV1 initialized at 50 Mbit/s | Smooth |
| Patched server, live change from 140 back to 50 Mbit/s | Smooth again without restarting; WayVR and the thumbstick menu work |

**Correction to the initial conclusion:** the diagnostic build is not itself a
fix for 140 Mbit/s. Its earlier successful session had initialized VAAPI at
50 Mbit/s. The server's D-Bus `Bitrate` property reports the requested bitrate;
it is not a measurement of the encoder's actual output. Restarting that same
build with 140 Mbit/s reproduces the problem. The unmodified Envision build
also works when initialized at 50 Mbit/s.

The client codec setting on Auto still uses AV1: Android's media resource
manager lists three `c2.qti.av1.decoder` instances, and the server explicitly
selects VAAPI/AV1 10-bit.

## Server findings

The user's executable is:

```text
/home/patola/.local/share/envision/prefixes/eecf1eef-a666-4025-9ca8-b9463956231b/bin/wivrn-server
```

Its checkout is the `xrservice/` subdirectory of:

```text
/home/patola/.local/share/envision/eecf1eef-a666-4025-9ca8-b9463956231b/
```

The installed and build-tree executables have identical SHA-256 hashes.
Envision pins `GIT_DESC=v26.9-89-g75e2df57` and `GIT_COMMIT=75e2df57...` in
its CMake cache, although the source checkout is at `869b0ce6`. Its `server/`
and `common/` sources are byte-for-byte identical to this branch's initial
sources. The patched Monado compositor/utilities match too. Build differences
include bundled Boost 1.89 versus system Boost 1.92, and enabled debug GUI and
Lighthouse support; those differences do not explain the cold-start bitrate
comparison. Envision's library search path does not change resolved libraries.

A concrete server bug was found in live VAAPI bitrate changes:

- `video_encoder_ffmpeg::encode` previously only changed the already-open
  `AVCodecContext::bit_rate` and `rc_max_rate` fields.
- FFmpeg 9.0.2 initializes VAAPI rate-control parameters in
  `vaapi_encode_init_rate_control`, called by `ff_vaapi_encode_init`.
  Merely changing the public fields does not rebuild those parameters.
- This branch reopens the VAAPI codec context when bitrate or frame rate
  changes, preserves the hardware frame pool and Vulkan imports, and requests
  a new keyframe. Unchanged settings do not reopen the encoder. Slider changes
  are coalesced until the requested value has been stable for 200 ms, to avoid
  reopening the hardware context for every intermediate slider value.

The user verified that 50 → 140 → 50 restores smoothness without restarting
the patched server. The server log confirms the encoder contexts were reopened
at the changed per-eye rates.

See the [FFmpeg VAAPI source](https://github.com/FFmpeg/FFmpeg/blob/n9.0.2/libavcodec/vaapi_encode.c)
for the initialization of `rc_params` and its global rate-control parameters.

This explains why decreasing the requested bitrate within an existing session
could fail to reduce actual traffic, and why the first comparison looked like
a server-build fix. It does not yet explain precisely what changed in the
Quest firmware or establish why the current wireless path fails at 140 Mbit/s.

The packet sender transmits video shards in bursts; a capture of the laggy
140 Mbit/s session contained a burst of 146 UDP packets with gaps below 300 us.
An optional pacing experiment at 200 Mbit/s reduced observed bursts to a
99th-percentile size of 9 packets (maximum 18), but 140 Mbit/s remained severely
laggy. That experimental code was removed.

An independent TCP transfer from the PC to an Android `toybox nc`/`dd` receiver
on the Quest moved 107,952,480 bytes in 8.008 seconds, approximately 107.8 Mbit/s,
with no WiVRn server or video stream running. This is below the requested
140 Mbit/s video rate in that test. It supports a current throughput limitation
on the wireless path but does not prove a particular physical link speed,
firmware defect, or access-point fault. The receiver is an Android shell tool,
so this is a diagnostic measurement rather than a calibrated Wi-Fi benchmark.
The reverse test received 273,330,176 bytes in 8.018 seconds, about 272.7 Mbit/s,
from Quest to PC. The observed asymmetry is therefore stronger evidence than
an assumption that a shared Ethernet segment is simply limited to 100 Mbit/s.
A follow-up receiver compiled with the Android NDK, using `recv` directly
without `nc` or `dd`, received 107,145,664 bytes in 8.364 seconds, about
102.5 Mbit/s (the PC sender measured 107.0 Mbit/s over its sending interval).
This reproduced the downlink result without the shell tools' copying path;
it is still a single-flow diagnostic measurement, not proof of the link's
maximum possible capacity.

The OS identifies WiVRn as an immersive app and puts its process in the TOP
foreground state. `dumpsys wifi` confirms a type-4 WiVRn lock and disabled
Wi-Fi power saving while the headset is worn. The PC Ethernet link reports
1 Gbit/s with no NIC errors; the headset reports Wi-Fi 6, a 1200 Mbit/s link,
and roughly -20 dBm signal strength. These observations do not exclude
firmware or access-point problems under streaming traffic.

The hardware decoders report `output.delay.value` of 15–17 during both laggy
Wi-Fi and smooth USB sessions. This field alone does not identify the cause.
The Wi-Fi-lock comparison also does **not** establish the lock as the cause.
The previously mentioned upstream packet-fragmentation and IDR-recovery
changes are already present in both current checkouts; they do not explain
the difference between those builds. USB also changes the transport to TCP,
so that comparison alone cannot identify a specific Wi-Fi driver defect.

During development, building only `wivrn-server` after a commit left the OpenXR
runtime library at the previous Git version. Monado then rejected WayVR with
`ipc_client_check_git_tag` / `XR_ERROR_RUNTIME_UNAVAILABLE`. A full build updated
both components and restored WayVR. Always rebuild all targets after changing
commits; do not work around this by disabling the IPC version check.

Changing the client codec setting from Auto to forced AV1 during an existing
server session changes the advertised codec list. The resume compatibility
check then rejects it; this was observed in the server log. Restart the server
to apply such changes, rather than interpreting that refusal as an AV1 decoder
failure. The check is in `wivrn_session::validate_headset_info`.

## How the relevant code fits together

- `server/encoder/ffmpeg/video_encoder_va.cpp` configures VAAPI encoding.
- `server/encoder/video_encoder.cpp` splits encoded frames into video shards.
- `server/encoder/idr_handler.cpp` handles lost-frame/keyframe recovery.
- `common/wivrn_sockets.cpp` implements TCP framing/encryption and UDP batching.
- `client/wivrn_client.h` polls both sockets on one receive thread.
- `client/scenes/stream_network.cpp` dispatches video, audio, and control packets.
- `client/decoder/shard_accumulator.cpp` reassembles shards and sends feedback.
- `client/decoder/android/android_decoder.cpp` feeds MediaCodec; waiting for an
  input buffer can block the receive thread and therefore both sockets.
- `client/wifi_lock.cpp` requests Android's low-latency Wi-Fi mode.

`ss` TCP `Send-Q` includes data not yet acknowledged, including unsent data.
A single nonzero sample does not prove a stuck connection. UDP `Send-Q = 0`
only shows that the local kernel has no queued outgoing datagrams; it does
not establish successful delivery or timely decoding at the headset.

## Build and run without replacing the usual installations

```bash
./tools/build-quest-diagnostics.sh
adb install -r build-quest-diagnostics/WiVRn-quest-diagnostics.apk
./build-quest-server/server/wivrn-server
```

Stop any existing server before starting this one. This server still uses the
usual WiVRn configuration and pairing store. It manages the active OpenXR
runtime while connected in the usual way. Its client must be built from the
same source; the installed store client may use an incompatible protocol.

The APK has package ID `org.meumeu.wivrn.questdiag` and label
**WiVRn Quest Diagnostics**, with its own settings and headset identity.
The helper uses Java 17 and the SDK/NDK already installed under Envision,
and reads the existing signing password directly from its file. It does
not copy or print signing credentials. Build outputs stay in ignored
directories. Override `WIVRN_JAVA_HOME`, `ANDROID_HOME`, `WIVRN_SIGNING_DIR`,
or `WIVRN_BUILD_JOBS` if needed.

Enable pairing with `./build-quest-server/server/wivrnctl pair` when connecting
this separate package for the first time.

## Reproduce the comparisons

Wi-Fi, with normal UDP video:

```bash
adb shell am start -a android.intent.action.VIEW -d wivrn://192.168.0.14 org.meumeu.wivrn.questdiag
```

For TCP-only Wi-Fi, change the URI to `wivrn+tcp://192.168.0.14`.
For USB, first run `adb reverse tcp:9757 tcp:9757`, then use
`wivrn+tcp://localhost`. Remove that forwarding with
`adb reverse --remove tcp:9757` when finished.

The sole experimental client change allows skipping the Wi-Fi lock:

```bash
adb shell setprop debug.wivrn.wifi_lock off
```

Force-stop and relaunch the diagnostic app after changing this property.
Use `adb shell setprop debug.wivrn.wifi_lock on` to restore normal behavior.
Any value other than `off` uses the original code path. This is a temporary
debug property, not a saved client setting; it resets on headset reboot.

Useful observations while actively wearing the headset and streaming:

```bash
ss -tinmp '( sport = :9757 or dport = :9757 )'
busctl --user get-property io.github.wivrn.Server /io/github/wivrn/Server io.github.wivrn.Server Bitrate
adb logcat -d -v threadtime 'WiVRn:V' '*:S'
adb shell dumpsys wifi
adb shell dumpsys activity processes
```

Raw logs and packet captures from this investigation are local-only in
`build-quest-diagnostics/`; they are intentionally not committed.
