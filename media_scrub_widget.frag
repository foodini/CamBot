#version 330 core
out vec4 FragColor;

in vec3 ourColor;
in vec2 texCoord;

uniform float time;
uniform float time_parametric;

void main() {
    float shade = 0.5 * (1.0 - abs(texCoord.y-0.5));
    shade *= time_parametric < texCoord.x ? 0.0 : 1.0;
    FragColor = vec4(shade*shade, 0.0, shade, 1.0);
}
