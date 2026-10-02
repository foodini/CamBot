#version 330 core
out vec4 FragColor;

//in vec3 ourColor;
in vec2 uv;

uniform float time_parametric;

void main() {
    float shade = 0.5 * (1.5 - abs(uv.y-0.5));
    float alpha = time_parametric < uv.x ? 0.0 : 1.0;
    shade *= alpha;
    FragColor = vec4(shade*shade, 0.0, shade, alpha);
}
