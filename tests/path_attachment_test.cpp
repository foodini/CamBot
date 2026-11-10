// Tests of path_attachment: when a text box attached to a path shows, and where it is and what is drawn with it.

#include "path_attachment.h"
#include <math.h>
#include <stdio.h>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); g_failures++; } } while (0)
static bool near(double a, double b) { return fabs(a - b) < 1e-6; }

static path_spans::Piece piece(int64_t start, int64_t end, int64_t from, int64_t to) {
	path_spans::Piece result;
	result.span = text_lanes::Span(start, end);
	result.from = from;
	result.to = to;
	return result;
}

static void test_alpha() {
	std::vector<path_spans::Piece> pieces;
	pieces.push_back(piece(100, 500, 100, 500));
	// Nothing outside the piece: the fade in begins where it does and the fade out ends where it does.
	CHECK(path_attachment::alpha_at(pieces, 50, 100, 99) == 0.0f);
	CHECK(path_attachment::alpha_at(pieces, 50, 100, 100) == 0.0f);
	CHECK(near(path_attachment::alpha_at(pieces, 50, 100, 125), 0.5));
	CHECK(path_attachment::alpha_at(pieces, 50, 100, 150) == 1.0f);
	CHECK(path_attachment::alpha_at(pieces, 50, 100, 300) == 1.0f);
	CHECK(path_attachment::alpha_at(pieces, 50, 100, 400) == 1.0f);
	CHECK(near(path_attachment::alpha_at(pieces, 50, 100, 450), 0.5));
	CHECK(path_attachment::alpha_at(pieces, 50, 100, 500) == 0.0f);
	CHECK(path_attachment::alpha_at(pieces, 50, 100, 501) == 0.0f);
	// Fades of no length are cuts, and the piece's last tick is shown.
	CHECK(path_attachment::alpha_at(pieces, 0, 0, 99) == 0.0f);
	CHECK(path_attachment::alpha_at(pieces, 0, 0, 100) == 1.0f);
	CHECK(path_attachment::alpha_at(pieces, 0, 0, 500) == 1.0f);
	CHECK(path_attachment::alpha_at(pieces, 0, 0, 501) == 0.0f);
	// A piece too short for both fades has them cut down in proportion: 60 long, so 20 and 40 of the 50 and 100.
	std::vector<path_spans::Piece> brief;
	brief.push_back(piece(0, 60, 0, 60));
	CHECK(near(path_attachment::alpha_at(brief, 50, 100, 10), 0.5));
	CHECK(near(path_attachment::alpha_at(brief, 50, 100, 20), 1.0));
	CHECK(near(path_attachment::alpha_at(brief, 50, 100, 40), 0.5));
	// Two pieces: each has its own fades.
	pieces.push_back(piece(520, 800, 900, 1180));
	CHECK(path_attachment::alpha_at(pieces, 50, 100, 510) == 0.0f);
	CHECK(near(path_attachment::alpha_at(pieces, 50, 100, 545), 0.5));
	CHECK(path_attachment::alpha_at(pieces, 50, 100, 600) == 1.0f);
	// No pieces, no box.
	CHECK(path_attachment::alpha_at(std::vector<path_spans::Piece>(), 50, 100, 300) == 0.0f);

	const std::vector<text_lanes::Span> spans = path_attachment::shown_spans(pieces);
	CHECK(spans.size() == 2 && spans[0].start == 100 && spans[0].end == 500 && spans[1].start == 520 && spans[1].end == 800);
}

static EditModel model_with_one_clip(int* path_id) {
	EditModel model;
	std::vector<std::string> names;
	names.push_back("A.mp4");
	std::vector<int64_t> durations;
	durations.push_back(2000);
	model.set_media(names, durations, 0.001);
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 100, 1100));        // Timeline 0 .. 1000.
	model.set_clips(clips);
	TrackedPath path;
	path.media = 0;
	path.curve.set_orientation_aware(false);
	path.curve.set_point(300, 0.2, 0.5);        // On the timeline at 200 ...
	path.curve.set_point(700, 0.6, 0.5);        // ... to 600.
	*path_id = model.add_path(path);
	return model;
}

