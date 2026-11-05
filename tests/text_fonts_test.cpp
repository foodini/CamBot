// The table of fonts for the text overlays (text_fonts.h).
#include <stdio.h>
#include <string.h>

#include "text_fonts.h"

static int g_failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_failures++; } } while (0)

int main() {
	CHECK(text_fonts::font_count() >= 2);
	CHECK(strcmp(text_fonts::font_name(0), "Arial") == 0);

	// Every font has a name and some file for every weight and slant.
	for (int font = 0; font < text_fonts::font_count(); font++) {
		CHECK(strlen(text_fonts::font_name(font)) > 0);
		for (int style = 0; style < 4; style++) {
			const text_fonts::Face face = text_fonts::choose_face(font, (style & 1) != 0, (style & 2) != 0);
			CHECK(!face.file.empty());
			CHECK(face.file.find(".ttf") != std::string::npos);
		}
		// The regular face is never faked.
		const text_fonts::Face regular = text_fonts::choose_face(font, false, false);
		CHECK(!regular.synth_bold && !regular.synth_italic);
	}

	// Arial has all four faces, so nothing is faked.
	text_fonts::Face face = text_fonts::choose_face(0, true, true);
	CHECK(face.file == "arialbi.ttf" && !face.synth_bold && !face.synth_italic);
	face = text_fonts::choose_face(0, true, false);
	CHECK(face.file == "arialbd.ttf" && !face.synth_bold);
	face = text_fonts::choose_face(0, false, true);
	CHECK(face.file == "ariali.ttf" && !face.synth_italic);

	// Impact has only the one, so bold and slant are faked.
	int impact = -1;
	for (int font = 0; font < text_fonts::font_count(); font++) {
		if (strcmp(text_fonts::font_name(font), "Impact") == 0)
			impact = font;
	}
	CHECK(impact >= 0);
	if (impact >= 0) {
		face = text_fonts::choose_face(impact, true, false);
		CHECK(face.file == "impact.ttf" && face.synth_bold && !face.synth_italic);
		face = text_fonts::choose_face(impact, false, true);
		CHECK(face.file == "impact.ttf" && !face.synth_bold && face.synth_italic);
		face = text_fonts::choose_face(impact, true, true);
		CHECK(face.file == "impact.ttf" && face.synth_bold && face.synth_italic);
	}

	// Out of range is font 0.
	CHECK(text_fonts::choose_face(-1, false, false).file == "arial.ttf");
	CHECK(text_fonts::choose_face(999, false, false).file == "arial.ttf");
	CHECK(strcmp(text_fonts::font_name(999), "Arial") == 0);
	CHECK(text_fonts::fallback_face().file == "arial.ttf");

	if (g_failures == 0)
		printf("all ok\n");
	return g_failures == 0 ? 0 : 1;
}
