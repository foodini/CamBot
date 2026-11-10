#include "tracking_path.h"

#include <math.h>

namespace tracking {

// ---- Vectors and rotations ----

double length(const Vec3& v) {
	return sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

double dot(const Vec3& a, const Vec3& b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 normalized(const Vec3& v) {
	const double size = length(v);
	if (size < 1e-300)
		return v;
	return Vec3(v.x / size, v.y / size, v.z / size);
}

static Vec3 cross(const Vec3& a, const Vec3& b) {
	return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

Quat conjugate(const Quat& q) {
	return Quat(q.w, -q.x, -q.y, -q.z);
}

Quat multiply(const Quat& a, const Quat& b) {
	return Quat(
		a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
		a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
		a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
		a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w);
}

Vec3 rotate(const Quat& q, const Vec3& v) {
	const Vec3 axis(q.x, q.y, q.z);
	const Vec3 twice = cross(axis, v);
	const Vec3 doubled(2.0 * twice.x, 2.0 * twice.y, 2.0 * twice.z);
	const Vec3 more = cross(axis, doubled);
	return Vec3(v.x + q.w * doubled.x + more.x, v.y + q.w * doubled.y + more.y, v.z + q.w * doubled.z + more.z);
}

Quat from_axis_angle(const Vec3& axis, double radians) {
	const Vec3 unit = normalized(axis);
	const double s = sin(radians * 0.5);
	return Quat(cos(radians * 0.5), unit.x * s, unit.y * s, unit.z * s);
}

Quat slerp(const Quat& a, const Quat& b, double t) {
	double cosine = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
	Quat end = b;
	if (cosine < 0.0) {     // q and -q are the same rotation; take the short way round.
		cosine = -cosine;
		end = Quat(-b.w, -b.x, -b.y, -b.z);
	}
	double weight_a;
	double weight_b;
	if (cosine > 0.9995) {  // Nearly the same: a straight blend is as good and avoids dividing by almost nothing.
		weight_a = 1.0 - t;
		weight_b = t;
	} else {
		const double angle = acos(cosine);
		const double s = sin(angle);
		weight_a = sin((1.0 - t) * angle) / s;
		weight_b = sin(t * angle) / s;
	}
	Quat result(weight_a * a.w + weight_b * end.w, weight_a * a.x + weight_b * end.x,
		weight_a * a.y + weight_b * end.y, weight_a * a.z + weight_b * end.z);
	const double size = sqrt(result.w * result.w + result.x * result.x + result.y * result.y + result.z * result.z);
	if (size > 0.0) {
		result.w /= size;
		result.x /= size;
		result.y /= size;
		result.z /= size;
	}
	return result;
}

// ---- The lens ----

CameraLens::CameraLens(double horizontal_fov, double aspect) :
	m_fov(horizontal_fov),
	m_aspect(aspect > 0.0 ? aspect : 1.0),
	m_half_width(tan(horizontal_fov * 0.5))
{}

Vec3 CameraLens::ray_for(double u, double v) const {
	const double x = (u - 0.5) * 2.0 * m_half_width;
	const double y = (v - 0.5) * 2.0 * m_half_width / m_aspect;
	return normalized(Vec3(x, y, 1.0));
}

bool CameraLens::project(const Vec3& direction, double* u, double* v) const {
	if (direction.z <= 1e-9)
		return false;
	const double x = direction.x / direction.z;
	const double y = direction.y / direction.z;
	*u = 0.5 + x / (2.0 * m_half_width);
	*v = 0.5 + y * m_aspect / (2.0 * m_half_width);
	return true;
}

// ---- The camera's orientation ----

void CameraOrientations::clear() {
	m_ticks.clear();
	m_orientations.clear();
}

void CameraOrientations::add(int64_t ticks, const Quat& orientation) {
	size_t at = m_ticks.size();
	while (at > 0 && m_ticks[at - 1] >= ticks)
		at--;
	if (at < m_ticks.size() && m_ticks[at] == ticks) {
		m_orientations[at] = orientation;
		return;
	}
	m_ticks.insert(m_ticks.begin() + at, ticks);
	m_orientations.insert(m_orientations.begin() + at, orientation);
}

bool CameraOrientations::at(int64_t ticks, Quat* orientation) const {
	const size_t count = m_ticks.size();
	if (count == 0)
		return false;
	if (ticks <= m_ticks[0]) {
		*orientation = m_orientations[0];
		return true;
	}
	if (ticks >= m_ticks[count - 1]) {
		*orientation = m_orientations[count - 1];
		return true;
	}
	// The last frame at or before `ticks`, by halving.
	size_t low = 0;
	size_t high = count - 1;
	while (high - low > 1) {
		const size_t middle = (low + high) / 2;
		if (m_ticks[middle] <= ticks)
			low = middle;
		else
			high = middle;
	}
	const double span = (double)(m_ticks[high] - m_ticks[low]);
	*orientation = slerp(m_orientations[low], m_orientations[high], (double)(ticks - m_ticks[low]) / span);
	return true;
}

// ---- Smooth curves through points ----
//
// Piecewise cubic Hermite with slopes chosen so the curve never goes beyond the points either side of it (it does not
// overshoot between two close points the way a looser spline can) and has no kinks. The usual "PCHIP" choice of slopes.

namespace {

double sign_of(double value) {
	if (value > 0.0)
		return 1.0;
	if (value < 0.0)
		return -1.0;
	return 0.0;
}

// The slope at an end from the two nearest gaps (h0 and delta0 at the end itself, h1 and delta1 next to it).
double end_slope(double h0, double h1, double delta0, double delta1) {
	const double slope = ((2.0 * h0 + h1) * delta0 - h0 * delta1) / (h0 + h1);
	if (sign_of(slope) != sign_of(delta0))
		return 0.0;
	if (sign_of(delta0) != sign_of(delta1) && fabs(slope) > 3.0 * fabs(delta0))
		return 3.0 * delta0;
	return slope;
}

void pchip_slopes(const std::vector<double>& t, const std::vector<double>& y, std::vector<double>* slopes) {
	const size_t count = t.size();
	slopes->assign(count, 0.0);
	if (count < 2)
		return;
	std::vector<double> gap(count - 1);
	std::vector<double> delta(count - 1);
	for (size_t k = 0; k + 1 < count; k++) {
		gap[k] = t[k + 1] - t[k];
		delta[k] = (y[k + 1] - y[k]) / gap[k];
	}
	if (count == 2) {
		(*slopes)[0] = delta[0];
		(*slopes)[1] = delta[0];
		return;
	}
	for (size_t k = 1; k + 1 < count; k++) {
		if (delta[k - 1] * delta[k] <= 0.0) {
			(*slopes)[k] = 0.0;       // A turning point: level there.
		} else {
			const double w1 = 2.0 * gap[k] + gap[k - 1];
			const double w2 = gap[k] + 2.0 * gap[k - 1];
			(*slopes)[k] = (w1 + w2) / (w1 / delta[k - 1] + w2 / delta[k]);
		}
	}
	(*slopes)[0] = end_slope(gap[0], gap[1], delta[0], delta[1]);
	(*slopes)[count - 1] = end_slope(gap[count - 2], gap[count - 3], delta[count - 2], delta[count - 3]);
}

// A smooth curve of up to three numbers through values at increasing times.
class SmoothCurve {
public:
	SmoothCurve(int dimensions) : m_dimensions(dimensions) {}
	void add(double t, double a, double b, double c) {
		m_t.push_back(t);
		m_y[0].push_back(a);
		m_y[1].push_back(b);
		m_y[2].push_back(c);
	}
	void finish() {
		for (int d = 0; d < m_dimensions; d++)
			pchip_slopes(m_t, m_y[d], &m_m[d]);
	}
	// Times outside the points take the nearest end.
	void at(double x, double* out) const {
		const size_t count = m_t.size();
		for (int d = 0; d < 3; d++)
			out[d] = 0.0;
		if (count == 0)
			return;
		size_t k = 0;
		if (x <= m_t[0]) {
			x = m_t[0];
		} else if (x >= m_t[count - 1]) {
			x = m_t[count - 1];
			k = count >= 2 ? count - 2 : 0;
		} else {
			while (k + 2 < count && m_t[k + 1] <= x)
				k++;
		}
		for (int d = 0; d < m_dimensions; d++) {
			if (count == 1) {
				out[d] = m_y[d][0];
				continue;
			}
			const double gap = m_t[k + 1] - m_t[k];
			const double s = (x - m_t[k]) / gap;
			const double s2 = s * s;
			const double s3 = s2 * s;
			out[d] = (2.0 * s3 - 3.0 * s2 + 1.0) * m_y[d][k] + (s3 - 2.0 * s2 + s) * gap * m_m[d][k]
				+ (-2.0 * s3 + 3.0 * s2) * m_y[d][k + 1] + (s3 - s2) * gap * m_m[d][k + 1];
		}
	}

private:
	int                 m_dimensions;
	std::vector<double> m_t;
	std::vector<double> m_y[3];
	std::vector<double> m_m[3];
};

}  // namespace

// ---- The path ----

void TrackingPath::set_point(int64_t ticks, double u, double v) {
	size_t at = m_points.size();
	while (at > 0 && m_points[at - 1].ticks >= ticks)
		at--;
	if (at < m_points.size() && m_points[at].ticks == ticks) {
		m_points[at].u = u;
		m_points[at].v = v;
		return;
	}
	m_points.insert(m_points.begin() + at, PathPoint(ticks, u, v));
}

void TrackingPath::set_points(const std::vector<PathPoint>& points) {
	m_points.clear();
	for (size_t i = 0; i < points.size(); i++)
		set_point(points[i].ticks, points[i].u, points[i].v);
}

bool TrackingPath::remove_point(int64_t ticks) {
	for (size_t i = 0; i < m_points.size(); i++) {
		if (m_points[i].ticks == ticks) {
			m_points.erase(m_points.begin() + i);
			return true;
		}
	}
	return false;
}

bool TrackingPath::screen_position_at(int64_t ticks, double* u, double* v) const {
	SmoothCurve curve(2);
	const double origin = (double)m_points.front().ticks;
	for (size_t i = 0; i < m_points.size(); i++)
		curve.add((double)m_points[i].ticks - origin, m_points[i].u, m_points[i].v, 0.0);
	curve.finish();
	double out[3];
	curve.at((double)ticks - origin, out);
	*u = out[0];
	*v = out[1];
	return true;
}

bool TrackingPath::oriented_position_at(int64_t ticks, const CameraLens& lens, const CameraOrientations& orientations,
	double* u, double* v) const {
	SmoothCurve curve(3);
	const double origin = (double)m_points.front().ticks;
	for (size_t i = 0; i < m_points.size(); i++) {
		Quat then;
		orientations.at(m_points[i].ticks, &then);
		const Vec3 direction = rotate(then, lens.ray_for(m_points[i].u, m_points[i].v));
		curve.add((double)m_points[i].ticks - origin, direction.x, direction.y, direction.z);
	}
	curve.finish();
	double out[3];
	curve.at((double)ticks - origin, out);
	const Vec3 direction = normalized(Vec3(out[0], out[1], out[2]));
	Quat now;
	orientations.at(ticks, &now);
	return lens.project(rotate(conjugate(now), direction), u, v);
}

bool TrackingPath::position_at(int64_t ticks, const CameraLens& lens, const CameraOrientations& orientations,
	double* u, double* v) const {
	if (m_points.empty())
		return false;
	if (ticks <= m_points.front().ticks) {
		*u = m_points.front().u;
		*v = m_points.front().v;
		return true;
	}
	if (ticks >= m_points.back().ticks) {
		*u = m_points.back().u;
		*v = m_points.back().v;
		return true;
	}
	if (m_orientation_aware && !orientations.empty())
		return oriented_position_at(ticks, lens, orientations, u, v);
	return screen_position_at(ticks, u, v);
}

bool TrackingPath::trim_left(int64_t ticks, const CameraLens& lens, const CameraOrientations& orientations) {
	if (m_points.empty() || ticks > m_points.back().ticks)
		return false;
	if (ticks <= m_points.front().ticks)
		return true;
	double u = 0.0;
	double v = 0.0;
	if (!position_at(ticks, lens, orientations, &u, &v))
		return false;
	size_t cut = 0;
	while (cut < m_points.size() && m_points[cut].ticks < ticks)
		cut++;
	m_points.erase(m_points.begin(), m_points.begin() + cut);
	if (m_points.empty() || m_points.front().ticks != ticks)
		m_points.insert(m_points.begin(), PathPoint(ticks, u, v));
	return true;
}

bool TrackingPath::trim_right(int64_t ticks, const CameraLens& lens, const CameraOrientations& orientations) {
	if (m_points.empty() || ticks < m_points.front().ticks)
		return false;
	if (ticks >= m_points.back().ticks)
		return true;
	double u = 0.0;
	double v = 0.0;
	if (!position_at(ticks, lens, orientations, &u, &v))
		return false;
	while (!m_points.empty() && m_points.back().ticks > ticks)
		m_points.pop_back();
	if (m_points.empty() || m_points.back().ticks != ticks)
		m_points.push_back(PathPoint(ticks, u, v));
	return true;
}

}  // namespace tracking
