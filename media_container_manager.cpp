#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "shader_s.h"

#include "media_container_manager.h"

//TODO(P1): height and width are floats some places and uint32_ts elsewhere.

MediaContainerMgr::MediaContainerMgr(const char* infile, int debug_write_rate, const std::string& vert, const std::string& frag,
                                     const glm::vec3* extents) :
    m_video_stream_index(-1),
    m_height(0),
    m_width(0),
    m_debug_write_rate(debug_write_rate),
    m_debug_write_countdown(debug_write_rate),
    m_shader(vert.c_str(), frag.c_str())
{
    // AVFormatContext holds header info from the format specified in the container:
    m_format_context = avformat_alloc_context();
    if (!m_format_context) {
        throw "ERROR could not allocate memory for Format Context";
    }
    
    // open the file and read its header. Codecs are not opened here.
    if (avformat_open_input(&m_format_context, infile, NULL, NULL) != 0) {
        throw "ERROR could not open input file for reading";
    }

    printf("format %s, duration %lldus, bit_rate %lld\n", m_format_context->iformat->name, m_format_context->duration, m_format_context->bit_rate);
    //read avPackets (?) from the avFormat (?) to get stream info. This populates format_context->streams.
    if (avformat_find_stream_info(m_format_context, NULL) < 0) {
        throw "ERROR could not get stream info";
    }

    for (unsigned int i = 0; i < m_format_context->nb_streams; i++) {
        AVCodecParameters* local_codec_parameters = NULL;
        local_codec_parameters = m_format_context->streams[i]->codecpar;
        printf("AVStream->time base before open coded %d/%d\n", m_format_context->streams[i]->time_base.num, m_format_context->streams[i]->time_base.den);
        printf("AVStream->r_frame_rate before open coded %d/%d\n", m_format_context->streams[i]->r_frame_rate.num, m_format_context->streams[i]->r_frame_rate.den);
        printf("AVStream->start_time %" PRId64 "\n", m_format_context->streams[i]->start_time);
        printf("AVStream->duration %" PRId64 "\n", m_format_context->streams[i]->duration);
        printf("duration(s): %lf\n", (float)m_format_context->streams[i]->duration / m_format_context->streams[i]->time_base.den * m_format_context->streams[i]->time_base.num);
        AVCodec* local_codec = NULL;
        local_codec = avcodec_find_decoder(local_codec_parameters->codec_id);
        if (local_codec == NULL) {
            throw "ERROR unsupported codec!";
        }

        if (local_codec_parameters-> codec_type == AVMEDIA_TYPE_VIDEO) {
            if (m_video_stream_index == -1) {
                m_video_stream_index = i;
                m_codec = local_codec;
                m_codec_parameters = local_codec_parameters;
            }
            m_height = local_codec_parameters->height;
            m_width = local_codec_parameters->width;
            printf("Video Codec: resolution %dx%d\n", m_width, m_height);
        }
        else if (local_codec_parameters->codec_type == AVMEDIA_TYPE_AUDIO) {
            printf("Audio Codec: %d channels, sample rate %d\n", local_codec_parameters->channels, local_codec_parameters->sample_rate);
        }

        printf("\tCodec %s ID %d bit_rate %lld\n", local_codec->name, local_codec->id, local_codec_parameters->bit_rate);
    }

    m_codec_context = avcodec_alloc_context3(m_codec);
    if (!m_codec_context) {
        throw "ERROR failed to allocate memory for AVCodecContext";
    }

    if (avcodec_parameters_to_context(m_codec_context, m_codec_parameters) < 0) {
        throw "ERROR failed to copy codec params to codec context";
    }

    if (avcodec_open2(m_codec_context, m_codec, NULL) < 0) {
        throw "ERROR avcodec_open2 failed to open codec";
    }

    m_frame = av_frame_alloc();
    if (!m_frame) {
        throw "ERROR failed to allocate AVFrame memory";
    }

    m_packet = av_packet_alloc();
    if (!m_packet) {
        throw "ERROR failed to allocate AVPacket memory";
    }

    init_rendering(extents);
}

MediaContainerMgr::~MediaContainerMgr() {
    avformat_close_input(&m_format_context);
    av_packet_free(&m_packet);
    av_frame_free(&m_frame);
    avcodec_free_context(&m_codec_context);

    glDeleteVertexArrays(1, &m_VAO);
    glDeleteBuffers(1, &m_VBO);
}

