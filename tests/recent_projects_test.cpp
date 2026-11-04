#include <stdio.h>
#include <stdlib.h>
#include <string>

#include "user_settings.h"

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); g_failures++; } } while (0)

static void test_list() {
	RecentProjects recent;
	CHECK(recent.count() == 0);
	recent.note_opened("C:\\flights\\a.ffsw");
	recent.note_opened("C:\\flights\\b.ffsw");
	recent.note_opened("C:\\flights\\c.ffsw");
	CHECK(recent.count() == 3);
	CHECK(recent.path(0) == "C:\\flights\\c.ffsw" && recent.path(2) == "C:\\flights\\a.ffsw");

	// Opening one again moves it to the top, without a second copy, whatever the spelling of case and slashes.
	recent.note_opened("c:/flights/A.FFSW");
	CHECK(recent.count() == 3);
	CHECK(recent.path(0) == "c:/flights/A.FFSW");
	CHECK(recent.path(1) == "C:\\flights\\c.ffsw" && recent.path(2) == "C:\\flights\\b.ffsw");

	recent.remove("C:\\FLIGHTS\\C.ffsw");
	CHECK(recent.count() == 2 && recent.path(1) == "C:\\flights\\b.ffsw");
	recent.note_opened("");
	CHECK(recent.count() == 2);

	// Only so many are kept: the oldest go.
	RecentProjects many;
	for (int i = 0; i < RecentProjects::MAX_ENTRIES + 5; i++)
		many.note_opened("p" + std::to_string(i) + ".ffsw");
	CHECK(many.count() == RecentProjects::MAX_ENTRIES);
	CHECK(many.path(0) == "p" + std::to_string(RecentProjects::MAX_ENTRIES + 4) + ".ffsw");
	CHECK(many.path(RecentProjects::MAX_ENTRIES - 1) == "p5.ffsw");

	// Reading back in order.
	RecentProjects read;
	read.append("x.ffsw");
	read.append("y.ffsw");
	read.append("X.ffsw");
	CHECK(read.count() == 2 && read.path(0) == "x.ffsw" && read.path(1) == "y.ffsw");
}

static void test_settings_round_trip(const char* directory) {
	// The settings file goes in a folder of its own for this test.
	static std::string setting;   // putenv() keeps the pointer, so this must outlive the function.
	setting = std::string("APPDATA=") + directory;
	putenv(const_cast<char*>(setting.c_str()));

	UserSettings first;
	CHECK(first.last_project.empty() && first.recent_projects.count() == 0);
	first.note_project_opened("C:\\My Flights\\Torrey: 2026\\flight one.ffsw");
	first.note_project_opened("D:\\videos\\second.ffsw");
	CHECK(first.save());

	UserSettings second;
	CHECK(second.last_project == "D:\\videos\\second.ffsw");
	CHECK(second.recent_projects.count() == 2);
	CHECK(second.recent_projects.path(0) == "D:\\videos\\second.ffsw");
	CHECK(second.recent_projects.path(1) == "C:\\My Flights\\Torrey: 2026\\flight one.ffsw");

	// Closing a project on purpose forgets the one to reopen, but keeps the list.
	second.forget_last_project();
	CHECK(second.save());
	UserSettings third;
	CHECK(third.last_project.empty());
	CHECK(third.recent_projects.count() == 2);
	// And the other settings are not disturbed.
	CHECK(third.window.timeline_height == UserSettings().window.timeline_height);
}

int main(int argc, char** argv) {
	test_list();
	if (argc > 1)
		test_settings_round_trip(argv[1]);
	printf(g_failures ? "FAILED\n" : "all ok\n");
	return g_failures;
}
