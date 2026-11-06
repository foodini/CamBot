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
#include "text_lanes.h"
#include "timeline_editor.h"

namespace {

const float  RULER_HEIGHT = 22.0f;
const float  TRACK_GAP = 4.0f;                 // Between the ruler and the clips.
const float  TRACK_BOTTOM_MARGIN = 6.0f;
const float  MIN_TRACK_HEIGHT = 24.0f;
const float  MAX_TRACK_HEIGHT = 120.0f;
const float  CLIP_ROUNDING = 6.0f;             // The corners of a clip.
const float  CLIP_BORDER_WIDTH = 2.0f;
const float  LANE_GAP = 3.0f;                  // Between the clips and the first row of text boxes, and between rows.
const float  LANE_PADDING = 3.0f;              // Inside a text box's block, round its words.
const float  TEXT_EDGE_GRAB = 8.0f;            // How near an end of a text box's block the mouse must be to take hold of the end.
const float  TEXT_ROUNDING = 4.0f;
const float  PREFERRED_TRACK_HEIGHT = 56.0f;   // The least the clips get when a new row of text boxes makes the timeline grow.
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
const ImU32 COLOR_SPEED_PLATE = IM_COL32(236, 168, 48, 255);       // Behind a clip's speed when it isn't normal.
const ImU32 COLOR_SPEED_TEXT = IM_COL32(20, 14, 0, 255);
const ImU32 COLOR_NAME_PLATE = IM_COL32(0, 0, 0, 150);             // Under a file's name, so it can be read over any picture.
const ImU32 COLOR_TEXT_BLOCK = IM_COL32(58, 64, 92, 255);
const ImU32 COLOR_TEXT_BORDER = IM_COL32(150, 160, 205, 255);
const ImU32 COLOR_TEXT_SELECTED = IM_COL32(255, 255, 255, 255);
const ImU32 COLOR_TEXT_WORDS = IM_COL32(235, 238, 250, 255);
const ImU32 COLOR_TEXT_FADE = IM_COL32(0, 0, 0, 80);               // Darkens the stretches where the words fade in and out.
const ImU32 COLOR_TEXT_GRAB = IM_COL32(255, 255, 255, 90);         // Shows the ends and the body that Ctrl and the mouse can take.
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

// Which part of a text box's block the mouse is on, from its left and right ends in pixels: one of the ends if within a few
// pixels of it (less on a block too narrow to spare them), otherwise the body.
int text_part_at(float x, float left, float right) {
	const float grab = (right - left) / 3.0f < TEXT_EDGE_GRAB ? (right - left) / 3.0f : TEXT_EDGE_GRAB;
	if (x < left + grab)
		return text_lanes::LEFT_END;
	if (x > right - grab)
		return text_lanes::RIGHT_END;
	return text_lanes::BODY;
}

// The first line of `text`, or the second (line 1) if `line` is 1; empty if there isn't one.
std::string line_of(const std::string& text, int line) {
	size_t begin = 0;
	for (int n = 0; n < line; n++) {
		begin = text.find('\n', begin);
		if (begin == std::string::npos)
			return std::string();
		begin++;
	}
	const size_t end = text.find('\n', begin);
	return text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
}

float larger(float a, float b) { return a > b ? a : b; }
float smaller(float a, float b) { return a < b ? a : b; }

}  // namespace

TimelineView::TimelineView(const char* window_name) :
	m_window_name(window_name), m_thumbnails(nullptr), m_panning(false), m_scrubbing(false), m_scrub_ticks(-1), m_drag_clip(-1), m_drag_to(-1),
	m_text_drag_id(0), m_text_drag_part(0), m_text_drag_original(), m_text_drag_span(), m_text_drag_origin_x(0.0f),
	m_previous_lane_count(0), m_height_request(0.0f),
	m_scanning(false), m_scan_origin_x(0.0f), m_scan_accumulator(0.0f), m_last_playhead(-1), m_follow(true)
{
}

