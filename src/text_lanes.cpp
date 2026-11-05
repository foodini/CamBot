#include "text_lanes.h"

#include <stddef.h>

namespace text_lanes {

namespace {

int64_t clamp(int64_t value, int64_t low, int64_t high) {
	if (value > high)
		value = high;
	if (value < low)
		value = low;
	return value;
}

}  // namespace

std::vector<int> assign(const std::vector<Span>& spans, int* lane_count) {
	const size_t count = spans.size();
	// Indices in order of start, the earlier of equal starts first.
	std::vector<size_t> order(count);
	for (size_t i = 0; i < count; i++) {
		size_t at = i;
		while (at > 0 && spans[order[at - 1]].start > spans[i].start) {
			order[at] = order[at - 1];
			at--;
		}
		order[at] = i;
	}

	std::vector<int> lanes(count, 0);
	std::vector<int64_t> lane_ends;       // Where the last box on each row ends.
	for (size_t n = 0; n < count; n++) {
		const Span& span = spans[order[n]];
		int lane = -1;
		for (size_t k = 0; k < lane_ends.size(); k++) {
			if (lane_ends[k] <= span.start) {
				lane = (int)k;
				break;
			}
		}
		if (lane < 0) {
			lane = (int)lane_ends.size();
			lane_ends.push_back(span.end);
		} else {
			lane_ends[lane] = span.end;
		}
		lanes[order[n]] = lane;
	}
	if (lane_count != NULL)
		*lane_count = (int)lane_ends.size();
	return lanes;
}

Span dragged(const Span& span, int part, int64_t delta, int64_t timeline_ticks, int64_t min_length) {
	Span result = span;
	if (part == BODY) {
		const int64_t length = span.length();
		int64_t latest_start = timeline_ticks - length;
		if (latest_start < 0)
			latest_start = 0;
		result.start = clamp(span.start + delta, 0, latest_start);
		result.end = result.start + length;
	} else if (part == LEFT_END) {
		int64_t latest_start = span.end - min_length;
		if (latest_start < 0)
			latest_start = 0;
		result.start = clamp(span.start + delta, 0, latest_start);
	} else if (part == RIGHT_END) {
		int64_t earliest_end = span.start + min_length;
		if (earliest_end > timeline_ticks && timeline_ticks > span.end)
			earliest_end = timeline_ticks;
		int64_t latest_end = timeline_ticks;
		if (latest_end < earliest_end)
			latest_end = earliest_end;
		result.end = clamp(span.end + delta, earliest_end, latest_end);
	}
	return result;
}

}  // namespace text_lanes
