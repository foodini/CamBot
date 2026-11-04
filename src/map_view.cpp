#include "map_view.h"

#include <math.h>

namespace {

const double PI = 3.14159265358979323846;
const double MAX_LATITUDE = 85.0511287798;
// How far to look for a sample with a position when the one asked for has none.
const int    NEAREST_FIX_SAMPLES = 10;
// The path has to have moved this far (in world units: about half a meter) between the samples either side
// of one for its direction to count as a heading.
const double MIN_HEADING_DISTANCE = 1.5e-8;
// Between points of a line that are farther apart than CURVE_ABOVE pixels (and not so far that it is
// really a jump, CURVE_BELOW), a curve through them is drawn with a point every CURVE_STEP pixels.
const float  CURVE_ABOVE = 6.0f;
const float  CURVE_BELOW = 400.0f;
const float  CURVE_STEP = 3.0f;

double clamp_double(double value, double low, double high) {
	return value < low ? low : (value > high ? high : value);
}

}  // namespace

namespace map_projection {

MapPoint from_lat_lon(double latitude_degrees, double longitude_degrees) {
	const double latitude = clamp_double(latitude_degrees, -MAX_LATITUDE, MAX_LATITUDE) * PI / 180.0;
	const double x = (longitude_degrees + 180.0) / 360.0;
	const double y = 0.5 - log(tan(PI / 4.0 + latitude / 2.0)) / (2.0 * PI);
	return MapPoint(x, y);
}

}  // namespace map_projection

MapViewport::MapViewport() : m_width(0.0f), m_height(0.0f), m_center(0.5, 0.5), m_zoom(map_zoom_limits::MIN_ZOOM) {
}

void MapViewport::set_size(float width, float height) {
	m_width = width > 0.0f ? width : 0.0f;
	m_height = height > 0.0f ? height : 0.0f;
}

double MapViewport::scale() const {
	return map_zoom_limits::TILE_PIXELS * pow(2.0, m_zoom);
}

void MapViewport::set_view(const MapPoint& center, double zoom) {
	m_center = center;
	m_zoom = clamp_double(zoom, map_zoom_limits::MIN_ZOOM, map_zoom_limits::MAX_ZOOM);
}

ScreenPoint MapViewport::to_screen(const MapPoint& point) const {
	const double pixels = scale();
	return ScreenPoint((float)((point.x - m_center.x) * pixels + (double)m_width * 0.5),
		(float)((point.y - m_center.y) * pixels + (double)m_height * 0.5));
}

MapPoint MapViewport::to_world(float screen_x, float screen_y) const {
	const double pixels = scale();
	return MapPoint(m_center.x + ((double)screen_x - (double)m_width * 0.5) / pixels,
		m_center.y + ((double)screen_y - (double)m_height * 0.5) / pixels);
}

bool MapViewport::in_view(const MapPoint& point, float margin) const {
	const ScreenPoint s = to_screen(point);
	return s.x >= -margin && s.x <= m_width + margin && s.y >= -margin && s.y <= m_height + margin;
}

bool MapViewport::in_inset(const MapPoint& point, double inset) const {
	const ScreenPoint s = to_screen(point);
	const float left = (float)(inset * (double)m_width);
	const float top = (float)(inset * (double)m_height);
	return s.x >= left && s.x <= m_width - left && s.y >= top && s.y <= m_height - top;
}

void MapViewport::zoom_about(float screen_x, float screen_y, double levels) {
	const MapPoint anchor = to_world(screen_x, screen_y);
	m_zoom = clamp_double(m_zoom + levels, map_zoom_limits::MIN_ZOOM, map_zoom_limits::MAX_ZOOM);
	const double pixels = scale();
	m_center.x = anchor.x - ((double)screen_x - (double)m_width * 0.5) / pixels;
	m_center.y = anchor.y - ((double)screen_y - (double)m_height * 0.5) / pixels;
}

void MapViewport::pan_pixels(float dx, float dy) {
	const double pixels = scale();
	m_center.x -= (double)dx / pixels;
	m_center.y -= (double)dy / pixels;
}

