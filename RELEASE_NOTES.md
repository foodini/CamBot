# CamBot release notes

## 1.0.0 (October 2026)

First release. CamBot overlays custom flight telemetry (altitude graph, map, climb/sink
rate, date/time, and so on) onto GoPro video for glider flying, lets you sync the two by
hand, and exports the result as an MP4.

### What it does

- Loads a video and a telemetry log, and lets you scrub both together, including
  right-click scrubbing through the video.
- Sync telemetry to video with the telemetry scrub widget, and mark the launch point
  from the menu.
- Rotate and zoom the video to level the horizon. The rotation angle is saved in the
  project file, and the exposed edges and corners are trimmed so the rotated frame never
  pokes into the UI.
- Save and reload projects (`.ffsw`), including the telemetry offset, launch point and
  rotation.
- Save a raw video frame, or a frame with the telemetry overlay.
- Export the full video with the overlay, keeping the original audio untouched.

### Changes since the previous builds

- **GPU video export.** Export now encodes with NVENC (`h264_nvenc`), about twice as fast
  as before with almost no CPU load. This requires an NVIDIA GPU. There is no software
  fallback.
- **No more MSYS2.** The build fetches a prebuilt, NVENC-capable FFmpeg automatically on
  the first build (`tools/fetch_ffmpeg.ps1`, into `third_party/ffmpeg`) and copies the DLLs
  next to the executable. The script re-checks the published checksum on every build and
  only re-downloads when a newer build is available.
- **Exported video was solid white** under NVENC. Fixed by feeding the encoder NV12.
- **Cancelling an export and starting it again resumed mid-video** instead of restarting.
  Fixed.
- **Telemetry load crash.** A race between loading telemetry and rendering it caused
  occasional crashes in the first ~15 seconds. The loader now publishes lines to the
  renderer only once they are complete, with no locking.
- **Faster telemetry loading**, roughly 2x, by reusing one JSON reader and avoiding
  per-line locale setup.
- **Save Project** now saves the live state (offset, rotation and so on) instead of stale
  values.

### Known limitations

- Windows and Visual Studio only, and an NVIDIA GPU is required for export.
- Telemetry loading is single-threaded. A 2-3 hour log (about 72,000-96,000 lines) still
  takes several seconds. Multithreaded parsing or a faster file format is the likely next
  step if that matters.
- Exporting while telemetry is still loading is not blocked yet.
- An empty telemetry file is not handled gracefully.
- The telemetry timezone correction is computed once per file, so a log that spans a DST
  change will be off by an hour after the change.
- See `TODO.txt` for the wish list, including an executable icon, audio while in UI mode,
  and a map aesthetic pass.
