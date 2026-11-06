#include "audio_stretch.h"

#include <algorithm>
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace audio_stretch {

namespace {

// How far ahead of what is buffered a request can be before the file is jumped to instead of read through.
const int64_t SKIP_LIMIT = 16384;
// How much sound is kept behind the place last asked for (the stretcher looks back a little), and how much may pile up before
// the oldest is let go.
const int64_t KEEP_BEHIND = 8192;
const int64_t TRIM_AT = 65536;

const double PI = 3.14159265358979323846;

// How far, each way, from where a grain would nominally be cut the best place is looked for: a little more than the longest
// period of a voice, so there is always a whole cycle to line up with.
const int SEARCH = 512;
// The correlation is first found using every this-many-th sample, then refined around the best at full resolution.
const int SEARCH_STRIDE = 4;
const int REFINE = 3;

}  // namespace

// ---------------------------------------------------------------------------------------------
// SourceWindow
// ---------------------------------------------------------------------------------------------

SourceWindow::SourceWindow() : m_reader(nullptr), m_first(0), m_frames(0) {
}

void SourceWindow::set_reader(AudioFileReader* reader) {
	if (reader != m_reader) {
		m_reader = reader;
		m_buffer.clear();
		m_first = 0;
		m_frames = 0;
	}
}

void SourceWindow::get(int64_t start, int count, float* out) {
	if (count <= 0)
		return;
	int lead = 0;
	if (start < 0) {
		lead = (int)((int64_t)count < -start ? (int64_t)count : -start);
		memset(out, 0, sizeof(float) * (size_t)lead * 2);
	}
	if (lead == count)
		return;
	const int64_t from = start + lead;
	const int need = count - lead;
	float* destination = out + (size_t)lead * 2;
	if (m_reader == nullptr) {
		memset(destination, 0, sizeof(float) * (size_t)need * 2);
		return;
	}

	if (m_frames == 0 || from < m_first || from > m_first + m_frames + SKIP_LIMIT) {
		m_buffer.clear();
		m_first = from;
		m_frames = 0;
	}
	const int64_t end = from + need;
	while (m_first + m_frames < end) {
		const int64_t at = m_first + m_frames;
		const int want = (int)(end - at);
		const size_t old_size = m_buffer.size();
		m_buffer.resize(old_size + (size_t)want * 2);
		if (m_reader->position() != at)
			m_reader->seek(at);
		const int got = m_reader->read(&m_buffer[old_size], want);
		if (got < want)
			memset(&m_buffer[old_size + (size_t)got * 2], 0, sizeof(float) * (size_t)(want - got) * 2);
		m_frames += want;
	}
	memcpy(destination, &m_buffer[(size_t)(from - m_first) * 2], sizeof(float) * (size_t)need * 2);

	if (from - m_first > TRIM_AT) {
		const int64_t drop = from - KEEP_BEHIND - m_first;
		m_buffer.erase(m_buffer.begin(), m_buffer.begin() + (size_t)drop * 2);
		m_first += drop;
		m_frames -= drop;
	}
}

// ---------------------------------------------------------------------------------------------
// resample
// ---------------------------------------------------------------------------------------------

void resample(SourceWindow* source, int64_t source_start, double speed, int64_t first_output, int frames, float* out) {
	const int BLOCK = 1024;
	std::vector<float> input;
	int done = 0;
	while (done < frames) {
		const int count = (frames - done) < BLOCK ? (frames - done) : BLOCK;
		const double x_first = (double)source_start + (double)(first_output + done) * speed;
		const double x_last = (double)source_start + (double)(first_output + done + count - 1) * speed;
		const double half = speed > 1.0 ? speed / 2.0 : 0.0;
		const int64_t low = (int64_t)floor(x_first - half) - 2;
		const int64_t high = (int64_t)floor(x_last + half) + 3;
		const int length = (int)(high - low + 1);
		input.resize((size_t)length * 2);
		source->get(low, length, input.data());

		for (int n = 0; n < count; n++) {
			const double x = (double)source_start + (double)(first_output + done + n) * speed;
			float* result = out + (size_t)(done + n) * 2;
			if (speed <= 1.0) {
				// Smooth (Catmull-Rom) interpolation between the samples either side.
				const int64_t whole = (int64_t)floor(x);
				const double t = x - (double)whole;
				const float* base = &input[(size_t)(whole - 1 - low) * 2];
				for (int channel = 0; channel < 2; channel++) {
					const double p0 = base[channel];
					const double p1 = base[2 + channel];
					const double p2 = base[4 + channel];
					const double p3 = base[6 + channel];
					result[channel] = (float)(p1 + 0.5 * t * (p2 - p0 + t * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3 + t * (3.0 * (p1 - p2) + p3 - p0))));
				}
			} else {
				// The average of the stretch of the original this sample covers: a box filter, so what is above the new, lower,
				// sample rate's reach is mostly taken out before it can fold down.
				const int taps = (int)ceil(speed);
				const double step = speed / (double)taps;
				double total[2] = { 0.0, 0.0 };
				for (int tap = 0; tap < taps; tap++) {
					const double position = x - half + ((double)tap + 0.5) * step;
					const int64_t whole = (int64_t)floor(position);
					const double t = position - (double)whole;
					const float* base = &input[(size_t)(whole - low) * 2];
					for (int channel = 0; channel < 2; channel++)
						total[channel] += (double)base[channel] * (1.0 - t) + (double)base[2 + channel] * t;
				}
				result[0] = (float)(total[0] / (double)taps);
				result[1] = (float)(total[1] / (double)taps);
			}
		}
		done += count;
	}
}

