#pragma once

// Zooming in on the preview while a path is edited (see path_panel.h), so the place for a point can be seen and picked more exactly.
// The picture on the preview is the canvas (the video with the overlays), shown through a window onto it: at zoom 1 the whole canvas, and
// at more a part of it, bigger. Only what is shown on screen changes; nothing in the picture, so an export is as it always was.
//
// Places on the canvas and on the preview are fractions of their widths and heights, from the top left (as in frame_mapping.h).
// No drawing in here, so it can be tested.

namespace preview_zoom {

const double MAX_ZOOM = 16.0;

struct View {
	double zoom;       // 1 to MAX_ZOOM: how many times bigger the canvas is shown.
	double center_x;   // The place on the canvas in the middle of the preview.
	double center_y;
	View() : zoom(1.0), center_x(0.5), center_y(0.5) {}
	bool zoomed() const { return zoom > 1.0 + 1e-9; }
};

// The part of the canvas shown, from its top left to its bottom right.
void region(const View& view, double* left, double* top, double* right, double* bottom);

// The place on the canvas shown at a place on the preview, and the other way round.
void to_canvas(const View& view, double preview_x, double preview_y, double* canvas_x, double* canvas_y);
void to_preview(const View& view, double canvas_x, double canvas_y, double* preview_x, double* preview_y);

// The view after zooming by `factor` (more than 1 to zoom in) about the place `anchor_x`, `anchor_y` on the preview: the place on the
// canvas that is under it stays under it, as far as the edges of the canvas allow. The zoom stays between 1 and MAX_ZOOM.
View zoomed(const View& view, double anchor_x, double anchor_y, double factor);

// The view after the picture is dragged by `dx`, `dy` (fractions of the preview): it moves with the mouse. Kept on the canvas.
View panned(const View& view, double dx, double dy);

// The view after it has moved just far enough to bring the place `canvas_x`, `canvas_y` on the canvas inside the window, no nearer its edge
// than `margin` (a fraction of the window's width, under a half). A place already inside is left alone. Kept on the canvas, so a place
// near its edge can end up nearer the edge of the window than the margin.
View followed(const View& view, double canvas_x, double canvas_y, double margin);

}  // namespace preview_zoom
