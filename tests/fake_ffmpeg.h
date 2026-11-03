#pragma once
// A stand-in for the few FFmpeg functions MediaDecoder uses, over synthetic "files" held in
// memory, so the decoder's own logic (switching files, draining, seeking, the frame cache, the
// audio offsets) can be tested without FFmpeg or any real media.
//
// A fake file has N video frames at 20 fps on a 1/600 time base (30 ticks per frame), a key frame
// every `gop` frames, a decoder that holds back `delay` frames (as real decoders do), and
// optionally audio (a packet of 2400 samples at 48 kHz per two video frames). Each decoded
// picture is 64x48 and carries its identity in the pixels: Y[0] and Y[1] hold the frame number
// within the file (low/high byte), U[0] the file's `tag`.
#include <string>

struct FakeFileSpec {
	int  frames;
	int  gop;
	int  delay;
	bool audio;
	int  tag;            // Shows up in U[0] of every frame.
	long long start_time;  // The video stream's start_time, in ticks (also added to every pts).
	long long audio_start; // Same for audio, in samples.
	int  width, height;
	bool discard_last;         // The last video packet carries AV_PKT_FLAG_DISCARD (real VIRB files do), so a decoder drops its frame.
	long long duration_extra;  // How much longer than the frames the stream claims to be, in ticks (negative: shorter).
	FakeFileSpec() : frames(100), gop(10), delay(2), audio(true), tag(0), start_time(0), audio_start(0), width(64), height(48), discard_last(false), duration_extra(0) {}
};

void fake_ffmpeg_reset();
void fake_ffmpeg_add_file(const std::string& path, const FakeFileSpec& spec);
