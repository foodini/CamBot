#include "text_panel.h"

#include <algorithm>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "imgui/imgui.h"

#include "media_container_manager.h"
#include "text_box_geometry.h"
#include "text_box.h"
#include "timeline_editor.h"
#include "util.h"

namespace {

const int    LIST_ROWS = 5;
const int    TEXT_LINES = 6;
const int    SPARE_BYTES = 2048;       // How much more than it holds the text field has room for.
const size_t LABEL_BYTES = 40;         // The most of a box's words shown in the list.

const double NEW_FADE_IN_SECONDS = 0.5;
const double NEW_DWELL_SECONDS = 4.0;
const double NEW_FADE_OUT_SECONDS = 0.5;

// The first line of `text`, cut short (with "..." on) if it is long, without cutting a UTF-8 character in two.
std::string preview_of(const std::string& text) {
	std::string line = text.substr(0, text.find('\n'));
	const bool more = line.size() < text.size();
	if (line.size() > LABEL_BYTES) {
		size_t cut = LABEL_BYTES;
		while (cut > 0 && (((unsigned char)line[cut]) & 0xC0) == 0x80)
			cut--;
		line = line.substr(0, cut) + "...";
	} else if (more) {
		line += "...";
	}
	return line;
}

// A dashed line from a to b: 6 pixels on, 4 off.
void draw_dashed_line(ImDrawList* draw_list, const ImVec2& a, const ImVec2& b, ImU32 color) {
	const float DASH = 6.0f;
	const float GAP = 4.0f;
	const float length = sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
	if (length < 1.0f)
		return;
	const ImVec2 step((b.x - a.x) / length, (b.y - a.y) / length);
	for (float at = 0.0f; at < length; at += DASH + GAP) {
		const float to = (at + DASH < length) ? at + DASH : length;
		draw_list->AddLine(ImVec2(a.x + step.x * at, a.y + step.y * at), ImVec2(a.x + step.x * to, a.y + step.y * to), color, 1.5f);
	}
}

}  // namespace

TextPanel::TextPanel(EditModel* model, MediaContainerMgr* media, TimelineEditor* editor)
	: m_model(model), m_media(media), m_editor(editor), m_selected(0), m_field_active(false), m_buffer_id(0), m_drag_part(-1), m_drag_start_mouse_x(0.0f),
	  m_drag_start_mouse_y(0.0f) {}

int TextPanel::selected_id() const {
	return (m_selected != 0 && m_model->find_text_box(m_selected) != NULL) ? m_selected : 0;
}

void TextPanel::draw(bool can_change) {
	if (!ImGui::CollapsingHeader("Text"))
		return;
	if (m_selected != 0 && m_model->find_text_box(m_selected) == NULL)
		m_selected = 0;

	const ImVec2 full_width(-FLT_MIN, 0.0f);
	const bool can_add = can_change && !m_model->edit_list().clips().empty();
	ImGui::BeginDisabled(!can_add);
	if (ImGui::Button("Add", full_width)) {
		TextBox box = text_box::default_box();
		// The box is fully in at the frame being shown, so what is typed is seen at once: the fade in comes before it (or, with
		// less than that much video before it, is as long as there is).
		const int64_t now_ticks = m_media->current_ticks();
		int64_t fade_in_ticks = m_model->seconds_to_ticks(NEW_FADE_IN_SECONDS);
		if (fade_in_ticks > now_ticks)
			fade_in_ticks = (now_ticks > 0) ? now_ticks : 0;
		box.anchor = m_model->edit_list().source_for(now_ticks - fade_in_ticks);
		box.fade_in_ticks = fade_in_ticks;
		box.dwell_ticks = m_model->seconds_to_ticks(NEW_DWELL_SECONDS);
		box.fade_out_ticks = m_model->seconds_to_ticks(NEW_FADE_OUT_SECONDS);
		const int id = m_editor->add_text_box(m_model, m_media, box);
		if (id != 0)
			m_selected = id;
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("Put a text box on the picture, starting at the frame shown now.");

	const std::vector<TextBox>& boxes = m_model->text_boxes();
	if (boxes.empty()) {
		ImGui::TextDisabled("(no text yet)");
		return;
	}

	// The list, in the order the boxes appear.
	std::vector<int64_t> starts(boxes.size());
	std::vector<size_t> order(boxes.size());
	for (size_t i = 0; i < boxes.size(); i++) {
		int64_t end = 0;
		m_model->text_box_span(boxes[i], &starts[i], &end);
		order[i] = i;
	}
	std::stable_sort(order.begin(), order.end(), [&starts](size_t a, size_t b) { return starts[a] < starts[b]; });

	const float row_height = ImGui::GetTextLineHeightWithSpacing();
	ImGui::BeginChild("##textlist", ImVec2(0.0f, row_height * (float)std::min<size_t>(boxes.size(), LIST_ROWS) + 8.0f), ImGuiChildFlags_Borders);
	for (size_t n = 0; n < order.size(); n++) {
		const TextBox& box = boxes[order[n]];
		char when[32];
		ffsw::make_time(when, (float)m_model->ticks_to_seconds(starts[order[n]]), false);
		const std::string label = std::string(when) + "  " + preview_of(box.text) + "##" + std::to_string(box.id);
		if (ImGui::Selectable(label.c_str(), box.id == m_selected))
			m_selected = (box.id == m_selected) ? 0 : box.id;     // Clicking the selected one again lets go of it.
	}
	ImGui::EndChild();

	TextBox* selected = (m_selected != 0) ? m_model->find_text_box(m_selected) : NULL;
	if (selected == NULL) {
		m_field_active = false;
		return;
	}

	// The field takes its words from the box whenever it isn't being typed in (so an undo shows up in it).
	if (!m_field_active || m_buffer_id != m_selected) {
		m_buffer_id = m_selected;
		m_buffer.assign(selected->text.begin(), selected->text.end());
		m_buffer.resize(selected->text.size() + SPARE_BYTES, '\0');
	}
	ImGui::BeginDisabled(!can_change);
	const bool edited = ImGui::InputTextMultiline("##text", m_buffer.data(), m_buffer.size(),
		ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * (float)TEXT_LINES + 8.0f));
	if (ImGui::IsItemActivated())
		m_editor->begin_box_edit(*m_model, *m_media);
	if (edited)
		m_editor->set_text(m_model, m_selected, std::string(m_buffer.data()));
	if (ImGui::IsItemDeactivated())
		m_editor->end_box_edit(*m_model, *m_media);
	m_field_active = ImGui::IsItemActive();
	ImGui::EndDisabled();

	ImGui::BeginDisabled(!can_change);
	if (ImGui::Button("Delete Box", full_width)) {
		m_field_active = false;
		m_editor->remove_text_box(m_model, m_media, m_selected);
		m_selected = 0;
	}
	ImGui::EndDisabled();
}

