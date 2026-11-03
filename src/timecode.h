#pragma once

// Position in the video as hours:minutes:seconds#frame, such as 1:13:15#22 (the 22nd frame, counting
// from 0, since the second began). Worked out from the frame's own time, so at 29.97 frames per
// second it doesn't pretend there are 30 in every second: a second holds 29 or 30 of them, and
// the number counts the frames whose time falls in that second.
//
// Times are in ticks of tick_num / tick_den seconds (the decoder's time base), and a frame lasts
// frame_ticks of them.

#include <stdint.h>
#include <string>

struct Timecode {
	int hours;
	int minutes;
	int seconds;
	int frame;
	Timecode() : hours(0), minutes(0), seconds(0), frame(0) {}
};

namespace timecode {

Timecode    for_ticks(int64_t ticks, int tick_num, int tick_den, int64_t frame_ticks);
// "1:13:15#22". `hours_width` pads the hours with spaces on the left up to that many characters
// (2 gives " 1:13:15#22"), so a readout that is redrawn as the time passes keeps its shape.
std::string format(const Timecode& time, int hours_width = 1);
// The frame rate as it is usually quoted: "30", "60", "29.97", "23.98".
std::string format_frame_rate(int tick_num, int tick_den, int64_t frame_ticks);

}  // namespace timecode
