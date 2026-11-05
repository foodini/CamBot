// Decoding UTF-8 and breaking text into lines for the text overlays (text_layout.h).
#include <math.h>
#include <stdio.h>

#include "text_box.h"
#include "text_layout.h"

static int g_failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_failures++; } } while (0)

// Lays out `text` with every character 10 wide, in a box `columns` characters wide, and gives each line back as a string.
static std::vector<std::string> lines_of(const std::string& text, int columns, std::vector<float>* widths = NULL) {
	const std::vector<uint32_t> cps = text_layout::decode_utf8(text);
	const std::vector<float> advances(cps.size(), 10.0f);
	const std::vector<text_layout::Line> lines = text_layout::wrap(cps, advances, columns * 10.0f);
	std::vector<std::string> out;
	for (size_t i = 0; i < lines.size(); i++) {
		std::string s;
		for (size_t k = lines[i].begin; k < lines[i].end; k++)
			s += (cps[k] < 128 ? (char)cps[k] : '?');
		out.push_back(s);
		if (widths)
			widths->push_back(lines[i].width);
	}
	return out;
}

static void test_decode() {
	std::vector<uint32_t> cps = text_layout::decode_utf8("aB\n");
	CHECK(cps.size() == 3 && cps[0] == 'a' && cps[1] == 'B' && cps[2] == '\n');

	cps = text_layout::decode_utf8("\xC3\xA9");            // e acute
	CHECK(cps.size() == 1 && cps[0] == 0xE9);
	cps = text_layout::decode_utf8("\xE2\x82\xAC");        // euro
	CHECK(cps.size() == 1 && cps[0] == 0x20AC);
	cps = text_layout::decode_utf8("\xF0\x9F\x98\x80");    // a four-byte character
	CHECK(cps.size() == 1 && cps[0] == 0x1F600);

	// Carriage returns go.
	cps = text_layout::decode_utf8("a\r\nb");
	CHECK(cps.size() == 3 && cps[1] == '\n');

	// Bad bytes become U+FFFD, and decoding carries on after them.
	cps = text_layout::decode_utf8("a\xFF" "b");
	CHECK(cps.size() == 3 && cps[1] == 0xFFFD && cps[2] == 'b');
	cps = text_layout::decode_utf8("a\xC3");               // Cut off.
	CHECK(cps.size() == 2 && cps[1] == 0xFFFD);
	cps = text_layout::decode_utf8("\xC0\x80");            // Overlong.
	CHECK(!cps.empty() && cps[0] == 0xFFFD);
	cps = text_layout::decode_utf8("\xED\xA0\x80");        // A surrogate.
	CHECK(cps.size() == 1 && cps[0] == 0xFFFD);
	cps = text_layout::decode_utf8("\xE2\x82" "x");        // Cut off by an ordinary character, which stays.
	CHECK(cps.size() == 2 && cps[0] == 0xFFFD && cps[1] == 'x');
	CHECK(text_layout::decode_utf8("").empty());
}

static void test_wrap() {
	std::vector<float> widths;
	std::vector<std::string> lines = lines_of("", 10);
	CHECK(lines.size() == 1 && lines[0].empty());

	lines = lines_of("hello world", 20, &widths);
	CHECK(lines.size() == 1 && lines[0] == "hello world" && widths[0] == 110.0f);

	lines = lines_of("hello world", 11);                 // Exactly full.
	CHECK(lines.size() == 1);

	widths.clear();
	lines = lines_of("hello world", 10, &widths);
	CHECK(lines.size() == 2 && lines[0] == "hello" && lines[1] == "world");
	CHECK(widths[0] == 50.0f && widths[1] == 50.0f);

	lines = lines_of("one two three four", 9);
	CHECK(lines.size() == 3 && lines[0] == "one two" && lines[1] == "three" && lines[2] == "four");

	// Spaces at a break and at the end of a line aren't part of it.
	widths.clear();
	lines = lines_of("ab    cd   ", 4, &widths);
	CHECK(lines.size() == 2 && lines[0] == "ab" && lines[1] == "cd");
	CHECK(widths[0] == 20.0f && widths[1] == 20.0f);

	// Line breaks in the text.
	lines = lines_of("ab\ncd\n\nef", 20);
	CHECK(lines.size() == 4 && lines[0] == "ab" && lines[1] == "cd" && lines[2].empty() && lines[3] == "ef");
	lines = lines_of("ab\n", 20);
	CHECK(lines.size() == 2 && lines[1].empty());
	lines = lines_of("a\r\nb", 20);
	CHECK(lines.size() == 2);

	// A word wider than the line is broken where it has to be.
	lines = lines_of("abcdefghij", 4);
	CHECK(lines.size() == 3 && lines[0] == "abcd" && lines[1] == "efgh" && lines[2] == "ij");
	lines = lines_of("ab abcdefghij cd", 4);
	CHECK(lines.size() == 5 && lines[0] == "ab" && lines[1] == "abcd" && lines[2] == "efgh" && lines[3] == "ij" && lines[4] == "cd");

	// The spaces typed at the start of the text stay (on its first line).
	lines = lines_of("  ab", 20);
	CHECK(lines.size() == 1 && lines[0] == "  ab");

	// A box too narrow for a single character still makes progress, one to a line.
	lines = lines_of("abc", 0);
	CHECK(lines.size() == 3);
	lines = lines_of("a b", 0);
	CHECK(lines.size() == 2);
}

static void test_widths_vary() {
	// Characters of different widths: 'i' 2 wide, 'm' 8 wide.
	const std::vector<uint32_t> cps = text_layout::decode_utf8("mm ii mm");
	std::vector<float> advances;
	for (size_t i = 0; i < cps.size(); i++)
		advances.push_back(cps[i] == 'm' ? 8.0f : (cps[i] == 'i' ? 2.0f : 3.0f));
	// mm = 16, space 3, ii = 4, space 3, mm = 16.
	std::vector<text_layout::Line> lines = text_layout::wrap(cps, advances, 23.0f);
	CHECK(lines.size() == 2);
	CHECK(lines[0].begin == 0 && lines[0].end == 5 && fabs(lines[0].width - 23.0f) < 1e-4);
	CHECK(lines[1].begin == 6 && lines[1].end == 8 && fabs(lines[1].width - 16.0f) < 1e-4);
	lines = text_layout::wrap(cps, advances, 22.0f);
	CHECK(lines.size() == 3);
}

static void test_line_offset() {
	CHECK(text_layout::line_offset(TEXT_ALIGN_LEFT, 100.0f, 40.0f) == 0.0f);
	CHECK(text_layout::line_offset(TEXT_ALIGN_CENTER, 100.0f, 40.0f) == 30.0f);
	CHECK(text_layout::line_offset(TEXT_ALIGN_RIGHT, 100.0f, 40.0f) == 60.0f);
	// A line wider than the box starts at its left edge whatever the alignment.
	CHECK(text_layout::line_offset(TEXT_ALIGN_CENTER, 100.0f, 140.0f) == 0.0f);
	CHECK(text_layout::line_offset(TEXT_ALIGN_RIGHT, 100.0f, 140.0f) == 0.0f);
}

int main() {
	test_decode();
	test_wrap();
	test_widths_vary();
	test_line_offset();
	if (g_failures == 0)
		printf("all ok\n");
	return g_failures == 0 ? 0 : 1;
}
