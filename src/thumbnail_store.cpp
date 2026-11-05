#include "thumbnail_store.h"

#include <stdio.h>

#include <glad/glad.h>

#include "media_decoder.h"

namespace {

const int PICTURE_HEIGHT = 144;
const size_t MAX_PICTURES = 300;       // About 40 MB of video memory at 256 x 144 x 4 bytes each.
const int    RESULTS_PER_FRAME = 8;    // Turned into textures in one frame.
const size_t MAX_FAILED = 2000;

}  // namespace

ThumbnailStore::ThumbnailStore()
	: m_picture_width(256), m_picture_height(PICTURE_HEIGHT), m_aspect(16.0f / 9.0f), m_fallback_step(30000), m_frame(0), m_opened(false),
	  m_catalog(), m_mutex(), m_wake(), m_queue(), m_results(), m_keyframes(), m_keyframes_ready(), m_failed(), m_busy(false), m_busy_key(),
	  m_paused(false), m_stop(false) {}

ThumbnailStore::~ThumbnailStore() {
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_stop = true;
	}
	m_wake.notify_all();
	if (m_thread.joinable())
		m_thread.join();
	// (The textures are not deleted here: this can be destroyed after the OpenGL context is, and they go with it.)
}

void ThumbnailStore::open(const std::vector<std::string>& paths, int width, int height, double ticks_per_second) {
	if (m_opened || paths.empty() || width <= 0 || height <= 0)
		return;
	m_opened = true;
	m_aspect = (float)width / (float)height;
	m_picture_height = PICTURE_HEIGHT;
	m_picture_width = (int)((double)PICTURE_HEIGHT * (double)width / (double)height + 0.5);
	m_fallback_step = (int64_t)(ticks_per_second > 1.0 ? ticks_per_second : 1.0);
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_keyframes.assign(paths.size(), std::vector<int64_t>());
		m_keyframes_ready.assign(paths.size(), 0);
	}
	m_thread = std::thread(&ThumbnailStore::worker_main, this, paths);
}

void ThumbnailStore::set_paused(bool paused) {
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_paused == paused)
			return;
		m_paused = paused;
	}
	m_wake.notify_all();
}

bool ThumbnailStore::is_queued_or_known(const thumbnails::ThumbKey& key) {
	if (m_busy && m_busy_key == key)
		return true;
	for (size_t i = 0; i < m_queue.size(); i++) {
		if (m_queue[i].key == key) {
			m_queue[i].wanted = true;
			return true;
		}
	}
	for (size_t i = 0; i < m_results.size(); i++) {
		if (m_results[i].key == key)
			return true;   // Done, and waiting to be taken at the start of the next frame.
	}
	for (size_t i = 0; i < m_failed.size(); i++) {
		if (m_failed[i] == key)
			return true;
	}
	return false;
}

void ThumbnailStore::begin_frame() {
	m_frame++;
	std::vector<Result> arrived;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (size_t i = 0; i < m_queue.size(); i++)
			m_queue[i].wanted = false;
		// A few at a time, so a burst of pictures doesn't hold up one frame.
		const size_t take = m_results.size() < (size_t)RESULTS_PER_FRAME ? m_results.size() : (size_t)RESULTS_PER_FRAME;
		arrived.assign(m_results.begin(), m_results.begin() + take);
		m_results.erase(m_results.begin(), m_results.begin() + take);
	}
	for (size_t i = 0; i < arrived.size(); i++) {
		const Result& result = arrived[i];
		if (!result.ok) {
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_failed.size() >= MAX_FAILED)
				m_failed.clear();
			m_failed.push_back(result.key);
			continue;
		}
		if (m_catalog.contains(result.key))
			continue;
		unsigned int texture = 0;
		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, result.width, result.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, result.pixels.data());
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture(GL_TEXTURE_2D, 0);
		m_catalog.add(result.key, texture, m_frame);
	}
}

