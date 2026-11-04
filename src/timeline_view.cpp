#include "timeline_view.h"

#include <math.h>
#include <string>

#include "imgui/imgui.h"

#include "env_config.h"
#include "file_io.h"
#include "media_container_manager.h"
#include "scan_gesture.h"
#include "timeline_edit.h"
#include "timeline_editor.h"

namespace {

const float  RULER_HEIGHT = 22.0f;
const float  TRACK_GAP = 4.0f;                 // Between the ruler and the clips.
const float  TRACK_BOTTOM_MARGIN = 6.0f;
const float  MIN_TRACK_HEIGHT = 24.0f;
const float  MAX_TRACK_HEIGHT = 64.0f;
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
const ImU32 COLOR_JOIN = IM_COL32(235, 200, 70, 255);
const ImU32 COLOR_PLAYHEAD = IM_COL32(240, 70, 70, 255);
const ImU32 COLOR_CURRENT_CLIP = IM_COL32(245, 245, 250, 255);   // Outlines the clip the playhead is in.

// Each file gets its own muted color, so footage from the same file looks the same wherever it is.
ImU32 color_for_media(int media) {
	float hue = fmodf((float)media * 0.618034f, 1.0f);
	float red = 0.0f, green = 0.0f, blue = 0.0f;
	ImGui::ColorConvertHSVtoRGB(hue, 0.35f, 0.50f, red, green, blue);
	return IM_COL32((int)(red * 255.0f), (int)(green * 255.0f), (int)(blue * 255.0f), 255);
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

float larger(float a, float b) { return a > b ? a : b; }
float smaller(float a, float b) { return a < b ? a : b; }

}  // namespace

TimelineView::TimelineView() :
	m_panning(false), m_scrubbing(false), m_scrub_ticks(-1), m_scanning(false), m_scan_origin_x(0.0f), m_scan_accumulator(0.0f), m_last_playhead(-1), m_follow(true)
{
}

void TimelineView::draw(const LayoutRect& rect, EditModel& model, MediaContainerMgr& media, TimelineEditor& editor) {
	if (rect.w < 1.0f || rect.h < 1.0f)
		return;
	ImGui::SetNextWindowPos(ImVec2(rect.x, rect.y));
	ImGui::SetNextWindowSize(ImVec2(rect.w, rect.h));
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
	if (!ImGui::Begin("Timeline", nullptr, flags)) {
		ImGui::End();
		return;
	}

	const EditList& clips = model.edit_list();
	const bool exporting = media.recording();

	// The row of labels and buttons above the timeline itself.
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Timeline");
	ImGui::SameLine(0.0f, 16.0f);
	if (ImGui::Button("Fit"))
		m_scale.fit();

	// The edits, which act on the clip under the playhead. They are off while an export runs.
	ImGui::SameLine(0.0f, 16.0f);
	if (edit_button("Split", !exporting && editor.can_split(model, media), "Cut the clip in two at the playhead (S)"))
		editor.split(&model, &media);
	ImGui::SameLine();
	if (edit_button("Trim Left", !exporting && editor.can_trim_left(model, media), "Delete the clip from its start up to the playhead frame ([)"))
		editor.trim_left(&model, &media);
	ImGui::SameLine();
	if (edit_button("Trim Right", !exporting && editor.can_trim_right(model, media), "Delete the clip from after the playhead frame to its end (])"))
		editor.trim_right(&model, &media);
	ImGui::SameLine();
	if (edit_button("Delete", !exporting && editor.can_remove(model, media), "Delete the clip under the playhead (Delete)"))
		editor.remove_clip(&model, &media);
	ImGui::SameLine(0.0f, 16.0f);
	if (edit_button("Undo", editor.can_undo(media), "Undo the last edit (Ctrl+Z)"))
		editor.undo(&model, &media);
	ImGui::SameLine();
	if (edit_button("Redo", editor.can_redo(media), "Redo the edit that was undone (Ctrl+Y)"))
		editor.redo(&model, &media);

	// A reminder of the mouse, if there's room for it.
	const char* hint = "Scroll to zoom.  Drag the ruler, middle-drag or Shift+scroll to pan.  Right-drag to scan.";
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
					m_scale.zoom_at(mouse_x, powf(ZOOM_PER_WHEEL_NOTCH, io.MouseWheel));
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
	if (exporting)
		m_panning = false;
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

	// The ruler.
	const float ruler_bottom = origin.y + RULER_HEIGHT;
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
	for (int i = 0; i < clips.count(); i++) {
		const Clip& clip = clips.clip(i);
		const double start = (double)clips.start(i);
		const double end = start + (double)clip.duration_ticks();
		float left = origin.x + m_scale.tick_to_x(start);
		float right = origin.x + m_scale.tick_to_x(end);
		if (right < strip_min.x || left > strip_max.x)
			continue;
		const bool join_in_view = left >= strip_min.x && i > 0;
		// Coordinates far outside the strip are held just outside it, where the clipping hides them.
		left = larger(left, strip_min.x - 4.0f);
		right = smaller(right, strip_max.x + 4.0f);
		draw->AddRectFilled(ImVec2(left, track_top), ImVec2(right, track_bottom), color_for_media(clip.media));
		draw->AddRect(ImVec2(left, track_top), ImVec2(right, track_bottom), COLOR_CLIP_BORDER);

		// The file's name, kept in view while any of the clip is.
		const float text_left = larger(left, strip_min.x);
		const float text_right = smaller(right, strip_max.x);
		if (text_right - text_left > 24.0f) {
			const std::string name = fileio::base_name(model.media_path(clip.media));
			draw->PushClipRect(ImVec2(text_left + 1.0f, track_top), ImVec2(text_right - 1.0f, track_bottom), true);
			draw->AddText(ImVec2(text_left + 6.0f, track_top + 5.0f), COLOR_CLIP_TEXT, name.c_str());
			draw->PopClipRect();
		}
		if (join_in_view)
			draw->AddLine(ImVec2(left, origin.y + 2.0f), ImVec2(left, track_bottom), COLOR_JOIN, 2.0f);
	}

	// The clip the playhead is in, which the edit buttons and keys act on.
	const int current = timeline_edit::current_clip(clips, playhead);
	if (current >= 0) {
		const double start = (double)clips.start(current);
		const double end = start + (double)clips.clip(current).duration_ticks();
		const float left = larger(origin.x + m_scale.tick_to_x(start), strip_min.x - 4.0f);
		const float right = smaller(origin.x + m_scale.tick_to_x(end), strip_max.x + 4.0f);
		draw->AddRect(ImVec2(left, track_top - 1.0f), ImVec2(right, track_bottom + 1.0f), COLOR_CURRENT_CLIP, 0.0f, 0, 2.0f);
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
