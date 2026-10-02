#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 uv_in;

out vec2 uv;

void main()
{
    uv = uv_in;
    gl_Position = vec4(aPos.xy, 0.0, 1.0);
}