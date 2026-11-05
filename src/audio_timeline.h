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
	AudioFileReader* reader_for(int media);
	void close_readers();

	std::vector<std::string>         m_paths;
	std::vector<audio_plan::Segment> m_segments;
	std::vector<AudioFileReader*>    m_readers;      // By media; null until needed.
	std::vector<char>                m_no_sound;     // By media: 1 for a file that has none (or won't open), tried once.
	int64_t                          m_position;
	std::string                      m_last_error;
};
