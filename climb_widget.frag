#version 330 core
out vec4 FragColor;

in vec2 uv;

uniform vec3 climb_rates;

//TODO(P0): there's got to be a #include for frag shaders. The color array and the Bezier 
//          computation shouldn't be repeated here and in the course shader.

const vec3 colors[4] = vec3[4](
    vec3(0.0, 0.0, 0.2),
    vec3(0.0, 0.0, 1.1),
    vec3(1.7, 2.3, 0.1),
    vec3(1.7, -0.5, 0.0)
);

void main() {
    float t = uv.y;
    FragColor = 
        vec4(colors[0] * pow(1.0-t, 3.0) + 
                colors[1] * 3.0 * t * pow(1.0-t, 2.0) +
                colors[2] * 3.0 * t * t * (1.0-t)     +
                colors[3] * pow(t, 3.0),
                1.0);
                 
    float climb_rate = (uv.x <  0.3333333 ? 1.0 : 0.0) * climb_rates.x +
                       (uv.x >= 0.3333333 && uv.x <= 0.6666667 ? 1.0 : 0.0) * climb_rates.y +
                       (uv.x > 0.6666667 ? 1.0 : 0.0) * climb_rates.z;
    climb_rate = max(-800.0, min(800.0, climb_rate));
    float line_height = (climb_rate + 800.0 ) / 1600.0;
    FragColor.rgb -= pow(1.0-abs(0.5-uv.y),240.0);
    FragColor += vec4(vec3(pow(1.0-abs(line_height-uv.y),120.0)), 0.0);
}
