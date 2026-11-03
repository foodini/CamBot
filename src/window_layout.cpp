#include "window_layout.h"

#include <math.h>

namespace window_layout {

namespace {

float at_least_zero(float value) {
	return value > 0.0f ? value : 0.0f;
}

}  // namespace

WindowLayout compute(float window_width, float window_height, float menu_height, float canvas_aspect) {
	WindowLayout layout;
	const float top = menu_height;
	const float below_menu = at_least_zero(window_height - top);

	// The panel never takes more than 40% of the window, so a narrow window still has a preview.
	float panel_width = LEFT_PANEL_WIDTH;
	if (panel_width > window_width * 0.4f)
		panel_width = window_width * 0.4f;
	panel_width = at_least_zero(floorf(panel_width));
	layout.left_panel = LayoutRect(0.0f, top, panel_width, below_menu);

	const float centre_x = panel_width;
	const float centre_width = at_least_zero(window_width - panel_width);
	float transport_height = TRANSPORT_HEIGHT;
	if (transport_height > below_menu)
		transport_height = below_menu;
	layout.transport = LayoutRect(centre_x, top + below_menu - transport_height, centre_width, transport_height);
	layout.preview_area = LayoutRect(centre_x, top, centre_width, at_least_zero(below_menu - transport_height));

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
