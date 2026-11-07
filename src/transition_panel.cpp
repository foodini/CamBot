#include "transition_panel.h"

#include <float.h>
#include <math.h>
#include <stdint.h>

#include "imgui/imgui.h"

#include "panel_section.h"

#include "media_container_manager.h"
#include "timeline_editor.h"
#include "timeline_selection.h"

TransitionPanel::TransitionPanel(EditModel* model, MediaContainerMgr* media, TimelineEditor* editor, TimelineSelection* selection)
	: m_model(model), m_media(media), m_editor(editor), m_selection(selection), m_last_seam(-1), m_field(0.0), m_field_active(false),
	  m_field_seam(-1), m_all_count(-1) {
}

int64_t TransitionPanel::seconds_to_ticks(int seam, double seconds) const {
	const int64_t frame = m_media->frame_ticks();
	const int64_t most = m_model->edit_list().max_transition_ticks(seam);
	if (frame <= 0 || most < frame)
		return 0;
	const double per_tick = m_model->seconds_per_tick();
	int64_t ticks = per_tick > 0.0 ? (int64_t)floor(seconds / per_tick / (double)frame + 0.5) * frame : frame;
	if (ticks < frame)
		ticks = frame;
	const int64_t whole_frames = (most / frame) * frame;
	if (ticks > whole_frames)
		ticks = whole_frames;
	return ticks;
}

int64_t TransitionPanel::default_ticks(int seam) const {
	return seconds_to_ticks(seam, 1.0);
}

void TransitionPanel::draw(bool can_change) {
	const int seam = m_selection->picked_seam();
	// A transition newly picked (by a click on its icon, or by a Split making one) opens the section, if it was closed.
	if (seam != m_last_seam) {
		if (seam >= 0)
			ImGui::SetNextItemOpen(true, ImGuiCond_Always);
		m_last_seam = seam;
		m_all_count = -1;
	}
	if (!panel_section::header("Transition"))
		return;
	const int count = m_model->edit_list().count();
	if (seam < 0 || seam > count)
		ImGui::TextDisabled("Click the square at a join between two clips, or at the start or end of the video, to edit the transition there.");
	else
		draw_picked(seam, can_change);
}

void TransitionPanel::draw_picked(int seam, bool can_change) {
	const EditList& list = m_model->edit_list();
	const int count = list.count();
	const bool at_start = seam == 0;
	const bool at_end = seam == count;
	if (at_start)
		ImGui::TextUnformatted("Start of the video");
	else if (at_end)
		ImGui::TextUnformatted("End of the video");
	else
		ImGui::Text("Between clip %d and clip %d", seam, seam + 1);

	// A fade at the start or the end, a crossfade between two clips.
	const int type = (at_start || at_end) ? TRANSITION_FADE : TRANSITION_CROSSFADE;
	const Transition now = list.transition(seam);
	const bool set = !now.is_cut() && now.type == type;
	const int64_t most = list.max_transition_ticks(seam);
	const int64_t frame = m_media->frame_ticks();
	const bool room = frame > 0 && most >= frame;

	ImGui::BeginDisabled(!can_change);
	const char* none_name = "None";
	const char* name = at_start ? "Fade in from black" : at_end ? "Fade out to black" : "Crossfade";
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::BeginCombo("##transition_type", set ? name : none_name)) {
		if (ImGui::Selectable(none_name, !set))
			m_editor->set_transition(m_model, m_media, seam, TRANSITION_CUT, 0);
		ImGui::BeginDisabled(!room);
		if (ImGui::Selectable(name, set) && !set)
			m_editor->set_transition(m_model, m_media, seam, type, default_ticks(seam));
		ImGui::EndDisabled();
		ImGui::EndCombo();
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
		if (at_start)
			ImGui::SetTooltip("The picture (with everything drawn on it) and the sound come up from black over this long.");
		else if (at_end)
			ImGui::SetTooltip("The picture (with everything drawn on it) and the sound go down to black over this long.");
		else
			ImGui::SetTooltip("The end of the clip before is laid over the start of the clip after, the picture and the sound giving way\n"
			                  "to the next. The video gets that much shorter, and both clips use their own footage in full.");
	}

	if (!set) {
		if (!room)
			ImGui::TextDisabled("%s", (at_start || at_end) ? "The video is too short for a fade." : "The clips are too short for a crossfade.");
		else if (!at_start && !at_end && m_model->join_is_continuous(seam))
			ImGui::TextDisabled("The footage carries straight on here, so a crossfade is not needed.");
		return;
	}

	// How long. Shown as it is whenever it isn't being typed in (so an undo shows up in it).
	if (!m_field_active || m_field_seam != seam)
		m_field = m_model->ticks_to_seconds(now.ticks);
	m_field_seam = seam;
	ImGui::BeginDisabled(!can_change);
	ImGui::SetNextItemWidth(90.0f);
	ImGui::InputDouble("##transition_length", &m_field, 0.0, 0.0, "%.2f");
	const bool entered = ImGui::IsItemDeactivatedAfterEdit();
	m_field_active = ImGui::IsItemActive();
	if (entered) {
		const int64_t ticks = seconds_to_ticks(seam, m_field);
		if (ticks > 0)
			m_editor->set_transition(m_model, m_media, seam, type, ticks);
		m_field = m_model->ticks_to_seconds(ticks > 0 ? ticks : now.ticks);
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("How long it lasts, in seconds, to the nearest frame. Type it and press Enter.");
	ImGui::SameLine();
	ImGui::Text("s  (up to %.2f)", m_model->ticks_to_seconds(frame > 0 ? (most / frame) * frame : 0));
	draw_apply_buttons(seam, now, can_change);
}

void TransitionPanel::draw_apply_buttons(int seam, const Transition& now, bool can_change) {
	const int count = m_model->edit_list().count();
	const bool at_an_end = seam == 0 || seam == count;
	const char* things = at_an_end ? "fades" : "crossfades";
	ImGui::Spacing();
	const bool ready = can_change && !m_editor->playing() && !m_model->in_source_view();
	ImGui::BeginDisabled(!ready);
	if (ImGui::Button("Apply to all transitions", ImVec2(-FLT_MIN, 0.0f)))
		m_all_count = m_editor->set_all_transitions(m_model, m_media, now.type, now.ticks, false);
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("Give every %s the length of this one, replacing any it has (shorter where the clips are short).\n"
		                  "Joins where the footage carries straight on (a clip that was split, or one camera file into the next) are left alone.", things);
	ImGui::BeginDisabled(!ready);
	if (ImGui::Button("Apply to un-configured transitions", ImVec2(-FLT_MIN, 0.0f)))
		m_all_count = m_editor->set_all_transitions(m_model, m_media, now.type, now.ticks, true);
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("Give this one's kind and length to the %s that have none, and leave the others as they are.\n"
		                  "Joins where the footage carries straight on are left alone.", things);
	if (m_all_count >= 0)
		ImGui::TextDisabled(m_all_count == 1 ? "1 transition changed." : "%d transitions changed.", m_all_count);
}
