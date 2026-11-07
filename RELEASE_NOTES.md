# CamBot release notes

## 1.0.0 (November 2026)

CamBot overlays custom flight telemetry (altitude graph, map, climb/sink rate, date/time, and so on) onto video from a camera on a
glider, and is a single-track video editor around that: cut, reorder and speed up or slow down clips, fade and crossfade between
them, put text on the video, line the telemetry up with the picture on a map, and export the result as an MP4 with sound.
Windows only; export needs an NVIDIA GPU.

### What it does

- **Editing.** The footage of a flight is played as one video and cut with Split, Trim Left / Right and Delete, clips are moved with
  Ctrl+drag, each clip has a speed (0.1x to 16x) and a treatment for its sound, and each join and the two ends can be a fade or a
  crossfade. Everything is undone with Ctrl+Z.
- **Text.** Text boxes with fades, fonts, colours and shadows are placed on the picture and timed on a track of their own.
- **Sound.** The sound of the edit is played in the preview and exported.
- **Camera Data (debug).** A pane for looking inside the metadata of DJI cameras. Details below.
- Loads the video files of a flight (as one continuous video) and, if there is one, a telemetry log, and lets you scrub both
  together on the timeline, including right-click scrubbing.
- Sync telemetry to video in the Sync / Reorder view, by clicking the map's path when the video is on a moment you can
  recognize, and set the launch point from the Edit menu.
- Rotate and zoom the video to level the horizon. The rotation angle is saved in the
  project file, and the exposed edges and corners are trimmed so the rotated frame never
  pokes into the UI.
- Save and reload projects (`.ffsw`), including the telemetry offset, launch point and
  rotation.
- Save a raw video frame, or a frame with the telemetry overlay.
- Export the edited video with the overlay and the sound of the edit (AAC), on the GPU.
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

### Editing, sync, sound and text

- The program now keeps the footage as a *library* of files, a *capture order* (the order they
  were recorded, which is the flight clock telemetry is synchronised to) and a list of *clips*
  (the edit timeline). A new project's clips are every file whole, in capture order; cutting,
  moving and changing clips edits that list, and the telemetry follows the footage.
- Telemetry sync is now stored as sync points tied to a file and a moment in it, instead of one
  number for the whole flight, so clips will show the right telemetry wherever they are placed,
  and a camera clock that runs fast or slow is spread evenly between two points. One
  sync point behaves like a single offset for the whole flight (the Up/Down nudge in the Sync / Reorder view). The launch marker is likewise tied to a file and a moment in it.
- **New window layout.** The window is resizable (minimum 900x600) and opens at the size, and
  maximized or not, it had when last closed (first run: 1200x900). The video and its overlays
  are shown as a preview that is as big as the window's height allows while keeping its shape,
  with only a thin border around it; the left panel takes all the width the preview doesn't use,
  and the divider between them can be dragged to make the panel wider (and the preview smaller),
  with the width remembered. A transport bar sits under the preview: the position as `h:mm:ss#frame / length (@29.97fps)`
  and the two screenshot buttons on the top row, and Start, -1, Play/Pause, +1, End below.
  Reverse, slow and fast play are shown but off (playback only goes forward). The left panel holds the sections (Source
  Material, Speed, Transition, Text and Camera Data). Everything is drawn through ImGui, so none of it ends up in exports.
- Each frame is drawn into an off-screen picture (the video with its overlays, 1920x1080) and then copied to the window. Exports and both screenshot
  buttons read that picture rather than the window, so the saved frame no longer depends on
  the window. Nothing should look different.
