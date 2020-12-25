#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in float climb_rate_in;

uniform mat4 projection;
uniform vec2 min_clip;
uniform vec2 max_clip;

out float climb_rate;
out float gl_ClipDistance[1];  // distance to each edge of the map, for clipping. //TODO(P1)
void main()
{
    gl_Position = projection * vec4(aPos.xy, 0.0, 1.0);
    climb_rate = climb_rate_in;
    gl_ClipDistance[0] = min(
        min(max_clip.x - gl_Position.x, gl_Position.x - min_clip.x),
        min(max_clip.y - gl_Position.y, gl_Position.y - min_clip.y)
    );
}