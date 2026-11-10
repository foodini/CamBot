#pragma once

// A text box attached to a path (see TextBox::path_id): when it shows, and where on the picture it is and what is drawn with it. The
// box shows for the path's lifespan, in each of the pieces of the timeline the edit has the path in (see path_spans.h), and never
// outside it: its fade in begins where the path's first point is in the piece, and its fade out ends where the last is. (A box has its
// own timing otherwise: the box's anchor and dwell are not used while it is attached.) A piece too short for both fades has them cut
// down in proportion, as text_box::set_total_ticks() does.
//
// A box attached to follow the path (ATTACH_FOLLOW) is at the marker plus its offset. A box attached with a leader line
// (ATTACH_LEADER) is where it is, and the line runs from the nearest point of its outline to the circle (the nearest point of the
// circle's perimeter to the box), or, where the path has no circle, to a small gap short of the marker.
//
// Places on the picture are fractions of the canvas, from its top left (see frame_mapping.h); lengths that are not fractions of its width
// are fractions of its height. No drawing in here, so it can be tested.

#include <stdint.h>
#include <vector>

#include "edit_model.h"
#include "path_spans.h"
#include "text_lanes.h"
#include "tracked_path.h"
#include "tracking_path.h"

namespace path_attachment {

// How far short of the marker a leader line stops where the path has no circle, as a fraction of the canvas's height.
const double LEADER_GAP = 0.006;

// ---- Time ----

// How opaque a box attached to a path with these pieces is at `timeline_ticks`: 0 outside the pieces, rising from 0 through `fade_in_ticks`
// from where a piece begins, and falling to 0 through `fade_out_ticks` to where it ends, 1 between (a fade of no length is a cut).
float alpha_at(const std::vector<path_spans::Piece>& pieces, int64_t fade_in_ticks, int64_t fade_out_ticks, int64_t timeline_ticks);

// The stretches of the timeline a box shows on: the pieces.
std::vector<text_lanes::Span> shown_spans(const std::vector<path_spans::Piece>& pieces);

// The footage tick of file `media` the path has at `timeline_ticks`, which has to be inside a piece; false if it is not.
bool footage_ticks_at(const EditList& list, int media, const std::vector<path_spans::Piece>& pieces, int64_t timeline_ticks,
	int64_t* footage_ticks);

// ---- The model's boxes ----

// The pieces of the path box `box` is attached to; none if it is not attached, or the path is not there or has no point.
std::vector<path_spans::Piece> pieces_of(const EditModel& model, const TextBox& box);

// When box `box` shows, whether or not it is attached: from the first frame it fades in on to where it has gone. (An attached box shows
// in pieces: this is from the start of the first to the end of the last, or 0 to 0 if it shows nowhere.)
void text_box_span(const EditModel& model, const TextBox& box, int64_t* start_ticks, int64_t* end_ticks);
// The stretches of the timeline it shows on: that one for an ordinary box, a stretch for each piece of the path for an attached one.
std::vector<text_lanes::Span> text_box_spans(const EditModel& model, const TextBox& box);
// How opaque the box is at `timeline_ticks`.
float text_box_alpha(const EditModel& model, const TextBox& box, int64_t timeline_ticks);

// ---- Place ----

// Where the footage tick `footage_ticks` of path `path` is on the canvas (a canvas `aspect` wide for its height, the picture turned by
// `rotation_radians`). False if the path has no point to give.
bool marker_canvas(const TrackedPath& path, int64_t footage_ticks, const tracking::CameraLens& lens,
	const tracking::CameraOrientations& orientations, double rotation_radians, double aspect, double* x, double* y);

// The line a leader line is drawn along: from the nearest point of the box's outline (`x`, `y`, `w`, `h`) to the centre (`center_x`,
// `center_y`) ending `stop_radius` (a fraction of the canvas's height) short of it, as places on the canvas. False if there is no line:
// the centre is in the box, or the box is within `stop_radius` of it.
bool leader_line(float x, float y, float w, float h, double center_x, double center_y, double stop_radius, double aspect,
	double* from_x, double* from_y, double* to_x, double* to_y);

// What is to be drawn for an attached box at one moment.
struct Placement {
	float  alpha;            // 0: draw nothing.
	float  x, y, w, h;       // The box.
	double marker_x, marker_y;
	double circle_radius;    // A fraction of the canvas's height; 0 if the path has no circle.
	bool   has_leader_line;
	double line[4];          // From x, y and to x, y.
	Placement() : alpha(0.0f), x(0.0f), y(0.0f), w(0.0f), h(0.0f), marker_x(0.0), marker_y(0.0), circle_radius(0.0), has_leader_line(false) {
		line[0] = line[1] = line[2] = line[3] = 0.0;
	}
};

// The box as it is at `timeline_ticks`: the path's place at that moment (from the camera's `orientations`, as tracking::TrackingPath
// gives it) and so where the box is, and what runs from it. `box` must be attached (its path_id is not 0) to a path of `model`;
// alpha is 0 if not or if the path is not there at that time.
Placement place(const EditModel& model, const TextBox& box, const tracking::CameraLens& lens,
	const tracking::CameraOrientations& orientations, double rotation_radians, double aspect, int64_t timeline_ticks);

}  // namespace path_attachment