void TimelineView::draw(const LayoutRect& rect, EditModel& model, MediaContainerMgr& media, TimelineEditor* editor, ReorderRequest* reorder,
		int* selected_text) {
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
	const char* hint = editor != nullptr
		? "Scroll to zoom.  Drag the ruler, middle-drag or Shift+scroll to pan.  Right-drag to scan.  Ctrl+drag a clip to move it, or a text box (or its ends) to retime it."
		: "Scroll to zoom.  Drag the ruler, middle-drag or Shift+scroll to pan.  Right-drag to scan.  Ctrl+drag a clip to move it.";
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

	// The text track: the text boxes as blocks on rows below the clips (only in the editor's own timeline, and not while the
	// footage is shown whole in the sync view). Worked out here, before the mouse is dealt with, as the mouse needs to know where they are.
	const bool show_text = editor != nullptr && selected_text != nullptr && !model.in_source_view();
	std::vector<int> text_ids;
	std::vector<text_lanes::Span> text_spans;
	std::vector<int> text_lane_of;
	int lane_count = 0;
	if (show_text) {
		const std::vector<TextBox>& boxes = model.text_boxes();
		for (size_t i = 0; i < boxes.size(); i++) {
			text_lanes::Span span;
			model.text_box_span(boxes[i], &span.start, &span.end);
			text_ids.push_back(boxes[i].id);
			text_spans.push_back(span);
		}
		text_lane_of = text_lanes::assign(text_spans, &lane_count);
	}
	const float ruler_bottom = origin.y + RULER_HEIGHT;
	const float track_top = ruler_bottom + TRACK_GAP;
	const float lane_height = 2.0f * ImGui::GetTextLineHeight() + 2.0f * LANE_PADDING;
	// The rows take what they need of the height, short of leaving the clips less than their least; any that don't fit are cut off
	// (the user makes the timeline taller to see them).
	float text_area = (float)lane_count * (lane_height + LANE_GAP);
	const float text_room = larger(0.0f, available.y - RULER_HEIGHT - TRACK_GAP - TRACK_BOTTOM_MARGIN - MIN_TRACK_HEIGHT);
	text_area = smaller(text_area, text_room);
	float track_height = available.y - RULER_HEIGHT - TRACK_GAP - TRACK_BOTTOM_MARGIN - text_area;
	track_height = larger(MIN_TRACK_HEIGHT, smaller(MAX_TRACK_HEIGHT, track_height));
	const float track_bottom = track_top + track_height;
	const float text_top = track_bottom + LANE_GAP;
	// A row of text boxes that is new, and doesn't fit, is a reason to make the timeline taller: otherwise it would appear below
	// the bottom of the window with nothing to show it is there.
	if (show_text) {
		if (lane_count > m_previous_lane_count) {
			const float wanted = (origin.y - rect.y) + RULER_HEIGHT + TRACK_GAP + PREFERRED_TRACK_HEIGHT + TRACK_BOTTOM_MARGIN +
				(float)lane_count * (lane_height + LANE_GAP) + ImGui::GetStyle().WindowPadding.y;
			if (wanted > rect.h)
				m_height_request = wanted;
		}
		m_previous_lane_count = lane_count;
	}
	// The block under the mouse, as an index into text_ids (or -1), and which part of it. A block's ends are where they really
	// are, which may be far off the strip when zoomed in.
	int text_under_mouse = -1;
	int text_part_under_mouse = text_lanes::BODY;
	if (show_text) {
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		for (int i = (int)text_ids.size() - 1; i >= 0 && text_under_mouse < 0; i--) {
			const float top = text_top + (float)text_lane_of[i] * (lane_height + LANE_GAP);
			const float left = origin.x + m_scale.tick_to_x((double)text_spans[i].start);
			const float right = origin.x + m_scale.tick_to_x((double)text_spans[i].end);
			if (mouse.x >= left && mouse.x <= right && mouse.y >= top && mouse.y <= top + lane_height &&
					top + lane_height <= origin.y + available.y) {
				text_under_mouse = i;
				text_part_under_mouse = text_part_at(mouse.x, left, right);
			}
		}
	}

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
			} else if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && io.KeyCtrl && text_under_mouse >= 0) {
				// Ctrl and the button on a text box: take hold of it (or an end of it) to change when it shows.
				m_text_drag_id = text_ids[text_under_mouse];
				m_text_drag_part = text_part_under_mouse;
				m_text_drag_span = text_spans[text_under_mouse];
				m_text_drag_original = *model.find_text_box(m_text_drag_id);
				m_text_drag_origin_x = io.MousePos.x;
				*selected_text = m_text_drag_id;
				editor->begin_box_edit(model, media);
				EnvConfig::instance->pause();
			} else if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && io.KeyCtrl) {
				// Ctrl and the button on a clip: pick it up, if there is somewhere else for it to go.
				const double ticks = m_scale.x_to_ticks(mouse_x);
				if (clips.count() > 1 && ticks >= 0.0 && ticks < (double)clips.total_ticks()) {
					m_drag_clip = clips.clip_index_for((int64_t)ticks);
					m_drag_to = m_drag_clip;
					EnvConfig::instance->pause();
				}
			} else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				if (text_under_mouse >= 0) {
					// A click on a text box selects it and takes the playhead to where it shows in full.
					*selected_text = text_ids[text_under_mouse];
					editor->go_to_text_box(model, &media, *selected_text);
					m_follow = true;
				} else {
					m_scrubbing = true;
					m_follow = true;
					m_scrub_ticks = -1;
					EnvConfig::instance->pause();
				}
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
	// A text box being dragged: it takes the shape the mouse's movement since the pick-up gives it, worked from the box as it was
	// then, and letting go ends the one undo step that began there.
	if (m_text_drag_id != 0) {
		TextBox* dragged = (show_text && !exporting) ? model.find_text_box(m_text_drag_id) : nullptr;
		if (dragged != nullptr && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			const int64_t delta = (int64_t)llround((double)(io.MousePos.x - m_text_drag_origin_x) * m_scale.ticks_per_pixel());
			const text_lanes::Span span = text_lanes::dragged(m_text_drag_span, m_text_drag_part, delta, clips.total_ticks(), media.frame_ticks());
			TextBox updated = m_text_drag_original;
			if (span.start != m_text_drag_span.start)
				updated.anchor = clips.source_for(span.start);     // (An end that stays put keeps its anchor as it was.)
			text_box::set_total_ticks(&updated, span.length());
			editor->set_timing(&model, m_text_drag_id, updated.anchor, updated.fade_in_ticks, updated.dwell_ticks, updated.fade_out_ticks);
			ImGui::SetMouseCursor(m_text_drag_part == text_lanes::BODY ? ImGuiMouseCursor_ResizeAll : ImGuiMouseCursor_ResizeEW);
		} else {
			if (editor != nullptr)
				editor->end_box_edit(model, media);
			m_text_drag_id = 0;
		}
	} else if (text_under_mouse >= 0 && hovered && io.KeyCtrl && !m_scrubbing && !m_scanning && m_drag_clip < 0 && !exporting) {
		ImGui::SetMouseCursor(text_part_under_mouse == text_lanes::BODY ? ImGuiMouseCursor_ResizeAll : ImGuiMouseCursor_ResizeEW);
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
	for (int i = 0; i < clips.count(); i++) {
		const double tint_start = (double)clips.start(i);
		const double tint_end = tint_start + (double)clips.clip(i).timeline_ticks();
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
	// The pictures are as wide as the track is tall, the video's shape. (A video upside down in the preview is upside down here.)
	const float picture_aspect = m_thumbnails != nullptr ? m_thumbnails->aspect() : 16.0f / 9.0f;
	const double picture_width = (double)(track_height * picture_aspect);
	const bool turned_over = cosf(media.rotation_angle()) < 0.0f;
	if (m_thumbnails != nullptr)
		m_thumbnails->begin_frame();
	for (int i = 0; i < clips.count(); i++) {
		const Clip& clip = clips.clip(i);
		const double start = (double)clips.start(i);
		const double end = start + (double)clip.timeline_ticks();
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
				// (The pictures are laid out along the timeline; a clip at another speed covers more or less of its footage per pixel.)
				const int64_t moment = thumbnails::tile_ticks(clip.in_ticks, clip.out_ticks, slot.index, picture_width, m_scale.ticks_per_pixel() * clip.speed);
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
			// A clip that doesn't play at normal speed says how fast it does, at the bottom left.
			if (clip.speed != 1.0 && room >= 24.0f) {
				char speed_text[32];
				snprintf(speed_text, sizeof(speed_text), clip.audio_mode == AUDIO_MUTE ? "%.3gx  muted" : "%.3gx", clip.speed);
				const ImVec2 speed_size = ImGui::CalcTextSize(speed_text);
				const float speed_width = speed_size.x + 10.0f;
				const ImVec2 speed_min(text_left + 5.0f, track_bottom - plate_height - 5.0f);
				if (room >= speed_width) {
					draw->AddRectFilled(speed_min, ImVec2(speed_min.x + speed_width, speed_min.y + plate_height), COLOR_SPEED_PLATE, 3.0f);
					draw->AddText(ImVec2(speed_min.x + 5.0f, speed_min.y + 2.0f), COLOR_SPEED_TEXT, speed_text);
				}
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

	// The text track.
	for (size_t i = 0; i < text_ids.size(); i++) {
		const float top = text_top + (float)text_lane_of[i] * (lane_height + LANE_GAP);
		if (top + lane_height > strip_max.y)
			continue;                      // Cut off for want of room.
		const TextBox* box = model.find_text_box(text_ids[i]);
		if (box == nullptr)
			continue;
		const float true_left = origin.x + m_scale.tick_to_x((double)text_spans[i].start);
		const float true_right = origin.x + m_scale.tick_to_x((double)text_spans[i].end);
		if (true_right < strip_min.x || true_left > strip_max.x)
			continue;
		const float left = larger(true_left, strip_min.x - 4.0f);
		const float right = smaller(true_right, strip_max.x + 4.0f);
		const float bottom = top + lane_height;
		const bool selected = text_ids[i] == *selected_text;
		draw->AddRectFilled(ImVec2(left, top), ImVec2(right, bottom), COLOR_TEXT_BLOCK, TEXT_ROUNDING);
		// Darker where the words are fading in and out.
		const float fade_in_width = (float)((double)box->fade_in_ticks / m_scale.ticks_per_pixel());
		const float fade_out_width = (float)((double)box->fade_out_ticks / m_scale.ticks_per_pixel());
		if (fade_in_width >= 1.0f)
			draw->AddRectFilled(ImVec2(left, top), ImVec2(smaller(true_left + fade_in_width, right), bottom), COLOR_TEXT_FADE, TEXT_ROUNDING, ImDrawFlags_RoundCornersLeft);
		if (fade_out_width >= 1.0f)
			draw->AddRectFilled(ImVec2(larger(true_right - fade_out_width, left), top), ImVec2(right, bottom), COLOR_TEXT_FADE, TEXT_ROUNDING, ImDrawFlags_RoundCornersRight);
		// The start of the words, two lines of them at most, held in view while any of the block is.
		const float words_left = larger(true_left, strip_min.x) + LANE_PADDING + 1.0f;
		const float words_right = smaller(true_right, strip_max.x) - LANE_PADDING;
		if (words_right > words_left) {
			draw->PushClipRect(ImVec2(words_left, top), ImVec2(words_right, bottom), true);
			const float line_height = ImGui::GetTextLineHeight();
			for (int line = 0; line < 2; line++) {
				const std::string words = line_of(box->text, line);
				draw->AddText(ImVec2(words_left, top + LANE_PADDING + (float)line * line_height), COLOR_TEXT_WORDS, words.c_str());
			}
			draw->PopClipRect();
		}
		draw->AddRect(ImVec2(left, top), ImVec2(right, bottom), selected ? COLOR_TEXT_SELECTED : COLOR_TEXT_BORDER, TEXT_ROUNDING, 0, selected ? 2.0f : 1.0f);
		// With Ctrl down over a block, the places the mouse would take hold of light up: the two ends, or all of it.
		if (io.KeyCtrl && (int)i == text_under_mouse && m_text_drag_id == 0 && !m_scrubbing && !m_scanning && m_drag_clip < 0) {
			const float grab = smaller(TEXT_EDGE_GRAB, (true_right - true_left) / 3.0f);
			if (text_part_under_mouse == text_lanes::LEFT_END)
				draw->AddRectFilled(ImVec2(left, top), ImVec2(left + grab, bottom), COLOR_TEXT_GRAB, TEXT_ROUNDING, ImDrawFlags_RoundCornersLeft);
			else if (text_part_under_mouse == text_lanes::RIGHT_END)
				draw->AddRectFilled(ImVec2(right - grab, top), ImVec2(right, bottom), COLOR_TEXT_GRAB, TEXT_ROUNDING, ImDrawFlags_RoundCornersRight);
			else
				draw->AddRectFilled(ImVec2(left + grab, top), ImVec2(right - grab, bottom), COLOR_TEXT_GRAB);
		}
	}

	// The clip the playhead is in, which the edit buttons and keys act on.
	const int current = timeline_edit::current_clip(clips, playhead);
	if (current >= 0) {
		const double start = (double)clips.start(current);
		const double end = start + (double)clips.clip(current).timeline_ticks();
		const float left = larger(origin.x + m_scale.tick_to_x(start), strip_min.x - 4.0f);
		const float right = smaller(origin.x + m_scale.tick_to_x(end), strip_max.x + 4.0f);
		draw->AddRect(ImVec2(left, track_top - 1.0f), ImVec2(right, track_bottom + 1.0f), COLOR_CURRENT_CLIP, CLIP_ROUNDING, 0, 2.0f);
	}

	// A clip being carried: a copy of it follows the mouse, and a mark shows where it would be dropped.
	if (m_drag_clip >= 0 && m_drag_clip < clips.count()) {
		const Clip& dragged = clips.clip(m_drag_clip);
		const float ghost_width = larger(8.0f, (float)((double)dragged.timeline_ticks() / m_scale.ticks_per_pixel()));
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
		draw->AddLine(ImVec2(playhead_x, origin.y), ImVec2(playhead_x, (text_area > 0.0f ? text_top + text_area : track_bottom) + 2.0f), COLOR_PLAYHEAD, 2.0f);
		draw->AddTriangleFilled(ImVec2(playhead_x - 6.0f, origin.y), ImVec2(playhead_x + 6.0f, origin.y), ImVec2(playhead_x, origin.y + 9.0f),
			COLOR_PLAYHEAD);
	}
	draw->PopClipRect();

	ImGui::End();
}
