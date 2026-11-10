#include "frame_mapping.h"
#include <math.h>

namespace frame_mapping {

double cover_scale(double rotation_radians, double aspect) {
	return fabs(cos(rotation_radians)) + aspect * fabs(sin(rotation_radians));
}

// The shader works on positions in NDC (-1 to 1, y up), turning them in a space where x and y are the same length: y is divided by the
// aspect before turning and multiplied by it after.
void picture_to_preview(double u, double v, double rotation_radians, double aspect, double* x, double* y) {
	const double px = 2.0 * u - 1.0;
	const double py = 1.0 - 2.0 * v;
	const double sin_t = sin(rotation_radians);
	const double cos_t = cos(rotation_radians);
	const double cover = cover_scale(rotation_radians, aspect);
	const double dy = py / aspect;
	const double out_x = cover * (px * cos_t - dy * sin_t);
	const double out_y = cover * (px * sin_t + dy * cos_t) * aspect;
	*x = (out_x + 1.0) * 0.5;
	*y = (1.0 - out_y) * 0.5;
}

void preview_to_picture(double x, double y, double rotation_radians, double aspect, double* u, double* v) {
	const double sin_t = sin(rotation_radians);
	const double cos_t = cos(rotation_radians);
	const double cover = cover_scale(rotation_radians, aspect);
	const double a = (2.0 * x - 1.0) / cover;
	const double b = (1.0 - 2.0 * y) / (cover * aspect);
	const double px = a * cos_t + b * sin_t;
	const double dy = -a * sin_t + b * cos_t;
	const double py = dy * aspect;
	*u = (px + 1.0) * 0.5;
	*v = (1.0 - py) * 0.5;
}

}  // namespace frame_mapping
