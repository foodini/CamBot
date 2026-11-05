#include "audio_output.h"

#include <string.h>
#include <vector>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

#include "audio_plan.h"

struct AudioOutput::State {
	HWAVEOUT           device;
	WAVEHDR            headers[BUFFER_COUNT];
	std::vector<short> data[BUFFER_COUNT];
	bool               busy[BUFFER_COUNT];   // Queued, and not yet played.
	DWORD              last_position;
	int64_t            wraps;                // Times the device's 32-bit count of frames has come round.
	State() : device(nullptr), last_position(0), wraps(0) {
		memset(headers, 0, sizeof(headers));
		for (int i = 0; i < BUFFER_COUNT; i++)
			busy[i] = false;
	}
};

AudioOutput::AudioOutput() : m_state(nullptr) {
}

AudioOutput::~AudioOutput() {
	close();
}

bool AudioOutput::open() {
	if (m_state != nullptr)
		return true;
	State* state = new State();
	WAVEFORMATEX format;
	memset(&format, 0, sizeof(format));
	format.wFormatTag = WAVE_FORMAT_PCM;
	format.nChannels = 2;
	format.nSamplesPerSec = audio_plan::SAMPLE_RATE;
	format.wBitsPerSample = 16;
	format.nBlockAlign = (WORD)(format.nChannels * format.wBitsPerSample / 8);
	format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
	if (waveOutOpen(&state->device, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
		delete state;
		return false;
	}
	for (int i = 0; i < BUFFER_COUNT; i++) {
		state->data[i].assign((size_t)BUFFER_FRAMES * 2, 0);
		state->headers[i].lpData = (LPSTR)state->data[i].data();
		state->headers[i].dwBufferLength = (DWORD)(BUFFER_FRAMES * 2 * sizeof(short));
		if (waveOutPrepareHeader(state->device, &state->headers[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
			for (int j = 0; j < i; j++)
				waveOutUnprepareHeader(state->device, &state->headers[j], sizeof(WAVEHDR));
			waveOutClose(state->device);
			delete state;
			return false;
		}
	}
	m_state = state;
	return true;
}

void AudioOutput::close() {
	if (m_state == nullptr)
		return;
	waveOutReset(m_state->device);
	for (int i = 0; i < BUFFER_COUNT; i++)
		waveOutUnprepareHeader(m_state->device, &m_state->headers[i], sizeof(WAVEHDR));
	waveOutClose(m_state->device);
	delete m_state;
	m_state = nullptr;
}

int AudioOutput::free_buffers() {
	if (m_state == nullptr)
		return 0;
	int free_count = 0;
	for (int i = 0; i < BUFFER_COUNT; i++) {
		if (m_state->busy[i] && (m_state->headers[i].dwFlags & WHDR_DONE))
			m_state->busy[i] = false;
		if (!m_state->busy[i])
			free_count++;
	}
	return free_count;
}

bool AudioOutput::submit(const float* frames) {
	if (free_buffers() == 0)
		return false;
	int which = 0;
	while (m_state->busy[which])
		which++;
	short* out = m_state->data[which].data();
	for (int i = 0; i < BUFFER_FRAMES * 2; i++) {
		float value = frames[i] * 32767.0f;
		if (value > 32767.0f)
			value = 32767.0f;
		if (value < -32768.0f)
			value = -32768.0f;
		out[i] = (short)value;
	}
	if (waveOutWrite(m_state->device, &m_state->headers[which], sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
		return false;
	m_state->busy[which] = true;
	return true;
}

int64_t AudioOutput::played_frames() {
	if (m_state == nullptr)
		return 0;
	MMTIME time;
	memset(&time, 0, sizeof(time));
	time.wType = TIME_SAMPLES;
	if (waveOutGetPosition(m_state->device, &time, sizeof(time)) != MMSYSERR_NOERROR)
		return m_state->wraps * 4294967296LL + m_state->last_position;
	DWORD position = m_state->last_position;
	if (time.wType == TIME_SAMPLES)
		position = time.u.sample;
	else if (time.wType == TIME_BYTES)
		position = time.u.cb / 4;   // Four bytes a frame: two channels of 16 bits.
	if (position < m_state->last_position)
		m_state->wraps++;
	m_state->last_position = position;
	return m_state->wraps * 4294967296LL + position;
}

void AudioOutput::reset() {
	if (m_state == nullptr)
		return;
	waveOutReset(m_state->device);
	for (int i = 0; i < BUFFER_COUNT; i++)
		m_state->busy[i] = false;
	m_state->last_position = 0;
	m_state->wraps = 0;
}

#else   // No speakers to be had.

struct AudioOutput::State {
};

AudioOutput::AudioOutput() : m_state(nullptr) {
}

AudioOutput::~AudioOutput() {
}

bool AudioOutput::open() { return false; }
void AudioOutput::close() {}
int AudioOutput::free_buffers() { return 0; }
bool AudioOutput::submit(const float*) { return false; }
int64_t AudioOutput::played_frames() { return 0; }
void AudioOutput::reset() {}

#endif
