#pragma once

// The arithmetic of the text boxes on the timeline: which row of the text track each box goes on, and what dragging the
// ends or the body of one does to the stretch of time it shows. No drawing in here, so it can be tested.
//
// Times are ticks of the edit timeline (see edit_model.h).

#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace text_lanes {

struct Span {
	int64_t start;
	int64_t end;     // One past the last tick it shows.
	Span() : start(0), end(0) {}
	Span(int64_t first, int64_t after_last) : start(first), end(after_last) {}
	int64_t length() const { return end - start; }
};

// The row (counting from 0, the top) of each of `spans`: boxes that overlap in time go on different rows, and each box
// takes the highest row that is free where it begins, working through them in order of start (boxes with the same start in
// the order given). Returned in the order of `spans`; *lane_count is how many rows there are.
std::vector<int> assign(const std::vector<Span>& spans, int* lane_count);

enum Part { BODY = 0, LEFT_END = 1, RIGHT_END = 2 };

// The stretch of time `span` becomes when `part` of it is dragged `delta` ticks later (earlier if negative), kept inside the
// timeline (0 to `timeline_ticks`) and at least `min_length` long. The body moves the whole span, keeping its length; an end
// moves just that end. A span that began off the timeline, or is longer than it, is brought back in only as far as it must be.
Span dragged(const Span& span, int part, int64_t delta, int64_t timeline_ticks, int64_t min_length);

}  // namespace text_lanes
