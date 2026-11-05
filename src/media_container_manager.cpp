#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <cmath>
#include <inttypes.h>
#include <utility> // std::move

#include "shader_s.h"

#include "env_config.h"
#include "file_io.h"
#include "interaction_manager.h"
#include "media_container_manager.h"
#include "util.h"

//TODO(P1): height and width are floats some places and uint32_ts elsewhere.

MediaContainerMgr::MediaContainerMgr(MediaDecoder* decoder, const std::string& vert, const std::string& frag,
    const glm::vec3* extents) :
    m_decoder(decoder),
    m_height(decoder->height()),
    m_width(decoder->width()),
    m_rotation_angle(0.0),
    m_rewind_step_repeat_bucket(-1),
    m_advance_step_repeat_bucket(-1),
    m_center_ndc_x((extents[0].x + extents[3].x) / 2.0f),
    m_center_ndc_y((extents[0].y + extents[3].y) / 2.0f),
    m_shader(vert.c_str(), frag.c_str()),
    m_recording(false),
    m_output_format(nullptr),
    m_output_format_context(nullptr),
    m_output_video_codec(nullptr),
    m_output_video_codec_context(nullptr),
    m_output_video_frame(nullptr),
    m_output_scale_context(nullptr),
    m_output_video_stream(nullptr),
    m_output_audio_stream(nullptr),
    m_last_audio_dts(AV_NOPTS_VALUE),
    m_dropped_audio_packets(0)
{
    init_rendering(extents);

    // Rotation nudge (held-repeat) and flip are this subsystem's own state (m_rotation_angle),
    // so they're bound here rather than in main() -- see InteractionMgr::bind_key()'s doc
    // comment. Comma/period (</>) rather than the arrow keys, now that LEFT/RIGHT do frame
    // stepping below. The on_down handles the initial tap; on_held re-fires every frame once
    // held past the 0.25s repeat threshold, matching the held-repeat feel the old if(key_down()
    // || key_held() >= 0.25) chain had.
    InteractionMgr::instance()->bind_char('.', GLFW_KEY_PERIOD,
        [this]() { m_rotation_angle -= 3.141592653f / 720.0f; },
        nullptr,
        [this](float held_seconds) {
            if (held_seconds >= 0.25f)
                m_rotation_angle -= 3.141592653f / 720.0f;
        });
    InteractionMgr::instance()->bind_char(',', GLFW_KEY_COMMA,
        [this]() { m_rotation_angle += 3.141592653f / 720.0f; },
        nullptr,
        [this](float held_seconds) {
            if (held_seconds >= 0.25f)
                m_rotation_angle += 3.141592653f / 720.0f;
        });
    InteractionMgr::instance()->bind_char('f', GLFW_KEY_F,
        [this]() { m_rotation_angle += 3.141592653f; },
        nullptr,
        nullptr);

    // Single-frame scrub, like the left/right arrows in most video editors: one video frame per
    // tap, auto-repeating at ~10fps (see ffsw::held_repeat_due()) while held rather than at
    // render frame rate -- a much gentler shuttle speed than the rotation keys' max-rate repeat.
    InteractionMgr::instance()->bind_key(GLFW_KEY_LEFT,
        [this]() { rewind_by(1); },
        nullptr,
        [this](float held_seconds) {
            if (ffsw::held_repeat_due(held_seconds, 0.25f, 0.1f, m_rewind_step_repeat_bucket))
                rewind_by(1);
        });
    InteractionMgr::instance()->bind_key(GLFW_KEY_RIGHT,
        [this]() { advance_by(1); },
        nullptr,
        [this](float held_seconds) {
            if (ffsw::held_repeat_due(held_seconds, 0.25f, 0.1f, m_advance_step_repeat_bucket))
                advance_by(1);
        });

    // Escape cancels an in-progress export (there's no other use for it -- see the ESC-to-quit
    // removal earlier in the project history). finalize_output() flushes the encoder and writes
    // a proper trailer either way, so a cancelled file is still a valid, playable .mp4, just
    // shorter than the full video.
    InteractionMgr::instance()->bind_key(GLFW_KEY_ESCAPE,
        [this]() { if (m_recording) finalize_output(); },
        nullptr,
        nullptr);
}

