#include "user_settings.h"
#include <cstdio>
int main() {
    UserSettings a;   // should be defaults
    printf("defaults: type %d win %.1f tau %.1f range %.0f trace %.0f/%.0f/%.0f zoom %.1f/%.1f/%.0f\n", a.climb_filter.type, a.climb_filter.window_seconds, a.climb_filter.time_constant_seconds, a.climb_color_range, a.map_trace.recent_width_px, a.map_trace.old_width_px, a.map_trace.recent_seconds, a.map_zoom.min_width_km, a.map_zoom.max_width_km, a.map_zoom.transition_seconds);
    a.climb_filter.type = 1; a.climb_filter.time_constant_seconds = 1.5f; a.climb_color_range = 600; a.map_trace.recent_width_px = 6; a.map_trace.recent_seconds = 300; a.map_zoom.min_width_km = 0.4f; a.map_zoom.max_width_km = 5; a.map_zoom.transition_seconds = 15;
    printf("save ok: %d\n", (int)a.save());
    UserSettings b;
    printf("reload:   type %d win %.1f tau %.1f range %.0f trace %.0f/%.0f/%.0f zoom %.1f/%.1f/%.0f\n", b.climb_filter.type, b.climb_filter.window_seconds, b.climb_filter.time_constant_seconds, b.climb_color_range, b.map_trace.recent_width_px, b.map_trace.old_width_px, b.map_trace.recent_seconds, b.map_zoom.min_width_km, b.map_zoom.max_width_km, b.map_zoom.transition_seconds);
}
