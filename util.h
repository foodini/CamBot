#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"


namespace ffsw {
	// Get time since GLFW was initted
	float elapsed();
	char* make_time(char* buf, float t, bool decimal);
}