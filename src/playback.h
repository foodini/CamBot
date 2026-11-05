#pragma once

// Plays the video in real time. While there is sound the speakers are the clock (what is heard is what is on screen, to within a
// frame); with no sound, or no speakers, the computer's clock is. If the machine can't decode fast enough, frames are skipped
// to keep up, rather than everything slowing down.
//
// Exports don't use this: they step frame by frame, as fast as the machine goes (MediaContainerMgr::advance_frame()).

#include <stdint.h>
#include <vector>

#include "audio_player.h"
#include "edit_model.h"
#include "media_container_manager.h"
#include "user_settings.h"

class Playback {
public:
	// `media` must outlive this object.
	explicit Playback(MediaContainerMgr* media);

	// How loud, and whether silenced. Takes effect at once, to within the little that is already queued.
	void set_sound(const SoundSettings& settings);

	// Call once a frame. If `paused`, makes sure the speakers are quiet and does nothing else; otherwise moves the picture on to
	// where the clock says it should be. False when playing has run off the end of the video (the caller pauses).
	bool update(bool paused);

private:
	void start(int64_t ticks);
	void stop();

	MediaContainerMgr* m_media;
	AudioPlayer        m_audio;
	bool               m_running;
	bool               m_has_sound;      // The speakers are the clock.
	double             m_wall_start;     // For when they aren't: when we began, and the picture's time then.
	int64_t            m_ticks_start;
	int64_t            m_last_shown;     // The picture's time as we left it: if it isn't that, someone moved it.
	std::vector<Clip>  m_clips;          // The edit we are playing: if it isn't that, it was changed.
};
