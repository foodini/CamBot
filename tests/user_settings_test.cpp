#include "user_settings.h"
#include <cmath>
#include <cstdio>
int main() {
    UserSettings a;   // should be defaults
    printf("defaults: type %d win %.1f tau %.1f range %.0f trace %.0f/%.0f/%.0f zoom %.1f/%.1f/%.0f\n", a.climb_filter.type, a.climb_filter.window_seconds, a.climb_filter.time_constant_seconds, a.climb_color_range, a.map_trace.recent_width_px, a.map_trace.old_width_px, a.map_trace.recent_seconds, a.map_zoom.min_width_km, a.map_zoom.max_width_km, a.map_zoom.transition_seconds);
    a.climb_filter.type = 1; a.climb_filter.time_constant_seconds = 1.5f; a.climb_color_range = 600; a.map_trace.recent_width_px = 6; a.map_trace.recent_seconds = 300; a.map_zoom.min_width_km = 0.4f; a.map_zoom.max_width_km = 5; a.map_zoom.transition_seconds = 15;
    if (a.map_dim < 0.249f || a.map_dim > 0.251f) { printf("FAIL: map_dim default is %f\n", a.map_dim); return 1; }
    a.map_dim = 0.4f;
    printf("save ok: %d\n", (int)a.save());
    UserSettings b;
    if (b.map_dim < 0.399f || b.map_dim > 0.401f) { printf("FAIL: map_dim came back as %f\n", b.map_dim); return 1; }
    // The text box defaults.
    {
        const UserSettings fresh;
        if (fresh.text_defaults.style.size < 0.04f || fresh.text_defaults.style.size > 0.05f || fresh.text_defaults.style.color[0] != 1.0f ||
                fresh.text_defaults.style.shadow_color[0] != 0.0f || fresh.text_defaults.dwell_seconds != 4.0f || !fresh.text_defaults.recent_colors.empty()) {
            // (Only meaningful on a first run, with no file to read; the checks below are the real ones.)
        }
        UserSettings t;
        t.text_defaults.x = 0.05f; t.text_defaults.y = 0.5f; t.text_defaults.w = 0.6f; t.text_defaults.h = 0.2f;
        t.text_defaults.style.font = 3; t.text_defaults.style.size = 0.06f; t.text_defaults.style.bold = true; t.text_defaults.style.italic = true;
        t.text_defaults.style.strike = true; t.text_defaults.style.align = 2;
        t.text_defaults.style.color[0] = 1.0f; t.text_defaults.style.color[1] = 0.0f; t.text_defaults.style.color[2] = 0.0f;
        t.text_defaults.style.shadow_color[0] = 0.0f; t.text_defaults.style.shadow_color[1] = 0.0f; t.text_defaults.style.shadow_color[2] = 1.0f;
        t.text_defaults.style.shadow_opacity = 0.5f; t.text_defaults.style.shadow_dx = 0.004f; t.text_defaults.style.shadow_dy = -0.002f;
        t.text_defaults.fade_in_seconds = 1.0f; t.text_defaults.dwell_seconds = 7.5f; t.text_defaults.fade_out_seconds = 2.0f;
        t.text_defaults.note_color_used(0x112233);
        t.text_defaults.note_color_used(0xff0000);
        t.text_defaults.note_color_used(0x112233);     // Back to the front, not twice.
        for (int i = 0; i < 20; i++)
            t.text_defaults.note_color_used(0x100000 + i);
        if ((int)t.text_defaults.recent_colors.size() != TextSettings::MAX_RECENT_COLORS || t.text_defaults.recent_colors[0] != 0x100013) { printf("FAIL: recent colors %d\n", (int)t.text_defaults.recent_colors.size()); return 1; }
        t.text_defaults.recent_colors.clear();
        t.text_defaults.note_color_used(0x112233);
        t.text_defaults.note_color_used(0xff0000);
        t.text_defaults.note_color_used(0x112233);
        if (t.text_defaults.recent_colors.size() != 2 || t.text_defaults.recent_colors[0] != 0x112233 || t.text_defaults.recent_colors[1] != 0xff0000) { printf("FAIL: recent order\n"); return 1; }
        t.save();
        const UserSettings r;
        const TextSettings& d = r.text_defaults;
        bool ok = fabsf(d.x - 0.05f) < 1e-5f && fabsf(d.y - 0.5f) < 1e-5f && fabsf(d.w - 0.6f) < 1e-5f && fabsf(d.h - 0.2f) < 1e-5f &&
            d.style.font == 3 && fabsf(d.style.size - 0.06f) < 1e-5f && d.style.bold && d.style.italic && d.style.strike && d.style.align == 2 &&
            d.style.color[0] == 1.0f && d.style.color[1] == 0.0f && d.style.color[2] == 0.0f &&
            d.style.shadow_color[2] == 1.0f && d.style.shadow_color[0] == 0.0f && fabsf(d.style.shadow_opacity - 0.5f) < 1e-5f &&
            fabsf(d.style.shadow_dx - 0.004f) < 1e-5f && fabsf(d.style.shadow_dy + 0.002f) < 1e-5f &&
            d.fade_in_seconds == 1.0f && d.dwell_seconds == 7.5f && d.fade_out_seconds == 2.0f &&
            d.recent_colors.size() == 2 && d.recent_colors[0] == 0x112233 && d.recent_colors[1] == 0xff0000;
        if (!ok) { printf("FAIL: text defaults did not come back\n"); return 1; }
        // A saved box that isn't in the frame is brought into it on reading.
        t.text_defaults.x = 0.9f; t.text_defaults.w = 0.5f;
        t.save();
        const UserSettings clamped;
        if (clamped.text_defaults.x + clamped.text_defaults.w > 1.0001f) { printf("FAIL: text box not clamped\n"); return 1; }
        printf("text defaults ok\n");
    }
    {
        UserSettings s;
        s.sound.volume = 0.35f; s.sound.muted = true;
        s.save();
        const UserSettings back;
        if (fabsf(back.sound.volume - 0.35f) > 0.001f || !back.sound.muted) { printf("FAIL: sound settings did not come back\n"); return 1; }
        // A volume out of range is ignored on reading (the default is used).
        s.sound.volume = 7.0f; s.sound.muted = false;
        s.save();
        const UserSettings ignored;
        if (ignored.sound.volume != 1.0f || ignored.sound.muted) { printf("FAIL: bad sound volume was not ignored\n"); return 1; }
        printf("sound settings ok\n");
    }
    printf("reload:   type %d win %.1f tau %.1f range %.0f trace %.0f/%.0f/%.0f zoom %.1f/%.1f/%.0f\n", b.climb_filter.type, b.climb_filter.window_seconds, b.climb_filter.time_constant_seconds, b.climb_color_range, b.map_trace.recent_width_px, b.map_trace.old_width_px, b.map_trace.recent_seconds, b.map_zoom.min_width_km, b.map_zoom.max_width_km, b.map_zoom.transition_seconds);
}
