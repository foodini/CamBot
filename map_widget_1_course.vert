#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in float u;   // I didn't need the v in the uv, just u

uniform mat4 projection;
uniform float xpos;  // bottom left corner of widget
uniform float ypos;  // bottom left corner of widget
uniform float width;
uniform float height;

out float tex_u;
out float glClipDistance[4];  // distance to each edge of the map, for clipping. //TODO(P1)
void main()
{
    gl_Position = projection * vec4(aPos.xy, 0.0, 1.0);
    tex_u = (u - 0.5) / 2.0 + 0.5;
}