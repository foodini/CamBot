#include "fake_ffmpeg.h"

#include <map>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <deque>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

static const int FRAME_TICKS = 30;   // 1/600 time base, 20 fps.

struct FakeFile {
	FakeFileSpec spec;
	AVIndexEntry index_entry;
};

static std::map<std::string, FakeFileSpec> g_files;

// Demuxer state hung off the AVFormatContext (found through a side table, so no AVFormatContext internals are needed).
struct FakeDemux {
	FakeFileSpec spec;
	int next_video;        // Next video packet to hand out.
	bool audio_pending;    // An audio packet is due after the video packet just given.
	int next_audio;
	AVIndexEntry entry;
};
// What a fake video packet carries in its payload, for the fake decoder to build the picture from.
struct PacketInfo {
	int       frame;
	int       tag;
	int       delay;
	int       width, height;
	long long pts;
	int       discard;   // The packet said "decode me but don't show me".
};
struct FakeDecoder {
	std::deque<PacketInfo> held;  // Frames waiting for the delay to pass.
	int  delay;
	bool draining;
};

static std::map<AVFormatContext*, FakeDemux*> g_demux;
static std::map<AVStream*, FakeDemux*> g_stream_demux;
static std::map<AVCodecContext*, FakeDecoder*> g_decoders;

static int g_seek_count = 0;
void fake_ffmpeg_reset() { g_files.clear(); }
int fake_ffmpeg_seek_count() { return g_seek_count; }
void fake_ffmpeg_add_file(const std::string& path, const FakeFileSpec& spec) { g_files[path] = spec; }

