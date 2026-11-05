#include "audio_file_reader.h"

#include <stdlib.h>
#include <string.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include "audio_plan.h"
#include "file_io.h"

namespace {

std::string error_text(int code) {
	char text[AV_ERROR_MAX_STRING_SIZE];
	av_strerror(code, text, sizeof(text));
	return text;
}

// Decoders need a little sound from before the place that is wanted to warm up on (the first frame out after a seek is not
// reliable), so a seek lands this far early and the extra is thrown away.
const int64_t SEEK_PREROLL = 4096;

// Two frames starting this close to where the last one ended are taken to be one run of sound, whatever rounding the file's
// timestamps have, so their small disagreements don't turn into clicks.
const int64_t CONTINUOUS_TOLERANCE = 64;

}  // namespace

AudioFileReader::AudioFileReader()
	: m_format(nullptr), m_decoder(nullptr), m_resampler(nullptr), m_packet(nullptr), m_frame(nullptr), m_stream(-1), m_start(0),
	  m_position(0), m_finished(false), m_flushing(false), m_next_known(false), m_next_decoded(0), m_buffer_start(0) {
}

AudioFileReader::~AudioFileReader() {
	close();
}

void AudioFileReader::close() {
	swr_free(&m_resampler);
	av_packet_free(&m_packet);
	av_frame_free(&m_frame);
	avcodec_free_context(&m_decoder);
	avformat_close_input(&m_format);
	m_stream = -1;
	m_buffer.clear();
	m_position = 0;
}

