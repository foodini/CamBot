#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 uv_in;

uniform mat4 projection;

out vec2 uv;

void main()
{
    gl_Position = projection * vec4(aPos.xy, 0.0, 1.0);
    uv = uv_in;
}