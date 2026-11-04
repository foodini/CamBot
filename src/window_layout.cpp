#include "window_layout.h"

#include <math.h>

namespace window_layout {

namespace {

float at_least_zero(float value) {
	return value > 0.0f ? value : 0.0f;
}

float smaller(float a, float b) {
	return a < b ? a : b;
}

}  // namespace

WindowLayout compute(float window_width, float window_height, float menu_height, float canvas_aspect, float panel_width, float timeline_height) {
	WindowLayout layout;
	layout.widest_panel = 0.0f;
	const float top = menu_height;
	const float below_menu = at_least_zero(window_height - top);

	// The timeline along the bottom, the divider above it, and what's left over is the upper band.
	float tallest_timeline = below_menu - SPLITTER_HEIGHT - MIN_UPPER_HEIGHT;
	float timeline_used = timeline_height;
	if (timeline_used < MIN_TIMELINE_HEIGHT)
		timeline_used = MIN_TIMELINE_HEIGHT;
	if (timeline_used > tallest_timeline)
		timeline_used = tallest_timeline;
	timeline_used = at_least_zero(floorf(timeline_used));
	float splitter_height = timeline_used > 0.0f ? SPLITTER_HEIGHT : 0.0f;
	layout.timeline = LayoutRect(0.0f, top + below_menu - timeline_used, at_least_zero(window_width), timeline_used);
	layout.splitter = LayoutRect(0.0f, layout.timeline.y - splitter_height, at_least_zero(window_width), splitter_height);
	const float upper_height = at_least_zero(layout.splitter.y - top);

	// The preview column needs to be as wide as the picture is at the biggest size the height
	// allows (plus its border), but no narrower than the transport bar's controls. The panel takes
	// all the rest, or what the user asked for if that's more, and is held between its limits.
	float transport_height = TRANSPORT_HEIGHT;
	if (transport_height > upper_height)
		transport_height = upper_height;
	const float preview_area_height = at_least_zero(upper_height - transport_height);
	float wanted_column = MIN_PREVIEW_COLUMN_WIDTH;
	if (canvas_aspect > 0.0f) {
		float picture_width = floorf(at_least_zero(preview_area_height - 2.0f * PREVIEW_MARGIN) * canvas_aspect + 0.5f) + 2.0f * PREVIEW_MARGIN;
		if (picture_width > wanted_column)
			wanted_column = picture_width;
	}
	layout.widest_panel = at_least_zero(floorf(window_width - PANEL_SPLITTER_WIDTH - MIN_PREVIEW_COLUMN_WIDTH));
	float panel_used = window_width - PANEL_SPLITTER_WIDTH - wanted_column;
	if (panel_used < panel_width)
		panel_used = panel_width;
	if (panel_used < MIN_PANEL_WIDTH)
		panel_used = MIN_PANEL_WIDTH;
	if (panel_used > layout.widest_panel)
		panel_used = layout.widest_panel;
	panel_used = at_least_zero(floorf(panel_used));
	layout.left_panel = LayoutRect(0.0f, top, panel_used, upper_height);
	layout.panel_splitter = LayoutRect(panel_used, top, at_least_zero(smaller(PANEL_SPLITTER_WIDTH, window_width - panel_used)), upper_height);

	const float centre_x = layout.panel_splitter.x + layout.panel_splitter.w;
	const float centre_width = at_least_zero(window_width - centre_x);
	layout.transport = LayoutRect(centre_x, top + upper_height - transport_height, centre_width, transport_height);
	layout.preview_area = LayoutRect(centre_x, top, centre_width, preview_area_height);

	const float available_width = at_least_zero(layout.preview_area.w - 2.0f * PREVIEW_MARGIN);
	const float available_height = at_least_zero(layout.preview_area.h - 2.0f * PREVIEW_MARGIN);
	if (available_width <= 0.0f || available_height <= 0.0f || canvas_aspect <= 0.0f)
		return layout;

	// Whole pixels, so the picture isn't blurred by landing between them.
	float width = floorf(available_width);
	float height = floorf(width / canvas_aspect + 0.5f);
	if (height > available_height) {
		height = floorf(available_height);
		width = floorf(height * canvas_aspect + 0.5f);
	}
	if (width > available_width)
		width = floorf(available_width);
	layout.preview = LayoutRect(
		layout.preview_area.x + floorf((layout.preview_area.w - width) / 2.0f),
		layout.preview_area.y + floorf((layout.preview_area.h - height) / 2.0f),
		width, height);
	return layout;
}

}  // namespace window_layout
