#include "transition_panel.h"

#include "imgui/imgui.h"

#include "timeline_selection.h"

TransitionPanel::TransitionPanel(EditModel* model, TimelineSelection* selection) : m_model(model), m_selection(selection), m_last_seam(-1) {
}

void TransitionPanel::draw(bool can_change) {
	(void)can_change;
	const int seam = m_selection->picked_seam();
	// A transition newly picked (by a click on its icon, or by a Split making one) opens the section, if it was closed.
	if (seam != m_last_seam) {
		if (seam >= 0)
			ImGui::SetNextItemOpen(true, ImGuiCond_Always);
		m_last_seam = seam;
	}
	if (!ImGui::CollapsingHeader("Transition"))
		return;
	const int count = m_model->edit_list().count();
	if (seam < 0 || seam > count) {
		ImGui::TextDisabled("Click the square at a join between two clips, or at the start or end of the video, to edit the transition there.");
		return;
	}
	if (seam == 0)
		ImGui::TextUnformatted("Start of the video");
	else if (seam == count)
		ImGui::TextUnformatted("End of the video");
	else
		ImGui::Text("Between clip %d and clip %d", seam, seam + 1);
	ImGui::TextUnformatted("Cut (no transition)");
	ImGui::TextDisabled("Fades and crossfades are still to come.");
}