unsigned int ThumbnailStore::want(int media, int64_t local_ticks, bool exact) {
	if (!m_opened)
		return 0;
	thumbnails::ThumbKey key;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (media < 0 || media >= (int)m_keyframes.size() || !m_keyframes_ready[media])
			return 0;   // Not known yet which keyframe it is, so nothing can be said.
		const int64_t ticks = exact ? local_ticks : thumbnails::keyframe_for(m_keyframes[media], local_ticks, m_fallback_step);
		key = thumbnails::ThumbKey(media, ticks, exact);
	}
	unsigned int texture = 0;
	if (m_catalog.find(key, m_frame, &texture))
		return texture;
	bool queue_it = false;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!is_queued_or_known(key)) {
			Queued queued;
			queued.key = key;
			queued.wanted = true;
			m_queue.push_back(queued);
			queue_it = true;
		}
	}
	if (queue_it)
		m_wake.notify_one();
	// Another picture of the same file stands in meanwhile.
	if (m_catalog.nearest(media, key.ticks, m_frame, &texture))
		return texture;
	return 0;
}

void ThumbnailStore::end_frame() {
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		size_t kept = 0;
		for (size_t i = 0; i < m_queue.size(); i++) {
			if (m_queue[i].wanted) {
				if (kept != i)
					m_queue[kept] = m_queue[i];
				kept++;
			}
		}
		m_queue.resize(kept);
	}
	if (m_catalog.count() > MAX_PICTURES) {
		const std::vector<unsigned int> removed = m_catalog.evict(MAX_PICTURES - MAX_PICTURES / 4, m_frame);
		for (size_t i = 0; i < removed.size(); i++) {
			unsigned int texture = removed[i];
			glDeleteTextures(1, &texture);
		}
	}
}

void ThumbnailStore::worker_main(std::vector<std::string> paths) {
	MediaDecoder decoder;
	decoder.set_quiet(true);   // The program's own decoder has already told the console about these files.
	std::string error;
	if (!decoder.open(paths, &error)) {
		printf("Timeline pictures: %s\n", error.c_str());
		return;
	}
	// Where each file's keyframes are, so which picture a moment comes to is known without decoding anything.
	for (int media = 0; media < decoder.media_count(); media++) {
		std::vector<int64_t> times;
		decoder.keyframe_times(media, &times);
		std::lock_guard<std::mutex> lock(m_mutex);
		m_keyframes[media] = times;
		m_keyframes_ready[media] = 1;
	}

	const int source_width = decoder.width();
	const int source_height = decoder.height();
	SwsContext* scaler = sws_getContext(source_width, source_height, AV_PIX_FMT_YUV420P, m_picture_width, m_picture_height,
		AV_PIX_FMT_RGBA, SWS_AREA, nullptr, nullptr, nullptr);
	if (scaler == nullptr) {
		printf("Timeline pictures: could not set up the picture scaler.\n");
		return;
	}
	const int64_t half_frame = decoder.frame_ticks() / 2;

	while (true) {
		thumbnails::ThumbKey key;
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			while (!m_stop && (m_paused || m_queue.empty()))
				m_wake.wait(lock);
			if (m_stop)
				break;
			key = m_queue.front().key;
			m_queue.erase(m_queue.begin());
			m_busy = true;
			m_busy_key = key;
		}

		Result result;
		result.key = key;
		// Every file is whole and in the order given, so the file's place on the decoder's timeline is its clip's.
		const int64_t at = decoder.edit_list().start(key.media) + key.ticks;
		// (The keyframe's own time can come back a whisker low from the conversions, which would land on the one before: a little on is safe.)
		const bool moved = key.exact ? decoder.advance_to(at) : decoder.seek_keyframe(at + half_frame);
		const AVFrame* frame = decoder.frame();
		if (moved && frame != nullptr) {
			result.width = m_picture_width;
			result.height = m_picture_height;
			result.pixels.assign((size_t)m_picture_width * (size_t)m_picture_height * 4, 0);
			unsigned char* destination[4] = { result.pixels.data(), nullptr, nullptr, nullptr };
			const int destination_stride[4] = { m_picture_width * 4, 0, 0, 0 };
			sws_scale(scaler, frame->data, frame->linesize, 0, source_height, destination, destination_stride);
			result.ok = true;
		}
		if (key.exact)
			decoder.release_cache();   // The frames decoded on the way to that one aren't needed again.

		std::lock_guard<std::mutex> lock(m_mutex);
		m_results.push_back(result);
		m_busy = false;
	}
	sws_freeContext(scaler);
}
