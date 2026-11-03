#include <stdio.h>

#include "imgui/imgui.h"
#include "interaction_manager.h"
#include "util.h"

InteractionMgr* InteractionMgr::c_instance = new InteractionMgr();

InteractionMgr::InteractionMgr() :
	m_keys_down(),
	m_keys_up(),
	m_keys_held(),
	m_keys_watched(),
	m_last_char_refresh(-1000.0f),
	m_mouse_button_down(false),
	m_mouse_button_up(false),
	m_mouse_button_held(-1.0),
	m_mouse_right_button_down(false),
	m_mouse_right_button_up(false),
	m_mouse_right_button_held(-1.0),
	m_canvas_x(0.0f), m_canvas_y(0.0f), m_canvas_w(0.0f), m_canvas_h(0.0f),
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

void InteractionMgr::tick(GLFWwindow* window) {
	static int count = -1;
	count++;
	refresh_char_bindings(false);
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
	// Where the cursor is on the canvas, whatever part of the window that is shown in. Until the
	// canvas has been placed there is nowhere on it, so the cursor is off to one side.
	if (m_canvas_w > 0.0f && m_canvas_h > 0.0f) {
		m_mouse_x_pos = -1.0f + 2.0f * ((float)x_pos - m_canvas_x) / m_canvas_w;
		m_mouse_y_pos = 1.0f - 2.0f * ((float)y_pos - m_canvas_y) / m_canvas_h;
	} else {
		m_mouse_x_pos = 2.0f;
		m_mouse_y_pos = 2.0f;
	}

	// Let ImGui have the click when it wants the mouse (e.g. the menu bar is open/hovered), so menu
	// interaction doesn't also register as a click on whatever widget happens to sit underneath it.
	bool imgui_wants_mouse = ImGui::GetIO().WantCaptureMouse;
	int left_mouse_button = imgui_wants_mouse ? GLFW_RELEASE : glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
	update_mouse_button_state(left_mouse_button == GLFW_PRESS, m_mouse_button_down, m_mouse_button_up, m_mouse_button_held);

	int right_mouse_button = imgui_wants_mouse ? GLFW_RELEASE : glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT);
	update_mouse_button_state(right_mouse_button == GLFW_PRESS, m_mouse_right_button_down, m_mouse_right_button_up, m_mouse_right_button_held);

	// Dispatch to whatever bound each key, now that this frame's down/up/held state is final.
	for (auto& entry : m_key_bindings) {
		int key = entry.first;
		KeyBinding& binding = entry.second;
		if (binding.on_down && key_down(key))
			binding.on_down();
		if (binding.on_up && key_up(key))
			binding.on_up();
		if (binding.on_held) {
			float held_seconds = key_held(key);
			if (held_seconds >= 0.0f)
				binding.on_held(held_seconds);
		}
	}
}

void InteractionMgr::update_mouse_button_state(bool pressed, bool& down, bool& up, float& held) {
	if (pressed) {
		if (!down && held < 0.0) {
			down = true;
			up = false;
			held = -1.0;
		} else {
			down = false;
			up = false;
			if (held <= 0.0)
				held = ffsw::elapsed();
		} 
	} else {
		if (down || held >= 0.0) {
			down = false;
			up = true;
			held = -1.0;
		}
		else {
			down = false;
			up = false;
			held = -1.0;
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

void InteractionMgr::bind_key(int key, std::function<void()> on_down, std::function<void()> on_up,
                               std::function<void(float)> on_held) {
	assert(m_key_bindings.find(key) == m_key_bindings.end() &&
	       "InteractionMgr::bind_key: key already claimed by another binding -- each key belongs to exactly one subsystem.");
	watch_key(key);
	KeyBinding binding;
	binding.on_down = on_down;
	binding.on_up = on_up;
	binding.on_held = on_held;
	m_key_bindings[key] = binding;
}

int InteractionMgr::key_for_character(char character, int fallback_key) {
	// glfwGetKeyName() reports the character a key makes in the current layout, unshifted.
	for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_GRAVE_ACCENT; key++) {
		const char* name = glfwGetKeyName(key, 0);
		if (name != NULL && name[0] == character && name[1] == '\0')
			return key;
	}
	return fallback_key;
}

void InteractionMgr::bind_char(char character, int fallback_key, std::function<void()> on_down, std::function<void()> on_up,
                               std::function<void(float)> on_held) {
	CharBinding char_binding;
	char_binding.character = character;
	char_binding.fallback_key = fallback_key;
	char_binding.key = GLFW_KEY_UNKNOWN;
	char_binding.binding.on_down = on_down;
	char_binding.binding.on_up = on_up;
	char_binding.binding.on_held = on_held;
	m_char_bindings.push_back(char_binding);
	refresh_char_bindings(true);
}

// Makes sure each character binding is on the key that carries its character in the layout the
// user has now. Asking the layout takes a little work for every binding, so unless forced this
// only looks about once a second.
void InteractionMgr::refresh_char_bindings(bool force) {
	if (m_char_bindings.empty())
		return;
	float now = ffsw::elapsed();
	if (!force && now - m_last_char_refresh < 1.0f)
		return;
	m_last_char_refresh = now;

	for (CharBinding& char_binding : m_char_bindings) {
		int key = key_for_character(char_binding.character, char_binding.fallback_key);
		if (key == char_binding.key)
			continue;
		// A key some other binding already owns can't be shared; stay where we are.
		if (m_key_bindings.find(key) != m_key_bindings.end())
			continue;
		if (char_binding.key != GLFW_KEY_UNKNOWN) {
			m_key_bindings.erase(char_binding.key);
			m_keys_down.erase(char_binding.key);
			m_keys_up.erase(char_binding.key);
			m_keys_held.erase(char_binding.key);
		}
		char_binding.key = key;
		m_key_bindings[key] = char_binding.binding;
		watch_key(key);
	}
}
