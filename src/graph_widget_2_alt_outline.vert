#version 330 core
layout (location = 0) in vec2 aPos;

uniform mat4 projection;

out vec2 texCoord;

void main()
{
    gl_Position = projection * vec4(aPos.xy, 0.0, 1.0);
}