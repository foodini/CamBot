#include "path_panel.h"

#include <algorithm>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "imgui/imgui.h"

#include "panel_section.h"

#include "env_config.h"
#include "frame_mapping.h"
#include "media_container_manager.h"
#include "text_panel.h"
#include "timecode.h"
#include "timeline_editor.h"

namespace {

const double PI = 3.14159265358979323846;
const int    LIST_ROWS = 4;
const float  CONTROL_LABEL_WIDTH = 82.0f;
const float  HIT_RADIUS = 10.0f;         // How near the mouse has to be to a point to take hold of it, in pixels.
const float  DRAG_PIXELS = 4.0f;         // How far the mouse goes with a point held before it is a drag and not a click.
const int    MOST_SAMPLES = 600;         // The most places the curve is worked out at to be drawn.
const size_t NAME_BYTES = 96;

ImU32 color_of(const PathCircle& circle, int alpha) {
	return IM_COL32((int)(circle.color[0] * 255.0f + 0.5f), (int)(circle.color[1] * 255.0f + 0.5f), (int)(circle.color[2] * 255.0f + 0.5f), alpha);
}

}  // namespace

PathPanel::PathPanel(EditModel* model, MediaContainerMgr* media, TimelineEditor* editor, TextPanel* text_panel)
	: m_model(model), m_media(media), m_editor(editor), m_text_panel(text_panel), m_selected(0), m_editing(false), m_curve_shown(true),
	  m_has_point(false), m_point_ticks(0), m_pressing(false), m_press_placed(false), m_press_moved(false), m_press_ticks(0),
	  m_press_x(0.0f), m_press_y(0.0f), m_changing(false) {}

TrackedPath* PathPanel::selected_path() {
	return m_selected != 0 ? m_model->find_path(m_selected) : NULL;
}

const TrackedPath* PathPanel::selected_path() const {
	return m_selected != 0 ? m_model->find_path(m_selected) : NULL;
}

tracking::CameraLens PathPanel::lens() const {
	const double aspect = m_media->get_height() > 0.0f ? (double)m_media->get_width() / (double)m_media->get_height() : 16.0 / 9.0;
	return tracking::CameraLens(m_model->path_fov_degrees() * PI / 180.0, aspect);
}

const tracking::CameraOrientations& PathPanel::orientations(const TrackedPath& path) const {
	return m_loader.orientations(path.media);
}

// Asks for the camera's orientation through the file the selected path follows (a quick call once it has been asked for).
void PathPanel::ask_for_orientations(const TrackedPath& path) {
	if (!path.curve.orientation_aware() || path.media < 0 || path.media >= m_media->media_count())
		return;
	const FileTime now = m_model->edit_list().source_for(m_media->current_ticks());
	const double start = now.media == path.media ? m_model->ticks_to_seconds(now.local_ticks) : 0.0;
	m_loader.request(path.media, m_media->media_path(path.media), m_model->seconds_per_tick(), start);
}

void PathPanel::begin_change() {
	if (!m_changing) {
		m_editor->begin_box_edit(*m_model, *m_media);
		m_changing = true;
	}
}

// A control that changes the path (a slider, a colour, a tick box) opens an undo step with its first change, and it is closed here
// when the mouse is up, so a drag is one step.
void PathPanel::settle() {
	if (m_changing && !ImGui::IsAnyMouseDown()) {
		m_editor->end_box_edit(*m_model, *m_media);
		m_changing = false;
	}
}

void PathPanel::start_editing() {
	m_editing = true;
	m_has_point = false;
	m_pressing = false;
	m_text_panel->select(0);      // A text box and a path are not edited together.
	EnvConfig::instance->pause();
}

void PathPanel::stop_editing() {
	if (m_pressing && (m_press_placed || m_press_moved))
		m_editor->end_box_edit(*m_model, *m_media);
	m_editing = false;
	m_pressing = false;
	m_has_point = false;
	m_hover_text.clear();
}

bool PathPanel::timeline_ticks_for(int media, int64_t local_ticks, int64_t* timeline_ticks) const {
	const EditList& list = m_model->edit_list();
	const std::vector<Clip>& clips = list.clips();
	const int64_t here = m_media->current_ticks();
	bool found = false;
	int64_t best = 0;
	for (int i = 0; i < (int)clips.size(); i++) {
		if (clips[i].media != media || local_ticks < clips[i].in_ticks || local_ticks > clips[i].out_ticks)
			continue;
		const int64_t at = list.timeline_for(i, local_ticks);
		if (!found || llabs(at - here) < llabs(best - here)) {
			best = at;
			found = true;
		}
	}
	if (found)
		*timeline_ticks = best;
	return found;
}

