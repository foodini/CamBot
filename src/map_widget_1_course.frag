#version 330 core
out vec4 FragColor;

//in vec2 gl_PointCoord;

in float climb_rate;
in float trace_future;   // 1.0 for dots the glider hasn't reached yet
in float trace_recent;   // 1.0 for dots in the most recent stretch

uniform float time;
// 0.0: draw everything but the most recent stretch. 1.0: draw only the most recent stretch, opaque.
uniform float trace_pass;
// +/- ft/min spanned by the color scale (EnvConfig::climb_color_range()).
uniform float climb_range;

// How much a not-yet-flown dot is washed out: this fraction of its color replaced by grey, and
// its opacity scaled to this much.
const float FUTURE_DESATURATION = 0.7;
const float FUTURE_OPACITY = 0.4;

const vec3 colors[4] = vec3[4](
    vec3(0.0, 0.0, 0.2),
    vec3(0.0, 0.0, 1.1),
    vec3(1.7, 2.3, 0.1),
    vec3(1.7, -0.5, 0.0)
);

// main() gets called once per dot. The dot's size is given.... elsewhere... and each pixel 
// in the dot has a gl_PointCoord. The distance from the center of the dot is used to 
// work out how much alpha to apply, to create a soft edge.
void main() {
    bool recent = trace_recent > 0.5;
    if ((trace_pass > 0.5) != recent) {
        discard;
    }

    vec2 pc = gl_PointCoord;
    pc = min(pc, 1.0);
    pc = max(pc, 0.0);
    float dist_from_center = min(1.0, 2.0 * distance(vec2(0.5), pc));
    // The most recent stretch is solid: fully opaque to the edge of the dot, with only the last
    // sliver of the rim softened to keep the edge from looking jagged. Everything else keeps the
    // soft, translucent dots.
    float alpha = recent ? (1.0 - smoothstep(0.85, 1.0, dist_from_center)) : pow((1-dist_from_center), 2.0);
    
   	float t = min(max((climb_rate + climb_range) / (2.0 * climb_range), 0.0), 1.0);

	vec3 color =
        colors[0] * pow(1.0-t, 3.0) + 
        colors[1] * 3.0 * t * pow(1.0-t, 2.0) +
        colors[2] * 3.0 * t * t * (1.0-t)     +
        colors[3] * pow(t, 3.0);

    if (trace_future > 0.5) {
        float grey = dot(clamp(color, 0.0, 1.0), vec3(0.299, 0.587, 0.114));
        color = mix(color, vec3(grey), FUTURE_DESATURATION);
        alpha *= FUTURE_OPACITY;
    }

	FragColor = vec4(color, alpha);
}
