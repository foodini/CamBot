#pragma once

// Where the parts of the main window go. Under the menu bar the window has two bands: above, a
// panel down the left and, beside it, the preview picture with the transport bar under it; below,
// the timeline editor across the whole width, with a divider between the bands that the user can
// drag to make the timeline taller or shorter. The preview column is only as wide as the picture
// needs (with a small border), so whatever the picture can't use goes to the left panel; a second
// divider, between the panel and the preview column, can be dragged to make the panel wider and
// the picture smaller. Plain arithmetic with no OpenGL or ImGui in it, so
// it can be tested. All numbers are window coordinates in pixels, with (0, 0) at the top left, the
// way ImGui and the mouse count them.

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
	LayoutRect panel_splitter; // The divider between the left panel and the preview column.
	LayoutRect transport;      // Under the preview.
	LayoutRect preview_area;   // All the room the preview could use.
	LayoutRect preview;        // The picture itself, as large as fits, centered in preview_area.
	LayoutRect splitter;       // The divider between the upper band and the timeline.
	LayoutRect timeline;       // The timeline editor, across the bottom.
	float      widest_panel;   // The most the left panel may be asked for; more is held to this.
};

// The sync view, which takes over the whole window while the telemetry is being lined up with the
// video and the files put in order. From the top: a bar with the Done and Cancel buttons; the panes
// side by side, the map on the left, a narrow strip of buttons in the middle (they apply to both the
// map and the video) and the video (with its telemetry) on the right, with the transport bar under
// the video (it moves the video and the telemetry together, so it goes with the part that does the
// same); and the timeline of the files, with a divider above it to make it taller or shorter,
// where they can be scrubbed through and put in order.
struct SyncLayout {
	LayoutRect header;
	LayoutRect map;            // The left of the panes.
	LayoutRect strip;          // Between the map and the video.
	LayoutRect preview_area;   // The right of the panes, above the transport bar.
	LayoutRect preview;        // The picture itself, as large as fits, centered in preview_area.
	LayoutRect transport;      // Under the video, as wide as its column.
	LayoutRect splitter;       // The divider between the panes and the timeline.
	LayoutRect timeline;
};

namespace window_layout {

const float LEFT_PANEL_WIDTH = 280.0f;     // The default; the user can drag the panel's edge.
const float MIN_PANEL_WIDTH = 120.0f;
const float PANEL_SPLITTER_WIDTH = 6.0f;
const float MIN_PREVIEW_COLUMN_WIDTH = 620.0f;   // The transport bar's buttons and times need this much.
const float TRANSPORT_HEIGHT = 72.0f;
const float PREVIEW_MARGIN = 8.0f;
const float SPLITTER_HEIGHT = 6.0f;
const float MIN_TIMELINE_HEIGHT = 110.0f;
const float MIN_UPPER_HEIGHT = 280.0f;     // The most the timeline may take is all but this much of the window.
const float DEFAULT_TIMELINE_HEIGHT = 180.0f;

// `menu_height` is the menu bar across the top, which the rest sits under. `canvas_aspect` is the
// picture's width divided by its height; the preview keeps it exactly (to the pixel) however the
// window is shaped. `panel_width` is the least the user wants the left panel to be; it is wider
// when the picture, at the biggest size the height allows, leaves room, and never so wide that
// the preview column is narrower than MIN_PREVIEW_COLUMN_WIDTH (the limit is in `widest_panel`).
// `timeline_height` is what the user asked for; it is kept between
// MIN_TIMELINE_HEIGHT and what leaves MIN_UPPER_HEIGHT above it, and the height used is in
// `timeline.h`. A window too small for anything gets empty rectangles, never negative ones.
WindowLayout compute(float window_width, float window_height, float menu_height, float canvas_aspect, float panel_width, float timeline_height);

const float SYNC_HEADER_HEIGHT = 36.0f;
const float SYNC_STRIP_WIDTH = 110.0f;        // The strip of buttons between the map and the video.
const float SYNC_MIN_PANES_HEIGHT = 200.0f;   // The most the timeline may take is all but this much (and the bars).

// Where the parts of the sync view go in a window of this size, under the menu bar. The timeline is
// `timeline_height` tall if that is within its limits (see MIN_TIMELINE_HEIGHT), and held to them if
// not. Like compute(), it never gives a negative size, and the picture keeps its shape to the pixel.
SyncLayout compute_sync(float window_width, float window_height, float menu_height, float canvas_aspect, float timeline_height);

}  // namespace window_layout
