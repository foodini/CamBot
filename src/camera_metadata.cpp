#include "camera_metadata.h"

#include <chrono>
#include <math.h>
#include <stdio.h>
#include <thread>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

#include "proto_wire.h"

namespace camera_metadata {

const char* stream_name(int stream) {
	return stream == STREAM_DJMD ? "djmd" : "dbgi";
}

std::string path_text(const std::vector<int>& path) {
	std::string text;
	for (size_t i = 0; i < path.size(); i++) {
		char one[16];
		snprintf(one, sizeof(one), "%s%d", i == 0 ? "" : ".", path[i]);
		text += one;
	}
	return text;
}

struct FieldName {
	const char* path;
	const char* name;
};

// From ExifTool's DJI tag list (its dvtm_ac206_* tags, whose ids are these paths with dashes), except the ones ending in "?", which are
// guesses from the values. Fields of messages that repeat are named once, for all of them.
static const FieldName DJMD_NAMES[] = {
	{ "1.1", "Header" },
	{ "1.1.1", "SchemaFile?" },
	{ "1.1.5", "SerialNumber" },
	{ "1.1.6", "FirmwareVersion" },
	{ "1.1.10", "Model" },
	{ "1.9", "Stabilization" },
	{ "1.9.1", "EIS" },
	{ "1.15", "FilmTone" },
	{ "1.15.1", "FilmToneColorFilter" },
	{ "2.3", "FrameInfo" },
	{ "2.3.1", "FrameWidth" },
	{ "2.3.2", "FrameHeight" },
	{ "2.3.3", "FrameRate" },
	{ "2.5", "Lens" },
	{ "2.5.1", "FOV" },
	{ "3", "Per-frame sample" },
	{ "3.1", "Frame" },
	{ "3.1.1", "FrameNumber" },
	{ "3.1.2", "Timestamp (microseconds)?" },
	{ "3.2", "Camera state" },
	{ "3.2.3.1", "ISO" },
	{ "3.2.4.1", "ShutterSpeed (numerator, denominator)" },
	{ "3.2.5.1", "Zoom" },
	{ "3.2.6.1", "ColorTemperature" },
	{ "3.2.9", "Quaternion (orientation)" },
	{ "3.2.9.1", "Quaternion1" },
	{ "3.2.9.2", "Quaternion2" },
	{ "3.2.9.3", "Quaternion3" },
	{ "3.2.9.4", "Quaternion4" },
	{ "3.2.10", "Accelerometer" },
	{ "3.2.10.2", "AccelerometerX (positive = acceleration forward)" },
	{ "3.2.10.3", "AccelerometerY (positive = acceleration to the right)" },
	{ "3.2.10.4", "AccelerometerZ (positive = acceleration down; resting reads -1)" },
	{ "3.2.10.0", "AccelerometerMagnitude (g; worked out from the three axes)" },
	{ "3.2.11.1", "TextureSetting" },
	{ "3.2.12.1", "NoiseReduction" },
	{ "3.2.13", "Barometer" },
	{ "3.2.13.2", "BarometricAltitude (decimetres above the 1013 hPa level)" },
	{ "3.2.13.3", "AirPressure (hPa, whole numbers)" },
	{ "3.3", "High-rate samples?" },
	{ "3.3.2", "High-rate orientation?" },
	{ "3.3.2.1", "Timestamp (microseconds)?" },
	{ "3.3.2.3", "Quaternion (about 1 kHz)?" },
	{ "3.3.2.3.1", "Quaternion1" },
	{ "3.3.2.3.2", "Quaternion2" },
	{ "3.3.2.3.3", "Quaternion3" },
	{ "3.3.2.3.4", "Quaternion4" },
	{ "3.3.2.4", "FrameBrightness?" },
	{ "3.4", "Source info?" },
	{ "3.4.1.4", "Product?" },
	{ "3.4.2", "GPS" },
	{ "3.4.2.1", "GPSInfo (units, latitude, longitude)" },
	{ "3.4.2.2", "GPSAltitude" },
	{ "3.4.2.6", "GPS time" },
	{ "3.4.2.6.1", "GPSDateTime" },
};

static const FieldName DBGI_NAMES[] = {
	{ "1.1.2.1", "SchemaFile?" },
	{ "1.2.1.1", "ImageSensor?" },
	{ "2.1.1.2", "Timestamp (microseconds)?" },
	{ "2.1.1.3", "FrameNumber?" },
	{ "2.1.20.10", "ISO?" },
};

static std::string lookup(const FieldName* table, size_t count, const std::string& path) {
	for (size_t i = 0; i < count; i++) {
		if (path == table[i].path)
			return table[i].name;
	}
	return std::string();
}

std::string field_name(int stream, const std::vector<int>& path) {
	const std::string text = path_text(path);
	if (stream == STREAM_DJMD)
		return lookup(DJMD_NAMES, sizeof(DJMD_NAMES) / sizeof(DJMD_NAMES[0]), text);
	return lookup(DBGI_NAMES, sizeof(DBGI_NAMES) / sizeof(DBGI_NAMES[0]), text);
}

struct ValueMeaning {
	const char* path;
	int         value;
	const char* meaning;
};

static const ValueMeaning DJMD_VALUES[] = {
	{ "1.9.1", 1, "Rock Steady" },
	{ "1.9.1", 2, "Horizon Steady" },
	{ "1.9.1", 4, "Rock Steady Plus" },
	{ "1.9.1", 5, "Horizon Balancing" },
	{ "1.15.1", 1, "WT" },
	{ "1.15.1", 2, "FE" },
	{ "1.15.1", 3, "TR" },
	{ "1.15.1", 4, "NV" },
	{ "1.15.1", 5, "NC" },
	{ "1.15.1", 6, "CC" },
	{ "2.5.1", 1, "Wide" },
	{ "2.5.1", 2, "Standard Dewarp" },
	{ "2.5.1", 3, "Ultra Wide" },
	{ "2.5.1", 6, "Natural Wide" },
	{ "2.5.1", 7, "Flat" },
};

std::string value_meaning(int stream, const std::vector<int>& path, int64_t value) {
	if (stream != STREAM_DJMD)
		return std::string();
	const std::string text = path_text(path);
	for (size_t i = 0; i < sizeof(DJMD_VALUES) / sizeof(DJMD_VALUES[0]); i++) {
		if (text == DJMD_VALUES[i].path && value == DJMD_VALUES[i].value)
			return DJMD_VALUES[i].meaning;
	}
	return std::string();
}

}  // namespace camera_metadata

