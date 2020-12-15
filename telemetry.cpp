#define _USE_MATH_DEFINES
#include "math.h"
#include "telemetry.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <locale>
#include <iomanip>
#include <sstream>
#include <vector>

#include "env_config.h"

Space::Space(float tile_size) :
	m_poly_vects(),
	m_poly_bufs(),
	m_tile_size(tile_size),
	m_center_lat(nan("")),
	m_center_lon(nan(""))
{
}

Space::~Space() {
	// TODO(P0): destroy all the poly bufs
}

glm::vec2 Space::latlon_to_coords(float lat, float lon) {
	double dist_x = ((double)lon - m_center_lon) / 90.0 * 10000000.0;
	dist_x *= cos(M_PI / 2.0 * lat / 90.0);
	double dist_y = ((double)lat - m_center_lat) / 90.0 * 10000000.0;

	return glm::vec2(dist_x*4.0, dist_y*4.0);
}

Space::SpatialIndex Space::coords_to_gridref(const glm::vec2& xy_coords) {
	/*
	!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	*/
	//return SpatialIndex(0, 0);

	SpatialIndex retval;
	retval.first = xy_coords.x / m_tile_size;
	if (xy_coords.x < 0.0)
		retval.first -= 1;
	retval.second = xy_coords.y / m_tile_size;
	if (xy_coords.y < 0.0)
		retval.second -= 1;

	return retval;
}

Space::SpatialIndex Space::latlon_to_gridref(float lat, float lon) {

	return coords_to_gridref(latlon_to_coords(lat, lon));
}

float* Space::get_course_buffer(float lat, float lon, uint32_t& size) {
	SpatialIndex spatial_index = latlon_to_gridref(lat, lon);

	//TODO(P2) Better way to get the length that allows delete of poly_vects?
	size = m_poly_vects.find(spatial_index)->second->size(); 
	return m_poly_bufs.find(spatial_index)->second;
}

std::vector<float>* Space::get_poly_verts(const SpatialIndex& spatial_index) {
	std::vector<float>* vert_vect;
	auto poly_vect_find = m_poly_vects.find(spatial_index);
	if (poly_vect_find == m_poly_vects.end()) {
		vert_vect = new std::vector<float>;
		m_poly_vects[spatial_index] = vert_vect;
	}
	else {
		vert_vect = poly_vect_find->second;
	}
	return vert_vect;
}

void Space::push_verts(std::vector<float>* vert_vect, const glm::vec2& p1, const glm::vec2& right, float climb_rate, bool degenerate) {
	glm::vec2 vert0 = p1 - right;
	glm::vec2 vert1 = p1 + right;
	if (degenerate) {
		for (uint32_t i = 0; i < 4; i++) {
			// Push the last 4 values onto the vector again.
			vert_vect->push_back((*vert_vect)[vert_vect->size() - 4]);
		}
	}
	vert_vect->push_back(vert0.x);
	vert_vect->push_back(vert0.y);
	vert_vect->push_back(0.0);
	vert_vect->push_back(climb_rate);
	vert_vect->push_back(vert1.x);
	vert_vect->push_back(vert1.y);
	vert_vect->push_back(1.0);
	vert_vect->push_back(climb_rate);
}

