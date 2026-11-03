#include <math.h>
#include <stdio.h>

#include "window_layout.h"

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); g_failures++; } } while (0)

static bool inside(const LayoutRect& inner, const LayoutRect& outer) {
	return inner.x >= outer.x - 0.001f && inner.y >= outer.y - 0.001f &&
		inner.x + inner.w <= outer.x + outer.w + 0.001f && inner.y + inner.h <= outer.y + outer.h + 0.001f;
}

static bool overlap(const LayoutRect& a, const LayoutRect& b) {
	return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static void test_typical_windows() {
	const float ASPECT = 1920.0f / 1214.0f;
	const float MENU = 19.0f;
	const float sizes[][2] = { {1200, 900}, {1920, 1080}, {800, 600}, {1000, 2000}, {3000, 700}, {1500, 1500} };
	for (int i = 0; i < 6; i++) {
		float w = sizes[i][0];
		float h = sizes[i][1];
		WindowLayout layout = window_layout::compute(w, h, MENU, ASPECT);
		LayoutRect whole(0, 0, w, h);
		CHECK(inside(layout.left_panel, whole));
		CHECK(inside(layout.transport, whole));
		CHECK(inside(layout.preview_area, whole));
		CHECK(inside(layout.preview, layout.preview_area));
		CHECK(!overlap(layout.left_panel, layout.transport));
		CHECK(!overlap(layout.left_panel, layout.preview_area));
		CHECK(!overlap(layout.transport, layout.preview_area));
		CHECK(layout.left_panel.y == MENU && layout.preview_area.y == MENU);
		// The shape is kept to within a pixel of rounding.
		CHECK(fabsf(layout.preview.w - layout.preview.h * ASPECT) <= 1.0f * ASPECT + 0.01f);
		// It uses all of one direction.
		float room_w = layout.preview_area.w - 2 * window_layout::PREVIEW_MARGIN;
		float room_h = layout.preview_area.h - 2 * window_layout::PREVIEW_MARGIN;
		CHECK(layout.preview.w >= room_w - 1.5f || layout.preview.h >= room_h - 1.5f);
		// Centered.
		CHECK(fabsf((layout.preview.x - layout.preview_area.x) - (layout.preview_area.x + layout.preview_area.w - layout.preview.x - layout.preview.w)) <= 1.0f);
	}
}

static void test_known_case() {
	// 1200 x 900: panel 280 wide, so 920 left; room for the preview 904 wide, 761 tall.
	WindowLayout layout = window_layout::compute(1200, 900, 19, 1920.0f / 1214.0f);
	CHECK(layout.left_panel.w == 280.0f);
	CHECK(layout.preview.w == 904.0f);
	CHECK(layout.preview.h == 572.0f);
	CHECK(layout.transport.h == window_layout::TRANSPORT_HEIGHT);
	CHECK(layout.transport.y + layout.transport.h == 900.0f);
}

static void test_tiny_windows() {
	const float sizes[][2] = { {0, 0}, {10, 10}, {100, 20}, {300, 60}, {50, 3000} };
	for (int i = 0; i < 5; i++) {
		WindowLayout layout = window_layout::compute(sizes[i][0], sizes[i][1], 19, 1.5f);
		CHECK(layout.left_panel.w >= 0 && layout.left_panel.h >= 0);
		CHECK(layout.transport.w >= 0 && layout.transport.h >= 0);
		CHECK(layout.preview_area.w >= 0 && layout.preview_area.h >= 0);
		CHECK(layout.preview.w >= 0 && layout.preview.h >= 0);
	}
	CHECK(window_layout::compute(1200, 900, 19, 0.0f).preview.w == 0.0f);
}

int main() {
	test_typical_windows();
	test_known_case();
	test_tiny_windows();
	printf(g_failures ? "FAILED\n" : "all ok\n");
	return g_failures;
}
