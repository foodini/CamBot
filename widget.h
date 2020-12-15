#pragma once

#include "widget_base.h"

class DateTimeWidget: public WidgetBase {
public:
	//TODO(P1): make width, height, pos all part of vec2s.
	DateTimeWidget(float width, float height, float x_pos, float y_pos);
	virtual ~DateTimeWidget();

	virtual void render();

protected:
private:
};


class MediaScrubWidget : public WidgetBase {
public:
	//TODO(P1): make width, height, pos all part of vec2s.
	MediaScrubWidget(float width, float height, float x_pos, float y_pos);
	virtual ~MediaScrubWidget();

	virtual void handle_input();
	virtual void render();

protected:
private:
};

class MapWidget : public WidgetBase {
public:
	//TODO(P1): make width, height, pos all part of vec2s.
	MapWidget(float width, float height, float x_pos, float y_pos);
	virtual ~MapWidget();

	virtual void render();

protected:
private:
	void           set_uniforms();
	void           _set_uniforms(Shader& shader);

	Shader         m_shader_course;
	Shader         m_shader_arrow;

	uint32_t       m_course_vao;
	uint32_t       m_course_vbo;
};