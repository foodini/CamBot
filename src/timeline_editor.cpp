#include "timeline_editor.h"

#include "env_config.h"
#include "media_container_manager.h"
#include "timeline_edit.h"
#include "util.h"

namespace {

EditState current_state(const EditModel& model, const MediaContainerMgr& media) {
	EditState state;
	state.clips = model.edit_list().clips();
	state.transitions = model.edit_list().transitions();
	state.text_boxes = model.text_boxes();
	state.edit_started = model.edit_started();
	state.playhead_ticks = media.current_ticks();
	return state;
}

}  // namespace

bool TimelineEditor::playing() const {
	return EnvConfig::instance != nullptr && !EnvConfig::instance->is_paused();
}

bool TimelineEditor::stop_for_edit(const char* what) {
	if (!playing())
		return false;
	EnvConfig::instance->pause();
	EnvConfig::instance->say(std::string(what) + " not done: the video was playing. It is paused now; find the frame and try again.", ffsw::elapsed());
	return true;
}

int TimelineEditor::target_clip(const EditModel& model, const MediaContainerMgr& media) const {
	if (m_selection != nullptr)
		return m_selection->effective_clip(model.edit_list(), media.current_ticks());
	return timeline_edit::current_clip(model.edit_list(), media.current_ticks());
}

bool TimelineEditor::can_split(const EditModel& model, const MediaContainerMgr& media) const {
	return !playing() && timeline_edit::can_split(model.edit_list(), media.current_ticks(), media.frame_ticks(), target_clip(model, media));
}

bool TimelineEditor::can_trim_left(const EditModel& model, const MediaContainerMgr& media) const {
	return !playing() && timeline_edit::can_trim_left(model.edit_list(), media.current_ticks(), media.frame_ticks(), target_clip(model, media));
}

bool TimelineEditor::can_trim_right(const EditModel& model, const MediaContainerMgr& media) const {
	return !playing() && timeline_edit::can_trim_right(model.edit_list(), media.current_ticks(), media.frame_ticks(), target_clip(model, media));
}

bool TimelineEditor::can_remove(const EditModel& model, const MediaContainerMgr& media) const {
	return !playing() && timeline_edit::can_remove(model.edit_list(), media.current_ticks(), media.frame_ticks(), target_clip(model, media));
}

bool TimelineEditor::can_set_speed(const EditModel& model, const MediaContainerMgr& media, double speed, int audio_mode) const {
	return !media.recording() && !playing() && timeline_edit::can_set_speed(model.edit_list(), media.current_ticks(), speed, audio_mode, target_clip(model, media));
}

bool TimelineEditor::can_undo(const MediaContainerMgr& media) const {
	return (m_history.can_undo() || m_box_edit_pending) && !media.recording();
}

bool TimelineEditor::can_redo(const MediaContainerMgr& media) const {
	return m_history.can_redo() && !media.recording();
}

bool TimelineEditor::apply_state(const EditState& state, EditModel* model, MediaContainerMgr* media) {
	const std::vector<Clip> old_clips = model->edit_list().clips();
	const std::vector<Transition> old_transitions = model->edit_list().transitions();
	const std::vector<TextBox> old_text_boxes = model->text_boxes();
	const bool old_started = model->edit_started();
	if (!model->set_edit(state.clips, state.transitions))
		return false;
	model->set_edit_started(state.edit_started);
	model->set_text_boxes(state.text_boxes);
	if (!media->set_clips(model->edit_list(), state.playhead_ticks)) {
		model->set_edit(old_clips, old_transitions);    // The decoder said no (the model's check should have caught it); put things back.
		model->set_edit_started(old_started);
		model->set_text_boxes(old_text_boxes);
		return false;
	}
	return true;
}

bool TimelineEditor::edit(Operation operation, EditModel* model, MediaContainerMgr* media) {
	if (media->recording())
		return false;
	if (stop_for_edit(operation == SPLIT ? "The split" : operation == REMOVE ? "The delete" : "The trim"))
		return false;
	commit_box_edit(*model, *media);
	const EditList& list = model->edit_list();
	const int64_t playhead = media->current_ticks();
	const int64_t frame = media->frame_ticks();
	const int clip = target_clip(*model, *media);
	timeline_edit::Result result;
	switch (operation) {
	case SPLIT:      result = timeline_edit::split(list, playhead, frame, clip); break;
	case TRIM_LEFT:  result = timeline_edit::trim_left(list, playhead, frame, clip); break;
	case TRIM_RIGHT: result = timeline_edit::trim_right(list, playhead, frame, clip); break;
	case REMOVE:     result = timeline_edit::remove_clip(list, playhead, frame, clip); break;
	}
	if (!result.ok)
		return false;

	EditState before = current_state(*model, *media);
	EditState after = before;           // The text boxes stay as they are.
	after.clips = result.clips;
	after.transitions = result.transitions;
	after.edit_started = true;          // The first edit by hand; from here on sync changes are warned about.
	after.playhead_ticks = result.playhead_ticks;
	EnvConfig::instance->pause();
	if (!apply_state(after, model, media))
		return false;
	m_history.record(before);
	return true;
}

