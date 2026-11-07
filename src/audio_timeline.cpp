#include "audio_timeline.h"

#include <string.h>

AudioTimeline::AudioTimeline() : m_position(0) {
	m_keeper_segments[0] = -1;
	m_keeper_segments[1] = -1;
}

AudioTimeline::~AudioTimeline() {
	close_readers();
}

void AudioTimeline::close_readers() {
	for (size_t i = 0; i < m_windows.size(); i++)
		delete m_windows[i];
	m_windows.clear();
	for (size_t i = 0; i < m_readers.size(); i++)
		delete m_readers[i];
	m_readers.clear();
	m_no_sound.clear();
	m_keeper_segments[0] = -1;
	m_keeper_segments[1] = -1;
}

void AudioTimeline::set_edit(const std::vector<std::string>& paths, const EditList& clips, double seconds_per_tick) {
	if (paths != m_paths) {
		close_readers();
		m_paths = paths;
		m_readers.assign(paths.size() * 2, nullptr);
		m_no_sound.assign(paths.size(), 0);
		m_windows.assign(paths.size() * 2, nullptr);
	}
	audio_plan::plan(clips, seconds_per_tick, &m_segments);
	m_edges = audio_plan::edge_fades(clips, seconds_per_tick);
	m_keeper_segments[0] = -1;
	m_keeper_segments[1] = -1;
	if (m_position > total_samples())
		m_position = total_samples();
}

int64_t AudioTimeline::total_samples() const {
	return m_segments.empty() ? 0 : m_segments.back().timeline_end();
}

void AudioTimeline::seek(int64_t sample) {
	if (sample < 0)
		sample = 0;
	if (sample > total_samples())
		sample = total_samples();
	m_position = sample;
}

AudioFileReader* AudioTimeline::reader_for(int media, int slot) {
	if (media < 0 || media >= (int)m_no_sound.size() || m_no_sound[media])
		return nullptr;
	const size_t at = (size_t)slot * m_no_sound.size() + (size_t)media;
	if (m_readers[at] != nullptr)
		return m_readers[at];
	AudioFileReader* reader = new AudioFileReader();
	std::string error;
	if (!reader->open(m_paths[media], &error)) {
		delete reader;
		m_no_sound[media] = 1;
		// Not having sound is not worth a message; a file that won't open is.
		if (m_last_error.empty() && error.find("has no sound") == std::string::npos)
			m_last_error = error;
		return nullptr;
	}
	m_readers[at] = reader;
	return reader;
}

audio_stretch::SourceWindow* AudioTimeline::window_for(int media, int slot) {
	AudioFileReader* reader = reader_for(media, slot);
	if (reader == nullptr)
		return nullptr;
	const size_t at = (size_t)slot * m_no_sound.size() + (size_t)media;
	if (m_windows[at] == nullptr) {
		m_windows[at] = new audio_stretch::SourceWindow();
		m_windows[at]->set_reader(reader);
	}
	return m_windows[at];
}

bool AudioTimeline::has_sound() {
	for (size_t i = 0; i < m_segments.size(); i++) {
		if (reader_for(m_segments[i].media, 0) != nullptr)
			return true;
	}
	return false;
}

