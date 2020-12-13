#version 330 core
layout (location = 0) in vec3 aPos;

uniform mat4 projection;
uniform float xpos;  // bottom left corner of widget
uniform float ypos;  // bottom left corner of widget
uniform float width;
uniform float height;

out vec2 texCoord;

void main()
{
    gl_Position = projection * vec4(aPos.xy, 0.0, 1.0);
    texCoord.x = (aPos.x - xpos) / (width - 1.0);
    texCoord.y = (aPos.y - ypos) / (height - 1.0);
}