- **Timeline.** A timeline now runs the full width of the window along the bottom,
  with a draggable divider above it; drag the divider to make it taller or shorter (the height
  is remembered, minimum 110 pixels). Each source file is a coloured block with its name,
  yellow lines mark the joins, a ruler shows time, and a red line shows the playhead. Left-click
  or drag to scrub (it follows keyframes while you drag and lands on the exact frame when you
  let go), right-drag to scan at a speed that grows with the distance dragged (the same feel as
  the old scrub bar), mouse wheel to zoom around the cursor (or around the very start or end of the video, when the cursor is
  within a few percent of the timeline's width of it), Shift+wheel, middle-drag or a left-drag on the ruler to pan,
  and the Fit button to show everything. The timeline follows the playhead during playback.
  This replaces the video scrub bar that used to sit under the video.
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
  tied to when each piece was recorded. Dragging clip edges to trim is not planned.
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
  the telemetry file.
- **Moving clips.** Hold Ctrl and drag a clip on the timeline to put it somewhere else in the order:
  a copy follows the mouse, the original goes nearly black where it was, and a white mark with a
  pulsing glow, running the whole height of the timeline, shows where it will land (drop it past the
  middle of a neighbour to swap with it). The picture on screen stays the
  same even though its place on the timeline changes. It can be undone like the cuts. A Ctrl+click
  that doesn't move does nothing. Turning the mouse wheel quickly now zooms the timeline a good deal
  less than it did (a click or less counts in full, the rest of a fast flick at 40%).
- **Sync / Reorder view.** **Sync / Reorder...** in Source Material now opens a view
  that takes over the window, for lining up the telemetry and putting the video files in order. It
  plays *all* the footage, every file whole in the order recorded, whatever your edit is (the edit
  is kept aside, saved with the project as usual, and comes back, showing the frame it was on, when
  you press **Done** or **Cancel**); the telemetry shows in full, not just the part your edit uses.
  The video is on the right with the usual transport bar under it, and below is a timeline of the
  files, the same one the editor has (click or drag to move through the footage, right-drag to scan,
  wheel to zoom, a ruler of times), with a divider above it to make it taller, sharing the editor's
  height. **Ctrl+drag a file** to change the order they were recorded in; the picture stays put. Done
  asks first if the new order would change the telemetry under clips you have already cut; Cancel
  puts the order, the sync and the launch mark back as they were when the view was opened. Editing
  keys, Undo and Export are off while it is open; the Up/Down sync nudge keys still work, and the
  strip under the video is for looking only.
- **Timeline buttons.** The Fit, Split, Trim, Delete, Undo and Redo buttons above the timeline did not
  respond to the mouse (the keys always worked): the thin divider above the timeline was an invisible
  window ImGui made 32 pixels tall, which reached down over the buttons and took their clicks. The
  dividers are now exactly their own size.
- **Smoother map tracks.** The telemetry logger records positions in steps of about 0.4 meters, which
  showed up as a staircase in the track when zoomed in far. Once the whole telemetry file has been
  read, each position used for *drawing* is averaged with its neighbors (a fifth of a second either
  side). The video's map course and its glider, and the sync map, are drawn from the smoothed
  positions (the map is redrawn from them a moment after the file finishes loading); distances,
  speeds and everything else still use the positions as logged.
- **Sync / Reorder view: setting the sync from the map.** Put the video on a moment you can recognize
  (the top of a wingover, the launch), then **click the path** where that happened: the frame on screen
  is now at that point of the telemetry (between samples, if that's where you clicked) and playback
  pauses. Before you click, a ring on the path shows the point a click would pick, and the cursor
  becomes a hand over it. Where the path went by under the mouse more than once, the pass nearest in
  time to the current sync is picked first, and clicking again at the same spot goes on to the next one,
  round and round; a small "2 of 4" by the mouse says which of how many. A click, or a Ctrl+press, away
  from the path does nothing. **Ctrl+drag** picks a point the same way and then slides the sync along
  that pass of the path, following the mouse wherever it is, so a point under a stack of crossing paths
  can be picked nearby and slid into place (it stays within five seconds of where it is, so it can't
  jump to another lap). The map stays where it is during all this. **Ctrl+Z** and **Ctrl+Y** (or
  Ctrl+Shift+Z) undo and redo what has been changed in the view: the sync (by clicking, sliding or the
  Up/Down keys; a held key, or a whole slide, is one step), **Set Activity Start**, and moving files.
  They don't move the video, so you can stop on another frame and flip between two syncs to compare
  them. The history starts empty each time the view is opened, and Cancel still puts everything back as
  it was on entry.
- **The sync is set only in the Sync / Reorder view.** The telemetry scrub bar and the grey strip (134 pixels)
  under the video are gone: the picture CamBot draws, shows and exports is now just the 1920 x 1080 video with
  its overlays, which are in the same places on it as before, and the preview is a little bigger. The
  Up/Down nudge keys now work only in the Sync / Reorder view, so the sync can't be changed by accident
  anywhere else. In that view, **right-dragging on the map** moves the sync the way right-dragging the old
  bar did, and the way it moves the video on the timeline: hold the right button anywhere on the map and
  drag sideways, and the further from where you pressed, the faster the path runs under the frame on
  screen (to the right takes the frame later along it), a telemetry sample at a time. The whole drag
  is one undo step, and the map keeps the arrow in sight.
- **Done in the Sync / Reorder view leaves the playhead on the frame that was on screen**, wherever the
  playhead was when the view was opened (Cancel still puts it back there). If the edit has cut that footage, it
  goes to the nearest footage that is left.
- **Pictures on the timeline.** Each clip on the timeline (and in the Sync / Reorder view's footage
  timeline) is now filled with small pictures of the video, like other editors' film strips. The first
  picture sits against the clip's left edge and shows the exact first frame of the clip; each one after
  it shows the moment of the clip that lies at its own left edge, so zooming changes what they show,
  and the last is cut off where the clip ends. Apart from the first, a picture is the keyframe at or
  before that moment, which is quick to get and is kept for next time. They are decoded on a thread
  of their own (never while an export runs), and until the right one arrives the nearest one of the same
  file stands in, so zooming blurs for a moment instead of going blank. Each clip has a rounded line in
  its file's color around it and the part of the ruler above it is tinted with that color; the colors
  (the Okabe-Ito set) are chosen to be told apart by people who see red and green poorly. In the clip's
  top left corner is the file's number (its place in recording order, from 1, on a patch of its color)
  and its name on a dark plate, so footage that has been cut up and shuffled can still be told apart. A
  clip too narrow for the name shows only the number, and hovering it names the file. The timeline's
  clips can now be up to 120 pixels tall (drag the divider above it).
- **Sync / Reorder view: a picture under the map.** The sync map now has an OpenStreetMap street map
  under the path, so the sync can be checked against roads, the shoreline and the landing field. It
  is always on, and darkened (25% to begin with) so the path stands out; the **Dim** slider in the
  strip between the map and the video changes that, and is remembered. The **Street / Satellite**
  button switches to USGS satellite imagery (United States only; at the deeper zoom levels CamBot first tries the USGS ready-made tiles, and when two
  of them turn out not to exist it asks the USGS NAIP imagery service, 0.3 to 0.6 meters a pixel, to draw each
  one instead for the rest of the run; the console says how long each tile waited, took to start answering and took in all, since those pictures can take half a minute), which is also what the map falls
  back to, with a note saying so, if the street map can't be reached and none of it is stored for
  the part of the world in view. (Click Street to try again.) "Map tiles unavailable" shows if
  neither can be had. The credit each source asks for is in the lower right corner. The tiles come
  over HTTPS from background threads, only for what is on
  screen (two requests at a time; OpenStreetMap never gets more, but when the USGS is slow to answer
  some, each one that has been out for over a second and a half lets one more go out, up to six, so a few
  slow pictures don't hold up the rest), and while one is on its way the same place from a lower zoom level is stretched in its
  place. CamBot identifies itself to the tile server as `CamBot (https://github.com/foodini/cambot)`.
  Every tile is kept in `%LOCALAPPDATA%\CamBot\map_tiles` (up to 200 MB; the oldest go first) and
  used as it is for six months, so editing a flight a few days or weeks old costs the free servers
  nothing; **Refresh Map** asks them whether the tiles now in view that are over a week old have
  changed. A server that fails to answer is left alone for longer and longer. Each tile request and
  answer is printed in the console window. As soon as the telemetry has been read, before the view is
  opened, CamBot starts fetching the (at most ten) street map tiles around where the flight starts: the
  zoom 18 tiles covering the walk to launch (up to the first sample faster than 6 knots, with 60 m
  added all round) and one wider tile at each of the zoom levels 17 to 14, so they are there when
  the view opens. (Note: the Windows build links WinHTTP, which is part of Windows; no new libraries.)
- **Sync / Reorder view: the map.** The left half is now a map of the whole flight (everything in the
  telemetry file, not only what your edit uses). The path is a bright line with a dark edge, in the
  same climb colors as the video's map (dark blue in strong sink to red in strong climb, over the same
  climb color range; the darkest blues are lightened a little so they show on the dark map), and all of
  it alike, so what is ahead of the frame on screen reads as easily as what is behind it. Where points
  are far enough apart on screen to show as corners (zoomed in), the line is drawn as a curve through
  them. A dot marks where the recording starts, a square where it ends, and a diamond where the
  activity starts. A yellow arrow shows where the frame on screen is and which way it was heading
  (dimmed if the frame is outside the telemetry's recording). The heading is the direction the path is
  going at that moment, taken from the sample before to the sample after, rather than the course the
  GPS reports, which the receiver has already averaged over a second or so and so trails a quick turn
  by a few tenths of a second (in the sample flight, 0.4 to 0.8 seconds, up to 70 degrees off in a
  wingover); it uses the logged course only where the path can't say, as when standing still. Turn the wheel to zoom (around the mouse), drag to move the map. The map
  starts out showing the whole flight. When the arrow moves up near an edge of the map (as the video
  plays or you scrub) the map follows it, keeping it in the middle half of the pane, and a little
  toward the back, so there is more room ahead of it (the way it has been heading over the last couple
  of seconds decides which side; a quick turn back on itself, such as a wingover, cancels out so the map
  stays put). Dragging the map
  or pressing **Fit** (the whole flight) stops that until the arrow next moves near an edge; **Center**
  puts the arrow in the middle and follows right away (the button is lit while following). Between the map and the
  video is a new strip with the **Set Activity Start** button, which makes the frame on screen the start
  of the activity (the same as Edit > Mark Launch Point), with the time it is set to underneath.
- **One flight, many video files.** Cameras split a long recording into pieces; a new
  project now takes all of them (select several files in the dialog) and plays them as one
  continuous video. Scrubbing, frame stepping, the timeline and export all run across the
  joins. The files have to match (same codec, resolution, frame rate and sound format);
  if one doesn't, CamBot says which file and why. GoPro chapter names (`GX010123`,
  `GX020123`, ...) and numbered names are put in recording order automatically, and the
  order is shown for confirmation. Audio is carried across the joins; where one file's
  sound overlaps the next by a few milliseconds the overlap is trimmed.
- **Projects are JSON now.** The `.ffsw` file is readable JSON (now format version 7). Versions 2 to 6
  open and are converted when next saved; older ones are not read. Saving writes to a temporary file and swaps
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
- **Text on the video.** The left panel has a new Text section. *Add* puts a
  text box ("Lorem Ipsum") on the picture, that is fully in at the frame being shown (it fades in over the half
  second before, so what you type is seen at once), holds for four seconds and fades out over half a second. Type what it should say in the field
  below the list, and *Delete Box* (or the Delete key, while a box is selected in the list) removes
  it. A text box belongs to a frame of the footage, so it stays with that part of the video when
  clips are cut or moved. The text is white with a black shadow, centered in the lower part of the
  picture, drawn in Arial and wrapped to the width of its box; it is on the preview, in exports and
  in the overlay screenshot, but not in the Sync / Reorder view. Any language Windows' fonts cover
  can be typed. Adding, removing and typing are undone with Ctrl+Z (a stretch of typing is one
  step). Text boxes are saved in the project file (format version 4; older projects still open).
  The box selected in the list is drawn on the preview as a dashed rectangle with a square handle at
  each corner: drag a corner (or anywhere close to it) to reshape the box (the opposite corner stays
  put), drag an edge to move just that side, or drag inside it to move it. It can't leave the picture or be squeezed smaller than a few percent of it, and a whole
  drag is one undo step. The two sections of the left panel, Source Material and Text, now start
  closed.
  **On the timeline,** text boxes have a track of their own below the clips: each box is a block as long as it shows (darker
  where it fades in and out) with the start of its words in it, on separate rows where boxes overlap (make the timeline
  taller to see more rows). Click a block to select the box and take the playhead to where the box is fully showing (the end of its fade in); hold Ctrl and drag a block to move the box
  in time, or drag either end of a block to make it start earlier or later or stop sooner or later. The box stays tied to
  a frame of the video, so it is re-tied to whichever frame it is now fully shown at; moving an end changes the dwell (and shrinks
  the fades only if there is no longer room for them). Each drag is one undo step.
  **Looks and timing.** With a box selected, the Text section also has its *Timing* (fade in, dwell and fade out, in
  seconds), *Type* (font from Arial, Segoe UI, Verdana, Times New Roman, Georgia, Courier New and Impact; size in pixels on
  a picture 1080 tall; bold, italic, strikethrough; left, center or right alignment; colour) and *Shadow* (opacity, with 0
  for none; how far across and down; colour). A colour swatch opens a picker with the red, green and blue values and a hex
  field, and below it the ten colours most lately used, for the text and the shadow alike. Each change is an undo step
  (a drag of a slider is one). Whatever you last changed on any box (its place and size in the picture, its type and
  colours, its shadow and its times) is what the next new box starts out as, and is remembered for the next run (in the
  user settings, with the recent colours).
- **Launch point.** The Edit menu item formerly called "Mark Launch Point" is now **Set Launch Here** (with a tooltip
  saying what it does: the flight timer and the distance flown count from the frame on screen). A project remembers
  whether you have set it. If you export without having done so, CamBot says so and offers to start the timer at the first
  frame of the exported video (0:00), or to let you go back and set it properly.
- **Export.** Export Video is unavailable while the telemetry is still being read (it would be missing the rest of the
  flight). When an export stops, a box now tells you how it went (finished, cancelled with Esc, or a problem, with
  what it was) and waits for OK, instead of a line of text at the bottom of the picture.
- **Telemetry files.** A telemetry file that can't be opened, or has no samples in it, is now reported (once, as soon as it
  is known) and the flight carries on as video alone. Blank lines in a telemetry file are skipped instead of being read as a
  broken sample.
- **Text panel.** A line under *Timing* (and tooltips on the size, opacity and offset fields) say that values are dragged to
  change them and Ctrl+click types one.
- **Sound in exports is now made from the edit.** It used to be copied, packet for packet, from the camera files, which
  clicked at cuts and could drift where clips were moved or files joined. CamBot now reads each clip's sound from its file,
  plays the clips one after another with a 10 ms fade each side of every cut (none where a clip just carries on from the
  one before, as after a split), and encodes the result as AAC (48 kHz stereo, 192 kbit/s). The sound is laid out from the
  edit itself, so it stays in step with the picture however the clips are cut and arranged. A file whose sound can't be
  read comes out silent, and the export says so when it finishes.
- **Sound while you edit, and real-time playback.** Play now plays the sound of the edit through the speakers (Windows'
  built-in audio, no new files to install), with a **Mute** button and a **Volume** slider at the end of the transport bar's
  lower row; both are remembered for next time and affect only what you hear, never an export. Playback also now keeps real
  time: the picture follows the sound (or, for footage with no sound or a machine with no speakers, the computer's clock), and
  if the machine can't decode fast enough it skips frames instead of running slowly. Before, playback advanced one frame per
  screen refresh, which only looked right when the screen's rate matched the video's. Stepping, scrubbing and the other ways
  of moving around are silent, and playing carries on from wherever you put the picture.
- **Speed.** A new **Speed** section in the left panel sets how fast the clip under the playhead (the one outlined on the
  timeline) plays: buttons for 0.25x, 0.5x, 1x, 2x, 4x and 8x, or any speed from 0.1x to 16x typed into the field (press Enter).
  The clip becomes longer or shorter on the timeline and what follows moves up or out to suit; it keeps the footage it had,
  so trimming and splitting still work on the footage, and a split gives both halves the speed. The telemetry goes along with
  the picture, so the overlay speeds up and slows down with it, while text boxes keep their own lengths. A clip off normal speed
  shows its speed in orange at the bottom left of its block. In preview and in the export, a slowed-down clip repeats frames
  and a sped-up one drops them (no blending). Each change is one undo step, and speeds are saved with the project (project
  format 5; older projects open at normal speed).
  The clip's sound has three treatments, chosen with the radio buttons under the speed (they apply once the clip is off normal
  speed): **Keep pitch** stretches or squeezes the sound in time without changing how high it is (a time-domain stretch, fine for
  speech and wind between about 0.5x and 2x, increasingly stuttery beyond), **Change pitch** plays it like a tape (higher when fast,
  lower when slow), and **Mute**. This is what you hear in preview and what the export carries.
  Known limitation: while a slowed-down clip is held on one frame, the overlay (telemetry, text fades) changes only when the
  frame does.
- **Transitions.** Every join between two clips, and the start and the end of the video, now has a small square on
  the timeline, half over each clip: click it to pick that *transition* (a clip is picked by clicking it). An empty square is a plain
  cut. Splitting a clip picks the new join, and Trim Left / Trim Right pick the transition at the edge they moved, ready for the **Transition** section of the left panel. At the start of the video the
  section offers **Fade in from black**, at the end **Fade out to black**, with a length in seconds (to the nearest frame, up to half
  the clip beside it); the whole picture, telemetry and text included, and the sound fade together, in the preview and the export. The
  square shows a ramp for a fade. A fade is one undo step and is saved with the project (project format 6; older projects open with no
  fades). Trim, Split and the Speed buttons are greyed when they would leave a fade longer than its clip allows; removing or moving
  clips shortens a fade to fit instead.
  **Crossfade** is the third choice at a join: the end of one clip and the start of the next play together, the picture blending
  from one to the other and the sound mixing with an equal-power curve (so the level stays steady), and the video gets shorter by the
  length of the crossfade. A crossfade can't be longer than half of either clip beside it. The timeline shows the overlap as a band
  and an X in the square. In the crossfade the overlays and telemetry belong to whichever clip is showing more of itself (they jump at
  the middle); scrubbing, stepping and playing all show the blend, and exports with crossfades are cut at the timeline's frame
  times, as with speed changes. Two decoders run in a crossfade, so playback can stutter at the start of one on slow machines.
  Under a transition that is set, **Apply to all transitions** gives its type and length to every other of its kind (all the joins,
  or the start and the end), and **Apply to un-configured transitions** only to those that have none; joins where the footage carries
  straight on (the same file, or one file ending as the next begins) are left alone.
  Trim, Split, Speed and Delete act on the clip you picked, or else on the clip under the playhead (in a crossfade the one showing
  more of itself). They are greyed while the video plays, and pressing their keys then pauses the video and says the edit was not
  done (press again once it has stopped).
- **Camera Data (debug).** A new section at the bottom of the left panel (closed until opened) shows what a DJI camera wrote into its
  video file about itself: the `djmd` stream (orientation, accelerometer, exposure, GPS when the camera had a fix) and the `dbgi`
  debugging stream, for the frame on screen, as a tree of fields with their values and, for the ones known (from ExifTool's DJI tag
  list), their names. Hover a field for the other ways its bytes can be read; click a number to plot that field over the whole file (a
  red line marks the playhead), which is how to tell what is worth using. The plot appears at once and fills in as the file is read,
  starting at the playhead, so the part being looked at is there first. The values most worth having at hand
  (frame, time, ISO, shutter speed, colour temperature, accelerometer X/Y/Z with the direction of each axis, zoom, and the camera's barometer: altitude and air pressure) are repeated in a
  "Key values" list above the tree; click one to plot it. The size of the accelerometer
  reading (`|a|`, about 1 g at rest) is there too, and plots like a field. The first plot of a stream reads the whole of it from the file
  (the part at the playhead first) and keeps it in memory, so after that any other field of the stream plots at once. The files name no schema we can read for the Osmo Action 6, so
  the messages are decoded without one and the odd field may be shown as the wrong kind of thing. It does nothing while the section is
  closed or an export runs.
- **Text boxes stay with the frame they were added on.** A new text box fades in so that it is fully shown at the frame on screen
  when **Add** is pressed, and it is tied to that frame, not to the frame the fade in begins on. So when the fade in starts in the
  clip before (or before the start of the video), the box still belongs to the clip it was added on, and goes wherever that clip
  goes; changing the fade in moves where the box begins, not the frame it is tied to. This changes the project format (7); older
  projects are converted when opened, with every box showing at the same times as before.
- **Left panel headers.** The section headers (Source Material, Speed, Transition, Text, Camera Data) are half as big again as the
  text, on a teal background of their own, and what is under each is indented by 4% of the panel's width, so it is clear which
  section a control belongs to.
- **Tests.** `tests/run_tests.sh` (run it with `wsl ./tests/run_tests.sh`) builds and runs
  the unit tests: climb filters, map zoom, flight statistics, altitude references, user
  settings, the source timeline and file ordering, project reading, and the multi-file
  decoder (against a fake FFmpeg that serves small synthetic files). New: speed (edit
  arithmetic, cuts, decoder frame choice), the audio stretcher, and the project file's JSON
  (read and written with jsoncpp's source, from the vcpkg folder; skipped if that isn't there).

### Changes from the earlier builds

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
  is planned.
- Telemetry loading is single-threaded. A 2-3 hour log (about 72,000-96,000 lines) still
  takes several seconds. Multithreaded parsing or a faster file format is the likely next
  step if that matters.
- The circling detector and the best-glide measure in the flight summary use first-guess
  thresholds (a full turn within 45 s to start zooming; 360 degrees of one-direction turning
  to count a circle; a 60-second glide that loses at least 100 ft). They have not yet been
  tuned against a real thermaling flight.
- The best glide is a ground glide (straight-line ground distance over altitude lost), so
  wind inflates or deflates it.
- The telemetry timezone correction is computed once per file, so a log that spans a DST
  change will be off by an hour after the change.
- In a crossfade the overlays and telemetry belong to whichever clip is showing more of itself, so they jump at the middle of it.
- A crossfade runs two decoders, so playback can stutter at the start of one, most on slower machines. Sync recovers by itself.
- While a slowed-down clip is held on one frame, the overlay (telemetry, text fades) changes only when the frame does.
- Sound at off-normal speed with **Keep pitch** is a simple time-domain stretch: fine for speech and wind between about 0.5x and 2x,
  increasingly stuttery beyond that. There is no reverse playback.
- Exports with speed changes or crossfades are cut at the timeline's frame times (the frame grid of the output), so they take
  longer than a plain export.
- After cancelling an export, Windows Media Player sometimes refuses the unfinished file the first time it is opened (error
  0x80070323) and plays it the second time. It is on `TODO.txt`.
- **Camera Data (debug)** reads the camera's streams without a schema, so some fields may be shown as the wrong kind of thing, and the
  few names given are from ExifTool's list or are guesses (those end in "?"). The units of the barometric altitude (decimetres) are
  worked out from comparing it with the collector, not from DJI. A stream is kept in memory up to 256 MB; past that the plot shows
  only the part that fitted. Nothing in it is used for overlays yet.
- See `TODO.txt` for the wish list, including labels pinned to ground landmarks, tracking other gliders, a vario tone, an executable icon and a map aesthetic pass.
