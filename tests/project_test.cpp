// Tests for the parts of project handling that don't need JSON or FFmpeg: the path helpers and
// the safe file reading and writing.
#include <assert.h>
#include <stdio.h>
#include <string.h>

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
	b.telemetry_offset_s = -1745.4856f;
	CHECK(a != b);
	a.telemetry_offset_s = -1745.4856f;
	CHECK(a == b);
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
	test_paths();
	test_file_io(dir);
	if (g_failures == 0)
		printf("project_test: all ok\n");
	return g_failures == 0 ? 0 : 1;
}
