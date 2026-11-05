#include "audio_timeline.h"

#include <string.h>

AudioTimeline::AudioTimeline() : m_position(0) {
}

AudioTimeline::~AudioTimeline() {
	close_readers();
}

void AudioTimeline::close_readers() {
	for (size_t i = 0; i < m_readers.size(); i++)
		delete m_readers[i];
	m_readers.clear();
	m_no_sound.clear();
}

void AudioTimeline::set_edit(const std::vector<std::string>& paths, const EditList& clips, double seconds_per_tick) {
	if (paths != m_paths) {
		close_readers();
		m_paths = paths;
		m_readers.assign(paths.size(), nullptr);
		m_no_sound.assign(paths.size(), 0);
	}
	audio_plan::plan(clips, seconds_per_tick, &m_segments);
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

AudioFileReader* AudioTimeline::reader_for(int media) {
	if (media < 0 || media >= (int)m_readers.size() || m_no_sound[media])
		return nullptr;
	if (m_readers[media] != nullptr)
		return m_readers[media];
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
	m_readers[media] = reader;
	return reader;
}

bool AudioTimeline::has_sound() {
	for (size_t i = 0; i < m_segments.size(); i++) {
		if (reader_for(m_segments[i].media) != nullptr)
			return true;
	}
	return false;
}

void AudioTimeline::render(float* out, int frames) {
	int done = 0;
	while (done < frames) {
		const int index = audio_plan::segment_at(m_segments, m_position);
		float* destination = out + (size_t)done * 2;
		if (index < 0) {
			// Past the end of the edit.
			memset(destination, 0, sizeof(float) * (size_t)(frames - done) * 2);
			m_position += frames - done;
			break;
		}
		const audio_plan::Segment& segment = m_segments[index];
		int64_t count = segment.timeline_end() - m_position;
		if (count > frames - done)
			count = frames - done;
		const int64_t offset = m_position - segment.timeline_start;

		int got = 0;
		AudioFileReader* reader = reader_for(segment.media);
		if (reader != nullptr) {
			const int64_t source = segment.source_start + offset;
			// Carries straight on from the last thing read when the edit does (a split, or the next block of the same clip).
			if (reader->position() != source)
				reader->seek(source);
			got = reader->read(destination, (int)count);
		}
		if (got < count)
			memset(destination + (size_t)got * 2, 0, sizeof(float) * (size_t)(count - got) * 2);

		// The fades at the ends of the clip.
		if (offset < audio_plan::FADE_SAMPLES || segment.length - offset - count < audio_plan::FADE_SAMPLES) {
			for (int64_t i = 0; i < count; i++) {
				const int64_t at = offset + i;
				if (at < audio_plan::FADE_SAMPLES || segment.length - at <= audio_plan::FADE_SAMPLES) {
					const float gain = audio_plan::fade_gain(segment, at);
					destination[i * 2] *= gain;
					destination[i * 2 + 1] *= gain;
				}
			}
		}
		done += (int)count;
		m_position += count;
	}
}
