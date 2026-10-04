#ifndef _API_EXAMPLE_H
#define _API_EXAMPLE_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "glm/glm.hpp"

#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>  // av_channel_layout_copy()
#include <libswscale/swscale.h>
}

#include "shader_s.h"

// One decoded video frame, kept around so a GOP we have already decoded through once can be
// replayed (forward or backward) without re-seeking/re-decoding. Plane data is copied out of
// the decoder's AVFrame tightly packed (no linesize padding), since the decoder reuses/
// overwrites its own frame buffer on every call.
struct CachedFrame {
	int64_t pts;
	std::vector<uint8_t> y, u, v;
};

class MediaContainerMgr {
public:
	MediaContainerMgr(const std::string& infile, const std::string& vert, const std::string& frag, 
		              const glm::vec3* extents);
	~MediaContainerMgr();
	bool advance_frame();
	bool advance_to(int64_t timestamp);
	// Checked by advance_to() before it seeks: if timestamp falls within the GOP we already
	// decoded through and cached, serve it straight from the cache (no seek, no decode) and
	// return true. See m_gop_cache below for what populates it.
	bool try_serve_from_cache(int64_t timestamp);
	bool advance_to_parametric(float parametric);
	bool advance_by(uint64_t timestamp_delta);
	bool rewind_by(uint64_t timestamp_delta);
	// Fast, approximate seek: seeks to the nearest preceding keyframe and decodes exactly that one
	// frame, no further -- O(1) regardless of GOP length or resolution, unlike advance_to() which
	// can end up decoding an entire GOP to land on an exact target. Used for interactive dragging/
	// scanning, where landing on the nearest keyframe (coarse) beats staying responsive at the cost
	// of an exact frame; advance_to()/advance_by()/rewind_by() remain the precise tools for small,
	// bounded moves (e.g. single-frame stepping).
	bool seek_to_keyframe_near(int64_t timestamp);
	bool seek_to_parametric_fast(float parametric);
	// Relative version of seek_to_keyframe_near, in frame-count units like advance_by()/
	// rewind_by() -- lets a caller ask for "about N frames from here, fast" without having to
	// reach for raw timestamps/tick units itself. Negative frame_delta moves backward.
	bool seek_by_frames_fast(int64_t frame_delta);
	float get_width() const { return (float)m_width; }
	float get_height() const { return (float)m_height; }
	uint64_t get_presentation_timestamp() const;
	double get_presentation_timefloat() const;
	float in_parametric() const; // Time in [0.0..1.0] since beginning of video
	float in_elapsed() const;    // Time, in seconds, since beginning of video.
	float in_duration() const;   // Length of video, in seconds.
	int64_t in_end_timestamp() const { return m_format_context->duration; }
	void rotation_angle(float angle) { m_rotation_angle = angle; }
	float rotation_angle() { return m_rotation_angle;  }

	unsigned long int get_frame_time() const;
	float frame_rate() const; // Native playback frame rate (frames/sec) of the video stream.
	float timestamp_to_seconds(uint64_t timestamp) const;
	void render();
	bool recording() { return m_recording; }

	// Major thanks to "shi-yan" who helped make this possible:
	// https://github.com/shi-yan/videosamples/blob/master/libavmp4encoding/main.cpp
	bool init_video_output(const std::string& video_file_name, unsigned int width, unsigned int height);
	bool output_video_frame(uint8_t* buf);
	bool finalize_output();

private:
	AVFormatContext*   m_format_context;
	const AVCodec*     m_video_input_codec;
	const AVCodec*     m_audio_input_codec;
	AVCodecParameters* m_video_codec_parameters;
	AVCodecParameters* m_audio_codec_parameters;
	AVCodecContext*    m_video_input_codec_context;
	AVCodecContext*    m_audio_input_codec_context;
	AVFrame*           m_last_video_frame;
	AVFrame*           m_last_audio_frame;
	AVPacket*          m_packet;
	uint32_t           m_video_stream_index;
	uint32_t           m_audio_stream_index;
	uint32_t           m_height;
	uint32_t           m_width;
	uint32_t           m_ui_height;
	float              m_rotation_angle;
	// Per-key auto-repeat state for the held-frame-step keys -- see ffsw::held_repeat_due().
	int                m_rewind_step_repeat_bucket;
	int                m_advance_step_repeat_bucket;

	// The video quad's own center, in NDC -- the pivot rotation happens around (see render()
	// and 3.3.shader.vert). Computed once from `extents` at construction time.
	float              m_center_ndc_x;
	float              m_center_ndc_y;

	// Caches every frame decoded while walking forward from a keyframe (advance_to()'s existing
	// seek-then-decode-to-target loop already does this walk for any backward move -- this just
	// keeps what it decodes instead of throwing it away), so repeat visits within that same GOP
	// -- the common case while scanning back and forth over a short stretch -- are instant.
	// Holds at most one GOP at a time, cleared whenever a seek lands on a different keyframe.
	std::vector<CachedFrame> m_gop_cache;
	int64_t                  m_gop_cache_keyframe_pts = AV_NOPTS_VALUE;
	
	unsigned int       m_yuv_textures[3];
	Shader             m_shader;
	unsigned int       m_VAO;
	unsigned int       m_VBO;

	void init_rendering(const glm::vec3* extents);
	int decode_packet();

	// For writing the output video:
	void free_output_assets();
	bool                   m_recording;
	const AVOutputFormat*  m_output_format;  // av_guess_format() now returns a const pointer
	AVFormatContext*       m_output_format_context;
	const AVCodec*         m_output_video_codec;
	AVCodecContext*        m_output_video_codec_context;
	AVFrame*               m_output_video_frame;
	SwsContext*            m_output_scale_context;
	AVStream*              m_output_video_stream;
	
	// Audio is a straight stream copy (see advance_frame()'s remux path), never decoded or
	// re-encoded -- the only thing needed for it is the output AVStream itself, with its
	// codecpar copied from the input audio stream.
	AVStream*              m_output_audio_stream;
};

#endif