#include "speed_panel.h"

#include <float.h>
#include <math.h>
#include <stdio.h>

#include "imgui/imgui.h"

#include "panel_section.h"

#include "media_container_manager.h"
#include "timeline_edit.h"
#include "timeline_editor.h"
#include "util.h"

namespace {

const double PRESETS[] = { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0 };
const char*  PRESET_LABELS[] = { "0.25x", "0.5x", "1x", "2x", "4x", "8x" };
const int    PRESET_COUNT = 6;

}  // namespace

SpeedPanel::SpeedPanel(EditModel* model, MediaContainerMgr* media, TimelineEditor* editor)
	: m_model(model), m_media(media), m_editor(editor), m_field(1.0), m_field_active(false), m_field_clip(-1) {
}

void SpeedPanel::draw(bool can_change) {
	if (!panel_section::header("Speed"))
		return;
	const EditList& list = m_model->edit_list();
	const int index = m_editor->target_clip(*m_model, *m_media);
	if (index < 0) {
		ImGui::TextDisabled("(no video)");
		return;
	}
	const Clip clip = list.clip(index);

	char length[32];
	ffsw::make_time(length, (float)m_model->ticks_to_seconds(clip.timeline_ticks()), false);
	ImGui::Text("Clip %d of %d, %s long", index + 1, list.count(), length);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("The clip outlined on the timeline: the one the playhead is in.\nHow long it lasts at its speed.");

	// The usual speeds, in a row; the clip's own is lit. Nothing here is changed while the video plays (pausing it is how to pick a frame).
	can_change = can_change && !m_editor->playing();
	ImGui::BeginDisabled(!can_change);
	const float spacing = ImGui::GetStyle().ItemSpacing.x;
	const float button_width = (ImGui::GetContentRegionAvail().x - spacing * (float)(PRESET_COUNT - 1)) / (float)PRESET_COUNT;
	for (int n = 0; n < PRESET_COUNT; n++) {
		if (n > 0)
			ImGui::SameLine();
		const bool current = clip.speed == PRESETS[n];
		// Greyed when the speed would leave a transition longer than its clips allow (see EditList::max_transition_ticks).
		const bool allowed = current || m_editor->can_set_speed(*m_model, *m_media, PRESETS[n], clip.audio_mode);
		if (current)
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
		ImGui::BeginDisabled(!allowed);
		if (ImGui::Button(PRESET_LABELS[n], ImVec2(button_width, 0.0f)))
			m_editor->set_clip_speed(m_model, m_media, PRESETS[n], clip.audio_mode);
		ImGui::EndDisabled();
		if (!allowed && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip(m_editor->playing() ? "Pause the video to change the speed." :
				"Not at this speed: the clip would be too short for the transition at its start or end.");
		if (current)
			ImGui::PopStyleColor();
	}
	ImGui::EndDisabled();

	// Any other speed. The field shows the clip's speed whenever it isn't being typed in (so an undo shows up in it).
	if (!m_field_active || m_field_clip != index)
		m_field = clip.speed;
	m_field_clip = index;
	ImGui::BeginDisabled(!can_change);
	ImGui::SetNextItemWidth(90.0f);
	ImGui::InputDouble("##speed", &m_field, 0.0, 0.0, "%.4g");
	const bool entered = ImGui::IsItemDeactivatedAfterEdit();
	m_field_active = ImGui::IsItemActive();
	if (entered) {
		double speed = m_field;
		if (!(speed >= MIN_SPEED))
			speed = MIN_SPEED;
		if (speed > MAX_SPEED)
			speed = MAX_SPEED;
		m_field = speed;
		m_editor->set_clip_speed(m_model, m_media, speed, clip.audio_mode);
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("Any speed from 0.1 to 16 times normal. Type it and press Enter.\nBelow 1 the clip is slowed down, above 1 sped up.\nTelemetry and the picture go along with it; text boxes keep their own lengths.");
	ImGui::SameLine();
	ImGui::Text("x  (0.1 to 16)");

	// What the sound does. Nothing, at normal speed.
	ImGui::Spacing();
	ImGui::TextUnformatted("Sound at this speed");
	const bool sound_matters = clip.speed != 1.0;
	ImGui::BeginDisabled(!can_change || !sound_matters);
	int mode = clip.audio_mode;
	const char* names[3] = { "Keep pitch", "Change pitch", "Mute" };
	const char* tips[3] = {
		"Stretch or squeeze the sound in time without changing how high it is.\nBest between about 0.5x and 2x; far beyond that it can only stutter.",
		"Play the sound faster or slower like a tape or a record: higher when fast, lower when slow.",
		"No sound for this clip." };
	for (int n = 0; n < 3; n++) {
		if (ImGui::RadioButton(names[n], &mode, n))
			m_editor->set_clip_speed(m_model, m_media, clip.speed, n);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("%s%s", tips[n], sound_matters ? "" : "\n(Only matters once the clip is off normal speed.)");
	}
	ImGui::EndDisabled();
}
