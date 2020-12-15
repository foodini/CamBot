#pragma once

#include <atomic>
#include <chrono>
#include <utility>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "glm/glm.hpp"

class TelemetryMgr;

struct pair_hash
{
	template <class T1, class T2>
	std::size_t operator() (const std::pair<T1, T2>& pair) const
	{
		return std::hash<T1>()(pair.first) ^ std::hash<T2>()(pair.second);
	}
};

class Space {
public:
	typedef std::pair<int32_t, int32_t> SpatialIndex;

	Space(float tile_size);
	~Space();
	float*              get_course_buffer(float lat, float lon, uint32_t& size);
	void                polygonalize(TelemetryMgr& telemetry_mgr, float line_width);
	glm::vec2           latlon_to_coords(float lat, float lon);
	SpatialIndex        latlon_to_gridref(float lat, float lon);

protected:
	SpatialIndex        coords_to_gridref(const glm::vec2& xy_coords);
	std::vector<float>* get_poly_verts(const SpatialIndex& spatial_index);
	void                push_verts(std::vector<float>* vert_vect, const glm::vec2& p1, const glm::vec2& right, float climb_rate, bool degenerate);

private:
	std::unordered_map<SpatialIndex, std::vector<float>*, pair_hash> m_poly_vects;
	std::unordered_map<SpatialIndex, float*, pair_hash> m_poly_bufs;

	float m_tile_size;
	float m_last_x;
	float m_last_y;
	float m_last_alt;
	float m_center_lat;
	float m_center_lon;
};

//TODO(P0): Anything that depends upon this will screw up if telemetry frames were dropped.
//          You should, when parsing the file, fill in any missing data (and warn if too much
//          is gone.)
#define TELEMETRY_FREQUENCY 10

class TelemetrySlice {
public:
	TelemetrySlice();
	TelemetrySlice(const std::string& line);

	int year()         const { return m_timestruct.tm_year + 1900; }
	int month()        const { return m_timestruct.tm_mon + 1; }
	int day()          const { return m_timestruct.tm_mday; }
	int hour()         const { return m_timestruct.tm_hour; }
	int minute()       const { return m_timestruct.tm_min; }
	int second()       const { return m_timestruct.tm_sec; }
	float course_rad() const;  // TODO(P1): precomp and store?
	float speed_mph()  const { return m_speed_kts * 1.15078f; }  // TODO(P1): precomp and store?
	float speed_kts()  const { return m_speed_kts; }
	float speed_kph()  const { return m_speed_kts * 1.852f; }  // TODO(P1): precomp and store?

	std::tm              m_timestruct;
	float                m_gps_lat;
	float                m_gps_lon;
	float                m_gps_alt;
	float                m_pressure[3];    // Left, Center, Right
	float                m_temperature[3]; // Left, Center, Right
	float                m_alt[3];
	glm::vec3            m_accel;
	glm::vec3            m_gyro;
	bool                 m_pulse;
	float                m_speed_kts;
	float                m_course_deg;
};

//TODO(P1): TelemetryMgr[x] should return the x-th TelemetrySlice - or a default one.
class TelemetryMgr {
public:
	TelemetryMgr(const std::string& path);
	~TelemetryMgr() {};

	void parse_telemetry_file(const std::string& path);

	//TODO(P1) get this behind an interface instead of public.
	const TelemetrySlice& operator[](int64_t index) const;
	uint32_t size() const { return m_telemetry.size(); }
	float* get_course_buffer(uint32_t& size);
	bool geometry_available() { return m_parse_done == true && m_thread_running == false; }

	glm::vec2 get_current_coords();
	std::pair<int32_t, int32_t> get_current_gridref();  // Really, only useful for debugging.

	void tick();

private:
	std::vector<TelemetrySlice> m_telemetry;
	Space                       m_space;
	std::thread                 m_parse_thread;
	std::atomic<bool>           m_parse_done;
	std::atomic<bool>           m_thread_running;
	TelemetrySlice              m_default_slice;
};