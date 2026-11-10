#include "project_data.h"

#include <math.h>

// Turning a project into the editing model and back. Kept apart from the reading and writing of
// the file (project_data.cpp) so that it can be tested without a JSON library.

namespace project_io {

namespace {

void set_error(std::string* error, const std::string& message) {
	if (error)
		*error = message;
}

// Ticks from one time base to another, rounded to the nearest tick.
int64_t rescale_ticks(int64_t ticks, int from_num, int from_den, int to_num, int to_den) {
	if (from_num == to_num && from_den == to_den)
		return ticks;
	long double seconds = (long double)ticks * (long double)from_num / (long double)from_den;
	return (int64_t)llroundl(seconds * (long double)to_den / (long double)to_num);
}

// The transitions of a project as they go with its clips in this run: in this run's time base, those that are of a kind their place
// allows kept, and each shortened if need be to what its clips leave room for (the project file may have been edited, and a change of
// time base rounds).
std::vector<Transition> transitions_for(const ProjectData& project, const std::vector<Clip>& clips, int from_num, int from_den, int tb_num, int tb_den) {
	std::vector<Transition> result(clips.size() + 1, Transition());
	if (project.transitions.size() != project.clips.size() + 1 || project.clips.size() != clips.size())
		return result;
	EditList fit;
	fit.set(clips);
	const int count = (int)clips.size();
	for (int seam = 0; seam <= count; seam++) {
		const Transition& saved = project.transitions[seam];
		const bool at_an_end = seam == 0 || seam == count;
		if (saved.is_cut() || (at_an_end && saved.type != TRANSITION_FADE) || (!at_an_end && saved.type != TRANSITION_CROSSFADE))
			continue;
		int64_t ticks = rescale_ticks(saved.ticks, from_num, from_den, tb_num, tb_den);
		const int64_t most = fit.max_transition_ticks(seam);
		if (ticks > most)
			ticks = most;
		if (ticks > 0)
			result[seam] = Transition(saved.type, ticks);
	}
	return result;
}

}  // namespace

bool apply_to_model(const ProjectData& project, const std::vector<int64_t>& media_durations, int tb_num, int tb_den,
	EditModel* model, std::string* error) {
	if (project.video_files.size() != media_durations.size() || tb_num <= 0 || tb_den <= 0) {
		set_error(error, "The project's video files do not match the files that were opened.");
		return false;
	}
	model->set_media(project.video_files, media_durations, (double)tb_num / (double)tb_den);

	// Times in the project are in the time base it was saved with, which can differ from this
	// run's if the first video file is a different one now.
	const bool same_base = project.time_base_num == 0 || (project.time_base_num == tb_num && project.time_base_den == tb_den);
	const int from_num = same_base ? tb_num : project.time_base_num;
	const int from_den = same_base ? tb_den : project.time_base_den;

	if (!project.capture_order.empty()) {
		if (!model->set_capture_order(project.capture_order)) {
			set_error(error, "The project's capture order does not match its video files.");
			return false;
		}
	}
	if (!project.clips.empty()) {
		std::vector<Clip> clips;
		for (size_t i = 0; i < project.clips.size(); i++) {
			const Clip& clip = project.clips[i];
			clips.push_back(Clip(clip.media, rescale_ticks(clip.in_ticks, from_num, from_den, tb_num, tb_den),
				rescale_ticks(clip.out_ticks, from_num, from_den, tb_num, tb_den), clip.speed, clip.audio_mode));
		}
		if (!model->set_edit(clips, transitions_for(project, clips, from_num, from_den, tb_num, tb_den))) {
			// The usual cause is a file that has been replaced by a shorter one.
			set_error(error, "The edit in this project uses part of a video file that is no longer there: a file is shorter than it "
				"was when the project was saved, or the project file has been changed.");
			return false;
		}
	} else {
		model->set_default_clips();
	}
	model->set_edit_started(project.edit_started);

	std::vector<SyncAnchor> anchors;
	for (size_t i = 0; i < project.sync_anchors.size(); i++) {
		SyncAnchor anchor = project.sync_anchors[i];
		anchor.at.local_ticks = rescale_ticks(anchor.at.local_ticks, from_num, from_den, tb_num, tb_den);
		anchors.push_back(anchor);
	}
	model->set_anchors(anchors);
	if (project.launch_mark.media >= 0 && project.launch_mark.media < model->media_count()) {
		FileTime launch = project.launch_mark;
		launch.local_ticks = rescale_ticks(launch.local_ticks, from_num, from_den, tb_num, tb_den);
		model->set_launch_mark(launch);
		model->set_launch_marked(true);
	} else if (!model->capture().order().empty()) {
		// Not marked: the start of the flight, as it always was.
		model->set_launch_mark(FileTime(model->capture().order()[0], 0));
		model->set_launch_marked(false);
	}

	std::vector<TextBox> text_boxes;
	for (size_t i = 0; i < project.text_boxes.size(); i++) {
		TextBox box = project.text_boxes[i];
		if (box.anchor.media < 0 || box.anchor.media >= model->media_count())
			continue;
		box.anchor.local_ticks = rescale_ticks(box.anchor.local_ticks, from_num, from_den, tb_num, tb_den);
		box.fade_in_ticks = rescale_ticks(box.fade_in_ticks, from_num, from_den, tb_num, tb_den);
		box.dwell_ticks = rescale_ticks(box.dwell_ticks, from_num, from_den, tb_num, tb_den);
		box.fade_out_ticks = rescale_ticks(box.fade_out_ticks, from_num, from_den, tb_num, tb_den);
		if (project.text_anchors_at_fade_start) {
			// An older project tied the box to the frame its fade in began on; now it is the frame the fade in ends on.
			const int64_t fully_in = model->timeline_ticks_nearest(box.anchor) + box.fade_in_ticks;
			box.anchor = model->edit_list().source_for(fully_in);
		}
		text_boxes.push_back(box);
	}
	model->set_text_boxes(text_boxes);

	// Paths: the times of their points in this run's time base, and only those of files that are there, with a name and the points in order
	// (reading the points puts them in order and keeps one to a frame).
	std::vector<TrackedPath> paths;
	for (size_t i = 0; i < project.paths.size(); i++) {
		TrackedPath path = project.paths[i];
		if (path.media < 0 || path.media >= model->media_count())
			continue;
		std::vector<tracking::PathPoint> points;
		for (size_t k = 0; k < path.curve.points().size(); k++) {
			const tracking::PathPoint& point = path.curve.points()[k];
			points.push_back(tracking::PathPoint(rescale_ticks(point.ticks, from_num, from_den, tb_num, tb_den), point.u, point.v));
		}
		path.curve.set_points(points);
		paths.push_back(path);
	}
	model->set_paths(paths);
	model->set_path_fov_degrees(project.path_fov_degrees);

	if (project.has_version2_sync) {
		model->set_offset((double)project.version2_offset_s);
		model->set_launch_flight_seconds((double)project.version2_launch_s);
	}
	return true;
}

void fill_from_model(const EditModel& model, int tb_num, int tb_den, ProjectData* project) {
	project->time_base_num = tb_num;
	project->time_base_den = tb_den;
	project->capture_order = model.capture().order();
	project->clips = model.clips_to_keep();
	project->transitions.clear();
	const std::vector<Transition>& transitions = model.transitions_to_keep();
	for (size_t i = 0; i < transitions.size(); i++) {
		if (!transitions[i].is_cut())
			project->transitions = transitions;      // At least one that is not a cut: keep them all.
	}
	project->edit_started = model.edit_started_to_keep();

	// One anchor at the very start of the flight that says the clocks agree is the same as
	// having none, so it isn't kept (nudging the sync back to zero shouldn't count as an edit).
	project->sync_anchors.clear();
	const std::vector<SyncAnchor>& anchors = model.anchors();
	for (size_t i = 0; i < anchors.size(); i++) {
		bool is_nothing = anchors.size() == 1 && !model.capture().order().empty() &&
			anchors[i].at == FileTime(model.capture().order()[0], 0) && anchors[i].telemetry_seconds == 0.0f;
		if (!is_nothing)
			project->sync_anchors.push_back(anchors[i]);
	}
	project->launch_mark = model.launch_marked() ? model.launch_mark() : FileTime(-1, 0);
	project->text_boxes = model.text_boxes();
	project->paths = model.paths();
	project->path_fov_degrees = model.path_fov_degrees();
	project->has_version2_sync = false;
	project->text_anchors_at_fade_start = false;
	project->version2_offset_s = 0.0f;
	project->version2_launch_s = 0.0f;
}

}  // namespace project_io
