#pragma once

// The Camera Data (debug) section of the left panel: what the camera wrote into the video file about itself (see camera_metadata.h),
// for the frame on screen. Each of the file's metadata streams is shown as a tree of its fields, with the values and, where known,
// the names. Click a number to plot that field over the whole file (the red line is where the playhead is); the values come from a
// thread of its own, which starts at the playhead, so the part being looked at fills in first and the program carries on while a long
// file is read; once the whole stream has been read it is kept, so the next field plotted is there at once. At the top are the handful of values most worth having at hand (frame, time, ISO, shutter, colour temperature,
// accelerometer, zoom), repeated from the tree. It is for finding out what is in there and what is worth
// using, not part of editing: nothing here is saved, and it does nothing at all while the section is closed or an export runs.

#include <string>
#include <vector>

#include "camera_metadata.h"
#include "proto_wire.h"

class MediaContainerMgr;

class CameraPanel {
public:
	explicit CameraPanel(MediaContainerMgr* media);

	// Draws the section, as part of the window the caller has open.
	void draw();

private:
	void draw_key_values();
	void draw_key_row(const char* label, const std::vector<int>& path, const char* format, double scale);
	void draw_accelerometer_rows();
	void key_row_response(const std::vector<int>& path);
	void draw_stream(int stream);
	void draw_fields(int stream, const std::vector<WireField>& fields, std::vector<int>* path);
	void draw_leaf(int stream, const WireField& field, int count, const std::vector<WireField>& all, size_t first, std::vector<int>* path);
	void draw_plot(double seconds, int media);
	void start_plot(int stream, const std::vector<int>& path, int media);
	// Takes what the thread has read since last time, into the arrays and the pixel columns.
	void take_new_values(int width, double span);
	void add_to_columns(size_t from, double span);
	void rebuild_columns(int width, double span);
	size_t nearest_value(double when) const;

	MediaContainerMgr*     m_media;
	CameraMetadata         m_meta;
	int                    m_open_media;           // The media file m_meta has open (-1: none).
	std::vector<WireField> m_fields[2];            // The sample on show, of each stream,
	double                 m_begins[2];            // and when it begins (-1: none yet).

	CameraPackets          m_packets[2];           // Every packet of each stream (once asked for), so any field can be plotted quickly.
	CameraSeries           m_series;               // Declared after the packets: it has to go first.
	int                    m_plot_stream;
	std::vector<int>       m_plot_path;            // The field being plotted (empty: none).
	std::string            m_plot_label;
	int                    m_plot_media;           // The media file the plot is of.
	double                 m_seconds;              // Where the playhead is in the file shown.

	// What has been read so far, in the order it came (the part from the playhead on, then the part before it).
	std::vector<double>    m_times;
	std::vector<float>     m_values;
	size_t                 m_second_part;          // Where in the arrays the part before the playhead begins (0: not reached yet).
	float                  m_low;                  // The lowest and highest value so far.
	float                  m_high;

	// The plot as pixel columns over the length of the file: per column, the lowest and highest value (columns with none have low > high).
	int                    m_columns_width;
	double                 m_columns_span;
	std::vector<float>     m_column_low;
	std::vector<float>     m_column_high;
};
