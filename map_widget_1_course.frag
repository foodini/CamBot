#version 330 core
out vec4 FragColor;

in vec2 gl_PointCoord;
in float climb_rate;

uniform float time;

void main() {
    vec2 pc = gl_PointCoord;
    pc = min(pc, 1.0);
    pc = max(pc, 0.0);
    float dist_from_center = min(1.0, 2.0 * distance(vec2(0.5), pc));
    float alpha = pow((1-dist_from_center), 2.0);
    
   	float climb_rate_parametric = (climb_rate + 800.0) / 1600.0;
	FragColor = vec4(1.0-climb_rate_parametric, 0.0, climb_rate_parametric, alpha);
}
