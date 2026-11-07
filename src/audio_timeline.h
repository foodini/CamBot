#pragma once

// The sound of an edit, played out as samples: each clip's stretch of its file's sound, one after another, with the short fades
// at the cuts (audio_plan.h). The same engine feeds the export's audio encoder and, later, the speakers.
//
// Not thread-safe: whoever uses it from more than one thread has to lock around the calls.

#include <stdint.h>
#include <string>
#include <vector>

#include "audio_file_reader.h"
#include "audio_plan.h"
#include "audio_stretch.h"
#include "edit_model.h"

class AudioTimeline {
public:
	AudioTimeline();
	~AudioTimeline();

	// Sets what is played: the media files (by number, as the clips name them) and the edit. Files already open stay open when
	// the files are the same ones as before. Leaves the position where it was (kept inside the new length).
	void set_edit(const std::vector<std::string>& paths, const EditList& clips, double seconds_per_tick);

	int64_t total_samples() const;
	// The sample the next render() begins at.
	int64_t position() const { return m_position; }
	void    seek(int64_t sample);

	// Writes the next `frames` frames (a left and a right float each) of the edit to `out` and moves on. A clip from a file with
	// no sound, or anything past the end of the edit, comes out as silence.
	void render(float* out, int frames);

	// Whether any clip of the edit has sound to play (this opens the files to find out).
	bool has_sound();

	// The first thing that went wrong opening a file's sound (other than it having none), for the user; empty if nothing has.
	const std::string& last_error() const { return m_last_error; }

private:
	// A file's sound is read through one of two readers, so that two clips that are crossfaded, which can be from the same file,
	// each carry straight on from where they were instead of both seeking back and forth every block. Clips that are crossfaded
	// use one or the other by their number, odd or even, so neighbours differ; the others use the first.
	AudioFileReader* reader_for(int media, int slot);
	audio_stretch::SourceWindow* window_for(int media, int slot);
	void close_readers();
	// Writes `count` frames of segment `index`, from timeline sample `position` (inside it), with its fades, to `destination`.
	void render_segment(int index, int64_t position, int count, float* destination);

	std::vector<std::string>         m_paths;
	std::vector<audio_plan::Segment> m_segments;
	audio_plan::EdgeFades            m_edges;        // The fade in and fade out of the whole edit.
	std::vector<AudioFileReader*>    m_readers;      // By slot * media count + media; null until needed.
	std::vector<char>                m_no_sound;     // By media: 1 for a file that has none (or won't open), tried once.
	std::vector<audio_stretch::SourceWindow*> m_windows;   // The same way; null until a clip at another speed needs one.
	audio_stretch::PitchKeeper       m_keepers[2];   // For the clips being stretched, if any (one for each slot).
	int                              m_keeper_segments[2];   // The segment each is working on, or -1.
	std::vector<float>               m_second;       // The second clip of a crossfade, rendered here to be added to the first.
	int64_t                          m_position;
	std::string                      m_last_error;
};
