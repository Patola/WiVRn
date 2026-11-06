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
| Installed 26.9, Wi-Fi with UDP video | Severe lag; repeated missing video shards and IDR requests/timeouts |
| Installed 26.9, Wi-Fi with TCP-only video | Severe lag; TCP send queue reached about 4.5 MB |
| Installed 26.9, USB with TCP-only video | Smooth, using the same AV1 hardware decoders |
| Source build, Wi-Fi at 50 Mbit/s, Wi-Fi lock disabled | Smooth |
| Same source build, Wi-Fi at 50 Mbit/s, normal Wi-Fi lock | Smooth |
| Same source build, Wi-Fi at a confirmed 140 Mbit/s, Auto/AV1, normal Wi-Fi lock | Smooth, confirmed by the user |

The 140 Mbit/s setting was subsequently verified using the server's D-Bus
`Bitrate` property. Android's media resource manager still listed three
`c2.qti.av1.decoder` instances with the client codec setting on Auto: the
server's explicit VAAPI/AV1 configuration selects AV1 in that mode too.
The fresh, matched source build therefore resolves the reproduced symptoms
without disabling the Wi-Fi lock or changing decoder parameters. The specific
upstream change or firmware interaction responsible has not been isolated.

The OS identifies WiVRn as an immersive app and puts its process in the TOP
foreground state. `dumpsys wifi` confirms a type-4 WiVRn lock and disabled
Wi-Fi power saving during the normal-lock test. The PC Ethernet link reports
1 Gbit/s with no NIC errors; the headset reports Wi-Fi 6, a 1200 Mbit/s link,
and roughly -20 dBm signal strength. These observations do not exclude
firmware or access-point problems under streaming traffic.

The hardware decoders report `output.delay.value` of 15–17 during both laggy
Wi-Fi and smooth USB sessions. This field alone does not identify the cause.
The Wi-Fi-lock comparison also does **not** establish the lock as the cause.

Relevant existing upstream changes since 26.9 include:

- `e8c92a72`: prevent video-packet fragmentation when appending timing data.
- `2fc4708b`: fix keyframe-recovery races, track decoder acknowledgements,
  and request a keyframe when decoder output stops advancing.

These are candidates, not individually proven fixes for this headset.
USB also changes the transport to TCP, so that comparison alone cannot
identify a specific Wi-Fi driver defect.

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
