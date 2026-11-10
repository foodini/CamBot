#pragma once

// A path as the project keeps it: the curve through the points the user set (see tracking_path.h) with a name, the file of footage it
// follows, and how its marker looks. The points' times are in ticks of that file (see edit_model.h), so editing the video never
// moves them. Plain data, so it can be tested.

#include <string>

#include "tracking_path.h"

// The field of view across the width of the picture that paths start with, in degrees. A first estimate for the Osmo Action 6 in its Wide
// lens mode, from how far the picture moved against how far the camera's own sensors say it turned (about 108 degrees), which is not
// exact: the user can change it to suit the lens they shoot with.
const double DEFAULT_PATH_FOV_DEGREES = 108.0;

// The circle drawn round the target, when the user asks for one.
struct PathCircle {
	bool  enabled;
	float radius;        // As a fraction of the frame's height, so it is the same size at any picture size.
	float color[3];      // Red, green, blue, 0 to 1.
	PathCircle() : enabled(false), radius(0.03f) {
		color[0] = 1.0f;
		color[1] = 0.85f;
		color[2] = 0.1f;
	}
	bool operator==(const PathCircle& other) const {
		return enabled == other.enabled && radius == other.radius && color[0] == other.color[0] && color[1] == other.color[1] &&
			color[2] == other.color[2];
	}
	bool operator!=(const PathCircle& other) const { return !(*this == other); }
};

struct TrackedPath {
	int                    id;       // Names it while the project is open (and in the project file); unique among the project's paths.
	std::string            name;
	int                    media;    // The file whose footage it follows: a path belongs to one file.
	tracking::TrackingPath curve;
	PathCircle             circle;
	TrackedPath() : id(0), media(0) {}
	bool operator==(const TrackedPath& other) const {
		return id == other.id && name == other.name && media == other.media && curve == other.curve && circle == other.circle;
	}
	bool operator!=(const TrackedPath& other) const { return !(*this == other); }
};