// The "codec tag" the camera gives each metadata stream: its name, as four letters.
static unsigned int tag_for(int stream) {
	return stream == camera_metadata::STREAM_DJMD ? MKTAG('d', 'j', 'm', 'd') : MKTAG('d', 'b', 'g', 'i');
}

static int find_stream(AVFormatContext* fmt, int stream) {
	for (unsigned int i = 0; i < fmt->nb_streams; i++) {
		if (fmt->streams[i]->codecpar->codec_tag == tag_for(stream))
			return (int)i;
	}
	return -1;
}

// Opens `path` far enough to read the streams' packets (the streams are described in the file's index, so nothing has to be decoded to
// find them), and has the demuxer pass over every stream but the wanted ones.
static AVFormatContext* open_streams(const std::string& path, int index[2]) {
	AVFormatContext* fmt = nullptr;
	if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0)
		return nullptr;
	index[0] = find_stream(fmt, camera_metadata::STREAM_DJMD);
	index[1] = find_stream(fmt, camera_metadata::STREAM_DBGI);
	if (index[0] < 0 && index[1] < 0) {
		avformat_close_input(&fmt);
		return nullptr;
	}
	for (unsigned int i = 0; i < fmt->nb_streams; i++) {
		if ((int)i != index[0] && (int)i != index[1])
			fmt->streams[i]->discard = AVDISCARD_ALL;
	}
	return fmt;
}