void TextPanel::draw_on_preview(const LayoutRect& preview, bool can_change) {
	TextBox* box = (m_selected != 0) ? m_model->find_text_box(m_selected) : NULL;
	if (box == NULL || !can_change || m_model->in_source_view() || preview.w < 1.0f || preview.h < 1.0f) {
		m_drag_part = -1;
		return;
	}

	const float HANDLE_SIZE = 12.0f;                       // How big the corner squares are drawn, in pixels.
	const ImVec2 top_left(preview.x + box->x * preview.w, preview.y + box->y * preview.h);
	const ImVec2 bottom_right(top_left.x + box->w * preview.w, top_left.y + box->h * preview.h);
	// The mouse is the corner's from well away from the square, so the corner wins over the edges and the box for anything
	// near it: a square 28 pixels across (14 each way), less on a very small box so the four don't crowd the middle out.
	// The edges are strips 12 pixels wide (6 each way), thinner on a very small box so they leave some of it to take hold of.
	const float box_width = bottom_right.x - top_left.x;
	const float box_height = bottom_right.y - top_left.y;
	float hit_size = 0.45f * fminf(box_width, box_height);
	hit_size = fminf(28.0f, fmaxf(HANDLE_SIZE, hit_size));
	float edge_size = 0.3f * fminf(box_width, box_height);
	edge_size = fminf(12.0f, fmaxf(4.0f, edge_size));
	const float MARGIN = hit_size * 0.5f + 1.0f;          // The window reaches this far out of the box, to take in the corners.

	// A window just the size of the box and its handles, so the mouse is the window's over the box (and the click doesn't
	// go on to pause the video) and the video's everywhere else.
	ImGui::SetNextWindowPos(ImVec2(top_left.x - MARGIN, top_left.y - MARGIN));
	ImGui::SetNextWindowSize(ImVec2(bottom_right.x - top_left.x + 2.0f * MARGIN, bottom_right.y - top_left.y + 2.0f * MARGIN));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing;
	if (ImGui::Begin("##text_box_on_preview", NULL, flags)) {
		// The corners first, then the edges, then the body: where items overlap the one submitted first gets the mouse.
		const ImVec2 corners[4] = { top_left, ImVec2(bottom_right.x, top_left.y), ImVec2(top_left.x, bottom_right.y), bottom_right };
		const char* const names[9] = { "##top_left", "##top_right", "##bottom_left", "##bottom_right",
			"##left", "##right", "##top", "##bottom", "##body" };
		const int parts[9] = { text_box_geometry::TOP_LEFT, text_box_geometry::TOP_RIGHT, text_box_geometry::BOTTOM_LEFT,
			text_box_geometry::BOTTOM_RIGHT, text_box_geometry::EDGE_LEFT, text_box_geometry::EDGE_RIGHT, text_box_geometry::EDGE_TOP,
			text_box_geometry::EDGE_BOTTOM, BODY };
		for (int slot = 0; slot < 9; slot++) {
			const int part = parts[slot];
			if (slot < 4) {
				ImGui::SetCursorScreenPos(ImVec2(corners[slot].x - hit_size * 0.5f, corners[slot].y - hit_size * 0.5f));
				ImGui::InvisibleButton(names[slot], ImVec2(hit_size, hit_size));
			} else if (part == text_box_geometry::EDGE_LEFT || part == text_box_geometry::EDGE_RIGHT) {
				const float edge_x = (part == text_box_geometry::EDGE_LEFT) ? top_left.x : bottom_right.x;
				ImGui::SetCursorScreenPos(ImVec2(edge_x - edge_size * 0.5f, top_left.y));
				ImGui::InvisibleButton(names[slot], ImVec2(edge_size, box_height));
			} else if (part == text_box_geometry::EDGE_TOP || part == text_box_geometry::EDGE_BOTTOM) {
				const float edge_y = (part == text_box_geometry::EDGE_TOP) ? top_left.y : bottom_right.y;
				ImGui::SetCursorScreenPos(ImVec2(top_left.x, edge_y - edge_size * 0.5f));
				ImGui::InvisibleButton(names[slot], ImVec2(box_width, edge_size));
			} else {
				ImGui::SetCursorScreenPos(top_left);
				ImGui::InvisibleButton(names[slot], ImVec2(box_width, box_height));
			}
			const ImVec2 mouse = ImGui::GetIO().MousePos;
			const float mouse_x = (mouse.x - preview.x) / preview.w;
			const float mouse_y = (mouse.y - preview.y) / preview.h;
			if (ImGui::IsItemActivated()) {
				m_drag_part = part;
				m_drag_start_box = text_box_geometry::Rect(box->x, box->y, box->w, box->h);
				m_drag_start_mouse_x = mouse_x;
				m_drag_start_mouse_y = mouse_y;
				m_editor->begin_box_edit(*m_model, *m_media);
			}
			if (ImGui::IsItemActive() && m_drag_part == part) {
				const float dx = mouse_x - m_drag_start_mouse_x;
				const float dy = mouse_y - m_drag_start_mouse_y;
				text_box_geometry::Rect moved;
				if (part == BODY)
					moved = text_box_geometry::move(m_drag_start_box, dx, dy);
				else if (slot < 4)
					moved = text_box_geometry::drag_corner(m_drag_start_box, part, dx, dy);
				else
					moved = text_box_geometry::drag_edge(m_drag_start_box, part, dx, dy);
				m_editor->set_geometry(m_model, m_selected, moved.x, moved.y, moved.w, moved.h);
			}
			if (ImGui::IsItemDeactivated() && m_drag_part == part) {
				m_editor->end_box_edit(*m_model, *m_media);
				m_drag_part = -1;
			}
			if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
				if (part == BODY)
					ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
				else if (part == text_box_geometry::TOP_LEFT || part == text_box_geometry::BOTTOM_RIGHT)
					ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
				else if (slot < 4)
					ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNESW);
				else if (part == text_box_geometry::EDGE_LEFT || part == text_box_geometry::EDGE_RIGHT)
					ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
				else
					ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
			}
		}

		// The dashed outline, light over a dark line so it shows on any picture, and a square handle at each corner.
		ImDrawList* draw_list = ImGui::GetWindowDrawList();
		const ImU32 dark = IM_COL32(0, 0, 0, 160);
		const ImU32 light = IM_COL32(255, 255, 255, 255);
		draw_list->AddRect(top_left, bottom_right, dark, 0.0f, 0, 3.0f);
		draw_dashed_line(draw_list, top_left, ImVec2(bottom_right.x, top_left.y), light);
		draw_dashed_line(draw_list, ImVec2(bottom_right.x, top_left.y), bottom_right, light);
		draw_dashed_line(draw_list, bottom_right, ImVec2(top_left.x, bottom_right.y), light);
		draw_dashed_line(draw_list, ImVec2(top_left.x, bottom_right.y), top_left, light);
		for (int c = 0; c < 4; c++) {
			const ImVec2 low(corners[c].x - HANDLE_SIZE * 0.5f, corners[c].y - HANDLE_SIZE * 0.5f);
			const ImVec2 high(corners[c].x + HANDLE_SIZE * 0.5f, corners[c].y + HANDLE_SIZE * 0.5f);
			draw_list->AddRectFilled(low, high, light);
			draw_list->AddRect(low, high, IM_COL32(0, 0, 0, 255), 0.0f, 0, 1.5f);
		}
	}
	ImGui::End();
	ImGui::PopStyleVar(2);
}
