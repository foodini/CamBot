// The project file itself (project_data.cpp): the JSON that is written and read, with the real jsoncpp. Needs jsoncpp's source,
// which run_tests.sh finds in the vcpkg folder and skips this test without.
#include <math.h>
#include <stdio.h>
#include <string>
#include <vector>

#include "project_data.h"

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); g_failures++; } } while (0)
#define CHECK_MSG(cond, ...) do { if (!(cond)) { printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); g_failures++; } } while (0)

static ProjectData sample_project() {
	ProjectData project;
	project.video_files.push_back("a.mp4");
	project.video_files.push_back("b.mp4");
	project.time_base_num = 1;
	project.time_base_den = 25000;
	project.capture_order.push_back(1);
	project.capture_order.push_back(0);
	project.clips.push_back(Clip(0, 0, 50000));
	project.clips.push_back(Clip(0, 50000, 125000, 0.5, AUDIO_MUTE));
	project.clips.push_back(Clip(1, 1000, 90000, 2.0, AUDIO_CHANGE_PITCH));
	project.clips.push_back(Clip(1, 90000, 100000, 0.3, AUDIO_KEEP_PITCH));
	project.clips.push_back(Clip(0, 5, 6, 16.0, AUDIO_KEEP_PITCH));
	project.clips.push_back(Clip(0, 5, 6, 0.1, AUDIO_CHANGE_PITCH));
	project.edit_started = true;
	project.sync_anchors.push_back(SyncAnchor(FileTime(0, 1234), 17.25f));
	project.launch_mark = FileTime(1, 777);
	return project;
}

static void test_speed_round_trip() {
	ProjectData project = sample_project();
	const std::string text = project_io::to_json(project);
	ProjectData back;
	std::string error;
	CHECK_MSG(project_io::parse_json(text, &back, &error), "%s", error.c_str());
	CHECK(back == project);
	CHECK(back.clips.size() == 6);
	CHECK(back.clips[1].speed == 0.5 && back.clips[1].audio_mode == AUDIO_MUTE);
	CHECK(back.clips[2].speed == 2.0 && back.clips[2].audio_mode == AUDIO_CHANGE_PITCH);
	CHECK(back.clips[3].speed == 0.3 && back.clips[3].audio_mode == AUDIO_KEEP_PITCH);   // 0.3 has no exact binary form: it must still come back.
	CHECK(back.clips[4].speed == 16.0 && back.clips[5].speed == 0.1);
	// Normal-speed clips with the usual treatment say nothing about either.
	ProjectData plain;
	plain.video_files.push_back("a.mp4");
	plain.time_base_num = 1;
	plain.time_base_den = 25000;
	plain.clips.push_back(Clip(0, 0, 100));
	const std::string plain_text = project_io::to_json(plain);
	CHECK(plain_text.find("speed") == std::string::npos);
	CHECK(plain_text.find("audio") == std::string::npos);
	CHECK(plain_text.find("\"version\" : 5") != std::string::npos);
	// Written again, the text is the same: nothing creeps.
	CHECK(project_io::to_json(back) == text);
	// And a speed change shows as a change to the project.
	ProjectData changed = project;
	changed.clips[1].speed = 0.25;
	CHECK(changed != project);
	changed = project;
	changed.clips[1].audio_mode = AUDIO_KEEP_PITCH;
	CHECK(changed != project);
}

static void test_through_the_model() {
	ProjectData project = sample_project();
	std::vector<int64_t> durations;
	durations.push_back(200000);
	durations.push_back(150000);
	EditModel model;
	std::string error;
	CHECK_MSG(project_io::apply_to_model(project, durations, 1, 25000, &model, &error), "%s", error.c_str());
	CHECK(model.edit_list().count() == 6);
	CHECK(model.edit_list().clip(1).speed == 0.5);
	CHECK(model.edit_list().clip(1).timeline_ticks() == 150000);
	ProjectData written = project;
	project_io::fill_from_model(model, 1, 25000, &written);
	CHECK(written.clips == project.clips);
	// Through a file's text too, with the time base changed on the way (speeds are not times, and stay as they were).
	const std::string text = project_io::to_json(written);
	ProjectData read;
	CHECK(project_io::parse_json(text, &read, &error));
	EditModel other;
	std::vector<int64_t> other_durations;
	other_durations.push_back(400000);
	other_durations.push_back(300000);
	CHECK_MSG(project_io::apply_to_model(read, other_durations, 1, 50000, &other, &error), "%s", error.c_str());
	CHECK(other.edit_list().clip(1).in_ticks == 100000 && other.edit_list().clip(1).speed == 0.5);
	CHECK(other.edit_list().clip(2).speed == 2.0 && other.edit_list().clip(2).audio_mode == AUDIO_CHANGE_PITCH);
}

