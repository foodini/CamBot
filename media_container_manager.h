#ifndef _API_EXAMPLE_H
#define _API_EXAMPLE_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "glm/glm.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include "shader_s.h"

class MediaContainerMgr {
public:
	MediaContainerMgr(const std::string& infile, const std::string& vert, const std::string& frag, 
		              const glm::vec3* extents);
	~MediaContainerMgr();
	bool advance_frame();
	bool advance_to(int64_t timestamp);
	bool advance_to_parametric(float parametric);
	bool advance_by(uint64_t timestamp_delta);
	bool rewind_by(uint64_t timestamp_delta);
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
	AVCodec*           m_video_input_codec;
	AVCodec*           m_audio_input_codec;
	AVCodecParameters* m_video_codec_parameters;
	AVCodecParameters* m_audio_codec_parameters;
	AVCodecContext*    m_video_input_codec_context;
	AVCodecContext*    m_audio_input_codec_context;
	AVFrame*           m_frame;
	AVPacket*          m_packet;
	uint32_t           m_video_stream_index;
	uint32_t           m_audio_stream_index;
	uint32_t           m_height;
	uint32_t           m_width;
	uint32_t           m_ui_height;
	float              m_rotation_angle;
	
	unsigned int       m_yuv_textures[3];
	Shader             m_shader;
	unsigned int       m_VAO;
	unsigned int       m_VBO;

	void init_rendering(const glm::vec3* extents);
	int decode_packet();

	// For writing the output video:
	void free_output_assets();
	bool                   m_recording;
	AVOutputFormat*        m_output_format;
	AVFormatContext*       m_output_format_context;
	AVCodec*               m_output_video_codec;
	AVCodecContext*        m_output_video_codec_context;
	AVFrame*               m_output_video_frame;
	SwsContext*            m_output_scale_context;
	AVStream*              m_output_video_stream;
	
	AVCodec*               m_output_audio_codec;
	AVStream*              m_output_audio_stream;
	AVCodecContext*        m_output_audio_codec_context;
};

#endif