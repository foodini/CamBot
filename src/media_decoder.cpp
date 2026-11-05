#include "media_decoder.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <utility>

extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
}

#include "file_io.h"

namespace {

std::string string_printf(const char* format, ...) {
	char buffer[1024];
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	return std::string(buffer);
}

std::string av_error_text(int code) {
	char buffer[AV_ERROR_MAX_STRING_SIZE];
	av_strerror(code, buffer, sizeof(buffer));
	return std::string(buffer);
}

bool is_valid_rate(AVRational rate) {
	return rate.num > 0 && rate.den > 0;
}

// Packed copy of one plane out of a frame, and back. Used for the frame cache.
void copy_plane_out(std::vector<uint8_t>* dst, const AVFrame* frame, int plane, int width, int height) {
	dst->resize((size_t)width * (size_t)height);
	for (int row = 0; row < height; row++)
		memcpy(&(*dst)[(size_t)row * width], frame->data[plane] + (size_t)row * frame->linesize[plane], width);
}

void copy_plane_in(AVFrame* frame, int plane, const std::vector<uint8_t>& src, int width, int height) {
	for (int row = 0; row < height; row++)
		memcpy(frame->data[plane] + (size_t)row * frame->linesize[plane], &src[(size_t)row * width], width);
}

const int64_t MAX_COAST_FRAMES = 300;                                  // A generous upper bound on real GOP length.
const uint64_t CACHE_BUDGET_BYTES = (uint64_t)1 * 1024 * 1024 * 1024;  // 1 GiB of cached frames, at most.

}  // namespace

MediaDecoder::MediaDecoder() :
	m_cur_clip(0),
	m_width(0),
	m_height(0),
	m_frame_ticks(1),
	m_video_bit_rate(0),
	m_video_params(nullptr),
	m_audio_params(nullptr),
	m_cur_file(-1),
	m_fmt(nullptr),
	m_vctx(nullptr),
	m_video_index(-1),
	m_audio_index(-1),
	m_quiet(false),
	m_video_start(0),
	m_audio_start(0),
	m_draining(false),
	m_packet(nullptr),
	m_decoded(nullptr),
	m_frame(nullptr),
	m_sws(nullptr),
	m_pts(0),
	m_decoder_pts(AV_NOPTS_VALUE),
	m_pts_before_switch(0),
	m_check_gap(false),
	m_capture_audio(false),
	m_cache_bytes(0)
{
	m_global_tb.num = 1;
	m_global_tb.den = 90000;
	m_audio_tb.num = 1;
	m_audio_tb.den = 48000;
	m_avg_frame_rate.num = 30;
	m_avg_frame_rate.den = 1;
	m_real_frame_rate = m_avg_frame_rate;
	m_file_video_tb = m_global_tb;
	m_file_audio_tb = m_audio_tb;
}

MediaDecoder::~MediaDecoder() {
	close_file();
	end_audio_capture();
	if (m_sws)
		sws_freeContext(m_sws);
	av_packet_free(&m_packet);
	av_frame_free(&m_decoded);
	av_frame_free(&m_frame);
	avcodec_parameters_free(&m_video_params);
	avcodec_parameters_free(&m_audio_params);
}

float MediaDecoder::frame_rate() const {
	return (float)av_q2d(m_avg_frame_rate);
}

// ---------------------------------------------------------------------------------------------
// Opening
// ---------------------------------------------------------------------------------------------

void MediaDecoder::close_file() {
	avcodec_free_context(&m_vctx);
	if (m_fmt)
		avformat_close_input(&m_fmt);
	m_video_index = -1;
	m_audio_index = -1;
	m_draining = false;
	m_cur_file = -1;
}