static std::string project_text(int version, const std::string& clips_json) {
	return "{\"format\":\"cambot-project\",\"version\":" + std::to_string(version) + ",\"video_files\":[\"a.mp4\"],\"time_base\":[1,25000],\"clips\":" +
		clips_json + "}";
}

static void test_older_and_bad_files() {
	std::string error;
	ProjectData project;
	// Version 4: no speeds anywhere; the clips are at normal speed.
	CHECK_MSG(project_io::parse_json(project_text(4, "[{\"media\":0,\"in\":0,\"out\":100},{\"media\":0,\"in\":100,\"out\":300}]"), &project, &error), "%s", error.c_str());
	CHECK(project.clips.size() == 2 && project.clips[0].speed == 1.0 && project.clips[1].audio_mode == AUDIO_KEEP_PITCH);
	CHECK(project.clips[1] == Clip(0, 100, 300));
	// Version 3 too.
	CHECK(project_io::parse_json(project_text(3, "[{\"media\":0,\"in\":0,\"out\":100}]"), &project, &error));
	// A newer file is refused politely.
	project = ProjectData();
	CHECK(!project_io::parse_json(project_text(6, "[{\"media\":0,\"in\":0,\"out\":100}]"), &project, &error));
	CHECK(error.find("newer version") != std::string::npos);
	// Speeds out of range or the wrong sort of thing are refused, with a sentence about it.
	CHECK(!project_io::parse_json(project_text(5, "[{\"media\":0,\"in\":0,\"out\":100,\"speed\":0.05}]"), &project, &error));
	CHECK(error.find("speed") != std::string::npos);
	CHECK(!project_io::parse_json(project_text(5, "[{\"media\":0,\"in\":0,\"out\":100,\"speed\":17}]"), &project, &error));
	CHECK(!project_io::parse_json(project_text(5, "[{\"media\":0,\"in\":0,\"out\":100,\"speed\":0}]"), &project, &error));
	CHECK(!project_io::parse_json(project_text(5, "[{\"media\":0,\"in\":0,\"out\":100,\"speed\":-2}]"), &project, &error));
	CHECK(!project_io::parse_json(project_text(5, "[{\"media\":0,\"in\":0,\"out\":100,\"speed\":\"fast\"}]"), &project, &error));
	CHECK(!project_io::parse_json(project_text(5, "[{\"media\":0,\"in\":0,\"out\":100,\"audio\":\"loud\"}]"), &project, &error));
	CHECK(error.find("audio") != std::string::npos);
	CHECK(!project_io::parse_json(project_text(5, "[{\"media\":0,\"in\":0,\"out\":100,\"audio\":3}]"), &project, &error));
	// Valid ones.
	CHECK_MSG(project_io::parse_json(project_text(5, "[{\"media\":0,\"in\":0,\"out\":100,\"speed\":4,\"audio\":\"mute\"}]"), &project, &error), "%s", error.c_str());
	CHECK(project.clips[0].speed == 4.0 && project.clips[0].audio_mode == AUDIO_MUTE);
	CHECK(project_io::parse_json(project_text(5, "[{\"media\":0,\"in\":0,\"out\":100,\"speed\":0.1},{\"media\":0,\"in\":0,\"out\":100,\"speed\":16}]"), &project, &error));
}

int main() {
	test_speed_round_trip();
	test_through_the_model();
	test_older_and_bad_files();
	if (g_failures == 0)
		printf("project_json_test: all ok\n");
	return g_failures == 0 ? 0 : 1;
}