void MediaContainerMgr::init_rendering(const glm::vec3* extents) {
    // set up vertex data (and buffer(s)) and configure vertex attributes
    // ------------------------------------------------------------------
    float vertices[] = {
        // positions                                // colors          // uv coords
        extents[0].x, extents[0].y, extents[0].z,   0.0f, 0.0f, 0.0f,  0.0f, 1.0f, // bottom left
        extents[1].x, extents[1].y, extents[1].z,   1.0f, 0.0f, 0.0f,   1.0f, 1.0f, // bottom right
        extents[2].x, extents[2].y, extents[2].z,   0.0f, 0.0f, 1.0f,   0.0f, 0.0f, // top left
        extents[3].x, extents[3].y, extents[3].z,   0.0f, 0.0f, 1.0f,   1.0f, 0.0f // top right
    };
    

    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    // bind the Vertex Array Object first, then bind and set vertex buffer(s), and then configure vertex attributes(s).
    glBindVertexArray(m_VAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // color attribute
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // uv attribute
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    // You can unbind the VAO afterwards so other VAO calls won't accidentally modify this VAO, but this rarely happens. Modifying other
    // VAOs requires a call to glBindVertexArray anyways so we generally don't unbind VAOs (nor VBOs) when it's not directly necessary.
    // glBindVertexArray(0);

    glGenTextures(3, m_yuv_textures);
    for (int channel = 0; channel < 3; channel++) {
        glBindTexture(GL_TEXTURE_2D, m_yuv_textures[channel]);
        // Set texture wrapping options
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        // Set texture filtering options
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    m_shader.use();

    glUniform1i(glGetUniformLocation(m_shader.ID, "texture0"), 0);
    glUniform1i(glGetUniformLocation(m_shader.ID, "texture1"), 1);
    glUniform1i(glGetUniformLocation(m_shader.ID, "texture2"), 2);

    advance_frame();
}

void MediaContainerMgr::render() {
    int height = (uint32_t)get_height();
    int width = (uint32_t)get_width();

    m_shader.setFloat("time", (float)get_presentation_timefloat());
    m_shader.setFloat("angle", 0.0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_yuv_textures[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, m_frame->data[0]);
    glGenerateMipmap(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_yuv_textures[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width / 2, height / 2, 0, GL_RED, GL_UNSIGNED_BYTE, m_frame->data[1]);
    glGenerateMipmap(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_yuv_textures[2]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width / 2, height / 2, 0, GL_RED, GL_UNSIGNED_BYTE, m_frame->data[2]);
    glGenerateMipmap(GL_TEXTURE_2D);

    // render the tristrip
    m_shader.use();

    glBindVertexArray(m_VAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

}

bool MediaContainerMgr::advance_frame() {
    while (true) {
        if (av_read_frame(m_format_context, m_packet) < 0) {
            // Do we actually need to unref the packet if it failed?
            av_packet_unref(m_packet);
            continue;
            //return false;
        }
        else {
            if (m_packet->stream_index == m_video_stream_index) {
                printf("AVPacket->pts %" PRId64 "\n", m_packet->pts);
                int response = decode_packet();
                av_packet_unref(m_packet);
                if (response != 0) {
                    continue;
                    //return false;
                }
                return true;
            }
        }

        // We're done with the packet (it's been unpacked to a frame), so deallocate & reset to defaults:
/*
        if (m_frame == NULL)
            return false;

        if (m_frame->data[0] == NULL || m_frame->data[1] == NULL || m_frame->data[2] == NULL) {
            printf("WARNING: null frame data");
            continue;
        }
*/
    }
}

int MediaContainerMgr::decode_packet() {
    // Supply raw packet data as input to a decoder
    // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga58bc4bf1e0ac59e27362597e467efff3
    int response = avcodec_send_packet(m_codec_context, m_packet);

    if (response < 0) {
        char buf[256];
        av_strerror(response, buf, 256);
        printf("Error while receiving a frame from the decoder: %s\n", buf);
        return response;
    }

    // Return decoded output data (into a frame) from a decoder
    // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga11e6542c4e66d3028668788a1a74217c
    response = avcodec_receive_frame(m_codec_context, m_frame);
    if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
        return response;
    } else if (response < 0) {
        char buf[256];
        av_strerror(response, buf, 256);
        printf("Error while receiving a frame from the decoder: %s\n", buf);
        return response;
    } else {
        printf(
            "Frame %d (type=%c, size=%d bytes) pts %lld key_frame %d [DTS %d]\n",
            m_codec_context->frame_number,
            av_get_picture_type_char(m_frame->pict_type),
            m_frame->pkt_size,
            m_frame->pts,
            m_frame->key_frame,
            m_frame->coded_picture_number
        );

        if (--m_debug_write_countdown == 0) {
            m_debug_write_countdown = m_debug_write_rate;
            char frame_filename[1024];
            snprintf(frame_filename, sizeof(frame_filename), "%s-%d.pgm", "frame", m_codec_context->frame_number);
            save_yellow_frame(m_frame->data[0], m_frame->linesize[0], frame_filename);
        }
    }
    return 0;
}

void MediaContainerMgr::save_yellow_frame(unsigned char* buf, int wrap, char* filename)
{
    FILE* f;
    f = fopen(filename, "w");
    // writing the minimal required header for a pgm file format
    // portable graymap format -> https://en.wikipedia.org/wiki/Netpbm_format#PGM_example
    fprintf(f, "P5\n%d %d\n%d\n", m_width, m_height, 255);

    // writing line by line
    for (unsigned int i = 0; i < m_height; i++)
        fwrite(buf + (uint64_t)i * wrap, 1, m_width, f);
    fclose(f);
}

uint64_t MediaContainerMgr::get_presentation_timestamp() const { 
    if (m_frame)
        return m_frame->pts;
    else
        return 0;
}

double MediaContainerMgr::get_presentation_timefloat() const { 
    if (m_format_context)
        return (double)get_presentation_timestamp() * (double)m_format_context->streams[m_video_stream_index]->time_base.num /
            m_format_context->streams[m_video_stream_index]->time_base.den;
    else
        return 0.0;
}

float MediaContainerMgr::in_parametric() const {
    if (m_format_context)
        return (float)m_frame->pts / m_format_context->streams[m_video_stream_index]->duration;
    else
        return 0.0;
}

float MediaContainerMgr::in_elapsed() const {
    return in_duration() * in_parametric();
}

// TODO: I'm sure this needs to be a num/den pair.
// Returns the number of ticks per frame. (How much to change timestamps to seek by one frame.)
unsigned long int MediaContainerMgr::get_frame_time() const {
    // Doing my best to avoid arithmetic errors by ordering the mults and divs. TODO: Fix this and do it right.
    return m_format_context->streams[m_video_stream_index]->time_base.den /
        m_format_context->streams[m_video_stream_index]->avg_frame_rate.num *
        m_format_context->streams[m_video_stream_index]->avg_frame_rate.den /
        m_format_context->streams[m_video_stream_index]->time_base.num;
}

//TODO(P1) I'm not checking bounds on seeks.
bool MediaContainerMgr::advance_to(int64_t timestamp) {
    //while (av_get_picture_type_char(m_frame->pict_type) != 'I)
    // rewind until we get to an IFrame
    av_seek_frame(m_format_context, m_video_stream_index, timestamp, AVSEEK_FLAG_BACKWARD);
    do {
        advance_frame();
    } while (m_frame->pts < timestamp);

    return true;
}

bool MediaContainerMgr::advance_to_parametric(float parametric) {
    advance_to(parametric * m_format_context->streams[m_video_stream_index]->duration);
    return true;
}

bool MediaContainerMgr::advance_by(uint64_t frame_count) {
    if (false && frame_count < 10) {
        do {
            advance_frame();
        } while (--frame_count);
        return true;
    } else {
        return advance_to(get_presentation_timestamp() + frame_count * get_frame_time());
    }
}

bool MediaContainerMgr::rewind_by(uint64_t frame_count) {
    return advance_to(get_presentation_timestamp() - frame_count * get_frame_time());
}

float MediaContainerMgr::in_duration() const {
    return (float)m_format_context->streams[m_video_stream_index]->duration / m_format_context->streams[m_video_stream_index]->time_base.den *
        m_format_context->streams[m_video_stream_index]->time_base.num;
}

float MediaContainerMgr::timestamp_to_seconds(uint64_t timestamp) const {
    return (float)timestamp / m_format_context->duration * in_duration();
}