MediaContainerMgr::~MediaContainerMgr() {
    // An export still in progress (the window was closed mid-export, say) gets a proper ending,
    // so what was written so far is a playable file.
    finalize_output();

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
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(0 * sizeof(float)));
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

    m_shader.use(); // ALL UNIFORMS MUST BE SET AFTER use() is called.

    glUniform1i(glGetUniformLocation(m_shader.ID, "texture0"), 0);
    glUniform1i(glGetUniformLocation(m_shader.ID, "texture1"), 1);
    glUniform1i(glGetUniformLocation(m_shader.ID, "texture2"), 2);

}

void MediaContainerMgr::render() {
    int height = (uint32_t)get_height();
    int width = (uint32_t)get_width();

    m_shader.use(); // ALL UNIFORMS MUST BE SET AFTER use() is called.
    m_shader.setFloat("time", (float)get_presentation_timefloat());
    m_shader.setFloat("angle", m_rotation_angle);

    // The rotation itself, and how much to zoom in while rotated so no empty corners show
    // without unnecessary cropping, both need the video rect's real pixel aspect ratio -- see
    // 3.3.shader.vert's uniform comments for the derivation. EnvConfig is guaranteed to exist by
    // the time render() is ever called (main() constructs it before the render loop starts),
    // same assumption WidgetBase::render_border() already makes.
    const EnvConfig* env_config = EnvConfig::instance;
    float viewport_width = env_config->screen_width();
    float viewport_height = env_config->screen_height();
    float video_rect_height = viewport_height;   // The video fills the canvas.
    // NDC x and y don't cover equal pixel counts here (the viewport isn't square in pixels), so
    // a plain rotation matrix applied to NDC coordinates would shear the image. This is the
    // correction factor the shader divides/multiplies by to undo that mismatch before/after
    // rotating.
    float aspect_correction = viewport_width / viewport_height;
    // Minimal zoom so the rotated video rect still fully covers its own display area with no
    // empty corners. NOTE: this does NOT simply repeat every 90 degrees for a non-square rect
    // like this one -- that's only true in the square case. What's actually true for any
    // rectangle is that |cos| and |sin| are each even and 180-degree-periodic on their own,
    // which is exactly what the formula below uses, so it needs no angle reduction/wrapping at
    // all (an earlier version reduced the angle into [0, half_pi) first, which assumed the
    // square-only 90-degree repeat and so got a small negative angle backwards -- treating
    // "just past zero" as "just before 90 degrees" instead).
    float video_rect_aspect = viewport_width / video_rect_height;
    float cover_scale = fabsf(cosf(m_rotation_angle)) + video_rect_aspect * fabsf(sinf(m_rotation_angle));
    m_shader.setFloat("aspect_correction", aspect_correction);
    m_shader.setFloat("cover_scale", cover_scale);
    m_shader.setFloat("center_x", m_center_ndc_x);
    m_shader.setFloat("center_y", m_center_ndc_y);

    // The decoder's rows can be padded past the picture's width (linesize); tell GL, rather
    // than assuming the planes are tightly packed.
    const AVFrame* frame = m_decoder->frame();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_yuv_textures[0]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, frame->data[0]);
    glGenerateMipmap(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_yuv_textures[1]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width / 2, height / 2, 0, GL_RED, GL_UNSIGNED_BYTE, frame->data[1]);
    glGenerateMipmap(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_yuv_textures[2]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[2]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width / 2, height / 2, 0, GL_RED, GL_UNSIGNED_BYTE, frame->data[2]);
    glGenerateMipmap(GL_TEXTURE_2D);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    // The rotated video's corners are clipped to the canvas (which is exactly the video rect: the
    // rectangle glReadPixels() captures for screenshots and export).
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, (int)viewport_width, (int)video_rect_height);

    // render the tristrip
    glBindVertexArray(m_VAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisable(GL_SCISSOR_TEST);
}

bool MediaContainerMgr::advance_frame() {
    bool got_frame = m_decoder->read_frame();
    // Audio comes along with the frames: whatever the decoder read on the way to this frame
    // goes straight into the export.
    if (m_recording) {
        write_pending_audio();
    }
    if (!got_frame) {
        finalize_output();
        return false;
    }
    return true;
}

uint64_t MediaContainerMgr::get_presentation_timestamp() const {
    int64_t pts = m_decoder->pts();
    return pts < 0 ? 0 : (uint64_t)pts;
}

