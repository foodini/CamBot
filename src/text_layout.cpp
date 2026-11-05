#include "text_layout.h"

#include "text_box.h"

namespace text_layout {

namespace {

const uint32_t REPLACEMENT = 0xFFFD;

bool is_space(uint32_t code_point) {
	return code_point == ' ' || code_point == '\t';
}

float width_of(const std::vector<float>& advances, size_t begin, size_t end) {
	float total = 0.0f;
	for (size_t i = begin; i < end; i++)
		total += advances[i];
	return total;
}

// Lays out the code points [begin, end), which hold no line break, adding its line(s) to *lines.
void wrap_one_line(const std::vector<uint32_t>& cps, const std::vector<float>& advances, size_t begin, size_t end, float max_width,
		std::vector<Line>* lines) {
	const float slack = 0.001f;    // So a line that is exactly full doesn't tip over on rounding.
	size_t line_begin = begin;     // The line being built: where it starts,
	size_t line_end = begin;       // where its last word ends,
	float  line_width = 0.0f;      // and how wide it is up to there.
	bool   has_word = false;
	size_t pos = begin;
	while (pos < end) {
		size_t word_begin = pos;
		while (word_begin < end && is_space(cps[word_begin]))
			word_begin++;
		size_t word_end = word_begin;
		while (word_end < end && !is_space(cps[word_end]))
			word_end++;
		if (word_end == word_begin)
			break;                 // Nothing but spaces left, and the ones at the end of a line don't count.
		const float space_width = width_of(advances, pos, word_begin);
		const float word_width = width_of(advances, word_begin, word_end);
		if (!has_word) {
			// The first word of the line (with any spaces the text put before it, if it is the first line): fits, or is broken up.
			size_t k = pos;
			float w = 0.0f;
			while (k < word_end && (k == pos || w + advances[k] <= max_width + slack)) {
				w += advances[k];
				k++;
			}
			if (k == word_end) {
				line_width = w;
				line_end = word_end;
				has_word = true;
				pos = word_end;
			} else {
				Line line = { line_begin, k, w };
				lines->push_back(line);
				line_begin = line_end = k;
				line_width = 0.0f;
				pos = k;
			}
		} else if (line_width + space_width + word_width <= max_width + slack) {
			line_width += space_width + word_width;
			line_end = word_end;
			pos = word_end;
		} else {
			Line line = { line_begin, line_end, line_width };
			lines->push_back(line);
			line_begin = line_end = word_begin;     // The spaces the break fell on are dropped.
			line_width = 0.0f;
			has_word = false;
			pos = word_begin;
		}
	}
	Line last = { line_begin, line_end, line_width };
	lines->push_back(last);
}

}  // namespace

std::vector<uint32_t> decode_utf8(const std::string& text) {
	std::vector<uint32_t> out;
	const size_t n = text.size();
	size_t i = 0;
	while (i < n) {
		const unsigned char c = (unsigned char)text[i];
		int extra = 0;
		uint32_t cp = 0;
		uint32_t minimum = 0;
		if (c < 0x80) {
			cp = c;
		} else if (c >= 0xC2 && c <= 0xDF) {
			extra = 1; cp = c & 0x1F; minimum = 0x80;
		} else if (c >= 0xE0 && c <= 0xEF) {
			extra = 2; cp = c & 0x0F; minimum = 0x800;
		} else if (c >= 0xF0 && c <= 0xF4) {
			extra = 3; cp = c & 0x07; minimum = 0x10000;
		} else {
			out.push_back(REPLACEMENT);
			i++;
			continue;
		}
		size_t used = 1;
		bool ok = true;
		for (int k = 1; k <= extra; k++) {
			if (i + k >= n || (((unsigned char)text[i + k]) & 0xC0) != 0x80) {
				ok = false;
				break;
			}
			cp = (cp << 6) | (((unsigned char)text[i + k]) & 0x3F);
			used++;
		}
		if (ok && (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)))
			ok = false;
		if (!ok) {
			out.push_back(REPLACEMENT);
			i += used;      // The bytes that did look like part of it go with it.
			continue;
		}
		if (cp != '\r')
			out.push_back(cp);
		i += used;
	}
	return out;
}

std::vector<Line> wrap(const std::vector<uint32_t>& code_points, const std::vector<float>& advances, float max_width) {
	std::vector<Line> lines;
	const size_t n = code_points.size();
	size_t begin = 0;
	for (;;) {
		size_t end = begin;
		while (end < n && code_points[end] != '\n')
			end++;
		wrap_one_line(code_points, advances, begin, end, max_width, &lines);
		if (end >= n)
			break;
		begin = end + 1;
	}
	return lines;
}

float line_offset(int align, float box_width, float line_width) {
	float spare = box_width - line_width;
	if (spare < 0.0f)
		spare = 0.0f;
	if (align == TEXT_ALIGN_CENTER)
		return spare * 0.5f;
	if (align == TEXT_ALIGN_RIGHT)
		return spare;
	return 0.0f;
}

}  // namespace text_layout
