#pragma once

// The headers of the sections of the left panel (Source Material, Speed, Transition, Text, Camera Data), drawn the same way
// everywhere: bigger than the text under them, on a colour of their own, with what is under each indented a little so it is clear
// which section it belongs to.
//
// The window draws everything in it between indent() calls (Indent before the first section, Unindent after the last), and every
// section starts with header(), which steps back out to the window's edge for the header itself and in again after it. So a section
// can leave early, whether its header is open or not, and the indent stays right.

namespace panel_section {

const float HEADER_TEXT_SCALE = 1.5f;       // The header's text, compared with the rest.
const float INDENT_FRACTION   = 0.04f;      // How far in what is under a header goes, as part of the window's width.

// How far in, in pixels, for the window being drawn.
float indent_width();

// Draws a header across the window; true if it is open (and the section's contents are to be drawn).
bool header(const char* label);

}  // namespace panel_section
