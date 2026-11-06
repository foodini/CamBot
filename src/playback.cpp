#include "playback.h"

#include <chrono>

namespace {

double now_seconds() {
	return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

Playback::Playback(MediaContainerMgr* media)
	: m_media(media), m_running(false), m_has_sound(false), m_wall_start(0.0), m_ticks_start(0), m_last_shown(0) {
}

void Playback::set_sound(const SoundSettings& settings) {
	m_audio.set_volume(settings.volume);
	m_audio.set_muted(settings.muted);
}

void Playback::start(int64_t ticks) {
	std::vector<std::string> paths;
	for (int i = 0; i < m_media->media_count(); i++)
		paths.push_back(m_media->media_path(i));
	const AVRational tick = m_media->tick_time_base();
	m_clips = m_media->edit_list().clips();
	m_audio.set_edit(paths, m_media->edit_list(), (double)tick.num / (double)tick.den);
	m_has_sound = m_audio.start(ticks);
	m_wall_start = now_seconds();
	m_ticks_start = ticks;
	m_last_shown = ticks;
	m_running = true;
}

void Playback::stop() {
	m_audio.stop();
	m_running = false;
}

bool Playback::update(bool paused) {
	if (paused) {
		if (m_running)
			stop();
		return true;
	}

	const int64_t shown = m_media->current_ticks();
	// Begin from wherever the picture is. That is also what happens when it has been moved since we last put it somewhere (a click
	// on the timeline, a step) or the edit has changed under us.
	if (!m_running || shown != m_last_shown || m_media->edit_list().clips() != m_clips)
		start(shown);
	m_audio.update();

	int64_t target;
	if (m_has_sound) {
		target = m_audio.position_ticks();
		if (target < 0)
			target = shown;
	} else {
		const AVRational tick = m_media->tick_time_base();
		const double seconds_per_tick = (double)tick.num / (double)tick.den;
		target = m_ticks_start + (int64_t)((now_seconds() - m_wall_start) / seconds_per_tick);
	}

	if (target >= m_media->total_ticks()) {
		m_media->advance_to(m_media->total_ticks());   // Lands on the last frame.
		m_last_shown = m_media->current_ticks();
		stop();
		return false;
	}
	// The newest frame that is not after the clock. (Where a clip is slowed down the same frame stays up for several times its
	// length, and where it is sped up several go by in the time of one; show_at() knows, and does nothing when the picture is right.)
	m_media->show_at(target);
	m_last_shown = m_media->current_ticks();
	return true;
}