static void test_footage_ticks() {
	int id = 0;
	EditModel model = model_with_one_clip(&id);
	std::vector<path_spans::Piece> pieces = path_spans::footage_pieces(model.edit_list(), 0, 300, 700);
	CHECK(pieces.size() == 1 && pieces[0].span.start == 200 && pieces[0].span.end == 600 && pieces[0].from == 300 && pieces[0].to == 700);
	int64_t ticks = -1;
	CHECK(path_attachment::footage_ticks_at(model.edit_list(), 0, pieces, 400, &ticks) && ticks == 500);
	CHECK(path_attachment::footage_ticks_at(model.edit_list(), 0, pieces, 200, &ticks) && ticks == 300);
	CHECK(path_attachment::footage_ticks_at(model.edit_list(), 0, pieces, 600, &ticks) && ticks == 700);
	// Not outside it.
	CHECK(!path_attachment::footage_ticks_at(model.edit_list(), 0, pieces, 199, &ticks));
	CHECK(!path_attachment::footage_ticks_at(model.edit_list(), 0, pieces, 601, &ticks));
}

static void test_leader_line() {
	double fx = 0.0, fy = 0.0, tx = 0.0, ty = 0.0;
	// A canvas twice as wide as it is high. The box's right edge is at 0.3 of the width, 0.6 of the height; the circle's centre is at 0.8
	// of the width, 1.6 of the height, so 1.0 of the height away, and the line ends 0.1 of the height short of it.
	CHECK(path_attachment::leader_line(0.1f, 0.4f, 0.2f, 0.2f, 0.8, 0.5, 0.1, 2.0, &fx, &fy, &tx, &ty));
	CHECK(near(fx, 0.3) && near(fy, 0.5) && near(tx, 0.75) && near(ty, 0.5));
	// Past a corner it leaves the corner, along the line to the centre.
	CHECK(path_attachment::leader_line(0.1f, 0.4f, 0.2f, 0.2f, 0.3, 0.9, 0.1, 1.0, &fx, &fy, &tx, &ty));
	CHECK(near(fx, 0.3) && near(fy, 0.6) && near(tx, 0.3) && near(ty, 0.8));
	CHECK(path_attachment::leader_line(0.1f, 0.4f, 0.2f, 0.2f, 0.6, 0.9, 0.0, 1.0, &fx, &fy, &tx, &ty));
	CHECK(near(fx, 0.3) && near(fy, 0.6) && near(tx, 0.6) && near(ty, 0.9));
	// Level with the box, it leaves the side it faces and stays level.
	CHECK(path_attachment::leader_line(0.1f, 0.4f, 0.2f, 0.2f, 0.1, 0.1, 0.05, 1.0, &fx, &fy, &tx, &ty));
	CHECK(near(fx, 0.1) && near(fy, 0.4) && near(tx, 0.1) && near(ty, 0.15));
	// No line when the centre is in the box, or the box is as near as the line is to stop.
	CHECK(!path_attachment::leader_line(0.1f, 0.4f, 0.2f, 0.2f, 0.2, 0.5, 0.05, 1.0, &fx, &fy, &tx, &ty));
	CHECK(!path_attachment::leader_line(0.1f, 0.4f, 0.2f, 0.2f, 0.34, 0.5, 0.05, 1.0, &fx, &fy, &tx, &ty));
}

