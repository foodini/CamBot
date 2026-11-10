#pragma once

// Where a place in the footage's picture shows on the preview, and the other way round. The picture is turned by the project's
// rotation, about the middle of the frame, and zoomed in just enough that no empty corners show (see MediaContainerMgr::render() and
// 3.3.shader.vert, which this follows). Places in the picture are (u, v): 0 to 1 from the left and from the top, as in tracking_path.h.
// Places on the preview are (x, y) as fractions of the preview's width and height, from its top left, the same way up.

namespace frame_mapping {

// How much the picture is zoomed in at `rotation_radians`, for a preview `aspect` (width over height) wide.
double cover_scale(double rotation_radians, double aspect);

void picture_to_preview(double u, double v, double rotation_radians, double aspect, double* x, double* y);
void preview_to_picture(double x, double y, double rotation_radians, double aspect, double* u, double* v);

}  // namespace frame_mapping
