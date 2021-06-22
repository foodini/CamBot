#include <stdio.h>

#include "interaction_manager.h"
#include "util.h"

InteractionMgr* InteractionMgr::c_instance = new InteractionMgr();

InteractionMgr::InteractionMgr() :
	m_keys_down(),
	m_keys_up(),
	m_keys_held(),
	m_keys_watched(),
	m_mouse_button_down(false),
	m_mouse_button_up(false),
	m_mouse_button_held(-1.0),
	m_mouse_x_pos(0.0), m_mouse_y_pos(0.0)
{
	if (InteractionMgr::c_instance != nullptr) {
		throw "Attempted to create a second InteractionMgr singleton.";
	}
	InteractionMgr::c_instance = this;
}

InteractionMgr::~InteractionMgr() {
	InteractionMgr::c_instance = nullptr;
}

// TODO(P0): Rewrite to run on the callback. Each callback will tell store what's been pressed or
//           released each frame. Upon tick, use that info to update down/up/held.
void InteractionMgr::tick(GLFWwindow* window) {
	static int count = -1;
	count++;
	// GLFW_REPEAT is only sent to the callback, so we have to manufacture "held" ourselves.
	for (auto i = m_keys_watched.begin(); i != m_keys_watched.end(); i++) {
		if (glfwGetKey(window, *i) == GLFW_PRESS) {
			if (m_keys_down.find(*i) == m_keys_down.end() && m_keys_held.find(*i) == m_keys_held.end()) {
				// Key wasn't down or held before, so this is a new press.
				m_keys_down.insert(*i);
				m_keys_up.erase(*i);
				m_keys_held.erase(*i);
			} else {
				// Key is down, but was either new down or held last frame, so it is still held now.
				if(m_keys_down.find(*i) != m_keys_down.end())
					m_keys_held[*i] = ffsw::elapsed();
				m_keys_down.erase(*i);
				m_keys_up.erase(*i);
			}
		} else {
			if (m_keys_down.find(*i) != m_keys_down.end() || m_keys_held.find(*i) != m_keys_held.end()) {
				// Key is currently up, and was either new down or held last frame, so this is new up.
				m_keys_down.erase(*i);
				m_keys_up.insert(*i);
				m_keys_held.erase(*i);
			} else {
				// Key is currently up, and was up last frame, as well.
				m_keys_down.erase(*i);
				m_keys_up.erase(*i);
				m_keys_held.erase(*i);
			}
		}
	}

	double x_pos, y_pos;
	glfwGetCursorPos(window, &x_pos, &y_pos);
	const EnvConfig* env_config = EnvConfig::instance;
	x_pos /= env_config->screen_width();
	y_pos /= env_config->screen_height();
	m_mouse_x_pos = (float)x_pos;
	m_mouse_y_pos = 1.0f - (float)y_pos;

	int mouse_button = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
	if (mouse_button == GLFW_PRESS) {
		if (!m_mouse_button_down && m_mouse_button_held < 0.0) {
			m_mouse_button_down = true;
			m_mouse_button_up = false;
			m_mouse_button_held = -1.0;
		} else {
			m_mouse_button_down = false;
			m_mouse_button_up = false;
			if (m_mouse_button_held <= 0.0)
				m_mouse_button_held = ffsw::elapsed();
		} 
	} else {
		if (m_mouse_button_down || m_mouse_button_held >= 0.0) {
			m_mouse_button_down = false;
			m_mouse_button_up = true;
			m_mouse_button_held = -1.0;
		}
		else {
			m_mouse_button_down = false;
			m_mouse_button_up = false;
			m_mouse_button_held = -1.0;
		}
	}
}

float InteractionMgr::key_held(int key) {
	auto find = m_keys_held.find(key);
	if (find != m_keys_held.end()) {
		return ffsw::elapsed() - find->second;
	} else {
		return -1.0;
	}
}

void InteractionMgr::key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	/*
	bool paused = true, rev=true, fwd=true;

	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		glfwSetWindowShouldClose(window, true);
	if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
		paused = !paused;
	if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
		rev = true;
	if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
		fwd = true;
	*/
}