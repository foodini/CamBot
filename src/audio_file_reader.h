#pragma once

// Reads the sound of one media file as plain samples: 48 kHz (audio_plan::SAMPLE_RATE), stereo, 32-bit float, however the file
// stored it. Positions are counted in samples from the first sample of the file's sound. The files an edit uses are each read
// through one of these (audio_timeline.h); nothing else in the program decodes audio.

#include <stdint.h>
#include <string>
#include <vector>

struct AVFormatContext;
struct AVCodecContext;
struct AVPacket;
struct AVFrame;
struct SwrContext;

class AudioFileReader {
public:
	AudioFileReader();
	~AudioFileReader();

	// Opens `path` for its sound. False, with a sentence for the user in *error, if it can't be opened or has no sound.
	bool open(const std::string& path, std::string* error);
	void close();
	bool is_open() const { return m_format != nullptr; }

	// Moves so the next read() starts at `sample`.
	bool seek(int64_t sample);
	// The sample the next read() starts at.
	int64_t position() const { return m_position; }
	// Fills `out` with up to `frames` frames (a left and a right float each) from the current position. Gives fewer than
	// asked only at the end of the file; a hole in the file's own timestamps reads as silence.
	int read(float* out, int frames);

private:
	// Decodes the next frame of sound into m_buffer. False at the end of the file.
	bool decode_more();
	bool convert(const AVFrame* frame);
	void reset_decoding();

	AVFormatContext*   m_format;
	AVCodecContext*    m_decoder;
	SwrContext*        m_resampler;
	AVPacket*          m_packet;
	AVFrame*           m_frame;
	int                m_stream;
	int64_t            m_start;           // The sound stream's start time, in its own time base: sample 0.
	int64_t            m_position;        // Next sample to hand out.
	bool               m_finished;        // Everything has been read from the file and the decoder.
	bool               m_flushing;        // The end of the file has been given to the decoder.
	bool               m_next_known;      // m_next_decoded says where the sound after the last frame goes.
	int64_t            m_next_decoded;
	std::vector<float> m_buffer;          // Decoded, resampled sound, interleaved; the last frame decoded.
	int64_t            m_buffer_start;    // The sample m_buffer[0] is.
};