bool AudioFileReader::open(const std::string& path, std::string* error) {
	close();
	const std::string name = fileio::base_name(path);
	int result = avformat_open_input(&m_format, path.c_str(), nullptr, nullptr);
	if (result < 0) {
		*error = "Could not open \"" + name + "\" for its sound: " + error_text(result);
		m_format = nullptr;
		return false;
	}
	result = avformat_find_stream_info(m_format, nullptr);
	if (result < 0) {
		*error = "Could not read \"" + name + "\": " + error_text(result);
		close();
		return false;
	}
	m_stream = av_find_best_stream(m_format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
	if (m_stream < 0) {
		*error = "\"" + name + "\" has no sound.";
		close();
		return false;
	}
	AVStream* stream = m_format->streams[m_stream];
	const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
	if (codec == nullptr) {
		*error = "There is no decoder for the sound in \"" + name + "\".";
		close();
		return false;
	}
	m_decoder = avcodec_alloc_context3(codec);
	if (m_decoder == nullptr || avcodec_parameters_to_context(m_decoder, stream->codecpar) < 0) {
		*error = "Out of memory while setting up the sound of \"" + name + "\".";
		close();
		return false;
	}
	result = avcodec_open2(m_decoder, codec, nullptr);
	if (result < 0) {
		*error = "Could not start the sound decoder for \"" + name + "\": " + error_text(result);
		close();
		return false;
	}
	m_packet = av_packet_alloc();
	m_frame = av_frame_alloc();
	if (m_packet == nullptr || m_frame == nullptr) {
		*error = "Out of memory while setting up the sound of \"" + name + "\".";
		close();
		return false;
	}
	// Sample 0 is where this stream starts, as it is for the picture (see MediaDecoder), so the two agree.
	m_start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
	reset_decoding();
	m_position = 0;
	return true;
}

// Forgets whatever has been decoded and where the decoder had got to.
void AudioFileReader::reset_decoding() {
	swr_free(&m_resampler);
	m_buffer.clear();
	m_buffer_start = 0;
	m_finished = false;
	m_flushing = false;
	m_next_known = false;
}

bool AudioFileReader::seek(int64_t sample) {
	if (!is_open())
		return false;
	if (sample < 0)
		sample = 0;
	AVStream* stream = m_format->streams[m_stream];
	const AVRational sample_base = { 1, audio_plan::SAMPLE_RATE };
	int64_t from = sample - SEEK_PREROLL;
	if (from < 0)
		from = 0;
	const int64_t target = m_start + av_rescale_q(from, sample_base, stream->time_base);
	// Landing at or before the target is what is wanted; failing to seek at all (a file that can't) is not fatal when the
	// target is the very start, as the reader is already there after open().
	int result = av_seek_frame(m_format, m_stream, target, AVSEEK_FLAG_BACKWARD);
	if (result < 0 && from > 0)
		return false;
	if (result < 0)
		result = avformat_seek_file(m_format, m_stream, INT64_MIN, m_start, m_start, 0);
	avcodec_flush_buffers(m_decoder);
	reset_decoding();
	m_position = sample;
	return true;
}

// Resamples one decoded frame into m_buffer, and says where it goes.
bool AudioFileReader::convert(const AVFrame* frame) {
	if (m_resampler == nullptr) {
		AVChannelLayout stereo;
		av_channel_layout_default(&stereo, 2);
		AVChannelLayout input = frame->ch_layout;
		if (input.order == AV_CHANNEL_ORDER_UNSPEC)
			av_channel_layout_default(&input, input.nb_channels > 0 ? input.nb_channels : 2);
		if (swr_alloc_set_opts2(&m_resampler, &stereo, AV_SAMPLE_FMT_FLT, audio_plan::SAMPLE_RATE, &input,
			(AVSampleFormat)frame->format, frame->sample_rate, 0, nullptr) < 0 || swr_init(m_resampler) < 0) {
			swr_free(&m_resampler);
			return false;
		}
	}
	const int capacity = swr_get_out_samples(m_resampler, frame->nb_samples);
	if (capacity <= 0)
		return false;
	m_buffer.assign((size_t)capacity * 2, 0.0f);
	uint8_t* out_planes[1] = { (uint8_t*)m_buffer.data() };
	const int produced = swr_convert(m_resampler, out_planes, capacity, (const uint8_t**)frame->extended_data, frame->nb_samples);
	if (produced < 0)
		return false;
	m_buffer.resize((size_t)produced * 2);

	// Where it goes: straight after the last frame if the file's timestamps say that too (give or take their rounding),
	// otherwise where they say.
	const AVRational sample_base = { 1, audio_plan::SAMPLE_RATE };
	int64_t pts = frame->pts;
	if (pts == AV_NOPTS_VALUE)
		pts = frame->best_effort_timestamp;
	bool placed = false;
	int64_t start = 0;
	if (pts != AV_NOPTS_VALUE) {
		start = av_rescale_q(pts - m_start, m_format->streams[m_stream]->time_base, sample_base);
		placed = true;
	}
	if (m_next_known && (!placed || llabs(start - m_next_decoded) <= CONTINUOUS_TOLERANCE))
		start = m_next_decoded;
	else if (!placed)
		start = m_position;
	m_buffer_start = start;
	m_next_decoded = start + produced;
	m_next_known = true;
	return true;
}

bool AudioFileReader::decode_more() {
	while (!m_finished) {
		int result = avcodec_receive_frame(m_decoder, m_frame);
		if (result == 0) {
			const bool converted = convert(m_frame);
			av_frame_unref(m_frame);
			if (converted && !m_buffer.empty())
				return true;
			continue;
		}
		if (result == AVERROR_EOF) {
			m_finished = true;
			break;
		}
		if (result != AVERROR(EAGAIN)) {
			m_finished = true;   // A decoder that has failed is as good as at the end.
			break;
		}
		if (m_flushing) {
			m_finished = true;
			break;
		}
		// It needs more: the next packet of sound (those of other streams are skipped).
		result = av_read_frame(m_format, m_packet);
		if (result < 0) {
			m_flushing = true;
			avcodec_send_packet(m_decoder, nullptr);
			continue;
		}
		if (m_packet->stream_index == m_stream)
			avcodec_send_packet(m_decoder, m_packet);
		av_packet_unref(m_packet);
	}
	m_buffer.clear();
	return false;
}

int AudioFileReader::read(float* out, int frames) {
	if (!is_open())
		return 0;
	int delivered = 0;
	while (delivered < frames) {
		const int64_t buffered = (int64_t)(m_buffer.size() / 2);
		const int64_t buffer_end = m_buffer_start + buffered;
		const int wanted = frames - delivered;
		if (buffered > 0 && m_position < m_buffer_start) {
			// A hole in the file's sound.
			int64_t gap = m_buffer_start - m_position;
			if (gap > wanted)
				gap = wanted;
			memset(out + (size_t)delivered * 2, 0, sizeof(float) * (size_t)gap * 2);
			delivered += (int)gap;
			m_position += gap;
		} else if (buffered > 0 && m_position < buffer_end) {
			int64_t available = buffer_end - m_position;
			if (available > wanted)
				available = wanted;
			memcpy(out + (size_t)delivered * 2, m_buffer.data() + (size_t)(m_position - m_buffer_start) * 2, sizeof(float) * (size_t)available * 2);
			delivered += (int)available;
			m_position += available;
		} else if (!decode_more()) {
			break;
		}
	}
	return delivered;
}
