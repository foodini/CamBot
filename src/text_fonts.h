#pragma once

// The fonts the text overlays can use. They are the ones Windows comes with (so there is nothing to install
// or ship), each as the files of its regular, bold, italic and bold italic faces. A face the font doesn't have
// is made from its regular one by thickening or slanting it (the renderer does that when it is told to).

#include <string>

namespace text_fonts {

// How many fonts there are; they are numbered 0 to font_count() - 1, and 0 is the default.
int font_count();

// The name to show the user for font `font`.
const char* font_name(int font);

// Which file draws font `font` in the given weight and slant, and what is still to be faked on top of it.
struct Face {
	std::string file;           // The file's name, without a directory ("arialbd.ttf").
	bool        synth_bold;     // The file isn't bold but the text should be.
	bool        synth_italic;   // The file isn't italic but the text should be.
};
// A `font` out of range is taken to be font 0.
Face choose_face(int font, bool bold, bool italic);

// The file that serves when a font's own can't be loaded (font 0's regular face).
Face fallback_face();

}  // namespace text_fonts