void Space::polygonalize(TelemetryMgr& telemetry_mgr, float line_width) {
	if (telemetry_mgr.size() < 3)
		return;

	m_center_lat = telemetry_mgr[0].m_gps_lat;
	m_center_lon = telemetry_mgr[0].m_gps_lon;
	glm::vec2 last_right(1.0, 0.0);
	
	for (int i = 1; i < telemetry_mgr.size() - 1; i++) {
		bool trigger = true;
		if (i == 7533) {
			trigger = false;
		}
		glm::vec2 p0 = latlon_to_coords(telemetry_mgr[i-1].m_gps_lat, telemetry_mgr[i-1].m_gps_lon);
		SpatialIndex s0 = coords_to_gridref(p0);
		glm::vec2 p1 = latlon_to_coords(telemetry_mgr[i].m_gps_lat, telemetry_mgr[i].m_gps_lon);
		SpatialIndex s1 = coords_to_gridref(p1);
		glm::vec2 p2 = latlon_to_coords(telemetry_mgr[i+1].m_gps_lat, telemetry_mgr[i+1].m_gps_lon);
		SpatialIndex s2 = coords_to_gridref(p2);
		float climb_rate = (telemetry_mgr[i].m_alt[1] - telemetry_mgr[i-1].m_alt[1]) / TELEMETRY_FREQUENCY;
		glm::vec2 d01 = p1 - p0;
		glm::vec2 d12 = p2 - p1;
		glm::vec2 avg_direction = glm::length(d01) * d12 + glm::length(d12) * d01;
		glm::vec2 right;
		if (glm::length(avg_direction) < 0.00001) {
			right = last_right;
		} else {
			right = glm::normalize(glm::vec2(avg_direction.y, -avg_direction.x));
			last_right = right;
		}
		right *= line_width / 2.0;

		std::vector<float>* vert_vect0 = get_poly_verts(s0);
		std::vector<float>* vert_vect1 = get_poly_verts(s1);
		if (s0 != s1) {
			// The last pair of points went in a different gridref. We have to start over.
			// either this is a new gridref, in which case we just drop the points in, or it's
			// an old one, in which case, we have to generate a degenerate poly to move to the
			// new location.
			if (vert_vect0->size() > 0) {
				// Should only happen with the first vert. 
				climb_rate = 0.0;
				push_verts(vert_vect0, p1, right, climb_rate, false);
			}
			if (vert_vect1->size() == 0) {
				climb_rate = 0.25;
				push_verts(vert_vect1, p1, right, climb_rate, false);
			} else {
				climb_rate = 0.5;
				push_verts(vert_vect1, p1, right, climb_rate, true);
			}
		} else {
			climb_rate = 0.75;
			push_verts(vert_vect1, p1, right, climb_rate, false);
		}
	}

	for (auto i = m_poly_vects.begin(); i != m_poly_vects.end(); i++) {
		std::vector<float>* poly_vect = i->second;
		float* poly_buf = new float[poly_vect->size()];
		m_poly_bufs[i->first] = poly_buf;
		uint32_t index = 0;
		for (auto flt = poly_vect->begin(); flt != poly_vect->end(); flt++, index++) {
			poly_buf[index] = *flt;
		}
	}
}

TelemetrySlice::TelemetrySlice() :
	m_timestruct(),
	m_gps_lat(0.0),
	m_gps_lon(0.0),
	m_gps_alt(0.0),
	m_accel(),
	m_gyro(),
	m_pulse(false),
	m_speed_kts(0.0),
	m_course_deg(0.0),
	m_temperature{ 0.0, 0.0, 0.0 },
	m_pressure{ 0.0, 0.0, 0.0 },
	m_alt{ 0.0, 0.0, 0.0 }
{
}