static int64_t first_pts(const AVStream* stream) {
	return stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
}

// ---------------------------------------------------------------------------------------------
// CameraMetadata
// ---------------------------------------------------------------------------------------------

CameraMetadata::CameraMetadata() : m_fmt(nullptr) {
	m_stream_index[0] = -1;
	m_stream_index[1] = -1;
}

CameraMetadata::~CameraMetadata() {
	close();
}

void CameraMetadata::close() {
	if (m_fmt != nullptr)
		avformat_close_input(&m_fmt);
	m_fmt = nullptr;
	m_stream_index[0] = -1;
	m_stream_index[1] = -1;
	m_sample[0] = Sample();
	m_sample[1] = Sample();
	m_path.clear();
}

bool CameraMetadata::open(const std::string& path) {
	close();
	m_fmt = open_streams(path, m_stream_index);
	if (m_fmt == nullptr) {
		m_stream_index[0] = -1;
		m_stream_index[1] = -1;
		return false;
	}
	m_path = path;
	return true;
}

int64_t CameraMetadata::target_for(int stream, double seconds) const {
	const AVStream* st = m_fmt->streams[m_stream_index[stream]];
	if (seconds < 0.0)
		seconds = 0.0;
	return first_pts(st) + (int64_t)floor(seconds / av_q2d(st->time_base) + 1e-6);
}

bool CameraMetadata::covers(const Sample& sample, int64_t target) const {
	if (!sample.valid)
		return false;
	const int64_t length = sample.duration > 0 ? sample.duration : 1;
	return target >= sample.pts && target < sample.pts + length;
}

// Seeks to `seconds` and takes the sample of each stream there: after a seek to a time, the first packet each stream gives is the last one
// that is not after it, and one seek serves both.
void CameraMetadata::refresh(double seconds) {
	const int primary = m_stream_index[camera_metadata::STREAM_DJMD] >= 0 ? camera_metadata::STREAM_DJMD : camera_metadata::STREAM_DBGI;
	const int64_t primary_target = target_for(primary, seconds);
	if (av_seek_frame(m_fmt, m_stream_index[primary], primary_target, AVSEEK_FLAG_BACKWARD) < 0)
		return;
	bool got[2] = { false, false };
	bool wanted[2] = { m_stream_index[0] >= 0, m_stream_index[1] >= 0 };
	AVPacket* packet = av_packet_alloc();
	if (packet == nullptr)
		return;
	for (int reads = 0; reads < 512 && ((wanted[0] && !got[0]) || (wanted[1] && !got[1])); reads++) {
		if (av_read_frame(m_fmt, packet) < 0)
			break;
		for (int stream = 0; stream < 2; stream++) {
			if (wanted[stream] && !got[stream] && packet->stream_index == m_stream_index[stream]) {
				Sample& sample = m_sample[stream];
				sample.bytes.assign((const char*)packet->data, (size_t)packet->size);
				sample.pts = packet->pts != AV_NOPTS_VALUE ? packet->pts : packet->dts;
				sample.duration = packet->duration;
				sample.valid = true;
				got[stream] = true;
			}
		}
		av_packet_unref(packet);
	}
	av_packet_free(&packet);
	for (int stream = 0; stream < 2; stream++) {
		if (wanted[stream] && !got[stream])
			m_sample[stream].valid = false;
	}
}

bool CameraMetadata::sample_at(int stream, double seconds, const std::string** bytes, double* begins) {
	if (m_fmt == nullptr || stream < 0 || stream >= 2 || m_stream_index[stream] < 0)
		return false;
	if (!covers(m_sample[stream], target_for(stream, seconds)))
		refresh(seconds);
	const Sample& sample = m_sample[stream];
	if (!sample.valid)
		return false;
	const AVStream* st = m_fmt->streams[m_stream_index[stream]];
	*bytes = &sample.bytes;
	*begins = (double)(sample.pts - first_pts(st)) * av_q2d(st->time_base);
	return true;
}

