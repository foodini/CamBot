# CamBot release notes

## Unreleased

### Multi-file flights, JSON projects

### Editing model (no visible change yet)

- The program now keeps the footage as a *library* of files, a *capture order* (the order they
  were recorded, which is the flight clock telemetry is synchronised to) and a list of *clips*
  (the edit timeline). Today the clips are simply every file whole, in capture order, so the
  program looks and behaves as before; this is groundwork for cutting and rearranging.
- Telemetry sync is now stored as sync points tied to a file and a moment in it, instead of one
  number for the whole flight, so clips will show the right telemetry wherever they are placed,
  and a camera clock that runs fast or slow can later be spread evenly between two points. One
  sync point behaves exactly like the old single offset (Up/Down nudge, dragging the telemetry
  bar). The launch marker is likewise tied to a file and a moment in it.
- **New window layout.** The window is resizable (minimum 900x600) and opens at the size, and
  maximized or not, it had when last closed (first run: 1200x900). The video and its overlays
  are shown as a preview that is as big as the window's height allows while keeping its shape,
  with only a thin border around it; the left panel takes all the width the preview doesn't use,
  and the divider between them can be dragged to make the panel wider (and the preview smaller),
  with the width remembered. A transport bar sits under the preview: the position as `h:mm:ss#frame / length (@29.97fps)`
  and the two screenshot buttons on the top row, and Start, -1, Play/Pause, +1, End below.
  Reverse, slow and fast play are shown but off until playback supports them. The left panel is a placeholder for the
  media list and the telemetry sync controls. For now the preview still includes the strip
  under the video with the telemetry scrub bar, which, with the right-click scan and
  click-to-pause, works as before. Everything is drawn through ImGui, so none of it ends up in exports.
- Each frame is now drawn into an off-screen picture (the video, the telemetry and the scrub-bar
  strip, 1920x1214 as before) and then copied to the window. Exports and both screenshot
  buttons read that picture rather than the window, so the saved frame no longer depends on
  the window. Nothing should look different.
- **Timeline.** A timeline now runs the full width of the window along the bottom,
  with a draggable divider above it; drag the divider to make it taller or shorter (the height
  is remembered, minimum 110 pixels). Each source file is a coloured block with its name,
  yellow lines mark the joins, a ruler shows time, and a red line shows the playhead. Left-click
  or drag to scrub (it follows keyframes while you drag and lands on the exact frame when you
  let go), right-drag to scan at a speed that grows with the distance dragged (the same feel as
  the old scrub bar), mouse wheel to zoom around the cursor, Shift+wheel, middle-drag or a left-drag on the ruler to pan,
  and the Fit button to show everything. The timeline follows the playhead during playback.
  This replaces the video scrub bar that used to sit under the video; the telemetry scrub bar
  is unchanged for now.
- **Opening and switching projects.** CamBot now starts with the project that was open last time
  (if its file is still there), or the one named on the command line (so a project file can be
  dropped on CamBot.exe); if neither can be opened it asks, as before. The File menu has **New
  Project...**, **Open Project...**, **Recent Projects** (the last ten, with the full path shown when
  you hover, and any that have gone greyed out) and **Close Project**, which asks you to pick a
  project the next time CamBot starts instead of reopening this one. Each asks first whether to
  save unsaved changes. Switching works by closing CamBot and starting it again on the other
  project (the window comes back at the same size), so a debugger attached to the old copy will
  not follow it. None of them are available while exporting.
- **Cutting the video.** The clip the playhead is in is outlined on the timeline, and the buttons
  above the timeline (and keys) act on it: **Split** (`S`) cuts it in two at the playhead, the
  playhead's frame starting the second piece; **Trim Left** (`[`) and **Trim Right** (`]`) delete
  the part of the clip before / after the playhead's frame, keeping that frame; **Delete**
  (`Delete`) removes the whole clip. What follows always moves up to close the gap, and the
  playhead goes to where the deleted footage began. **Undo** (`Ctrl+Z`) and **Redo** (`Ctrl+Y`, or
  `Ctrl+Shift+Z`) step back and forward through the edits, restoring the playhead too. Edits
  pause playback, are not possible during an export, and are saved with the project (the title bar
  shows the `*`). The telemetry stays matched to the footage wherever it ends up, since it is
  tied to when each piece was recorded. Cuts in the middle of a file can make a small click in
  the exported sound for now. Dragging clip edges to trim and dragging clips to reorder them are
  still to come.
