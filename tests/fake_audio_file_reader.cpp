// A stand-in for AudioFileReader (audio_file_reader.h) so the timeline can be tested without FFmpeg or any real sound. A "file"
// is any path; its left channel at sample n is n and its right channel is the file's tag (the first letter of its name: a = 1,
// b = 2...), so where every sample came from can be read straight off the output. A path starting with "silent" has no
// sound, and one starting with "missing" will not open. One starting with "tone" is a steady 1013 Hz sine (a pitch that does not divide a tick, so cuts land at all phases) in both channels instead
// (for hearing, in the numbers, whether a cut makes a click). Files are 100000 samples long.
#include "audio_file_reader.h"

#include <math.h>
#include <string.h>

static int g_seeks = 0;
static int g_opens = 0;

void fake_audio_reset() {
	g_seeks = 0;
	g_opens = 0;
}
int fake_audio_seek_count() { return g_seeks; }
int fake_audio_open_count() { return g_opens; }

static const int64_t FILE_SAMPLES = 100000;

AudioFileReader::AudioFileReader()
	: m_format(nullptr), m_decoder(nullptr), m_resampler(nullptr), m_packet(nullptr), m_frame(nullptr), m_stream(-1), m_start(0),
	  m_position(0), m_finished(false), m_flushing(false), m_next_known(false), m_next_decoded(0), m_buffer_start(0) {
}

AudioFileReader::~AudioFileReader() {
	close();
}

void AudioFileReader::close() {
	m_format = nullptr;
}

bool AudioFileReader::open(const std::string& path, std::string* error) {
	g_opens++;
	if (path.compare(0, 7, "missing") == 0) {
		*error = "Could not open \"" + path + "\" for its sound.";
		return false;
	}
	if (path.compare(0, 6, "silent") == 0) {
		*error = "\"" + path + "\" has no sound.";
		return false;
	}
	m_format = reinterpret_cast<AVFormatContext*>(this);   // Anything but null: it's open.
	m_stream = path[0] - 'a' + 1;                          // The tag.
	m_start = path.compare(0, 4, "tone") == 0 ? 1 : 0;     // Whether it is the tone.
	m_position = 0;
	return true;
}

bool AudioFileReader::seek(int64_t sample) {
	g_seeks++;
	m_position = sample < 0 ? 0 : sample;
	return true;
}

int AudioFileReader::read(float* out, int frames) {
	int delivered = 0;
	while (delivered < frames && m_position < FILE_SAMPLES) {
		if (m_start) {
			const float value = (float)sin(2.0 * 3.14159265358979 * 1013.0 * (double)m_position / 48000.0);
			out[delivered * 2] = value;
			out[delivered * 2 + 1] = value;
		} else {
			out[delivered * 2] = (float)m_position;
			out[delivered * 2 + 1] = (float)m_stream;
		}
		delivered++;
		m_position++;
	}
	return delivered;
}