void PathPanel::go_to_frame(int media, int64_t local_ticks) {
	int64_t target = 0;
	if (m_media->recording() || !timeline_ticks_for(media, local_ticks, &target))
		return;
	const int64_t last_frame = m_model->edit_list().total_ticks() - m_media->frame_ticks();
	if (target > last_frame)
		target = last_frame;
	if (target < 0)
		target = 0;
	EnvConfig::instance->pause();
	m_media->seek_to_keyframe_near(target);     // Near first and then exact, as scrubbing does, so a far jump is quick.
	m_media->advance_to(target);
}

bool PathPanel::delete_pressed() {
	TrackedPath* path = selected_path();
	if (!m_editing || path == NULL || m_media->recording())
		return false;
	if (m_has_point) {
		m_editor->remove_path_point(m_model, m_media, path->id, m_point_ticks);
		m_has_point = false;
	}
	return true;
}

bool PathPanel::trim_pressed(bool left) {
	TrackedPath* path = selected_path();
	if (!m_editing || path == NULL || m_media->recording())
		return false;
	m_editor->trim_path(m_model, m_media, path->id, left, lens(), orientations(*path));
	return true;
}

void PathPanel::draw(bool can_change) {
	settle();
	if (!panel_section::header("Paths"))
		return;
	if (m_selected != 0 && m_model->find_path(m_selected) == NULL) {
		m_selected = 0;
		stop_editing();
	}

	const ImVec2 full_width(-FLT_MIN, 0.0f);
	const bool can_add = can_change && !m_model->edit_list().clips().empty();
	ImGui::BeginDisabled(!can_add);
	if (ImGui::Button("Add Path", full_width)) {
		// A path starts out empty, for the user to put its first point on the frame shown.
		TrackedPath path;
		int number = (int)m_model->paths().size() + 1;
		for (;; number++) {
			const std::string name = "Path " + std::to_string(number);
			bool taken = false;
			for (size_t i = 0; i < m_model->paths().size(); i++)
				taken = taken || m_model->paths()[i].name == name;
			if (!taken) {
				path.name = name;
				break;
			}
		}
		path.media = m_model->edit_list().source_for(m_media->current_ticks()).media;
		const int id = m_editor->add_path(m_model, m_media, path);
		if (id != 0) {
			m_selected = id;
			m_curve_shown = true;
			start_editing();
		}
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("Follow something that moves through the picture: add a path, then click on it in the video\nat a few frames. The path joins the points up.");

	const std::vector<TrackedPath>& paths = m_model->paths();
	if (paths.empty()) {
		ImGui::TextDisabled("(no paths yet)");
		return;
	}

	const float row_height = ImGui::GetTextLineHeightWithSpacing();
	ImGui::BeginChild("##pathlist", ImVec2(0.0f, row_height * (float)std::min<size_t>(paths.size(), LIST_ROWS) + 8.0f), ImGuiChildFlags_Borders);
	for (size_t n = 0; n < paths.size(); n++) {
		const TrackedPath& entry = paths[n];
		const std::string label = entry.name + "  (" + std::to_string(entry.curve.points().size()) + ")##" + std::to_string(entry.id);
		if (ImGui::Selectable(label.c_str(), entry.id == m_selected)) {
			const bool was_selected = entry.id == m_selected;
			stop_editing();
			m_selected = was_selected ? 0 : entry.id;
		}
	}
	ImGui::EndChild();

	TrackedPath* path = selected_path();
	if (path == NULL)
		return;
	const int id = path->id;
	const float control_width = -CONTROL_LABEL_WIDTH;
	const bool on_this_file = m_editor->footage_ticks_at_playhead(*m_model, *m_media, path->media) >= 0;

	ImGui::BeginDisabled(!can_change);
	ImGui::BeginDisabled(!m_editing && !on_this_file);
	if (ImGui::Button(m_editing ? "Done Editing" : "Edit Points", full_width)) {
		if (m_editing)
			stop_editing();
		else
			start_editing();
	}
	ImGui::EndDisabled();
	if (!on_this_file)
		ImGui::TextDisabled("This path is on footage from another file.");
	if (m_editing) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		ImGui::TextWrapped("Click the picture to put the path's point for this frame there, drag to move it. Click a point to go to its frame. "
			"Delete removes the selected point, [ and ] cut the path to this frame, Tab hides the curve, Esc finishes.");
		ImGui::PopStyleColor();
	}

	char name_buffer[NAME_BYTES];
	snprintf(name_buffer, sizeof(name_buffer), "%s", path->name.c_str());
	ImGui::SetNextItemWidth(control_width);
	const bool renamed = ImGui::InputText("Name", name_buffer, sizeof(name_buffer));
	if (ImGui::IsItemActivated())
		m_editor->begin_box_edit(*m_model, *m_media);
	if (renamed)
		m_editor->set_path_name(m_model, id, std::string(name_buffer));
	if (ImGui::IsItemDeactivated())
		m_editor->end_box_edit(*m_model, *m_media);

	bool aware = path->curve.orientation_aware();
	if (ImGui::Checkbox("Follow camera orientation", &aware)) {
		begin_change();
		m_editor->set_path_orientation_aware(m_model, id, aware);
	}
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Keeps the path on a target that holds its bearing while the camera turns and banks,\nand makes the points you put down correct for the target's own movement.");
	if (aware) {
		if (m_loader.loading(path->media))
			ImGui::TextDisabled("Reading the camera's orientation... %d%%", (int)(m_loader.progress() * 100.0f));
		else if (m_loader.has_orientations(path->media))
			ImGui::TextDisabled("Following the camera's orientation.");
		else if (m_loader.is_read(path->media))
			ImGui::TextDisabled("No orientation in this file: using places in the picture.");
		float fov = (float)m_model->path_fov_degrees();
		ImGui::SetNextItemWidth(control_width);
		if (ImGui::DragFloat("Lens FOV", &fov, 0.2f, 30.0f, 170.0f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp))
			m_model->set_path_fov_degrees(fov);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("How wide the lens sees across the picture, for the lens mode you shoot in. If a target that is still\nin the world drifts across the picture as the camera turns, this is off: the right value stops it.");
	}

	PathCircle circle = path->circle;
	bool circle_changed = false;
	if (ImGui::Checkbox("Circle", &circle.enabled))
		circle_changed = true;
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Draws a circle round the target in the export.");
	float percent = circle.radius * 100.0f;
	ImGui::SetNextItemWidth(control_width);
	if (ImGui::DragFloat("Size", &percent, 0.05f, 0.5f, 25.0f, "%.1f%%", ImGuiSliderFlags_AlwaysClamp)) {
		circle.radius = percent / 100.0f;
		circle_changed = true;
	}
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("The circle's radius, as a percent of the picture's height.");
	if (ImGui::ColorEdit3("Color", circle.color, ImGuiColorEditFlags_NoInputs))
		circle_changed = true;
	if (circle_changed) {
		begin_change();
		m_editor->set_path_circle(m_model, id, circle);
	}
	ImGui::EndDisabled();
}

