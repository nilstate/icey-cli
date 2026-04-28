# Changelog

## Unreleased

## 0.2.3 - 2026-04-28

- Bumped the pinned `icey` dependency from `2.4.8` to `2.4.9` (codec registry now runtime-probes encoders so Linux CI no longer picks `h264_nvenc` on hosts without CUDA).

## 0.2.2 - 2026-04-28

- Bumped the pinned `icey` dependency from `2.4.7` to `2.4.8` (cross-platform CI build fixes for Windows MSVC and Linux without libavdevice).
- `scripts/facetime-demo.sh` rewritten as a single-process bring-up: just `icey-server --source 'avfoundation:0:none'`. mediamtx and the ffmpeg subprocess are gone, along with the orphaned `docs/mediamtx.yml`. Audio is off by default (`AUDIO_DEVICE=none`); set `AUDIO_DEVICE=0` to capture the built-in mic.
- `docs/facetime-on-macbook.md` rewritten for the native single-process path. Drops the mediamtx and ffmpeg-CLI sections, the RTP-over-UDP loss workaround, and the duplicate-frame timestamp workaround. Adds a troubleshooting entry for the AVFoundation freeze that 2.4.7 fixed (`av_read_frame` returning `AVERROR(EAGAIN)`).
- `SKILL.md` updated to describe the single-process M4 path instead of the prior `mediamtx + ffmpeg + icey-server` chain.

## 0.2.1 - 2026-04-28

- `MediaSession` opens local OS cameras directly when `--source` is a libavdevice URL (`avfoundation:0:none`, `v4l2:/dev/video0`, `dshow:video=...`). Removes the `mediamtx + ffmpeg + RTSP` relay from the FaceTime camera demo on macOS; the same dispatch is wired for v4l2 (Linux) and dshow (Windows). Per-source demuxer hints (framerate, video_size) come from the existing `videoFps` / `videoWidth` / `videoHeight` config fields. Network-source RTSP options stay scoped to network sources only.
- `--doctor` and the start-up preflight recognise device URLs and skip the local-file existence check.
- `/api/config` now emits `source.value` / `source.kind` / `source.remote`, matching the existing doctor JSON shape. The web UI sidebar shows the configured source instead of "(no source configured)".
- Bumped the pinned `icey` dependency from `2.4.6` to `2.4.7` (native device input, `avdevice_register_all` init, `MediaCapture::run` no longer treats `AVERROR(EAGAIN)` as fatal).
- Browser smoke (`web/tests/browser-smoke.mjs`) updated for the 0.2.0 UI selectors (`status--online`, `is-hidden`, `codec-value` / `bitrate-value`) and now accepts device-URL sources via `MEDIA_SERVER_SOURCE='avfoundation:0:none'`. Validates the native device input path end-to-end on macOS.

## 0.2.0 - 2026-04-28

- Rebuilt the bundled web UI as a video-first surface. Persistent dismissable side rail with peers, events, and pipeline stats. HUD overlays for glass-to-glass latency, fps, codec, bitrate, voice activity, and motion regions. Idle auto-fade for chrome and cursor. Speaker default-muted to avoid the FaceTime-mic-into-speakers feedback loop on a single-Mac demo
- New `MediaSession` handling for live network sources: detects `rtsp://`, `rtsps://`, `rtmp://`, `rtmps://`, `srt://` prefixes, applies low-latency demuxer hints (`rtsp_transport=tcp`, `fflags=nobuffer`, `flags=low_delay`, `analyzeduration` and `probesize` capped to a single keyframe, no reorder buffer), and disables file-style framerate limiting and looping
- `MediaSession::StateChanged` now wraps source bring-up in a try/catch so RTSP open failures hang up the call and stay alive instead of taking the entire process down
- Browser-captured audio constraints now request WebRTC echo cancellation, noise suppression, and auto gain control
- New `make facetime-demo` (and `scripts/facetime-demo.sh`) brings up mediamtx, ffmpeg (avfoundation), and icey-server together with one `Ctrl-C` tear-down. macOS only
- New `docs/facetime-on-macbook.md` covers the realtime FaceTime camera path, including the libc++ portability fix in `nilstate/icey`, mediamtx TCP-only config, ffmpeg `-use_wallclock_as_timestamps 1` (which is also what makes the in-browser glass-to-glass latency badge work), the audio resampler bring-up, and the source-loss crash guard
- Bumped the pinned `icey` dependency to use the audio encoder resampler fix, the `MediaCapture::setOpenOptions` API, and the libc++ `file_clock::to_sys` portability fix from `nilstate/icey`

## 0.1.3 - 2026-04-14

- Bumped the pinned `icey` dependency release from `2.4.4` to `2.4.5`
- Hardened release packaging so `ICEY_SOURCE_DIR` no longer silently drifts from the pinned `icey` tag when release metadata is available

## 0.1.2 - 2026-04-06

- Hardened `icey-server` as a standalone product repo on top of `icey`
- Added runtime intelligence event plumbing for `vision` and `speech`
- Added browser smoke coverage and release-check scaffolding
- Added operator-facing health and status endpoints
