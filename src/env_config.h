#pragma once

#include <limits>
#include <vector>

#include "font_manager.h"
#include "media_container_manager.h"
#include "altitude_reference.h"
#include "project_file_manager.h"
#include "user_settings.h"
//#include "telemetry.h"

class TelemetryMgr;
class TelemetrySlice;

// Defined in main.cpp; the render loop's single playback-paused flag. EnvConfig::pause()/
// is_paused() are the way code outside main.cpp (widgets, in particular) reach it, so there
// stays one source of truth for whether the video is advancing on its own.
extern bool paused;

class EnvConfig {
public:
	EnvConfig(MediaContainerMgr* mcm, FontManager* fm, ProjectFileManager* pfm, float screen_width, float screen_height, float ui_height);

	//Setters
	bool screen_height(float height)                       { m_screen_height = height; return true; }
	bool screen_width(float width)                         { m_screen_width = width; return true; }
	// Thin forwarders -- TelemetryMgr owns m_telemetry_offset and the telemetry-side math now; this
	// just combines it with media_in_elapsed() for the ~10 call sites (widgets, mostly) that already
	// reach it through EnvConfig and don't need to change.
	bool telemetry_offset(float offset);
	bool advance_to_parametric(float parametric)           { return media_mgr->advance_to_parametric(parametric); }
	bool advance_by(uint64_t frame_count)                  { return media_mgr->advance_by(frame_count); }
	bool rewind_by(uint64_t frame_count)                   { return media_mgr->rewind_by(frame_count); }
	// Fast, approximate seek for interactive dragging/scanning -- see MediaContainerMgr::
	// seek_to_keyframe_near for why this is the one to reach for from a widget, not advance_to_parametric
	// / advance_by() / rewind_by().
	bool seek_to_parametric_fast(float parametric)         { return media_mgr->seek_to_parametric_fast(parametric); }
	bool seek_by_frames_fast(int64_t frame_delta)          { return media_mgr->seek_by_frames_fast(frame_delta); }
	bool launch_time(float launch_time);
	// The settings below belong to the user, not the project (see user_settings.h), so a look
	// dialed in once carries over to every video.
	//
	// Re-smooths every telemetry slice's climb rate with new settings (TelemetryMgr does the work)
	// and saves the user settings. False, changing nothing, while telemetry is still loading.
	bool climb_filter_settings(const ClimbFilterSettings& settings);
	// Re-fits every slice's map zoom to new limits; same rules as above.
	bool map_zoom_settings(const MapZoomSettings& settings);
	// +/- ft/min that the map course colors and the climb indicator span. Doesn't save by itself
	// (it's set every frame while a slider is dragged) -- call save_user_settings() when done.
	void climb_color_range(float range)                    { m_user_settings.climb_color_range = range; }
	// Map course trace widths and recent period. Same deal: set freely every frame, then
	// save_user_settings() when the user lets go.
	void map_trace_settings(const MapTraceSettings& settings) { m_user_settings.map_trace = settings; }
	// Flight summary duration and XC toggle. Same deal: set freely, then save_user_settings() on release.
	void summary_settings(const SummarySettings& settings) { m_user_settings.summary = settings; }
	void save_user_settings() const                        { m_user_settings.save(); }
	// The true altitudes (ft) the user gave for the start and end of the video. These belong to
	// the project (they're about this video's location), so setting them saves the project file.
	// They take effect through sync_altitude_correction().
	void altitude_references(const AltitudeReferences& refs) { m_altitude_refs = refs; save_project(); }
	// Call once per frame: (re)applies the altitude references to the telemetry whenever they,
	// or where the video starts and ends in telemetry time, have changed. Cheap otherwise.
	void sync_altitude_correction();
	// Pauses normal playback (used when an interactive scrub/scan begins); there's no un-pause
	// call here because nothing currently auto-resumes -- releasing the mouse just leaves it paused.
	void pause()                                           { paused = true; }

