#include "timeline_view.h"

#include <math.h>
#include <stdio.h>
#include <string>

#include "imgui/imgui.h"

#include "env_config.h"
#include "file_io.h"
#include "media_container_manager.h"
#include "scan_gesture.h"
#include "thumbnail_layout.h"
#include "thumbnail_store.h"
#include "timeline_edit.h"
#include "timeline_editor.h"

namespace {

const float  RULER_HEIGHT = 22.0f;
const float  TRACK_GAP = 4.0f;                 // Between the ruler and the clips.
const float  TRACK_BOTTOM_MARGIN = 6.0f;
const float  MIN_TRACK_HEIGHT = 24.0f;
const float  MAX_TRACK_HEIGHT = 120.0f;
const float  CLIP_ROUNDING = 6.0f;             // The corners of a clip.
const float  CLIP_BORDER_WIDTH = 2.0f;
const float  RULER_TINT_FACTOR = 0.5f;         // The file's color on the ruler is darkened, so the times on it stay easy to read.
const int    MAX_PICTURES_PER_CLIP = 80;
const double FRAME_PIXELS_AT_MAX_ZOOM = 32.0;  // Zoomed in as far as it goes, one frame is this wide.
const double MIN_LABEL_PIXELS = 90.0;          // Least room between labelled marks on the ruler.
const float  ZOOM_PER_WHEEL_NOTCH = 1.25f;
const float  PAN_PIXELS_PER_WHEEL_NOTCH = 80.0f;

const ImU32 COLOR_BACKGROUND = IM_COL32(24, 24, 28, 255);
const ImU32 COLOR_RULER_LINE = IM_COL32(70, 70, 80, 255);
const ImU32 COLOR_RULER_MARK = IM_COL32(120, 120, 135, 255);
const ImU32 COLOR_RULER_TEXT = IM_COL32(190, 190, 205, 255);
const ImU32 COLOR_CLIP_BORDER = IM_COL32(12, 12, 15, 255);
const ImU32 COLOR_CLIP_TEXT = IM_COL32(240, 240, 245, 255);
const ImU32 COLOR_CLIP_BACKDROP = IM_COL32(30, 30, 36, 255);       // Behind the pictures, where one hasn't arrived.
const ImU32 COLOR_NAME_PLATE = IM_COL32(0, 0, 0, 150);             // Under a file's name, so it can be read over any picture.
const ImU32 COLOR_JOIN = IM_COL32(235, 200, 70, 255);
const ImU32 COLOR_PLAYHEAD = IM_COL32(240, 70, 70, 255);
const ImU32 COLOR_CURRENT_CLIP = IM_COL32(245, 245, 250, 255);   // Outlines the clip the playhead is in.
const ImU32 COLOR_DROP_MARK = IM_COL32(255, 255, 255, 255);      // Where a dragged clip would be dropped: as bright as it gets,
const ImU32 COLOR_DROP_GLOW = IM_COL32(235, 245, 255, 255);      // with a pulsing glow, and a dark edge so it shows on pale blocks too.
const ImU32 COLOR_DROP_EDGE = IM_COL32(0, 0, 0, 230);

// The files' colors, chosen to be told apart by people who see red and green poorly: the Okabe-Ito set, whose colors differ
// along the blue-yellow axis and in brightness, which such eyes still see. They go round again past the seventh file, so the
// number on each clip (see capture_number) is what finally tells files apart.
const ImU32 FILE_COLORS[7] = {
	IM_COL32(0, 114, 178, 255),     // Blue.
	IM_COL32(230, 159, 0, 255),     // Orange.
	IM_COL32(86, 180, 233, 255),    // Sky blue.
	IM_COL32(240, 228, 66, 255),    // Yellow.
	IM_COL32(213, 94, 0, 255),      // Vermillion.
	IM_COL32(0, 158, 115, 255),     // Bluish green.
	IM_COL32(204, 121, 167, 255),   // Reddish purple.
};

ImU32 color_for_number(int number) {
	return FILE_COLORS[((number > 0 ? number : 1) - 1) % 7];
}

// Where the file comes in the order the files were recorded, from 1: the number shown on its clips. (In the Sync /
// Reorder view that is the order the files are laid out in, left to right.)
int capture_number(const EditModel& model, int media) {
	const std::vector<int>& order = model.capture().order();
	for (size_t i = 0; i < order.size(); i++) {
		if (order[i] == media)
			return (int)i + 1;
	}
	return media + 1;
}

// Black or white, whichever reads better on `background`.
ImU32 text_color_on(ImU32 background) {
	const int red = (int)((background >> IM_COL32_R_SHIFT) & 0xFF);
	const int green = (int)((background >> IM_COL32_G_SHIFT) & 0xFF);
	const int blue = (int)((background >> IM_COL32_B_SHIFT) & 0xFF);
	return (299 * red + 587 * green + 114 * blue) > 150000 ? IM_COL32(0, 0, 0, 255) : IM_COL32(255, 255, 255, 255);
}

// A button that is greyed out, and does nothing, unless `enabled`; hovering it says what it does.
bool edit_button(const char* label, bool enabled, const char* tooltip) {
	ImGui::BeginDisabled(!enabled);
	bool pressed = ImGui::Button(label);
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("%s", tooltip);
	return pressed && enabled;
}

// `color` with its red, green and blue scaled by `factor` (below 1 is darker), fully opaque.
ImU32 darkened(ImU32 color, float factor) {
	const int red = (int)((float)((color >> IM_COL32_R_SHIFT) & 0xFF) * factor);
	const int green = (int)((float)((color >> IM_COL32_G_SHIFT) & 0xFF) * factor);
	const int blue = (int)((float)((color >> IM_COL32_B_SHIFT) & 0xFF) * factor);
	return IM_COL32(red, green, blue, 255);
}

float larger(float a, float b) { return a > b ? a : b; }
float smaller(float a, float b) { return a < b ? a : b; }

}  // namespace

