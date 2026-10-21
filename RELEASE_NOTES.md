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
- **Smoothed climb rate.** The map trace and the climb indicator show a smoothed climb
  rate instead of the raw (and very noisy) barometer rate. Choose between a trailing-window
  slope and a critically damped alpha-beta filter, and set its window or time constant;
  the settings window shows the noise and lag each choice costs. Both filters are strictly
  causal (a sample's rate depends only on that sample and earlier ones), so the overlay
  shows what an instrument could have shown at that moment. The filters live in
  `climb_filter.h`, with no dependencies, so they can be lifted into the collector firmware.
  The color range of the map and the indicator is adjustable too.
- **Map trace and zoom.** The flown part of the trace is drawn wide for the last stretch
  (30-600 s, configurable) and thinner for older track, with the most recent trace drawn
  opaque on top. Track the glider hasn't flown yet is muted. The map stays centered on the
  pilot, shows a wide view in straight flight, and zooms in to keep the last circles in
  view once the glider has turned a full circle within 45 s, easing back out when the
  turning stops. The closest and farthest widths (0.2-20 km) and the transition time are
  settings. The zoom is computed per telemetry sample, so scrubbing and export agree.
- **Altitude references.** Enter the true altitude at the start and end of the video
  (View > Altitude Reference...) and the barometric altitude is shifted to match, with the
  shift changing smoothly from one to the other, so slow sensor drift disappears. The
  window shows how far the barometer drifted between the two, and warns if it is
  implausibly large. This correction is for presentation and is not causal; the climb rate
  always uses the barometer as logged. Saved in the project file.
- **Flight summary.** The last seconds of the video (10 by default) show a "Flight
  Summary", fading in over one second: flight time, distance flown, min and max altitude,
  max height above launch, max climb and sink, and max ground speed. An option adds
  cross-country statistics: distance covered, best ground glide, complete circles, and time
  circling. The statistics cover the same stretch of telemetry as the altitude graph.
- **User settings.** The climb filter, color range, map trace and zoom settings, and the
  flight summary options are saved per user (`%APPDATA%\CamBot\user_settings.txt`) rather
  than per project, so a look you dial in carries over from video to video.

### Changes since the previous builds

- **The telemetry window now spans the video.** The map, altitude graph and scrub-widget
  cutoff markers used to start at the launch marker and run one video-length from there,
  so with footage before launch they ran past the end of the video. They now cover exactly
  what is rendered.
- **Distance flown** is measured from the launch marker, not from the start of the window,
  and reads zero before launch.
- **The map's `scale:` text was removed.**
- **Climb rates were sometimes stale.** The stored climb rate was written after the slice
  had been copied, so the climb indicator and map text could show a leftover value. Fixed
  as part of the climb-rate work.
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
- The circling detector and the best-glide measure in the flight summary use first-guess
  thresholds (a full turn within 45 s to start zooming; 360 degrees of one-direction turning
  to count a circle; a 60-second glide that loses at least 100 ft). They have not yet been
  tuned against a real thermaling flight.
- The best glide is a ground glide (straight-line ground distance over altitude lost), so
  wind inflates or deflates it.
- The telemetry timezone correction is computed once per file, so a log that spans a DST
  change will be off by an hour after the change.
- See `TODO.txt` for the wish list, including an executable icon, audio while in UI mode,
  and a map aesthetic pass.
