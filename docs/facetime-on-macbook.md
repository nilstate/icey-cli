# FaceTime Camera to Browser on a MacBook

Your MacBook's FaceTime camera, in a browser tab, sub-100ms glass-to-glass. The pipeline is native end to end: AVFoundation on the camera side, a single Mach-O binary in the middle, the browser on the other end. No Docker, no Electron, no Python sidecar, no third-party media services. Motion regions drawn over the video as it moves, a voice-activity meter when someone speaks, a real latency number ticking in the corner.

If "FaceTime camera" sounds wrong on your machine, that's because Apple has been quietly dropping the FaceTime branding from new MacBook Pros. macOS Settings still calls it "FaceTime HD Camera". `ffmpeg -f avfoundation -list_devices` calls it "MacBook Pro Camera" on M-series. Same hardware. Instructions below work either way.

The pipeline is two extra processes alongside icey:

```text
FaceTime camera ── ffmpeg (avfoundation) ──► mediamtx ──► icey-server ──► browser
```

icey's `MediaCapture::openFile()` lets ffmpeg auto-detect the source. That's fine for files, RTSP, and HTTP URLs. macOS AVFoundation needs the input format set explicitly, which `openFile` doesn't do, so a direct camera pull from icey isn't viable today. mediamtx plus ffmpeg bridges the gap and gives icey an RTSP feed. RTSP-to-browser is the wedge icey is built around anyway.

## TL;DR

```bash
brew install cmake pkg-config openssl@3 ffmpeg mediamtx
git clone https://github.com/nilstate/icey
git clone https://github.com/nilstate/icey-cli
cd icey-cli
cmake --preset dev && cmake --build --preset dev
(cd web && npm install && npm run build)
make facetime-demo
# open http://localhost:4500/  and click Watch
```

That's it. The rest of this page explains why each step exists, the macOS-specific bear-traps that will bite you, and how to verify it actually works.

## Prerequisites

```bash
brew install cmake pkg-config openssl@3 ffmpeg mediamtx
```

`cmake` and `pkg-config` to configure the build. `openssl@3` for TLS and DTLS. `ffmpeg` provides the libraries icey links against (`libavformat`, `libavcodec`, `libavfilter`, `libavutil`, `libswscale`, `libswresample`) plus the CLI used to push the camera into RTSP. `mediamtx` is the small RTSP server we use as the bridge.

Node 20+ for the web UI build.

## Source layout

icey-cli expects a sibling `icey` source tree. The CMake preset hard-codes `${sourceDir}/../icey`:

```bash
cd ~/dev/0state
git clone https://github.com/nilstate/icey
git clone https://github.com/nilstate/icey-cli
```

You should now have:

```text
~/dev/0state/
  icey/
  icey-cli/
```

If you put them somewhere else, override `ICEY_SOURCE_DIR` when configuring.

## Build the server

From the `icey-cli` directory:

```bash
cmake --preset dev
cmake --build --preset dev
```

The output binary lands at `build-dev/src/server/icey-server`. On macOS arm64 it should be `Mach-O 64-bit executable arm64`:

```bash
file build-dev/src/server/icey-server
```

### libc++ vs libstdc++ portability

If the build fails inside `icey/src/http/src/server.cpp` with:

```text
error: no viable conversion from returned value of type
'time_point<[...], duration<__int128, ratio<[...], 1000000000>>>'
to function return type
'time_point<[...], duration<long long, ratio<[...], 1000000>>>'
```

Apple's libc++ returns `file_clock::to_sys` at nanosecond precision. libstdc++ on Linux returns at microsecond precision, so the same code compiles cleanly there and breaks here. Wrap the conversion in a `time_point_cast`:

```cpp
std::chrono::system_clock::time_point toSystemTime(stdfs::file_time_type fileTime)
{
    return std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        std::chrono::file_clock::to_sys(fileTime));
}
```

That is the only macOS-specific source change.

## Build the web UI

```bash
cd web
npm install
npm run build
cd ..
```

Output goes to `web/dist/`. `icey-server` will pick it up via `--web-root web/dist`.

## The fast path: `make facetime-demo`

Once the binary and the web UI are built, the entire stack comes up with one command:

```bash
make facetime-demo
```

This runs `scripts/facetime-demo.sh`, which starts mediamtx, ffmpeg, and icey-server with the right flags, multiplexes their logs, and tears all three down on `Ctrl-C`. Open `http://localhost:4500/` and click **Watch** in the side panel.