// ---------------------------------------------------------------------------------------------
// CameraPackets
// ---------------------------------------------------------------------------------------------

CameraPackets::CameraPackets() : m_running(false), m_cancel(false), m_progress_permille(0), m_duration(0.0), m_started(false), m_second_run(0) {
}

CameraPackets::~CameraPackets() {
	stop();
}

void CameraPackets::stop() {
	m_cancel.store(true);
	if (m_thread.joinable())
		m_thread.join();
	m_cancel.store(false);
	m_running.store(false);
	m_started = false;
	m_duration.store(0.0);
	m_progress_permille.store(0);
	std::lock_guard<std::mutex> lock(m_mutex);
	for (size_t i = 0; i < m_packets.size(); i++)
		delete m_packets[i];
	m_packets.clear();
	m_second_run = 0;
	m_error.clear();
	m_note.clear();
}

void CameraPackets::start(const std::string& file, int stream, double start_seconds) {
	stop();
	m_file = file;
	m_started = true;
	m_running.store(true);
	m_thread = std::thread(&CameraPackets::run, this, file, stream, start_seconds);
}

std::string CameraPackets::error() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_error;
}

std::string CameraPackets::note() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_note;
}

size_t CameraPackets::count() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_packets.size();
}

const CameraPacket* CameraPackets::at(size_t index) const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return index < m_packets.size() ? m_packets[index] : nullptr;
}

// The arrival order is two runs, each forward in time, the second being the part before the first run began; in the order of time the
// second comes first.
size_t CameraPackets::index_at_position(size_t position) const {
	std::lock_guard<std::mutex> lock(m_mutex);
	const size_t second_length = m_packets.size() - m_second_run;
	return position < second_length ? m_second_run + position : position - second_length;
}

size_t CameraPackets::position_at(double seconds) const {
	const size_t total = count();
	if (total == 0)
		return 0;
	size_t lower = 0;
	size_t upper = total;
	while (lower < upper) {
		const size_t middle = lower + (upper - lower) / 2;
		if (at(index_at_position(middle))->time < seconds)
			lower = middle + 1;
		else
			upper = middle;
	}
	return lower < total ? lower : total - 1;
}

