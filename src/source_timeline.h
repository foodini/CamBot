#pragma once

// The source timeline: any number of video files (the pieces a camera cuts one flight into)
// presented as one continuous video on a single global clock. Time on that clock is counted in
// ticks of the first file's video-stream time base, as a 64-bit integer (floats get coarse over
// multi-hour footage). A file starts where the previous one ended; there is no gap between them.
//
// This is just the bookkeeping. MediaDecoder (media_decoder.h) does the opening, seeking and
// decoding, and asks this class which file a global time lands in. Kept free of FFmpeg so it can
// be tested on its own.

#include <stdint.h>
#include <string>
#include <vector>

struct SourceFile {
	std::string path;
	int64_t     duration_ticks;  // How long the file is, in global ticks.
	int64_t     start_ticks;     // Where the file begins on the global clock.
};

class SourceTimeline {
public:
	SourceTimeline() : m_total_ticks(0) {}

	void clear() {
		m_files.clear();
		m_total_ticks = 0;
	}

	// Appends a file after the last one.
	void add_file(const std::string& path, int64_t duration_ticks) {
		SourceFile file;
		file.path = path;
		file.duration_ticks = duration_ticks;
		file.start_ticks = m_total_ticks;
		m_files.push_back(file);
		m_total_ticks += duration_ticks;
	}

	int                file_count() const         { return (int)m_files.size(); }
	const SourceFile&  file(int index) const      { return m_files[index]; }
	int64_t            total_ticks() const        { return m_total_ticks; }

	// The file a global time falls in. Times before the start give the first file, and times at
	// or past the end give the last. Only meaningful if there's at least one file.
	int file_index_for(int64_t global_ticks) const {
		int low = 0;
		int high = (int)m_files.size() - 1;
		// Last file whose start is at or before global_ticks.
		while (low < high) {
			int middle = low + (high - low + 1) / 2;
			if (m_files[middle].start_ticks <= global_ticks)
				low = middle;
			else
				high = middle - 1;
		}
		return low;
	}

	int64_t to_local(int file_index, int64_t global_ticks) const  { return global_ticks - m_files[file_index].start_ticks; }
	int64_t to_global(int file_index, int64_t local_ticks) const  { return local_ticks + m_files[file_index].start_ticks; }

private:
	std::vector<SourceFile> m_files;
	int64_t                 m_total_ticks;
};