void AudioTimeline::render_segment(int index, int64_t position, int count, float* destination) {
	const audio_plan::Segment& segment = m_segments[index];
	// Only clips that are crossfaded need a reader of their own (alternating with the neighbour's); the rest, a split's two halves
	// included, share one and carry straight on.
	const int slot = (segment.xfade_in > 0 || segment.xfade_out > 0) ? (index & 1) : 0;
	const int64_t offset = position - segment.timeline_start;

	int got = 0;
	AudioFileReader* reader = reader_for(segment.media, slot);
	if (segment.speed != 1.0 && segment.mode == AUDIO_MUTE) {
		got = 0;       // Silence (below).
	} else if (segment.speed != 1.0 && reader != nullptr) {
		// Played at another speed: the sound is made from the file's, not simply read from it.
		audio_stretch::SourceWindow* window = window_for(segment.media, slot);
		if (segment.mode == AUDIO_CHANGE_PITCH) {
			audio_stretch::resample(window, segment.source_start, segment.speed, offset, count, destination);
		} else {
			if (m_keeper_segments[slot] != index || m_keepers[slot].next_output() != offset) {
				m_keepers[slot].start(window, segment.source_start, segment.speed, offset);
				m_keeper_segments[slot] = index;
			}
			m_keepers[slot].render(destination, count);
		}
		got = count;
	} else if (reader != nullptr) {
		const int64_t source = segment.source_start + offset;
		// Carries straight on from the last thing read when the edit does (a split, or the next block of the same clip).
		if (reader->position() != source)
			reader->seek(source);
		got = reader->read(destination, count);
	}
	if (got < count)
		memset(destination + (size_t)got * 2, 0, sizeof(float) * (size_t)(count - got) * 2);

	// The fades at the ends of the clip: the short ones at a cut, and the long ones of a crossfade.
	if (offset < audio_plan::FADE_SAMPLES || segment.length - offset - count < audio_plan::FADE_SAMPLES) {
		for (int i = 0; i < count; i++) {
			const int64_t at = offset + i;
			if (at < audio_plan::FADE_SAMPLES || segment.length - at <= audio_plan::FADE_SAMPLES) {
				const float gain = audio_plan::fade_gain(segment, at);
				destination[i * 2] *= gain;
				destination[i * 2 + 1] *= gain;
			}
		}
	}
	if ((segment.xfade_in > 0 && offset < segment.xfade_in) ||
			(segment.xfade_out > 0 && offset + count > segment.length - segment.xfade_out)) {
		for (int i = 0; i < count; i++) {
			const float gain = audio_plan::crossfade_gain(segment, offset + i);
			destination[i * 2] *= gain;
			destination[i * 2 + 1] *= gain;
		}
	}
}

void AudioTimeline::render(float* out, int frames) {
	int done = 0;
	while (done < frames) {
		int first = -1;
		int second = -1;
		const int covered = audio_plan::covering(m_segments, m_position, &first, &second);
		float* destination = out + (size_t)done * 2;
		if (covered == 0) {
			// Past the end of the edit.
			memset(destination, 0, sizeof(float) * (size_t)(frames - done) * 2);
			m_position += frames - done;
			break;
		}
		// As far as nothing changes: until one of the segments ends, or another begins.
		const int last = covered == 2 ? second : first;
		int64_t count = m_segments[first].timeline_end() - m_position;
		if (covered == 2 && m_segments[second].timeline_end() - m_position < count)
			count = m_segments[second].timeline_end() - m_position;
		if (last + 1 < (int)m_segments.size() && m_segments[last + 1].timeline_start - m_position < count)
			count = m_segments[last + 1].timeline_start - m_position;
		if (count > frames - done)
			count = frames - done;
		if (count < 1)
			count = 1;

		render_segment(first, m_position, (int)count, destination);
		if (covered == 2) {
			// The other clip of a crossfade, added to the first.
			if (m_second.size() < (size_t)count * 2)
				m_second.resize((size_t)count * 2);
			render_segment(second, m_position, (int)count, m_second.data());
			for (int64_t i = 0; i < count * 2; i++)
				destination[i] += m_second[(size_t)i];
		}

		// The fade in and out of the whole edit, if the block touches either.
		const int64_t total = total_samples();
		if ((m_edges.in_samples > 0 && m_position < m_edges.in_samples) ||
				(m_edges.out_samples > 0 && m_position + count > total - m_edges.out_samples)) {
			for (int64_t i = 0; i < count; i++) {
				const float gain = audio_plan::edge_gain(m_edges, total, m_position + i);
				destination[i * 2] *= gain;
				destination[i * 2 + 1] *= gain;
			}
		}
		done += (int)count;
		m_position += count;
	}
}
