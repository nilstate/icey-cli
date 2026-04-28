# Changelog

## Unreleased

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