bool TimelineEditor::split(EditModel* model, MediaContainerMgr* media) {
	const int index = target_clip(*model, *media);
	if (!edit(SPLIT, model, media))
		return false;
	if (m_selection != nullptr && index >= 0) {
		m_selection->adopt_clips(model->edit_list());
		m_selection->select_transition(index + 1);       // The join between the two pieces.
	}
	return true;
}
bool TimelineEditor::trim_left(EditModel* model, MediaContainerMgr* media) {
	const int index = target_clip(*model, *media);
	if (!edit(TRIM_LEFT, model, media))
		return false;
	if (m_selection != nullptr && index >= 0) {
		m_selection->adopt_clips(model->edit_list());
		m_selection->select_transition(index);           // The seam at the start of the clip.
	}
	return true;
}

bool TimelineEditor::trim_right(EditModel* model, MediaContainerMgr* media) {
	const int index = target_clip(*model, *media);
	if (!edit(TRIM_RIGHT, model, media))
		return false;
	if (m_selection != nullptr && index >= 0) {
		m_selection->adopt_clips(model->edit_list());
		m_selection->select_transition(index + 1);       // The seam at the end of the clip.
	}
	return true;
}
bool TimelineEditor::remove_clip(EditModel* model, MediaContainerMgr* media) { return edit(REMOVE, model, media); }

bool TimelineEditor::move_clip(EditModel* model, MediaContainerMgr* media, int from, int to) {
	if (media->recording())
		return false;
	commit_box_edit(*model, *media);
	const timeline_edit::Result result = timeline_edit::move_clip(model->edit_list(), from, to, media->current_ticks());
	if (!result.ok)
		return false;
	EditState before = current_state(*model, *media);
	EditState after = before;
	after.clips = result.clips;
	after.transitions = result.transitions;
	after.edit_started = true;
	after.playhead_ticks = result.playhead_ticks;
	EnvConfig::instance->pause();
	if (!apply_state(after, model, media))
		return false;
	m_history.record(before);
	return true;
}

bool TimelineEditor::set_clip_speed(EditModel* model, MediaContainerMgr* media, double speed, int audio_mode) {
	if (media->recording())
		return false;
	if (stop_for_edit("The speed change"))
		return false;
	commit_box_edit(*model, *media);
	const timeline_edit::Result result = timeline_edit::set_speed(model->edit_list(), media->current_ticks(), speed, audio_mode, media->frame_ticks(), target_clip(*model, *media));
	if (!result.ok)
		return false;
	EditState before = current_state(*model, *media);
	EditState after = before;
	after.clips = result.clips;
	after.transitions = result.transitions;
	after.edit_started = true;
	after.playhead_ticks = result.playhead_ticks;
	EnvConfig::instance->pause();
	if (!apply_state(after, model, media))
		return false;
	m_history.record(before);
	return true;
}

bool TimelineEditor::set_transition(EditModel* model, MediaContainerMgr* media, int seam, int type, int64_t ticks) {
	if (media->recording())
		return false;
	commit_box_edit(*model, *media);
	const EditList& list = model->edit_list();
	if (seam < 0 || seam > list.count())
		return false;
	const Transition wanted = (type == TRANSITION_CUT || ticks <= 0) ? Transition() : Transition(type, ticks);
	if (list.transition(seam) == wanted)
		return false;
	EditState before = current_state(*model, *media);
	EditState after = before;
	after.transitions[seam] = wanted;
	EditList candidate;
	candidate.set(after.clips, after.transitions);
	if (!candidate.transitions_valid())
		return false;
	after.playhead_ticks = timeline_edit::same_frame_after(list, candidate, before.playhead_ticks);   // A crossfade moves what follows it.
	EnvConfig::instance->pause();
	if (!apply_state(after, model, media))
		return false;
	m_history.record(before);
	return true;
}

int TimelineEditor::set_all_transitions(EditModel* model, MediaContainerMgr* media, int type, int64_t ticks, bool only_unset) {
	if (media->recording() || model->in_source_view())
		return 0;
	commit_box_edit(*model, *media);
	const EditList& list = model->edit_list();
	const int64_t frame = media->frame_ticks();
	if (frame <= 0 || ticks < frame)
		return 0;
	EditState before = current_state(*model, *media);
	EditState after = before;
	int changed = 0;
	for (int seam = 0; seam <= list.count(); seam++) {
		const bool at_an_end = seam == 0 || seam == list.count();
		if (at_an_end != (type == TRANSITION_FADE))
			continue;
		if (!at_an_end && model->join_is_continuous(seam))
			continue;
		if (only_unset && !list.transition(seam).is_cut())
			continue;
		int64_t length = list.max_transition_ticks(seam);
		if (length > ticks)
			length = ticks;
		length -= length % frame;
		if (length < frame)
			continue;
		const Transition wanted(type, length);
		if (after.transitions[seam] == wanted)
			continue;
		after.transitions[seam] = wanted;
		changed++;
	}
	if (changed == 0)
		return 0;
	EditList candidate;
	candidate.set(after.clips, after.transitions);
	if (!candidate.transitions_valid())
		return 0;
	after.playhead_ticks = timeline_edit::same_frame_after(list, candidate, before.playhead_ticks);
	EnvConfig::instance->pause();
	if (!apply_state(after, model, media))
		return 0;
	m_history.record(before);
	return changed;
}

