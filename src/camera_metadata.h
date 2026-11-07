#pragma once

// The camera's own metadata, from the two data streams DJI cameras write into their video files next to the picture and the
// sound: "djmd" (one small protobuf message per video frame: orientation, the accelerometer, exposure, and GPS when the camera has
// a fix) and "dbgi" (the camera's debugging information, larger, and not documented anywhere). Both are read here only to be looked
// at, in a debug section of the left panel (see camera_panel.h), to find out what is in them and what is worth using. There is no
// published schema for the Osmo Action 6 (the files name theirs as dvtm_ac206.proto), so the messages are read without one (see
// proto_wire.h) and the few fields that are known are named from ExifTool's DJI tag list.
//
// Times here are seconds from the start of the file, which is how the video is counted too.

#include <atomic>
#include <mutex>
#include <stdint.h>
#include <string>
#include <thread>
#include <vector>

struct AVFormatContext;

namespace camera_metadata {

const int STREAM_DJMD = 0;
const int STREAM_DBGI = 1;
const int STREAM_COUNT = 2;

const char* stream_name(int stream);                        // "djmd" or "dbgi".
std::string path_text(const std::vector<int>& path);        // "3.2.9.1".
// What a field is, from ExifTool's tag list for the camera (a name ending in "?" is a guess from looking at the data); "" if
// nothing is known about it. `path` is the field numbers from the top of the message.
std::string field_name(int stream, const std::vector<int>& path);
// What `value` of an enumerated field means (the stabilization mode, say); "" if the field is not one or the value is not known.
std::string value_meaning(int stream, const std::vector<int>& path, int64_t value);

// A last field number that stands for a worked-out value, not a field of the message: the size (in g) of the accelerometer's three
// axes, which are the fields 2, 3 and 4 beside it (so 3, 2, 10, 0 for the camera's accelerometer).
const int FIELD_ACCEL_MAGNITUDE = 0;

}  // namespace camera_metadata

// The metadata streams of one video file, opened for reading single samples (one per video frame) at given times.
class CameraMetadata {
public:
	CameraMetadata();
	~CameraMetadata();

	// Opens `path`, and finds its metadata streams. False if it can't be opened or has neither. Closes whatever was open before.
	bool open(const std::string& path);
	void close();
	const std::string& path() const { return m_path; }
	bool has(int stream) const { return m_stream_index[stream] >= 0; }

	// The sample of `stream` for `seconds` into the file: the last one that begins at or before it. *bytes points at it, until the
	// next call; *begins is when it begins. False if there is none. Asking for the sample that was last returned costs nothing.
	bool sample_at(int stream, double seconds, const std::string** bytes, double* begins);

private:
	struct Sample {
		std::string bytes;
		int64_t     pts;
		int64_t     duration;
		bool        valid;
		Sample() : pts(0), duration(0), valid(false) {}
	};
	bool covers(const Sample& sample, int64_t target) const;
	void refresh(double seconds);
	int64_t target_for(int stream, double seconds) const;

	std::string     m_path;
	AVFormatContext* m_fmt;
	int             m_stream_index[2];
	Sample          m_sample[2];
};

// One packet of a metadata stream, as the camera wrote it.
struct CameraPacket {
	double      time;      // Seconds from the start of the file.
	double      length;    // How long it lasts.
	std::string bytes;
	CameraPacket() : time(0.0), length(0.0) {}
};

// All the packets of one metadata stream of one file, kept in memory, read by a thread of its own so the program carries on while it
// runs. Reading begins at the moment the caller names (where the playhead is, as that is what is wanted first), goes on to the end of
// the file, and then covers the part before. Kept like this, any field of the stream can be pulled out (see CameraSeries) without
// reading the file again: that read is the slow part (many small reads from all over the file), taking the packets apart is quick.
// Packets are held in the order they arrive, so the first ones are from the starting moment on, and the ones after the read has gone
// round to the start of the file are earlier in time than the ones before them. A stream that would need more than MAX_BYTES is kept as
// far as that goes (see note()).
class CameraPackets {
public:
	static const size_t MAX_BYTES = 256 * 1024 * 1024;

