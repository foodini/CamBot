#include "text_box_geometry.h"

namespace text_box_geometry {

namespace {

float clamp(float value, float low, float high) {
	if (value > high)
		value = high;
	if (value < low)
		value = low;
	return value;
}

}  // namespace

Rect clamp_to_frame(const Rect& box) {
	Rect result;
	result.w = clamp(box.w, MIN_WIDTH, 1.0f);
	result.h = clamp(box.h, MIN_HEIGHT, 1.0f);
	result.x = clamp(box.x, 0.0f, 1.0f - result.w);
	result.y = clamp(box.y, 0.0f, 1.0f - result.h);
	return result;
}

Rect move(const Rect& start, float dx, float dy) {
	Rect moved = start;
	moved.x = clamp(start.x + dx, 0.0f, 1.0f - start.w);
	moved.y = clamp(start.y + dy, 0.0f, 1.0f - start.h);
	return moved;
}

Rect drag_corner(const Rect& start, int corner, float dx, float dy) {
	float left = start.x;
	float top = start.y;
	float right = start.x + start.w;
	float bottom = start.y + start.h;
	if (corner == TOP_LEFT || corner == BOTTOM_LEFT)
		left = clamp(left + dx, 0.0f, right - MIN_WIDTH);
	else
		right = clamp(right + dx, left + MIN_WIDTH, 1.0f);
	if (corner == TOP_LEFT || corner == TOP_RIGHT)
		top = clamp(top + dy, 0.0f, bottom - MIN_HEIGHT);
	else
		bottom = clamp(bottom + dy, top + MIN_HEIGHT, 1.0f);
	return Rect(left, top, right - left, bottom - top);
}

Rect drag_edge(const Rect& start, int edge, float dx, float dy) {
	float left = start.x;
	float top = start.y;
	float right = start.x + start.w;
	float bottom = start.y + start.h;
	if (edge == EDGE_LEFT)
		left = clamp(left + dx, 0.0f, right - MIN_WIDTH);
	else if (edge == EDGE_RIGHT)
		right = clamp(right + dx, left + MIN_WIDTH, 1.0f);
	else if (edge == EDGE_TOP)
		top = clamp(top + dy, 0.0f, bottom - MIN_HEIGHT);
	else if (edge == EDGE_BOTTOM)
		bottom = clamp(bottom + dy, top + MIN_HEIGHT, 1.0f);
	return Rect(left, top, right - left, bottom - top);
}

}  // namespace text_box_geometry
