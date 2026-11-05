#pragma once

// Plays an edit's sound through the speakers, and says where on the timeline what is being heard has got to. While there is
// sound, that is the clock the picture follows (see playback.h).
//
// The program drives it from its main loop: start() to begin, update() once a frame to keep the speakers fed, position_ticks()
// to ask where we are, stop() to end. Nothing here runs on a thread of its own.

#include <stdint.h>
#include <string>
#include <vector>

#include "audio_output.h"
#include "audio_timeline.h"
#include "edit_model.h"

class AudioPlayer {
public:
	AudioPlayer();

	// The edit to play (see AudioTimeline::set_edit). A change takes effect from the next start().
	void set_edit(const std::vector<std::string>& paths, const EditList& clips, double seconds_per_tick);

	// How loud, 0 (nothing) to 1 (as recorded), and whether it is silenced. They take effect within a fraction of a second,
	// smoothly, and playing carries on while muted so the picture keeps its clock.
	void set_volume(float volume) { m_volume = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume); }
	void set_muted(bool muted)    { m_muted = muted; }

	// Starts playing from `timeline_ticks`. False (and nothing plays) if there are no speakers or none of the edit has sound,
	// in which case whoever is playing the picture has to keep time some other way.
	bool start(int64_t timeline_ticks);
	void stop();
	bool playing() const { return m_playing; }
	// Keeps the speakers fed. Call once a frame, at least, while playing.
	void update();
	// Where the sound coming out of the speakers now belongs on the timeline, in ticks. -1 if not playing.
	int64_t position_ticks();

private:
	void fill();

	AudioOutput        m_output;
	AudioTimeline      m_timeline;
	bool               m_output_tried;
	bool               m_output_ok;
	double             m_seconds_per_tick;
	float              m_volume;
	bool               m_muted;
	float              m_gain;          // What the last buffer ended at, for ramping to the next one.
	bool               m_playing;
	int64_t            m_start_sample;  // Timeline sample the speakers began at.
	std::vector<float> m_block;
};