If you'd rather drive each component yourself, the next section walks through what `make facetime-demo` is doing.

## Manual bring-up

### Configure mediamtx

mediamtx 1.18 does not auto-create RTSP paths from publish. Its default transport list (`[udp, multicast, tcp]`) also causes RTP-over-UDP packet loss between mediamtx and icey on a busy localhost. icey logs `RTP: missed N packets` and `concealing ... errors in P frame`. The browser shows blocky, fragmenting video.

Drop a small `mediamtx.yml` next to where you'll start it:

```yaml
rtspTransports: [tcp]

paths:
  all_others:
```

`all_others` is mediamtx's catch-all path entry. It accepts any path on first publish. `rtspTransports: [tcp]` forces both publish and read over TCP, which eliminates the loss.

```bash
mediamtx ./mediamtx.yml
```

mediamtx logs `configuration loaded from .../mediamtx.yml` and opens RTSP on `:8554`.

### List the camera

```bash
ffmpeg -f avfoundation -list_devices true -i "" 2>&1
```

You should see something like:

```text
[0] MacBook Pro Camera        # or "FaceTime HD Camera" on older Macs
[1] MacBook Pro Desk View Camera
[2] Capture screen 0
[0] MacBook Pro Microphone
```

The first run also triggers macOS to prompt for camera and microphone access. Grant both to your terminal app (Terminal, iTerm, Ghostty, etc.) in `System Settings → Privacy & Security`. Until that's granted, ffmpeg fails with `Operation not permitted`.

### Push the camera into RTSP

```bash
ffmpeg \
  -f avfoundation -framerate 30 -video_size 1280x720 -pixel_format nv12 \
  -use_wallclock_as_timestamps 1 \
  -i "0:0" \
  -fps_mode cfr -r 30 \
  -c:v libx264 -preset ultrafast -tune zerolatency -profile:v baseline \
  -pix_fmt yuv420p -g 60 -b:v 2M \
  -c:a aac -b:a 128k -ar 48000 -ac 2 \
  -f rtsp -rtsp_transport tcp \
  rtsp://localhost:8554/cam
```

Notes that aren't obvious.

`0:0` is video device 0 (the FaceTime camera) plus audio device 0 (the built-in mic). Use `0:none` for video-only. Recommended on a single-Mac demo, because the mic will pick up your own speakers playing back the audio and create a feedback loop. Headphones avoid it. Browser-side default speaker mute (already in place) avoids it. `0:none` avoids it.

`-use_wallclock_as_timestamps 1` plus `-fps_mode cfr -r 30` is the fix for ffmpeg's `frame duplication too large, skipping` storm on AVFoundation. AVFoundation reports a 1000k timebase but no proper frame rate, so the timestamp pipeline drifts. Stamping with the wall clock and pinning the output to constant 30fps clears it. This is also what makes icey's glass-to-glass latency badge work: RTP timestamps end up encoding capture wallclock, which the browser can decode back symmetrically.

`-rtsp_transport tcp` matches mediamtx's TCP-only config.

When this works, mediamtx logs `[path cam] stream is available and online, 2 tracks (H264, MPEG-4 Audio)`. ffmpeg prints steady `frame= ... fps=30 ... speed=1.00x`.

### Run icey-server

```bash
./build-dev/src/server/icey-server \
  --source rtsp://localhost:8554/cam \
  --web-root web/dist \
  --no-turn
```

`--no-turn` skips the embedded TURN server. Not needed for a browser on the same machine. Add it back when you start testing across networks.

Open `http://localhost:4500/` and click **Watch** on the icey peer. Within a second or two, the FaceTime camera lands in the browser tab.

## What you should see

Top-right of the video, the `latency` badge ticks around 60 to 100ms. This is true glass-to-glass, derived from the RTP timestamp on each frame versus `Date.now()` on the browser side. Anything above ~150ms turns amber. Above ~400ms, red.

Top edge of the page, a thin green pulse line flickers per arriving frame. It dims to amber if frames stall.

Bottom-left, a voice-activity ring pulses green and a level meter when icey detects speech in the audio stream.

On the video itself, green bounding boxes with corner ticks are drawn over moving regions, fading after about 600ms. Driven by icey's motion detector.

Top-right also shows live `fps`, `codec`, and `bitrate`.

The right rail has the peer list, scrolling event timeline, and pipeline stats: source fps, sampled fps, vision queue depth, dropped count, vision latency, snapshot and clip counts.

