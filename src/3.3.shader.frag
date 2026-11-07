#version 330 core
out vec4 FragColor;

in vec3 ourColor;
in vec2 TexCoord;

uniform sampler2D texture0;
uniform sampler2D texture1;
uniform sampler2D texture2;
// The second picture of a crossfade, and how far the picture has gone from the first to the second (0 to 1; 0: just the first).
uniform sampler2D texture3;
uniform sampler2D texture4;
uniform sampler2D texture5;
uniform float mix_weight;

vec3 rgb_of(sampler2D y_plane, sampler2D u_plane, sampler2D v_plane) {
    // https://stackoverflow.com/questions/28603510/convert-yuv420p-to-rgb888-in-c
    mat3 conv = mat3(1.164,  0.000,  1.596,
                     1.164, -0.392, -0.813,
                     1.164,  2.017,  0.000);

    //Each of the textures is a monochrome texture, so their color is in the .r channel.

    vec3 YUV = vec3(texture(y_plane, TexCoord).r - 16.0/255.0,
                    texture(u_plane, TexCoord).r - 0.5,
                    texture(v_plane, TexCoord).r - 0.5);

    return YUV * conv;
}

void main() {
    vec3 color = rgb_of(texture0, texture1, texture2);
    if (mix_weight > 0.0)
        color = mix(color, rgb_of(texture3, texture4, texture5), mix_weight);
    FragColor = vec4(color, 1.0);
}
