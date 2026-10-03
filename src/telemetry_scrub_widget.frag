#version 330 core
out vec4 FragColor;

//in vec3 ourColor;
in vec2 uv;

uniform float time_parametric;

// The two cutoff markers (see TelemetryScrubWidget) -- clamped [0,1] positions, each with its
// own color (white = inside the recorded telemetry, mid-grey = clamped because we ran out of
// telemetry on that side -- chosen in C++ rather than here, to keep this shader color-blind-
// scheme-agnostic). marker_halfwidth/halo_halfwidth are uv-space half-widths for a crisp core
// line and a soft, wider halo around it, so you don't have to click the exact pixel to grab one
// (see TelemetryScrubWidget::on_left_press(), which uses the same pixel count for its hit test).
uniform float cutoff_left;
uniform float cutoff_right;
uniform vec3  cutoff_left_color;
uniform vec3  cutoff_right_color;
uniform float marker_halfwidth;
uniform float halo_halfwidth;

void draw_marker(inout vec3 color, inout float alpha, float dist, vec3 marker_color) {
    float core = 1.0 - smoothstep(0.0, marker_halfwidth, dist);
    float halo = (1.0 - smoothstep(0.0, halo_halfwidth, dist)) * 0.35;
    color = mix(color, marker_color, halo);
    color = mix(color, marker_color, core);
    alpha = max(alpha, max(core, halo));
}

void main() {
    float shade = 0.5 * (1.5 - abs(uv.y-0.5));
    float alpha = time_parametric < uv.x ? 0.0 : 1.0;
    shade *= alpha;
    vec3 color = vec3(shade, shade, 0.0);

    draw_marker(color, alpha, abs(uv.x - cutoff_left), cutoff_left_color);
    draw_marker(color, alpha, abs(uv.x - cutoff_right), cutoff_right_color);

    FragColor = vec4(color, alpha);
}
