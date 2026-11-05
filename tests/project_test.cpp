// Tests for the parts of project handling that don't need JSON or FFmpeg: the path helpers, the
// safe file reading and writing, and turning a project into the editing model and back.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "file_io.h"
#include "project_data.h"

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); g_failures++; } } while (0)

static void test_project_data_equality() {
	ProjectData a, b;
	CHECK(a == b);
	b.video_files.push_back("x.mp4");
	CHECK(a != b);
	a.video_files.push_back("x.mp4");
	CHECK(a == b);
	b.rotation_rad = 0.25f;
	CHECK(a != b);
	b.rotation_rad = 0.0f;
	b.altitude_refs.end_same_as_start = true;
	CHECK(a != b);
	b.altitude_refs.end_same_as_start = false;
	b.sync_anchors.push_back(SyncAnchor(FileTime(0, 0), 1745.4856f));
	CHECK(a != b);
	a.sync_anchors.push_back(SyncAnchor(FileTime(0, 0), 1745.4856f));
	CHECK(a == b);
	b.clips.push_back(Clip(0, 0, 100));
	CHECK(a != b);
	b.clips.clear();
	b.launch_mark = FileTime(0, 5);
	CHECK(a != b);
}

static ProjectData make_project(int files) {
	ProjectData project;
	for (int i = 0; i < files; i++)
		project.video_files.push_back("v" + std::to_string(i) + ".mp4");
	return project;
}

// A project that says nothing about editing gets the files whole, in the order listed, and
// writing the model back gives a project that applies to the same model.
static void test_defaults_and_round_trip() {
	std::vector<int64_t> durations;
	durations.push_back(1000);
	durations.push_back(500);
	durations.push_back(2000);
	ProjectData project = make_project(3);
	EditModel model;
	std::string error;
	CHECK(project_io::apply_to_model(project, durations, 1, 30000, &model, &error));
	CHECK(model.edit_list().count() == 3);
	CHECK(model.edit_list().total_ticks() == 3500);
	CHECK(model.offset() == 0.0 && model.anchors().empty());
	CHECK(model.launch_flight_seconds() == 0.0);

	// Make some edits, write them to a project, apply that to a fresh model.
	std::vector<int> order;
	order.push_back(0); order.push_back(2); order.push_back(1);
	CHECK(model.set_capture_order(order));
	std::vector<Clip> clips;
	clips.push_back(Clip(2, 100, 900));
	clips.push_back(Clip(0, 0, 50));
	CHECK(model.set_clips(clips));
	model.set_edit_started(true);
	model.set_offset(-12.5);
	model.set_launch_flight_seconds(model.ticks_to_seconds(1100));
	project_io::fill_from_model(model, 1, 30000, &project);

	EditModel again;
	CHECK(project_io::apply_to_model(project, durations, 1, 30000, &again, &error));
	CHECK(again.capture().order() == model.capture().order());
	CHECK(again.edit_list().clips() == model.edit_list().clips());
	CHECK(again.edit_started());
	CHECK(again.offset() == model.offset());
	CHECK(again.launch_mark() == model.launch_mark());
	// Filling again from the new model gives the same project, so nothing looks unsaved.
	ProjectData project_again = project;
	project_io::fill_from_model(again, 1, 30000, &project_again);
	CHECK(project_again == project);
}

// Version 2 projects had one offset and a launch time in seconds of the flight.
static void test_version2_migration() {
	std::vector<int64_t> durations;
	durations.push_back(30000 * 10);
	durations.push_back(30000 * 20);
	ProjectData project = make_project(2);
	project.has_version2_sync = true;
	project.version2_offset_s = -1745.5f;
	project.version2_launch_s = 12.0f;
	EditModel model;
	std::string error;
	CHECK(project_io::apply_to_model(project, durations, 1, 30000, &model, &error));
	CHECK(fabs(model.offset() - (-1745.5)) < 1e-6);
	CHECK(fabs(model.telemetry_seconds(100.0) - 1845.5) < 1e-3);
	CHECK(fabs(model.launch_flight_seconds() - 12.0) < 1e-4);
	ProjectData written = project;
	project_io::fill_from_model(model, 1, 30000, &written);
	CHECK(!written.has_version2_sync);

	// A version 2 project with no offset has no sync points once normalized.
	ProjectData plain = make_project(2);
	plain.has_version2_sync = true;
	EditModel plain_model;
	CHECK(project_io::apply_to_model(plain, durations, 1, 30000, &plain_model, &error));
	project_io::fill_from_model(plain_model, 1, 30000, &plain);
	CHECK(plain.sync_anchors.empty());
}

