#include "sync_view.h"

#include <math.h>
#include <stdio.h>
#include <string>

#include "imgui/imgui.h"

#include "env_config.h"
#include "file_io.h"
#include "media_container_manager.h"
#include "telemetry.h"
#include "timeline_edit.h"
#include "timeline_scale.h"
#include "util.h"

namespace {


// H:MM:SS.s, with a minus sign for times before the start.
std::string format_seconds(double seconds) {
	const bool negative = seconds < 0.0;
	if (negative)
		seconds = -seconds;
	const int whole = (int)seconds;
	const int tenths = (int)((seconds - (double)whole) * 10.0);
	char text[64];
	snprintf(text, sizeof(text), "%s%d:%02d:%02d.%d", negative ? "-" : "", whole / 3600, (whole / 60) % 60, whole % 60, tenths);
	return std::string(text);
}

ImGuiWindowFlags bar_flags() {
	return ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
}

// The map. The path is colored by climb the way the video's map is, all of it alike (so what is ahead
// of the frame on screen is as easy to read as what is behind), as a bright line with a dark edge so it
// shows on anything.
const ImU32 COLOR_MAP_BACKGROUND = IM_COL32(28, 30, 34, 255);
const ImU32 COLOR_PATH_EDGE      = IM_COL32(0, 0, 0, 220);
const ImU32 COLOR_MARK_LIGHT     = IM_COL32(255, 255, 255, 255);
const ImU32 COLOR_MARK_GRAY      = IM_COL32(150, 150, 150, 255);
const ImU32 COLOR_MARKER         = IM_COL32(255, 215, 0, 255);
const ImU32 COLOR_MARKER_EDGE    = IM_COL32(0, 0, 0, 255);
const ImU32 COLOR_MAP_TEXT       = IM_COL32(235, 235, 235, 255);
const ImU32 COLOR_MAP_TEXT_BACK  = IM_COL32(0, 0, 0, 150);

const float PATH_WIDTH   = 4.0f;    // The core of the line; the edge is a little wider.
const float EDGE_EXTRA   = 3.0f;
const float MIN_PATH_BRIGHTNESS = 0.45f;   // Climb colors darker than this (strong sink) are lightened to it, to show on the dark map.
const float PATH_SPACING = 2.0f;    // Points closer than this on the screen are left out of the line.
const float PATH_MARGIN  = 60.0f;   // The line is only drawn this far past the edge of the pane.

const double FOLLOW_INSET = 0.25;   // Following keeps the frame on screen out of this fraction of the pane's edges.
const double RESUME_INSET = 0.08;   // The map was moved by hand: when the frame on screen moves this near an edge, following starts again.
const double FIT_MARGIN   = 0.08;   // Fitting the whole path leaves this much empty at each edge.
const float  CLICK_SLOP   = 4.0f;   // A press that moves less than this, in pixels, is a click.
const double ZOOM_LEVELS_PER_NOTCH = 0.5;
const float  PICK_RADIUS = 12.0f;           // Pixels: how near the path a click has to be to pick a point on it.
const float  CYCLE_SLOP = 4.0f;             // A click this near the last one, in pixels, is at the same spot.
const double SLIDE_WINDOW_SAMPLES = 50.0;   // A slide stays within this many samples (5 seconds) of where it is, so it stays on its pass.
const double NUDGE_RUN_SECONDS = 0.5;       // Nudges this close together are one undo step.
const size_t MAX_HISTORY = 200;
const double MIN_SPEED_FOR_HEADING = 3.0;   // Knots; slower than this the heading isn't to be trusted.
const float  AHEAD_SECONDS = 2.0f;          // How long a stretch of the heading is averaged for the look-ahead.
const float  LOOKAHEAD = 0.12f;             // At most this fraction of the pane's width and height is added ahead of the frame on screen.

void draw_path(ImDrawList* draw, const MapPolyline& line, float origin_x, float origin_y, ImU32 color, float width) {
	std::vector<ImVec2> points;
	int start = 0;
	for (size_t run = 0; run < line.run_ends.size(); run++) {
		const int end = line.run_ends[run];
		if (end - start >= 2) {
			points.clear();
			for (int i = start; i < end; i++)
				points.push_back(ImVec2(origin_x + line.points[i].x, origin_y + line.points[i].y));
			draw->AddPolyline(points.data(), (int)points.size(), color, ImDrawFlags_None, width);
		}
		start = end;
	}
}

// The path, in the climb colors: a stretch of one color is one line, and where the color changes a
// round cap is put at the join so the line has no notch there.
void draw_climb_path(ImDrawList* draw, const MapPolyline& line, float origin_x, float origin_y, float climb_range, float width) {
	std::vector<ImVec2> points;
	int start = 0;
	for (size_t run = 0; run < line.run_ends.size(); run++) {
		const int end = line.run_ends[run];
		int step = -1;
		points.clear();
		for (int i = start; i + 1 < end; i++) {
			const int segment_step = map_colors::climb_step((line.climb[i] + line.climb[i + 1]) * 0.5f, climb_range);
			if (segment_step != step) {
				const ImVec2 joint(origin_x + line.points[i].x, origin_y + line.points[i].y);
				if (step >= 0 && points.size() >= 2) {
					const MapColor old_color = map_colors::step_color(step, MIN_PATH_BRIGHTNESS);
					draw->AddPolyline(points.data(), (int)points.size(), ImColor(old_color.r, old_color.g, old_color.b), ImDrawFlags_None, width);
				}
				points.clear();
				points.push_back(joint);
				step = segment_step;
				const MapColor color = map_colors::step_color(step, MIN_PATH_BRIGHTNESS);
				draw->AddCircleFilled(joint, width * 0.5f, ImColor(color.r, color.g, color.b), 12);
			}
			points.push_back(ImVec2(origin_x + line.points[i + 1].x, origin_y + line.points[i + 1].y));
		}
		if (step >= 0 && points.size() >= 2) {
			const MapColor color = map_colors::step_color(step, MIN_PATH_BRIGHTNESS);
			draw->AddPolyline(points.data(), (int)points.size(), ImColor(color.r, color.g, color.b), ImDrawFlags_None, width);
		}
		start = end;
	}
}

// Small shapes with a dark outline, so they show on anything.
void draw_diamond(ImDrawList* draw, const ImVec2& center, float radius, ImU32 fill) {
	const ImVec2 corners[4] = {
		ImVec2(center.x, center.y - radius), ImVec2(center.x + radius, center.y),
		ImVec2(center.x, center.y + radius), ImVec2(center.x - radius, center.y) };
	draw->AddConvexPolyFilled(corners, 4, COLOR_MARKER_EDGE);
	const float inner = radius - 2.5f;
	const ImVec2 inside[4] = {
		ImVec2(center.x, center.y - inner), ImVec2(center.x + inner, center.y),
		ImVec2(center.x, center.y + inner), ImVec2(center.x - inner, center.y) };
	draw->AddConvexPolyFilled(inside, 4, fill);
}

void draw_square(ImDrawList* draw, const ImVec2& center, float half, ImU32 fill) {
	draw->AddRectFilled(ImVec2(center.x - half - 2.0f, center.y - half - 2.0f), ImVec2(center.x + half + 2.0f, center.y + half + 2.0f), COLOR_MARKER_EDGE);
	draw->AddRectFilled(ImVec2(center.x - half, center.y - half), ImVec2(center.x + half, center.y + half), fill);
}

void draw_dot(ImDrawList* draw, const ImVec2& center, float radius, ImU32 fill) {
	draw->AddCircleFilled(center, radius + 2.0f, COLOR_MARKER_EDGE, 20);
	draw->AddCircleFilled(center, radius, fill, 20);
}

// The frame on screen: an arrow pointing the way the flight was heading (degrees clockwise from north;
// the map has north up). `alpha` dims it when the frame is not in the telemetry's recording.
void draw_heading_arrow(ImDrawList* draw, const ImVec2& center, float heading_degrees, int alpha) {
	const float radians = heading_degrees * 3.14159265f / 180.0f;
	const float forward_x = sinf(radians);
	const float forward_y = -cosf(radians);
	const float right_x = -forward_y;
	const float right_y = forward_x;
	const ImVec2 tip(center.x + forward_x * 17.0f, center.y + forward_y * 17.0f);
	const ImVec2 left(center.x - forward_x * 11.0f - right_x * 10.0f, center.y - forward_y * 11.0f - right_y * 10.0f);
	const ImVec2 notch(center.x - forward_x * 4.0f, center.y - forward_y * 4.0f);
	const ImVec2 right(center.x - forward_x * 11.0f + right_x * 10.0f, center.y - forward_y * 11.0f + right_y * 10.0f);
	const ImVec2 outline[4] = { tip, right, notch, left };
	const ImU32 edge = (COLOR_MARKER_EDGE & 0x00FFFFFF) | ((ImU32)alpha << 24);
	const ImU32 fill = (COLOR_MARKER & 0x00FFFFFF) | ((ImU32)alpha << 24);
	draw->AddPolyline(outline, 4, edge, ImDrawFlags_Closed, 5.0f);
	draw->AddTriangleFilled(tip, right, notch, fill);
	draw->AddTriangleFilled(tip, notch, left, fill);
	draw->AddCircleFilled(center, 3.0f, edge, 12);
}

void draw_label(ImDrawList* draw, float x, float y, const char* text) {
	const ImVec2 size = ImGui::CalcTextSize(text);
	draw->AddRectFilled(ImVec2(x, y), ImVec2(x + size.x + 8.0f, y + size.y + 4.0f), COLOR_MAP_TEXT_BACK, 3.0f);
	draw->AddText(ImVec2(x + 4.0f, y + 2.0f), COLOR_MAP_TEXT, text);
}

// The place on the timeline of whole files of a moment in a file (0 if the file isn't there).
int64_t timeline_ticks_for(const EditModel& model, const FileTime& moment) {
	for (int i = 0; i < model.edit_list().count(); i++) {
		if (model.edit_list().clip(i).media == moment.media)
			return model.edit_list().timeline_for(i, moment.local_ticks);
	}
	return 0;
}

}  // namespace

