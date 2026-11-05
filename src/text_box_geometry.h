#pragma once

// Moving and resizing a text box about the picture (the arithmetic of dragging it with the mouse). A box is a rectangle in
// fractions of the frame, as in text_box.h; these keep it inside the frame and keep it from being squeezed away to nothing.
// Each is given the rectangle as it was when the drag began and how far the mouse has moved since (also in fractions of the
// frame), not the last step, so a box pushed against an edge comes back out when the mouse does.

namespace text_box_geometry {

struct Rect {
	float x;     // Left and top, and width and height, as fractions of the frame's width and height.
	float y;
	float w;
	float h;
	Rect() : x(0.0f), y(0.0f), w(0.0f), h(0.0f) {}
	Rect(float left, float top, float width, float height) : x(left), y(top), w(width), h(height) {}
};

const float MIN_WIDTH = 0.05f;
const float MIN_HEIGHT = 0.03f;

enum Corner { TOP_LEFT = 0, TOP_RIGHT = 1, BOTTOM_LEFT = 2, BOTTOM_RIGHT = 3 };
enum Edge { EDGE_LEFT = 4, EDGE_RIGHT = 5, EDGE_TOP = 6, EDGE_BOTTOM = 7 };

// The box moved by (dx, dy), as far as the frame allows.
Rect move(const Rect& start, float dx, float dy);

// The box with one corner dragged by (dx, dy), the opposite corner staying where it is.
Rect drag_corner(const Rect& start, int corner, float dx, float dy);

// The box with one edge dragged by (dx, dy) (only the one that matters to that edge is used), the other three staying where they are.
Rect drag_edge(const Rect& start, int edge, float dx, float dy);

}  // namespace text_box_geometry