double MediaContainerMgr::get_presentation_timefloat() const {
    return m_decoder->ticks_to_seconds(m_decoder->pts());
}

float MediaContainerMgr::in_parametric() const {
    return (float)((double)m_decoder->pts() / (double)m_decoder->total_ticks());
}

float MediaContainerMgr::in_elapsed() const {
    return (float)m_decoder->ticks_to_seconds(m_decoder->pts());
}

float MediaContainerMgr::in_duration() const {
    return (float)m_decoder->duration_seconds();
}

int MediaContainerMgr::clip_count() const {
    return m_decoder->edit_list().count();
}

float MediaContainerMgr::clip_start_parametric(int index) const {
    return (float)((double)m_decoder->edit_list().start(index) / (double)m_decoder->total_ticks());
}

int MediaContainerMgr::current_clip_index() const {
    return m_decoder->current_clip();
}

std::string MediaContainerMgr::current_media_name() const {
    int clip = m_decoder->current_clip();
    if (clip < 0)
        return std::string();
    return fileio::base_name(m_decoder->media_path(m_decoder->edit_list().clip(clip).media));
}

int64_t MediaContainerMgr::current_ticks() const {
    return m_decoder->pts();
}

float MediaContainerMgr::frame_rate() const {
    return m_decoder->frame_rate();
}

bool MediaContainerMgr::advance_to(int64_t timestamp) {
    return m_decoder->advance_to(timestamp);
}

bool MediaContainerMgr::advance_to_parametric(float parametric) {
    return m_decoder->advance_to((int64_t)((double)parametric * (double)m_decoder->total_ticks()));
}

bool MediaContainerMgr::seek_to_keyframe_near(int64_t timestamp) {
    return m_decoder->seek_keyframe(timestamp);
}

bool MediaContainerMgr::seek_to_parametric_fast(float parametric) {
    return m_decoder->seek_keyframe((int64_t)((double)parametric * (double)m_decoder->total_ticks()));
}

bool MediaContainerMgr::seek_by_frames_fast(int64_t frame_delta) {
    return m_decoder->seek_by_frames_fast(frame_delta);
}

bool MediaContainerMgr::advance_by(uint64_t frame_count) {
    return m_decoder->step_frames((int64_t)frame_count);
}

bool MediaContainerMgr::rewind_by(uint64_t frame_count) {
    return m_decoder->step_frames(-(int64_t)frame_count);
}

// ---------------------------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------------------------

bool MediaContainerMgr::init_video_output(const std::string& video_file_name, unsigned int width, unsigned int height) {
    if (m_recording)
        return true;
    m_last_error.clear();
    if (start_output(video_file_name, width, height))
        return true;
    // Whatever got built before the failure is torn down, so the program isn't left believing
    // an export is running.
    abort_output();
    if (m_last_error.empty())
        m_last_error = "The export could not be started.";
    return false;
}

