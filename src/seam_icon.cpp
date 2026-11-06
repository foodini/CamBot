#include "seam_icon.h"

namespace seam_icon {

namespace {

float larger(float a, float b) { return a > b ? a : b; }
float smaller(float a, float b) { return a < b ? a : b; }

Span finish(float left, float right) {
	Span span;
	span.left = left;
	span.right = right;
	span.visible = right - left >= ICON_MIN_WIDTH;
	return span;
}

}  // namespace

Span for_join(float zone_left, float zone_right, float before_width, float after_width, float side) {
	if (zone_right - zone_left >= side)
		return finish(zone_left, zone_right);
	const float center = (zone_left + zone_right) / 2.0f;
	const float left = larger(center - side / 2.0f, zone_left - ICON_SHARE * before_width);
	const float right = smaller(center + side / 2.0f, zone_right + ICON_SHARE * after_width);
	return finish(left, right);
}

Span for_start(float zone_left, float zone_right, float clip_width, float side) {
	if (zone_right - zone_left >= side)
		return finish(zone_left, zone_right);
	return finish(zone_left, zone_left + smaller(side, ICON_SHARE * clip_width));
}

Span for_end(float zone_left, float zone_right, float clip_width, float side) {
	if (zone_right - zone_left >= side)
		return finish(zone_left, zone_right);
	return finish(zone_right - smaller(side, ICON_SHARE * clip_width), zone_right);
}

}  // namespace seam_icon
