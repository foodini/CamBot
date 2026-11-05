#pragma once

// A moment in one media file, counted from the start of that file. (Used by the edit model and by
// the things that attach to footage, such as text boxes.)

#include <stdint.h>

struct FileTime {
	int     media;
	int64_t local_ticks;
	FileTime() : media(0), local_ticks(0) {}
	FileTime(int media_index, int64_t ticks) : media(media_index), local_ticks(ticks) {}
	bool operator==(const FileTime& other) const { return media == other.media && local_ticks == other.local_ticks; }
	bool operator!=(const FileTime& other) const { return !(*this == other); }
};
