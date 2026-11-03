#version 330 core
out vec4 FragColor;

//in vec3 ourColor;
in vec2 uv;

uniform float time_parametric;
// Where one source file ends and the next begins, as [0..1] positions along the bar.
const int MAX_BREAKS = 256;
uniform float breaks[MAX_BREAKS];
uniform int break_count;

void main() {
    float shade = 0.5 * (1.5 - abs(uv.y-0.5));
    float alpha = time_parametric < uv.x ? 0.0 : 1.0;
    shade *= alpha;
    FragColor = vec4(shade*shade, 0.0, shade, alpha);

    // Join markers: a thin vertical bar, about two pixels wide at any bar width.
    float half_width = fwidth(uv.x);
    for (int i = 0; i < break_count; i++) {
        if (abs(uv.x - breaks[i]) < half_width)
            FragColor = vec4(1.0, 0.85, 0.2, 1.0);
    }
}
