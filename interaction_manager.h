#pragma once

#include <set>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "env_config.h"

class InteractionMgr {
public:
	InteractionMgr();
	~InteractionMgr();
	static InteractionMgr* instance() { return c_instance; }
	static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
	void tick(GLFWwindow* window);

	void watch_key(int key)     { m_keys_watched.insert(key); }
	bool key_down(int key)      { return m_keys_down.find(key) != m_keys_down.end(); }
	bool key_up(int key)        { return m_keys_up.find(key) != m_keys_up.end(); }
	float key_held(int key);
	bool mouse_button_down()    { return m_mouse_button_down; }
	bool mouse_button_up()      { return m_mouse_button_up; }
	float mouse_button_held()   { return m_mouse_button_held; }
	float mouse_x_pos()         { return m_mouse_x_pos; }
	float mouse_y_pos()          { return m_mouse_y_pos; }

private:
	static InteractionMgr*      c_instance;
	std::set<int>               m_keys_down;
	std::set<int>               m_keys_up;
	std::map<int,float>         m_keys_held;
	std::set<int>               m_keys_watched;
	bool                        m_mouse_button_down;
	bool                        m_mouse_button_up;
	float                       m_mouse_button_held;
	float                       m_mouse_x_pos;
	float                       m_mouse_y_pos;
};