void MapViewport::center_on(const MapPoint& point) {
	m_center = point;
}

void MapViewport::fit(const MapPoint& low, const MapPoint& high, double margin) {
	m_center = MapPoint((low.x + high.x) * 0.5, (low.y + high.y) * 0.5);
	const double room_x = (double)m_width * (1.0 - 2.0 * margin);
	const double room_y = (double)m_height * (1.0 - 2.0 * margin);
	const double extent_x = high.x - low.x;
	const double extent_y = high.y - low.y;
	// A box this small is a point, whatever the pane.
	if (room_x <= 0.0 || room_y <= 0.0 || (extent_x < 1e-9 && extent_y < 1e-9)) {
		m_zoom = map_zoom_limits::MAX_FIT_ZOOM - 2.0;
		return;
	}
	double pixels = 1e30;
	if (extent_x > 1e-12)
		pixels = room_x / extent_x;
	if (extent_y > 1e-12 && room_y / extent_y < pixels)
		pixels = room_y / extent_y;
	double zoom = log(pixels / map_zoom_limits::TILE_PIXELS) / log(2.0);
	m_zoom = clamp_double(zoom, map_zoom_limits::MIN_ZOOM, map_zoom_limits::MAX_FIT_ZOOM);
}

bool MapViewport::keep_in_view(const MapPoint& point, double inset, float bias_x, float bias_y) {
	if (m_width <= 0.0f || m_height <= 0.0f)
		return false;
	const ScreenPoint s = to_screen(point);
	if (s.x < 0.0f || s.x > m_width || s.y < 0.0f || s.y > m_height) {
		// Put it where the middle of the allowed area is.
		const double pixels = scale();
		m_center.x = point.x - (double)bias_x / pixels;
		m_center.y = point.y - (double)bias_y / pixels;
		return true;
	}
	const float left = (float)(inset * (double)m_width) + bias_x;
	const float right = m_width - (float)(inset * (double)m_width) + bias_x;
	const float top = (float)(inset * (double)m_height) + bias_y;
	const float bottom = m_height - (float)(inset * (double)m_height) + bias_y;
	float dx = 0.0f;
	float dy = 0.0f;
	if (s.x < left)
		dx = s.x - left;
	else if (s.x > right)
		dx = s.x - right;
	if (s.y < top)
		dy = s.y - top;
	else if (s.y > bottom)
		dy = s.y - bottom;
	if (dx == 0.0f && dy == 0.0f)
		return false;
	// The view moves toward the point, so the map moves under it the other way.
	pan_pixels(-dx, -dy);
	return true;
}

MapTrace::MapTrace() : m_have_bounds(false) {
}

void MapTrace::clear() {
	m_points.clear();
	m_valid.clear();
	m_climb.clear();
	m_have_bounds = false;
}

void MapTrace::add(double latitude_degrees, double longitude_degrees, float climb_feet_per_minute) {
	m_climb.push_back(climb_feet_per_minute);
	const bool fix = !(latitude_degrees == 0.0 && longitude_degrees == 0.0) &&
		fabs(latitude_degrees) <= 90.0 && fabs(longitude_degrees) <= 180.0;
	if (!fix) {
		m_points.push_back(MapPoint());
		m_valid.push_back(0);
		return;
	}
	const MapPoint point = map_projection::from_lat_lon(latitude_degrees, longitude_degrees);
	m_points.push_back(point);
	m_valid.push_back(1);
	if (!m_have_bounds) {
		m_low = point;
		m_high = point;
		m_have_bounds = true;
		return;
	}
	if (point.x < m_low.x) m_low.x = point.x;
	if (point.y < m_low.y) m_low.y = point.y;
	if (point.x > m_high.x) m_high.x = point.x;
	if (point.y > m_high.y) m_high.y = point.y;
}

bool MapTrace::bounds(MapPoint* low, MapPoint* high) const {
	if (!m_have_bounds)
		return false;
	*low = m_low;
	*high = m_high;
	return true;
}