extern "C" {

int av_strerror(int errnum, char* errbuf, size_t errbuf_size) { snprintf(errbuf, errbuf_size, "fake error %d", errnum); return 0; }
const char* avcodec_get_name(enum AVCodecID) { return "fakecodec"; }
const char* av_get_pix_fmt_name(enum AVPixelFormat) { return "yuv420p"; }

int64_t av_rescale_q(int64_t a, AVRational bq, AVRational cq) {
	// Rounded to nearest, like FFmpeg's.
	long double v = (long double)a * bq.num * cq.den / ((long double)bq.den * cq.num);
	return (int64_t)(v < 0 ? v - 0.5L : v + 0.5L);
}

// ---- packets and frames ----
AVPacket* av_packet_alloc(void) { return (AVPacket*)calloc(1, sizeof(AVPacket)); }
void av_packet_unref(AVPacket* pkt) {
	if (pkt->data) free(pkt->data);
	memset(pkt, 0, sizeof(*pkt));
	pkt->pts = AV_NOPTS_VALUE;
	pkt->dts = AV_NOPTS_VALUE;
}
void av_packet_free(AVPacket** pkt) { if (*pkt) { av_packet_unref(*pkt); free(*pkt); *pkt = nullptr; } }
AVPacket* av_packet_clone(const AVPacket* src) {
	AVPacket* copy = (AVPacket*)calloc(1, sizeof(AVPacket));
	*copy = *src;
	copy->data = nullptr; copy->size = 0;
	if (src->data) { copy->data = (uint8_t*)malloc(src->size); memcpy(copy->data, src->data, src->size); copy->size = src->size; }
	return copy;
}

AVFrame* av_frame_alloc(void) { AVFrame* f = (AVFrame*)calloc(1, sizeof(AVFrame)); f->pts = AV_NOPTS_VALUE; f->best_effort_timestamp = AV_NOPTS_VALUE; return f; }
void av_frame_unref(AVFrame* f) {
	if (f->opaque) { for (int i = 0; i < 3; i++) free(f->data[i]); }
	int fmt = f->format, w = f->width, h = f->height;
	(void)fmt; (void)w; (void)h;
	memset(f->data, 0, sizeof(f->data));
	memset(f->linesize, 0, sizeof(f->linesize));
	f->opaque = nullptr;
	f->pts = AV_NOPTS_VALUE;
	f->best_effort_timestamp = AV_NOPTS_VALUE;
}
void av_frame_free(AVFrame** f) { if (*f) { av_frame_unref(*f); free(*f); *f = nullptr; } }
static void alloc_planes(AVFrame* f, int w, int h) {
	int strides[3] = { (w + 31) / 32 * 32 + 32, (w / 2 + 31) / 32 * 32, (w / 2 + 31) / 32 * 32 };  // Padded on purpose.
	int heights[3] = { h, h / 2, h / 2 };
	for (int i = 0; i < 3; i++) { f->linesize[i] = strides[i]; f->data[i] = (uint8_t*)calloc(strides[i], heights[i]); }
	f->opaque = (void*)1;
}
int av_frame_get_buffer(AVFrame* f, int) { alloc_planes(f, f->width, f->height); return 0; }
void av_image_copy(uint8_t* const dst_data[4], const int dst_linesizes[4], const uint8_t* const src_data[4], const int src_linesizes[4],
	enum AVPixelFormat, int width, int height) {
	int w[3] = { width, width / 2, width / 2 };
	int h[3] = { height, height / 2, height / 2 };
	for (int p = 0; p < 3; p++)
		for (int r = 0; r < h[p]; r++)
			memcpy(dst_data[p] + (size_t)r * dst_linesizes[p], src_data[p] + (size_t)r * src_linesizes[p], w[p]);
}

// ---- swscale (not exercised) ----
struct SwsContext* sws_getCachedContext(struct SwsContext* ctx, int, int, enum AVPixelFormat, int, int, enum AVPixelFormat, int, SwsFilter*, SwsFilter*, const double*) {
	if (!ctx) ctx = (SwsContext*)calloc(1, sizeof(SwsContext));
	return ctx;
}
int sws_scale(struct SwsContext*, const uint8_t* const[], const int[], int, int, uint8_t* const[], const int[]) { return 0; }
void sws_freeContext(struct SwsContext* ctx) { free(ctx); }

// ---- parameters ----
AVCodecParameters* avcodec_parameters_alloc(void) { return (AVCodecParameters*)calloc(1, sizeof(AVCodecParameters)); }
void avcodec_parameters_free(AVCodecParameters** p) { if (*p) { free(*p); *p = nullptr; } }
int avcodec_parameters_copy(AVCodecParameters* dst, const AVCodecParameters* src) { *dst = *src; return 0; }

// ---- demuxer ----
int avformat_open_input(AVFormatContext** ps, const char* url, const AVInputFormat*, AVDictionary**) {
	std::map<std::string, FakeFileSpec>::iterator it = g_files.find(url);
	if (it == g_files.end()) return AVERROR(ENOENT);
	AVFormatContext* fmt = (AVFormatContext*)calloc(1, sizeof(AVFormatContext));
	FakeDemux* demux = new FakeDemux();
	demux->spec = it->second;
	demux->next_video = 0; demux->audio_pending = false; demux->next_audio = 0;
	memset(&demux->entry, 0, sizeof(demux->entry));
	const FakeFileSpec& s = demux->spec;
	int stream_count = s.audio ? 2 : 1;
	fmt->nb_streams = stream_count;
	fmt->streams = (AVStream**)calloc(stream_count, sizeof(AVStream*));
	for (int i = 0; i < stream_count; i++) {
		AVStream* st = (AVStream*)calloc(1, sizeof(AVStream));
		st->index = i;
		st->codecpar = (AVCodecParameters*)calloc(1, sizeof(AVCodecParameters));
		fmt->streams[i] = st;
		g_stream_demux[st] = demux;
	}
	AVStream* v = fmt->streams[0];
	v->time_base.num = 1; v->time_base.den = 600;
	v->start_time = s.start_time;
	v->duration = (int64_t)s.frames * FRAME_TICKS + s.duration_extra;
	v->avg_frame_rate.num = 20; v->avg_frame_rate.den = 1;
	v->r_frame_rate = v->avg_frame_rate;
	v->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
	v->codecpar->codec_id = AV_CODEC_ID_H264;
	v->codecpar->width = s.width; v->codecpar->height = s.height;
	v->codecpar->format = AV_PIX_FMT_YUV420P;
	v->codecpar->bit_rate = 1000000;
	if (s.audio) {
		AVStream* a = fmt->streams[1];
		a->time_base.num = 1; a->time_base.den = 48000;
		a->start_time = s.audio_start;
		a->duration = (int64_t)s.frames * 2400;
		a->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
		a->codecpar->codec_id = AV_CODEC_ID_AAC;
		a->codecpar->sample_rate = 48000;
		a->codecpar->ch_layout.nb_channels = 2;
	}
	fmt->duration = (int64_t)s.frames * FRAME_TICKS * AV_TIME_BASE / 600;
	fmt->bit_rate = 1200000;
	g_demux[fmt] = demux;
	*ps = fmt;
	return 0;
}
int avformat_find_stream_info(AVFormatContext*, AVDictionary**) { return 0; }
int av_find_best_stream(AVFormatContext* fmt, enum AVMediaType type, int, int, const AVCodec**, int) {
	for (unsigned i = 0; i < fmt->nb_streams; i++)
		if (fmt->streams[i]->codecpar->codec_type == type) return (int)i;
	return AVERROR_STREAM_NOT_FOUND;
}
void avformat_close_input(AVFormatContext** ps) {
	AVFormatContext* fmt = *ps;
	FakeDemux* demux = g_demux[fmt];
	for (unsigned i = 0; i < fmt->nb_streams; i++) {
		g_stream_demux.erase(fmt->streams[i]);
		free(fmt->streams[i]->codecpar);
		free(fmt->streams[i]);
	}
	free(fmt->streams);
	g_demux.erase(fmt);
	delete demux;
	free(fmt);
	*ps = nullptr;
}

int av_read_frame(AVFormatContext* fmt, AVPacket* pkt) {
	FakeDemux* d = g_demux[fmt];
	const FakeFileSpec& s = d->spec;
	if (d->audio_pending) {
		d->audio_pending = false;
		pkt->stream_index = 1;
		pkt->pts = pkt->dts = s.audio_start + (int64_t)d->next_audio * 4800;
		pkt->duration = 4800;
		pkt->size = 4; pkt->data = (uint8_t*)calloc(1, 4);
		pkt->flags = AV_PKT_FLAG_KEY;
		d->next_audio++;
		return 0;
	}
	if (d->next_video >= s.frames) return AVERROR_EOF;
	int i = d->next_video++;
	pkt->stream_index = 0;
	pkt->pts = pkt->dts = s.start_time + (int64_t)i * FRAME_TICKS;
	pkt->duration = FRAME_TICKS;
	PacketInfo info;
	info.frame = i; info.tag = s.tag; info.delay = s.delay; info.width = s.width; info.height = s.height; info.pts = pkt->pts;
	pkt->size = sizeof(info); pkt->data = (uint8_t*)calloc(1, sizeof(info));
	memcpy(pkt->data, &info, sizeof(info));
	pkt->flags = (i % s.gop == 0) ? AV_PKT_FLAG_KEY : 0;
	if (s.discard_last && i == s.frames - 1) pkt->flags |= AV_PKT_FLAG_DISCARD;
	if (s.audio && (i % 2) == 1) d->audio_pending = true;
	return 0;
}
int av_seek_frame(AVFormatContext* fmt, int, int64_t ts, int) {
	g_seek_count++;
	FakeDemux* d = g_demux[fmt];
	int64_t local = ts - d->spec.start_time;
	int frame = (int)(local / FRAME_TICKS);
	if (local < 0) frame = 0;
	if (frame >= d->spec.frames) frame = d->spec.frames - 1;
	frame -= frame % d->spec.gop;     // Back to the key frame.
	d->next_video = frame;
	d->next_audio = frame / 2;
	d->audio_pending = false;
	return 0;
}
const AVIndexEntry* avformat_index_get_entry_from_timestamp(AVStream* st, int64_t ts, int) {
	FakeDemux* d = g_stream_demux[st];
	int64_t local = ts - d->spec.start_time;
	int frame = (int)(local / FRAME_TICKS);
	if (local < 0) frame = 0;
	if (frame >= d->spec.frames) frame = d->spec.frames - 1;
	frame -= frame % d->spec.gop;
	d->entry.timestamp = d->spec.start_time + (int64_t)frame * FRAME_TICKS;
	return &d->entry;
}

// ---- decoder ----
const AVCodec* avcodec_find_decoder(enum AVCodecID) { static AVCodec c; return &c; }
AVCodecContext* avcodec_alloc_context3(const AVCodec*) { return (AVCodecContext*)calloc(1, sizeof(AVCodecContext)); }
int avcodec_parameters_to_context(AVCodecContext* ctx, const AVCodecParameters*) { (void)ctx; return 0; }
int avcodec_open2(AVCodecContext* ctx, const AVCodec*, AVDictionary**) {
	FakeDecoder* d = new FakeDecoder();
	d->delay = 0;
	d->draining = false;
	g_decoders[ctx] = d;
	return 0;
}
void avcodec_free_context(AVCodecContext** ctx) {
	if (!*ctx) return;
	std::map<AVCodecContext*, FakeDecoder*>::iterator it = g_decoders.find(*ctx);
	if (it != g_decoders.end()) { delete it->second; g_decoders.erase(it); }
	free(*ctx);
	*ctx = nullptr;
}
void avcodec_flush_buffers(AVCodecContext* ctx) {
	FakeDecoder* d = g_decoders[ctx];
	d->held.clear(); d->draining = false;
}

int avcodec_send_packet(AVCodecContext* ctx, const AVPacket* pkt) {
	FakeDecoder* d = g_decoders[ctx];
	if (pkt == nullptr) { d->draining = true; return 0; }
	if (d->draining) return AVERROR_EOF;
	PacketInfo info;
	memcpy(&info, pkt->data, sizeof(info));
	info.discard = (pkt->flags & AV_PKT_FLAG_DISCARD) ? 1 : 0;
	d->delay = info.delay;
	d->held.push_back(info);
	return 0;
}

int avcodec_receive_frame(AVCodecContext* ctx, AVFrame* frame) {
	FakeDecoder* d = g_decoders[ctx];
	PacketInfo info;
	while (true) {
		bool ready = !d->held.empty() && ((int)d->held.size() > d->delay || d->draining);
		if (!ready)
			return d->draining ? AVERROR_EOF : AVERROR(EAGAIN);
		info = d->held.front();
		d->held.pop_front();
		if (!info.discard)
			break;   // Frames from discard-flagged packets are decoded but never handed out.
	}
	frame->format = AV_PIX_FMT_YUV420P;
	frame->width = info.width;
	frame->height = info.height;
	alloc_planes(frame, info.width, info.height);
	frame->data[0][0] = (uint8_t)(info.frame & 255);
	frame->data[0][1] = (uint8_t)(info.frame >> 8);
	frame->data[1][0] = (uint8_t)info.tag;
	frame->pts = info.pts;
	frame->best_effort_timestamp = info.pts;
	return 0;
}

}  // extern "C"