// ---------------------------------------------------------------------------------------------
// PitchKeeper
// ---------------------------------------------------------------------------------------------

PitchKeeper::PitchKeeper()
	: m_source(nullptr), m_source_start(0), m_speed(1.0), m_next_output(0), m_next_block(0), m_ready_pos(HOP), m_previous_start(0),
	  m_have_previous(false) {
	m_window.resize(GRAIN);
	for (int n = 0; n < GRAIN; n++) {
		const double s = sin(PI * (double)n / (double)GRAIN);
		m_window[n] = (float)(s * s);       // Two of these, half a grain apart, add up to 1.
	}
	m_accumulator.assign((size_t)GRAIN * 2, 0.0f);
	m_ready.assign((size_t)HOP * 2, 0.0f);
}

int64_t PitchKeeper::nominal_start(int64_t block) const {
	return m_source_start + (int64_t)llround((double)(block * HOP) * m_speed);
}

void PitchKeeper::start(SourceWindow* source, int64_t source_start, double speed, int64_t first_output) {
	m_source = source;
	m_source_start = source_start;
	m_speed = speed;
	if (first_output < 0)
		first_output = 0;
	m_next_output = first_output;
	std::fill(m_accumulator.begin(), m_accumulator.end(), 0.0f);
	m_have_previous = false;

	const int64_t block = first_output / HOP;
	m_next_block = block;
	if (block > 0) {
		// The first part of this block is the second half of the grain before it, so make that one (cut exactly where it
		// nominally goes, as there is nothing to line it up with), and let go of its first half.
		m_previous_start = nominal_start(block - 1);
		add_grain(m_previous_start, false);
		shift_accumulator();
		m_have_previous = true;
	}
	make_block();
	m_ready_pos = (int)(first_output - block * HOP);
}

void PitchKeeper::shift_accumulator() {
	memmove(&m_accumulator[0], &m_accumulator[(size_t)HOP * 2], sizeof(float) * (size_t)HOP * 2);
	memset(&m_accumulator[(size_t)HOP * 2], 0, sizeof(float) * (size_t)HOP * 2);
}

