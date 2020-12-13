#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
layout(location = 2) in vec2 aTexCoord;

out vec3 ourColor;
out vec2 TexCoord;
uniform float time;
uniform float angle;

void main()
{
    float sin_t = sin(angle);
    float cos_t = cos(angle);
    float remainder = mod(angle, 3.14159/2.0);
    float scale = sin(remainder) + cos(remainder);
    gl_Position = vec4(- sin_t * aPos.y + cos_t * aPos.x, cos_t * aPos.y + sin_t * aPos.x, aPos.z, 1.0);
    gl_Position = vec4(scale * gl_Position.xy, 0.0, 1.0);
    ourColor = aColor;
    TexCoord = aTexCoord;
}