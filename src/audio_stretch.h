#pragma once

// Playing sound faster or slower than it was recorded (a clip's speed, see edit_model.h). Two ways:
//
//   * resample(): the sound is read at another rate, like a tape or a record played at the wrong speed, so its pitch moves with the
//     speed. Faster than normal, each output sample is the average of the stretch of the original it covers, which keeps the high
//     notes that the speed-up would otherwise fold down into the audible range as hiss.
//
//   * PitchKeeper: the sound is cut into short overlapping pieces (grains of about 30 ms) which are laid down again closer together
//     or further apart, so it lasts a different time but every piece still sounds at its own pitch. Where each piece is cut from is
//     nudged a few milliseconds, to the place its waveform lines up best with the piece before it, so the joins don't buzz (this
//     is called WSOLA, "waveform similarity overlap-add"). It works in the time domain, with no Fourier transform. It does well on
//     speech and wind; far from normal speed (a tenth, or sixteen times) it can only be what it is, a stuttering imitation.
//
// All of it is stereo, 32-bit float, at audio_plan::SAMPLE_RATE, counted in sample frames. Not thread-safe.

#include <stdint.h>
#include <vector>

#include "audio_file_reader.h"

namespace audio_stretch {

// A file's sound read at positions that mostly move forward, with a little memory behind, so a stretcher can look at the same
// stretch of sound again and again without the file being read again. Anything before the start of the sound or past its end is
// silence.
class SourceWindow {
public:
	SourceWindow();
	// Where the sound comes from (null for none: silence). The reader must outlive this.
	void set_reader(AudioFileReader* reader);
	// Copies `count` frames beginning at frame `start` of the file's sound to `out`.
	void get(int64_t start, int count, float* out);

private:
	AudioFileReader*   m_reader;
	std::vector<float> m_buffer;          // Interleaved frames m_first .. m_first + m_frames - 1.
	int64_t            m_first;
	int64_t            m_frames;
};

// Reads `frames` output frames of `source` played at `speed` times normal speed, without keeping the pitch: output frame n is
// the sound at frame `source_start + n * speed`, where n counts from `first_output`.
void resample(SourceWindow* source, int64_t source_start, double speed, int64_t first_output, int frames, float* out);

// Stretches sound in time without changing its pitch: output frame n comes from about frame `source_start + n * speed`.
class PitchKeeper {
public:
	PitchKeeper();

	// Starts (or restarts) a stretch whose next output is frame `first_output` of it. `source` must stay valid while this is used.
	void start(SourceWindow* source, int64_t source_start, double speed, int64_t first_output);
	// The output frame the next render() gives.
	int64_t next_output() const { return m_next_output; }
	bool    started() const { return m_source != nullptr; }
	// The next `frames` output frames. Calls carry on from one another.
	void render(float* out, int frames);

	// The size of a grain, and the step between grains (half a grain), in frames.
	enum { GRAIN = 1536, HOP = 768 };

private:
	void    make_block();
	int64_t nominal_start(int64_t block) const;
	int64_t choose_start(int64_t block);
	void    add_grain(int64_t grain_start, bool first_of_segment);
	void    shift_accumulator();

	SourceWindow*      m_source;
	int64_t            m_source_start;
	double             m_speed;
	int64_t            m_next_output;     // The output frame the next render() gives.
	int64_t            m_next_block;      // The next block (HOP frames of output) to make.
	int                m_ready_pos;       // How much of m_ready has been given out (HOP: all of it).
	int64_t            m_previous_start;  // Where the last grain came from in the source, if there was one.
	bool               m_have_previous;
	std::vector<float> m_accumulator;     // GRAIN frames of output being built, stereo interleaved.
	std::vector<float> m_ready;           // HOP frames, the block now being given out.
	std::vector<float> m_window;          // The grain's shape.
	std::vector<float> m_grain;
	std::vector<float> m_search;
	std::vector<float> m_reference;
	std::vector<float> m_mono_search;
	std::vector<float> m_mono_reference;
};

}  // namespace audio_stretch
