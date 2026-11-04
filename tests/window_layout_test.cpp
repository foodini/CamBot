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
	return a.x < b.x + b.w - 0.001f && b.x < a.x + a.w - 0.001f && a.y < b.y + b.h - 0.001f && b.y < a.y + a.h - 0.001f;
}

static void test_typical_windows() {
	const float ASPECT = 1920.0f / 1214.0f;
	const float MENU = 19.0f;
	const float sizes[][2] = { {1200, 900}, {1920, 1080}, {900, 600}, {1000, 2000}, {3000, 700}, {1500, 1500} };
	const float timelines[] = { 0.0f, 110.0f, 180.0f, 400.0f, 5000.0f };
	const float panels[] = { 0.0f, 280.0f, 5000.0f };
	for (int i = 0; i < 6; i++) {
	  for (int p = 0; p < 3; p++) {
		for (int j = 0; j < 5; j++) {
			float w = sizes[i][0];
			float h = sizes[i][1];
			WindowLayout layout = window_layout::compute(w, h, MENU, ASPECT, panels[p], timelines[j]);
			LayoutRect whole(0, 0, w, h);
			const LayoutRect* parts[] = { &layout.left_panel, &layout.panel_splitter, &layout.transport, &layout.preview_area, &layout.splitter, &layout.timeline };
			for (int a = 0; a < 6; a++) {
				CHECK(inside(*parts[a], whole));
				for (int b = a + 1; b < 6; b++)
					CHECK(!overlap(*parts[a], *parts[b]));
			}
			// The panel, its divider and the preview column fill the width between them.
			CHECK(layout.panel_splitter.x == layout.left_panel.w);
			CHECK(layout.preview_area.x == layout.panel_splitter.x + layout.panel_splitter.w);
			CHECK(fabsf(layout.preview_area.x + layout.preview_area.w - w) < 0.01f);
			CHECK(layout.left_panel.w >= window_layout::MIN_PANEL_WIDTH - 0.01f && layout.left_panel.w <= layout.widest_panel + 0.01f);
			CHECK(layout.preview_area.w >= window_layout::MIN_PREVIEW_COLUMN_WIDTH - 0.01f);
			CHECK(inside(layout.preview, layout.preview_area));
			CHECK(layout.left_panel.y == MENU && layout.preview_area.y == MENU);
			// The timeline is the full width of the window, along its bottom, with the divider on top of it.
			CHECK(layout.timeline.x == 0.0f && layout.timeline.w == w);
			CHECK(layout.timeline.y + layout.timeline.h == h);
			CHECK(layout.splitter.w == w && layout.splitter.y + layout.splitter.h == layout.timeline.y);
			// The timeline's height is the one asked for, held between its limits.
			CHECK(layout.timeline.h >= window_layout::MIN_TIMELINE_HEIGHT);
			CHECK(layout.timeline.h <= timelines[j] || timelines[j] < window_layout::MIN_TIMELINE_HEIGHT);
			CHECK(layout.preview_area.h + layout.transport.h >= window_layout::MIN_UPPER_HEIGHT - 0.01f);
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
	}
}

static void test_known_case() {
	// 1200 x 900 with a 180 pixel timeline: 881 under the menu, so 695 above the divider, of which
	// the transport bar takes 72, leaving 623 for the picture and its border: the picture can be 607
	// tall, and then 960 wide (1214 is 2 x 607), so the preview column wants 976 and the panel is
	// left with 1200 - 6 - 976 = 218.
	WindowLayout layout = window_layout::compute(1200, 900, 19, 1920.0f / 1214.0f, 100.0f, 180.0f);
	CHECK(layout.left_panel.w == 218.0f);
	CHECK(layout.left_panel.h == 695.0f);
	CHECK(layout.panel_splitter.x == 218.0f && layout.panel_splitter.w == window_layout::PANEL_SPLITTER_WIDTH);
	CHECK(layout.preview_area.x == 224.0f && layout.preview_area.w == 976.0f);
	CHECK(layout.preview.x == 232.0f && layout.preview.w == 960.0f && layout.preview.h == 607.0f);
	CHECK(layout.timeline.h == 180.0f && layout.timeline.y == 720.0f);
	CHECK(layout.splitter.y == 714.0f && layout.splitter.h == 6.0f);
	CHECK(layout.transport.h == window_layout::TRANSPORT_HEIGHT);
	CHECK(layout.transport.y + layout.transport.h == 714.0f);
	CHECK(layout.widest_panel == 1200.0f - 6.0f - window_layout::MIN_PREVIEW_COLUMN_WIDTH);
}

static void test_panel_width() {
	const float ASPECT = 1920.0f / 1214.0f;
	// Asking for more than the picture leaves over shrinks the picture to fit: 280 + 6 leaves 914
	// for the column, so the picture is 898 wide.
	WindowLayout layout = window_layout::compute(1200, 900, 19, ASPECT, 280.0f, 180.0f);
	CHECK(layout.left_panel.w == 280.0f);
	CHECK(layout.preview.w == 898.0f && layout.preview.h == 568.0f);
	// Asking for far too much stops where the column is as narrow as it may be.
	layout = window_layout::compute(1200, 900, 19, ASPECT, 5000.0f, 180.0f);
	CHECK(layout.left_panel.w == layout.widest_panel);
	CHECK(layout.preview_area.w == window_layout::MIN_PREVIEW_COLUMN_WIDTH);
	CHECK(layout.preview.w == window_layout::MIN_PREVIEW_COLUMN_WIDTH - 2.0f * window_layout::PREVIEW_MARGIN);
	// A wide, short window: the picture is as tall as fits, and the panel gets the width it doesn't use.
	layout = window_layout::compute(3000, 700, 19, ASPECT, 280.0f, 180.0f);
	CHECK(layout.left_panel.w > 1500.0f);
	CHECK(layout.preview.h == layout.preview_area.h - 2.0f * window_layout::PREVIEW_MARGIN);
}

static void test_timeline_height_limits() {
	// Asked for too little or too much, it is held between the limits.
	CHECK(window_layout::compute(1200, 900, 19, 1.5f, 280.0f, 20.0f).timeline.h == window_layout::MIN_TIMELINE_HEIGHT);
	float tallest = 900.0f - 19.0f - window_layout::SPLITTER_HEIGHT - window_layout::MIN_UPPER_HEIGHT;
	CHECK(window_layout::compute(1200, 900, 19, 1.5f, 280.0f, 5000.0f).timeline.h == tallest);
	// The window growing taller gives the extra to the upper band, not the timeline.
	CHECK(window_layout::compute(1200, 1200, 19, 1.5f, 280.0f, 180.0f).timeline.h == 180.0f);
}

static void test_tiny_windows() {
	const float sizes[][2] = { {0, 0}, {10, 10}, {100, 20}, {300, 60}, {50, 3000}, {900, 300} };
	for (int i = 0; i < 6; i++) {
		WindowLayout layout = window_layout::compute(sizes[i][0], sizes[i][1], 19, 1.5f, 280.0f, 180.0f);
		const LayoutRect* parts[] = { &layout.left_panel, &layout.panel_splitter, &layout.transport, &layout.preview_area, &layout.preview, &layout.splitter, &layout.timeline };
		for (int a = 0; a < 7; a++)
			CHECK(parts[a]->w >= 0 && parts[a]->h >= 0);
	}
	CHECK(window_layout::compute(1200, 900, 19, 0.0f, 280.0f, 180.0f).preview.w == 0.0f);
}

int main() {
	test_typical_windows();
	test_known_case();
	test_panel_width();
	test_timeline_height_limits();
	test_tiny_windows();
	printf(g_failures ? "FAILED\n" : "all ok\n");
	return g_failures;
}
