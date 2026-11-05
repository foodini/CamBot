#include "audio_player.h"

AudioPlayer::AudioPlayer()
	: m_output_tried(false), m_output_ok(false), m_seconds_per_tick(1.0 / 90000.0), m_volume(1.0f), m_muted(false), m_gain(1.0f),
	  m_playing(false), m_start_sample(0), m_block((size_t)AudioOutput::BUFFER_FRAMES * 2) {
}

void AudioPlayer::set_edit(const std::vector<std::string>& paths, const EditList& clips, double seconds_per_tick) {
	m_seconds_per_tick = seconds_per_tick;
	m_timeline.set_edit(paths, clips, seconds_per_tick);
}

bool AudioPlayer::start(int64_t timeline_ticks) {
	if (!m_output_tried) {
		m_output_tried = true;
		m_output_ok = m_output.open();
	}
	if (!m_output_ok || !m_timeline.has_sound()) {
		stop();
		return false;
	}
	m_output.reset();
	m_timeline.seek(audio_plan::ticks_to_samples(timeline_ticks, m_seconds_per_tick));
	m_start_sample = m_timeline.position();
	m_gain = m_muted ? 0.0f : m_volume;   // From the start, not rising from whatever it was.
	m_playing = true;
	fill();
	return true;
}

void AudioPlayer::stop() {
	if (m_playing)
		m_output.reset();
	m_playing = false;
}

void AudioPlayer::update() {
	if (m_playing)
		fill();
}

void AudioPlayer::fill() {
	const int frames = AudioOutput::BUFFER_FRAMES;
	while (m_output.free_buffers() > 0) {
		m_timeline.render(m_block.data(), frames);
		// To the volume wanted, in a smooth ramp across the buffer so changing it makes no click.
		const float target = m_muted ? 0.0f : m_volume;
		if (target == m_gain) {
			if (target != 1.0f) {
				for (size_t i = 0; i < m_block.size(); i++)
					m_block[i] *= target;
			}
		} else {
			for (int i = 0; i < frames; i++) {
				const float gain = m_gain + (target - m_gain) * (float)(i + 1) / (float)frames;
				m_block[(size_t)i * 2] *= gain;
				m_block[(size_t)i * 2 + 1] *= gain;
			}
			m_gain = target;
		}
		if (!m_output.submit(m_block.data()))
			break;
	}
}

int64_t AudioPlayer::position_ticks() {
	if (!m_playing)
		return -1;
	return audio_plan::samples_to_ticks(m_start_sample + m_output.played_frames(), m_seconds_per_tick);
}