void PitchKeeper::add_grain(int64_t grain_start, bool first_of_segment) {
	m_grain.resize((size_t)GRAIN * 2);
	m_source->get(grain_start, GRAIN, m_grain.data());
	for (int n = 0; n < GRAIN; n++) {
		// The very first grain of a stretch has nothing before it to fade out, so it starts at full strength.
		const float w = (first_of_segment && n < HOP) ? 1.0f : m_window[n];
		m_accumulator[(size_t)n * 2] += m_grain[(size_t)n * 2] * w;
		m_accumulator[(size_t)n * 2 + 1] += m_grain[(size_t)n * 2 + 1] * w;
	}
}

int64_t PitchKeeper::choose_start(int64_t block) {
	const int64_t nominal = nominal_start(block);
	if (!m_have_previous)
		return nominal;

	// What would sound right next: the sound that followed the part of the last grain already used.
	const int length = HOP;
	const int span = 2 * SEARCH + length;
	m_search.resize((size_t)span * 2);
	m_reference.resize((size_t)length * 2);
	m_source->get(nominal - SEARCH, span, m_search.data());
	m_source->get(m_previous_start + HOP, length, m_reference.data());
	m_mono_search.resize((size_t)span);
	m_mono_reference.resize((size_t)length);
	for (int i = 0; i < span; i++)
		m_mono_search[i] = m_search[(size_t)i * 2] + m_search[(size_t)i * 2 + 1];
	for (int i = 0; i < length; i++)
		m_mono_reference[i] = m_reference[(size_t)i * 2] + m_reference[(size_t)i * 2 + 1];

	int best = SEARCH;
	double best_score = -1e300;
	for (int j = 0; j <= 2 * SEARCH; j++) {
		double along = 0.0;
		double energy = 0.0;
		for (int i = 0; i < length; i += SEARCH_STRIDE) {
			const double s = m_mono_search[j + i];
			along += (double)m_mono_reference[i] * s;
			energy += s * s;
		}
		const double score = along / sqrt(energy + 1e-9);
		// (A tie, as in silence, stays with the one nearest the middle: the nominal place.)
		if (score > best_score + 1e-12 || (fabs(score - best_score) <= 1e-12 && abs(j - SEARCH) < abs(best - SEARCH))) {
			best_score = score;
			best = j;
		}
	}
	// Refine at full resolution around the best.
	int refined = best;
	double refined_score = -1e300;
	for (int j = best - REFINE; j <= best + REFINE; j++) {
		if (j < 0 || j > 2 * SEARCH)
			continue;
		double along = 0.0;
		double energy = 0.0;
		for (int i = 0; i < length; i++) {
			const double s = m_mono_search[j + i];
			along += (double)m_mono_reference[i] * s;
			energy += s * s;
		}
		const double score = along / sqrt(energy + 1e-9);
		if (score > refined_score) {
			refined_score = score;
			refined = j;
		}
	}
	return nominal - SEARCH + refined;
}

void PitchKeeper::make_block() {
	const int64_t block = m_next_block;
	const int64_t grain_start = choose_start(block);
	add_grain(grain_start, block == 0);
	memcpy(m_ready.data(), m_accumulator.data(), sizeof(float) * (size_t)HOP * 2);
	shift_accumulator();
	m_previous_start = grain_start;
	m_have_previous = true;
	m_next_block = block + 1;
}

void PitchKeeper::render(float* out, int frames) {
	if (m_source == nullptr) {
		memset(out, 0, sizeof(float) * (size_t)frames * 2);
		return;
	}
	int done = 0;
	while (done < frames) {
		if (m_ready_pos >= HOP) {
			make_block();
			m_ready_pos = 0;
		}
		int count = HOP - m_ready_pos;
		if (count > frames - done)
			count = frames - done;
		memcpy(out + (size_t)done * 2, &m_ready[(size_t)m_ready_pos * 2], sizeof(float) * (size_t)count * 2);
		m_ready_pos += count;
		done += count;
	}
	m_next_output += frames;
}

}  // namespace audio_stretch
