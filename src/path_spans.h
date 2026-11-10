#pragma once

// Where a path shows on the edit timeline (for its boxes on the timeline editor, see timeline_view.h). A path lives on the footage of
// one file, from its first point to its last (see tracking_path.h), and the edit may use that footage in several pieces (the clip was
// split, parts were cut out, the clips were put in another order), so a path has a stretch of the timeline for each clip that holds some
// of its lifespan. No drawing in here, so it can be tested.
//
// Times are ticks: the path's own in ticks of its file, the results in ticks of the edit timeline (see edit_model.h).

#include <stdint.h>
#include <vector>

#include "edit_model.h"
#include "text_lanes.h"

namespace path_spans {

// One stretch of the timeline where a path is, and the part of the file's footage it shows there.
struct Piece {
	text_lanes::Span span;     // On the timeline.
	int64_t          from;     // The file's ticks at its start and at its end (`span.end` is the tick of `to`).
	int64_t          to;
	Piece() : from(0), to(0) {}
};

// What pieces() gives, with where in the footage each stretch is.
std::vector<Piece> footage_pieces(const EditList& list, int media, int64_t first_ticks, int64_t last_ticks);

// The stretches of the timeline where a path on file `media`, lasting from `first_ticks` to `last_ticks` of that file, is: one for each
// clip of the file that uses some of that lifespan, in the order of the clips, each from where the lifespan begins in the clip (or the clip
// begins) to where it ends in the clip (or the clip ends). A path of one point gives stretches with no length. None for a lifespan the edit
// does not use, a file it does not use, or a lifespan that is backwards.
std::vector<text_lanes::Span> pieces(const EditList& list, int media, int64_t first_ticks, int64_t last_ticks);

}  // namespace path_spans
