#include "edit_model.h"

#include <math.h>

// ---------------------------------------------------------------------------------------------
// EditList
// ---------------------------------------------------------------------------------------------

EditList::EditList() : m_total(0) {
}

void EditList::clear() {
	m_clips.clear();
	m_starts.clear();
	m_total = 0;
}

void EditList::set(const std::vector<Clip>& clips) {
	m_clips = clips;
	m_starts.clear();
	m_total = 0;
	for (size_t i = 0; i < m_clips.size(); i++) {
		m_starts.push_back(m_total);
		m_total += m_clips[i].duration_ticks();
	}
}

int EditList::clip_index_for(int64_t timeline_ticks) const {
	if (m_clips.empty())
		return -1;
	// The last clip that starts at or before the time.
	int low = 0;
	int high = (int)m_clips.size() - 1;
	while (low < high) {
		int middle = low + (high - low + 1) / 2;
		if (m_starts[middle] <= timeline_ticks)
			low = middle;
		else
			high = middle - 1;
	}
	return low;
}

FileTime EditList::source_for(int64_t timeline_ticks) const {
	int index = clip_index_for(timeline_ticks);
	if (index < 0)
		return FileTime();
	if (timeline_ticks < 0)
		timeline_ticks = 0;
	if (timeline_ticks > m_total)
		timeline_ticks = m_total;
	return FileTime(m_clips[index].media, m_clips[index].in_ticks + (timeline_ticks - m_starts[index]));
}

int64_t EditList::timeline_for(int index, int64_t local_ticks) const {
	int64_t time = m_starts[index] + (local_ticks - m_clips[index].in_ticks);
	if (time < m_starts[index])
		time = m_starts[index];
	int64_t end = m_starts[index] + m_clips[index].duration_ticks();
	if (time > end)
		time = end;
	return time;
}

// ---------------------------------------------------------------------------------------------
// CaptureSequence
// ---------------------------------------------------------------------------------------------

CaptureSequence::CaptureSequence() : m_total(0) {
}

bool CaptureSequence::is_valid_order(const std::vector<int>& order, int media_count) {
	if ((int)order.size() != media_count)
		return false;
	std::vector<bool> seen((size_t)media_count, false);
	for (size_t i = 0; i < order.size(); i++) {
		if (order[i] < 0 || order[i] >= media_count || seen[order[i]])
			return false;
		seen[order[i]] = true;
	}
	return true;
}

bool CaptureSequence::set(const std::vector<int>& order, const std::vector<int64_t>& durations) {
	if (!is_valid_order(order, (int)durations.size()))
		return false;
	m_order = order;
	m_durations = durations;
	m_starts.assign(durations.size(), 0);
	m_total = 0;
	for (size_t position = 0; position < m_order.size(); position++) {
		m_starts[m_order[position]] = m_total;
		m_total += m_durations[m_order[position]];
	}
	return true;
}

FileTime CaptureSequence::file_time_for(int64_t flight_ticks) const {
	if (m_order.empty())
		return FileTime();
	if (flight_ticks < 0)
		flight_ticks = 0;
	if (flight_ticks > m_total)
		flight_ticks = m_total;
	// The last file in the order that starts at or before the time.
	int found = m_order[0];
	for (size_t position = 0; position < m_order.size(); position++) {
		if (m_starts[m_order[position]] <= flight_ticks)
			found = m_order[position];
	}
	return FileTime(found, flight_ticks - m_starts[found]);
}

// ---------------------------------------------------------------------------------------------
// EditModel
// ---------------------------------------------------------------------------------------------

EditModel::EditModel() : m_seconds_per_tick(1.0 / 90000.0), m_edit_started(false), m_in_source_view(false), m_stashed_edit_started(false),
	m_text_boxes(), m_next_text_id(1) {
}

int64_t EditModel::seconds_to_ticks(double seconds) const {
	if (m_seconds_per_tick <= 0.0)
		return 0;
	return (int64_t)llround(seconds / m_seconds_per_tick);
}

void EditModel::set_media(const std::vector<std::string>& paths, const std::vector<int64_t>& durations, double seconds_per_tick) {
	m_paths = paths;
	m_durations = durations;
	m_seconds_per_tick = seconds_per_tick;
	std::vector<int> identity;
	for (int i = 0; i < (int)paths.size(); i++)
		identity.push_back(i);
	m_capture.set(identity, m_durations);
	m_in_source_view = false;
	m_stashed_clips.clear();
	set_default_clips();
	m_edit_started = false;
	m_anchors.clear();
	m_launch_mark = FileTime();
	m_text_boxes.clear();
	m_next_text_id = 1;
	rebuild_sync();
}

void EditModel::set_text_boxes(const std::vector<TextBox>& boxes) {
	m_text_boxes = boxes;
	m_next_text_id = 1;
	for (size_t i = 0; i < m_text_boxes.size(); i++) {
		if (m_text_boxes[i].id >= m_next_text_id)
			m_next_text_id = m_text_boxes[i].id + 1;
	}
}

