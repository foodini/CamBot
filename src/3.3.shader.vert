#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
layout(location = 2) in vec2 aTexCoord;

out vec3 ourColor;
out vec2 TexCoord;
uniform float time;
uniform float angle;
// Viewport pixel width / pixel height (see MediaContainerMgr::render()). NDC x and y don't cover
// equal pixel counts here, so rotating aPos.xy directly would shear the image -- dividing dy by
// this before rotating, then multiplying the rotated y back by it afterward, rotates in a space
// where x and y really are equal-sized units, giving a true rotation instead of a shear.
uniform float aspect_correction;
// How much to zoom in so the rotated video rect still fully covers its own display area with no
// empty corners, without cropping more than that minimum requires. 1.0 at any multiple of 90
// degrees; largest partway between them. Depends on the video rect's own aspect ratio, not just
// the angle, so it's computed on the CPU side (see MediaContainerMgr::render()) rather than here.
uniform float cover_scale;
// The video quad's own center, in NDC -- rotation pivots around this, not the NDC origin.
uniform float center_x;
uniform float center_y;

void main()
{
    float sin_t = sin(angle);
    float cos_t = cos(angle);

    float dx = aPos.x - center_x;
    float dy = (aPos.y - center_y) / aspect_correction;

    float rotated_x = dx * cos_t - dy * sin_t;
    float rotated_y = (dx * sin_t + dy * cos_t) * aspect_correction;

    gl_Position = vec4(center_x + cover_scale * rotated_x, center_y + cover_scale * rotated_y, aPos.z, 1.0);
    ourColor = aColor;
    TexCoord = aTexCoord;
}