SyncView::SyncView() : m_active(false), m_timeline("SyncTimeline"), m_follow(false), m_last_sample(0.0), m_ahead_x(0.0f), m_ahead_y(0.0f), m_trace_position_version(0), m_trace_climb_version(0), m_fit_pending(true),
		m_map_pressed(false), m_map_dragging(false), m_press_x(0.0f), m_press_y(0.0f), m_drag_x(0.0f), m_drag_y(0.0f),
		m_gesture(GESTURE_PAN), m_slide_sample(0.0), m_cycle_valid(false), m_cycle_x(0.0f), m_cycle_y(0.0f), m_cycle_reference(0.0),
		m_cycle_index(0), m_cycle_count(0), m_change_time(0.0), m_change_was_nudge(false), m_slide_group(false), m_slide_group_started(false),
		m_entry_playhead(0) {
}

bool SyncView::enter(EditModel* model, MediaContainerMgr* media) {
	if (m_active || model->edit_list().count() == 0)
		return false;
	EnvConfig::instance->pause();
	m_entry_playhead = media->current_ticks();
	const FileTime on_screen = model->edit_list().source_for(m_entry_playhead);

	m_entry_order = model->capture().order();
	m_entry_anchors = model->anchors();
	m_entry_launch_mark = model->launch_mark();
	m_entry_model = *model;

	model->begin_source_view();
	// The same moment of the same file, on the timeline of whole files.
	const int64_t show_ticks = timeline_ticks_for(*model, on_screen);
	if (!media->set_clips(model->edit_list().clips(), show_ticks)) {
		model->end_source_view();
		return false;
	}
	m_timeline.fit();
	// The map starts out showing the whole flight, and follows the frame on screen once it nears the edge.
	m_trace.clear();
	m_follow = false;
	m_ahead_x = 0.0f;
	m_ahead_y = 0.0f;
	m_fit_pending = true;
	m_map_pressed = false;
	m_map_dragging = false;
	m_gesture = GESTURE_PAN;
	m_cycle_valid = false;
	m_undo.clear();
	m_redo.clear();
	m_known = snapshot(*model);
	m_change_time = 0.0;
	m_change_was_nudge = false;
	m_slide_group = false;
	m_slide_group_started = false;
	m_active = true;
	return true;
}

