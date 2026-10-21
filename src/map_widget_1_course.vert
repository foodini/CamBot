#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in float climb_rate_in;
layout (location = 2) in float sample_index_in;   // telemetry sample index this dot was laid down at

uniform mat4 projection;
uniform vec2 min_clip;
uniform vec2 max_clip;

// Trace appearance -- see EnvConfig::map_trace_settings(). trace_current_index is the playhead
// in telemetry sample indexes (fractional).
uniform float trace_current_index;
uniform float trace_sample_hz;
uniform float trace_recent_seconds;   // The most recent this-many seconds are drawn at trace_recent_width...
uniform float trace_recent_width;     // ...in pixels,
uniform float trace_old_width;        // easing down to this width for everything older.

// How many seconds of trace it takes to ease from the recent width down to the older width.
const float TRACE_TAPER_SECONDS = 10.0;

out float climb_rate;
out float trace_future;   // 1.0 for dots the glider hasn't reached yet, 0.0 for ones it has.
out float trace_recent;   // 1.0 for dots within the most recent trace_recent_seconds, drawn in their own opaque pass.
out float gl_ClipDistance[1];  // distance to each edge of the map, for clipping. //TODO(P1)
void main()
{
    gl_Position = projection * vec4(aPos.xy, 0.0, 1.0);
    climb_rate = climb_rate_in;

    float age_seconds = (trace_current_index - sample_index_in) / trace_sample_hz;  // negative: still ahead of the glider
    trace_future = age_seconds < 0.0 ? 1.0 : 0.0;
    trace_recent = (age_seconds >= 0.0 && age_seconds <= trace_recent_seconds) ? 1.0 : 0.0;
    float taper = smoothstep(trace_recent_seconds, trace_recent_seconds + TRACE_TAPER_SECONDS, age_seconds);
    gl_PointSize = trace_future > 0.5 ? trace_old_width : mix(trace_recent_width, trace_old_width, taper);
    gl_ClipDistance[0] = min(
        min(max_clip.x - gl_Position.x, gl_Position.x - min_clip.x),
        min(max_clip.y - gl_Position.y, gl_Position.y - min_clip.y)
    );
}