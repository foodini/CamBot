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

static std::string project_text(int version, const std::string& clips_json);

static void test_transitions_in_the_file() {
	ProjectData project = sample_project();
	project.transitions.assign(project.clips.size() + 1, Transition());
	project.transitions[0] = Transition(TRANSITION_FADE, 12000);
	project.transitions[6] = Transition(TRANSITION_FADE, 3000);
	const std::string text = project_io::to_json(project);
	CHECK(text.find("\"transitions\"") != std::string::npos);
	CHECK(text.find("\"fade\"") != std::string::npos);
	ProjectData back;
	std::string error;
	CHECK_MSG(project_io::parse_json(text, &back, &error), "%s", error.c_str());
	CHECK(back == project);
	CHECK(back.transitions.size() == 7 && back.transitions[0] == project.transitions[0] && back.transitions[6] == project.transitions[6]);
	CHECK(project_io::to_json(back) == text);
	ProjectData changed = project;
	changed.transitions[0].ticks = 13000;
	CHECK(changed != project);
	// A list with only cuts in it is no list at all.
	ProjectData cuts = sample_project();
	cuts.transitions.assign(cuts.clips.size() + 1, Transition());
	CHECK(project_io::to_json(cuts).find("transitions") == std::string::npos);

	// Bad ones.
	const std::string two_clips = "[{\"media\":0,\"in\":0,\"out\":1000},{\"media\":0,\"in\":1000,\"out\":3000}]";
	const std::string head = "{\"format\":\"cambot-project\",\"version\":6,\"video_files\":[\"a.mp4\"],\"time_base\":[1,25000],\"clips\":" + two_clips + ",\"transitions\":";
	CHECK(project_io::parse_json(head + "[{\"seam\":0,\"type\":\"fade\",\"ticks\":100},{\"seam\":2,\"type\":\"fade\",\"ticks\":300}]}", &back, &error));
	CHECK(back.transitions.size() == 3 && back.transitions[0].ticks == 100 && back.transitions[1].is_cut() && back.transitions[2].ticks == 300);
	CHECK(!project_io::parse_json(head + "[{\"seam\":3,\"type\":\"fade\",\"ticks\":100}]}", &back, &error));
	CHECK(error.find("not in the edit") != std::string::npos);
	CHECK(!project_io::parse_json(head + "[{\"seam\":0,\"type\":\"wipe\",\"ticks\":100}]}", &back, &error));
	CHECK(error.find("type") != std::string::npos);
	CHECK(!project_io::parse_json(head + "[{\"seam\":0,\"ticks\":100}]}", &back, &error));
	CHECK(!project_io::parse_json(head + "[{\"seam\":0,\"type\":\"fade\",\"ticks\":-1}]}", &back, &error));
	CHECK(!project_io::parse_json(head + "{}}", &back, &error));
	CHECK(!project_io::parse_json("{\"format\":\"cambot-project\",\"version\":6,\"video_files\":[\"a.mp4\"],\"time_base\":[1,25000],\"transitions\":[{\"seam\":0,\"type\":\"fade\",\"ticks\":5}]}", &back, &error));
	// An older file has none, and reads as all cuts.
	CHECK(project_io::parse_json(project_text(5, two_clips), &back, &error));
	CHECK(back.transitions.empty());
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
	CHECK(plain_text.find("\"version\" : 8") != std::string::npos);
	CHECK(plain_text.find("transitions") == std::string::npos);
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

// A whole project, with something of everything in it, through the model, the text of the file, and back: nothing is lost,
// reordered or rounded on the way, and writing it a second time gives the same text.
static ProjectData full_project() {
	ProjectData project;
	project.video_files.push_back("first.mp4");
	project.video_files.push_back("second.mp4");
	project.video_files.push_back("third.mp4");
	project.time_base_num = 1;
	project.time_base_den = 25000;
	project.capture_order.push_back(2);     // Recorded in a different order from the one they were added in.
	project.capture_order.push_back(0);
	project.capture_order.push_back(1);
	project.clips.push_back(Clip(1, 10000, 90000));
	project.clips.push_back(Clip(0, 5000, 65000, 0.5, AUDIO_KEEP_PITCH));
	project.clips.push_back(Clip(2, 0, 50000, 2.0, AUDIO_CHANGE_PITCH));
	project.clips.push_back(Clip(0, 100000, 150000, 0.25, AUDIO_MUTE));
	project.clips.push_back(Clip(1, 90000, 120000));
	project.edit_started = true;
	project.sync_anchors.push_back(SyncAnchor(FileTime(0, 20000), 1755.5f));
	project.sync_anchors.push_back(SyncAnchor(FileTime(1, 60000), 1801.25f));
	project.launch_mark = FileTime(1, 12345);
	project.transitions.assign(project.clips.size() + 1, Transition());
	project.transitions[0] = Transition(TRANSITION_FADE, 12500);      // Half the first clip is 40000.
	project.transitions[5] = Transition(TRANSITION_FADE, 15000);      // Half the last, which is 30000 (at normal speed).
	TextBox first = text_box::default_box();
	first.id = 3;
	first.anchor = FileTime(1, 20000);
	first.fade_in_ticks = 12500;
	first.dwell_ticks = 75000;
	first.fade_out_ticks = 25000;
	first.text = "Fort Funston\nsecond line";
	first.style.bold = true;
	first.style.align = TEXT_ALIGN_RIGHT;
	TextBox second = text_box::default_box();
	second.id = 9;
	second.anchor = FileTime(0, 30000);
	second.text = "Short";
	project.text_boxes.push_back(first);
	project.text_boxes.push_back(second);
	TrackedPath hawk;
	hawk.id = 2;
	hawk.name = "Hawk \"A\"";
	hawk.media = 1;
	hawk.curve.set_point(10000, 0.125, 0.5);
	hawk.curve.set_point(20000, 0.5, 0.25);
	hawk.curve.set_point(90000, 0.875, 0.75);
	hawk.circle.enabled = true;
	hawk.circle.radius = 0.0625f;
	hawk.circle.color[1] = 0.5f;
	TrackedPath still;
	still.id = 5;
	still.media = 0;
	still.curve.set_orientation_aware(false);
	still.curve.set_point(0, 0.5, 0.5);
	project.paths.push_back(hawk);
	project.paths.push_back(still);
	project.path_fov_degrees = 97.5;
	project.telemetry_file = "D:\\flights\\2026.09.27 Fort Funston.LOG";
	project.window_start_elapsed_s = 12.5f;
	project.rotation_rad = 3.14159274f;
	project.altitude_refs.start_ft = 12.5f;
	project.altitude_refs.end_ft = 40.0f;
	project.altitude_refs.end_same_as_start = false;
	return project;
}

static void test_full_round_trip() {
	std::vector<int64_t> durations;
	durations.push_back(200000);
	durations.push_back(150000);
	durations.push_back(100000);
	const ProjectData project = full_project();
	std::string error;
	EditModel model;
	CHECK_MSG(project_io::apply_to_model(project, durations, 1, 25000, &model, &error), "%s", error.c_str());
	CHECK(model.edit_list().count() == 5);
	CHECK(model.capture().order().size() == 3 && model.capture().order()[0] == 2 && model.capture().order()[1] == 0 && model.capture().order()[2] == 1);
	CHECK(model.anchors().size() == 2);
	CHECK(model.text_boxes().size() == 2);
	CHECK(model.paths().size() == 2 && model.paths()[0] == project.paths[0]);
	CHECK(model.path_fov_degrees() == 97.5);
	CHECK(model.edit_list().transition(0) == project.transitions[0] && model.edit_list().transition(5) == project.transitions[5]);

	ProjectData written = project;
	project_io::fill_from_model(model, 1, 25000, &written);
	CHECK(written == project);

	// Through the text of the file.
	const std::string text = project_io::to_json(written);
	ProjectData read;
	CHECK_MSG(project_io::parse_json(text, &read, &error), "%s", error.c_str());
	CHECK(read == project);
	CHECK(project_io::to_json(read) == text);

	// And into a fresh model, which must be the same as the first in everything that can be asked of it.
	EditModel again;
	CHECK_MSG(project_io::apply_to_model(read, durations, 1, 25000, &again, &error), "%s", error.c_str());
	CHECK(again.edit_list().clips() == model.edit_list().clips());
	CHECK(again.edit_list().total_ticks() == model.edit_list().total_ticks());
	CHECK(again.capture().order() == model.capture().order());
	CHECK(again.anchors() == model.anchors());
	CHECK(again.text_boxes() == model.text_boxes());
	CHECK(again.paths() == model.paths() && again.path_fov_degrees() == model.path_fov_degrees());
	CHECK(again.edit_list().transitions() == model.edit_list().transitions());
	CHECK(again.launch_flight_seconds() == model.launch_flight_seconds());
	CHECK(again.edit_started() == model.edit_started());
	const int64_t total = model.edit_list().total_ticks();
	for (int step = 0; step <= 10; step++) {
		const int64_t ticks = total * step / 11;
		CHECK(again.telemetry_seconds_for_timeline(ticks) == model.telemetry_seconds_for_timeline(ticks));
	}
	ProjectData rewritten = read;
	project_io::fill_from_model(again, 1, 25000, &rewritten);
	CHECK(rewritten == project);
	CHECK(project_io::to_json(rewritten) == text);
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
	CHECK(!project_io::parse_json(project_text(9, "[{\"media\":0,\"in\":0,\"out\":100}]"), &project, &error));
	CHECK(error.find("newer version") != std::string::npos);
	// Paths: older files have none; bad ones are refused with a sentence about it.
	const std::string base = "{\"format\":\"cambot-project\",\"version\":8,\"video_files\":[\"a.mp4\"],\"time_base\":[1,25000],\"clips\":[{\"media\":0,\"in\":0,\"out\":100}],\"paths\":";
	CHECK_MSG(project_io::parse_json(base + "[{\"id\":1,\"media\":0,\"points\":[[200,0.5,0.5],[100,0.1,0.2]]}]}", &project, &error), "%s", error.c_str());
	CHECK(project.paths.size() == 1 && project.paths[0].curve.points().size() == 2 && project.paths[0].curve.first_ticks() == 100);
	CHECK(project.paths[0].curve.orientation_aware() && !project.paths[0].circle.enabled && project.path_fov_degrees == DEFAULT_PATH_FOV_DEGREES);
	CHECK(!project_io::parse_json(base + "[{\"id\":1,\"media\":0},{\"id\":1,\"media\":0}]}", &project, &error));
	CHECK(error.find("same") != std::string::npos);
	CHECK(!project_io::parse_json(base + "[{\"id\":1,\"media\":3}]}", &project, &error));
	CHECK(!project_io::parse_json(base + "[{\"id\":1,\"media\":0,\"points\":[[1,0.5]]}]}", &project, &error));
	CHECK(error.find("point") != std::string::npos);
	CHECK(!project_io::parse_json(base + "[{\"id\":1,\"media\":0,\"circle\":{\"radius\":0}}]}", &project, &error));
	CHECK(!project_io::parse_json(base + "{}}", &project, &error));
	CHECK(!project_io::parse_json("{\"format\":\"cambot-project\",\"version\":8,\"video_files\":[\"a.mp4\"],\"clips\":[{\"media\":0,\"in\":0,\"out\":100}],\"paths\":[{\"id\":1,\"media\":0}]}", &project, &error));
	CHECK(error.find("time_base") != std::string::npos);
	CHECK(!project_io::parse_json("{\"format\":\"cambot-project\",\"version\":8,\"video_files\":[\"a.mp4\"],\"path_fov\":500}", &project, &error));
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

static void test_transitions_through_the_model() {
	ProjectData project = sample_project();
	project.clips.clear();
	project.clips.push_back(Clip(0, 0, 50000));          // 2 seconds.
	project.clips.push_back(Clip(1, 1000, 91000));       // 3.6 seconds.
	project.transitions.assign(3, Transition());
	project.transitions[0] = Transition(TRANSITION_FADE, 20000);     // Over the 25000 that half the first clip allows? No: under.
	project.transitions[2] = Transition(TRANSITION_FADE, 45000);     // Exactly half of 90000.
	project.edit_started = true;
	std::vector<int64_t> durations;
	durations.push_back(200000);
	durations.push_back(150000);
	EditModel model;
	std::string error;
	CHECK_MSG(project_io::apply_to_model(project, durations, 1, 25000, &model, &error), "%s", error.c_str());
	CHECK(model.edit_list().transition(0) == project.transitions[0] && model.edit_list().transition(2) == project.transitions[2]);
	// With another time base the lengths are converted.
	EditModel other;
	std::vector<int64_t> other_durations;
	other_durations.push_back(400000);
	other_durations.push_back(300000);
	CHECK_MSG(project_io::apply_to_model(project, other_durations, 1, 50000, &other, &error), "%s", error.c_str());
	CHECK(other.edit_list().transition(0).ticks == 40000 && other.edit_list().transition(2).ticks == 90000);
	// A file that was edited to have too long a fade, or a kind of transition that isn't allowed there, is read all the same: shortened, or a cut.
	ProjectData wrong = project;
	wrong.transitions[0] = Transition(TRANSITION_FADE, 99999);
	wrong.transitions[1] = Transition(TRANSITION_FADE, 100);
	wrong.transitions[2] = Transition(TRANSITION_CROSSFADE, 100);
	EditModel fixed;
	CHECK_MSG(project_io::apply_to_model(wrong, durations, 1, 25000, &fixed, &error), "%s", error.c_str());
	CHECK(fixed.edit_list().transition(0).type == TRANSITION_FADE && fixed.edit_list().transition(0).ticks == 25000);
	CHECK(fixed.edit_list().transition(1).is_cut() && fixed.edit_list().transition(2).is_cut());
	// A crossfade at a join is kept, shortened to what the two clips leave room for.
	ProjectData crossed = project;
	crossed.transitions[0] = Transition();
	crossed.transitions[1] = Transition(TRANSITION_CROSSFADE, 99999);
	crossed.transitions[2] = Transition();
	EditModel joined;
	CHECK_MSG(project_io::apply_to_model(crossed, durations, 1, 25000, &joined, &error), "%s", error.c_str());
	CHECK(joined.edit_list().transition(1).type == TRANSITION_CROSSFADE);
	CHECK(joined.edit_list().transition(1).ticks == joined.edit_list().max_transition_ticks(1) && joined.edit_list().transitions_valid());
	// Written back out, what is in the model.
	ProjectData written = project;
	project_io::fill_from_model(model, 1, 25000, &written);
	CHECK(written.transitions == project.transitions);
	ProjectData cleared = project;
	project_io::fill_from_model(fixed, 1, 25000, &cleared);
	CHECK(cleared.transitions.size() == 3 && cleared.transitions[0].ticks == 25000);
	EditModel plain;
	ProjectData no_clips;
	no_clips.video_files = project.video_files;
	CHECK(project_io::apply_to_model(no_clips, durations, 1, 25000, &plain, &error));
	ProjectData filled;
	project_io::fill_from_model(plain, 1, 25000, &filled);
	CHECK(filled.transitions.empty());
}

int main() {
	test_speed_round_trip();
	test_transitions_in_the_file();
	test_transitions_through_the_model();
	test_through_the_model();
	test_full_round_trip();
	test_older_and_bad_files();
	if (g_failures == 0)
		printf("project_json_test: all ok\n");
	return g_failures == 0 ? 0 : 1;
}
