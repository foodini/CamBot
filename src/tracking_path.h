#pragma once

#include <stdint.h>
#include <vector>

// Paths: how something that moves through the picture (a hawk, another glider) is followed. A path is the
// points the user has set, each one a place in the picture at a frame of the footage, and the curve through them.
// This is the arithmetic of that and nothing else -- no window, no GPU, no file -- so it can be tested.
//
// Orientation-aware paths. The camera turns and banks with the glider, so something that holds its bearing still
// swings across the picture. When the camera's orientation is known (the DJI cameras record it for every frame),
// a point is kept as a direction in the world, not as a place in the picture: the click is turned into the
// direction it points in, using the lens and the camera's orientation at that frame. The directions are what is
// interpolated between the points, and each frame's place in the picture comes from turning the direction back
// with that frame's orientation. A path with no correction in it is then a target holding its bearing, and the
// points the user adds correct for the target's own motion. Without orientation (or with the box unticked) the
// places in the picture are interpolated directly.
//
// Places in the picture are (u, v): u from 0 at the left edge to 1 at the right, v from 0 at the top to 1 at
// the bottom, of the footage as it was recorded (before any rotation the project applies). Times are the
// footage's own ticks, never the edited timeline's, so editing cannot break a path.

namespace tracking {

struct Vec3 {
	double x, y, z;
	Vec3() : x(0.0), y(0.0), z(0.0) {}
	Vec3(double x_in, double y_in, double z_in) : x(x_in), y(y_in), z(z_in) {}
};

double length(const Vec3& v);
double dot(const Vec3& a, const Vec3& b);
Vec3 normalized(const Vec3& v);    // A zero vector comes back as it is.

// A rotation, as a unit quaternion.
struct Quat {
	double w, x, y, z;
	Quat() : w(1.0), x(0.0), y(0.0), z(0.0) {}
	Quat(double w_in, double x_in, double y_in, double z_in) : w(w_in), x(x_in), y(y_in), z(z_in) {}
};

Quat conjugate(const Quat& q);     // The opposite rotation.
Quat multiply(const Quat& a, const Quat& b);   // Rotating by b, then by a.
Vec3 rotate(const Quat& q, const Vec3& v);
Quat from_axis_angle(const Vec3& axis, double radians);
Quat slerp(const Quat& a, const Quat& b, double t);   // The turn from a to b, t of the way, by the short way round.

// The lens, as a pinhole: how a place in the picture and a direction out of the camera correspond. Camera
// directions have x to the right, y down and z out along the lens axis. Real lenses bend straight lines more
// than this (the wide modes, anyway), which shows up as error away from the middle of the picture.
class CameraLens {
public:
	// `horizontal_fov` in radians across the whole width; `aspect` is the picture's width over its height.
	CameraLens(double horizontal_fov, double aspect);
	double horizontal_fov() const { return m_fov; }
	Vec3 ray_for(double u, double v) const;      // The unit direction a place in the picture looks out along.
	// The place in the picture a direction comes from; false (and *u, *v untouched) when it is behind the camera.
	// The place can be outside the picture (0 to 1) and still be true.
	bool project(const Vec3& direction, double* u, double* v) const;

private:
	double m_fov;
	double m_aspect;
	double m_half_width;   // tan(fov / 2): half the picture's width at one unit out.
};

// The camera's orientation at the frames of a piece of footage: which way the lens axis and the picture's right
// and down point in the world, as the rotation from camera directions to world directions. Kept in order of time.
class CameraOrientations {
public:
	CameraOrientations() {}
	void clear();
	void add(int64_t ticks, const Quat& orientation);   // A time already there is replaced.
	bool empty() const { return m_ticks.empty(); }
	// The orientation at `ticks`, blended between the two frames around it; before the first or after the last it is
	// the nearest one. False when there are none.
	bool at(int64_t ticks, Quat* orientation) const;

private:
	std::vector<int64_t> m_ticks;
	std::vector<Quat>    m_orientations;
};

// One point the user set: this place in the picture, at this frame of the footage.
struct PathPoint {
	int64_t ticks;
	double  u;
	double  v;
	PathPoint() : ticks(0), u(0.0), v(0.0) {}
	PathPoint(int64_t ticks_in, double u_in, double v_in) : ticks(ticks_in), u(u_in), v(v_in) {}
	bool operator==(const PathPoint& other) const { return ticks == other.ticks && u == other.u && v == other.v; }
	bool operator!=(const PathPoint& other) const { return !(*this == other); }
};

class TrackingPath {
public:
	TrackingPath() : m_orientation_aware(true) {}

	bool orientation_aware() const { return m_orientation_aware; }
	void set_orientation_aware(bool aware) { m_orientation_aware = aware; }

	bool empty() const { return m_points.empty(); }
	const std::vector<PathPoint>& points() const { return m_points; }
	// The path lasts from its first point to its last. Only meaningful when it is not empty.
	int64_t first_ticks() const { return m_points.front().ticks; }
	int64_t last_ticks() const { return m_points.back().ticks; }

	// One point to a frame: a point already at `ticks` is moved to the new place.
	void set_point(int64_t ticks, double u, double v);
	// Replaces the points with these (in any order; of two at one frame, the later in the list is kept).
	void set_points(const std::vector<PathPoint>& points);
	bool operator==(const TrackingPath& other) const { return m_orientation_aware == other.m_orientation_aware && m_points == other.m_points; }
	bool operator!=(const TrackingPath& other) const { return !(*this == other); }
	// False when there is no point at `ticks`.
	bool remove_point(int64_t ticks);

	// Where the path is in the picture at `ticks`. Before the first point and after the last, it is where that end
	// point was put, and does not move (something attached to the path waits at the end). In between, it follows
	// the curve through the points, by orientation when this path is orientation-aware and `orientations` has
	// anything, and by the places in the picture otherwise. False when the path is empty, or the direction it
	// points in is behind the camera; the place can be outside the picture and still be true.
	bool position_at(int64_t ticks, const CameraLens& lens, const CameraOrientations& orientations, double* u, double* v) const;

	// Shorten the path so it begins (or ends) at `ticks`: the points before (after) it go, and a point is put at
	// `ticks`, where the path was, unless there already is one. Nothing to cut when `ticks` is at or beyond the end
	// that is being trimmed. False, and nothing done, when `ticks` is past the other end of the path (it would
	// leave nothing) or the path is empty.
	bool trim_left(int64_t ticks, const CameraLens& lens, const CameraOrientations& orientations);
	bool trim_right(int64_t ticks, const CameraLens& lens, const CameraOrientations& orientations);

private:
	std::vector<PathPoint> m_points;   // In order of time, one to a frame.
	bool                   m_orientation_aware;

	bool oriented_position_at(int64_t ticks, const CameraLens& lens, const CameraOrientations& orientations, double* u, double* v) const;
	bool screen_position_at(int64_t ticks, double* u, double* v) const;
};

}  // namespace tracking
