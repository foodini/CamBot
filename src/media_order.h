#pragma once

// Putting a group of video files into the order they were recorded in. Cameras split one long
// recording into pieces, and plain alphabetical order doesn't always keep them in sequence:
//
//   - GoPro names are GX<chapter><recording>.MP4 (GH.. for HEVC, GL.. for low-res): two digits of
//     chapter, then four of recording number. Chapters of one recording share the number, so
//     GX010123, GX020123, GX010124 must sort by recording first, then chapter -- alphabetical
//     order would put the second chapter of recording 123 after the first of 124.
//   - Everything else (DJI_20261103101500_0001_D.MP4 and so on) is sorted "naturally": case
//     doesn't matter, and runs of digits compare by value, so clip2 comes before clip10.
//
// This is only a best guess at the order. The media library lets the user change it.

#include <algorithm>
#include <ctype.h>
#include <string>
#include <vector>

namespace media_order {

// Just the file name, from a path using either kind of slash.
inline std::string base_name(const std::string& path) {
	size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

// True for G[HXL]<2 digits><4 digits>.<ext>, filling in the chapter and recording numbers.
inline bool parse_gopro_name(const std::string& path, int* chapter, int* recording) {
	std::string name = base_name(path);
	if (name.size() < 9 || toupper((unsigned char)name[0]) != 'G')
		return false;
	char kind = (char)toupper((unsigned char)name[1]);
	if (kind != 'H' && kind != 'X' && kind != 'L')
		return false;
	for (int i = 2; i < 8; i++) {
		if (!isdigit((unsigned char)name[i]))
			return false;
	}
	if (name[8] != '.')
		return false;
	*chapter = (name[2] - '0') * 10 + (name[3] - '0');
	*recording = (name[4] - '0') * 1000 + (name[5] - '0') * 100 + (name[6] - '0') * 10 + (name[7] - '0');
	return true;
}

// Case-insensitive comparison where runs of digits compare by value. Negative, zero or positive,
// like strcmp.
inline int natural_compare(const std::string& a, const std::string& b) {
	size_t i = 0;
	size_t j = 0;
	while (i < a.size() && j < b.size()) {
		if (isdigit((unsigned char)a[i]) && isdigit((unsigned char)b[j])) {
			// Skip leading zeros, then the longer run is the bigger number; same length compares digit by digit.
			size_t i_end = i;
			size_t j_end = j;
			while (i_end < a.size() && isdigit((unsigned char)a[i_end]))
				i_end++;
			while (j_end < b.size() && isdigit((unsigned char)b[j_end]))
				j_end++;
			size_t i_start = i;
			size_t j_start = j;
			while (i_start + 1 < i_end && a[i_start] == '0')
				i_start++;
			while (j_start + 1 < j_end && b[j_start] == '0')
				j_start++;
			size_t i_len = i_end - i_start;
			size_t j_len = j_end - j_start;
			if (i_len != j_len)
				return i_len < j_len ? -1 : 1;
			for (size_t k = 0; k < i_len; k++) {
				if (a[i_start + k] != b[j_start + k])
					return a[i_start + k] < b[j_start + k] ? -1 : 1;
			}
			i = i_end;
			j = j_end;
		} else {
			int ca = tolower((unsigned char)a[i]);
			int cb = tolower((unsigned char)b[j]);
			if (ca != cb)
				return ca < cb ? -1 : 1;
			i++;
			j++;
		}
	}
	if (i < a.size())
		return 1;
	if (j < b.size())
		return -1;
	return 0;
}

// The ordering itself. GoPro-named files come after everything else, so mixing the two kinds
// still gives one consistent order.
inline bool comes_before(const std::string& a, const std::string& b) {
	int chapter_a = 0, recording_a = 0, chapter_b = 0, recording_b = 0;
	bool gopro_a = parse_gopro_name(a, &chapter_a, &recording_a);
	bool gopro_b = parse_gopro_name(b, &chapter_b, &recording_b);
	if (gopro_a != gopro_b)
		return gopro_b;
	if (gopro_a) {
		if (recording_a != recording_b)
			return recording_a < recording_b;
		if (chapter_a != chapter_b)
			return chapter_a < chapter_b;
	}
	int result = natural_compare(base_name(a), base_name(b));
	if (result != 0)
		return result < 0;
	return a < b;
}

inline void order_files(std::vector<std::string>* paths) {
	std::sort(paths->begin(), paths->end(), comes_before);
}

}  // namespace media_order