bool TimelineEditor::undo(EditModel* model, MediaContainerMgr* media) {
	if (media->recording())
		return false;
	commit_box_edit(*model, *media);
	EditState restored;
	if (!m_history.undo(current_state(*model, *media), &restored))
		return false;
	EnvConfig::instance->pause();
	if (!apply_state(restored, model, media)) {
		// Can't happen (the clips were valid when they were recorded), but if it does the history no
		// longer matches what is on screen, so start it over rather than trust it.
		m_history.clear();
		return false;
	}
	return true;
}

bool TimelineEditor::redo(EditModel* model, MediaContainerMgr* media) {
	if (media->recording())
		return false;
	commit_box_edit(*model, *media);
	EditState restored;
	if (!m_history.redo(current_state(*model, *media), &restored))
		return false;
	EnvConfig::instance->pause();
	if (!apply_state(restored, model, media)) {
		m_history.clear();
		return false;
	}
	return true;
}

void TimelineEditor::go_to_text_box(const EditModel& model, MediaContainerMgr* media, int id) {
	const TextBox* box = model.find_text_box(id);
	if (box == NULL || media->recording())
		return;
	int64_t start = 0;
	int64_t end = 0;
	model.text_box_span(*box, &start, &end);
	int64_t target = start + box->fade_in_ticks;
	const int64_t last_frame = model.edit_list().total_ticks() - media->frame_ticks();
	if (target > last_frame)
		target = last_frame;
	if (target < 0)
		target = 0;
	EnvConfig::instance->pause();
	media->seek_to_keyframe_near(target);     // Near first and then exact, as scrubbing does, so a far jump is quick.
	media->advance_to(target);
}

void TimelineEditor::commit_box_edit(const EditModel& model, const MediaContainerMgr& media) {
	(void)media;
	if (!m_box_edit_pending)
		return;
	m_box_edit_pending = false;
	if (model.text_boxes() != m_box_edit_before.text_boxes)
		m_history.record(m_box_edit_before);
}

int TimelineEditor::add_text_box(EditModel* model, MediaContainerMgr* media, const TextBox& box) {
	if (media->recording())
		return 0;
	commit_box_edit(*model, *media);
	const EditState before = current_state(*model, *media);
	const int id = model->add_text_box(box);
	m_history.record(before);
	return id;
}

bool TimelineEditor::remove_text_box(EditModel* model, MediaContainerMgr* media, int id) {
	if (media->recording() || model->find_text_box(id) == NULL)
		return false;
	commit_box_edit(*model, *media);
	const EditState before = current_state(*model, *media);
	model->remove_text_box(id);
	m_history.record(before);
	return true;
}

void TimelineEditor::begin_box_edit(const EditModel& model, const MediaContainerMgr& media) {
	commit_box_edit(model, media);
	m_box_edit_before = current_state(model, media);
	m_box_edit_pending = true;
}

void TimelineEditor::set_text(EditModel* model, int id, const std::string& text) {
	TextBox* box = model->find_text_box(id);
	if (box != NULL)
		box->text = text;
}

void TimelineEditor::set_geometry(EditModel* model, int id, float x, float y, float w, float h) {
	TextBox* box = model->find_text_box(id);
	if (box == NULL)
		return;
	box->x = x;
	box->y = y;
	box->w = w;
	box->h = h;
}

void TimelineEditor::set_style(EditModel* model, int id, const TextStyle& style) {
	TextBox* box = model->find_text_box(id);
	if (box != NULL)
		box->style = style;
}

void TimelineEditor::set_timing(EditModel* model, int id, const FileTime& anchor, int64_t fade_in_ticks, int64_t dwell_ticks,
		int64_t fade_out_ticks) {
	TextBox* box = model->find_text_box(id);
	if (box == NULL)
		return;
	box->anchor = anchor;
	box->fade_in_ticks = fade_in_ticks;
	box->dwell_ticks = dwell_ticks;
	box->fade_out_ticks = fade_out_ticks;
}

void TimelineEditor::end_box_edit(const EditModel& model, const MediaContainerMgr& media) {
	commit_box_edit(model, media);
}
