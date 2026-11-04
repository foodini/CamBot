#include "sync_view.h"

#include <math.h>
#include <stdio.h>
#include <string>

#include "imgui/imgui.h"

#include "env_config.h"
#include "file_io.h"
#include "media_container_manager.h"
#include "timeline_edit.h"
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

}  // namespace

SyncView::SyncView() : m_active(false), m_timeline("SyncTimeline"), m_entry_playhead(0) {
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
	int64_t show_ticks = 0;
	for (int i = 0; i < model->edit_list().count(); i++) {
		if (model->edit_list().clip(i).media == on_screen.media) {
			show_ticks = model->edit_list().timeline_for(i, on_screen.local_ticks);
			break;
		}
	}
	if (!media->set_clips(model->edit_list().clips(), show_ticks)) {
		model->end_source_view();
		return false;
	}
	m_timeline.fit();
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
	m_active = false;
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
}

SyncView::Outcome SyncView::draw(const SyncLayout& layout, EditModel& model, MediaContainerMgr& media, bool has_telemetry) {
	Outcome outcome = STAY;
	draw_header(layout.header, &outcome);
	draw_map_pane(layout.map, model, media, has_telemetry);
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

void SyncView::draw_map_pane(const LayoutRect& rect, const EditModel& model, const MediaContainerMgr& media, bool has_telemetry) {
	if (rect.w < 1.0f || rect.h < 1.0f)
		return;
	ImGui::SetNextWindowPos(ImVec2(rect.x, rect.y));
	ImGui::SetNextWindowSize(ImVec2(rect.w, rect.h));
	if (ImGui::Begin("SyncMap", nullptr, bar_flags())) {
		ImGui::TextDisabled("The map will be here.");
		ImGui::Spacing();
		const FileTime moment = model.edit_list().source_for(media.current_ticks());
		const double flight = model.flight_seconds(moment);
		ImGui::TextUnformatted("The frame on screen is");
		ImGui::Text("  in file %d of %d: %s", moment.media + 1, model.media_count(), fileio::base_name(model.media_path(moment.media)).c_str());
		ImGui::Text("  %s into that file", format_seconds(model.ticks_to_seconds(moment.local_ticks)).c_str());
		ImGui::Text("  %s into the flight", format_seconds(flight).c_str());
		if (has_telemetry)
			ImGui::Text("  %s on the telemetry's clock", format_seconds(model.telemetry_seconds(flight)).c_str());
	}
	ImGui::End();
}