- **The telemetry follows the edit.** The map, the altitude graph, the scrub widget and the
  flight summary now cover only the stretch of telemetry from the start of the first piece of
  video you use to the end of the last, found through the sync. Telemetry from before take-off
  (or after landing) is dropped as soon as the footage around it is trimmed away, and it
  comes back if the cut is undone. The altitude references (Edit > Altitude Reference) now
  describe the start and end of that stretch, and the launch marker's readouts follow the sync.
  (The `window_start_elapsed_s` value in old project files is still read and written but no
  longer used.)
- **Telemetry is optional.** A flight can be a video alone: New Project asks whether there is a
  telemetry file, and without one the map, graph, scrub widget and telemetry menu items are
  simply not shown. Telemetry can be added later with **Load Telemetry...** in the new
  **Source Material** section of the left panel (it saves the project and restarts CamBot to
  read the file). The section also lists the video files in the order they were recorded and
  the telemetry file; **Sync / Reorder...** is there but does nothing yet.

- **One flight, many video files.** Cameras split a long recording into pieces; a new
  project now takes all of them (select several files in the dialog) and plays them as one
  continuous video. Scrubbing, frame stepping, the timeline and export all run across the
  joins. The files have to match (same codec, resolution, frame rate and sound format);
  if one doesn't, CamBot says which file and why. GoPro chapter names (`GX010123`,
  `GX020123`, ...) and numbered names are put in recording order automatically, and the
  order is shown for confirmation. Audio is carried across the joins; where one file's
  sound overlaps the next by a few milliseconds the overlap is trimmed.
- **Projects are JSON now.** The `.ffsw` file is readable JSON (version 3). Version 2
  projects open and are converted when next saved; older ones are not read. Saving writes to a temporary file and swaps
  it in (retrying briefly if a virus scanner or editor has the file), so a crash or a full
  disk can't leave a half-written project.
- **Saving is explicit.** The project is only written by File > Save Project or Ctrl+S
  (the S printed on your keyboard, whatever the layout). The title bar shows `name.ffsw *`
  while there are unsaved changes, and closing with changes asks whether to save. A
  recovery copy (`name.ffsw.autosave`) is written at most once a minute while there are
  unsaved changes; if CamBot ends without saving, the next open offers to restore it. It
  never replaces the project itself, and a normal exit deletes it.
- **Menus and help.** Edit > Rotate 180 Degrees / Rotate Left a Little / Rotate Right a
  Little. Help > Keyboard Shortcuts lists every key and mouse binding.
- **Joins are visible.** The video scrub bar shows a thin yellow bar at each point where one
  source file ends and the next begins, and the right end of the bar names the file the
  frame on screen comes from (with its place in the sequence, such as "2 of 5").
- **No frame lost at a join.** Garmin VIRB files mark their last frame "discard" and FFmpeg
  then drops it, which cost one frame at every join. CamBot now keeps it. Stepping backward
  across a join also works when a file's last frame isn't where its length says it should be.
- **Letter shortcuts follow your keyboard layout.** F, comma, period and Ctrl+S are the keys
  that make those characters in your current layout (so S is under the right little finger
  on Dvorak and the left ring finger on QWERTY). The layout is re-checked about once a
  second, so switching layouts while CamBot runs works. Space, the arrows and Esc don't
  depend on layout. On layouts without Latin letters the QWERTY positions are used.
- **Missing files.** If a video or the telemetry file has moved, CamBot looks beside the
  project file first, then asks you to locate it (and remembers where the first one was
  found for the rest).
- **Errors instead of crashes.** A video that can't be opened, a file that doesn't match
  the others, an export that can't start, and a project that can't be read or saved are
  now explained in a message box. Cancelling the project dialog just closes the program.
- **Playing on after stepping back** continues from the frame on screen instead of
  jumping ahead, and stepping forward past the cached frames and back no longer skips.
- **10-bit video** (e.g. HEVC from newer cameras) is shown by converting to 8-bit. A proper
  high-bit-depth path comes with the output-resolution work.
- **Tests.** `tests/run_tests.sh` (run it with `wsl ./tests/run_tests.sh`) builds and runs
  the unit tests: climb filters, map zoom, flight statistics, altitude references, user
  settings, the source timeline and file ordering, project reading, and the multi-file
  decoder (against a fake FFmpeg that serves small synthetic files).

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
- Export still renders at 1920x1080 whatever the source resolution; an output-size choice
  is the next piece of work.
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