TelemetrySlice::TelemetrySlice(const std::string& line) :
	m_pulse(false)
{
	char decimal_point, unit, lat_dir, lon_dir, open_brace, close_brace;
	int ms;

	std::istringstream SS(line);
	SS.imbue(std::locale("en_US.utf-8"));

	std::istringstream ss(line);
	ss.imbue(std::locale("en_US.utf-8"));
	ss >> std::get_time(&m_timestruct, "%y.%m.%d %H:%M:%S") >> decimal_point >> ms
		>> m_gps_lat >> lat_dir >> m_gps_lon >> lon_dir
		>> m_temperature[0] >> unit >> m_pressure[0] >> unit
		>> m_temperature[1] >> unit >> m_pressure[1] >> unit
		>> m_temperature[2] >> unit >> m_pressure[2] >> unit
		>> unit >> unit >> open_brace >> m_accel.x >> m_accel.y >> m_accel.z >> close_brace
		>> unit >> open_brace >> m_gyro.x >> m_gyro.y >> m_gyro.z >> close_brace
		>> m_speed_kts >> unit >> m_course_deg;

	if (ss.fail()) {
		throw "Unable to parse time.";
	}
	else {
		if (lat_dir == 'S')
			m_gps_lat = -m_gps_lat;
		if (lon_dir == 'W')
			m_gps_lon = -m_gps_lon;

		// Work out the difference between local and gmt. Remove twice that difference from the
		// computed time to counter the fact that the conversion is taking us backward. Since
		// the conversoin assumes we're giving it a local time and want a GMT out of it, it's 
		// going the wrong direction. This will have bugs, especially if you're eding video
		// on the other side of a DST change. 
		m_timestruct.tm_isdst = -1; 
		time_t local_epoch = mktime(&m_timestruct);     // Assumes that the input is LOCALTIME
		std::tm* new_timestruct = gmtime(&local_epoch); // Output provided assumed input was GMT.
		time_t diff_epoch = mktime(new_timestruct);
		local_epoch -= 2*(diff_epoch - local_epoch);
		std::tm* true_local = gmtime(&local_epoch);

		m_timestruct.tm_year = true_local->tm_year;
		m_timestruct.tm_mon = true_local->tm_mon;
		m_timestruct.tm_mday = true_local->tm_mday;
		m_timestruct.tm_hour = true_local->tm_hour;

		for (uint32_t i = 0; i < 3; i++) {
			if (m_pressure[i] < 1.0) {
				//TODO(P0): fix the fact that we're getting a pressure of 0 from the hardware and protect
				//          against it properly here.
				m_pressure[i] = 1013.25;
			}
			m_alt[i] = (pow(1013.25 / m_pressure[i], 1.0 / 5.257) - 1.0) * (m_temperature[i] + 273.15) / 0.0065;
		}
	}
}

float TelemetrySlice::course_rad() const {
	return 3.14159265f * (360.0f - m_course_deg) / 180.0f + 3.14159265f/2.0f;
}

TelemetryMgr::TelemetryMgr(const std::string& path) :
	m_space(100.0),
	m_parse_done(false)
{
	m_thread_running = true;
	m_parse_thread = std::thread(&TelemetryMgr::parse_telemetry_file, this, path);
}

void TelemetryMgr::parse_telemetry_file(const std::string& path) {
	//TODO(P2): User needs to know if there were issues with the file.
	//TODO(P2): Check that the file exists (and feedback to the user.)
	//TODO(P0): This accempts a non-existent file as being empty. Complain if file 1) DNE or 2) empty.
	std::fstream infile(path);
	std::string line;

	bool need_new_default = true;
	while (std::getline(infile, line)) {
		if (line[0] != '#') {
			m_telemetry.push_back(TelemetrySlice(line));
			if (need_new_default) {
				m_default_slice = m_telemetry[0];
				need_new_default = false;
			}
		}
	}

	m_space.polygonalize(*this, 10.0);

	//TODO(P2): Reap the thread when it's done. Better to do this when TelemetryMgr is receiving regular
	//          calls and can periodically look at the bool & attempt to join().
	m_parse_done = true;
}

const TelemetrySlice& TelemetryMgr::operator[](int64_t index) const {
	// if telemetry is empty, or we're indexing outside its bounds, return a thing that indicates no data available.
	if ((uint64_t)index >= m_telemetry.size() || index < 0)
		return m_default_slice;
	return m_telemetry[index];
}

float* TelemetryMgr::get_course_buffer(uint32_t& size) {
	auto slice = EnvConfig::instance().telemetry_slice();
	return m_space.get_course_buffer(slice.m_gps_lat, slice.m_gps_lon, size);
}

glm::vec2 TelemetryMgr::get_current_coords() {
	TelemetrySlice current_slice = (*this)[EnvConfig::instance().telemetry_index()];
	return m_space.latlon_to_coords(current_slice.m_gps_lat, current_slice.m_gps_lon);
}

Space::SpatialIndex TelemetryMgr::get_current_gridref() {
	TelemetrySlice current_slice = (*this)[EnvConfig::instance().telemetry_index()];
	return m_space.latlon_to_gridref(current_slice.m_gps_lat, current_slice.m_gps_lon);
}

void TelemetryMgr::tick() {
	if (m_thread_running and m_parse_done) {
		m_parse_thread.join();
		m_thread_running = false;
	}
}