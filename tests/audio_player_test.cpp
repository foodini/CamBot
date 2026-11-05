// Playing an edit through the speakers (audio_player.h), against a fake reader (the left channel is the sample's number in the
// file, the right is the file's tag) and fake speakers that play only when told to.
#include <math.h>
#include <stdio.h>
#include <vector>

#include "audio_player.h"

void fake_output_reset_all(bool can_open);
int  fake_output_reset_count();
const std::vector<float>& fake_output_submitted();
int  fake_output_queued();
void fake_output_play(int64_t frames);

static int g_failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_failures++; } } while (0)

static const double TICK = 0.001;      // 48 samples to a tick.
static const int BUFFER = AudioOutput::BUFFER_FRAMES;

static std::vector<std::string> names() {
	std::vector<std::string> paths;
	paths.push_back("a.mp4");
	paths.push_back("silent.mp4");
	return paths;
}

static EditList list_of(const Clip& one) {
	std::vector<Clip> clips;
	clips.push_back(one);
	EditList list;
	list.set(clips);
	return list;
}

static void test_start_fills_the_speakers() {
	fake_output_reset_all(true);
	AudioPlayer player;
	player.set_edit(names(), list_of(Clip(0, 0, 2000)), TICK);
	CHECK(!player.playing());
	CHECK(player.position_ticks() == -1);
	CHECK(player.start(500));
	CHECK(player.playing());
	CHECK(fake_output_queued() == AudioOutput::BUFFER_COUNT);
	CHECK(player.position_ticks() == 500);
	// What was queued is the file's sound from where it was asked to start (500 ticks = sample 24000), unfaded in the middle.
	const std::vector<float>& sent = fake_output_submitted();
	CHECK(sent.size() == (size_t)AudioOutput::BUFFER_COUNT * BUFFER * 2);
	CHECK(sent[0] == 24000.0f && sent[1] == 1.0f);
	CHECK(sent[100 * 2] == 24100.0f);
}

static void test_position_follows_what_is_played() {
	fake_output_reset_all(true);
	AudioPlayer player;
	player.set_edit(names(), list_of(Clip(0, 0, 2000)), TICK);
	player.start(500);
	fake_output_play(2400);   // 50 ticks.
	CHECK(player.position_ticks() == 550);
	fake_output_play(24);     // Half a tick is still the same tick, rounded.
	CHECK(player.position_ticks() == 551 || player.position_ticks() == 550);
	// Only as much as the device has played counts, not what has been queued.
	CHECK(fake_output_queued() == AudioOutput::BUFFER_COUNT - 2);
}

static void test_update_keeps_the_sound_continuous() {
	fake_output_reset_all(true);
	AudioPlayer player;
	player.set_edit(names(), list_of(Clip(0, 0, 3000)), TICK);
	player.start(1000);
	for (int i = 0; i < 20; i++) {
		fake_output_play(BUFFER * 3 / 2);   // Not a whole number of buffers.
		player.update();
	}
	const std::vector<float>& sent = fake_output_submitted();
	CHECK(sent.size() > (size_t)BUFFER * 2 * 20);
	// Every frame is the one after the last: the file's sample numbers run on without a gap or a repeat.
	bool continuous = true;
	for (size_t i = 1; i * 2 < sent.size(); i++)
		continuous = continuous && sent[i * 2] == sent[(i - 1) * 2] + 1.0f;
	CHECK(continuous);
	// And the queue is kept full.
	CHECK(fake_output_queued() == AudioOutput::BUFFER_COUNT);
}

