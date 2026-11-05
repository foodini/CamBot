#pragma once

// A piece of text laid over the video: what it says, where it sits in the picture, how it looks, and
// when it shows. Plain data with no FreeType or OpenGL in it, so it can be tested (drawing it is
// text_renderer.h's job).
//
// A text box belongs to a frame of the footage: `anchor` is the moment of a media file it begins on, so cutting
// or moving the clip it is on carries it along. How long it shows is its own: it fades in, holds (the
// "dwell"), and fades out, one after the other, in ticks of the edit timeline (see edit_model.h; the
// project file keeps them as ticks, with the time base).
//
// The box's place and size are fractions of the frame (x and w of its width, y and h of its height, with
// y counted down from the top), and the type's size and the shadow's offset are fractions of its height,
// so the same text lands in the same place at any picture size. The bottom of the box is a fiction: text
// that doesn't fit just carries on below it.

#include <stdint.h>
#include <string>

#include "file_time.h"

enum TextAlign { TEXT_ALIGN_LEFT = 0, TEXT_ALIGN_CENTER = 1, TEXT_ALIGN_RIGHT = 2 };

struct TextStyle {
	int   font;             // Which of the fonts of text_fonts.h.
	float size;             // The em height of the type, as a fraction of the frame's height (0.0444 is 48 points at 1080 tall).
	bool  bold;
	bool  italic;
	bool  strike;
	int   align;            // A TextAlign.
	float color[3];         // Red, green, blue, 0 to 1.
	float shadow_dx;        // The shadow's place relative to the text, as fractions of the frame's height (down and right are +).
	float shadow_dy;
	float shadow_color[3];
	float shadow_opacity;   // 0 for no shadow.

	TextStyle();
	bool operator==(const TextStyle& other) const;
	bool operator!=(const TextStyle& other) const { return !(*this == other); }
};

struct TextBox {
	int         id;              // Names it while the project is open (and in the project file); unique among the project's boxes.
	FileTime    anchor;          // The frame of the footage it begins on.
	int64_t     fade_in_ticks;
	int64_t     dwell_ticks;
	int64_t     fade_out_ticks;
	std::string text;            // UTF-8, with line breaks as "\n".
	float       x;               // The box, as fractions of the frame: left, top, width, height.
	float       y;
	float       w;
	float       h;
	TextStyle   style;

	TextBox();
	// How long it shows, from the first frame it fades in on to where it has gone.
	int64_t total_ticks() const { return fade_in_ticks + dwell_ticks + fade_out_ticks; }
	bool operator==(const TextBox& other) const;
	bool operator!=(const TextBox& other) const { return !(*this == other); }
};

namespace text_box {

// How opaque the text is `ticks_from_start` after its first frame: 0 before and after it shows, rising from 0 to
// 1 through the fade in, 1 for the dwell, and falling back to 0 through the fade out. (A fade of no length is a cut.)
float alpha_at(const TextBox& box, int64_t ticks_from_start);

// What a new text box looks like to begin with: white, with a black shadow, centered, in the lower part of the frame.
TextBox default_box();

}  // namespace text_box
