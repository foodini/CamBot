#version 330 core
out vec4 FragColor;

in vec2 uv;

void main() {
    FragColor = vec4(0.0, 0.0, 0.0, uv.y<0.33?uv.x:0.0);
}