	CameraPackets();
	~CameraPackets();

	// Starts reading `stream` of `file` from `start_seconds` into it, forgetting whatever was held and stopping a read in progress.
	void start(const std::string& file, int stream, double start_seconds);
	// Stops a read in progress (and waits for the thread), and forgets everything.
	void stop();
	bool started() const { return m_started; }
	const std::string& file() const { return m_file; }
	// Whether the thread is still reading.
	bool  running() const { return m_running.load(); }
	float progress() const { return (float)m_progress_permille.load() / 1000.0f; }
	// How long the stream is, in seconds; 0 until the thread has opened the file (and if the file does not say).
	double duration() const { return m_duration.load(); }
	// What went wrong, if it did, or what is missing (only once running() is false). Both are "" if all is well.
	std::string error() const;
	std::string note() const;

	// How many packets there are so far.
	size_t count() const;
	// The packet that arrived `index`th, or null if there are not that many yet. Valid until the next start() or stop().
	const CameraPacket* at(size_t index) const;
	// With the read finished (running() false): the packets in the order of their times. `position` is how far along that order; this
	// is the index of the packet there, and the position of the first one at or after `seconds` (the last if none is).
	size_t index_at_position(size_t position) const;
	size_t position_at(double seconds) const;

private:
	void run(std::string file, int stream, double start_seconds);

	std::string                 m_file;
	std::thread                 m_thread;
	std::atomic<bool>           m_running;
	std::atomic<bool>           m_cancel;
	std::atomic<int>            m_progress_permille;
	std::atomic<double>         m_duration;
	bool                        m_started;
	mutable std::mutex          m_mutex;           // Guards what follows.
	std::vector<CameraPacket*>  m_packets;
	size_t                      m_second_run;      // Where in m_packets the part before the starting moment begins (the count if it does not).
	std::string                 m_error;
	std::string                 m_note;
};

// The values of one field across a whole stream, taken from the packets as they come in, by a thread of its own: the program
// carries on while it runs, and the values arrive as they are got. The main thread calls take_new() every so often and draws what it
// gets. A field that holds several values per sample (the high-rate orientation) has them spread evenly over the sample's time. With
// all the packets already held, the values come in time order from `start_seconds` on and then from the start of the file; if they
// are still being read, in the order they come.
class CameraSeries {
public:
	CameraSeries();
	~CameraSeries();

	// Starts taking `path` (field numbers from the top of the message; one ending in camera_metadata::FIELD_ACCEL_MAGNITUDE is the
	// size of the accelerometer's three values) from `packets`, which must stay as they are until stop() (or the next start()).
	void start(const CameraPackets* packets, const std::vector<int>& path, double start_seconds);
	// Stops (and waits for the thread), and forgets what it had.
	void stop();
	bool  running() const { return m_running.load(); }
	// How far along the stream the thread is.
	float progress() const { return (float)m_progress_permille.load() / 1000.0f; }
	double duration() const;
	// Moves the values taken since the last call to the end of *times and *values (times in seconds from the start of the file), and
	// says how many there were. Within a call they run forward in time, except where the order goes round to the start of the file.
	size_t take_new(std::vector<double>* times, std::vector<float>* values);
	// What went wrong, if it did (only once running() is false).
	std::string error() const;
	// What is not as it should be even though there are values (only once running() is false).
	std::string note() const;
	// Whether any value was found (only once running() is false).
	bool found_any() const { return m_found_any; }

private:
	void run(std::vector<int> path, double start_seconds);
	void emit(double time, double step, const std::vector<double>& values);

	const CameraPackets* m_packets;
	std::thread          m_thread;
	std::atomic<bool>    m_running;
	std::atomic<bool>    m_cancel;
	std::atomic<int>     m_progress_permille;
	std::atomic<bool>   m_found_any;
	std::string          m_error;
	std::mutex           m_mutex;               // Guards the chunk.
	std::vector<double>  m_chunk_times;
	std::vector<float>   m_chunk_values;
};
