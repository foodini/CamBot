#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in float u_in;

uniform mat4 projection;

out float u;

void main()
{
    gl_Position = projection * vec4(aPos.xy, 0.0, 1.0);
    u = u_in;
}