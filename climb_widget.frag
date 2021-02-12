#version 330 core
out vec4 FragColor;

in vec2 uv;

uniform vec3 climb_rates;

void main() {
    FragColor = vec4(1-uv.y, 0.0, uv.y, 1.0);
    float climb_rate = (uv.x <  0.3333333 ? 1.0 : 0.0) * climb_rates.x +
                       (uv.x >= 0.3333333 && uv.x <= 0.6666667 ? 1.0 : 0.0) * climb_rates.y +
                       (uv.x > 0.6666667 ? 1.0 : 0.0) * climb_rates.z;
    climb_rate = max(-800.0, min(800.0, climb_rate));
    float line_height = (climb_rate + 800.0 ) / 1600.0;
    FragColor += vec4(vec3(pow(1.0-abs(line_height-uv.y),80.0)), 0.0);
    FragColor.g += pow(1.0-abs(0.5-uv.y),120.0);
}
