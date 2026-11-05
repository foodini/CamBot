#pragma once

// Decoding video from a group of files, played as an edit list. No OpenGL in here; see
// MediaContainerMgr for putting the frames on screen and exporting them.
//
// The source is a list of clips (see edit_model.h): pieces of the media files played one after
// the other on the edit timeline. Every time this class hands out is a count of ticks on that
// timeline, ticks being the first file's video-stream time base. Right after open() the clips
// are the files, each whole, in the order given; set_clips() changes that. For a single whole
// file the timeline is simply the file's own video timestamps, with the stream's start_time
// (normally 0) taken off so the first frame is at 0.
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

#include "edit_model.h"

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

	// Whether open() describes the files on the console, as it does by default. Set before open().
	void set_quiet(bool quiet) { m_quiet = quiet; }

	// Opens the files, in this order, as one source and decodes the first frame. On failure
	// returns false and leaves a sentence for the user in *error; nothing is left half-open.
	bool open(const std::vector<std::string>& paths, std::string* error);
	bool is_open() const { return m_frame != nullptr; }
	// Replaces the edit list and shows the frame at `show_ticks` of the new timeline. False,
	// changing nothing, if a clip names a missing file or lies outside its file.
	bool set_clips(const std::vector<Clip>& clips, int64_t show_ticks);
	// The most recent problem met while decoding after open() (a file that vanished mid-flight,
	// say), for the UI to show. Empty if none.
	const std::string& last_error() const { return m_last_error; }

	// ---- What the source is ----
	int           width() const  { return m_width; }
	int           height() const { return m_height; }
	AVRational    time_base() const { return m_global_tb; }      // One tick, in seconds.
	int64_t       total_ticks() const { return m_clips.total_ticks(); }
	double        ticks_to_seconds(int64_t ticks) const { return (double)ticks * m_global_tb.num / m_global_tb.den; }
	double        duration_seconds() const { return ticks_to_seconds(total_ticks()); }
	float         frame_rate() const;          // Frames per second.
	int64_t       frame_ticks() const { return m_frame_ticks; }  // Ticks from one frame to the next.
	// The media files, as given to open(), and how long each is.
	int                 media_count() const { return (int)m_paths.size(); }
	const std::string&  media_path(int media) const { return m_paths[media]; }
	int64_t             media_duration_ticks(int media) const { return m_media_durations[media]; }
	const EditList&     edit_list() const { return m_clips; }
	int64_t       video_bit_rate() const { return m_video_bit_rate; }
	AVRational    video_avg_frame_rate() const { return m_avg_frame_rate; }
	AVRational    video_real_frame_rate() const { return m_real_frame_rate; }
	bool          has_audio() const { return m_audio_params != nullptr; }
	// Describe the first file's streams. Valid as long as this object is open.
	const AVCodecParameters* video_parameters() const { return m_video_params; }

	// ---- The current frame ----
	const AVFrame* frame() const { return m_frame; }   // 8-bit YUV 4:2:0, width() x height().
	int64_t        pts() const { return m_pts; }       // Global ticks. 0 before the first frame.
	// The clip the frame on screen belongs to.
	int            current_clip() const { return m_clips.clip_index_for(m_pts); }

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

	// Lets go of the frames kept for replaying a GOP already decoded (up to a gigabyte of them), for a decoder
	// that is not going to be played backward over them again. The frame on screen stays.
	void release_cache();

	// ---- Keyframes ----
	// The times of file `media`'s keyframes, in ticks from the start of that file, earliest first, as its own index
	// has them; empty if it has no index. False if the file can't be opened. This opens that file, so whatever was
	// being read from another has to be sought to again (as with any move to another file).
	bool keyframe_times(int media, std::vector<int64_t>* local_ticks);

private:
	// Media probing and per-file state.
	bool open_file(int index, std::string* error);
	void close_file();
	bool read_file_properties(int index, std::string* error);
	bool next_clip(bool have_frame, int64_t frame_local_ticks, bool* keep_frame);
	bool position_media(int media, int64_t local_ticks);
	int64_t local_ticks_of(const AVFrame* frame) const;
	bool seek_to(int64_t ticks);

	// Frames.
	bool decode_next();
	bool store_decoded_frame(int64_t local_ticks);

	// Cache.
	bool serve_from_cache(int64_t ticks);
	bool serve_from_cache_before(int64_t ticks);
	void cache_add_current(int64_t previous_decoder_pts, bool starts_new_run);

	std::vector<std::string> m_paths;
	std::vector<int64_t>     m_media_durations;
	EditList                 m_clips;
	int                      m_cur_clip;       // The clip the decoder is working through.
	std::string              m_last_error;

	// Properties of the source as a whole, from the first file.
	int                      m_width;
	int                      m_height;
	AVRational               m_global_tb;
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
	bool                     m_quiet;
	AVRational               m_file_video_tb;
	int64_t                  m_video_start;      // Stream start_time, in file ticks.
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

	std::vector<CachedFrame> m_cache;
	uint64_t                 m_cache_bytes;
};
