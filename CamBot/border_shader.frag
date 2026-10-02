#version 330 core
out vec4 FragColor;

in vec2 uv;

uniform float border_height;
uniform float border_width;
uniform float time;

const vec3 yellow = vec3(1.0, 1.0, 0.0);
const vec3 black = vec3(0.0, 0.0, 0.0);

void main() {
    vec3 color = vec3(0.0, 0.0, 0.0);
    if (abs(0.5 - uv.y) > (0.5 - border_height)) {
        color += int(time * 3.0 + uv.x / border_width) % 2 == 0 ? yellow : black;
        FragColor.a = 1.0;
    }
    
    if (abs(0.5 - uv.x) > (0.5 - border_width)) {
        color += int(time * 3.0 + uv.y / border_height) % 2 == 0 ? yellow : black;
        FragColor.a = 1.0;
    }
    
    FragColor.rgb = color;
}
