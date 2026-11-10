#include "path_tracker.h"

#include "frame_mapping.h"
#include "media_container_manager.h"

namespace {
const double PI = 3.14159265358979323846;
}  // namespace

PathTracker::PathTracker(EditModel* model, MediaContainerMgr* media, double canvas_aspect)
	: m_model(model), m_media(media), m_canvas_aspect(canvas_aspect) {}

void PathTracker::update() {
	m_loader.update();
}

tracking::CameraLens PathTracker::lens() const {
	// The lens sees the picture the footage has, whatever the canvas it is shown on.
	const double aspect = m_media->get_height() > 0.0f ? (double)m_media->get_width() / (double)m_media->get_height() : 16.0 / 9.0;
	return tracking::CameraLens(m_model->path_fov_degrees() * PI / 180.0, aspect);
}

void PathTracker::request(const TrackedPath& path, bool urgent) {
	if (!path.curve.orientation_aware() || path.media < 0 || path.media >= m_media->media_count())
		return;
	if (!urgent && m_loader.busy() && !m_loader.loading(path.media))
		return;      // Another file is being read: asking now would stop it.
	const FileTime now = m_model->edit_list().source_for(m_media->current_ticks());
	const double start = now.media == path.media ? m_model->ticks_to_seconds(now.local_ticks) : 0.0;
	m_loader.request(path.media, m_media->media_path(path.media), m_model->seconds_per_tick(), start);
}

bool PathTracker::orientations_pending() const {
	const std::vector<TrackedPath>& paths = m_model->paths();
	for (size_t i = 0; i < paths.size(); i++) {
		const TrackedPath& path = paths[i];
		if (path.curve.empty() || !path.curve.orientation_aware() || m_loader.is_read(path.media))
			continue;
		bool drawn = path.circle.enabled;
		for (size_t k = 0; k < m_model->text_boxes().size(); k++)
			drawn = drawn || m_model->text_boxes()[k].path_id == path.id;
		if (drawn)
			return true;
	}
	return false;
}

const tracking::CameraOrientations& PathTracker::orientations(const TrackedPath& path) const {
	return m_loader.orientations(path.media);
}

path_attachment::Placement PathTracker::place(const TextBox& box, double rotation_radians, double aspect, int64_t timeline_ticks) {
	const TrackedPath* path = m_model->find_path(box.path_id);
	if (path == NULL)
		return path_attachment::Placement();
	request(*path);
	return path_attachment::place(*m_model, box, lens(), orientations(*path), rotation_radians, aspect, timeline_ticks);
}

void PathTracker::scene(double rotation_radians, int64_t timeline_ticks, PathScene* scene) {
	scene->circles.clear();
	scene->lines.clear();
	scene->boxes.clear();
	const double aspect = m_canvas_aspect;
	const std::vector<TextBox>& boxes = m_model->text_boxes();
	for (size_t i = 0; i < boxes.size(); i++) {
		const TextBox& box = boxes[i];
		PathScene::Box shown;
		shown.index = i;
		if (box.path_id == 0) {
			shown.opacity = path_attachment::text_box_alpha(*m_model, box, timeline_ticks);
			shown.x = box.x;
			shown.y = box.y;
			if (shown.opacity > 0.0f)
				scene->boxes.push_back(shown);
			continue;
		}
		const TrackedPath* path = m_model->find_path(box.path_id);
		const path_attachment::Placement placement = place(box, rotation_radians, aspect, timeline_ticks);
		if (path == NULL || placement.alpha <= 0.0f)
			continue;
		shown.opacity = placement.alpha;
		shown.x = placement.x;
		shown.y = placement.y;
		scene->boxes.push_back(shown);
		if (placement.has_leader_line) {
			PathScene::Line line;
			line.from_x = placement.line[0];
			line.from_y = placement.line[1];
			line.to_x = placement.line[2];
			line.to_y = placement.line[3];
			for (int c = 0; c < 3; c++)
				line.color[c] = path->circle.color[c];
			line.opacity = placement.alpha;
			scene->lines.push_back(line);
		}
		if (!path->circle.enabled)
			continue;
		// One circle to a path, as opaque as the most opaque of its text.
		bool drawn = false;
		for (size_t c = 0; c < scene->circles.size(); c++) {
			if (scene->circles[c].path_id == path->id) {
				drawn = true;
				if (placement.alpha > scene->circles[c].opacity)
					scene->circles[c].opacity = placement.alpha;
			}
		}
		if (!drawn) {
			PathScene::Circle circle;
			circle.path_id = path->id;
			circle.x = placement.marker_x;
			circle.y = placement.marker_y;
			circle.radius = placement.circle_radius;
			for (int c = 0; c < 3; c++)
				circle.color[c] = path->circle.color[c];
			circle.opacity = placement.alpha;
			scene->circles.push_back(circle);
		}
	}
	// The circles of paths with no text attached, while the path is there.
	const std::vector<TrackedPath>& paths = m_model->paths();
	for (size_t i = 0; i < paths.size(); i++) {
		const TrackedPath& path = paths[i];
		if (!path.circle.enabled || path.curve.empty())
			continue;
		bool drawn = false;
		for (size_t c = 0; c < scene->circles.size(); c++)
			drawn = drawn || scene->circles[c].path_id == path.id;
		if (drawn)
			continue;
		PathScene::Circle circle;
		circle.path_id = path.id;
		if (!marker(path, rotation_radians, aspect, timeline_ticks, &circle.x, &circle.y))
			continue;
		circle.radius = (double)path.circle.radius * frame_mapping::cover_scale(rotation_radians, aspect);
		for (int c = 0; c < 3; c++)
			circle.color[c] = path.circle.color[c];
		circle.opacity = 1.0f;
		scene->circles.push_back(circle);
	}
}

bool PathTracker::marker(const TrackedPath& path, double rotation_radians, double aspect, int64_t timeline_ticks, double* x, double* y) {
	if (path.curve.empty())
		return false;
	request(path);
	const std::vector<path_spans::Piece> pieces = path_spans::footage_pieces(m_model->edit_list(), path.media, path.curve.first_ticks(), path.curve.last_ticks());
	int64_t footage_ticks = 0;
	if (!path_attachment::footage_ticks_at(m_model->edit_list(), path.media, pieces, timeline_ticks, &footage_ticks))
		return false;
	return path_attachment::marker_canvas(path, footage_ticks, lens(), orientations(path), rotation_radians, aspect, x, y);
}
