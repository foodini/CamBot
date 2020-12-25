#version 330 core
out vec4 FragColor;

in vec2 texCoord;

uniform float time;
uniform float time_parametric;
uniform float course;
uniform float speed;

//https://www.youtube.com/watch?v=sl9x19EnKng&t=1h8m25s
float smax(float a, float b, float k) {
    float h = max(k - abs(a-b), 0.0);
    return max(a,b) + (0.25 / k)*h*h;
}

float smin(float a, float b, float k) {
    return min(a + k, b + k);
}

#define CHORD_LEN (0.075 / 2.0)
#define LEAD_EDGE_SLOPE (1/2.0)
#define TRAIL_EDGE_SLOPE (1/4.0)

void main() {
    FragColor = vec4(0.0, 0.0, 0.0, 0.0);
    vec2 flight_direction = vec2(cos(course), sin(course));  // Magically normalized
    vec2 right = vec2(sin(course), -cos(course));

    vec2 pixel_direction = texCoord - 0.5;
    float fwd_proj = dot(pixel_direction, flight_direction);
    float right_proj = dot(pixel_direction, right);
    float leading_edge = smax(fwd_proj + right_proj*LEAD_EDGE_SLOPE - CHORD_LEN, fwd_proj - right_proj*LEAD_EDGE_SLOPE - CHORD_LEN, 0.02);
    float trailing_edge = min((-fwd_proj) + right_proj*TRAIL_EDGE_SLOPE - CHORD_LEN, (-fwd_proj) - right_proj*TRAIL_EDGE_SLOPE - CHORD_LEN);
    float wing_shape = smax(trailing_edge, leading_edge, 0.07);
    if (wing_shape < 0.0) {
        if (wing_shape < -0.005) 
            FragColor = vec4(0.0, 0.0, 1.0, 1.0);
        else
            FragColor = vec4(1.0, 1.0, 0.0, 0.7);
    }
}
