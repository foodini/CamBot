#pragma once

#include <string>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"


namespace ffsw {
	// Get time since GLFW was initted
	float elapsed();
	char* make_time(char* buf, float t, bool decimal);
	void  sleep(uint32_t milliseconds);

	std::string file_dialog(const wchar_t* extension, const wchar_t* title = nullptr, bool must_exist = true);
	std::string format(const char* fmt, ...);

	// For a key that auto-repeats while held (e.g. a frame-by-frame scrub key): true once every
	// `interval` seconds, but only once `held_seconds` has passed `initial_delay` -- so a tap
	// doesn't also trigger a repeat, and holding doesn't repeat at render frame rate. `bucket` is
	// the caller's own per-key state: a plain int, initialized to -1 and otherwise left alone
	// between calls (it self-resets the moment held_seconds drops back below initial_delay, i.e.
	// on the next fresh press, so callers don't need to reset it themselves on key-down).
	bool held_repeat_due(float held_seconds, float initial_delay, float interval, int& bucket);
}