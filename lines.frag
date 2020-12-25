#version 330 core
out vec4 FragColor;

in vec2 gl_PointCoord;

void main() {
    vec2 pc = gl_PointCoord;
    pc = min(pc, 1.0);
    pc = max(pc, 0.0);
    float dist_from_center = min(1.0, 2.0 * distance(vec2(0.5), pc));
    float alpha = (1-dist_from_center) * (1-dist_from_center);
    FragColor = vec4(1.0, 1.0, 0.0, alpha);
}
