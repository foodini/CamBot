#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 uv_in;


uniform mat4 projection;
uniform float xpos;  // bottom left corner of widget
uniform float ypos;  // bottom left corner of widget
uniform float width;
uniform float height;

out vec2 texCoord;

void main()
{
    gl_Position = projection * vec4(aPos.xy*1.0, 0.0, 1.0);
    texCoord.x = (aPos.x - xpos);
    texCoord.y = (aPos.y - ypos);
    texCoord = uv_in.xy;
}