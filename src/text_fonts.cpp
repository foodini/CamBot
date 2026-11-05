#include "text_fonts.h"

namespace text_fonts {

namespace {

struct Entry {
	const char* name;
	const char* regular;    // File names; null where the font has no such face.
	const char* bold;
	const char* italic;
	const char* bold_italic;
};

const Entry FONTS[] = {
	{ "Arial",           "arial.ttf",    "arialbd.ttf",    "ariali.ttf",    "arialbi.ttf" },
	{ "Segoe UI",        "segoeui.ttf",  "segoeuib.ttf",   "segoeuii.ttf",  "segoeuiz.ttf" },
	{ "Verdana",         "verdana.ttf",  "verdanab.ttf",   "verdanai.ttf",  "verdanaz.ttf" },
	{ "Times New Roman", "times.ttf",    "timesbd.ttf",    "timesi.ttf",    "timesbi.ttf" },
	{ "Georgia",         "georgia.ttf",  "georgiab.ttf",   "georgiai.ttf",  "georgiaz.ttf" },
	{ "Courier New",     "cour.ttf",     "courbd.ttf",     "couri.ttf",     "courbi.ttf" },
	{ "Impact",          "impact.ttf",   NULL,             NULL,            NULL },
};

const int FONT_COUNT = (int)(sizeof(FONTS) / sizeof(FONTS[0]));

}  // namespace

int font_count() {
	return FONT_COUNT;
}

const char* font_name(int font) {
	if (font < 0 || font >= FONT_COUNT)
		font = 0;
	return FONTS[font].name;
}

Face choose_face(int font, bool bold, bool italic) {
	if (font < 0 || font >= FONT_COUNT)
		font = 0;
	const Entry& entry = FONTS[font];
	Face face;
	face.synth_bold = false;
	face.synth_italic = false;
	const char* wanted = bold ? (italic ? entry.bold_italic : entry.bold) : (italic ? entry.italic : entry.regular);
	if (wanted != NULL) {
		face.file = wanted;
		return face;
	}
	// Not there: take the nearest face the font has and fake the rest. Slanting a bold face is the better of the two ways
	// round, so look for bold first.
	if (bold && italic) {
		if (entry.bold != NULL) {
			face.file = entry.bold;
			face.synth_italic = true;
		} else if (entry.italic != NULL) {
			face.file = entry.italic;
			face.synth_bold = true;
		} else {
			face.file = entry.regular;
			face.synth_bold = true;
			face.synth_italic = true;
		}
	} else if (bold) {
		face.file = entry.regular;
		face.synth_bold = true;
	} else {
		face.file = entry.regular;
		face.synth_italic = true;
	}
	return face;
}

Face fallback_face() {
	return choose_face(0, false, false);
}

}  // namespace text_fonts
