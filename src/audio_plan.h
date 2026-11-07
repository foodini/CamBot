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
	double  speed;            // Samples of the file's sound used per sample of the timeline (1 for normal speed).
	int     mode;             // An AudioMode: what is done with the sound if the speed isn't 1.
	bool    fade_in;          // False where the sound carries straight on from the clip before (a split that wasn't trimmed).
	bool    fade_out;
	// Where the clip is crossfaded with the one before it / after it: the sound rises from nothing over the first `xfade_in` samples
	// and falls to nothing over the last `xfade_out`, while the other clip's does the opposite, so the two (which overlap there on the
	// timeline) add up to a steady loudness (see crossfade_gain). 0 for a cut. A segment that crossfades at an end has no fade_in /
	// fade_out at that end.
	int64_t xfade_in;
	int64_t xfade_out;
	Segment() : media(0), timeline_start(0), length(0), source_start(0), speed(1.0), mode(AUDIO_KEEP_PITCH), fade_in(true), fade_out(true),
	            xfade_in(0), xfade_out(0) {}
	int64_t timeline_end() const { return timeline_start + length; }
};

// Ticks (of the edit's time base, `seconds_per_tick` long) to samples, to the nearest sample.
int64_t ticks_to_samples(int64_t ticks, double seconds_per_tick);

// Samples back to ticks, to the nearest tick.
int64_t samples_to_ticks(int64_t samples, double seconds_per_tick);

// The sound of `clips`, one segment per clip. They tile the timeline exactly (each begins where the one before ends) except where
// two clips are crossfaded: there the later one begins before the earlier ends, by the length of the crossfade.
// A cut that joins two stretches of the same file with nothing missing between gets no fade, as there is no cut to hear. (That
// goes for clips played at the same speed, and not for sound that is stretched without changing its pitch, which is made afresh
// for each clip and so doesn't join up.)
void plan(const EditList& clips, double seconds_per_tick, std::vector<Segment>* segments);

// The volume (0 to 1) `offset` samples into `segment`: a smooth rise over the first FADE_SAMPLES if it fades in, a smooth
// fall over the last if it fades out. A segment too short for both fades shares what it has.
float fade_gain(const Segment& segment, int64_t offset);

// The fades at the start and the end of the whole edit (the transitions at seams 0 and count, see EditList), in samples: the sound
// rises from nothing over the first `in_samples` and falls to nothing over the last `out_samples` (0: no fade).
struct EdgeFades {
	int64_t in_samples;
	int64_t out_samples;
	EdgeFades() : in_samples(0), out_samples(0) {}
};

EdgeFades edge_fades(const EditList& clips, double seconds_per_tick);

// The volume (0 to 1) at timeline sample `sample` of an edit `total_samples` long: a straight line up over the first in_samples and
// down over the last out_samples, the same shape as the fade of the picture (EditList::fade_level).
float edge_gain(const EdgeFades& fades, int64_t total_samples, int64_t sample);

// The volume (0 to 1) `offset` samples into `segment` that its crossfades leave: a quarter of a sine wave up over the first xfade_in
// samples and the matching quarter of a cosine down over the last xfade_out, so that where two segments overlap their volumes'
// squares add up to 1 (the sound's power stays level) and are the same at the same moment.
float crossfade_gain(const Segment& segment, int64_t offset);

// The index of a segment holding timeline sample `sample`, or -1 if it is before the first or past the last. (Where two overlap,
// either; see covering().)
int segment_at(const std::vector<Segment>& segments, int64_t sample);

// The segments holding timeline sample `sample`: none (0), one (1, in *first), or two crossfading (2, the earlier in *first and the
// later in *second). Whichever are not there are -1.
int covering(const std::vector<Segment>& segments, int64_t sample, int* first, int* second);

}  // namespace audio_plan
