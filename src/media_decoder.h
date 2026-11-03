#pragma once

// Decoding video from a group of files as one continuous source. No OpenGL in here; see
// MediaContainerMgr for putting the frames on screen and exporting them.
//
// The files are laid end to end on one "global clock" (see source_timeline.h). Every time this
// class hands out is a count of ticks on that clock, ticks being the first file's video-stream
// time base. For a single file the clock is simply the file's own video timestamps, with the
// stream's start_time (normally 0) taken off so the first frame is at 0.
//
// The current frame is always available as 8-bit planar YUV 4:2:0 (frame()), whatever the file
// holds. 10-bit footage is squeezed down to 8 bits for now; a real high-bit-depth path comes
// with the render-target work.
//
// Rules the files have to follow (checked in open()): same video codec, resolution and frame
// rate, and the same audio codec, sample rate and channel count (or none of them with audio).
// That is what a camera produces when it splits one recording.

#include <deque>
#include <stdint.h>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}

#include "source_timeline.h"

// One decoded video frame, kept around so a GOP we have already decoded through once can be
// replayed (forward or backward) without re-seeking/re-decoding. Plane data is copied out of
// the decoder's frame tightly packed (no linesize padding).
struct CachedFrame {
	int64_t pts;  // Global ticks.
	std::vector<uint8_t> y, u, v;
};

class MediaDecoder {
public:
	MediaDecoder();
	~MediaDecoder();

	// Opens the files, in this order, as one source and decodes the first frame. On failure
	// returns false and leaves a sentence for the user in *error; nothing is left half-open.
	bool open(const std::vector<std::string>& paths, std::string* error);
	bool is_open() const { return m_frame != nullptr; }
	// The most recent problem met while decoding after open() (a file that vanished mid-flight,
	// say), for the UI to show. Empty if none.
	const std::string& last_error() const { return m_last_error; }

	// ---- What the source is ----
	int           width() const  { return m_width; }
	int           height() const { return m_height; }
	AVRational    time_base() const { return m_global_tb; }      // One tick, in seconds.
	int64_t       total_ticks() const { return m_timeline.total_ticks(); }
	double        ticks_to_seconds(int64_t ticks) const { return (double)ticks * m_global_tb.num / m_global_tb.den; }
	double        duration_seconds() const { return ticks_to_seconds(total_ticks()); }
	float         frame_rate() const;          // Frames per second.
	int64_t       frame_ticks() const { return m_frame_ticks; }  // Ticks from one frame to the next.
	int           file_count() const { return m_timeline.file_count(); }
	const SourceTimeline& timeline() const { return m_timeline; }
	int64_t       video_bit_rate() const { return m_video_bit_rate; }
	AVRational    video_avg_frame_rate() const { return m_avg_frame_rate; }
	AVRational    video_real_frame_rate() const { return m_real_frame_rate; }
	bool          has_audio() const { return m_audio_params != nullptr; }
	// Describe the first file's streams. Valid as long as this object is open.
	const AVCodecParameters* video_parameters() const { return m_video_params; }
	const AVCodecParameters* audio_parameters() const { return m_audio_params; }
	AVRational    audio_time_base() const { return m_audio_tb; }  // Time base of audio packets handed out below.

	// ---- The current frame ----
	const AVFrame* frame() const { return m_frame; }   // 8-bit YUV 4:2:0, width() x height().
	int64_t        pts() const { return m_pts; }       // Global ticks. 0 before the first frame.
	int            current_file() const { return m_cur_file; }

	// ---- Moving around ----
	// The frame after the one on screen, carrying on into the next file when one ends. False at
	// the end of the last file (the current frame stays the last one).
	bool read_frame();
	// Lands on the first frame at or after `ticks` (or on the last frame, if that's past the end).
	// Cheap for a short hop forward or inside a GOP already decoded; otherwise seeks to the
	// keyframe before it and decodes up to it.
	bool advance_to(int64_t ticks);
	// Moves `count` frames (negative: backward), exactly.
	bool step_frames(int64_t count);
	// Fast, approximate: lands on the keyframe at or before `ticks`, decoding just that frame.
	bool seek_keyframe(int64_t ticks);
	// About `count` frames from here, using keyframes when one is in the way.
	bool seek_by_frames_fast(int64_t count);

	// ---- Audio, for export ----
	// Audio is never decoded here, only passed along as compressed packets. While capturing,
	// every audio packet the decoder reads is queued, with timestamps on the global clock in
	// audio_time_base() units. Poll with pop_audio_packet() after each read_frame().
	void begin_audio_capture();
	void end_audio_capture();
	// The oldest queued packet, or nullptr. The caller owns it (av_packet_free()).
	AVPacket* pop_audio_packet();

private:
	// Media probing and per-file state.
	bool open_file(int index, std::string* error);
	void close_file();
	bool read_file_properties(int index, std::string* error);
	bool next_file();
	bool seek_to(int64_t ticks);

	// Frames.
	bool decode_next();
	bool store_decoded_frame();
	void queue_audio_packet();

	// Cache.
	bool serve_from_cache(int64_t ticks);
	bool serve_from_cache_before(int64_t ticks);
	void cache_add_current(int64_t previous_decoder_pts, bool starts_new_run);

	std::vector<std::string> m_paths;
	SourceTimeline           m_timeline;
	std::string              m_last_error;

	// Properties of the source as a whole, from the first file.
	int                      m_width;
	int                      m_height;
	AVRational               m_global_tb;
	AVRational               m_audio_tb;
	AVRational               m_avg_frame_rate;
	AVRational               m_real_frame_rate;
	int64_t                  m_frame_ticks;
	int64_t                  m_video_bit_rate;
	AVCodecParameters*       m_video_params;
	AVCodecParameters*       m_audio_params;

	// The file currently open.
	int                      m_cur_file;
	AVFormatContext*         m_fmt;
	AVCodecContext*          m_vctx;
	int                      m_video_index;
	int                      m_audio_index;
	AVRational               m_file_video_tb;
	AVRational               m_file_audio_tb;
	int64_t                  m_video_start;      // Stream start_time, in file ticks.
	int64_t                  m_audio_start;
	bool                     m_draining;         // End of file reached; waiting for the decoder to run dry.

	AVPacket*                m_packet;
	AVFrame*                 m_decoded;          // Straight from the decoder.
	AVFrame*                 m_frame;            // Converted to 8-bit YUV 4:2:0; what callers see.
	SwsContext*              m_sws;
	int64_t                  m_pts;              // Of m_frame.
	int64_t                  m_decoder_pts;      // Of the newest frame the decoder produced (m_frame
	                                             // can lag it, when it was served from the cache).
	int64_t                  m_pts_before_switch;
	bool                     m_check_gap;

	bool                     m_capture_audio;
	std::deque<AVPacket*>    m_audio_queue;

	std::vector<CachedFrame> m_cache;
	uint64_t                 m_cache_bytes;
};
