#pragma once

// Fades the whole picture to black: a black sheet over everything drawn so far on the canvas, at a given
// opacity. It is how the edit's fade in from black at its start and fade out to black at its end are shown
// (see Transition in edit_model.h). Draw it last in the frame, after the telemetry and the text, so the
// overlays fade with the video, and so the preview, the export and the screenshot with overlays all agree.
//
// Needs an OpenGL context, and the canvas bound with its viewport set.

#include <glad/glad.h>

class FadeOverlay {
public:
	FadeOverlay();
	~FadeOverlay();

	// Covers the canvas with black at `opacity` (0 to 1). Does nothing at 0 or less.
	void draw(float opacity);

private:
	FadeOverlay(const FadeOverlay&);
	FadeOverlay& operator=(const FadeOverlay&);

	GLuint m_program;
	GLuint m_vao;
	GLint  m_opacity_location;
};
