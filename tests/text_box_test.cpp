// The text boxes laid over the video: how opaque one is through its life, the model's list of them, and where on the
// timeline a box shows.
#include <math.h>
#include <stdio.h>

#include "edit_model.h"
#include "text_box.h"

static int g_failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_failures++; } } while (0)
#define CHECK_NEAR(a, b, tol) do { double va = (a), vb = (b); if (fabs(va - vb) > (tol)) { printf("FAIL line %d: %s = %.9f, wanted %.9f\n", __LINE__, #a, va, vb); g_failures++; } } while (0)

static TextBox timed_box(int64_t fade_in, int64_t dwell, int64_t fade_out) {
	TextBox box = text_box::default_box();
	box.fade_in_ticks = fade_in;
	box.dwell_ticks = dwell;
	box.fade_out_ticks = fade_out;
	return box;
}

static void test_alpha() {
	const TextBox box = timed_box(100, 300, 200);   // 600 ticks in all.
	CHECK(box.total_ticks() == 600);
	CHECK(text_box::alpha_at(box, -1) == 0.0f);
	CHECK(text_box::alpha_at(box, 0) == 0.0f);
	CHECK_NEAR(text_box::alpha_at(box, 50), 0.5, 1e-6);
	CHECK(text_box::alpha_at(box, 100) == 1.0f);
	CHECK(text_box::alpha_at(box, 250) == 1.0f);
	CHECK(text_box::alpha_at(box, 399) == 1.0f);
	CHECK(text_box::alpha_at(box, 400) == 1.0f);   // The fade out begins here, still at full.
	CHECK_NEAR(text_box::alpha_at(box, 500), 0.5, 1e-6);
	CHECK_NEAR(text_box::alpha_at(box, 599), 0.005, 1e-6);
	CHECK(text_box::alpha_at(box, 600) == 0.0f);
	CHECK(text_box::alpha_at(box, 99999) == 0.0f);

	// Fades of no length are cuts.
	const TextBox cut = timed_box(0, 100, 0);
	CHECK(text_box::alpha_at(cut, 0) == 1.0f);
	CHECK(text_box::alpha_at(cut, 99) == 1.0f);
	CHECK(text_box::alpha_at(cut, 100) == 0.0f);
	// Nothing at all.
	CHECK(text_box::alpha_at(timed_box(0, 0, 0), 0) == 0.0f);
	// Only fades: up, then straight down.
	const TextBox fades = timed_box(100, 0, 100);
	CHECK_NEAR(text_box::alpha_at(fades, 50), 0.5, 1e-6);
	CHECK(text_box::alpha_at(fades, 100) == 1.0f);
	CHECK_NEAR(text_box::alpha_at(fades, 150), 0.5, 1e-6);
}

static void test_defaults() {
	const TextBox box = text_box::default_box();
	CHECK(box.text == "Lorem Ipsum");
	CHECK(box.style.color[0] == 1.0f && box.style.color[1] == 1.0f && box.style.color[2] == 1.0f);
	CHECK(box.style.shadow_color[0] == 0.0f && box.style.shadow_opacity == 1.0f);
	CHECK(box.style.align == TEXT_ALIGN_CENTER);
	CHECK(box.w > 0.0f && box.h > 0.0f && box.x >= 0.0f && box.x + box.w <= 1.0f && box.y + box.h <= 1.0f);
	TextBox other = box;
	CHECK(box == other);
	other.style.italic = true;
	CHECK(box != other);
	other = box;
	other.anchor = FileTime(1, 5);
	CHECK(box != other);
}

static EditModel make_model() {
	EditModel model;
	std::vector<std::string> paths;
	paths.push_back("a.mp4");
	paths.push_back("b.mp4");
	std::vector<int64_t> durations;
	durations.push_back(1000);
	durations.push_back(1000);
	model.set_media(paths, durations, 1.0 / 100.0);
	return model;
}

static void test_list() {
	EditModel model = make_model();
	CHECK(model.text_boxes().empty());
	TextBox box = text_box::default_box();
	const int first = model.add_text_box(box);
	const int second = model.add_text_box(box);
	CHECK(first != second);
	CHECK(model.text_boxes().size() == 2);
	CHECK(model.find_text_box(first) != nullptr);
	CHECK(model.find_text_box(first)->id == first);
	CHECK(model.find_text_box(99) == nullptr);
	model.find_text_box(second)->text = "changed";
	CHECK(model.text_boxes()[1].text == "changed");
	CHECK(!model.remove_text_box(99));
	CHECK(model.remove_text_box(first));
	CHECK(model.text_boxes().size() == 1);
	CHECK(model.find_text_box(first) == nullptr);
	// Ids are not reused while the project is open: the next is past every one in use.
	const int third = model.add_text_box(box);
	CHECK(third != second);
	CHECK(model.find_text_box(second) != nullptr);

	// Replacing the list (as undo does) leaves new ids past the largest.
	std::vector<TextBox> boxes;
	box.id = 40;
	boxes.push_back(box);
	model.set_text_boxes(boxes);
	CHECK(model.text_boxes().size() == 1);
	CHECK(model.add_text_box(box) == 41);
	// Setting up the media again starts a project afresh.
	std::vector<std::string> paths(1, "x.mp4");
	std::vector<int64_t> durations(1, 500);
	model.set_media(paths, durations, 0.01);
	CHECK(model.text_boxes().empty());
}

static void test_span() {
	EditModel model = make_model();
	// A box begun on a frame of the second file, 200 ticks in. The files are laid end to end, so that is tick 1200.
	TextBox box = timed_box(50, 100, 50);
	box.anchor = FileTime(1, 200);
	int64_t start = 0;
	int64_t end = 0;
	model.text_box_span(box, &start, &end);
	CHECK(start == 1200);
	CHECK(end == 1400);

	// Cut the first 300 ticks out of the first file: the box's footage moves earlier, and it goes with it.
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 300, 1000));
	clips.push_back(Clip(1, 0, 1000));
	CHECK(model.set_clips(clips));
	model.text_box_span(box, &start, &end);
	CHECK(start == 900);
	CHECK(end == 1100);

	// Swap the clips round: the box follows its footage to the front.
	clips.clear();
	clips.push_back(Clip(1, 0, 1000));
	clips.push_back(Clip(0, 300, 1000));
	CHECK(model.set_clips(clips));
	model.text_box_span(box, &start, &end);
	CHECK(start == 200);
	CHECK(end == 400);

	// Cut the box's own frame out: it moves to the nearest footage that is left.
	clips.clear();
	clips.push_back(Clip(0, 0, 1000));
	clips.push_back(Clip(1, 500, 1000));
	CHECK(model.set_clips(clips));
	model.text_box_span(box, &start, &end);
	CHECK(start == 1000);   // The start of the surviving footage of its file, the nearest to it.
	CHECK(end == 1200);
}

int main() {
	test_alpha();
	test_defaults();
	test_list();
	test_span();
	printf(g_failures ? "FAILED\n" : "all ok\n");
	return g_failures ? 1 : 0;
}