int EditModel::add_text_box(const TextBox& box) {
	TextBox added = box;
	added.id = m_next_text_id++;
	m_text_boxes.push_back(added);
	return added.id;
}

bool EditModel::remove_text_box(int id) {
	for (size_t i = 0; i < m_text_boxes.size(); i++) {
		if (m_text_boxes[i].id == id) {
			m_text_boxes.erase(m_text_boxes.begin() + i);
			return true;
		}
	}
	return false;
}

TextBox* EditModel::find_text_box(int id) {
	for (size_t i = 0; i < m_text_boxes.size(); i++) {
		if (m_text_boxes[i].id == id)
			return &m_text_boxes[i];
	}
	return nullptr;
}

const TextBox* EditModel::find_text_box(int id) const {
	for (size_t i = 0; i < m_text_boxes.size(); i++) {
		if (m_text_boxes[i].id == id)
			return &m_text_boxes[i];
	}
	return nullptr;
}

void EditModel::text_box_span(const TextBox& box, int64_t* start_ticks, int64_t* end_ticks) const {
	const int64_t start = timeline_ticks_nearest(box.anchor);
	*start_ticks = start;
	*end_ticks = start + box.total_ticks();
}

bool EditModel::set_capture_order(const std::vector<int>& order) {
	if (!m_capture.set(order, m_durations))
		return false;
	rebuild_sync();
	return true;
}

bool EditModel::set_clips(const std::vector<Clip>& clips) {
	for (size_t i = 0; i < clips.size(); i++) {
		const Clip& clip = clips[i];
		if (clip.media < 0 || clip.media >= media_count())
			return false;
		if (clip.in_ticks < 0 || clip.out_ticks <= clip.in_ticks || clip.out_ticks > m_durations[clip.media])
			return false;
	}
	m_edit_list.set(clips);
	return true;
}

void EditModel::set_default_clips() {
	std::vector<Clip> clips;
	const std::vector<int>& order = m_capture.order();
	for (size_t position = 0; position < order.size(); position++)
		clips.push_back(Clip(order[position], 0, m_durations[order[position]]));
	m_edit_list.set(clips);
}

void EditModel::begin_source_view() {
	if (m_in_source_view)
		return;
	m_stashed_clips = m_edit_list.clips();
	m_stashed_edit_started = m_edit_started;
	m_in_source_view = true;
	set_default_clips();
}

void EditModel::end_source_view() {
	if (!m_in_source_view)
		return;
	m_in_source_view = false;
	m_edit_list.set(m_stashed_clips);
	m_edit_started = m_stashed_edit_started;
	m_stashed_clips.clear();
}

void EditModel::set_anchors(const std::vector<SyncAnchor>& anchors) {
	m_anchors.clear();
	for (size_t i = 0; i < anchors.size(); i++) {
		if (anchors[i].at.media >= 0 && anchors[i].at.media < media_count())
			m_anchors.push_back(anchors[i]);
	}
	rebuild_sync();
}

void EditModel::rebuild_sync() {
	m_resolved.clear();
	for (size_t i = 0; i < m_anchors.size(); i++) {
		ResolvedAnchor resolved;
		resolved.flight_seconds = flight_seconds(m_anchors[i].at);
		resolved.offset = resolved.flight_seconds - (double)m_anchors[i].telemetry_seconds;
		// Keep them earliest first (there are only ever a few).
		size_t position = m_resolved.size();
		m_resolved.push_back(resolved);
		while (position > 0 && m_resolved[position - 1].flight_seconds > m_resolved[position].flight_seconds) {
			ResolvedAnchor swap = m_resolved[position - 1];
			m_resolved[position - 1] = m_resolved[position];
			m_resolved[position] = swap;
			position--;
		}
	}
}

double EditModel::offset_at(double flight_seconds_value) const {
	if (m_resolved.empty())
		return 0.0;
	if (flight_seconds_value <= m_resolved.front().flight_seconds)
		return m_resolved.front().offset;
	if (flight_seconds_value >= m_resolved.back().flight_seconds)
		return m_resolved.back().offset;
	for (size_t i = 0; i + 1 < m_resolved.size(); i++) {
		const ResolvedAnchor& low = m_resolved[i];
		const ResolvedAnchor& high = m_resolved[i + 1];
		if (flight_seconds_value >= low.flight_seconds && flight_seconds_value < high.flight_seconds) {
			double along = (flight_seconds_value - low.flight_seconds) / (high.flight_seconds - low.flight_seconds);
			return low.offset + along * (high.offset - low.offset);
		}
	}
	return m_resolved.back().offset;
}

double EditModel::telemetry_seconds(double flight_seconds_value) const {
	return flight_seconds_value - offset_at(flight_seconds_value);
}

double EditModel::offset() const {
	if (m_resolved.empty())
		return 0.0;
	return m_resolved.front().offset;
}

void EditModel::add_to_offset(double delta) {
	for (size_t i = 0; i < m_anchors.size(); i++)
		m_anchors[i].telemetry_seconds = (float)((double)m_anchors[i].telemetry_seconds - delta);
	rebuild_sync();
}

