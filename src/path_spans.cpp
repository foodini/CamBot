#include "path_spans.h"

namespace path_spans {

std::vector<Piece> footage_pieces(const EditList& list, int media, int64_t first_ticks, int64_t last_ticks) {
	std::vector<Piece> result;
	if (last_ticks < first_ticks)
		return result;
	const std::vector<Clip>& clips = list.clips();
	for (int i = 0; i < (int)clips.size(); i++) {
		const Clip& clip = clips[i];
		if (clip.media != media)
			continue;
		const int64_t from = first_ticks > clip.in_ticks ? first_ticks : clip.in_ticks;
		const int64_t to = last_ticks < clip.out_ticks ? last_ticks : clip.out_ticks;
		// The clip's last tick is the one before its out point: a lifespan beginning at the out point is not in it.
		if (from >= clip.out_ticks || from > to)
			continue;
		Piece piece;
		piece.span = text_lanes::Span(list.timeline_for(i, from), list.timeline_for(i, to));
		piece.from = from;
		piece.to = to;
		result.push_back(piece);
	}
	return result;
}

std::vector<text_lanes::Span> pieces(const EditList& list, int media, int64_t first_ticks, int64_t last_ticks) {
	std::vector<text_lanes::Span> result;
	const std::vector<Piece> found = footage_pieces(list, media, first_ticks, last_ticks);
	for (size_t i = 0; i < found.size(); i++)
		result.push_back(found[i].span);
	return result;
}

}  // namespace path_spans
