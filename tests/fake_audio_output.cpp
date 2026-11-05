// A stand-in for AudioOutput (audio_output.h): speakers that play only when the test says so. Whatever is submitted is kept, so the
// test can look at it, and fake_output_play() lets that many frames "come out", oldest buffer first. There is one set of speakers
// for the whole test, which fake_output_reset_all() puts back to new.
#include "audio_output.h"

#include <deque>
#include <vector>

struct AudioOutput::State {
	int unused;
};

static bool g_can_open = true;
static bool g_is_open = false;
static int g_resets = 0;
static int g_queued = 0;                  // Buffers submitted and not yet played right through.
static int64_t g_into_first = 0;          // Frames of the oldest queued buffer already played.
static int64_t g_played = 0;
static std::vector<float> g_submitted;    // Everything submitted since the last reset(), in order.

void fake_output_reset_all(bool can_open) {
	g_can_open = can_open;
	g_resets = 0;
	g_queued = 0;
	g_into_first = 0;
	g_played = 0;
	g_submitted.clear();
}
int fake_output_reset_count() { return g_resets; }
const std::vector<float>& fake_output_submitted() { return g_submitted; }
int fake_output_queued() { return g_queued; }

// Lets `frames` frames be played.
void fake_output_play(int64_t frames) {
	while (frames > 0 && g_queued > 0) {
		const int64_t left_in_first = AudioOutput::BUFFER_FRAMES - g_into_first;
		const int64_t step = frames < left_in_first ? frames : left_in_first;
		g_into_first += step;
		g_played += step;
		frames -= step;
		if (g_into_first == AudioOutput::BUFFER_FRAMES) {
			g_into_first = 0;
			g_queued--;
		}
	}
}

AudioOutput::AudioOutput() : m_state(nullptr) {
}
AudioOutput::~AudioOutput() {
	close();
}
bool AudioOutput::open() {
	if (!g_can_open)
		return false;
	g_is_open = true;
	if (m_state == nullptr)
		m_state = new State();
	return true;
}
void AudioOutput::close() {
	delete m_state;
	m_state = nullptr;
	g_is_open = false;
}
int AudioOutput::free_buffers() {
	return g_is_open ? BUFFER_COUNT - g_queued : 0;
}
bool AudioOutput::submit(const float* frames) {
	if (free_buffers() == 0)
		return false;
	g_queued++;
	g_submitted.insert(g_submitted.end(), frames, frames + BUFFER_FRAMES * 2);
	return true;
}
int64_t AudioOutput::played_frames() {
	return g_played;
}
void AudioOutput::reset() {
	g_resets++;
	g_queued = 0;
	g_into_first = 0;
	g_played = 0;
	g_submitted.clear();
}