bool MediaContainerMgr::start_output(const std::string& video_file_name, unsigned int width, unsigned int height) {
    m_recording = true;
    m_last_audio_dts = AV_NOPTS_VALUE;
    m_dropped_audio_packets = 0;

    if (!(m_output_format = av_guess_format(nullptr, video_file_name.c_str(), nullptr))) {
        m_last_error = "Cannot tell what kind of file to write from that file name. Try a name ending in .mp4.";
        return false;
    }

    int err = avformat_alloc_output_context2(&m_output_format_context, m_output_format, nullptr, video_file_name.c_str());
    if (err < 0) {
        m_last_error = "Failed to set up the output file.";
        return false;
    }

    //TODO(P0): Break out the video and audio inits into their own methods.
    // Explicitly requesting h264_nvenc (rather than letting avcodec_find_encoder() pick
    // whatever generic H.264 encoder is registered, which in practice meant software
    // libx264) is what actually gets export onto the GPU. This assumes an NVIDIA GPU +
    // driver are present; there is deliberately no software-encoder fallback.
    m_output_video_codec = avcodec_find_encoder_by_name("h264_nvenc");
    if (!m_output_video_codec) {
        m_last_error = "Could not find the h264_nvenc video encoder. Exporting needs an NVIDIA graphics card and a current driver.";
        return false;
    }
    m_output_video_stream = avformat_new_stream(m_output_format_context, m_output_video_codec);
    if (!m_output_video_stream) {
        m_last_error = "Failed to create the video stream in the output file.";
        return false;
    }
    m_output_video_codec_context = avcodec_alloc_context3(m_output_video_codec);
    if (!m_output_video_codec_context) {
        m_last_error = "Out of memory while setting up the video encoder.";
        return false;
    }
    m_output_video_stream->codecpar->codec_id = m_output_video_codec->id;
    m_output_video_stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    m_output_video_stream->codecpar->width = width;
    m_output_video_stream->codecpar->height = height;
    m_output_video_stream->codecpar->format = AV_PIX_FMT_NV12;
    // Use the same bit rate as the input.
    m_output_video_stream->codecpar->bit_rate = m_decoder->video_bit_rate();
    m_output_video_stream->avg_frame_rate = m_decoder->video_avg_frame_rate();
    avcodec_parameters_to_context(m_output_video_codec_context, m_output_video_stream->codecpar);
    // Frame times are the decoder's global ticks, so a flight made of several files comes out
    // as one continuous video.
    m_output_video_codec_context->time_base = m_decoder->time_base();

    //EXPERIMENTAL: (Does this need to be set?)
    m_output_video_stream->time_base = m_decoder->time_base();

    //TODO(P1): Set these to match the input stream?
    m_output_video_codec_context->max_b_frames = 2;
    m_output_video_codec_context->gop_size = 12;
    m_output_video_codec_context->framerate = m_decoder->video_real_frame_rate();
    //m_output_codec_context->refcounted_frames = 0;
    // "p1" is h264_nvenc's fastest preset (NVENC's equivalent of libx264's "ultrafast") --
    // for export, speed is the entire point. "hq" tuning trades a little of that speed back
    // for quality; output size/quality is otherwise governed by the bit_rate copied from the
    // input stream above, same as the old libx264 path.
    av_opt_set(m_output_video_codec_context, "preset", "p1", 0);
    av_opt_set(m_output_video_codec_context, "tune", "hq", 0);
    avcodec_parameters_from_context(m_output_video_stream->codecpar, m_output_video_codec_context);

    // Audio is a straight stream copy (see write_pending_audio()) -- no decoder or encoder
    // needed, just an output stream whose codecpar matches the input audio exactly.
    if (m_decoder->has_audio()) {
        m_output_audio_stream = avformat_new_stream(m_output_format_context, nullptr);
        if (!m_output_audio_stream) {
            m_last_error = "Failed to create the audio stream in the output file.";
            return false;
        }
        err = avcodec_parameters_copy(m_output_audio_stream->codecpar, m_decoder->audio_parameters());
        if (err < 0) {
            m_last_error = "Failed to copy the audio settings to the output file.";
            return false;
        }
        // The input and output containers don't necessarily agree on codec tag numbering --
        // clearing it lets the muxer pick the right tag for the output container itself rather
        // than carrying over a stale (or wrong) one from the input.
        m_output_audio_stream->codecpar->codec_tag = 0;
    }

    err = avcodec_open2(m_output_video_codec_context, m_output_video_codec, nullptr);
    if (err < 0) {
        m_last_error = "Failed to start the video encoder. Exporting needs an NVIDIA graphics card and a current driver.";
        return false;
    }

    if (!(m_output_format->flags & AVFMT_NOFILE)) {
        err = avio_open(&m_output_format_context->pb, video_file_name.c_str(), AVIO_FLAG_WRITE);
        if (err < 0) {
            m_last_error = "Could not create the output file. Is the folder writable, and is the file open in another program?";
            return false;
        }
    }

    err = avformat_write_header(m_output_format_context, NULL);
    if (err < 0) {
        m_last_error = "Failed to write the output file's header.";
        return false;
    }

    av_dump_format(m_output_format_context, 0, video_file_name.c_str(), 1);

    // Export always covers the whole input, start to finish, regardless of wherever playback
    // happened to be sitting when it was started. The audio capture begins first, so the audio
    // read on the way to the first frame isn't lost, and seek_keyframe() always does a real
    // seek (never an answer from the frame cache, which would leave the demuxer wherever the
    // previous run stopped).
    m_decoder->begin_audio_capture();
    if (!m_decoder->seek_keyframe(0)) {
        m_last_error = "Could not go back to the start of the video.";
        return false;
    }
    write_pending_audio();

    return true;
}