void CameraPackets::run(std::string file, int stream, double start_seconds) {
	int index[2] = { -1, -1 };
	AVFormatContext* fmt = open_streams(file, index);
	if (fmt == nullptr || index[stream] < 0) {
		if (fmt != nullptr)
			avformat_close_input(&fmt);
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_error = "This file has no " + std::string(camera_metadata::stream_name(stream)) + " stream.";
		}
		m_running.store(false);
		return;
	}
	// Only the one stream is wanted here.
	for (unsigned int i = 0; i < fmt->nb_streams; i++) {
		if ((int)i != index[stream])
			fmt->streams[i]->discard = AVDISCARD_ALL;
	}
	const AVStream* st = fmt->streams[index[stream]];
	const double seconds_per_tick = av_q2d(st->time_base);
	const int64_t start = first_pts(st);
	const double total_seconds = st->duration != AV_NOPTS_VALUE ? (double)st->duration * seconds_per_tick : (fmt->duration > 0 ? (double)fmt->duration / AV_TIME_BASE : 0.0);
	m_duration.store(total_seconds);

	if (start_seconds < 0.0)
		start_seconds = 0.0;
	// Two passes: from where the caller is to the end, then from the beginning up to where the first pass began.
	int64_t first_pass_began = AV_NOPTS_VALUE;
	double  covered_first_pass = 0.0;
	size_t  bytes_held = 0;
	bool    too_big = false;
	bool    second_began = false;
	AVPacket* packet = av_packet_alloc();
	for (int pass = 0; pass < 2 && packet != nullptr && !m_cancel.load() && !too_big; pass++) {
		const int64_t from = pass == 0 ? start + (int64_t)floor(start_seconds / seconds_per_tick + 1e-6) : start;
		if (av_seek_frame(fmt, index[stream], from, AVSEEK_FLAG_BACKWARD) < 0) {
			if (pass == 0)
				av_seek_frame(fmt, index[stream], start, AVSEEK_FLAG_BACKWARD);
			else
				break;
		}
		if (pass == 1) {
			std::lock_guard<std::mutex> lock(m_mutex);
			m_second_run = m_packets.size();
			second_began = true;
		}
		while (!m_cancel.load() && av_read_frame(fmt, packet) >= 0) {
			if (packet->stream_index != index[stream]) {
				av_packet_unref(packet);
				continue;
			}
			const int64_t pts = packet->pts != AV_NOPTS_VALUE ? packet->pts : packet->dts;
			if (pass == 0 && first_pass_began == AV_NOPTS_VALUE)
				first_pass_began = pts;
			if (pass == 1 && first_pass_began != AV_NOPTS_VALUE && pts >= first_pass_began) {
				av_packet_unref(packet);
				break;   // Back where the first pass began.
			}
			if (bytes_held + (size_t)packet->size > MAX_BYTES) {
				too_big = true;
				av_packet_unref(packet);
				break;
			}
			const double time = (double)(pts - start) * seconds_per_tick;
			CameraPacket* kept = new CameraPacket();
			kept->time = time;
			kept->length = (double)packet->duration * seconds_per_tick;
			kept->bytes.assign((const char*)packet->data, (size_t)packet->size);
			bytes_held += (size_t)packet->size;
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_packets.push_back(kept);
			}
			if (total_seconds > 0.0) {
				const double covered = pass == 0 ? time - (double)(first_pass_began - start) * seconds_per_tick : covered_first_pass + time;
				int permille = (int)(1000.0 * covered / total_seconds);
				m_progress_permille.store(permille < 0 ? 0 : (permille > 999 ? 999 : permille));
				if (pass == 0)
					covered_first_pass = covered;
			}
			av_packet_unref(packet);
		}
	}
	av_packet_free(&packet);
	avformat_close_input(&fmt);

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		// With no second pass, the second run is empty: it begins at the end.
		if (!second_began)
			m_second_run = m_packets.size();
		if (too_big) {
			char text[160];
			snprintf(text, sizeof(text), "The %s stream is too big to hold (over %d MB): what is shown is only the part of it that fitted.",
				camera_metadata::stream_name(stream), (int)(MAX_BYTES / (1024 * 1024)));
			m_note = text;
		}
	}
	if (!m_cancel.load())
		m_progress_permille.store(1000);
	m_running.store(false);
}

// ---------------------------------------------------------------------------------------------
// CameraSeries
// ---------------------------------------------------------------------------------------------

CameraSeries::CameraSeries() : m_packets(nullptr), m_running(false), m_cancel(false), m_progress_permille(0), m_found_any(false) {
}

CameraSeries::~CameraSeries() {
	stop();
}

void CameraSeries::stop() {
	m_cancel.store(true);
	if (m_thread.joinable())
		m_thread.join();
	m_cancel.store(false);
	m_running.store(false);
	m_found_any.store(false);
	m_progress_permille.store(0);
	m_error.clear();
	std::lock_guard<std::mutex> lock(m_mutex);
	m_chunk_times.clear();
	m_chunk_values.clear();
}

void CameraSeries::start(const CameraPackets* packets, const std::vector<int>& path, double start_seconds) {
	stop();
	m_packets = packets;
	m_running.store(true);
	m_thread = std::thread(&CameraSeries::run, this, path, start_seconds);
}

double CameraSeries::duration() const {
	return m_packets != nullptr ? m_packets->duration() : 0.0;
}

