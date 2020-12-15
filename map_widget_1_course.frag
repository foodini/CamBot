#version 330 core
out vec4 FragColor;

in float tex_u;
in float climb_rate;

uniform float time;
uniform float time_parametric;
uniform float course;
uniform float speed;

void main() {
    float dist_from_edge = 0.5 - abs(0.5-tex_u);
	//FragColor = vec4(vec3(dist_from_edge + 0.5), min(1.0, dist_from_edge * 4.0));
	FragColor = vec4(1.0, 1.0, 1.0, min(1.0, dist_from_edge * 1.5 ));
}