void SyncView::leave(bool keep_changes, EditModel* model, MediaContainerMgr* media) {
	if (!m_active)
		return;
	EnvConfig::instance->pause();
	if (!keep_changes) {
		model->set_capture_order(m_entry_order);
		model->set_anchors(m_entry_anchors);
		model->set_launch_mark(m_entry_launch_mark);
	}
	model->end_source_view();
	// The edit was not touched in here, so where the playhead was is still a place on it.
	media->set_clips(model->edit_list().clips(), m_entry_playhead);
	m_undo.clear();
	m_redo.clear();
	m_active = false;
}

SyncView::SyncState SyncView::snapshot(const EditModel& model) {
	SyncState state;
	state.order = model.capture().order();
	state.anchors = model.anchors();
	state.launch_mark = model.launch_mark();
	return state;
}

void SyncView::record_change(const EditModel& model, bool nudge) {
	const SyncState now = snapshot(model);
	if (now == m_known)
		return;
	const double time = ImGui::GetTime();
	if (m_slide_group) {
		// All of a slide is one step; what it started from is kept the first time it changes anything.
		if (!m_slide_group_started) {
			m_undo.push_back(m_known);
			m_slide_group_started = true;
			m_redo.clear();
		}
	} else if (nudge && m_change_was_nudge && !m_undo.empty() && time - m_change_time < NUDGE_RUN_SECONDS) {
		// Another nudge close behind the last one: the step already kept covers it.
	} else {
		m_undo.push_back(m_known);
		m_redo.clear();
		m_change_was_nudge = nudge;
	}
	if (m_undo.size() > MAX_HISTORY)
		m_undo.erase(m_undo.begin());
	m_change_time = time;
	m_known = now;
	m_cycle_valid = false;
}

