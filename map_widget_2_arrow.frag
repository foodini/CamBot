#version 330 core
out vec4 FragColor;

in vec2 texCoord;

uniform float time;
uniform float time_parametric;
uniform float course;
uniform float speed;

void main() {
    vec2 flight_direction = vec2(cos(course), sin(course));
    flight_direction = normalize(flight_direction);
    vec2 pixel_direction = texCoord - 0.5;
    float projection = dot(flight_direction, pixel_direction);
    float pixel_dist = length(pixel_direction);

    FragColor = vec4(0.0, 0.0, 0.0, 0.0);
    if (projection >= 0.0) {
        float dist_from_line = length(pixel_direction - (flight_direction * projection));
        if (pixel_dist + 3.0 * dist_from_line < 0.05) {
            FragColor = vec4(1.0, 1.0, 0.0, 1.0);
        }
    }
}