static void test_place() {
	int id = 0;
	EditModel model = model_with_one_clip(&id);
	tracking::CameraLens lens(1.8, 1.0);
	tracking::CameraOrientations none;
	TextBox box = text_box::default_box();
	box.id = 1;
	box.path_id = id;
	box.fade_in_ticks = 100;
	box.fade_out_ticks = 100;
	box.offset_x = 0.05f;
	box.offset_y = -0.1f;
	box.x = 0.7f;
	box.y = 0.7f;
	box.w = 0.2f;
	box.h = 0.1f;

	// Following the path: the marker (halfway along it at 400 on the timeline: u 0.4) and the offset.
	box.attach_mode = ATTACH_FOLLOW;
	path_attachment::Placement at = path_attachment::place(model, box, lens, none, 0.0, 1.0, 400);
	CHECK(at.alpha == 1.0f && near(at.marker_x, 0.4) && near(at.marker_y, 0.5));
	CHECK(near(at.x, 0.45) && near(at.y, 0.4) && near(at.w, 0.2) && near(at.h, 0.1) && !at.has_leader_line && near(at.circle_radius, 0.0));
	// Fading in from the path's first point (at 200 on the timeline) and out to its last (600), the marker moving all the while.
	at = path_attachment::place(model, box, lens, none, 0.0, 1.0, 250);
	CHECK(near(at.alpha, 0.5) && near(at.marker_x, 0.25) && near(at.x, 0.30));
	at = path_attachment::place(model, box, lens, none, 0.0, 1.0, 550);
	CHECK(near(at.alpha, 0.5) && near(at.marker_x, 0.55));
	// Nowhere before or after.
	CHECK(path_attachment::place(model, box, lens, none, 0.0, 1.0, 190).alpha == 0.0f);
	CHECK(path_attachment::place(model, box, lens, none, 0.0, 1.0, 601).alpha == 0.0f);

	// With a leader line, the box stays and the line runs to the circle's perimeter.
	box.attach_mode = ATTACH_LEADER;
	model.find_path(id)->circle.enabled = true;
	model.find_path(id)->circle.radius = 0.05f;
	at = path_attachment::place(model, box, lens, none, 0.0, 1.0, 400);
	CHECK(near(at.x, 0.7) && near(at.y, 0.7) && near(at.circle_radius, 0.05) && at.has_leader_line);
	CHECK(near(hypot(at.line[2] - at.marker_x, at.line[3] - at.marker_y), 0.05));
	CHECK(near(at.line[0], 0.7) && near(at.line[1], 0.7));
	// Where the path has no circle it stops a gap short of the marker.
	model.find_path(id)->circle.enabled = false;
	at = path_attachment::place(model, box, lens, none, 0.0, 1.0, 400);
	CHECK(at.has_leader_line && near(at.circle_radius, 0.0) && near(hypot(at.line[2] - at.marker_x, at.line[3] - at.marker_y), path_attachment::LEADER_GAP));
	// The circle is a fraction of the height, so a turn of the picture (which zooms it) scales it.
	model.find_path(id)->circle.enabled = true;
	at = path_attachment::place(model, box, lens, none, 3.14159265358979 / 2.0, 2.0, 400);
	CHECK(near(at.circle_radius, 0.05 * 2.0));

	// The span of the box and its alpha, through the model.
	int64_t start = 0, end = 0;
	path_attachment::text_box_span(model, box, &start, &end);
	CHECK(start == 200 && end == 600);
	CHECK(path_attachment::text_box_alpha(model, box, 400) == 1.0f && path_attachment::text_box_alpha(model, box, 150) == 0.0f);
	// A box with no path keeps its own timing.
	TextBox plain = text_box::default_box();
	plain.anchor = FileTime(0, 300);
	plain.fade_in_ticks = 100;
	plain.dwell_ticks = 200;
	plain.fade_out_ticks = 100;
	path_attachment::text_box_span(model, plain, &start, &end);
	CHECK(start == 100 && end == 500);
	CHECK(path_attachment::text_box_spans(model, plain).size() == 1);
	// One whose path is not there shows nowhere.
	box.path_id = id + 7;
	CHECK(path_attachment::text_box_alpha(model, box, 400) == 0.0f && path_attachment::text_box_spans(model, box).empty());
	CHECK(path_attachment::place(model, box, lens, none, 0.0, 1.0, 400).alpha == 0.0f);
}

int main() {
	test_alpha();
	test_footage_ticks();
	test_leader_line();
	test_place();
	if (g_failures == 0)
		printf("all ok\n");
	return g_failures == 0 ? 0 : 1;
}
