#include "path_attachment.h"

#include <math.h>

#include "frame_mapping.h"

namespace path_attachment {

float alpha_at(const std::vector<path_spans::Piece>& pieces, int64_t fade_in_ticks, int64_t fade_out_ticks, int64_t timeline_ticks) {
	float best = 0.0f;
	for (size_t i = 0; i < pieces.size(); i++) {
		const int64_t start = pieces[i].span.start;
		const int64_t end = pieces[i].span.end;
		if (timeline_ticks < start || timeline_ticks > end)
			continue;
		// Too short a piece for both fades has them cut down in proportion.
		double fade_in = (double)fade_in_ticks;
		double fade_out = (double)fade_out_ticks;
		const double length = (double)(end - start);
		if (fade_in + fade_out > length && fade_in + fade_out > 0.0) {
			const double scale = length / (fade_in + fade_out);
			fade_in *= scale;
			fade_out *= scale;
		}
		float alpha = 1.0f;
		if (fade_in > 0.0 && (double)(timeline_ticks - start) < fade_in)
			alpha = (float)((double)(timeline_ticks - start) / fade_in);
		else if (fade_out > 0.0 && (double)(end - timeline_ticks) < fade_out)
			alpha = (float)((double)(end - timeline_ticks) / fade_out);
		if (alpha > best)
			best = alpha;
	}
	return best;
}

std::vector<text_lanes::Span> shown_spans(const std::vector<path_spans::Piece>& pieces) {
	std::vector<text_lanes::Span> result;
	for (size_t i = 0; i < pieces.size(); i++)
		result.push_back(pieces[i].span);
	return result;
}

bool footage_ticks_at(const EditList& list, int media, const std::vector<path_spans::Piece>& pieces, int64_t timeline_ticks,
	int64_t* footage_ticks) {
	for (size_t i = 0; i < pieces.size(); i++) {
		const path_spans::Piece& piece = pieces[i];
		if (timeline_ticks < piece.span.start || timeline_ticks > piece.span.end)
			continue;
		// The footage that is on the timeline then (the nearest end of the piece if that is another file's, as where clips overlap).
		const FileTime source = list.source_for(timeline_ticks);
		int64_t ticks = source.media == media ? source.local_ticks : piece.from;
		if (ticks < piece.from)
			ticks = piece.from;
		if (ticks > piece.to)
			ticks = piece.to;
		*footage_ticks = ticks;
		return true;
	}
	return false;
}

std::vector<path_spans::Piece> pieces_of(const EditModel& model, const TextBox& box) {
	std::vector<path_spans::Piece> none;
	if (box.path_id == 0)
		return none;
	const TrackedPath* path = model.find_path(box.path_id);
	if (path == NULL || path->curve.empty())
		return none;
	return path_spans::footage_pieces(model.edit_list(), path->media, path->curve.first_ticks(), path->curve.last_ticks());
}

void text_box_span(const EditModel& model, const TextBox& box, int64_t* start_ticks, int64_t* end_ticks) {
	if (box.path_id == 0) {
		model.text_box_span(box, start_ticks, end_ticks);
		return;
	}
	const std::vector<text_lanes::Span> spans = shown_spans(pieces_of(model, box));
	*start_ticks = 0;
	*end_ticks = 0;
	for (size_t i = 0; i < spans.size(); i++) {
		if (i == 0 || spans[i].start < *start_ticks)
			*start_ticks = spans[i].start;
		if (i == 0 || spans[i].end > *end_ticks)
			*end_ticks = spans[i].end;
	}
}

std::vector<text_lanes::Span> text_box_spans(const EditModel& model, const TextBox& box) {
	if (box.path_id != 0)
		return shown_spans(pieces_of(model, box));
	std::vector<text_lanes::Span> spans;
	text_lanes::Span span;
	model.text_box_span(box, &span.start, &span.end);
	spans.push_back(span);
	return spans;
}

float text_box_alpha(const EditModel& model, const TextBox& box, int64_t timeline_ticks) {
	if (box.path_id != 0)
		return alpha_at(pieces_of(model, box), box.fade_in_ticks, box.fade_out_ticks, timeline_ticks);
	int64_t start = 0;
	int64_t end = 0;
	model.text_box_span(box, &start, &end);
	return text_box::alpha_at(box, timeline_ticks - start);
}

bool marker_canvas(const TrackedPath& path, int64_t footage_ticks, const tracking::CameraLens& lens,
	const tracking::CameraOrientations& orientations, double rotation_radians, double aspect, double* x, double* y) {
	double u = 0.0;
	double v = 0.0;
	if (!path.curve.position_at(footage_ticks, lens, orientations, &u, &v))
		return false;
	frame_mapping::picture_to_preview(u, v, rotation_radians, aspect, x, y);
	return true;
}

bool leader_line(float x, float y, float w, float h, double center_x, double center_y, double stop_radius, double aspect,
	double* from_x, double* from_y, double* to_x, double* to_y) {
	// In units of the canvas's height both ways, so that distances are distances.
	const double left = (double)x * aspect;
	const double right = ((double)x + (double)w) * aspect;
	const double top = (double)y;
	const double bottom = (double)y + (double)h;
	const double cx = center_x * aspect;
	const double cy = center_y;
	const double nx = cx < left ? left : (cx > right ? right : cx);
	const double ny = cy < top ? top : (cy > bottom ? bottom : cy);
	const double dx = cx - nx;
	const double dy = cy - ny;
	const double distance = sqrt(dx * dx + dy * dy);
	if (distance <= stop_radius || distance <= 0.0)
		return false;
	*from_x = nx / aspect;
	*from_y = ny;
	*to_x = (cx - dx / distance * stop_radius) / aspect;
	*to_y = cy - dy / distance * stop_radius;
	return true;
}

Placement place(const EditModel& model, const TextBox& box, const tracking::CameraLens& lens,
	const tracking::CameraOrientations& orientations, double rotation_radians, double aspect, int64_t timeline_ticks) {
	Placement placement;
	const TrackedPath* path = model.find_path(box.path_id);
	if (box.path_id == 0 || path == NULL || path->curve.empty())
		return placement;
	const std::vector<path_spans::Piece> pieces = pieces_of(model, box);
	int64_t footage_ticks = 0;
	if (!footage_ticks_at(model.edit_list(), path->media, pieces, timeline_ticks, &footage_ticks))
		return placement;
	double marker_x = 0.0;
	double marker_y = 0.0;
	if (!marker_canvas(*path, footage_ticks, lens, orientations, rotation_radians, aspect, &marker_x, &marker_y))
		return placement;
	placement.alpha = alpha_at(pieces, box.fade_in_ticks, box.fade_out_ticks, timeline_ticks);
	placement.marker_x = marker_x;
	placement.marker_y = marker_y;
	placement.circle_radius = path->circle.enabled ? (double)path->circle.radius * frame_mapping::cover_scale(rotation_radians, aspect) : 0.0;
	placement.w = box.w;
	placement.h = box.h;
	if (box.attach_mode == ATTACH_LEADER) {
		placement.x = box.x;
		placement.y = box.y;
		const double stop = path->circle.enabled ? placement.circle_radius : LEADER_GAP;
		placement.has_leader_line = leader_line(box.x, box.y, box.w, box.h, marker_x, marker_y, stop, aspect,
			&placement.line[0], &placement.line[1], &placement.line[2], &placement.line[3]);
	} else {
		placement.x = (float)(marker_x + (double)box.offset_x);
		placement.y = (float)(marker_y + (double)box.offset_y);
	}
	return placement;
}

}  // namespace path_attachment
