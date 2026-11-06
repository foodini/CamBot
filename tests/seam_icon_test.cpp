// Where the transition icons go on the timeline (seam_icon.h).
#include <math.h>
#include <stdio.h>

#include "seam_icon.h"

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); g_failures++; } } while (0)
#define CHECK_NEAR(a, b) do { const float va = (a); const float vb = (b); if (fabsf(va - vb) > 1e-3f) { printf("FAIL %s:%d: %s = %f, wanted %f\n", __FILE__, __LINE__, #a, va, vb); g_failures++; } } while (0)

static void test_a_cut_between_wide_clips() {
	// A plain cut at x = 500, between clips hundreds of pixels wide: the icon is centered on it, side wide.
	const seam_icon::Span span = seam_icon::for_join(500.0f, 500.0f, 400.0f, 400.0f, 20.0f);
	CHECK(span.visible);
	CHECK_NEAR(span.left, 490.0f);
	CHECK_NEAR(span.right, 510.0f);
}

static void test_a_long_transition_stretches_the_icon() {
	const seam_icon::Span span = seam_icon::for_join(480.0f, 540.0f, 400.0f, 400.0f, 20.0f);
	CHECK(span.visible);
	CHECK_NEAR(span.left, 480.0f);
	CHECK_NEAR(span.right, 540.0f);
	// One just shorter than the icon is not stretched: it is the icon, centered on the zone.
	const seam_icon::Span near = seam_icon::for_join(500.0f, 510.0f, 400.0f, 400.0f, 20.0f);
	CHECK_NEAR(near.left, 495.0f);
	CHECK_NEAR(near.right, 515.0f);
	// One as long as the icon is exactly the zone.
	const seam_icon::Span exact = seam_icon::for_join(500.0f, 520.0f, 400.0f, 400.0f, 20.0f);
	CHECK_NEAR(exact.left, 500.0f);
	CHECK_NEAR(exact.right, 520.0f);
}

static void test_a_short_clip_crushes_the_icon() {
	// The clip before is 10 pixels wide: the icon may take 4.5 of them. The clip after is wide.
	const seam_icon::Span span = seam_icon::for_join(500.0f, 500.0f, 10.0f, 400.0f, 20.0f);
	CHECK(span.visible);
	CHECK_NEAR(span.left, 495.5f);
	CHECK_NEAR(span.right, 510.0f);
	// Both short.
	const seam_icon::Span both = seam_icon::for_join(500.0f, 500.0f, 10.0f, 10.0f, 20.0f);
	CHECK_NEAR(both.left, 495.5f);
	CHECK_NEAR(both.right, 504.5f);
	CHECK(both.visible);
	// Clips a couple of pixels wide: the icon is gone.
	const seam_icon::Span tiny = seam_icon::for_join(500.0f, 500.0f, 2.0f, 2.0f, 20.0f);
	CHECK(!tiny.visible);
	// A clip with no width at all.
	CHECK(!seam_icon::for_join(500.0f, 500.0f, 0.0f, 0.0f, 20.0f).visible);
}

static void test_the_two_ends() {
	// The start: against the left edge of the first clip, inside it.
	const seam_icon::Span start = seam_icon::for_start(100.0f, 100.0f, 300.0f, 20.0f);
	CHECK(start.visible);
	CHECK_NEAR(start.left, 100.0f);
	CHECK_NEAR(start.right, 120.0f);
	// A first clip too narrow to hold it all.
	const seam_icon::Span narrow = seam_icon::for_start(100.0f, 100.0f, 20.0f, 20.0f);
	CHECK_NEAR(narrow.left, 100.0f);
	CHECK_NEAR(narrow.right, 109.0f);
	CHECK(!seam_icon::for_start(100.0f, 100.0f, 5.0f, 20.0f).visible);
	// A fade in longer than the icon covers the whole of it.
	const seam_icon::Span fade = seam_icon::for_start(100.0f, 160.0f, 300.0f, 20.0f);
	CHECK_NEAR(fade.left, 100.0f);
	CHECK_NEAR(fade.right, 160.0f);
	// The end is the mirror of that.
	const seam_icon::Span end = seam_icon::for_end(700.0f, 700.0f, 300.0f, 20.0f);
	CHECK(end.visible);
	CHECK_NEAR(end.left, 680.0f);
	CHECK_NEAR(end.right, 700.0f);
	const seam_icon::Span end_narrow = seam_icon::for_end(700.0f, 700.0f, 20.0f, 20.0f);
	CHECK_NEAR(end_narrow.left, 691.0f);
	CHECK_NEAR(end_narrow.right, 700.0f);
	const seam_icon::Span fade_out = seam_icon::for_end(640.0f, 700.0f, 300.0f, 20.0f);
	CHECK_NEAR(fade_out.left, 640.0f);
	CHECK_NEAR(fade_out.right, 700.0f);
}

int main() {
	test_a_cut_between_wide_clips();
	test_a_long_transition_stretches_the_icon();
	test_a_short_clip_crushes_the_icon();
	test_the_two_ends();
	if (g_failures == 0)
		printf("seam_icon_test: all ok\n");
	return g_failures == 0 ? 0 : 1;
}
