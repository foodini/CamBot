#define _USE_MATH_DEFINES
#include "math.h"
#include "telemetry.h"
#include "util.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <locale>
#include <iomanip>
#include <sstream>
#include <vector>

#include "env_config.h"
#include "widget_base.h"

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
	m_pulse(false),
	m_gps_alt(-1000000.0f)
{
	char decimal_point, unit, lat_dir, lon_dir, open_brace, close_brace;
	int ms;

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

TelemetryMgr* TelemetryMgr::instance = nullptr;

TelemetryMgr::TelemetryMgr(const std::string& path, std::vector<WidgetBase*>* widgets) :
	m_parse_done(false)
{
	if (TelemetryMgr::instance != nullptr) {
		throw "Cannot create second TelemetryMgr";
	}
	TelemetryMgr::instance = this;

	m_thread_running = true;
	m_parse_thread = std::thread(&TelemetryMgr::parse_telemetry_file, this, path, widgets);
	//parse_telemetry_file(path, widgets);
}

TelemetryMgr::~TelemetryMgr() {
	TelemetryMgr::instance = nullptr;
}

void TelemetryMgr::parse_telemetry_file(const std::string& path, std::vector<WidgetBase*>* widgets) {
	//TODO(P2): User needs to know if there were issues with the file.
	//TODO(P2): Check that the file exists (and feedback to the user.)
	//TODO(P0): This accempts a non-existent file as being empty. Complain if file 1) DNE or 2) empty.
	std::fstream infile(path);
	std::string line;
	std::vector<std::string> lines;

	// I do this as two separate loops because the read is fast, but construction of the slices is slow and,
	// if I want to get polygonalization started early, I need a full count of the number of lines before I
	// start sending slices to Widgets for processing.
	while (std::getline(infile, line)) {
		if (line[0] != '#') {
			lines.push_back(line);
		}
	}

	uint32_t index = 0;
	for (auto i = lines.begin(); i != lines.end(); index++, i++) {
		TelemetrySlice slice = TelemetrySlice(*i);
		m_telemetry.push_back(slice);
		for (auto widget = widgets->begin(); widget != widgets->end(); widget++) {
			(*widget)->polygonalize(slice, index, lines.size());
		}
	}
	m_default_slice = m_telemetry[0];

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

void TelemetryMgr::tick() {
	if (m_thread_running and m_parse_done) {
		m_parse_thread.join();
		m_thread_running = false;
	}
}