void SyncView::apply_state(const SyncState& state, EditModel* model, MediaContainerMgr* media) {
	if (state.order != model->capture().order()) {
		// The picture on screen stays where it is, as when the files are moved by hand.
		const FileTime on_screen = model->edit_list().source_for(media->current_ticks());
		if (model->set_capture_order(state.order)) {
			model->refresh_source_view();
			EnvConfig::instance->pause();
			media->set_clips(model->edit_list().clips(), timeline_ticks_for(*model, on_screen));
		}
	}
	model->set_anchors(state.anchors);
	model->set_launch_mark(state.launch_mark);
}

bool SyncView::undo(EditModel* model, MediaContainerMgr* media) {
	if (!m_active || m_undo.empty() || media->recording())
		return false;
	// (Anything changed since the last look, such as a nudge, is kept first, so it can be undone too.)
	record_change(*model, false);
	const SyncState target = m_undo.back();
	m_undo.pop_back();
	m_redo.push_back(m_known);
	apply_state(target, model, media);
	m_known = snapshot(*model);
	m_change_was_nudge = false;
	m_cycle_valid = false;
	return true;
}

bool SyncView::redo(EditModel* model, MediaContainerMgr* media) {
	if (!m_active || m_redo.empty() || media->recording())
		return false;
	const SyncState target = m_redo.back();
	m_redo.pop_back();
	m_undo.push_back(m_known);
	apply_state(target, model, media);
	m_known = snapshot(*model);
	m_change_was_nudge = false;
	m_cycle_valid = false;
	return true;
}

bool SyncView::confirm_done(const EditModel& model) const {
	if (!m_active || !m_entry_model.edit_started())
		return true;
	// What the edit would be, with the real clips back, against what it was.
	EditModel after = model;
	after.end_source_view();
	const int changed = EditModel::clips_with_changed_telemetry(m_entry_model, after);
	if (changed == 0)
		return true;
	return ffsw::confirm_box("CamBot - Sync / Reorder",
		std::to_string(changed) + (changed == 1 ? " clip" : " clips") + " in your edit will show different telemetry after this change. "
		"The footage stays as it is, but the telemetry under it moves.\n\nKeep the change? (Cancel in the Sync / Reorder bar puts everything back.)");
}

void SyncView::reorder_files(int from, int to, EditModel* model, MediaContainerMgr* media) {
	if (media->recording())
		return;
	// Each clip of the source view is a whole file, so moving a clip is moving a file in the order.
	const timeline_edit::Result moved = timeline_edit::move_clip(model->edit_list(), from, to, media->current_ticks());
	if (!moved.ok)
		return;
	std::vector<int> order;
	for (size_t i = 0; i < moved.clips.size(); i++)
		order.push_back(moved.clips[i].media);
	if (!model->set_capture_order(order))
		return;
	model->refresh_source_view();
	EnvConfig::instance->pause();
	media->set_clips(model->edit_list().clips(), moved.playhead_ticks);
	record_change(*model, false);
}

SyncView::Outcome SyncView::draw(const SyncLayout& layout, EditModel& model, MediaContainerMgr& media, bool has_telemetry) {
	Outcome outcome = STAY;
	// Something outside, the nudge keys, may have changed the sync since the last frame.
	record_change(model, true);
	draw_header(layout.header, &outcome);
	draw_map_pane(layout.map, model, media, has_telemetry);
	draw_strip(layout.strip, model, media);
	ReorderRequest reorder;
	m_timeline.draw(layout.timeline, model, media, nullptr, &reorder);
	if (reorder.requested)
		reorder_files(reorder.from, reorder.to, &model, &media);
	return outcome;
}

void SyncView::draw_header(const LayoutRect& rect, Outcome* outcome) {
	if (rect.w < 1.0f || rect.h < 1.0f)
		return;
	ImGui::SetNextWindowPos(ImVec2(rect.x, rect.y));
	ImGui::SetNextWindowSize(ImVec2(rect.w, rect.h));
	if (ImGui::Begin("SyncHeader", nullptr, bar_flags())) {
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Sync / Reorder");
		ImGui::SameLine(0.0f, 24.0f);
		if (ImGui::Button("Done"))
			*outcome = DONE;
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Keep what was changed here and go back to the edit");
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
			*outcome = CANCEL;
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Put the sync and the order of the files back as they were, and go back to the edit");
		const char* hint = "All the footage is shown, as it was recorded. Your edit is kept and comes back afterward.";
		ImGui::SameLine(0.0f, 24.0f);
		if (ImGui::CalcTextSize(hint).x < ImGui::GetContentRegionAvail().x)
			ImGui::TextDisabled("%s", hint);
	}
	ImGui::End();
}