void PathPanel::draw_on_preview(const LayoutRect& preview, bool can_change) {
	m_loader.update();
	m_hover_text.clear();
	TrackedPath* path = selected_path();
	if (path == NULL) {
		m_editing = false;
		m_pressing = false;
		m_has_point = false;
		return;
	}
	ask_for_orientations(*path);
	if (!can_change || m_model->in_source_view() || preview.w < 1.0f || preview.h < 1.0f) {
		m_pressing = false;
		return;
	}
	if (m_editing && m_text_panel->selected_id() != 0)
		stop_editing();          // A text box was picked: that is the one being edited now.
	if (m_has_point && !path->curve.empty()) {
		bool there = false;
		for (size_t i = 0; i < path->curve.points().size(); i++)
			there = there || path->curve.points()[i].ticks == m_point_ticks;
		m_has_point = there;
	} else if (path->curve.empty()) {
		m_has_point = false;
	}
	// Only the footage of the path's own file has the path on it.
	if (m_editor->footage_ticks_at_playhead(*m_model, *m_media, path->media) < 0) {
		m_pressing = false;
		return;
	}
	const ImGuiIO& io = ImGui::GetIO();
	if (!io.WantTextInput) {
		if (ImGui::IsKeyPressed(ImGuiKey_Tab, false))
			m_curve_shown = !m_curve_shown;
		if (m_editing && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
			stop_editing();
	}
	if (m_editing)
		handle_mouse(preview, *path);
	path = selected_path();
	if (path != NULL)
		draw_overlay(preview, *path);
}

void PathPanel::handle_mouse(const LayoutRect& preview, const TrackedPath& path) {
	const ImGuiIO& io = ImGui::GetIO();
	const int id = path.id;
	const double rotation = m_media->rotation_angle();
	const double aspect = (double)preview.w / (double)preview.h;
	const int64_t now = m_editor->footage_ticks_at_playhead(*m_model, *m_media, path.media);
	const ImVec2 mouse = io.MousePos;
	const bool over = mouse.x >= preview.x && mouse.x < preview.x + preview.w && mouse.y >= preview.y && mouse.y < preview.y + preview.h;

	// Where the mouse is in the picture.
	double mouse_u = 0.0;
	double mouse_v = 0.0;
	frame_mapping::preview_to_picture((mouse.x - preview.x) / preview.w, (mouse.y - preview.y) / preview.h, rotation, aspect, &mouse_u, &mouse_v);
	mouse_u = std::min(1.0, std::max(0.0, mouse_u));
	mouse_v = std::min(1.0, std::max(0.0, mouse_v));

	if (m_pressing) {
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			if (m_press_placed || m_press_moved)
				m_editor->end_box_edit(*m_model, *m_media);
			else if (m_press_ticks != now)
				go_to_frame(path.media, m_press_ticks);      // A click on another frame's point goes to its frame.
			m_pressing = false;
			return;
		}
		if (!m_press_placed && !m_press_moved) {
			const float dx = mouse.x - m_press_x;
			const float dy = mouse.y - m_press_y;
			if (dx * dx + dy * dy >= DRAG_PIXELS * DRAG_PIXELS) {
				m_press_moved = true;
				m_editor->begin_box_edit(*m_model, *m_media);
			}
		}
		if (m_press_placed || m_press_moved)
			m_editor->set_path_point(m_model, id, m_press_ticks, mouse_u, mouse_v);
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
		return;
	}
	if (!over || io.WantCaptureMouse)
		return;

	// The point nearest the mouse, if one is near enough.
	const std::vector<tracking::PathPoint>& points = path.curve.points();
	int hit = -1;
	float hit_distance = HIT_RADIUS * HIT_RADIUS;
	for (size_t i = 0; i < points.size(); i++) {
		double x, y;
		frame_mapping::picture_to_preview(points[i].u, points[i].v, rotation, aspect, &x, &y);
		const float dx = preview.x + (float)x * preview.w - mouse.x;
		const float dy = preview.y + (float)y * preview.h - mouse.y;
		if (dx * dx + dy * dy <= hit_distance) {
			hit_distance = dx * dx + dy * dy;
			hit = (int)i;
		}
	}
	if (hit >= 0) {
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		const int64_t ticks = points[hit].ticks;
		const AVRational tick = m_media->tick_time_base();
		int64_t shown_at = ticks;
		timeline_ticks_for(path.media, ticks, &shown_at);
		const std::string when = timecode::format(timecode::for_ticks(shown_at, tick.num, tick.den, m_media->frame_ticks()), 2);
		const long frames = lround((double)(ticks - now) / (double)m_media->frame_ticks());
		char text[128];
		if (frames == 0)
			snprintf(text, sizeof(text), "Point at %s: this frame", when.c_str());
		else
			snprintf(text, sizeof(text), "Point at %s: %ld frame%s %s", when.c_str(), labs(frames), labs(frames) == 1 ? "" : "s", frames > 0 ? "ahead" : "behind");
		m_hover_text = text;
	}

	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		EnvConfig::instance->pause();
		m_pressing = true;
		m_press_x = mouse.x;
		m_press_y = mouse.y;
		m_press_moved = false;
		if (hit >= 0) {
			m_press_placed = false;
			m_press_ticks = points[hit].ticks;
		} else {
			// Not on a point: this frame's point goes here (a frame has one, so one that is already there moves).
			m_press_placed = true;
			m_press_ticks = now;
			m_editor->begin_box_edit(*m_model, *m_media);
			m_editor->set_path_point(m_model, id, now, mouse_u, mouse_v);
		}
		m_has_point = true;
		m_point_ticks = m_press_ticks;
	}
}