The combination is the differentiator. Plain RTSP-to-browser is solved. The live intelligence overlays plus a real latency number is what you can't get without icey.

## Verify

```bash
curl -s http://localhost:4500/api/health
curl -s http://localhost:4500/api/ready
curl -s http://localhost:4500/api/status | jq
```

`/api/status` reports mode, peer identity, session counts, and intelligence state. If `ready=false`, run `--doctor` for a preflight diagnostic:

```bash
./build-dev/src/server/icey-server --doctor
```

## Troubleshooting

**ffmpeg exits with `Operation not permitted` reading from `avfoundation`.**
Camera (and microphone, if you're capturing audio) permission for the terminal app is missing. macOS won't surface the prompt again until you toggle the permission in `System Settings → Privacy & Security → Camera`.

**mediamtx logs `path 'cam' is not configured`.**
You're running mediamtx without a config. The default empty configuration rejects all paths in v1.18+. Add the `mediamtx.yml` shown above and restart.

**ffmpeg connects to mediamtx but icey-server logs `Cannot open the media source`.**
Confirm mediamtx is actually receiving the publish. Its log prints `[RTSP] [session ...] is publishing to path 'cam'` on success. If ffmpeg is up and mediamtx is silent, the ffmpeg `-f rtsp ... rtsp://localhost:8554/cam` URL didn't resolve. Check that mediamtx is bound to loopback and not just IPv6.

**ffmpeg floods stderr with `frame duplication too large, skipping` and never produces output.**
AVFoundation timestamp issue. The input stream advertises a 1000k timebase but no real frame rate. Add `-use_wallclock_as_timestamps 1` to the input flags and `-fps_mode cfr -r 30` to the output flags as shown above.

**Browser plays the camera but the picture fragments, blocks, or freezes intermittently.**
icey's log will show this clearly:

```text
[rtsp @ ...] max delay reached. need to consume packet
[rtsp @ ...] RTP: missed 12 packets
[h264 @ ...] concealing 640 DC, 640 AC, 640 MV errors in P frame
```

That's RTP-over-UDP loss between mediamtx and icey's RTSP client. Force TCP everywhere via `rtspTransports: [tcp]` in `mediamtx.yml`. Re-launch mediamtx and the loss disappears.

**Audio sounds garbled, chipmunky, or wrong-channel.**
icey's audio encoder must call `AudioContext::open()` after `create()` so the resampler is set up. Without it, the AAC decoder's planar `fltp` samples get written to the opus encoder's interleaved `flt` FIFO and the channels mis-interleave. The fix is in `icey/src/av/src/audiopacketencoder.cpp`. `AudioPacketEncoder::onStreamStateChange` should call both `AudioEncoder::create()` and `AudioEncoder::open()`. Rebuild after applying.

**You hear yourself echoing on the demo.**
The FaceTime mic is capturing the laptop speakers playing back the icey audio. The browser-side speaker is **default-muted** specifically to avoid this. If you've unmuted it, put on headphones, or run ffmpeg video-only (`-i "0:none"`), or accept the loop. There is no browser-side AEC fix because the mic isn't browser-owned.

**You see the page but clicking the icey peer doesn't start a call.**
Hard-refresh (Cmd+Shift+R). The action button isn't the only target. The entire peer row is clickable.

**Glass-to-glass latency reads way too high.**
Check that ffmpeg has `-use_wallclock_as_timestamps 1` set. Without it, RTP timestamps don't carry capture wallclock and the browser-side derivation reverts to a coarse fallback that often shows nonsense.

**icey-server dies when the RTSP source disappears.**
This was a real bug in earlier builds. `MediaCapture::openStream` threw on RTSP open failure, the throw escaped the session callback, and the binary terminated. Fixed in `MediaSession::StateChanged` by catching exceptions during session bring-up and hanging up the call instead. If you're hitting this on an older build, rebuild against the current source.

## What's next

Once stream mode is working, walk through the rest of icey's mode matrix in the same shell layout.

1. **record**. Point a browser at a running `icey-server --mode record`, send H.264 from the browser, get timestamped MP4 files under `--record-dir`.
2. **relay**. First browser to call becomes the source. Later callers receive that source via server-side fanout, with no decode/re-encode in the middle.
3. TURN. Drop `--no-turn` and put the server behind a NAT to exercise the embedded RFC 5766 relay on `:3478`.

That ordering keeps signalling, send path, receive path, relay, and public addressing failures separated. Mixing them on day one is how people end up debugging the wrong layer.