static void test_volume_and_mute() {
	fake_output_reset_all(true);
	AudioPlayer player;
	player.set_edit(names(), list_of(Clip(0, 0, 2000)), TICK);   // (The fake file is a little over 2000 ticks long.)
	player.set_volume(0.5f);
	player.start(1000);
	// The right channel is a steady 1 at the file's tag, so it shows the volume directly.
	CHECK(fake_output_submitted()[1] == 0.5f);

	// Muting ramps down across a buffer and then is silent.
	player.set_muted(true);
	fake_output_play(BUFFER * 4);
	player.update();
	const std::vector<float>& sent = fake_output_submitted();
	const size_t first_new = (size_t)AudioOutput::BUFFER_COUNT * BUFFER * 2;
	CHECK(sent.size() == first_new + (size_t)4 * BUFFER * 2);
	CHECK(sent[first_new + 1] < 0.5f && sent[first_new + 1] > 0.0f);                    // On its way down.
	CHECK(sent[first_new + (BUFFER - 1) * 2 + 1] == 0.0f);                              // Down.
	CHECK(sent[first_new + (size_t)(BUFFER + 10) * 2 + 1] == 0.0f);                     // And staying there.
	// ...but still playing, still counting.
	CHECK(player.playing());

	// Unmuting comes back up, to the volume, not past it.
	player.set_muted(false);
	fake_output_play(BUFFER * 4);
	player.update();
	const size_t second_new = first_new + (size_t)4 * BUFFER * 2;
	CHECK(sent.size() == second_new + (size_t)4 * BUFFER * 2);
	CHECK(sent[second_new + 1] > 0.0f && sent[second_new + 1] < 0.5f);
	CHECK(sent[second_new + (size_t)(BUFFER + 10) * 2 + 1] == 0.5f);

	// A volume set while muted is what it comes back to.
	player.set_muted(true);
	fake_output_play(BUFFER * 4);
	player.update();
	player.set_volume(0.25f);
	player.set_muted(false);
	fake_output_play(BUFFER * 4);
	player.update();
	CHECK(sent[sent.size() - 1] == 0.25f);
}

static void test_stop_and_restart() {
	fake_output_reset_all(true);
	AudioPlayer player;
	player.set_edit(names(), list_of(Clip(0, 0, 2000)), TICK);
	player.start(100);
	fake_output_play(4800);
	player.stop();
	CHECK(!player.playing());
	CHECK(player.position_ticks() == -1);
	CHECK(fake_output_queued() == 0);
	const int resets = fake_output_reset_count();
	player.update();          // Not playing: leaves the speakers alone.
	CHECK(fake_output_queued() == 0);
	// Starting again, somewhere else, counts from there.
	CHECK(player.start(1200));
	CHECK(fake_output_reset_count() > resets);
	CHECK(player.position_ticks() == 1200);
	CHECK(fake_output_submitted()[0] == 57600.0f);
	fake_output_play(480);
	CHECK(player.position_ticks() == 1210);
}

static void test_the_end_of_the_edit() {
	fake_output_reset_all(true);
	AudioPlayer player;
	player.set_edit(names(), list_of(Clip(0, 0, 100)), TICK);   // 4800 samples: four buffers.
	CHECK(player.start(0));
	const std::vector<float>& sent = fake_output_submitted();
	CHECK(sent.size() == (size_t)AudioOutput::BUFFER_COUNT * BUFFER * 2);
	CHECK(sent[(size_t)(4800 - 2) * 2] != 0.0f || sent[(size_t)(4800 - 3) * 2] != 0.0f);   // Sound to the last fraction of the end.
	CHECK(sent[(size_t)4800 * 2] == 0.0f);                                                   // Then silence.
	CHECK(sent[sent.size() - 1] == 0.0f);
	// The clock runs on past the end, which is how the picture knows to stop.
	fake_output_play(BUFFER * 8);
	CHECK(player.position_ticks() >= 100);
}

static void test_no_speakers_or_no_sound() {
	fake_output_reset_all(false);
	AudioPlayer without_device;
	without_device.set_edit(names(), list_of(Clip(0, 0, 2000)), TICK);
	CHECK(!without_device.start(0));
	CHECK(!without_device.playing());

	fake_output_reset_all(true);
	AudioPlayer silent;
	silent.set_edit(names(), list_of(Clip(1, 0, 2000)), TICK);   // The file with no sound.
	CHECK(!silent.start(0));
	CHECK(!silent.playing());
	CHECK(silent.position_ticks() == -1);
	CHECK(fake_output_queued() == 0);
}

int main() {
	test_start_fills_the_speakers();
	test_position_follows_what_is_played();
	test_update_keeps_the_sound_continuous();
	test_volume_and_mute();
	test_stop_and_restart();
	test_the_end_of_the_edit();
	test_no_speakers_or_no_sound();
	if (g_failures == 0)
		printf("audio_player_test: all ok\n");
	return g_failures == 0 ? 0 : 1;
}
