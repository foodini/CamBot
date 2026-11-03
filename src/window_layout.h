#pragma once

// Where the parts of the main window go: a panel down the left, a transport bar along the bottom
// of the rest, and the preview picture in whatever space is left over. Plain arithmetic with no
// OpenGL or ImGui in it, so it can be tested. All numbers are window coordinates in pixels, with
// (0, 0) at the top left, the way ImGui and the mouse count them.

struct LayoutRect {
	float x;
	float y;
	float w;
	float h;
	LayoutRect() : x(0.0f), y(0.0f), w(0.0f), h(0.0f) {}
	LayoutRect(float left, float top, float width, float height) : x(left), y(top), w(width), h(height) {}
};

struct WindowLayout {
	LayoutRect left_panel;
	LayoutRect transport;      // Under the preview.
	LayoutRect preview_area;   // All the room the preview could use.
	LayoutRect preview;        // The picture itself, as large as fits, centered in preview_area.
};

namespace window_layout {

const float LEFT_PANEL_WIDTH = 280.0f;
const float TRANSPORT_HEIGHT = 72.0f;
const float PREVIEW_MARGIN = 8.0f;

// `menu_height` is the menu bar across the top, which the rest sits under. `canvas_aspect` is the
// picture's width divided by its height; the preview keeps it exactly (to the pixel) however the
// window is shaped. A window too small for anything gets empty rectangles, never negative ones.
WindowLayout compute(float window_width, float window_height, float menu_height, float canvas_aspect);

}  // namespace window_layout