std::string CameraSeries::error() const {
	if (!m_error.empty())
		return m_error;
	return m_packets != nullptr ? m_packets->error() : std::string();
}

std::string CameraSeries::note() const {
	return m_packets != nullptr ? m_packets->note() : std::string();
}

size_t CameraSeries::take_new(std::vector<double>* times, std::vector<float>* values) {
	std::lock_guard<std::mutex> lock(m_mutex);
	const size_t count = m_chunk_values.size();
	times->insert(times->end(), m_chunk_times.begin(), m_chunk_times.end());
	values->insert(values->end(), m_chunk_values.begin(), m_chunk_values.end());
	m_chunk_times.clear();
	m_chunk_values.clear();
	return count;
}

// Hands on a sample's values, spread over `step` seconds from the sample's time.
void CameraSeries::emit(double time, double step, const std::vector<double>& values) {
	const size_t count = values.size();
	std::lock_guard<std::mutex> lock(m_mutex);
	for (size_t j = 0; j < count; j++) {
		m_chunk_times.push_back(time + step * (double)j / (double)count);
		m_chunk_values.push_back((float)values[j]);
	}
	m_found_any.store(true);
}

void CameraSeries::run(std::vector<int> path, double start_seconds) {
	const bool magnitude = !path.empty() && path.back() == camera_metadata::FIELD_ACCEL_MAGNITUDE;
	// With the packets all in, go in the order of time from the starting moment, round to the start of the file; if they are still
	// coming, go in the order they come.
	const bool by_time = !m_packets->running();
	const size_t first_position = by_time ? m_packets->position_at(start_seconds) : 0;
	size_t done = 0;
	double last_step = 0.0;
	std::vector<double> sample_values;
	while (!m_cancel.load()) {
		const bool finished = !m_packets->running();   // Looked at first: what is in after this is all there is.
		const size_t total = m_packets->count();
		if (done >= total) {
			if (finished)
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(3));
			continue;
		}
		// The sample's length is the time to the next one; for that, the next one has to be in (unless this is the last).
		if (done + 1 >= total && !finished) {
			std::this_thread::sleep_for(std::chrono::milliseconds(3));
			continue;
		}
		const size_t here = by_time ? m_packets->index_at_position((first_position + done) % total) : done;
		const size_t after = by_time ? m_packets->index_at_position((first_position + done + 1) % total) : done + 1;
		const CameraPacket* current = m_packets->at(here);
		const CameraPacket* next = done + 1 < total ? m_packets->at(after) : nullptr;
		sample_values.clear();
		{
			const uint8_t* bytes = (const uint8_t*)current->bytes.data();
			if (magnitude) {
				std::vector<int> axis_path = path;
				double axis[3] = { 0.0, 0.0, 0.0 };
				bool all = true;
				for (int k = 0; k < 3; k++) {
					axis_path.back() = 2 + k;
					std::vector<double> one;
					proto_wire::collect_from_bytes(bytes, current->bytes.size(), axis_path, &one);
					if (one.empty())
						all = false;
					else
						axis[k] = one[0];
				}
				if (all)
					sample_values.push_back(sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]));
			} else {
				proto_wire::collect_from_bytes(bytes, current->bytes.size(), path, &sample_values);
			}
		}
		if (!sample_values.empty()) {
			double step = next != nullptr ? next->time - current->time : 0.0;
			if (step <= 0.0)
				step = current->length > 0.0 ? current->length : last_step;
			else
				last_step = step;
			emit(current->time, step, sample_values);
		}
		done++;
		const size_t known = m_packets->count();
		m_progress_permille.store(finished && known > 0 ? (int)(1000 * done / known) : (int)(m_packets->progress() * 999.0f));
	}
	if (!m_cancel.load()) {
		if (!m_found_any.load() && m_packets->error().empty())
			m_error = "No values for that field were found in the file.";
		m_progress_permille.store(1000);
	}
	m_running.store(false);
}
