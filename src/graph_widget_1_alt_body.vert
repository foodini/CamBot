#version 330 core
layout (location = 0) in vec2 aPos;

uniform mat4 projection;
uniform float xpos;  // bottom left corner of widget
uniform float ypos;  // bottom left corner of widget
uniform float width;
uniform float height;

out float climb_rate;
void main() {
	gl_Position = projection * vec4(aPos.xy, 0.0, 1.0);
}