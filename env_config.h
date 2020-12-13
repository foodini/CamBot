#pragma once

#include <limits>
#include <vector>

#include "font_manager.h"
#include "media_container_manager.h"
#include "telemetry.h"

class EnvConfig {
public:
	EnvConfig(MediaContainerMgr& mcm, TelemetryMgr& tm, FontManager& fm, float screen_width, float screen_height, float ui_height);

	//Setters
	bool screen_height(float height)                       { m_screen_height = height; return true; }
	bool screen_width(float width)                         { m_screen_width = width; return true; }
	bool telemetry_offset(float offset);
	bool advance_to_parametric(float parametric)           { return m_media_mgr.advance_to_parametric(parametric); }
	bool launch_time(float launch_time)                    { m_launch_time = launch_time; return true; }

	//Getters
	//TODO(P0): set these to sane values (0 and whatever the file max is) when the video is read and
	//          prevent them being set outside that range.
	float                   media_height()           const { return m_media_mgr.get_height(); }
	float                   media_width()            const { return m_media_mgr.get_width(); }
	float                   screen_height()          const { return m_screen_height; }
	float                   screen_width()           const { return m_screen_width; }
	int32_t                 telemetry_index()        const;
	const TelemetrySlice&   telemetry_slice()        const;
	TelemetryMgr&           telemetry_mgr()          const { return m_telemetry_mgr; }
	FontManager&            font_mgr()               const { return m_font_manager; }
	float                   media_in_elapsed()       const; // Wall time passed since start of video.
	float                   media_in_duration()      const; // Wall time length of video.
	float                   media_out_elapsed()      const;
	float                   telemetry_offset()       const { return m_telemetry_offset; }
	static EnvConfig&       instance()                     { return *c_instance; }

	// Current time, [0.0..1.0] from beginning to end of telemetry. Used by widgets to interpolate place
	float                   time_parametric()    const;
	float                   flight_time()        const; // Wall time since launch. (negative: wall time until launch)

private:
	static EnvConfig*        c_instance;
	MediaContainerMgr&       m_media_mgr;
	TelemetryMgr&            m_telemetry_mgr;
	FontManager&             m_font_manager;
	float                    m_launch_time;

	uint64_t                 m_media_pts_start;
	uint64_t                 m_media_pts_end;
	float                    m_telemetry_offset; 

	float                    m_screen_width;
	float                    m_screen_height;
	float                    m_ui_height;
};