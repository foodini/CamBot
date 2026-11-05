#pragma once

// What the sound of an edit is made of, as plain arithmetic (no files, no FFmpeg): which stretch of which file's sound plays
// where on the timeline, and the short fades that stop a cut from clicking. audio_timeline.h does the playing.
//
// Everything here is counted in samples at SAMPLE_RATE, the one rate the program mixes and exports in, with a sample 0 that is
// the start of the timeline (or of a file's sound).

#include <stdint.h>
#include <vector>

#include "edit_model.h"

namespace audio_plan {

const int SAMPLE_RATE = 48000;
// How long the fade at a cut is, each side of it (10 ms: too short to hear as a dip, long enough to take the click out).
const int FADE_SAMPLES = 480;

// One clip's worth of sound.
struct Segment {
	int     media;
	int64_t timeline_start;   // Where it begins on the timeline.
	int64_t length;
	int64_t source_start;     // Where in the file's sound it comes from.
	bool    fade_in;          // False where the sound carries straight on from the clip before (a split that wasn't trimmed).
	bool    fade_out;
	Segment() : media(0), timeline_start(0), length(0), source_start(0), fade_in(true), fade_out(true) {}
	int64_t timeline_end() const { return timeline_start + length; }
};

// Ticks (of the edit's time base, `seconds_per_tick` long) to samples, to the nearest sample.
int64_t ticks_to_samples(int64_t ticks, double seconds_per_tick);

// Samples back to ticks, to the nearest tick.
int64_t samples_to_ticks(int64_t samples, double seconds_per_tick);

// The sound of `clips`, one segment per clip, tiling the timeline exactly (each begins where the one before ends).
// A cut that joins two stretches of the same file with nothing missing between gets no fade, as there is no cut to hear.
void plan(const EditList& clips, double seconds_per_tick, std::vector<Segment>* segments);

// The volume (0 to 1) `offset` samples into `segment`: a smooth rise over the first FADE_SAMPLES if it fades in, a smooth
// fall over the last if it fades out. A segment too short for both fades shares what it has.
float fade_gain(const Segment& segment, int64_t offset);

// The index of the segment holding timeline sample `sample`, or -1 if it is before the first or past the last.
int segment_at(const std::vector<Segment>& segments, int64_t sample);

}  // namespace audio_plan