// Opens file `index` for decoding (closing whichever was open) and leaves it at its beginning.
bool MediaDecoder::open_file(int index, std::string* error) {
	close_file();
	const std::string& path = m_paths[index];
	std::string name = fileio::base_name(path);

	int result = avformat_open_input(&m_fmt, path.c_str(), nullptr, nullptr);
	if (result < 0) {
		*error = string_printf("Could not open \"%s\": %s", name.c_str(), av_error_text(result).c_str());
		m_fmt = nullptr;
		return false;
	}
	result = avformat_find_stream_info(m_fmt, nullptr);
	if (result < 0) {
		*error = string_printf("Could not read the streams in \"%s\": %s", name.c_str(), av_error_text(result).c_str());
		close_file();
		return false;
	}

	m_video_index = av_find_best_stream(m_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	if (m_video_index < 0) {
		*error = string_printf("\"%s\" has no video in it.", name.c_str());
		close_file();
		return false;
	}
	// Cameras also write timecode and telemetry streams that have no decoder. They're simply
	// not looked at, so only the audio stream has to be one we know about.
	m_audio_index = av_find_best_stream(m_fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
	if (m_audio_index < 0)
		m_audio_index = -1;

	AVStream* video_stream = m_fmt->streams[m_video_index];
	const AVCodec* decoder = avcodec_find_decoder(video_stream->codecpar->codec_id);
	if (decoder == nullptr) {
		*error = string_printf("\"%s\" is encoded in a format (%s) this build of CamBot cannot decode.", name.c_str(),
			avcodec_get_name(video_stream->codecpar->codec_id));
		close_file();
		return false;
	}
	m_vctx = avcodec_alloc_context3(decoder);
	if (m_vctx == nullptr) {
		*error = "Out of memory while opening the video decoder.";
		close_file();
		return false;
	}
	result = avcodec_parameters_to_context(m_vctx, video_stream->codecpar);
	if (result < 0) {
		*error = string_printf("Could not set up the video decoder for \"%s\": %s", name.c_str(), av_error_text(result).c_str());
		close_file();
		return false;
	}
	m_vctx->thread_count = 0;  // Let FFmpeg pick a thread count to suit the machine.
	result = avcodec_open2(m_vctx, decoder, nullptr);
	if (result < 0) {
		*error = string_printf("Could not start the video decoder for \"%s\": %s", name.c_str(), av_error_text(result).c_str());
		close_file();
		return false;
	}

	m_file_video_tb = video_stream->time_base;
	m_video_start = video_stream->start_time == AV_NOPTS_VALUE ? 0 : video_stream->start_time;
	if (m_audio_index >= 0) {
		AVStream* audio_stream = m_fmt->streams[m_audio_index];
		m_file_audio_tb = audio_stream->time_base;
		m_audio_start = audio_stream->start_time == AV_NOPTS_VALUE ? 0 : audio_stream->start_time;
	}
	m_cur_file = index;
	m_draining = false;
	return true;
}

// Run while file `index` is open during open(): records what the first file looks like, checks
// every other file against it, and works out each file's length.
bool MediaDecoder::read_file_properties(int index, std::string* error) {
	std::string name = fileio::base_name(m_paths[index]);
	AVStream* video_stream = m_fmt->streams[m_video_index];
	AVCodecParameters* video = video_stream->codecpar;
	AVRational avg_rate = is_valid_rate(video_stream->avg_frame_rate) ? video_stream->avg_frame_rate : video_stream->r_frame_rate;
	if (!is_valid_rate(avg_rate)) {
		*error = string_printf("Could not work out the frame rate of \"%s\".", name.c_str());
		return false;
	}

	if (index == 0) {
		m_global_tb = video_stream->time_base;
		m_width = video->width;
		m_height = video->height;
		m_avg_frame_rate = avg_rate;
		m_real_frame_rate = is_valid_rate(video_stream->r_frame_rate) ? video_stream->r_frame_rate : avg_rate;
		m_frame_ticks = av_rescale_q(1, av_inv_q(avg_rate), m_global_tb);
		if (m_frame_ticks < 1)
			m_frame_ticks = 1;
		m_video_bit_rate = video->bit_rate;
		if (m_video_bit_rate <= 0)
			m_video_bit_rate = m_fmt->bit_rate;
		if (m_video_bit_rate <= 0)
			m_video_bit_rate = 50000000;
		avcodec_parameters_free(&m_video_params);
		avcodec_parameters_free(&m_audio_params);
		m_video_params = avcodec_parameters_alloc();
		avcodec_parameters_copy(m_video_params, video);
		if (m_audio_index >= 0) {
			m_audio_params = avcodec_parameters_alloc();
			avcodec_parameters_copy(m_audio_params, m_fmt->streams[m_audio_index]->codecpar);
			m_audio_tb = m_file_audio_tb;
		}
		if (m_width <= 0 || m_height <= 0 || (m_width % 2) != 0 || (m_height % 2) != 0) {
			*error = string_printf("\"%s\" has an unusable picture size (%dx%d).", name.c_str(), m_width, m_height);
			return false;
		}
		if (!m_quiet)
			printf("Source: %dx%d, %.3f fps, %s video, pixel format %s, time base %d/%d\n", m_width, m_height, av_q2d(avg_rate),
				avcodec_get_name(video->codec_id), av_get_pix_fmt_name((AVPixelFormat)video->format), m_global_tb.num, m_global_tb.den);
	} else {
		if (video->codec_id != m_video_params->codec_id) {
			*error = string_printf("\"%s\" uses %s video but the first file uses %s. All the files of one flight have to match.",
				name.c_str(), avcodec_get_name(video->codec_id), avcodec_get_name(m_video_params->codec_id));
			return false;
		}
		if (video->width != m_width || video->height != m_height) {
			*error = string_printf("\"%s\" is %dx%d but the first file is %dx%d. All the files of one flight have to match.",
				name.c_str(), video->width, video->height, m_width, m_height);
			return false;
		}
		double ratio = av_q2d(avg_rate) / av_q2d(m_avg_frame_rate);
		if (fabs(ratio - 1.0) > 0.001) {
			*error = string_printf("\"%s\" runs at %.3f frames per second but the first file runs at %.3f. All the files of one flight have to match.",
				name.c_str(), av_q2d(avg_rate), av_q2d(m_avg_frame_rate));
			return false;
		}
	}

	// Audio has to be present in all the files or in none, and the same kind.
	if (index > 0) {
		bool first_has_audio = m_audio_params != nullptr;
		if (first_has_audio != (m_audio_index >= 0)) {
			*error = string_printf("\"%s\" %s sound but the first file %s. All the files of one flight have to match.", name.c_str(),
				m_audio_index >= 0 ? "has" : "has no", first_has_audio ? "does" : "does not");
			return false;
		}
		if (m_audio_index >= 0) {
			AVCodecParameters* audio = m_fmt->streams[m_audio_index]->codecpar;
			if (audio->codec_id != m_audio_params->codec_id || audio->sample_rate != m_audio_params->sample_rate ||
				audio->ch_layout.nb_channels != m_audio_params->ch_layout.nb_channels) {
				*error = string_printf("The sound in \"%s\" is not the same kind as in the first file. All the files of one flight have to match.",
					name.c_str());
				return false;
			}
		}
	}

	// Length, in global ticks.
	int64_t duration = video_stream->duration;
	if (duration == AV_NOPTS_VALUE || duration <= 0) {
		if (m_fmt->duration == AV_NOPTS_VALUE || m_fmt->duration <= 0) {
			*error = string_printf("Could not work out how long \"%s\" is.", name.c_str());
			return false;
		}
		duration = av_rescale_q(m_fmt->duration, AV_TIME_BASE_Q, m_file_video_tb);
	}
	int64_t ticks = av_rescale_q(duration, m_file_video_tb, m_global_tb);
	m_media_durations.push_back(ticks);
	if (!m_quiet)
		printf("  %s: %.2f s%s (video starts at %.4f s)\n", name.c_str(), ticks_to_seconds(ticks), m_audio_index >= 0 ? "" : " (no sound)",
			(double)m_video_start * m_file_video_tb.num / m_file_video_tb.den);
	return true;
}

bool MediaDecoder::open(const std::vector<std::string>& paths, std::string* error) {
	std::string local_error;
	if (error == nullptr)
		error = &local_error;
	error->clear();

	// Starting over, in case this object was used before.
	close_file();
	end_audio_capture();
	m_clips.clear();
	m_media_durations.clear();
	m_cur_clip = 0;
	m_cache.clear();
	m_cache_bytes = 0;
	if (m_sws) {
		sws_freeContext(m_sws);
		m_sws = nullptr;
	}
	av_packet_free(&m_packet);
	av_frame_free(&m_decoded);
	av_frame_free(&m_frame);
	m_last_error.clear();
	m_pts = 0;
	m_decoder_pts = AV_NOPTS_VALUE;

	if (paths.empty()) {
		*error = "There are no video files to open.";
		return false;
	}
	m_paths = paths;

	// First pass: look at every file, so a mismatched or unreadable one is reported before any
	// time is spent on the rest.
	for (int i = 0; i < (int)m_paths.size(); i++) {
		if (!open_file(i, error) || !read_file_properties(i, error)) {
			close_file();
			m_clips.clear();
			m_media_durations.clear();
			return false;
		}
	}
	// Until told otherwise the clips are the files, each whole, in the order given.
	{
		std::vector<Clip> whole_files;
		for (int i = 0; i < (int)m_paths.size(); i++)
			whole_files.push_back(Clip(i, 0, m_media_durations[i]));
		m_clips.set(whole_files);
	}

	// Second pass: start at the beginning of the first.
	if (!open_file(0, error)) {
		m_clips.clear();
		m_media_durations.clear();
		return false;
	}

	m_packet = av_packet_alloc();
	m_decoded = av_frame_alloc();
	m_frame = av_frame_alloc();
	if (!m_packet || !m_decoded || !m_frame) {
		*error = "Out of memory while opening the video.";
		close_file();
		m_clips.clear();
		m_media_durations.clear();
		av_packet_free(&m_packet);
		av_frame_free(&m_decoded);
		av_frame_free(&m_frame);
		return false;
	}
	m_frame->format = AV_PIX_FMT_YUV420P;
	m_frame->width = m_width;
	m_frame->height = m_height;
	if (av_frame_get_buffer(m_frame, 32) < 0) {
		*error = "Out of memory while opening the video.";
		close_file();
		m_clips.clear();
		m_media_durations.clear();
		av_packet_free(&m_packet);
		av_frame_free(&m_decoded);
		av_frame_free(&m_frame);
		return false;
	}

	if (!decode_next()) {
		*error = m_last_error.empty() ? std::string("Could not decode any video from the first file.") : m_last_error;
		close_file();
		m_clips.clear();
		m_media_durations.clear();
		av_packet_free(&m_packet);
		av_frame_free(&m_decoded);
		av_frame_free(&m_frame);
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------------------------
// Reading frames
// ---------------------------------------------------------------------------------------------

// Opens the file after the current one. False if the current one is the last (or the next
// cannot be opened, which is also recorded in m_last_error).
// Moves on to the clip after the current one, because the current one has ended: either a frame
// at or past its end turned up (have_frame, at frame_local_ticks into its file), or the file ran
// out. If the next clip carries on from the same place in the same file, nothing is reopened or
// re-read; *keep_frame says the frame in hand is already the next clip's first. False at the end
// of the last clip.
bool MediaDecoder::next_clip(bool have_frame, int64_t frame_local_ticks, bool* keep_frame) {
	*keep_frame = false;
	int next = m_cur_clip + 1;
	if (next >= m_clips.count())
		return false;
	const Clip& target = m_clips.clip(next);
	const Clip& old_clip = m_clips.clip(m_cur_clip);
	const int64_t half_frame = m_frame_ticks / 2;
	bool same_media = target.media == m_cur_file && m_fmt != nullptr;
	bool file_ran_out = m_draining || !have_frame;

	m_pts_before_switch = m_decoder_pts;
	// The gap check is about frames lost between files, so it only applies when the footage
	// carries on from the end of a file into another, or the file ended early.
	m_check_gap = target.media != old_clip.media || file_ran_out;
	m_cur_clip = next;

	if (same_media && !file_ran_out) {
		// The decoder is in the right file already and still has frames. If the frame in hand is
		// the one the next clip starts on, the next clip simply carries on from here; if the clip
		// starts further back (an overlap, or a piece used twice), it has to seek back.
		if (frame_local_ticks >= target.in_ticks - half_frame && frame_local_ticks < target.in_ticks + half_frame) {
			*keep_frame = true;
			return true;
		}
		if (frame_local_ticks < target.in_ticks - half_frame && target.in_ticks - frame_local_ticks < MAX_COAST_FRAMES * m_frame_ticks) {
			return true;             // Its start is a little ahead; decode on to it.
		}
	}
	return position_media(target.media, target.in_ticks);
}

// Gets the decoder ready to read file `media` from the keyframe at or before `local_ticks`
// (counted from the start of that file), opening the file first if it isn't the one open.
bool MediaDecoder::position_media(int media, int64_t local_ticks) {
	bool just_opened = false;
	if (media != m_cur_file || m_fmt == nullptr) {
		std::string error;
		if (!open_file(media, &error)) {
			m_last_error = error;
			return false;
		}
		just_opened = true;
	}
	if (!(just_opened && local_ticks <= 0)) {
		int64_t file_ticks = av_rescale_q(local_ticks, m_global_tb, m_file_video_tb) + m_video_start;
		if (av_seek_frame(m_fmt, m_video_index, file_ticks, AVSEEK_FLAG_BACKWARD) < 0) {
			// Some files have no usable index; the beginning is always a safe place to land.
			av_seek_frame(m_fmt, m_video_index, m_video_start, AVSEEK_FLAG_BACKWARD);
		}
	}
	avcodec_flush_buffers(m_vctx);
	m_draining = false;
	// Packets already queued for an export in progress still belong to it; otherwise they're stale.
	if (!m_capture_audio) {
		while (!m_audio_queue.empty()) {
			av_packet_free(&m_audio_queue.front());
			m_audio_queue.pop_front();
		}
	}
	return true;
}

// Where a decoded frame is in its own file, counted from the start of the file in ticks.
int64_t MediaDecoder::local_ticks_of(const AVFrame* frame) const {
	int64_t raw_pts = frame->pts;
	if (raw_pts == AV_NOPTS_VALUE)
		raw_pts = frame->best_effort_timestamp;
	if (raw_pts != AV_NOPTS_VALUE)
		return av_rescale_q(raw_pts - m_video_start, m_file_video_tb, m_global_tb);
	// No timestamp at all: one frame after the last one.
	const Clip& clip = m_clips.clip(m_cur_clip);
	if (m_decoder_pts == AV_NOPTS_VALUE)
		return clip.in_ticks;
	return clip.in_ticks + (m_decoder_pts - m_clips.start(m_cur_clip)) + m_frame_ticks;
}

void MediaDecoder::queue_audio_packet() {
	// Which clip is this sound for? Packets are read a little ahead of the picture, so it can
	// belong to a following clip of the same file as well as the current one.
	int64_t packet_time = (m_packet->pts != AV_NOPTS_VALUE) ? m_packet->pts : m_packet->dts;
	int clip_index = m_cur_clip;
	if (packet_time != AV_NOPTS_VALUE) {
		int64_t local_start = av_rescale_q(packet_time - m_audio_start, m_file_audio_tb, m_global_tb);
		int64_t local_end = local_start + av_rescale_q(m_packet->duration, m_file_audio_tb, m_global_tb);
		if (local_end <= local_start)
			local_end = local_start + 1;
		clip_index = -1;
		for (int c = m_cur_clip; c < m_clips.count() && c <= m_cur_clip + 3; c++) {
			const Clip& clip = m_clips.clip(c);
			if (clip.media == m_cur_file && local_end > clip.in_ticks && local_start < clip.out_ticks) {
				clip_index = c;
				break;
			}
		}
		if (clip_index < 0)
			return;   // Sound from a part of the file that isn't in the edit.
	}

	AVPacket* copy = av_packet_clone(m_packet);
	if (copy == nullptr)
		return;
	// Onto the timeline: take off this file's own start, change to the first file's audio time
	// base, and move by where the clip begins relative to where it was cut from. That is
	// converted as a whole (rather than adding up roundings), so the error never builds up.
	int64_t clip_offset = av_rescale_q(m_clips.start(clip_index) - m_clips.clip(clip_index).in_ticks, m_global_tb, m_audio_tb);
	if (copy->pts != AV_NOPTS_VALUE)
		copy->pts = av_rescale_q(copy->pts - m_audio_start, m_file_audio_tb, m_audio_tb) + clip_offset;
	if (copy->dts != AV_NOPTS_VALUE)
		copy->dts = av_rescale_q(copy->dts - m_audio_start, m_file_audio_tb, m_audio_tb) + clip_offset;
	copy->duration = av_rescale_q(copy->duration, m_file_audio_tb, m_audio_tb);
	copy->pos = -1;
	m_audio_queue.push_back(copy);
}

// Turns what the decoder just produced (m_decoded) into the current frame, at `local_ticks` into
// its file, and works out its time on the edit timeline.
bool MediaDecoder::store_decoded_frame(int64_t local_ticks) {
	AVFrame* src = m_decoded;
	int64_t pts = m_clips.timeline_for(m_cur_clip, local_ticks);

	bool is_420 = src->format == AV_PIX_FMT_YUV420P || src->format == AV_PIX_FMT_YUVJ420P;
	if (is_420 && src->width == m_width && src->height == m_height) {
		av_image_copy(m_frame->data, m_frame->linesize, (const uint8_t**)src->data, src->linesize, AV_PIX_FMT_YUV420P, m_width, m_height);
	} else {
		// Anything else (10-bit, 4:2:2, a mid-stream size change) goes through swscale.
		m_sws = sws_getCachedContext(m_sws, src->width, src->height, (AVPixelFormat)src->format, m_width, m_height,
			AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
		if (m_sws == nullptr) {
			m_last_error = "Could not convert the video to a format CamBot can show.";
			return false;
		}
		sws_scale(m_sws, src->data, src->linesize, 0, src->height, m_frame->data, m_frame->linesize);
	}
	m_frame->pts = pts;
	m_pts = pts;
	m_decoder_pts = pts;

	if (m_check_gap) {
		m_check_gap = false;
		if (m_pts_before_switch != AV_NOPTS_VALUE) {
			double gap_frames = (double)(pts - m_pts_before_switch) / (double)m_frame_ticks;
			// One frame apart is perfect; anything else means footage was lost or doubled at the join.
			if (fabs(gap_frames - 1.0) > 0.5)
				printf("Note: file %d starts %.1f frames after the previous one ends (1.0 is seamless).\n", m_cur_file + 1, gap_frames);
		}
	}
	return true;
}

// The next frame in order after the one on screen. Normally that means decoding one, but when
// the picture on screen was answered from the cache the decoder is ahead of it, and the next
// frame is the next one in the cache.
bool MediaDecoder::read_frame() {
	if (m_frame == nullptr)
		return false;
	if (m_decoder_pts != AV_NOPTS_VALUE && m_pts != m_decoder_pts) {
		if (serve_from_cache(m_pts + 1))
			return true;
		// The frame after the one on screen was never cached (the cache only holds what
		// advance_to() decoded). Decoding on would skip it, so find it the long way.
		return advance_to(m_pts + 1);
	}
	return decode_next();
}

// Decodes the next frame the decoder has, whatever is on screen. Frames that aren't in the
// current clip are passed over: those before it (decoded from the keyframe on the way to its
// start) are dropped, and the first one past its end moves on to the next clip.
bool MediaDecoder::decode_next() {
	if (m_frame == nullptr)
		return false;
	int read_errors = 0;
	bool have_decoded = false;   // m_decoded holds a frame that hasn't been placed yet.
	while (true) {
		if (!have_decoded) {
			int result = avcodec_receive_frame(m_vctx, m_decoded);
			if (result == 0) {
				have_decoded = true;
			} else if (result == AVERROR_EOF) {
				// This file's decoder has handed over every frame it had. Carry on with the next
				// clip. (Draining it first is what keeps each file's last few frames from being lost.)
				bool keep = false;
				if (!next_clip(false, 0, &keep))
					return false;
				continue;
			} else if (m_draining) {
				// AVERROR(EAGAIN) means it wants more data, and any other error is a bad packet,
				// which we skip by feeding it the next one. But already told it the file is over, yet no
				// more frames and no EOF: don't spin.
				bool keep = false;
				if (!next_clip(false, 0, &keep))
					return false;
				continue;
			} else {
				result = av_read_frame(m_fmt, m_packet);
				if (result < 0) {
					av_packet_unref(m_packet);
					// End of file, or the file stopped being readable: either way, no more from it.
					if (result == AVERROR_EOF || ++read_errors > 100) {
						avcodec_send_packet(m_vctx, nullptr);
						m_draining = true;
					}
					continue;
				}
				read_errors = 0;
				if (m_packet->stream_index == m_video_index) {
					// Garmin VIRB files mark their last video packet "discard" (the MP4's edit list ends
					// a frame early), and the decoder then drops that picture, which loses one frame at
					// every join. The picture is good, so keep it. Packets before the start of the stream
					// (a decoder warm-up the edit list really does hide) stay discarded.
					if (m_packet->flags & AV_PKT_FLAG_DISCARD) {
						int64_t packet_time = (m_packet->pts != AV_NOPTS_VALUE) ? m_packet->pts : m_packet->dts;
						if (packet_time != AV_NOPTS_VALUE && packet_time >= m_video_start)
							m_packet->flags &= ~AV_PKT_FLAG_DISCARD;
					}
					avcodec_send_packet(m_vctx, m_packet);
				} else if (m_packet->stream_index == m_audio_index && m_capture_audio) {
					queue_audio_packet();
				}
				av_packet_unref(m_packet);
				continue;
			}
		}

		// A frame is in hand: is it in the current clip? Frames are matched to clips by the
		// nearest frame, so a cut that falls between two frames is half a frame either way.
		const int64_t half_frame = m_frame_ticks / 2;
		int64_t local = local_ticks_of(m_decoded);
		const Clip& clip = m_clips.clip(m_cur_clip);
		if (local < clip.in_ticks - half_frame) {
			av_frame_unref(m_decoded);       // Before the clip: only decoded to get here.
			have_decoded = false;
			continue;
		}
		if (local >= clip.out_ticks - half_frame) {
			bool keep = false;
			if (!next_clip(true, local, &keep)) {
				av_frame_unref(m_decoded);
				return false;
			}
			if (!keep) {
				av_frame_unref(m_decoded);
				have_decoded = false;
			}
			continue;
		}
		bool stored = store_decoded_frame(local);
		av_frame_unref(m_decoded);
		return stored;
	}
}

// ---------------------------------------------------------------------------------------------
// Seeking
// ---------------------------------------------------------------------------------------------

// Points the demuxer at the keyframe at or before `ticks` of the timeline (opening that clip's
// file if it isn't the current one). Doesn't read a frame.
bool MediaDecoder::seek_to(int64_t ticks) {
	if (ticks < 0)
		ticks = 0;
	if (ticks >= total_ticks())
		ticks = total_ticks() - 1;
	if (ticks < 0)
		ticks = 0;

	FileTime source = m_clips.source_for(ticks);
	m_cur_clip = m_clips.clip_index_for(ticks);
	if (!position_media(source.media, source.local_ticks))
		return false;
	m_decoder_pts = AV_NOPTS_VALUE;
	m_check_gap = false;
	return true;
}

bool MediaDecoder::serve_from_cache(int64_t ticks) {
	if (m_cache.empty() || ticks < m_cache.front().pts || ticks > m_cache.back().pts)
		return false;
	// Frames are in decode order, which is time order: find the first one at or after `ticks`.
	size_t low = 0;
	size_t high = m_cache.size() - 1;
	while (low < high) {
		size_t middle = low + (high - low) / 2;
		if (m_cache[middle].pts >= ticks)
			high = middle;
		else
			low = middle + 1;
	}
	const CachedFrame& cached = m_cache[low];
	copy_plane_in(m_frame, 0, cached.y, m_width, m_height);
	copy_plane_in(m_frame, 1, cached.u, m_width / 2, m_height / 2);
	copy_plane_in(m_frame, 2, cached.v, m_width / 2, m_height / 2);
	m_frame->pts = cached.pts;
	m_pts = cached.pts;
	return true;
}

// Shows the last cached frame at or before `ticks`: the opposite question to serve_from_cache().
bool MediaDecoder::serve_from_cache_before(int64_t ticks) {
	if (m_cache.empty() || ticks < m_cache.front().pts)
		return false;
	size_t found = 0;
	for (size_t i = 0; i < m_cache.size() && m_cache[i].pts <= ticks; i++)
		found = i;
	const CachedFrame& cached = m_cache[found];
	copy_plane_in(m_frame, 0, cached.y, m_width, m_height);
	copy_plane_in(m_frame, 1, cached.u, m_width / 2, m_height / 2);
	copy_plane_in(m_frame, 2, cached.v, m_width / 2, m_height / 2);
	m_frame->pts = cached.pts;
	m_pts = cached.pts;
	return true;
}

// Adds the current frame to the cache if it directly follows what's already there (or if it's
// the first frame after a seek). Frames are only kept while they form one unbroken run, so
// anything in the cache can be shown without decoding.
void MediaDecoder::cache_add_current(int64_t previous_decoder_pts, bool starts_new_run) {
	uint64_t frame_bytes = (uint64_t)m_width * (uint64_t)m_height * 3 / 2;
	if (m_cache_bytes + frame_bytes > CACHE_BUDGET_BYTES)
		return;
	bool continues_run = !m_cache.empty() && previous_decoder_pts != AV_NOPTS_VALUE && m_cache.back().pts == previous_decoder_pts;
	if (!starts_new_run && !continues_run)
		return;
	CachedFrame cached;
	cached.pts = m_pts;
	copy_plane_out(&cached.y, m_frame, 0, m_width, m_height);
	copy_plane_out(&cached.u, m_frame, 1, m_width / 2, m_height / 2);
	copy_plane_out(&cached.v, m_frame, 2, m_width / 2, m_height / 2);
	m_cache.push_back(std::move(cached));
	m_cache_bytes += frame_bytes;
}

bool MediaDecoder::advance_to(int64_t ticks) {
	if (!is_open())
		return false;
	if (ticks < 0)
		ticks = 0;
	if (ticks > total_ticks())
		ticks = total_ticks();

	// A GOP we've already decoded through can answer for free: no seek and no decoding.
	if (serve_from_cache(ticks))
		return true;

	// Seeking costs the same wherever it goes, while decoding forward costs a frame at a time,
	// so only a short hop forward from where the *decoder* is (which can be ahead of what's
	// on screen when the cache answered last time) skips the seek.
	int64_t distance = (m_decoder_pts == AV_NOPTS_VALUE) ? -1 : ticks - m_decoder_pts;
	bool coast = distance > 0 && distance < MAX_COAST_FRAMES * m_frame_ticks;
	bool starts_new_run = false;
	if (!coast) {
		if (!seek_to(ticks))
			return false;
		m_cache.clear();
		m_cache_bytes = 0;
		starts_new_run = true;
	}

	bool got_frame = false;
	while (true) {
		int64_t previous_decoder_pts = m_decoder_pts;
		if (!decode_next())
			break;  // End of the media (the last frame stays up), or an unreadable file.
		got_frame = true;
		cache_add_current(previous_decoder_pts, starts_new_run);
		starts_new_run = false;
		if (m_pts >= ticks)
			break;
	}
	if (!got_frame && coast && m_decoder_pts != AV_NOPTS_VALUE && m_pts != m_decoder_pts) {
		// Ran into the end of the media while the picture on screen was an older one (it had come
		// from the cache): show the last frame there is.
		return advance_to(m_decoder_pts);
	}
	return got_frame || coast;
}

bool MediaDecoder::set_clips(const std::vector<Clip>& clips, int64_t show_ticks) {
	if (!is_open() || clips.empty())
		return false;
	for (size_t i = 0; i < clips.size(); i++) {
		const Clip& clip = clips[i];
		if (clip.media < 0 || clip.media >= media_count())
			return false;
		if (clip.in_ticks < 0 || clip.out_ticks <= clip.in_ticks || clip.out_ticks > m_media_durations[clip.media])
			return false;
	}
	m_clips.set(clips);
	// Nothing decoded so far means anything in the new arrangement: forget it all, and force the
	// next move to seek.
	m_cache.clear();
	m_cache_bytes = 0;
	m_decoder_pts = AV_NOPTS_VALUE;
	m_cur_clip = 0;
	return advance_to(show_ticks);
}

bool MediaDecoder::step_frames(int64_t count) {
	// Half a frame of slack, because the frames' real spacing isn't always a whole number of
	// ticks: "first frame at or after (wanted - half a frame)" is the wanted frame.
	int64_t start_pts = m_pts;
	int64_t wanted = start_pts + count * m_frame_ticks;
	bool moved = advance_to(wanted - m_frame_ticks / 2);
	if (count < 0 && m_pts >= start_pts) {
		// Asking for "the first frame at or after" a time before this one found this one again.
		// That happens at a join when the earlier file's last frame ends well short of where
		// the file says it ends (cameras do this): there is no frame near the wanted time, so the
		// first one at or after it is in the next file. The decoder has just run through the
		// frames before this one, so the earlier frame is in the cache: show the nearest one.
		int64_t limit = (count == -1) ? start_pts - 1 : wanted;
		if (serve_from_cache_before(limit) && m_pts < start_pts) {
			printf("Note: stepping back from %.3f s, the nearest earlier frame is at %.3f s (%.1f frames before).\n",
				ticks_to_seconds(start_pts), ticks_to_seconds(m_pts), (double)(start_pts - m_pts) / (double)m_frame_ticks);
			return true;
		}
	}
	return moved;
}

bool MediaDecoder::seek_keyframe(int64_t ticks) {
	if (!is_open())
		return false;
	if (!seek_to(ticks))
		return false;
	m_cache.clear();
	m_cache_bytes = 0;
	if (!decode_next())
		return false;
	cache_add_current(AV_NOPTS_VALUE, true);
	return true;
}

bool MediaDecoder::seek_by_frames_fast(int64_t count) {
	if (!is_open())
		return false;
	int64_t target = m_pts + count * m_frame_ticks;
	if (target < 0)
		target = 0;
	if (count > 0) {
		// Only jump if a keyframe really lies between here and the target -- otherwise
		// "keyframe at or before" would land behind us, a step backward dressed up as a seek.
		// Checked via the file's index (read-only); a speculative seek we then had to undo would
		// cost the very GOP-decode burst this function exists to avoid.
		int target_clip = m_clips.clip_index_for(target);
		int now_clip = m_clips.clip_index_for(m_pts);
		if (target_clip == now_clip && m_clips.clip(now_clip).media == m_cur_file && m_fmt != nullptr) {
			AVStream* stream = m_fmt->streams[m_video_index];
			FileTime target_source = m_clips.source_for(target);
			FileTime now_source = m_clips.source_for(m_pts);
			int64_t file_target = av_rescale_q(target_source.local_ticks, m_global_tb, m_file_video_tb) + m_video_start;
			int64_t file_now = av_rescale_q(now_source.local_ticks, m_global_tb, m_file_video_tb) + m_video_start;
			const AVIndexEntry* entry = avformat_index_get_entry_from_timestamp(stream, file_target, AVSEEK_FLAG_BACKWARD);
			if (entry != nullptr && entry->timestamp > file_now)
				return seek_keyframe(target);
			return step_frames(count);  // Still inside this GOP: decode forward for real.
		}
		return seek_keyframe(target);  // A clip boundary is in the way, and the next clip starts where it is cut.
	}
	return seek_keyframe(target);
}

void MediaDecoder::release_cache() {
	std::vector<CachedFrame>().swap(m_cache);
	m_cache_bytes = 0;
}

bool MediaDecoder::keyframe_times(int media, std::vector<int64_t>* local_ticks) {
	local_ticks->clear();
	if (!is_open() || media < 0 || media >= media_count())
		return false;
	if (media != m_cur_file || m_fmt == nullptr) {
		std::string error;
		if (!open_file(media, &error)) {
			m_last_error = error;
			return false;
		}
	}
	AVStream* stream = m_fmt->streams[m_video_index];
	// The index is asked for the keyframe at or before a time, starting from the end of the file and going
	// back one keyframe at a time (nothing else of it is needed, and it is the call the decoder already makes).
	std::vector<int64_t> backwards;
	int64_t at = m_video_start + av_rescale_q(m_media_durations[media], m_global_tb, m_file_video_tb);
	while (true) {
		const AVIndexEntry* entry = avformat_index_get_entry_from_timestamp(stream, at, AVSEEK_FLAG_BACKWARD);
		if (entry == nullptr)
			break;
		if (!backwards.empty() && entry->timestamp >= backwards.back())
			break;   // Nothing earlier.
		backwards.push_back(entry->timestamp);
		at = entry->timestamp - 1;
	}
	for (size_t i = backwards.size(); i > 0; i--) {
		int64_t ticks = av_rescale_q(backwards[i - 1] - m_video_start, m_file_video_tb, m_global_tb);
		if (ticks < 0)
			ticks = 0;
		if (!local_ticks->empty() && ticks <= local_ticks->back())
			continue;
		local_ticks->push_back(ticks);
	}
	return true;
}

// ---------------------------------------------------------------------------------------------
// Audio capture
// ---------------------------------------------------------------------------------------------

void MediaDecoder::begin_audio_capture() {
	end_audio_capture();
	m_capture_audio = true;
}

void MediaDecoder::end_audio_capture() {
	m_capture_audio = false;
	while (!m_audio_queue.empty()) {
		av_packet_free(&m_audio_queue.front());
		m_audio_queue.pop_front();
	}
}

AVPacket* MediaDecoder::pop_audio_packet() {
	if (m_audio_queue.empty())
		return nullptr;
	AVPacket* packet = m_audio_queue.front();
	m_audio_queue.pop_front();
	return packet;
}