void EditModel::set_offset(double new_offset) {
	if (m_anchors.empty()) {
		if (m_capture.order().empty())
			return;
		FileTime start(m_capture.order()[0], 0);
		m_anchors.push_back(SyncAnchor(start, (float)(0.0 - new_offset)));
		rebuild_sync();
		return;
	}
	add_to_offset(new_offset - offset());
}

double EditModel::flight_seconds(const FileTime& time) const {
	if (time.media < 0 || time.media >= media_count())
		return 0.0;
	return ticks_to_seconds(m_capture.flight_ticks(time));
}

int64_t EditModel::timeline_ticks_nearest(const FileTime& moment) const {
	const int64_t at = m_capture.flight_ticks(moment);
	int best = -1;
	int64_t best_distance = 0;
	bool before_best = false;
	for (int i = 0; i < m_edit_list.count(); i++) {
		const Clip& clip = m_edit_list.clip(i);
		const int64_t first = m_capture.flight_ticks(FileTime(clip.media, clip.in_ticks));
		const int64_t last = m_capture.flight_ticks(FileTime(clip.media, clip.out_ticks));
		int64_t distance = 0;
		bool before = false;
		if (at < first) {
			distance = first - at;
			before = true;
		} else if (at >= last) {
			distance = at - last + 1;
		}
		if (best < 0 || distance < best_distance) {
			best = i;
			best_distance = distance;
			before_best = before;
		}
	}
	if (best < 0)
		return 0;
	const Clip& clip = m_edit_list.clip(best);
	if (best_distance == 0)
		return m_edit_list.timeline_for(best, moment.local_ticks);
	return m_edit_list.timeline_for(best, before_best ? clip.in_ticks : clip.out_ticks);
}

FileTime EditModel::file_time_for_flight_seconds(double flight_seconds_value) const {
	return m_capture.file_time_for(seconds_to_ticks(flight_seconds_value));
}

double EditModel::launch_flight_seconds() const {
	return flight_seconds(m_launch_mark);
}

void EditModel::set_launch_flight_seconds(double seconds) {
	m_launch_mark = file_time_for_flight_seconds(seconds);
}

double EditModel::flight_seconds_for_timeline(int64_t timeline_ticks) const {
	if (m_edit_list.count() == 0)
		return 0.0;
	return flight_seconds(m_edit_list.source_for(timeline_ticks));
}

double EditModel::telemetry_seconds_for_timeline(int64_t timeline_ticks) const {
	return telemetry_seconds(flight_seconds_for_timeline(timeline_ticks));
}

bool EditModel::used_flight_span(double* start_seconds, double* end_seconds) const {
	if (m_edit_list.count() == 0)
		return false;
	double earliest = 0.0;
	double latest = 0.0;
	for (int i = 0; i < m_edit_list.count(); i++) {
		const Clip& clip = m_edit_list.clip(i);
		const double start = flight_seconds(FileTime(clip.media, clip.in_ticks));
		const double end = flight_seconds(FileTime(clip.media, clip.out_ticks));
		if (i == 0 || start < earliest)
			earliest = start;
		if (i == 0 || end > latest)
			latest = end;
	}
	*start_seconds = earliest;
	*end_seconds = latest;
	return true;
}

bool EditModel::used_telemetry_span(double* start_seconds, double* end_seconds) const {
	double flight_start = 0.0;
	double flight_end = 0.0;
	if (!used_flight_span(&flight_start, &flight_end))
		return false;
	*start_seconds = telemetry_seconds(flight_start);
	*end_seconds = telemetry_seconds(flight_end);
	return true;
}

int EditModel::clips_with_changed_telemetry(const EditModel& before, const EditModel& after) {
	const double TOLERANCE_SECONDS = 0.0005;
	const EditList& list = before.edit_list();
	if (before.media_count() != after.media_count())
		return list.count();

	int changed = 0;
	for (int i = 0; i < list.count(); i++) {
		const Clip& clip = list.clip(i);
		// Telemetry time is a straight line between sync points, so it differs somewhere in the
		// clip only if it differs at an end of the clip or at a point where a line bends.
		std::vector<int64_t> samples;
		samples.push_back(clip.in_ticks);
		samples.push_back(clip.out_ticks);
		const EditModel* models[2] = { &before, &after };
		for (int m = 0; m < 2; m++) {
			for (size_t a = 0; a < models[m]->m_resolved.size(); a++) {
				int64_t flight = models[m]->seconds_to_ticks(models[m]->m_resolved[a].flight_seconds);
				int64_t local = flight - models[m]->m_capture.flight_start(clip.media);
				if (local > clip.in_ticks && local < clip.out_ticks)
					samples.push_back(local);
			}
		}
		bool differs = false;
		for (size_t s = 0; s < samples.size() && !differs; s++) {
			FileTime time(clip.media, samples[s]);
			double seconds_before = before.telemetry_seconds(before.flight_seconds(time));
			double seconds_after = after.telemetry_seconds(after.flight_seconds(time));
			if (fabs(seconds_before - seconds_after) > TOLERANCE_SECONDS)
				differs = true;
		}
		if (differs)
			changed++;
	}
	return changed;
}
