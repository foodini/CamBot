#pragma once

#include <string>
#include <vector>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"


namespace ffsw {
	// Get time since GLFW was initted
	float elapsed();
	char* make_time(char* buf, float t, bool decimal);
	void  sleep(uint32_t milliseconds);

	// Both return an empty result if the user cancels (or the dialog fails).
	std::string file_dialog(const wchar_t* extension, const wchar_t* title = nullptr, bool must_exist = true);
	// Like file_dialog(), but the user may pick several existing files at once. They come back in
	// the order Windows lists them, which is not necessarily the order the user clicked.
	std::vector<std::string> file_dialog_multi(const wchar_t* extension, const wchar_t* title = nullptr);

	// A message the user has to click away. `text` and `title` are UTF-8.
	void message_box(const std::string& title, const std::string& text, bool is_error = true);
	// A question with OK and Cancel; true if the user chose OK.
	bool confirm_box(const std::string& title, const std::string& text);
	// A question with Yes, No and Cancel.
	enum Answer { ANSWER_YES, ANSWER_NO, ANSWER_CANCEL };
	Answer yes_no_cancel_box(const std::string& title, const std::string& text);
	std::string format(const char* fmt, ...);

	// For a key that auto-repeats while held (e.g. a frame-by-frame scrub key): true once every
	// `interval` seconds, but only once `held_seconds` has passed `initial_delay` -- so a tap
	// doesn't also trigger a repeat, and holding doesn't repeat at render frame rate. `bucket` is
	// the caller's own per-key state: a plain int, initialized to -1 and otherwise left alone
	// between calls (it self-resets the moment held_seconds drops back below initial_delay, i.e.
	// on the next fresh press, so callers don't need to reset it themselves on key-down).
	bool held_repeat_due(float held_seconds, float initial_delay, float interval, int& bucket);
}