// Undoes a start_output() that got partway: closes the file, frees everything and clears the
// recording flag. Whatever was written is left on disk (it's not a valid video, but deleting
// the user's file is not our call).
void MediaContainerMgr::abort_output() {
    if (m_output_format_context && m_output_format && !(m_output_format->flags & AVFMT_NOFILE) && m_output_format_context->pb) {
        avio_closep(&m_output_format_context->pb);
    }
    m_decoder->end_audio_capture();
    free_output_assets();
    m_recording = false;
}

// Moves the audio packets the decoder has queued into the output file. They arrive on the
// global clock; the only fix-up here is dropping any packet that would go backward in time.
// Separately-recorded files rarely agree on exactly how long their audio is, and where one file's
// sound runs a few milliseconds past the start of the next, the muxer would refuse the overlap.
void MediaContainerMgr::write_pending_audio() {
    AVPacket* packet;
    while ((packet = m_decoder->pop_audio_packet()) != nullptr) {
        if (!m_recording || m_output_audio_stream == nullptr) {
            av_packet_free(&packet);
            continue;
        }
        if (packet->dts != AV_NOPTS_VALUE) {
            if (m_last_audio_dts != AV_NOPTS_VALUE && packet->dts <= m_last_audio_dts) {
                m_dropped_audio_packets++;
                av_packet_free(&packet);
                continue;
            }
            m_last_audio_dts = packet->dts;
        }
        packet->stream_index = m_output_audio_stream->index;
        av_packet_rescale_ts(packet, m_decoder->audio_time_base(), m_output_audio_stream->time_base);
        int err = av_interleaved_write_frame(m_output_format_context, packet);
        if (err) {
            printf("  audio remux error: %d\n", err);
        }
        av_packet_free(&packet);
    }
}

// Writes out every packet the encoder has ready. (It runs a few frames behind what's been
// sent to it, so right after a send there may be none, or in a flush, several.)
bool MediaContainerMgr::write_encoded_packets() {
    AVPacket* pkt = av_packet_alloc();
    if (!pkt)
        return false;
    int written = 0;
    while (avcodec_receive_packet(m_output_video_codec_context, pkt) == 0) {
        pkt->stream_index = m_output_video_stream->index;
        av_packet_rescale_ts(pkt, m_output_video_codec_context->time_base, m_output_video_stream->time_base);
        av_interleaved_write_frame(m_output_format_context, pkt);
        written++;
    }
    av_packet_free(&pkt);
    return written > 0;
}

//TODO(P2): make this a member. (Thanks to https://emvlo.wordpress.com/2016/03/10/sws_scale/)
void PrepareFlipFrameJ420(AVFrame* pFrame) {
    for (int i = 0; i < 4; i++) {
        if (i)
            pFrame->data[i] += pFrame->linesize[i] * ((pFrame->height >> 1) - 1);
        else
            pFrame->data[i] += pFrame->linesize[i] * (pFrame->height - 1);
        pFrame->linesize[i] = -pFrame->linesize[i];
    }
}

//TODO(P0): Solve this width/height bullshit once and for all. We can't count on having enough screen space for the
//          full video resolution, but I'd rather not "edit" at 1/2 resolution, then render to a frame buffer to make
//          the video. Maybe bring the thing up at half-res, have a way to change resolutions, and output at whatever
//          the current UI resolution is. The EnvConfig would then be the official source of the resolution of

