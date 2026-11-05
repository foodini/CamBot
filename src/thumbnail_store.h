#pragma once

// The small pictures of the video that fill the clips on the timeline. A second decoder, on a
// thread of its own, gets them from the files, so playing, scrubbing and exporting are never held up
// by it; this class turns what comes back into OpenGL textures and says which one to draw for a
// moment of a file (see thumbnail_layout.h for where on a clip they go and what they show). There are
// no callbacks: each frame the timeline says which pictures it wants (want), and what has arrived is
// taken at the start of the next one.
//
// A picture is the keyframe of the file at or before the moment asked for, which is quick to get and
// can be used again, apart from the first picture of a clip, which is the exact frame (that is the one
// the user is cutting around). While the right picture is on its way, the nearest one of the same file
// that is already here stands in, so zooming and scrolling blur for a moment instead of going blank.
//
// All of it is for the main thread (the one with the OpenGL context), except the decoding.

#include <condition_variable>
#include <mutex>
#include <stdint.h>
#include <string>
#include <thread>
#include <vector>

#include "thumbnail_layout.h"

class ThumbnailStore {
public:
	ThumbnailStore();
	~ThumbnailStore();

	// Starts getting pictures from these files, numbered the way the edit model numbers its media. `width` and
	// `height` are the video's, and `ticks_per_second` is how many ticks of the edit model make a second. Once.
	void open(const std::vector<std::string>& paths, int width, int height, double ticks_per_second);

	// Width over height of the video: what shape the pictures are.
	float aspect() const { return m_aspect; }

	// While paused (an export in progress needs the machine to itself), nothing new is decoded.
	void set_paused(bool paused);

	// Each frame: begin_frame(), then want() for every picture the timeline shows, then end_frame().
	void begin_frame();
	// The OpenGL texture to draw for this moment of this file (`local_ticks` from the start of the file), or 0 if
	// there is nothing to show yet (not even a stand-in). `exact` asks for that frame and not just the keyframe before it.
	unsigned int want(int media, int64_t local_ticks, bool exact);
	// Requests that were not wanted this frame and haven't started are dropped, and pictures nobody has used
	// for a while are deleted.
	void end_frame();

private:
	struct Queued {
		thumbnails::ThumbKey key;
		bool                 wanted;
	};
	struct Result {
		thumbnails::ThumbKey key;
		bool                 ok;
		int                  width;
		int                  height;
		std::vector<unsigned char> pixels;   // RGBA.
		Result() : key(), ok(false), width(0), height(0), pixels() {}
	};

	void worker_main(std::vector<std::string> paths);
	bool is_queued_or_known(const thumbnails::ThumbKey& key);   // Needs m_mutex held.

	// The picture size: a fixed height, as wide as the video's shape makes it.
	int                      m_picture_width;
	int                      m_picture_height;
	float                    m_aspect;
	int64_t                  m_fallback_step;      // Ticks: a second.
	int64_t                  m_frame;              // Frames drawn, to tell what was used lately.
	bool                     m_opened;

	thumbnails::Catalog      m_catalog;            // Main thread only.

	std::mutex               m_mutex;
	std::condition_variable  m_wake;
	std::vector<Queued>      m_queue;
	std::vector<Result>      m_results;
	std::vector<std::vector<int64_t> > m_keyframes;   // Per file, as the decoder found them.
	std::vector<char>        m_keyframes_ready;
	std::vector<thumbnails::ThumbKey> m_failed;       // Not asked for again.
	bool                     m_busy;
	thumbnails::ThumbKey     m_busy_key;
	bool                     m_paused;
	bool                     m_stop;
	std::thread              m_thread;
};
