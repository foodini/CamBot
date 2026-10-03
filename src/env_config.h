#pragma once

#include <limits>
#include <vector>

#include "font_manager.h"
#include "media_container_manager.h"
#include "project_file_manager.h"
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
	// Telemetry's own recorded length, and the current media position mapped onto it -- see
	// TelemetryMgr::duration()/elapsed_at(). Used by TelemetryScrubWidget.
	float                   telemetry_duration()      const;
	float                   telemetry_elapsed()       const;
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

	uint64_t                m_media_pts_start;
	uint64_t                m_media_pts_end;

	float                   m_screen_width;
	float                   m_screen_height;
	float                   m_ui_height;

	glm::mat4               m_screen_to_pixel_space_projection;
	glm::mat4               m_pixel_to_screen_space_projection;
};