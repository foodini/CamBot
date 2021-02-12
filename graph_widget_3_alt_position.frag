#version 330 core
out vec4 FragColor;

in float u;

//TODO(p2) Need a better shape.
void main() {
    vec2 center_to_pixel = gl_PointCoord - vec2(0.5f);
    float dist_from_center = min(1.0, length(center_to_pixel));
    float alpha = 1.0 - dist_from_center * 4.0;
    FragColor = vec4(0.0, 0.0, 1.0, alpha);
}