static void test_bad_projects_are_refused() {
	std::vector<int64_t> durations;
	durations.push_back(1000);
	durations.push_back(1000);
	EditModel model;
	std::string error;

	ProjectData wrong_count = make_project(3);
	CHECK(!project_io::apply_to_model(wrong_count, durations, 1, 30000, &model, &error));

	ProjectData too_long = make_project(2);
	too_long.time_base_num = 1;
	too_long.time_base_den = 30000;
	too_long.clips.push_back(Clip(1, 0, 1001));
	error.clear();
	CHECK(!project_io::apply_to_model(too_long, durations, 1, 30000, &model, &error));
	CHECK(!error.empty());

	ProjectData bad_order = make_project(2);
	bad_order.capture_order.push_back(0);
	bad_order.capture_order.push_back(0);
	CHECK(!project_io::apply_to_model(bad_order, durations, 1, 30000, &model, &error));
}

// Clips saved with one time base are converted if the first file now has another.
static void test_time_base_change() {
	std::vector<int64_t> durations;
	durations.push_back(90000 * 10);
	ProjectData project = make_project(1);
	project.time_base_num = 1;
	project.time_base_den = 30000;
	project.clips.push_back(Clip(0, 30000, 60000));   // 1 s to 2 s
	EditModel model;
	std::string error;
	CHECK(project_io::apply_to_model(project, durations, 1, 90000, &model, &error));
	CHECK(model.edit_list().clip(0).in_ticks == 90000);
	CHECK(model.edit_list().clip(0).out_ticks == 180000);
}

// Text boxes ride along with the rest of the editing state, and their times follow a change of time base.
static void test_text_boxes_round_trip() {
	std::vector<int64_t> durations;
	durations.push_back(30000 * 20);
	durations.push_back(30000 * 10);
	ProjectData project = make_project(2);
	project.time_base_num = 1;
	project.time_base_den = 30000;
	TextBox box = text_box::default_box();
	box.id = 7;
	box.anchor = FileTime(1, 30000 * 3);
	box.fade_in_ticks = 15000;
	box.dwell_ticks = 30000 * 4;
	box.fade_out_ticks = 15000;
	box.text = "Fort Funston\nsecond line";
	box.style.bold = true;
	box.style.align = TEXT_ALIGN_RIGHT;
	project.text_boxes.push_back(box);
	ProjectData other = project;
	CHECK(project == other);
	other.text_boxes[0].style.shadow_opacity = 0.5f;
	CHECK(project != other);

	EditModel model;
	std::string error;
	CHECK(project_io::apply_to_model(project, durations, 1, 30000, &model, &error));
	CHECK(model.text_boxes().size() == 1);
	CHECK(model.text_boxes()[0] == box);
	// The next one added gets an id nobody has.
	const int added = model.add_text_box(text_box::default_box());
	CHECK(added == 8);
	CHECK(model.remove_text_box(added));
	ProjectData written;
	project_io::fill_from_model(model, 1, 30000, &written);
	CHECK(written.text_boxes == project.text_boxes);

	// In a project saved with another time base, the anchor and the durations are converted.
	EditModel converted;
	CHECK(project_io::apply_to_model(project, durations, 1, 90000, &converted, &error));
	CHECK(converted.text_boxes().size() == 1);
	CHECK(converted.text_boxes()[0].anchor == FileTime(1, 90000 * 3));
	CHECK(converted.text_boxes()[0].fade_in_ticks == 45000);
	CHECK(converted.text_boxes()[0].dwell_ticks == 90000 * 4);
	CHECK(converted.text_boxes()[0].text == box.text);

	// A box on a file that isn't there is left out rather than refused.
	ProjectData stray = project;
	stray.text_boxes[0].anchor.media = 5;
	EditModel without;
	CHECK(project_io::apply_to_model(stray, durations, 1, 30000, &without, &error));
	CHECK(without.text_boxes().empty());
}

