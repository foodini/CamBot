#pragma once

// What the program needs to know to say where a path is at a moment of the video: the lens the footage was shot with (a setting of the
// project), and the camera's orientation through each file, which is read in the background (see OrientationLoader). The path panel
// (path_panel.h), the text panel's attachment of a box to a path (text_panel.h) and the drawing of the video (where attached boxes, circles
// and leader lines are placed) all ask it, so a file is read once however many want it.
//
// Call update() once a frame.

#include <stddef.h>
#include <stdint.h>
#include <vector>

#include "edit_model.h"
#include "orientation_loader.h"
#include "path_attachment.h"
#include "tracked_path.h"
#include "tracking_path.h"

class MediaContainerMgr;

// What is to be drawn of the text boxes and the paths at one moment (see PathTracker::scene()).
struct PathScene {
	struct Circle {
		int    path_id;
		double x, y;          // Where it is on the canvas, and its radius as a fraction of the canvas's height.
		double radius;
		float  color[3];
		float  opacity;
	};
	struct Line {
		double from_x, from_y, to_x, to_y;
		float  color[3];
		float  opacity;
	};
	struct Box {
		size_t index;         // Of the box in EditModel::text_boxes().
		float  x, y;          // Where its top left is on the canvas.
		float  opacity;
	};
	std::vector<Circle> circles;
	std::vector<Line>   lines;
	std::vector<Box>    boxes;      // Only the boxes showing.
};

class PathTracker {
public:
	// `canvas_aspect` is the canvas's width over its height (the preview shows the canvas whole).
	PathTracker(EditModel* model, MediaContainerMgr* media, double canvas_aspect);

	// Once a frame: takes the next few packets of the file being read apart.
	void update();

	double canvas_aspect() const { return m_canvas_aspect; }
	// The pinhole lens of the project's field of view, for the picture the footage has.
	tracking::CameraLens lens() const;
	// Asks for the camera's orientation through the file `path` follows (a quick call once it has been asked for, or when the path has no
	// use for it), starting from where the playhead is if that is in the file. A file is read at a time: with `urgent` another file being
	// read is given up on (as when the user is working on this path), otherwise this waits its turn.
	void request(const TrackedPath& path, bool urgent = false);
	// Whether a path that is drawn into the picture (it has a circle, or text attached) is still waiting for its file's orientations to be
	// read, so that what is drawn is not yet what an export would have.
	bool orientations_pending() const;
	// What a path's curve is worked out through: the orientations of its file (none until they have been read).
	const tracking::CameraOrientations& orientations(const TrackedPath& path) const;
	// How far the reading of the file `media` is along; for showing the user.
	const OrientationLoader& loader() const { return m_loader; }

	// Where an attached box is, and what is drawn with it, at `timeline_ticks` (see path_attachment::place()); asks for the orientations
	// it needs. `rotation_radians` is the project's rotation of the picture and `aspect` the canvas's width over its height.
	path_attachment::Placement place(const TextBox& box, double rotation_radians, double aspect, int64_t timeline_ticks);
	// What to draw at `timeline_ticks`: the text boxes showing and where they are (an attached one where its path puts it), a circle for each
	// path that has one switched on (opaque while the path is there, or, when text is attached to it, as opaque as the most opaque of that
	// text), and a leader line for each box that has one, all of them in the path's colour. `rotation_radians` is the project's rotation of
	// the picture.
	void scene(double rotation_radians, int64_t timeline_ticks, PathScene* scene);
	// Where path `path` is on the canvas at `timeline_ticks`. False if the path is not there then (see path_attachment::footage_ticks_at()), or
	// has no place to give.
	bool marker(const TrackedPath& path, double rotation_radians, double aspect, int64_t timeline_ticks, double* x, double* y);

private:
	EditModel*         m_model;
	MediaContainerMgr* m_media;
	double             m_canvas_aspect;
	OrientationLoader  m_loader;
};