void SyncView::update_trace() {
	const TelemetryMgr* telemetry = TelemetryMgr::instance;
	if (telemetry == nullptr)
		return;
	// Built from the positions smoothed for drawing, and the climb rates; both are done over when the
	// telemetry smooths its positions (once, when it has all been read) or the climb filter is changed.
	if (m_trace_position_version != telemetry->position_version() || m_trace_climb_version != telemetry->climb_version()) {
		m_trace.clear();
		m_trace_position_version = telemetry->position_version();
		m_trace_climb_version = telemetry->climb_version();
	}
	const int available = (int)telemetry->size();
	while (m_trace.count() < available) {
		const TelemetrySlice& slice = (*telemetry)[m_trace.count()];
		m_trace.add(slice.m_draw_lat, slice.m_draw_lon, slice.m_climb_rate[1]);
	}
}

bool SyncView::pick_candidate(float x, float y, double current_sample, double* sample, int* index, int* count, double* reference) const {
	std::vector<MapPick> passes;
	m_trace.find_passes(m_map, x, y, PICK_RADIUS, &passes);
	if (passes.empty())
		return false;
	const int total = (int)passes.size();
	const float dx = x - m_cycle_x;
	const float dy = y - m_cycle_y;
	const bool continuing = m_cycle_valid && dx * dx + dy * dy <= CYCLE_SLOP * CYCLE_SLOP && total == m_cycle_count;
	*reference = continuing ? m_cycle_reference : current_sample;
	// Nearest in time to the reference first (there are only ever a few).
	for (int i = 0; i < total; i++) {
		int best = i;
		for (int j = i + 1; j < total; j++) {
			if (fabs(passes[j].sample - *reference) < fabs(passes[best].sample - *reference))
				best = j;
		}
		const MapPick kept = passes[i];
		passes[i] = passes[best];
		passes[best] = kept;
	}
	*index = continuing ? (m_cycle_index + 1) % total : 0;
	*sample = passes[*index].sample;
	*count = total;
	return true;
}

void SyncView::apply_pick(double sample, EditModel* model, MediaContainerMgr* media) {
	// The frame on screen is this moment of the footage, and the telemetry says it is the point picked.
	const FileTime frame = model->edit_list().source_for(media->current_ticks());
	std::vector<SyncAnchor> anchors;
	anchors.push_back(SyncAnchor(frame, (float)(sample / (double)TELEMETRY_FREQUENCY)));
	model->set_anchors(anchors);
	EnvConfig::instance->pause();
	// The user is working on the map: it stays where they have it until the frame on screen next nears an edge.
	m_follow = false;
	m_fit_pending = false;
	// (It is the user who moved the frame on screen, so that is not a reason to start following it.)
	m_last_sample = sample;
	record_change(*model, false);
}

void SyncView::draw_map_pane(const LayoutRect& rect, EditModel& model, MediaContainerMgr& media, bool has_telemetry) {
	if (rect.w < 1.0f || rect.h < 1.0f)
		return;
	ImGui::SetNextWindowPos(ImVec2(rect.x, rect.y));
	ImGui::SetNextWindowSize(ImVec2(rect.w, rect.h));
	// The map goes right up to the edges of its pane.
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	const bool open = ImGui::Begin("SyncMap", nullptr, bar_flags());
	ImGui::PopStyleVar(2);
	if (open)
		draw_map_contents(rect, model, media, has_telemetry);
	ImGui::End();
}