bool MapTrace::position_at(double sample, MapPoint* position) const {
	if (m_points.empty())
		return false;
	const int last = (int)m_points.size() - 1;
	if (sample < 0.0)
		sample = 0.0;
	if (sample > (double)last)
		sample = (double)last;
	const int index = (int)floor(sample);
	if (m_valid[index]) {
		if (index < last && m_valid[index + 1]) {
			const double fraction = sample - (double)index;
			position->x = m_points[index].x + (m_points[index + 1].x - m_points[index].x) * fraction;
			position->y = m_points[index].y + (m_points[index + 1].y - m_points[index].y) * fraction;
		} else {
			*position = m_points[index];
		}
		return true;
	}
	for (int step = 1; step <= NEAREST_FIX_SAMPLES; step++) {
		if (index - step >= 0 && m_valid[index - step]) {
			*position = m_points[index - step];
			return true;
		}
	}
	for (int step = 1; step <= NEAREST_FIX_SAMPLES; step++) {
		if (index + step <= last && m_valid[index + step]) {
			*position = m_points[index + step];
			return true;
		}
	}
	return false;
}

bool MapTrace::heading_at(double sample, float* degrees) const {
	MapPoint before;
	MapPoint after;
	if (!position_at(sample - 1.0, &before) || !position_at(sample + 1.0, &after))
		return false;
	const double dx = after.x - before.x;
	const double dy = after.y - before.y;
	if (dx * dx + dy * dy < MIN_HEADING_DISTANCE * MIN_HEADING_DISTANCE)
		return false;
	// World y grows to the south, so north is -y.
	double heading = atan2(dx, -dy) * 180.0 / PI;
	if (heading < 0.0)
		heading += 360.0;
	*degrees = (float)heading;
	return true;
}

