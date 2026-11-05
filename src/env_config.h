#pragma once

#include <limits>
#include <vector>

#include "font_manager.h"
#include "edit_model.h"
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
	EnvConfig(MediaContainerMgr* mcm, FontManager* fm, ProjectFileManager* pfm, EditModel* model, float screen_width, float screen_height);

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
	// How much the sync view's map picture is darkened (0 to 0.9). Set freely, then save_user_settings() on release, like the others.
	void map_dim(float dim)                                { m_user_settings.map_dim = dim < 0.0f ? 0.0f : (dim > 0.9f ? 0.9f : dim); }
	// Map course trace widths and recent period. Same deal: set freely every frame, then
	// save_user_settings() when the user lets go.
	void map_trace_settings(const MapTraceSettings& settings) { m_user_settings.map_trace = settings; }
	// Flight summary duration and XC toggle. Same deal: set freely, then save_user_settings() on release.
	void summary_settings(const SummarySettings& settings) { m_user_settings.summary = settings; }
	void save_user_settings() const                        { m_user_settings.save(); }
	// What a new text box starts out as (see user_settings.h). Set freely; call save_user_settings() when the user lets go.
	void text_defaults(const TextSettings& settings)       { m_user_settings.text_defaults = settings; }
	// Which project is open: it is the one to open at the next start, and goes to the top of the recent
	// list. And the other way: the user closed it on purpose, so the next start should not open it.
	// Both save the user settings.
	void note_project_opened(const std::string& path)      { m_user_settings.note_project_opened(path); m_user_settings.save(); }
	void forget_last_project()                             { m_user_settings.forget_last_project(); m_user_settings.save(); }
	// The main window's size, kept for the next run. Saved by save_user_settings().
	void window_settings(const WindowSettings& settings)   { m_user_settings.window = settings; }
	// The true altitudes (ft) the user gave for the start and end of the video. These belong to
	// the project (they're about this video's location), so setting them saves the project file.
	// They take effect through sync_altitude_correction().
	void altitude_references(const AltitudeReferences& refs) { m_altitude_refs = refs; }
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
	float                   frame_rate()             const { return media_mgr->frame_rate(); }
	bool                    is_paused()              const { return paused; }
	int32_t                 telemetry_index()        const;
	const TelemetrySlice&   telemetry_slice()        const;
	float                   telemetry_offset()       const;
	const UserSettings&     user_settings()          const { return m_user_settings; }
	const ClimbFilterSettings& climb_filter_settings() const { return m_user_settings.climb_filter; }
	const MapZoomSettings&  map_zoom_settings()      const { return m_user_settings.map_zoom; }
	float                   climb_color_range()      const { return m_user_settings.climb_color_range; }
	float                   map_dim()                const { return m_user_settings.map_dim; }
	const MapTraceSettings& map_trace_settings()     const { return m_user_settings.map_trace; }
	const SummarySettings&  summary_settings()       const { return m_user_settings.summary; }
	const TextSettings&     text_defaults()          const { return m_user_settings.text_defaults; }
	const AltitudeReferences& altitude_references()  const { return m_altitude_refs; }
	// Where the first and last frames of the video fall in telemetry, as sample indices clamped
	// to the recording -- the moments the altitude references describe.
	int32_t                 telemetry_video_start_index() const;
	int32_t                 telemetry_video_end_index()   const;
	// Telemetry's own recorded length, and the current media position mapped onto it -- see
	// TelemetryMgr::duration()/elapsed_at().
	float                   telemetry_duration()      const;
	float                   telemetry_elapsed()       const;
	// The window of telemetry that is shown, in telemetry seconds: from where the first frame of
	// footage used falls in telemetry to where the last one does. It follows the clips and the sync
	// as they change (see EditModel::used_telemetry_span), so trimming footage trims the telemetry shown
	// too. The start and the end of that span.
	float                   telemetry_window_start_elapsed() const;
	float                   telemetry_window_end_elapsed() const;
	// The telemetry index range backing that window, clamped to the telemetry file's actual recorded
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
	// Where the frame on screen comes from on the flight clock: seconds into the footage laid end to
	// end in the order it was recorded (see edit_model.h). This, not media_in_elapsed(), is what
	// telemetry is looked up by; the two agree until the video is cut or rearranged.
	float                   flight_elapsed()         const;
	// Length of all the footage on the flight clock, which is the stretch of telemetry the windows
	// below describe (they don't shrink when clips are cut out of the video).
	float                   flight_duration()        const;
	float                   media_out_elapsed()      const;

	// Current time, [0.0..1.0] from beginning to end of telemetry. Used by widgets to interpolate place
	float                   time_parametric()        const;
	float                   flight_time()            const; // Time on the flight clock since launch. (negative: until launch)
	
	// Writes the project file now (File > Save, Ctrl+S). Returns false, having told the user
	// why, if it couldn't.
	bool                    save_project();
	// Call once per frame. Copies the live state (offset, launch, rotation, altitude references)
	// into the project, so project_unsaved() is right however the state was changed (menus, keys
	// or the sync view), and writes the recovery copy when one is due (see ProjectFileManager).
	void                    poll_project();
	bool                    project_unsaved() const   { return project_file_mgr->has_unsaved_changes(); }
	// For a normal exit: the recovery copy is no longer wanted.
	void                    discard_recovery()        { project_file_mgr->discard_recovery(); }

	static EnvConfig*       instance;
	FontManager*            font_mgr;
	MediaContainerMgr*      media_mgr;
	ProjectFileManager*     project_file_mgr;
	EditModel*              edit_model;

	const glm::mat4& screen_to_pixel_space_projection() const { return m_screen_to_pixel_space_projection; }
	const glm::mat4& pixel_to_screen_space_projection() const { return m_pixel_to_screen_space_projection; }

private:
	void                    recompute_projections();
	AltitudeReferences      m_altitude_refs;
	UserSettings            m_user_settings;
	void                    push_state_to_project();

	uint64_t                m_media_pts_start;
	uint64_t                m_media_pts_end;

	float                   m_screen_width;
	float                   m_screen_height;

	glm::mat4               m_screen_to_pixel_space_projection;
	glm::mat4               m_pixel_to_screen_space_projection;
};