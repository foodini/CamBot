#version 330 core
out vec4 FragColor;

in vec3 ourColor;
in vec2 TexCoord;

uniform sampler2D texture0;
uniform sampler2D texture1;
uniform sampler2D texture2;

void main() {
    // https://stackoverflow.com/questions/28603510/convert-yuv420p-to-rgb888-in-c
    mat3 conv = mat3(1.164,  0.000,  1.596,
                     1.164, -0.392, -0.813,
                     1.164,  2.017,  0.000);

    //Each of the textures is a monochrome texture, so their color is in the .r channel.

    vec3 YUV = vec3(texture(texture0, TexCoord).r - 16.0/255.0,
                    texture(texture1, TexCoord).r - 0.5,
                    texture(texture2, TexCoord).r - 0.5);
       
    FragColor = vec4(YUV * conv, 1.0);
}