//TODO(P0): Solve this width/height bullshit once and for all. We can't count on having enough screen space for the
//          full video resolution, but I'd rather not "edit" at 1/2 resolution, then render to a frame buffer to make
//          the video. Maybe bring the thing up at half-res, have a way to change resolutions, and output at whatever
//          the current UI resolution is. The EnvConfig would then be the official source of the resolution of
//          everything, except the input resolution... and even that would dictate the aspect ratio.
bool MediaContainerMgr::output_video_frame(uint8_t* buf) {
    int err;

    if (!m_recording || !m_output_video_codec_context)
        return false;

    if (!m_output_video_frame) {
        m_output_video_frame = av_frame_alloc();
        m_output_video_frame->format = AV_PIX_FMT_NV12;
        m_output_video_frame->width = m_output_video_codec_context->width;
        m_output_video_frame->height = m_output_video_codec_context->height;
        err = av_frame_get_buffer(m_output_video_frame, 32);
        if (err < 0) {
            printf("Failed to allocate output frame.\n");
            return false;
        }
    }

    if (!m_output_scale_context) {
        //TODO(P1): Do this in hardware? It's a lot of work to buy back a few seconds of processing time. If
        //          anyone else takes an interest in the project, it'll be worth it, but it may never save me
        //          as much time as it costs.
        m_output_scale_context = sws_getContext(m_output_video_codec_context->width, m_output_video_codec_context->height, 
                                                AV_PIX_FMT_RGB24,
                                                m_output_video_codec_context->width, m_output_video_codec_context->height, 
                                                AV_PIX_FMT_NV12, SWS_BICUBIC, nullptr, nullptr, nullptr);
    }

    int inLinesize[1] = { 3 * m_output_video_codec_context->width };
    sws_scale(m_output_scale_context, (const uint8_t* const*)&buf, inLinesize, 0, m_output_video_codec_context->height,
              m_output_video_frame->data, m_output_video_frame->linesize);
    PrepareFlipFrameJ420(m_output_video_frame);
    // The decoder's global time for the frame on screen: continuous across all the input files.
    m_output_video_frame->pts = m_decoder->pts();
    err = avcodec_send_frame(m_output_video_codec_context, m_output_video_frame);
    if (err < 0) {
        printf("  ERROR sending new video frame output: ");
        switch (err) {
        case AVERROR(EAGAIN):
            printf("AVERROR(EAGAIN): %d\n", err);
            break;
        case AVERROR_EOF:
            printf("AVERROR_EOF: %d\n", err);
            break;
        case AVERROR(EINVAL):
            printf("AVERROR(EINVAL): %d\n", err);
            break;
        case AVERROR(ENOMEM):
            printf("AVERROR(ENOMEM): %d\n", err);
            break;
        }

        return false;
    }

    write_encoded_packets();

    return true;
}

// Frees everything init_video_output() allocated, and resets the pointers back to the
// construction-time nullptr state, so a later init_video_output() call (a fresh export,
// started after this one finished or was cancelled) builds everything fresh instead of
// finding stale pointers left over from the previous export. Called only from
// finalize_output(), once the muxer is done with them.
void MediaContainerMgr::free_output_assets() {
    if (m_output_video_frame) {
        av_frame_free(&m_output_video_frame);
    }
    if (m_output_scale_context) {
        sws_freeContext(m_output_scale_context);
        m_output_scale_context = nullptr;
    }
    if (m_output_video_codec_context) {
        avcodec_free_context(&m_output_video_codec_context);
    }
    if (m_output_format_context) {
        // Frees the AVFormatContext and its streams (m_output_video_stream/
        // m_output_audio_stream point into it, so they go away here too, not separately) --
        // not the AVIOContext/pb, which the caller already closed with avio_close().
        avformat_free_context(m_output_format_context);
        m_output_format_context = nullptr;
    }
    m_output_video_stream = nullptr;
    m_output_audio_stream = nullptr;
    m_output_video_codec = nullptr;
    m_output_format = nullptr; // not owned -- av_guess_format() hands back a static table entry
}

bool MediaContainerMgr::finalize_output() {
    if (!m_recording)
        return true;

    // Let the encoder empty out the frames it's still holding, and write the audio that was
    // read but not yet written, before the file is closed.
    avcodec_send_frame(m_output_video_codec_context, nullptr);
    write_encoded_packets();
    write_pending_audio();
    m_decoder->end_audio_capture();

    av_write_trailer(m_output_format_context);
    bool closed_cleanly = true;
    if (!(m_output_format->flags & AVFMT_NOFILE)) {
        int err = avio_close(m_output_format_context->pb);
        if (err < 0) {
            printf("Failed to close file. err: %d\n", err);
            m_last_error = "The exported file could not be closed properly. Is the disk full?";
            closed_cleanly = false;
        }
    }
    if (m_dropped_audio_packets > 0) {
        printf("Export: dropped %d overlapping audio packets at file joins.\n", m_dropped_audio_packets);
    }

    // Whether or not the close above succeeded, the output is done with -- leave the assets
    // freed and m_recording false so a later init_video_output() (a fresh export after this
    // one finished or was cancelled) starts from a clean slate instead of reusing stale
    // pointers sized/opened for the previous export.
    free_output_assets();
    m_recording = false;

    return closed_cleanly;
}