void MapTrace::build_polyline(int first, int last, const MapViewport& view, float min_spacing, float margin, MapPolyline* out) const {
	out->clear();
	if (first < 0)
		first = 0;
	if (last > (int)m_points.size() - 1)
		last = (int)m_points.size() - 1;
	if (first > last)
		return;

	// Thin: keep a point only when it is far enough from the last one kept, and always the final one.
	std::vector<ScreenPoint> thin;
	std::vector<float> thin_climb;
	ScreenPoint latest;
	bool have_latest = false;
	bool last_kept = false;
	const float spacing_squared = min_spacing * min_spacing;
	for (int i = first; i <= last; i++) {
		if (!m_valid[i])
			continue;
		const ScreenPoint s = view.to_screen(m_points[i]);
		last_kept = false;
		if (have_latest) {
			const float dx = s.x - latest.x;
			const float dy = s.y - latest.y;
			if (dx * dx + dy * dy < spacing_squared)
				continue;
		}
		thin.push_back(s);
		thin_climb.push_back(m_climb[i]);
		latest = s;
		have_latest = true;
		last_kept = true;
	}
	if (!have_latest)
		return;
	// The loop only knows the last point was left out if it skipped it; put it back.
	if (!last_kept) {
		for (int i = last; i >= first; i--) {
			if (m_valid[i]) {
				thin.push_back(view.to_screen(m_points[i]));
				thin_climb.push_back(m_climb[i]);
				break;
			}
		}
	}

	// Cull: keep the points on or near the pane and their neighbors (so the lines that cross its edge
	// are still there), and start a new run wherever the line is away from the pane for a while.
	const int count = (int)thin.size();
	std::vector<char> near_pane(count);
	for (int k = 0; k < count; k++) {
		near_pane[k] = thin[k].x >= -margin && thin[k].x <= view.width() + margin &&
			thin[k].y >= -margin && thin[k].y <= view.height() + margin;
	}
	std::vector<ScreenPoint> kept;
	std::vector<float> kept_climb;
	std::vector<int> kept_run_ends;
	bool in_run = false;
	for (int k = 0; k < count; k++) {
		const bool keep = near_pane[k] || (k > 0 && near_pane[k - 1]) || (k + 1 < count && near_pane[k + 1]);
		if (keep) {
			kept.push_back(thin[k]);
			kept_climb.push_back(thin_climb[k]);
			in_run = true;
		} else if (in_run) {
			kept_run_ends.push_back((int)kept.size());
			in_run = false;
		}
	}
	if (in_run)
		kept_run_ends.push_back((int)kept.size());

	// Curve: between points far enough apart to show as corners, draw a Catmull-Rom curve through them
	// (using the points either side of the two to give it a direction), with a point every few pixels.
	int run_start = 0;
	for (size_t r = 0; r < kept_run_ends.size(); r++) {
		const int run_end = kept_run_ends[r];
		for (int k = run_start; k < run_end; k++) {
			out->points.push_back(kept[k]);
			out->climb.push_back(kept_climb[k]);
			if (k + 1 >= run_end)
				continue;
			const ScreenPoint p1 = kept[k];
			const ScreenPoint p2 = kept[k + 1];
			const ScreenPoint p0 = kept[k > run_start ? k - 1 : k];
			const ScreenPoint p3 = kept[k + 2 < run_end ? k + 2 : k + 1];
			const float dx = p2.x - p1.x;
			const float dy = p2.y - p1.y;
			const float length = sqrtf(dx * dx + dy * dy);
			if (length <= CURVE_ABOVE || length > CURVE_BELOW)
				continue;
			const int pieces = (int)ceilf(length / CURVE_STEP);
			for (int piece = 1; piece < pieces; piece++) {
				const float t = (float)piece / (float)pieces;
				const float t2 = t * t;
				const float t3 = t2 * t;
				const float x = 0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * t + (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 +
					(-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3);
				const float y = 0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t + (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 +
					(-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3);
				out->points.push_back(ScreenPoint(x, y));
				out->climb.push_back(kept_climb[k] + (kept_climb[k + 1] - kept_climb[k]) * t);
			}
		}
		out->run_ends.push_back((int)out->points.size());
		run_start = run_end;
	}
}

namespace map_colors {

namespace {

// The four control points of the video map's color scale, a cubic Bezier curve from strong sink to
// strong climb, whose red, green and blue are cut off at 0 and 1 (map_widget_1_course.frag).
const float CONTROL[4][3] = {
	{ 0.0f, 0.0f, 0.2f },
	{ 0.0f, 0.0f, 1.1f },
	{ 1.7f, 2.3f, 0.1f },
	{ 1.7f, -0.5f, 0.0f }
};

float clamp01(float value) {
	return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

}  // namespace

int climb_step(float climb, float range) {
	if (!(climb == climb))
		climb = 0.0f;
	if (!(range > 0.0f))
		range = 1.0f;
	const float t = clamp01((climb + range) / (2.0f * range));
	int step = (int)(t * (float)CLIMB_STEPS);
	if (step > CLIMB_STEPS - 1)
		step = CLIMB_STEPS - 1;
	return step;
}

float brightness(const MapColor& color) {
	return 0.299f * color.r + 0.587f * color.g + 0.114f * color.b;
}

MapColor step_color(int step, float min_brightness) {
	if (step < 0)
		step = 0;
	if (step > CLIMB_STEPS - 1)
		step = CLIMB_STEPS - 1;
	const float t = ((float)step + 0.5f) / (float)CLIMB_STEPS;
	const float u = 1.0f - t;
	const float weights[4] = { u * u * u, 3.0f * t * u * u, 3.0f * t * t * u, t * t * t };
	float rgb[3];
	for (int channel = 0; channel < 3; channel++) {
		float sum = 0.0f;
		for (int point = 0; point < 4; point++)
			sum += CONTROL[point][channel] * weights[point];
		rgb[channel] = clamp01(sum);
	}
	MapColor color(rgb[0], rgb[1], rgb[2]);
	const float level = brightness(color);
	if (level < min_brightness) {
		// Toward white by just enough.
		const float lift = (min_brightness - level) / (1.0f - level);
		color.r += (1.0f - color.r) * lift;
		color.g += (1.0f - color.g) * lift;
		color.b += (1.0f - color.b) * lift;
	}
	return color;
}

}  // namespace map_colors
