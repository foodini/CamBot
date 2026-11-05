#pragma once

// Breaking text into lines, for the text overlays. This is the arithmetic only: it is given the width of
// every character (the renderer works those out from the font) and says where each line starts and ends,
// so it can be tested without FreeType or OpenGL.
//
// Text is UTF-8. A line break in it ("\n") always starts a new line; otherwise a line is broken between words
// (at spaces) when the next word wouldn't fit, and a word wider than the whole line is broken wherever it
// has to be. The spaces a break falls on are left out of the line, and so are the spaces at its end (so
// centered and right-aligned text lines up on the letters, not on the invisible ones).
// Characters are single code points; combining marks, right-to-left scripts and emoji sequences get no special treatment.

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace text_layout {

// The code points of UTF-8 `text`. Bytes that aren't valid UTF-8 come out as U+FFFD, and carriage returns are dropped
// (so "\r\n" and "\n" are the same line break).
std::vector<uint32_t> decode_utf8(const std::string& text);

// One line of the text: code points [begin, end) of the whole, which are `width` wide in all.
struct Line {
	size_t begin;
	size_t end;
	float  width;
};

// Breaks `code_points` into lines no wider than `max_width` where it can. `advances` has the width of each code
// point (it may include the kerning to the one after). There is always at least one line (an empty one for empty
// text), and every line has at least one character unless it is a blank line the text asked for.
std::vector<Line> wrap(const std::vector<uint32_t>& code_points, const std::vector<float>& advances, float max_width);

// Where a line of `line_width` starts across a box `box_width` wide for a TextAlign (text_box.h); never left of the box.
float line_offset(int align, float box_width, float line_width);

}  // namespace text_layout