void PathPanel::draw_overlay(const LayoutRect& preview, const TrackedPath& path) {
	const double rotation = m_media->rotation_angle();
	const double aspect = (double)preview.w / (double)preview.h;
	const int64_t now = m_editor->footage_ticks_at_playhead(*m_model, *m_media, path.media);
	const tracking::CameraLens path_lens = lens();
	const tracking::CameraOrientations& view_orientations = orientations(path);
	const int64_t frame = std::max<int64_t>(1, m_media->frame_ticks());

	ImGui::SetNextWindowPos(ImVec2(preview.x, preview.y));
	ImGui::SetNextWindowSize(ImVec2(preview.w, preview.h));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus;
	if (ImGui::Begin("##path_on_preview", NULL, flags)) {
		ImDrawList* draw_list = ImGui::GetWindowDrawList();
		const ImU32 dark = IM_COL32(0, 0, 0, 200);
		const ImU32 light = IM_COL32(255, 255, 255, 255);
		const ImU32 color = color_of(path.circle, 255);
		struct Screen {
			static ImVec2 at(const LayoutRect& rect, double rotation_radians, double aspect_ratio, double u, double v) {
				double x, y;
				frame_mapping::picture_to_preview(u, v, rotation_radians, aspect_ratio, &x, &y);
				return ImVec2(rect.x + (float)x * rect.w, rect.y + (float)y * rect.h);
			}
		};

		if (m_curve_shown && !path.curve.empty()) {
			// The curve: where the path is in the picture at each frame it lasts, drawn all at once on the frame shown.
			const int64_t first = path.curve.first_ticks();
			const int64_t last = path.curve.last_ticks();
			int64_t step = frame;
			if ((last - first) / frame > MOST_SAMPLES)
				step = frame * (((last - first) / frame + MOST_SAMPLES - 1) / MOST_SAMPLES);
			std::vector<ImVec2> run;
			for (int64_t t = first; first < last; t += step) {
				if (t > last)
					t = last;
				double u, v;
				if (path.curve.position_at(t, path_lens, view_orientations, &u, &v)) {
					run.push_back(Screen::at(preview, rotation, aspect, u, v));
				} else if (!run.empty()) {
					if (run.size() >= 2) {
						draw_list->AddPolyline(run.data(), (int)run.size(), dark, 0, 4.0f);
						draw_list->AddPolyline(run.data(), (int)run.size(), color, 0, 2.0f);
					}
					run.clear();
				}
				if (t == last)
					break;
			}
			if (run.size() >= 2) {
				draw_list->AddPolyline(run.data(), (int)run.size(), dark, 0, 4.0f);
				draw_list->AddPolyline(run.data(), (int)run.size(), color, 0, 2.0f);
			}
			if (m_editing) {
				// The points that were set: a dot each, a ring on the one of the frame shown, and a filled white one on the selected point.
				for (size_t i = 0; i < path.curve.points().size(); i++) {
					const tracking::PathPoint& point = path.curve.points()[i];
					const ImVec2 at = Screen::at(preview, rotation, aspect, point.u, point.v);
					draw_list->AddCircleFilled(at, 5.0f, color);
					draw_list->AddCircle(at, 5.0f, dark, 0, 1.5f);
					if (point.ticks == now)
						draw_list->AddCircle(at, 9.0f, light, 0, 2.0f);
					if (m_has_point && point.ticks == m_point_ticks)
						draw_list->AddCircleFilled(at, 2.5f, light);
				}
			}
		}

		// The marker: the circle that will be round the target, at the place the path has on this frame.
		double u, v;
		if (!path.curve.empty() && path.curve.position_at(now, path_lens, view_orientations, &u, &v)) {
			const bool during = now >= path.curve.first_ticks() && now <= path.curve.last_ticks();
			if (during || m_editing) {
				const int alpha = during ? 255 : 110;
				const ImVec2 at = Screen::at(preview, rotation, aspect, u, v);
				const ImU32 marker = color_of(path.circle, alpha);
				if (path.circle.enabled) {
					const float radius = path.circle.radius * preview.h * (float)frame_mapping::cover_scale(rotation, aspect);
					draw_list->AddCircle(at, radius, IM_COL32(0, 0, 0, alpha * 200 / 255), 0, 5.0f);
					draw_list->AddCircle(at, radius, marker, 0, 3.0f);
				} else if (m_editing) {
					draw_list->AddLine(ImVec2(at.x - 8.0f, at.y), ImVec2(at.x + 8.0f, at.y), marker, 2.0f);
					draw_list->AddLine(ImVec2(at.x, at.y - 8.0f), ImVec2(at.x, at.y + 8.0f), marker, 2.0f);
				}
			}
		}
	}
	ImGui::End();
	ImGui::PopStyleVar(2);
}