void SyncView::draw_map_contents(const LayoutRect& rect, EditModel& model, MediaContainerMgr& media, bool has_telemetry) {
	TelemetryMgr* telemetry = TelemetryMgr::instance;
	update_trace();
	const bool have_trace = has_telemetry && telemetry != nullptr && m_trace.count() > 0;
	m_map.set_size(rect.w, rect.h);
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const ImGuiIO& io = ImGui::GetIO();
	draw->AddRectFilled(ImVec2(rect.x, rect.y), ImVec2(rect.x + rect.w, rect.y + rect.h), COLOR_MAP_BACKGROUND);

	// Where the frame on screen is, in the telemetry.
	const FileTime moment = model.edit_list().source_for(media.current_ticks());
	const double flight = model.flight_seconds(moment);
	const double telemetry_time = model.telemetry_seconds(flight);
	const double sample = telemetry_time * (double)TELEMETRY_FREQUENCY;
	MapPoint marker;
	bool have_marker = false;
	bool in_recording = false;
	float heading = 0.0f;
	float speed_knots = 0.0f;
	if (have_trace) {
		have_marker = m_trace.position_at(sample, &marker);
		in_recording = sample >= 0.0 && sample <= (double)(m_trace.count() - 1);
		int index = (int)floor(sample);
		if (index < 0)
			index = 0;
		if (index > m_trace.count() - 1)
			index = m_trace.count() - 1;
		speed_knots = (*telemetry)[index].m_speed_kts;
		// The heading is the direction the path is going at that moment, so the arrow always points along
		// the line it is on. (The course the GPS reports is already averaged over a second or so by the
		// receiver, and in a quick turn it trails the real direction by up to a few tenths of a second.) It is
		// the logged course only where the path doesn't say, as when standing still.
		if (!m_trace.heading_at(sample, &heading))
			heading = (*telemetry)[index].m_course_deg;
	}

	// Which way the flight has been heading, averaged over the last couple of seconds. The map keeps the
	// frame on screen toward the other side of the pane, to show more of what is ahead of it. Averaging the
	// heading as a vector, not an angle, means a quick turn back on itself (a wingover) cancels out and the
	// map hardly moves for it.
	if (have_marker && in_recording && speed_knots >= MIN_SPEED_FOR_HEADING) {
		const float radians = heading * 3.14159265f / 180.0f;
		const float blend = 1.0f - expf(-io.DeltaTime / AHEAD_SECONDS);
		m_ahead_x += (sinf(radians) - m_ahead_x) * blend;
		m_ahead_y += (-cosf(radians) - m_ahead_y) * blend;
	}

	// What part of the world is showing: all of the flight to begin with, then wherever the frame on screen is.
	MapPoint low;
	MapPoint high;
	if (m_fit_pending && have_trace && m_trace.bounds(&low, &high)) {
		m_map.fit(low, high, FIT_MARGIN);
		if (telemetry->parsing_done())
			m_fit_pending = false;
	}
	// Not following (the whole flight was fitted, or the map was moved by hand): the frame on screen is left
	// alone until it moves, in the video, up to an edge of the map that is in view. Then following starts.
	const bool frame_moved = have_marker && fabs(sample - m_last_sample) > 0.01;
	m_last_sample = sample;
	if (!m_follow && frame_moved && !m_map_dragging && !m_fit_pending && have_marker && !m_map.in_inset(marker, RESUME_INSET) && m_map.in_view(marker, 0.0f))
		m_follow = true;
	if (m_follow && have_marker && !m_map_dragging && !m_fit_pending)
		m_map.keep_in_view(marker, FOLLOW_INSET, -m_ahead_x * LOOKAHEAD * rect.w, -m_ahead_y * LOOKAHEAD * rect.h);

	// The mouse. The buttons drawn later in the corner are over this and get the clicks that are on them.
	// A drag moves the map; a click on the path puts the frame on screen at that point of it (see
	// pick_candidate); Ctrl+drag does that and goes on sliding it along the path under the mouse.
	const float mouse_x = io.MousePos.x - rect.x;
	const float mouse_y = io.MousePos.y - rect.y;
	ImGui::SetCursorScreenPos(ImVec2(rect.x, rect.y));
	ImGui::SetNextItemAllowOverlap();
	ImGui::InvisibleButton("##map", ImVec2(rect.w, rect.h), ImGuiButtonFlags_MouseButtonLeft);
	const bool hovered = ImGui::IsItemHovered();
	if (ImGui::IsItemActivated()) {
		m_map_pressed = true;
		m_map_dragging = false;
		m_gesture = GESTURE_PAN;
		m_press_x = io.MousePos.x;
		m_press_y = io.MousePos.y;
		m_drag_x = m_press_x;
		m_drag_y = m_press_y;
		if (io.KeyCtrl) {
			// Pick a point on the path now, then slide it. Away from the path, Ctrl+drag does nothing.
			double picked;
			int index;
			int count;
			double reference;
			if (have_trace && pick_candidate(mouse_x, mouse_y, sample, &picked, &index, &count, &reference)) {
				m_gesture = GESTURE_SLIDE;
				m_slide_sample = picked;
				m_slide_group = true;
				m_slide_group_started = false;
				apply_pick(picked, &model, &media);
				m_cycle_valid = true;
				m_cycle_x = mouse_x;
				m_cycle_y = mouse_y;
				m_cycle_reference = reference;
				m_cycle_index = index;
				m_cycle_count = count;
			} else {
				m_gesture = GESTURE_NONE;
			}
		}
	}
	if (m_map_pressed && ImGui::IsItemActive()) {
		if (m_gesture == GESTURE_SLIDE) {
			// Along the same pass: the nearest point of it to the mouse, near where it was.
			MapPick nearest;
			if (m_trace.nearest_near(m_map, mouse_x, mouse_y, m_slide_sample, SLIDE_WINDOW_SAMPLES, &nearest)) {
				m_slide_sample = nearest.sample;
				apply_pick(nearest.sample, &model, &media);
			}
		} else if (m_gesture == GESTURE_PAN) {
			if (!m_map_dragging) {
				const float dx = io.MousePos.x - m_press_x;
				const float dy = io.MousePos.y - m_press_y;
				m_map_dragging = dx * dx + dy * dy > CLICK_SLOP * CLICK_SLOP;
			}
			if (m_map_dragging) {
				m_map.pan_pixels(io.MousePos.x - m_drag_x, io.MousePos.y - m_drag_y);
				m_drag_x = io.MousePos.x;
				m_drag_y = io.MousePos.y;
				// Moving the map by hand means the user wants to look somewhere else.
				m_follow = false;
				m_fit_pending = false;
				m_cycle_valid = false;
			}
		}
	}
	if (ImGui::IsItemDeactivated()) {
		if (m_gesture == GESTURE_SLIDE) {
			m_slide_group = false;
		} else if (m_gesture == GESTURE_PAN && !m_map_dragging && have_trace) {
			// A click that didn't move the map.
			double picked;
			int index;
			int count;
			double reference;
			if (pick_candidate(mouse_x, mouse_y, sample, &picked, &index, &count, &reference)) {
				apply_pick(picked, &model, &media);
				m_cycle_valid = true;
				m_cycle_x = mouse_x;
				m_cycle_y = mouse_y;
				m_cycle_reference = reference;
				m_cycle_index = index;
				m_cycle_count = count;
			}
		}
		m_map_pressed = false;
		m_map_dragging = false;
		m_gesture = GESTURE_PAN;
	}
	if (hovered && io.MouseWheel != 0.0f) {
		m_map.zoom_about(mouse_x, mouse_y, ZOOM_LEVELS_PER_NOTCH * timeline_zoom::notches((double)io.MouseWheel));
		m_fit_pending = false;
		m_cycle_valid = false;
	}

	// The path, with a dark edge under the colors.
	if (have_trace) {
		MapPolyline line;
		m_trace.build_polyline(0, m_trace.count() - 1, m_map, PATH_SPACING, PATH_MARGIN, &line);
		draw_path(draw, line, rect.x, rect.y, COLOR_PATH_EDGE, PATH_WIDTH + EDGE_EXTRA);
		draw_climb_path(draw, line, rect.x, rect.y, EnvConfig::instance->climb_color_range(), PATH_WIDTH);

		// Where the recording starts (a dot) and ends (a square), and where the activity starts (a diamond).
		int first_fix = 0;
		while (first_fix < m_trace.count() && !m_trace.valid(first_fix))
			first_fix++;
		int last_fix = m_trace.count() - 1;
		while (last_fix > first_fix && !m_trace.valid(last_fix))
			last_fix--;
		if (first_fix < m_trace.count()) {
			const ScreenPoint start = m_map.to_screen(m_trace.point(first_fix));
			const ScreenPoint end = m_map.to_screen(m_trace.point(last_fix));
			draw_dot(draw, ImVec2(rect.x + start.x, rect.y + start.y), 5.0f, COLOR_MARK_LIGHT);
			draw_square(draw, ImVec2(rect.x + end.x, rect.y + end.y), 4.0f, COLOR_MARK_GRAY);
		}
		const double launch_sample = model.telemetry_seconds(model.launch_flight_seconds()) * (double)TELEMETRY_FREQUENCY;
		MapPoint launch;
		if (launch_sample >= 0.0 && launch_sample <= (double)(m_trace.count() - 1) && m_trace.position_at(launch_sample, &launch)) {
			const ScreenPoint s = m_map.to_screen(launch);
			draw_diamond(draw, ImVec2(rect.x + s.x, rect.y + s.y), 9.0f, COLOR_MARK_LIGHT);
		}
		if (have_marker) {
			const ScreenPoint s = m_map.to_screen(marker);
			draw_heading_arrow(draw, ImVec2(rect.x + s.x, rect.y + s.y), heading, in_recording ? 255 : 110);
		}

		// Where a click would put the frame on screen: a ring on that point of the path. (Where several
		// passes of the path are under the mouse, which of them, of how many.)
		if (hovered && !m_map_pressed) {
			double pick_sample;
			int pick_index;
			int pick_count;
			double reference;
			MapPoint at;
			if (pick_candidate(mouse_x, mouse_y, sample, &pick_sample, &pick_index, &pick_count, &reference) && m_trace.position_at(pick_sample, &at)) {
				const ScreenPoint s = m_map.to_screen(at);
				const ImVec2 center(rect.x + s.x, rect.y + s.y);
				draw->AddCircle(center, 11.0f, COLOR_MARKER_EDGE, 24, 5.0f);
				draw->AddCircle(center, 11.0f, COLOR_MARK_LIGHT, 24, 2.5f);
				ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
				if (pick_count > 1) {
					char which[32];
					snprintf(which, sizeof(which), "%d of %d", pick_index + 1, pick_count);
					draw_label(draw, io.MousePos.x + 16.0f, io.MousePos.y + 16.0f, which);
				}
			}
		}
	}

	// What is shown, and what to do.
	char line_one[512];
	snprintf(line_one, sizeof(line_one), "%s  (file %d of %d)  %s", fileio::base_name(model.media_path(moment.media)).c_str(),
		moment.media + 1, model.media_count(), format_seconds(model.ticks_to_seconds(moment.local_ticks)).c_str());
	draw_label(draw, rect.x + 6.0f, rect.y + 6.0f, line_one);
	float next_y = rect.y + 6.0f + ImGui::GetTextLineHeight() + 8.0f;
	if (has_telemetry) {
		char line_two[256];
		snprintf(line_two, sizeof(line_two), "Flight %s   Telemetry %s", format_seconds(flight).c_str(), format_seconds(telemetry_time).c_str());
		draw_label(draw, rect.x + 6.0f, next_y, line_two);
	}
	const char* status = nullptr;
	if (!has_telemetry)
		status = "This project has no telemetry, so there is no path to show.";
	else if (m_trace.count() == 0)
		status = "Reading the telemetry...";
	else if (!telemetry->parsing_done())
		status = "Reading the telemetry...";
	if (status != nullptr) {
		const ImVec2 size = ImGui::CalcTextSize(status);
		const bool centered = !have_trace;
		draw_label(draw, rect.x + (centered ? (rect.w - size.x) * 0.5f : 6.0f),
			centered ? rect.y + rect.h * 0.5f : rect.y + rect.h - size.y - 12.0f, status);
	}
	if (have_trace && !in_recording)
		draw_label(draw, rect.x + 6.0f, rect.y + rect.h - 2.0f * ImGui::GetTextLineHeight() - 24.0f,
			"The frame on screen is outside the telemetry's recording.");
	if (have_trace) {
		const char* hint = "Wheel to zoom, drag to move the map";
		const ImVec2 size = ImGui::CalcTextSize(hint);
		draw_label(draw, rect.x + rect.w - size.x - 14.0f, rect.y + rect.h - size.y - 12.0f, hint);
	}

	// Buttons in the top right corner.
	if (have_trace) {
		const float button_width = 60.0f;
		ImGui::SetCursorScreenPos(ImVec2(rect.x + rect.w - 2.0f * button_width - 14.0f, rect.y + 6.0f));
		if (ImGui::Button("Fit", ImVec2(button_width, 0.0f)) && m_trace.bounds(&low, &high)) {
			m_map.fit(low, high, FIT_MARGIN);
			m_fit_pending = false;
			m_follow = false;
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Show the whole flight");
		ImGui::SameLine(0.0f, 4.0f);
		// Lit while the map is following the frame on screen.
		// (Whether it was lit is settled before the click, which can turn following on.)
		const bool lit = m_follow;
		if (lit)
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
		if (ImGui::Button("Center", ImVec2(button_width, 0.0f))) {
			m_follow = true;
			m_fit_pending = false;
			if (have_marker)
				m_map.center_on(marker);
		}
		if (lit)
			ImGui::PopStyleColor();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Center on the frame on screen and keep it in sight as the video plays");
	}
}

void SyncView::draw_strip(const LayoutRect& rect, EditModel& model, const MediaContainerMgr& media) {
	if (rect.w < 1.0f || rect.h < 1.0f)
		return;
	ImGui::SetNextWindowPos(ImVec2(rect.x, rect.y));
	ImGui::SetNextWindowSize(ImVec2(rect.w, rect.h));
	if (ImGui::Begin("SyncStrip", nullptr, bar_flags())) {
		const float width = ImGui::GetContentRegionAvail().x;
		if (ImGui::Button("Set Activity\nStart", ImVec2(width, 56.0f))) {
			model.set_launch_mark(model.edit_list().source_for(media.current_ticks()));
			record_change(model, false);
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Make the frame on screen the start of the activity: 0:00 on the clock in the video");
		ImGui::Spacing();
		ImGui::TextDisabled("Starts at");
		ImGui::TextUnformatted(format_seconds(model.launch_flight_seconds()).c_str());
	}
	ImGui::End();
}
