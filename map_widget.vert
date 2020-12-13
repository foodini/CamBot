#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 course_position;
layout (location = 2) in vec3 course_color;

uniform mat4 projection;
uniform float xpos;  // bottom left corner of widget
uniform float ypos;  // bottom left corner of widget
uniform float width;
uniform float height;

out vec2 texCoord;
out float glClipDistance[4];  // distance to each edge of the map, for clipping. //TODO(P1)
void main()
{
    gl_Position = projection * vec4(aPos.xy, 0.0, 1.0);
    texCoord.x = (aPos.x - xpos) / (width - 1.0);
    texCoord.y = (aPos.y - ypos) / (height - 1.0);
}