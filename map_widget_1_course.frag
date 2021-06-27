#version 330 core
out vec4 FragColor;

in vec2 gl_PointCoord;

//TODO(P1): The climb_rate should be turned into a parametric on the user side, so they
//          can adjust the min and max.
in float climb_rate;

uniform float time;

const vec3 colors[4] = vec3[4](
    vec3(0.0, 0.0, 0.2),
    vec3(0.0, 0.0, 1.1),
    vec3(1.7, 2.3, 0.1),
    vec3(1.7, -0.5, 0.0)
);

// main() gets called once per dot. The dot's size is given.... elsewhere... and each pixel 
// in the dot has a gl_PointCoord. The distance from the center of the dot is used to 
// work out how much alpha to apply, to create a soft edge.
void main() {
    vec2 pc = gl_PointCoord;
    pc = min(pc, 1.0);
    pc = max(pc, 0.0);
    float dist_from_center = min(1.0, 2.0 * distance(vec2(0.5), pc));
    float alpha = pow((1-dist_from_center), 2.0);
    
   	float t = min(max((climb_rate + 800.0) / 1600.0, 0.0), 1.0);

	FragColor = 
        vec4(colors[0] * pow(1.0-t, 3.0) + 
             colors[1] * 3.0 * t * pow(1.0-t, 2.0) +
             colors[2] * 3.0 * t * t * (1.0-t)     +
             colors[3] * pow(t, 3.0),
             alpha);
}
