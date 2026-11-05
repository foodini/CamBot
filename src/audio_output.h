#pragma once

// The speakers. Sound is handed over in fixed-size buffers (BUFFER_FRAMES stereo frames of 32-bit float each), and the device
// says how many frames it has played so far, which is what keeps the picture in step with what is heard. Nothing calls back: the
// program asks (free_buffers(), played_frames()) when it wants to know, once a frame.
//
// On Windows this is waveOut (winmm), which every version has. Elsewhere there is no device and open() says so, so the rest
// of the program still builds and runs, in silence.

#include <stdint.h>

class AudioOutput {
public:
	enum {
		BUFFER_FRAMES = 1200,   // 25 ms at 48 kHz.
		BUFFER_COUNT  = 8       // So up to 200 ms is queued: enough for the program to be busy for a moment without a gap.
	};

	AudioOutput();
	~AudioOutput();

	// Opens the default device for 48 kHz stereo. False if there isn't one (or it won't open).
	bool open();
	void close();
	bool is_open() const { return m_state != nullptr; }

	// How many more buffers can be queued right now.
	int  free_buffers();
	// Queues BUFFER_FRAMES frames (interleaved left, right; values beyond -1..1 are clipped). Sound starts playing as soon as the
	// first buffer is queued, and carries on from one buffer to the next. False if there was no room or the device refused.
	bool submit(const float* frames);
	// How many frames the device has played since open() or the last reset().
	int64_t played_frames();
	// Stops, throws away whatever is queued and starts counting played frames from 0 again.
	void reset();

private:
	AudioOutput(const AudioOutput&);
	AudioOutput& operator=(const AudioOutput&);

	struct State;
	State* m_state;
};
