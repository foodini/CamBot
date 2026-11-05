#ifndef _API_EXAMPLE_H
#define _API_EXAMPLE_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "glm/glm.hpp"

#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>  // av_channel_layout_copy()
#include <libswscale/swscale.h>
}

#include "audio_timeline.h"
#include "media_decoder.h"
#include "shader_s.h"

// Shows the frames a MediaDecoder produces (the OpenGL side), answers the rest of the program's
// questions about "where are we in the video", and exports the result. Finding, decoding and
// seeking frames, across however many files the flight was recorded in, is MediaDecoder's job.
class MediaContainerMgr {
public:
	// `decoder` must already be open and must outlive this object.
	MediaContainerMgr(MediaDecoder* decoder, const std::string& vert, const std::string& frag,
		              const glm::vec3* extents);
	~MediaContainerMgr();

	// Next frame. At the end of the video returns false (and finishes an export in progress).
	bool advance_frame();
	// Lands on the first frame at or after `timestamp`, a count of decoder ticks (see
	// MediaDecoder) from the beginning of the first file.
	bool advance_to(int64_t timestamp);
	bool advance_to_parametric(float parametric);
	bool advance_by(uint64_t frame_count);
	bool rewind_by(uint64_t frame_count);
	// Fast, approximate seek: lands on the nearest preceding keyframe and decodes exactly that
	// one frame -- O(1) regardless of GOP length or resolution, unlike advance_to() which can
	// end up decoding an entire GOP to land on an exact target. Used for interactive dragging/
	// scanning, where landing on the nearest keyframe (coarse) beats staying responsive at the
	// cost of an exact frame; advance_to()/advance_by()/rewind_by() remain the precise tools for
	// small, bounded moves (e.g. single-frame stepping).
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
	// The clips the timeline is made of, for showing where the joins are. Positions are [0..1]
	// along the timeline.
	int         clip_count() const;
	float       clip_start_parametric(int index) const;          // Where clip `index` begins.
	int         current_clip_index() const;                      // The clip the frame on screen is from.
	std::string current_media_name() const;                      // That clip's file, without its folder.
	// Replaces the clips being played (see MediaDecoder::set_clips) and shows the frame at
	// `show_ticks` of the new timeline. False, changing nothing, if the clips are not valid.
	bool        set_clips(const std::vector<Clip>& clips, int64_t show_ticks) { return m_decoder->set_clips(clips, show_ticks); }
	// The tick count at the frame on screen (see MediaDecoder), for mapping it through the edit model.
	int64_t     current_ticks() const;
	// What one tick is, in seconds (numerator / denominator).
	AVRational  tick_time_base() const { return m_decoder->time_base(); }
	int64_t     frame_ticks() const { return m_decoder->frame_ticks(); }   // How long one frame lasts, in ticks.
	// The edit being played and the files it is made of, for playing the sound that goes with it.
	const EditList&    edit_list() const { return m_decoder->edit_list(); }
	int                media_count() const { return m_decoder->media_count(); }
	const std::string& media_path(int media) const { return m_decoder->media_path(media); }
	int64_t     total_ticks() const { return m_decoder->total_ticks(); }   // How long the whole video is, in ticks.
	void rotation_angle(float angle) { m_rotation_angle = angle; }
	float rotation_angle() { return m_rotation_angle;  }
	// Turns the picture by `radians` more (the same thing the F, comma and period keys do).
	void rotate_by(float radians) { m_rotation_angle += radians; }

	float frame_rate() const; // Native playback frame rate (frames/sec) of the video stream.
	void render();
	bool recording() const { return m_recording; }

	// Major thanks to "shi-yan" who helped make this possible:
	// https://github.com/shi-yan/videosamples/blob/master/libavmp4encoding/main.cpp
	//
	// Returns false (with last_error() saying why) if the export could not be started; nothing
	// is left half-created in that case.
	bool init_video_output(const std::string& video_file_name, unsigned int width, unsigned int height);
	bool output_video_frame(uint8_t* buf);
	bool finalize_output();
	// Whether the last export was stopped early with Escape (it is still a valid, shorter file).
	bool export_cancelled() const { return m_export_cancelled; }
	// A sentence about what went wrong with the last export step that failed, for the user.
	const std::string& last_error() const { return m_last_error; }

private:
	MediaDecoder*      m_decoder;
	uint32_t           m_height;
	uint32_t           m_width;
	float              m_rotation_angle;
	// Per-key auto-repeat state for the held-frame-step keys -- see ffsw::held_repeat_due().
	int                m_rewind_step_repeat_bucket;
	int                m_advance_step_repeat_bucket;

	// The video quad's own center, in NDC -- the pivot rotation happens around (see render()
	// and 3.3.shader.vert). Computed once from `extents` at construction time.
	float              m_center_ndc_x;
	float              m_center_ndc_y;

	unsigned int       m_yuv_textures[3];
	Shader             m_shader;
	unsigned int       m_VAO;
	unsigned int       m_VBO;

	void init_rendering(const glm::vec3* extents);

	// For writing the output video:
	bool start_output(const std::string& video_file_name, unsigned int width, unsigned int height);
	void abort_output();
	void free_output_assets();
	bool start_audio_output();
	// The sound of the export is made from the edit (see audio_timeline.h) and encoded as AAC, rather than copied from the
	// files, so that cuts get their fades and the sound stays in step with the picture however the clips are arranged.
	// Renders the sound up to the end of the frame on screen, and encodes it.
	void write_audio_for_current_frame();
	void write_audio_through(int64_t timeline_sample);
	// Encodes whole encoder frames of what is waiting; with `flush`, the last part-frame too, and then the encoder is emptied.
	void encode_audio(bool flush);
	void write_audio_packets();
	bool write_encoded_packets();
	bool                   m_recording;
	bool                   m_export_cancelled;
	std::string            m_last_error;
	const AVOutputFormat*  m_output_format;  // av_guess_format() now returns a const pointer
	AVFormatContext*       m_output_format_context;
	const AVCodec*         m_output_video_codec;
	AVCodecContext*        m_output_video_codec_context;
	AVFrame*               m_output_video_frame;
	SwsContext*            m_output_scale_context;
	AVStream*              m_output_video_stream;

	// The output's sound. Null stream if the video has no sound.
	AVStream*              m_output_audio_stream;
	AVCodecContext*        m_output_audio_codec_context;
	AVFrame*               m_output_audio_frame;      // One encoder frame's worth, reused.
	AudioTimeline          m_audio_timeline;
	std::vector<float>     m_audio_pending;           // Rendered, interleaved, not yet given to the encoder.
	int64_t                m_audio_samples_encoded;   // Sent to the encoder so far (the next frame's time).
};

#endif
