#include "preview_zoom.h"

namespace preview_zoom {

namespace {

double clamped(double value, double low, double high) {
	return value < low ? low : (value > high ? high : value);
}

// The view with its middle moved so the window stays on the canvas.
View kept_on_canvas(View view) {
	const double half = 0.5 / view.zoom;
	view.center_x = clamped(view.center_x, half, 1.0 - half);
	view.center_y = clamped(view.center_y, half, 1.0 - half);
	return view;
}

}  // namespace

void region(const View& view, double* left, double* top, double* right, double* bottom) {
	const double half = 0.5 / view.zoom;
	*left = view.center_x - half;
	*top = view.center_y - half;
	*right = view.center_x + half;
	*bottom = view.center_y + half;
}

void to_canvas(const View& view, double preview_x, double preview_y, double* canvas_x, double* canvas_y) {
	double left, top, right, bottom;
	region(view, &left, &top, &right, &bottom);
	*canvas_x = left + preview_x * (right - left);
	*canvas_y = top + preview_y * (bottom - top);
}

void to_preview(const View& view, double canvas_x, double canvas_y, double* preview_x, double* preview_y) {
	double left, top, right, bottom;
	region(view, &left, &top, &right, &bottom);
	*preview_x = (canvas_x - left) / (right - left);
	*preview_y = (canvas_y - top) / (bottom - top);
}

View zoomed(const View& view, double anchor_x, double anchor_y, double factor) {
	double canvas_x, canvas_y;
	to_canvas(view, anchor_x, anchor_y, &canvas_x, &canvas_y);
	View result = view;
	result.zoom = clamped(view.zoom * factor, 1.0, MAX_ZOOM);
	// The window's left edge is where it has to be for the place under the anchor to stay under it.
	const double span = 1.0 / result.zoom;
	result.center_x = canvas_x - anchor_x * span + 0.5 * span;
	result.center_y = canvas_y - anchor_y * span + 0.5 * span;
	return kept_on_canvas(result);
}

View panned(const View& view, double dx, double dy) {
	View result = view;
	result.center_x -= dx / view.zoom;
	result.center_y -= dy / view.zoom;
	return kept_on_canvas(result);
}

View followed(const View& view, double canvas_x, double canvas_y, double margin) {
	// How far the place may be from the middle of the window, on each axis.
	const double reach = (0.5 - margin) / view.zoom;
	View result = view;
	if (canvas_x - result.center_x > reach)
		result.center_x = canvas_x - reach;
	else if (result.center_x - canvas_x > reach)
		result.center_x = canvas_x + reach;
	if (canvas_y - result.center_y > reach)
		result.center_y = canvas_y - reach;
	else if (result.center_y - canvas_y > reach)
		result.center_y = canvas_y + reach;
	return kept_on_canvas(result);
}

}  // namespace preview_zoom
