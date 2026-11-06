#pragma once

// The little square on the timeline at each join between clips, and at the start and end of the video, that stands for the
// transition there (an empty box for a plain cut) and is what is clicked to edit it. Only the arithmetic of where to draw it is
// here, so it can be tested.
//
// The icon is `side` pixels square (a third of the height of the clips' row), and sits on the join with half of it over each clip.
// When the transition takes longer than that to play (its zone, from where the second clip starts to where the first ends, is
// wider than the icon) the icon is stretched to cover the whole zone. A clip too short to hold its half of an icon (which may
// take up to ICON_SHARE of the clip, so the icons at the two ends of a short clip never meet) gets the icon crushed to what it
// can hold, and one that comes out narrower than ICON_MIN_WIDTH is left out. At the very start and end of the video, where
// there is no clip on the outside, the icon sits wholly inside the clip, against its edge.

namespace seam_icon {

const float ICON_SHARE = 0.45f;
const float ICON_MIN_WIDTH = 3.0f;

struct Span {
	bool  visible;
	float left;      // Pixels, in the same terms as the arguments.
	float right;
	Span() : visible(false), left(0.0f), right(0.0f) {}
};

// A join between two clips. The transition's zone runs from `zone_left` to `zone_right` (equal for a plain cut); `before_width`
// and `after_width` are the widths in pixels of the clips either side.
Span for_join(float zone_left, float zone_right, float before_width, float after_width, float side);
// The start of the video, with the first clip `clip_width` wide. The zone runs from `zone_left` to `zone_right`.
Span for_start(float zone_left, float zone_right, float clip_width, float side);
// The end of the video, with the last clip `clip_width` wide.
Span for_end(float zone_left, float zone_right, float clip_width, float side);

}  // namespace seam_icon
