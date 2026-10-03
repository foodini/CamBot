#pragma once

#include <cassert>
#include <functional>
#include <map>
#include <set>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "env_config.h"

class InteractionMgr {
public:
	InteractionMgr();
	~InteractionMgr();
	static InteractionMgr* instance() { return c_instance; }
	void tick(GLFWwindow* window);

	void watch_key(int key)     { m_keys_watched.insert(key); }
	bool key_down(int key)      { return m_keys_down.find(key) != m_keys_down.end(); }
	bool key_up(int key)        { return m_keys_up.find(key) != m_keys_up.end(); }
	float key_held(int key);

	// Claims `key` for whichever subsystem calls this. From then on, tick() calls on_down/on_up/
	// on_held for this key right after it updates that frame's key state -- this is what lets the
	// old main()-loop if(key_down(...)) chain not exist at all. Pass nullptr for any callback you
	// don't need, but the key itself is claimed for all three message types regardless: once
	// bound, a later attempt by anything else to bind any of down/up/held for the same key is a
	// bug, not a runtime condition to handle gracefully, so it's an assert() rather than a bool
	// return. on_held receives elapsed-seconds-since-press (same semantics as key_held() above),
	// so a repeat threshold (e.g. "treat held >= 0.25s as a repeat") lives in the bound lambda,
	// not in here.
	void bind_key(int key, std::function<void()> on_down, std::function<void()> on_up,
	              std::function<void(float held_seconds)> on_held);

	bool mouse_button_down()    { return m_mouse_button_down; }
	bool mouse_button_up()      { return m_mouse_button_up; }
	float mouse_button_held()   { return m_mouse_button_held; }
	bool mouse_right_button_down()  { return m_mouse_right_button_down; }
	bool mouse_right_button_up()    { return m_mouse_right_button_up; }
	float mouse_right_button_held() { return m_mouse_right_button_held; }
	float mouse_x_pos()         { return m_mouse_x_pos; }
	float mouse_y_pos()          { return m_mouse_y_pos; }

private:
	// Shared press/release state-machine update for a single mouse button; called once per
	// button per tick() so the left and right buttons don't need two copies of this logic.
	void update_mouse_button_state(bool pressed, bool& down, bool& up, float& held);

	struct KeyBinding {
		std::function<void()>      on_down;
		std::function<void()>      on_up;
		std::function<void(float)> on_held;
	};

	static InteractionMgr*      c_instance;
	std::set<int>               m_keys_down;
	std::set<int>               m_keys_up;
	std::map<int,float>         m_keys_held;
	std::set<int>               m_keys_watched;
	std::map<int,KeyBinding>    m_key_bindings;
	bool                        m_mouse_button_down;
	bool                        m_mouse_button_up;
	float                       m_mouse_button_held;
	bool                        m_mouse_right_button_down;
	bool                        m_mouse_right_button_up;
	float                       m_mouse_right_button_held;
	float                       m_mouse_x_pos;
	float                       m_mouse_y_pos;
};