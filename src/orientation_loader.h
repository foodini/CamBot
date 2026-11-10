#pragma once

#include "camera_metadata.h"
#include "tracking_path.h"
#include <map>
#include <string>

// The camera's orientation through each media file, for the paths (see tracking_path.h), read from the files' djmd streams (see
// dji_orientation.h). A file is read the first time it is asked for: its packets come in on a thread of their own (CameraPackets),
// and when they are all in, update() turns them into orientations a few at a time, so the program never stops for it. Only the
// orientations are kept after that. A file with none (not from a DJI camera, say) is remembered as having none, and paths on it
// work from the places in the picture.
class OrientationLoader {
public:
	OrientationLoader();

	// Asks for the orientations of `media`, a file at `path` whose ticks are `seconds_per_tick` long, and a start at `start_seconds`
	// into it (where the playhead is, as that is what is wanted first). Does nothing if it is read already or being read; if another
	// file is being read, that is given up on (to be asked for again when it is needed).
	void request(int media, const std::string& path, double seconds_per_tick, double start_seconds);
	// Call once a frame: takes the next few packets apart.
	void update();
	// Forgets everything (new media, or a new project).
	void clear();

	// The orientations of `media`: always something, empty until they are in (and for a file that has none).
	const tracking::CameraOrientations& orientations(int media) const;
	// Whether `media` is still being read, and how far it has got, 0 to 1.
	bool  loading(int media) const { return m_loading && m_loading_media == media; }
	// Whether any file is being read.
	bool  busy() const { return m_loading; }
	float progress() const;
	// Whether `media` has been read (whether or not it had orientations), and whether it has any.
	bool  is_read(int media) const { return m_finished.find(media) != m_finished.end(); }
	bool  has_orientations(int media) const;

private:
	struct Finished {
		tracking::CameraOrientations orientations;
		Finished() {}
	};
	std::map<int, Finished>      m_finished;
	CameraPackets                m_packets;
	bool                         m_loading;
	int                          m_loading_media;
	double                       m_seconds_per_tick;
	size_t                       m_taken;              // How many of the packets (in the order of time) have been turned into orientations.
	tracking::CameraOrientations m_building;
	tracking::CameraOrientations m_none;
};