	//Getters
	//TODO(P0): set these to sane values (0 and whatever the file max is) when the video is read and
	//          prevent them being set outside that range.
	float                   media_height()           const { return media_mgr->get_height(); }
	float                   media_width()            const { return media_mgr->get_width(); }
	float                   screen_height()          const { return m_screen_height; }
	float                   screen_width()           const { return m_screen_width; }
	// The strip of screen_height() reserved below the video for the scrubbers/graph/map -- the
	// video's own display rect is screen_height() - ui_height() tall. Needed by
	// MediaContainerMgr::render() to know the video rect's real aspect ratio for its rotation
	// cover-scale (see 3.3.shader.vert).
	float                   ui_height()              const { return m_ui_height; }
	float                   frame_rate()             const { return media_mgr->frame_rate(); }
	bool                    is_paused()              const { return paused; }
	int32_t                 telemetry_index()        const;
	const TelemetrySlice&   telemetry_slice()        const;
	float                   telemetry_offset()       const;
	const UserSettings&     user_settings()          const { return m_user_settings; }
	const ClimbFilterSettings& climb_filter_settings() const { return m_user_settings.climb_filter; }
	const MapZoomSettings&  map_zoom_settings()      const { return m_user_settings.map_zoom; }
	float                   climb_color_range()      const { return m_user_settings.climb_color_range; }
	const MapTraceSettings& map_trace_settings()     const { return m_user_settings.map_trace; }
	const SummarySettings&  summary_settings()       const { return m_user_settings.summary; }
	const AltitudeReferences& altitude_references()  const { return m_altitude_refs; }
	// Where the first and last frames of the video fall in telemetry, as sample indices clamped
	// to the recording -- the moments the altitude references describe.
	int32_t                 telemetry_video_start_index() const;
	int32_t                 telemetry_video_end_index()   const;
	// Telemetry's own recorded length, and the current media position mapped onto it -- see
	// TelemetryMgr::duration()/elapsed_at(). Used by TelemetryScrubWidget.
	float                   telemetry_duration()      const;
	float                   telemetry_elapsed()       const;
	// Where the video's first frame falls in telemetry, as of the last time the launch marker
	// was (re)placed by launch_time() above: telemetry's elapsed time at the marker (snapshotted
	// then -- not a live function of the current sync offset, so an ordinary TSW drag afterward
	// can't move it), less how far into the video the marker is. This is the start of the window
	// the map, the altitude graph and the flight summary restrict themselves to, and what
	// TelemetryScrubWidget's left cutoff marker displays.
	float                   telemetry_window_start_elapsed() const;
	// The telemetry index range backing that window -- one video-duration wide, so it covers
	// exactly the telemetry that's rendered -- clamped to the telemetry file's actual recorded
	// bounds. Used by the map, the altitude graph and the flight summary so none of them pull in
	// telemetry recorded outside that window (TelemetryMgr itself stays video/launch-agnostic, so
	// this combination lives here rather than there).
	int32_t                 telemetry_window_start_index() const;
	int32_t                 telemetry_window_end_index()   const;
	// Where the launch marker falls in telemetry (clamped to the recording).
	int32_t                 telemetry_launch_index() const;
	// Distance flown (km), from the launch marker to the current playhead (zero before launch) --
	// replaces a plain telemetry_slice().m_total_distance read, which was cumulative since
	// telemetry recording began.
	float                   telemetry_distance_flown() const;
	float                   media_in_elapsed()       const; // Wall time passed since start of video.
	float                   media_in_duration()      const; // Wall time length of video.
	float                   media_out_elapsed()      const;

	// Current time, [0.0..1.0] from beginning to end of telemetry. Used by widgets to interpolate place
	float                   time_parametric()        const;
	float                   flight_time()            const; // Wall time since launch. (negative: wall time until launch)
	
	void                    save_project();

	static EnvConfig*       instance;
	FontManager*            font_mgr;
	MediaContainerMgr*      media_mgr;
	ProjectFileManager*     project_file_mgr;

	const glm::mat4& screen_to_pixel_space_projection() const { return m_screen_to_pixel_space_projection; }
	const glm::mat4& pixel_to_screen_space_projection() const { return m_pixel_to_screen_space_projection; }

private:
	void                    recompute_projections();
	float                   m_launch_time;
	AltitudeReferences      m_altitude_refs;
	UserSettings            m_user_settings;

	uint64_t                m_media_pts_start;
	uint64_t                m_media_pts_end;

	float                   m_screen_width;
	float                   m_screen_height;
	float                   m_ui_height;

	glm::mat4               m_screen_to_pixel_space_projection;
	glm::mat4               m_pixel_to_screen_space_projection;
};