TimelineView::TimelineView(const char* window_name) :
	m_window_name(window_name), m_thumbnails(nullptr), m_panning(false), m_scrubbing(false), m_scrub_ticks(-1), m_drag_clip(-1), m_drag_to(-1),
	m_scanning(false), m_scan_origin_x(0.0f), m_scan_accumulator(0.0f), m_last_playhead(-1), m_follow(true)
{
}

void TimelineView::draw(const LayoutRect& rect, EditModel& model, MediaContainerMgr& media, TimelineEditor* editor, ReorderRequest* reorder) {
	if (rect.w < 1.0f || rect.h < 1.0f)
		return;
	ImGui::SetNextWindowPos(ImVec2(rect.x, rect.y));
	ImGui::SetNextWindowSize(ImVec2(rect.w, rect.h));
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
	if (!ImGui::Begin(m_window_name.c_str(), nullptr, flags)) {
		ImGui::End();
		return;
	}

	const EditList& clips = model.edit_list();
	const bool exporting = media.recording();

	// The row of labels and buttons above the timeline itself.
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(editor != nullptr ? "Timeline" : "Footage");
	ImGui::SameLine(0.0f, 16.0f);
	if (ImGui::Button("Fit"))
		m_scale.fit();

	// The edits, which act on the clip under the playhead. They are off while an export runs.
	if (editor != nullptr) {
		ImGui::SameLine(0.0f, 16.0f);
		if (edit_button("Split", !exporting && editor->can_split(model, media), "Cut the clip in two at the playhead (S)"))
			editor->split(&model, &media);
		ImGui::SameLine();
		if (edit_button("Trim Left", !exporting && editor->can_trim_left(model, media), "Delete the clip from its start up to the playhead frame ([)"))
			editor->trim_left(&model, &media);
		ImGui::SameLine();
		if (edit_button("Trim Right", !exporting && editor->can_trim_right(model, media), "Delete the clip from after the playhead frame to its end (])"))
			editor->trim_right(&model, &media);
		ImGui::SameLine();
		if (edit_button("Delete", !exporting && editor->can_remove(model, media), "Delete the clip under the playhead (Delete)"))
			editor->remove_clip(&model, &media);
		ImGui::SameLine(0.0f, 16.0f);
		if (edit_button("Undo", editor->can_undo(media), "Undo the last edit (Ctrl+Z)"))
			editor->undo(&model, &media);
		ImGui::SameLine();
		if (edit_button("Redo", editor->can_redo(media), "Redo the edit that was undone (Ctrl+Y)"))
			editor->redo(&model, &media);
	}

	// A reminder of the mouse, if there's room for it.
	const char* hint = "Scroll to zoom.  Drag the ruler, middle-drag or Shift+scroll to pan.  Right-drag to scan.  Ctrl+drag a clip to move it.";
	ImGui::SameLine(0.0f, 16.0f);
	if (ImGui::CalcTextSize(hint).x < ImGui::GetContentRegionAvail().x)
		ImGui::TextDisabled("%s", hint);

	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const ImVec2 available = ImGui::GetContentRegionAvail();
	if (available.x < 8.0f || available.y < RULER_HEIGHT) {
		ImGui::End();
		return;
	}
	m_scale.set_extent(clips.total_ticks(), (double)media.frame_ticks() / FRAME_PIXELS_AT_MAX_ZOOM);
	m_scale.set_width(available.x);

	// One surface over the whole strip takes the mouse; what it means depends on the button.
	ImGui::InvisibleButton("##timeline_surface", available,
		ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
	const bool hovered = ImGui::IsItemHovered();
	const bool activated = ImGui::IsItemActivated();
	const bool active = ImGui::IsItemActive();
	ImGuiIO& io = ImGui::GetIO();
	const float mouse_x = io.MousePos.x - origin.x;

	if (!exporting) {
		// The wheel: zoom about the cursor, or with Shift (or a sideways wheel) move along.
		if (hovered) {
			if (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f)
				m_follow = false;
			if (io.KeyShift) {
				float wheel = io.MouseWheel != 0.0f ? io.MouseWheel : io.MouseWheelH;
				if (wheel != 0.0f)
					m_scale.pan_by_pixels(-wheel * PAN_PIXELS_PER_WHEEL_NOTCH);
			} else {
				if (io.MouseWheel != 0.0f)
					m_scale.zoom_at(mouse_x, powf(ZOOM_PER_WHEEL_NOTCH, (float)timeline_zoom::notches((double)io.MouseWheel)));
				if (io.MouseWheelH != 0.0f)
					m_scale.pan_by_pixels(io.MouseWheelH * PAN_PIXELS_PER_WHEEL_NOTCH);
			}
		}

		// A button going down on the strip starts a gesture, which then runs until it is let go,
		// wherever the mouse has gone by then.
		if (activated) {
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && io.MousePos.y - origin.y < RULER_HEIGHT) {
				// Pressed on the ruler: that slides the timeline instead of moving the playhead.
				m_panning = true;
			} else if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && io.KeyCtrl) {
				// Ctrl and the button on a clip: pick it up, if there is somewhere else for it to go.
				const double ticks = m_scale.x_to_ticks(mouse_x);
				if (clips.count() > 1 && ticks >= 0.0 && ticks < (double)clips.total_ticks()) {
					m_drag_clip = clips.clip_index_for((int64_t)ticks);
					m_drag_to = m_drag_clip;
					EnvConfig::instance->pause();
				}
			} else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				m_scrubbing = true;
				m_follow = true;
				m_scrub_ticks = -1;
				EnvConfig::instance->pause();
			} else if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
				m_scanning = true;
				m_follow = true;
				m_scan_origin_x = io.MousePos.x;
				m_scan_accumulator = 0.0f;
				EnvConfig::instance->pause();
			}
		}
		if (active && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
			m_scale.pan_by_pixels(-io.MouseDelta.x);
			m_follow = false;
		}
		if (m_panning) {
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				m_scale.pan_by_pixels(-io.MouseDelta.x);   // The timeline moves with the mouse, as if grabbed.
				m_follow = false;
			} else
				m_panning = false;
		}
	}
	if (exporting) {
		m_panning = false;
		m_drag_clip = -1;
	}
	// A clip being carried: where it would land follows the mouse, and letting go drops it.
	if (m_drag_clip >= 0) {
		if (m_drag_clip >= clips.count()) {
			m_drag_clip = -1;      // The clips changed under it (an undo, say).
		} else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			m_drag_to = timeline_edit::drop_index(clips, m_drag_clip, m_scale.x_to_ticks(mouse_x));
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		} else {
			if (reorder != nullptr && timeline_edit::can_move(clips, m_drag_clip, m_drag_to)) {
				reorder->requested = true;
				reorder->from = m_drag_clip;
				reorder->to = m_drag_to;
			}
			m_drag_clip = -1;
		}
	}
	if (hovered && io.MousePos.y - origin.y < RULER_HEIGHT && !m_scrubbing && !m_scanning)
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
	if (m_panning)
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

	if (m_scrubbing) {
		if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && !exporting) {
			int64_t ticks = m_scale.x_to_timeline_ticks(mouse_x);
			if (ticks != m_scrub_ticks) {
				media.seek_to_keyframe_near(ticks);
				m_scrub_ticks = ticks;
			}
		} else {
			if (m_scrub_ticks >= 0 && !exporting)
				media.advance_to(m_scrub_ticks);
			m_scrubbing = false;
		}
	}
	if (m_scanning) {
		if (ImGui::IsMouseDown(ImGuiMouseButton_Right) && !exporting) {
			float drag = scan_gesture::drag_for_pixels(io.MousePos.x - m_scan_origin_x);
			m_scan_accumulator += scan_gesture::rate_for_offset(drag, media.frame_rate()) * io.DeltaTime;
			// A frame at a time, whatever the rate (a longer jump is what scrubbing is for).
			if (m_scan_accumulator >= 1.0f) {
				m_scan_accumulator -= 1.0f;
				media.advance_by(1);
			} else if (m_scan_accumulator <= -1.0f) {
				m_scan_accumulator += 1.0f;
				media.rewind_by(1);
			}
		} else {
			m_scanning = false;
		}
	}

	// The playhead, and keeping it in sight as it moves (while playing, say) when zoomed in.
	const int64_t playhead = media.current_ticks();
	if ((double)playhead >= m_scale.view_start() && (double)playhead <= m_scale.view_end())
		m_follow = true;
	if (m_follow && playhead != m_last_playhead)
		m_scale.reveal((double)playhead);
	m_last_playhead = playhead;

	// ---- Drawing ----
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const ImVec2 strip_min = origin;
	const ImVec2 strip_max(origin.x + available.x, origin.y + available.y);
	draw->PushClipRect(strip_min, strip_max, true);
	draw->AddRectFilled(strip_min, strip_max, COLOR_BACKGROUND);

	// The ruler, tinted with the color of the file under each stretch of it, so footage cut up and shuffled about
	// can be told by color alone.
	const float ruler_bottom = origin.y + RULER_HEIGHT;
	for (int i = 0; i < clips.count(); i++) {
		const double tint_start = (double)clips.start(i);
		const double tint_end = tint_start + (double)clips.clip(i).duration_ticks();
		float tint_left = origin.x + m_scale.tick_to_x(tint_start);
		float tint_right = origin.x + m_scale.tick_to_x(tint_end);
		if (tint_right < strip_min.x || tint_left > strip_max.x)
			continue;
		tint_left = larger(tint_left, strip_min.x);
		tint_right = smaller(tint_right, strip_max.x);
		draw->AddRectFilled(ImVec2(tint_left, origin.y), ImVec2(tint_right, ruler_bottom), darkened(color_for_number(capture_number(model, clips.clip(i).media)), RULER_TINT_FACTOR));
	}
	const double seconds_per_tick = model.seconds_per_tick();
	const double step = timeline_ruler::step_seconds(m_scale.ticks_per_pixel() * seconds_per_tick, MIN_LABEL_PIXELS);
	const int64_t first_mark = (int64_t)floor(m_scale.view_start() * seconds_per_tick / step);
	const int64_t last_mark = (int64_t)ceil(m_scale.view_end() * seconds_per_tick / step);
	for (int64_t mark = first_mark; mark <= last_mark; mark++) {
		if (mark < 0)
			continue;
		const double seconds = (double)mark * step;
		const float x = origin.x + m_scale.tick_to_x(seconds / seconds_per_tick);
		draw->AddLine(ImVec2(x, ruler_bottom - 7.0f), ImVec2(x, ruler_bottom), COLOR_RULER_MARK);
		const std::string text = timeline_ruler::label(seconds, step);
		draw->AddText(ImVec2(x + 4.0f, origin.y + 3.0f), COLOR_RULER_TEXT, text.c_str());
	}
	draw->AddLine(ImVec2(strip_min.x, ruler_bottom), ImVec2(strip_max.x, ruler_bottom), COLOR_RULER_LINE);

	// The clips, one block each, with a mark at every join.
	const float track_top = ruler_bottom + TRACK_GAP;
	float track_height = available.y - RULER_HEIGHT - TRACK_GAP - TRACK_BOTTOM_MARGIN;
	track_height = larger(MIN_TRACK_HEIGHT, smaller(MAX_TRACK_HEIGHT, track_height));
	const float track_bottom = track_top + track_height;
	// The pictures are as wide as the track is tall, the video's shape. (A video upside down in the preview is upside down here.)
	const float picture_aspect = m_thumbnails != nullptr ? m_thumbnails->aspect() : 16.0f / 9.0f;
	const double picture_width = (double)(track_height * picture_aspect);
	const bool turned_over = cosf(media.rotation_angle()) < 0.0f;
	if (m_thumbnails != nullptr)
		m_thumbnails->begin_frame();
	for (int i = 0; i < clips.count(); i++) {
		const Clip& clip = clips.clip(i);
		const double start = (double)clips.start(i);
		const double end = start + (double)clip.duration_ticks();
		float left = origin.x + m_scale.tick_to_x(start);
		float right = origin.x + m_scale.tick_to_x(end);
		if (right < strip_min.x || left > strip_max.x)
			continue;
		const bool join_in_view = left >= strip_min.x && i > 0;
		// The ends as they really are (in doubles, as zoomed in far they are a long way off the strip), for laying out the pictures.
		const double true_left = (double)origin.x + (start - m_scale.view_start()) / m_scale.ticks_per_pixel();
		const double true_right = (double)origin.x + (end - m_scale.view_start()) / m_scale.ticks_per_pixel();
		// Coordinates far outside the strip are held just outside it, where the clipping hides them.
		left = larger(left, strip_min.x - 4.0f);
		right = smaller(right, strip_max.x + 4.0f);
		const int number = capture_number(model, clip.media);
		const ImU32 file_color = color_for_number(number);
		draw->AddRectFilled(ImVec2(left, track_top), ImVec2(right, track_bottom), COLOR_CLIP_BACKDROP, CLIP_ROUNDING);

		// The pictures: from the clip's left end, one after another, the last cut short where the clip ends.
		if (m_thumbnails != nullptr) {
			const std::vector<thumbnails::TileSlot> slots = thumbnails::visible_tiles(true_left, true_right, (double)strip_min.x - 1.0,
				(double)strip_max.x + 1.0, picture_width, MAX_PICTURES_PER_CLIP);
			for (size_t t = 0; t < slots.size(); t++) {
				const thumbnails::TileSlot& slot = slots[t];
				ImDrawFlags corners = ImDrawFlags_RoundCornersNone;
				if (slot.index == 0)
					corners |= ImDrawFlags_RoundCornersLeft;
				if (slot.right >= true_right - 0.001)
					corners |= ImDrawFlags_RoundCornersRight;
				const int64_t moment = thumbnails::tile_ticks(clip.in_ticks, clip.out_ticks, slot.index, picture_width, m_scale.ticks_per_pixel());
				// The first picture of a clip is that exact frame: it is the one being cut around.
				const unsigned int texture = m_thumbnails->want(clip.media, moment, slot.index == 0);
				const ImVec2 picture_min((float)slot.left, track_top);
				const ImVec2 picture_max((float)slot.right, track_bottom);
				if (texture == 0)
					continue;   // The backdrop shows.
				const float shown = (float)slot.shown;
				const ImVec2 uv_min = turned_over ? ImVec2(1.0f, 1.0f) : ImVec2(0.0f, 0.0f);
				const ImVec2 uv_max = turned_over ? ImVec2(1.0f - shown, 0.0f) : ImVec2(shown, 1.0f);
				draw->AddImageRounded((ImTextureID)(uintptr_t)texture, picture_min, picture_max, uv_min, uv_max, IM_COL32_WHITE,
					CLIP_ROUNDING, corners);
			}
		}
		if (i == m_drag_clip)
			draw->AddRectFilled(ImVec2(left, track_top), ImVec2(right, track_bottom), IM_COL32(0, 0, 0, 200), CLIP_ROUNDING);   // Nearly black where it was picked up.
		draw->AddRect(ImVec2(left, track_top), ImVec2(right, track_bottom), file_color, CLIP_ROUNDING, 0, CLIP_BORDER_WIDTH);

		// The file's number and name, in the corner of the clip and kept in view while any of the clip is, on a dark plate so
		// it reads over any picture; the number is on a patch of the file's color. A clip too narrow for the whole of it shows
		// just the number (or nothing, when narrower still), and hovering it names the file.
		const float text_left = larger(left, strip_min.x);
		const float text_right = smaller(right, strip_max.x);
		const std::string name = fileio::base_name(model.media_path(clip.media));
		bool name_hidden = true;
		if (text_right - text_left > 24.0f) {
			char number_text[16];
			snprintf(number_text, sizeof(number_text), "%d", number);
			const ImVec2 number_size = ImGui::CalcTextSize(number_text);
			const ImVec2 name_size = ImGui::CalcTextSize(name.c_str());
			const float badge_width = number_size.x + 10.0f;
			const float plate_height = number_size.y + 4.0f;
			const float full_width = badge_width + name_size.x + 10.0f;
			const float room = text_right - text_left - 10.0f;
			const ImVec2 plate_min(text_left + 5.0f, track_top + 5.0f);
			draw->PushClipRect(ImVec2(text_left + 1.0f, track_top), ImVec2(text_right - 1.0f, track_bottom), true);
			if (room >= full_width) {
				draw->AddRectFilled(plate_min, ImVec2(plate_min.x + full_width, plate_min.y + plate_height), COLOR_NAME_PLATE, 3.0f);
				draw->AddText(ImVec2(plate_min.x + badge_width + 5.0f, plate_min.y + 2.0f), COLOR_CLIP_TEXT, name.c_str());
				name_hidden = false;
			}
			if (room >= badge_width) {
				draw->AddRectFilled(plate_min, ImVec2(plate_min.x + badge_width, plate_min.y + plate_height), file_color, 3.0f);
				draw->AddText(ImVec2(plate_min.x + 5.0f, plate_min.y + 2.0f), text_color_on(file_color), number_text);
			}
			draw->PopClipRect();
		}
		if (name_hidden && hovered && !m_scrubbing && !m_scanning && m_drag_clip < 0 && io.MousePos.x >= left && io.MousePos.x <= right &&
				io.MousePos.y >= track_top && io.MousePos.y <= track_bottom)
			ImGui::SetTooltip("%d: %s", number, name.c_str());
		if (join_in_view)
			draw->AddLine(ImVec2(left, origin.y + 2.0f), ImVec2(left, track_bottom), COLOR_JOIN, 2.0f);
	}
	if (m_thumbnails != nullptr)
		m_thumbnails->end_frame();

	// The clip the playhead is in, which the edit buttons and keys act on.
	const int current = timeline_edit::current_clip(clips, playhead);
	if (current >= 0) {
		const double start = (double)clips.start(current);
		const double end = start + (double)clips.clip(current).duration_ticks();
		const float left = larger(origin.x + m_scale.tick_to_x(start), strip_min.x - 4.0f);
		const float right = smaller(origin.x + m_scale.tick_to_x(end), strip_max.x + 4.0f);
		draw->AddRect(ImVec2(left, track_top - 1.0f), ImVec2(right, track_bottom + 1.0f), COLOR_CURRENT_CLIP, CLIP_ROUNDING, 0, 2.0f);
	}

	// A clip being carried: a copy of it follows the mouse, and a mark shows where it would be dropped.
	if (m_drag_clip >= 0 && m_drag_clip < clips.count()) {
		const Clip& dragged = clips.clip(m_drag_clip);
		const float ghost_width = larger(8.0f, (float)((double)dragged.duration_ticks() / m_scale.ticks_per_pixel()));
		const float ghost_left = origin.x + mouse_x - ghost_width / 2.0f;
		draw->AddRectFilled(ImVec2(ghost_left, track_top), ImVec2(ghost_left + ghost_width, track_bottom),
			(color_for_number(capture_number(model, dragged.media)) & 0x00FFFFFF) | IM_COL32(0, 0, 0, 170));
		draw->AddRect(ImVec2(ghost_left, track_top), ImVec2(ghost_left + ghost_width, track_bottom), COLOR_CURRENT_CLIP);
		if (m_drag_to != m_drag_clip) {
			// The drop mark runs the whole height of the strip, well clear of the blocks, with a glow that
			// breathes so it can't be missed.
			const float marker_x = origin.x + m_scale.tick_to_x((double)timeline_edit::drop_marker_ticks(clips, m_drag_clip, m_drag_to));
			const float pulse = 0.5f + 0.5f * sinf((float)ImGui::GetTime() * 7.0f);
			const float top_y = strip_min.y;
			const float bottom_y = strip_max.y;
			draw->AddLine(ImVec2(marker_x, top_y), ImVec2(marker_x, bottom_y), COLOR_DROP_EDGE, 9.0f);
			for (int layer = 3; layer >= 0; layer--) {
				const int alpha = (int)((50.0f + 70.0f * pulse) / (float)(layer + 1));
				const ImU32 glow = (COLOR_DROP_GLOW & 0x00FFFFFF) | ((ImU32)alpha << IM_COL32_A_SHIFT);
				draw->AddLine(ImVec2(marker_x, top_y), ImVec2(marker_x, bottom_y), glow, 5.0f + 6.0f * (float)layer);
			}
			draw->AddLine(ImVec2(marker_x, top_y), ImVec2(marker_x, bottom_y), COLOR_DROP_MARK, 4.0f);
			// Arrowheads at both ends, pointing at the place.
			const float head = 9.0f;
			draw->AddTriangleFilled(ImVec2(marker_x - head, top_y), ImVec2(marker_x + head, top_y), ImVec2(marker_x, top_y + head * 1.4f), COLOR_DROP_EDGE);
			draw->AddTriangleFilled(ImVec2(marker_x - head + 2.0f, top_y), ImVec2(marker_x + head - 2.0f, top_y), ImVec2(marker_x, top_y + head * 1.4f - 3.0f), COLOR_DROP_MARK);
			draw->AddTriangleFilled(ImVec2(marker_x - head, bottom_y), ImVec2(marker_x + head, bottom_y), ImVec2(marker_x, bottom_y - head * 1.4f), COLOR_DROP_EDGE);
			draw->AddTriangleFilled(ImVec2(marker_x - head + 2.0f, bottom_y), ImVec2(marker_x + head - 2.0f, bottom_y), ImVec2(marker_x, bottom_y - head * 1.4f + 3.0f), COLOR_DROP_MARK);
		}
	}

	// The playhead on top of it all.
	const float playhead_x = origin.x + m_scale.tick_to_x((double)playhead);
	if (playhead_x >= strip_min.x - 6.0f && playhead_x <= strip_max.x + 6.0f) {
		draw->AddLine(ImVec2(playhead_x, origin.y), ImVec2(playhead_x, track_bottom + 2.0f), COLOR_PLAYHEAD, 2.0f);
		draw->AddTriangleFilled(ImVec2(playhead_x - 6.0f, origin.y), ImVec2(playhead_x + 6.0f, origin.y), ImVec2(playhead_x, origin.y + 9.0f),
			COLOR_PLAYHEAD);
	}
	draw->PopClipRect();

	ImGui::End();
}
