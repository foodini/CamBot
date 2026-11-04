#pragma once

// The geometry of the map in the sync view: where a latitude and longitude land on the screen, how
// the view pans and zooms, and how the flight's trace is turned into a line that can be drawn
// quickly. Plain arithmetic with no OpenGL or ImGui in it, so it can be tested.
//
// Places are kept in "world" coordinates, the Web Mercator projection that web map tiles use,
// scaled so the whole world is 1 by 1: x runs from 0 at longitude -180 to 1 at +180, and y from 0
// in the far north to 1 in the far south (so it grows down the screen, like pixels do). That way
// a map tile layer can be put under the trace later without converting anything.
//
// The view is described the way tile maps do it: a center in world coordinates and a zoom level
// (fractional here), where a world 1 unit wide is 256 * 2^zoom pixels wide. Screen positions are
// counted in pixels from the top left corner of the map pane.

#include <vector>

struct MapPoint {
	double x;
	double y;
	MapPoint() : x(0.0), y(0.0) {}
	MapPoint(double world_x, double world_y) : x(world_x), y(world_y) {}
};

struct ScreenPoint {
	float x;
	float y;
	ScreenPoint() : x(0.0f), y(0.0f) {}
	ScreenPoint(float screen_x, float screen_y) : x(screen_x), y(screen_y) {}
};

namespace map_projection {

// Latitudes beyond +-85.0511 degrees (where the projection ends) are held there.
MapPoint from_lat_lon(double latitude_degrees, double longitude_degrees);

}  // namespace map_projection

namespace map_zoom_limits {

const double TILE_PIXELS = 256.0;
const double MIN_ZOOM = 2.0;
const double MAX_ZOOM = 19.0;      // The deepest level the tile servers have.
const double MAX_FIT_ZOOM = 17.0;  // Fitting a trace that hardly moves doesn't zoom in further than this.

}  // namespace map_zoom_limits

class MapViewport {
public:
	MapViewport();

	// The size of the pane in pixels. The center stays where it is.
	void   set_size(float width, float height);
	float  width() const { return m_width; }
	float  height() const { return m_height; }

	MapPoint center() const { return m_center; }
	double   zoom() const { return m_zoom; }
	// Pixels per world unit.
	double   scale() const;
	void     set_view(const MapPoint& center, double zoom);

	ScreenPoint to_screen(const MapPoint& point) const;
	MapPoint    to_world(float screen_x, float screen_y) const;
	// Whether the point is in the pane, or within `margin` pixels of it.
	bool        in_view(const MapPoint& point, float margin) const;
	// Whether the point is in the pane and not within the fraction `inset` of its width and height of an edge.
	bool        in_inset(const MapPoint& point, double inset) const;

	// Zooms by `levels` (positive is closer), keeping the place under (screen_x, screen_y) where it is.
	// Held between the zoom limits.
	void zoom_about(float screen_x, float screen_y, double levels);
	// Moves the map under the mouse: what was at a place on the screen is `dx`, `dy` pixels from it.
	void pan_pixels(float dx, float dy);
	void center_on(const MapPoint& point);
	// Shows everything from `low` to `high`, with the fraction `margin` of the pane's width and height left empty
	// at each edge. A box that is a single point is shown at a sensible zoom.
	void fit(const MapPoint& low, const MapPoint& high, double margin);
	// Moves the view as little as it takes to bring `point` inside the pane with the fraction `inset` of its
	// width and height held back at each edge, or, if the point isn't in the pane at all, puts it in the
	// middle. `bias_x` and `bias_y` (pixels) move that allowed area, and the middle, over by that much; a
	// point that is moving can be given room ahead of it that way. True if the view moved.
	bool keep_in_view(const MapPoint& point, double inset, float bias_x = 0.0f, float bias_y = 0.0f);

private:
	float    m_width;
	float    m_height;
	MapPoint m_center;
	double   m_zoom;
};

// The lines to draw for a stretch of the trace. Each run is a stretch that is all on or near the pane;
// run_ends[i] is where run i stops in `points` (and where run i + 1 starts). `climb` has the climb rate
// (feet a minute) at each point.
struct MapPolyline {
	std::vector<ScreenPoint> points;
	std::vector<float>       climb;
	std::vector<int>         run_ends;
	void clear() { points.clear(); climb.clear(); run_ends.clear(); }
};

// The colors of the path. This is the climb color scale of the video's map (map_widget_1_course.frag),
// so a color means the same in both: from dark blue in strong sink, through blue, to yellow, to red in
// strong climb, with `range` (feet a minute) each side of level flight spanning it.
struct MapColor {
	float r;
	float g;
	float b;
	MapColor() : r(0.0f), g(0.0f), b(0.0f) {}
	MapColor(float red, float green, float blue) : r(red), g(green), b(blue) {}
};

namespace map_colors {

// The scale is cut into this many steps, so a stretch of the path of one step's color is one line.
const int CLIMB_STEPS = 32;

int      climb_step(float climb, float range);
// The color of a step. Colors darker than `min_brightness` (0 to 1) are lightened, toward white, until
// they are that bright, so strong sink shows on a dark map.
MapColor step_color(int step, float min_brightness);
// The brightness (0 to 1) of a color, the way the eye weighs the three colors.
float    brightness(const MapColor& color);

}  // namespace map_colors

// The flight's path: one position for each telemetry sample, in order. Samples where the GPS had no
// fix hold a place but have no position.
class MapTrace {
public:
	MapTrace();

	void clear();
	// Adds the next sample. A latitude and longitude of exactly 0, 0 (or out of range) is no fix.
	void add(double latitude_degrees, double longitude_degrees, float climb_feet_per_minute = 0.0f);

	int             count() const { return (int)m_points.size(); }
	bool            valid(int index) const { return m_valid[index] != 0; }
	const MapPoint& point(int index) const { return m_points[index]; }
	float           climb(int index) const { return m_climb[index]; }
	// The box around every sample that has a position. False if there are none.
	bool            bounds(MapPoint* low, MapPoint* high) const;

	// Where the flight was at a (fractional) sample number, in between samples when both have a position.
	// A sample with no position takes the nearest one before it, or failing that after it, within a
	// second's worth of samples. False if there is none.
	bool position_at(double sample, MapPoint* position) const;

	// The way the flight was heading at a (fractional) sample number, in degrees clockwise from north, worked
	// out from the path itself: the direction from one sample before to one sample after (so it is centered
	// on the sample and neither leads nor lags it, which a heading the GPS receiver reports, already averaged
	// over a second or so, does). False if the path doesn't move enough there to say, as when standing still.
	bool heading_at(double sample, float* degrees) const;

	// The line for samples `first` to `last`, as it falls on `view`. Points closer than `min_spacing` pixels to
	// the one before are left out (the last one stays), and so are stretches more than `margin` pixels outside the pane.
	// Where the points that are left are far enough apart to show as corners, the line between them is drawn
	// as a curve through them, with points added.
	void build_polyline(int first, int last, const MapViewport& view, float min_spacing, float margin, MapPolyline* out) const;

private:
	std::vector<MapPoint> m_points;
	std::vector<char>     m_valid;
	std::vector<float>    m_climb;
	bool                  m_have_bounds;
	MapPoint              m_low;
	MapPoint              m_high;
};