// Whether the user has marked launch survives saving and loading; until they have, nothing is written for it.
static void test_launch_marked_round_trip() {
	std::vector<int64_t> durations;
	durations.push_back(30000 * 20);
	durations.push_back(30000 * 10);
	ProjectData project = make_project(2);
	project.time_base_num = 1;
	project.time_base_den = 30000;
	std::string error;

	// Nothing marked: launch stands at the start of the flight, but is not counted as chosen.
	EditModel unmarked;
	CHECK(project_io::apply_to_model(project, durations, 1, 30000, &unmarked, &error));
	CHECK(!unmarked.launch_marked());
	CHECK(unmarked.launch_mark() == FileTime(0, 0));
	ProjectData written;
	project_io::fill_from_model(unmarked, 1, 30000, &written);
	CHECK(written.launch_mark.media < 0);

	// Marking it makes it stick.
	unmarked.set_launch_flight_seconds(0.5);
	CHECK(unmarked.launch_marked());
	project_io::fill_from_model(unmarked, 1, 30000, &written);
	CHECK(written.launch_mark.media == 0);
	written.video_files = project.video_files;   // (The file names are the caller's to fill in.)
	EditModel reloaded;
	CHECK(project_io::apply_to_model(written, durations, 1, 30000, &reloaded, &error));
	CHECK(reloaded.launch_marked());
	CHECK(reloaded.launch_mark() == unmarked.launch_mark());

	// And a new flight starts out unmarked again.
	std::vector<std::string> paths;
	paths.push_back("a.mp4");
	paths.push_back("b.mp4");
	reloaded.set_media(paths, durations, 1.0 / 30000.0);
	CHECK(!reloaded.launch_marked());
}

static void test_paths() {
	CHECK(fileio::base_name("C:\\a\\b\\c.mp4") == "c.mp4");
	CHECK(fileio::base_name("/a/b/c.mp4") == "c.mp4");
	CHECK(fileio::base_name("c.mp4") == "c.mp4");
	CHECK(fileio::directory_of("C:\\a\\b\\c.mp4") == "C:\\a\\b");
	CHECK(fileio::directory_of("c.mp4") == "");
	CHECK(fileio::join("C:\\a", "x.mp4") == "C:\\a\\x.mp4");
	CHECK(fileio::join("/a", "x.mp4") == "/a/x.mp4");
	CHECK(fileio::join("/a/", "x.mp4") == "/a/x.mp4");
	CHECK(fileio::join("", "x.mp4") == "x.mp4");
	CHECK(fileio::extension_of("A/Flight.MP4") == "mp4");
	CHECK(fileio::extension_of("noext") == "");
	CHECK(fileio::extension_of("dir.d/noext") == "");
}

static void test_file_io(const char* dir) {
	std::string path = fileio::join(dir, "cambot_fileio_test.txt");
	std::string error;
	remove(path.c_str());
	CHECK(!fileio::file_exists(path));
	CHECK(fileio::write_text_atomic(path, "first\r\nversion", &error));
	std::string text;
	CHECK(fileio::read_text(path, &text, &error));
	CHECK(text == "first\r\nversion");
	// Replacing keeps going and leaves no temp file behind.
	CHECK(fileio::write_text_atomic(path, "second", &error));
	CHECK(fileio::read_text(path, &text, &error));
	CHECK(text == "second");
	CHECK(!fileio::file_exists(path + ".tmp"));
	// Big file (crosses the read buffer size several times).
	std::string big(100000, 'x');
	CHECK(fileio::write_text_atomic(path, big, &error));
	CHECK(fileio::read_text(path, &text, &error));
	CHECK(text == big);
	// Copy.
	std::string copy = path + ".bak";
	CHECK(fileio::copy_file(path, copy, &error));
	CHECK(fileio::read_text(copy, &text, &error) && text == big);
	// Failures report an error and don't throw.
	CHECK(!fileio::read_text(fileio::join(dir, "no_such_file.txt"), &text, &error));
	CHECK(!error.empty());
	CHECK(!fileio::write_text_atomic(fileio::join(dir, "no_such_dir/x.txt"), "x", &error));
	CHECK(!error.empty());
	// Modification times: later writes are later; missing files are 0; delete_file() really deletes.
	CHECK(fileio::modified_time(path) > 0);
	CHECK(fileio::modified_time(fileio::join(dir, "no_such_file.txt")) == 0);
	CHECK(fileio::delete_file(copy));
	CHECK(!fileio::file_exists(copy));
	CHECK(fileio::delete_file(copy));   // Already gone: still fine.
	remove(path.c_str());
}

int main(int argc, char** argv) {
	const char* dir = argc > 1 ? argv[1] : ".";
	test_project_data_equality();
	test_defaults_and_round_trip();
	test_version2_migration();
	test_bad_projects_are_refused();
	test_time_base_change();
	test_text_boxes_round_trip();
	test_launch_marked_round_trip();
	test_paths();
	test_file_io(dir);
	if (g_failures == 0)
		printf("project_test: all ok\n");
	return g_failures == 0 ? 0 : 1;
}
