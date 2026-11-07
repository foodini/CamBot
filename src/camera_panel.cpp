#include "camera_panel.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "imgui/imgui.h"

#include "panel_section.h"

#include "file_io.h"
#include "media_container_manager.h"

static const float PLOT_HEIGHT = 120.0f;

static bool is_number_field(const WireField& field);

CameraPanel::CameraPanel(MediaContainerMgr* media)
	: m_media(media), m_open_media(-1), m_plot_stream(0), m_plot_media(-1), m_seconds(0.0), m_second_part(0), m_low(0.0f), m_high(0.0f),
	  m_columns_width(0), m_columns_span(0.0) {
	m_begins[0] = -1.0;
	m_begins[1] = -1.0;
}

void CameraPanel::draw() {
	if (!panel_section::header("Camera Data (debug)"))
		return;
	if (m_media->recording()) {
		ImGui::TextDisabled("Not shown while exporting.");
		return;
	}

	// Where in which file the frame on screen comes from.
	const FileTime where = m_media->edit_list().source_for(m_media->current_ticks());
	const AVRational tick = m_media->tick_time_base();
	const double seconds = (double)where.local_ticks * (double)tick.num / (double)tick.den;
	m_seconds = seconds;
	if (where.media != m_open_media) {
		m_open_media = where.media;
		m_series.stop();
		m_packets[0].stop();
		m_packets[1].stop();
		m_meta.open(m_media->media_path(where.media));
		m_begins[0] = -1.0;
		m_begins[1] = -1.0;
		m_fields[0].clear();
		m_fields[1].clear();
		if (!m_plot_path.empty())
			start_plot(m_plot_stream, m_plot_path, where.media);   // The same field, of this file.
	}

	ImGui::Text("%s   %.3f s", fileio::base_name(m_media->media_path(where.media)).c_str(), seconds);
	if (!m_meta.has(camera_metadata::STREAM_DJMD) && !m_meta.has(camera_metadata::STREAM_DBGI)) {
		ImGui::TextDisabled("This file has no camera metadata streams (the djmd and dbgi streams of DJI cameras).");
		return;
	}

	for (int stream = 0; stream < camera_metadata::STREAM_COUNT; stream++) {
		if (!m_meta.has(stream))
			continue;
		const std::string* bytes = nullptr;
		double begins = 0.0;
		if (m_meta.sample_at(stream, seconds, &bytes, &begins)) {
			if (begins != m_begins[stream]) {
				m_begins[stream] = begins;
				proto_wire::parse((const uint8_t*)bytes->data(), bytes->size(), &m_fields[stream]);
			}
		} else {
			m_begins[stream] = -1.0;
			m_fields[stream].clear();
		}
		if (stream == camera_metadata::STREAM_DJMD)
			draw_key_values();
		draw_stream(stream);
	}
	draw_plot(seconds, where.media);
}

// The first field at `path` (field numbers from the top message down), or null.
static const WireField* find_field(const std::vector<WireField>& fields, const int* path, size_t depth) {
	for (size_t i = 0; i < fields.size(); i++) {
		if (fields[i].number != path[0])
			continue;
		if (depth == 1)
			return &fields[i];
		if (fields[i].kind != WIRE_MESSAGE)
			continue;
		const WireField* found = find_field(fields[i].children, path + 1, depth - 1);
		if (found != nullptr)
			return found;
	}
	return nullptr;
}

// One of the key values: a number field of the djmd sample, shown as `format` of its value times `scale`. Click to plot, as in the tree.
void CameraPanel::draw_key_row(const char* label, const std::vector<int>& path, const char* format, double scale) {
	const WireField* field = find_field(m_fields[camera_metadata::STREAM_DJMD], path.data(), path.size());
	char value[64];
	if (field == nullptr || !is_number_field(*field)) {
		snprintf(value, sizeof(value), "(none)");
	} else {
		double number = 0.0;
		proto_wire::number_of(*field, &number);
		snprintf(value, sizeof(value), format, number * scale);
	}
	char text[160];
	snprintf(text, sizeof(text), "%s  =  %s", label, value);
	const bool plotted = !m_plot_path.empty() && m_plot_stream == camera_metadata::STREAM_DJMD && m_plot_path == path;
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (plotted)
		flags |= ImGuiTreeNodeFlags_Selected;
	ImGui::PushID(label);
	ImGui::TreeNodeEx("##key", flags, "%s", text);
	if (field != nullptr && is_number_field(*field))
		key_row_response(path);
	ImGui::PopID();
}

// Clicking the row just drawn plots its field; hovering says so.
void CameraPanel::key_row_response(const std::vector<int>& path) {
	if (ImGui::IsItemClicked())
		start_plot(camera_metadata::STREAM_DJMD, path, m_open_media);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Field %s of the djmd stream.\nClick to plot it.", camera_metadata::path_text(path).c_str());
}

// The accelerometer's three axes with their decimal points in a line, and the size of the three together (about 1 g at rest, more in a
// turn) on the Y line.
void CameraPanel::draw_accelerometer_rows() {
	const char* labels[3] = { "Accel X (+ forward)", "Accel Y (+ right)", "Accel Z (+ down; rest = -1)" };
	const std::vector<int> paths[3] = { { 3, 2, 10, 2 }, { 3, 2, 10, 3 }, { 3, 2, 10, 4 } };
	double value[3] = { 0.0, 0.0, 0.0 };
	bool have[3] = { false, false, false };
	for (int axis = 0; axis < 3; axis++) {
		const WireField* field = find_field(m_fields[camera_metadata::STREAM_DJMD], paths[axis].data(), paths[axis].size());
		have[axis] = field != nullptr && is_number_field(*field) && proto_wire::number_of(*field, &value[axis]);
	}
	// Where things go, as distances from the left of the window's content.
	const float row_left = ImGui::GetCursorPosX();
	const float label_left = row_left + ImGui::GetTreeNodeToLabelSpacing();
	const float equals_left = label_left + ImGui::CalcTextSize(labels[2]).x + ImGui::CalcTextSize(" ").x * 2.0f;
	const float whole_right = equals_left + ImGui::CalcTextSize("=  ").x + ImGui::CalcTextSize("-0").x;   // The right edge of the part before the point.
	for (int axis = 0; axis < 3; axis++) {
		ImGui::PushID(axis);
		const bool plotted = !m_plot_path.empty() && m_plot_stream == camera_metadata::STREAM_DJMD && m_plot_path == paths[axis];
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
		if (plotted)
			flags |= ImGuiTreeNodeFlags_Selected;
		if (axis == 1)
			flags |= ImGuiTreeNodeFlags_AllowOverlap;   // The size of the three is on this line, and is its own row to click.
		ImGui::TreeNodeEx("##accel", flags, "%s", labels[axis]);
		if (have[axis])
			key_row_response(paths[axis]);
		ImGui::SameLine(equals_left);
		ImGui::TextUnformatted("=");
		if (have[axis]) {
			char text[48];
			snprintf(text, sizeof(text), "%.3f", value[axis]);
			char* point = strchr(text, '.');
			std::string fraction = point != nullptr ? std::string(point) : std::string();
			if (point != nullptr)
				*point = 0;
			ImGui::SameLine(whole_right - ImGui::CalcTextSize(text).x);
			ImGui::TextUnformatted(text);
			ImGui::SameLine(0.0f, 0.0f);
			ImGui::Text("%s g", fraction.c_str());
			if (axis == 1 && have[0] && have[2]) {
				ImGui::SameLine(0.0f, ImGui::CalcTextSize(" ").x * 6.0f);
				char size_text[48];
				snprintf(size_text, sizeof(size_text), "|a| = %.3f g", sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]));
				const std::vector<int> size_path = { 3, 2, 10, camera_metadata::FIELD_ACCEL_MAGNITUDE };
				ImGui::PushID("size");
				ImGui::Selectable(size_text, !m_plot_path.empty() && m_plot_stream == camera_metadata::STREAM_DJMD && m_plot_path == size_path, ImGuiSelectableFlags_None, ImGui::CalcTextSize(size_text));
				key_row_response(size_path);
				ImGui::PopID();
			}
		} else {
			ImGui::SameLine(whole_right - ImGui::CalcTextSize("-0").x);
			ImGui::TextUnformatted("(none)");
		}
		ImGui::PopID();
	}
}

void CameraPanel::draw_key_values() {
	if (m_begins[camera_metadata::STREAM_DJMD] < 0.0)
		return;
	ImGui::SeparatorText("Key values");
	draw_key_row("Frame", { 3, 1, 1 }, "%.0f", 1.0);
	draw_key_row("Time (camera clock)", { 3, 1, 2 }, "%.3f s", 1e-6);
	draw_key_row("ISO", { 3, 2, 3, 1 }, "%.0f", 1.0);
	// The shutter speed is two numbers packed together: 1 and 200 for 1/200 s.
	const WireField* shutter = find_field(m_fields[camera_metadata::STREAM_DJMD], std::vector<int>({ 3, 2, 4, 1 }).data(), 4);
	int numerator = 0;
	int denominator = 0;
	char shutter_text[48] = "(none)";
	if (shutter != nullptr && sscanf(proto_wire::packed_varints(shutter->bytes).c_str(), "%d, %d", &numerator, &denominator) == 2)
		snprintf(shutter_text, sizeof(shutter_text), "%d/%d s", numerator, denominator);
	ImGui::Text("   Shutter  =  %s", shutter_text);
	draw_key_row("Color temperature", { 3, 2, 6, 1 }, "%.0f K", 1.0);
	draw_accelerometer_rows();
	draw_key_row("Zoom", { 3, 2, 5, 1 }, "%.2f", 1.0);
	draw_key_row("Barometric altitude", { 3, 2, 13, 2 }, "%.1f m", 0.1);
	draw_key_row("Air pressure", { 3, 2, 13, 3 }, "%.0f hPa", 1.0);
	ImGui::SeparatorText("All fields");
}

void CameraPanel::draw_stream(int stream) {
	ImGui::PushID(stream);
	char title[96];
	if (m_begins[stream] >= 0.0)
		snprintf(title, sizeof(title), "%s   (sample at %.3f s)###stream", camera_metadata::stream_name(stream), m_begins[stream]);
	else
		snprintf(title, sizeof(title), "%s   (no sample here)###stream", camera_metadata::stream_name(stream));
	ImGui::SetNextItemOpen(stream == camera_metadata::STREAM_DJMD, ImGuiCond_Once);
	if (ImGui::TreeNode(title)) {
		std::vector<int> path;
		draw_fields(stream, m_fields[stream], &path);
		ImGui::TreePop();
	}
	ImGui::PopID();
}

// A float as it is worth showing: all the digits it has, or, for bits that are not a sensible float, the integer they are.
static std::string number_text(const WireField& field) {
	char text[64];
	if (field.kind == WIRE_VARINT) {
		snprintf(text, sizeof(text), "%lld", (long long)(int64_t)field.bits);
	} else if (field.kind == WIRE_FIXED32) {
		const float value = proto_wire::float_of(field);
		if (proto_wire::plausible_float(value))
			snprintf(text, sizeof(text), "%.7g", (double)value);
		else
			snprintf(text, sizeof(text), "0x%08x", (unsigned)field.bits);
	} else {
		double value = 0.0;
		proto_wire::number_of(field, &value);
		const double as_double = proto_wire::double_of(field);
		if (value == as_double)
			snprintf(text, sizeof(text), "%.12g", value);
		else
			snprintf(text, sizeof(text), "%lld", (long long)(int64_t)field.bits);
	}
	return text;
}

static std::string value_text(int stream, const std::vector<int>& path, const WireField& field) {
	if (field.kind == WIRE_TEXT)
		return "\"" + field.bytes + "\"";
	if (field.kind == WIRE_BYTES) {
		if (field.bytes.empty())
			return "(empty)";
		char size[32];
		snprintf(size, sizeof(size), "  (%d bytes)", (int)field.bytes.size());
		const std::string packed = proto_wire::packed_varints(field.bytes);
		return (packed.empty() ? proto_wire::hex(field.bytes, 12) : packed) + size;
	}
	std::string text = number_text(field);
	if (field.kind == WIRE_VARINT) {
		const std::string meaning = camera_metadata::value_meaning(stream, path, (int64_t)field.bits);
		if (!meaning.empty())
			text += "  (" + meaning + ")";
	}
	return text;
}

static bool is_number_field(const WireField& field) {
	return field.kind == WIRE_VARINT || field.kind == WIRE_FIXED32 || field.kind == WIRE_FIXED64;
}

void CameraPanel::draw_fields(int stream, const std::vector<WireField>& fields, std::vector<int>* path) {
	size_t i = 0;
	while (i < fields.size()) {
		// A run of fields with the same number is one repeated field.
		size_t end = i + 1;
		while (end < fields.size() && fields[end].number == fields[i].number && fields[end].kind == fields[i].kind)
			end++;
		const int count = (int)(end - i);
		const WireField& field = fields[i];
		path->push_back(field.number);
		ImGui::PushID((int)i);
		if (field.kind == WIRE_MESSAGE) {
			const std::string name = camera_metadata::field_name(stream, *path);
			char label[160];
			snprintf(label, sizeof(label), "%d%s%s", field.number, name.empty() ? "" : "  ", name.c_str());
			if (count == 1) {
				if (ImGui::TreeNode(label)) {
					draw_fields(stream, field.children, path);
					ImGui::TreePop();
				}
			} else {
				char group[200];
				snprintf(group, sizeof(group), "%s   x%d", label, count);
				if (ImGui::TreeNode(group)) {
					for (size_t k = i; k < end; k++) {
						ImGui::PushID((int)k);
						char item[32];
						snprintf(item, sizeof(item), "[%d]", (int)(k - i));
						if (ImGui::TreeNode(item)) {
							draw_fields(stream, fields[k].children, path);
							ImGui::TreePop();
						}
						ImGui::PopID();
					}
					ImGui::TreePop();
				}
			}
		} else {
			draw_leaf(stream, field, count, fields, i, path);
		}
		ImGui::PopID();
		path->pop_back();
		i = end;
	}
}

void CameraPanel::draw_leaf(int stream, const WireField& field, int count, const std::vector<WireField>& all, size_t first, std::vector<int>* path) {
	const std::string name = camera_metadata::field_name(stream, *path);
	std::string text = value_text(stream, *path, field);
	if (count > 1) {
		// A repeated number: the first few of its values.
		text.clear();
		for (int k = 0; k < count && k < 4; k++)
			text += (k == 0 ? "" : ", ") + value_text(stream, *path, all[first + (size_t)k]);
		if (count > 4)
			text += ", ...";
	}
	char label[320];
	snprintf(label, sizeof(label), "%d%s%s  =  %s", field.number, name.empty() ? "" : "  ", name.c_str(), text.c_str());
	const bool plotted = !m_plot_path.empty() && m_plot_stream == stream && m_plot_path == *path;
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (plotted)
		flags |= ImGuiTreeNodeFlags_Selected;
	ImGui::TreeNodeEx("##leaf", flags, "%s", label);
	const bool numeric = is_number_field(field);
	if (numeric && ImGui::IsItemClicked())
		start_plot(stream, *path, m_open_media);
	if (ImGui::IsItemHovered()) {
		char tip[512];
		if (numeric) {
			double value = 0.0;
			proto_wire::number_of(field, &value);
			snprintf(tip, sizeof(tip), "Field %s\nas an integer: %lld (unsigned %llu, hex 0x%llx)\nas a float (32 bits): %.9g\nas a double (64 bits): %.12g\nClick to plot it.",
				camera_metadata::path_text(*path).c_str(), (long long)(int64_t)field.bits, (unsigned long long)field.bits, (unsigned long long)field.bits,
				(double)proto_wire::float_of(field), proto_wire::double_of(field));
		} else {
			snprintf(tip, sizeof(tip), "Field %s\n%d bytes: %s", camera_metadata::path_text(*path).c_str(), (int)field.bytes.size(), proto_wire::hex(field.bytes, 48).c_str());
		}
		ImGui::SetTooltip("%s", tip);
	}
}

void CameraPanel::start_plot(int stream, const std::vector<int>& path, int media) {
	m_plot_stream = stream;
	m_plot_path = path;
	m_plot_media = media;
	const std::string name = camera_metadata::field_name(stream, path);
	m_plot_label = std::string(camera_metadata::stream_name(stream)) + " " + camera_metadata::path_text(path) + (name.empty() ? "" : "  " + name);
	m_times.clear();
	m_values.clear();
	m_second_part = 0;
	m_low = FLT_MAX;
	m_high = -FLT_MAX;
	m_columns_width = 0;
	m_columns_span = 0.0;
	// The packets of the stream are read (from the playhead on) the first time; after that they are in memory.
	const std::string file = m_media->media_path(media);
	m_series.stop();
	if (!m_packets[stream].started() || m_packets[stream].file() != file)
		m_packets[stream].start(file, stream, m_seconds);
	m_series.start(&m_packets[stream], path, m_seconds);
}

// The pixel column a time falls in, for a plot `width` columns wide over `span` seconds.
static int column_for(double time, double span, int width) {
	int column = span > 0.0 ? (int)(time / span * (double)(width - 1) + 0.5) : 0;
	if (column < 0)
		column = 0;
	if (column >= width)
		column = width - 1;
	return column;
}

void CameraPanel::add_to_columns(size_t from, double span) {
	for (size_t i = from; i < m_values.size(); i++) {
		const float value = m_values[i];
		if (value < m_low)
			m_low = value;
		if (value > m_high)
			m_high = value;
		const size_t column = (size_t)column_for(m_times[i], span, m_columns_width);
		if (value < m_column_low[column])
			m_column_low[column] = value;
		if (value > m_column_high[column])
			m_column_high[column] = value;
	}
}

void CameraPanel::rebuild_columns(int width, double span) {
	m_columns_width = width;
	m_columns_span = span;
	m_column_low.assign((size_t)width, FLT_MAX);
	m_column_high.assign((size_t)width, -FLT_MAX);
	m_low = FLT_MAX;
	m_high = -FLT_MAX;
	add_to_columns(0, span);
}

void CameraPanel::take_new_values(int width, double span) {
	const size_t before = m_values.size();
	const double last_before = m_times.empty() ? 0.0 : m_times.back();
	m_series.take_new(&m_times, &m_values);
	if (m_values.size() == before && width == m_columns_width && span == m_columns_span)
		return;
	if (m_second_part == 0 && m_values.size() > before) {
		// The read has gone round to the start of the file when a time comes that is earlier than the one before it.
		double previous = last_before;
		for (size_t i = before; i < m_times.size(); i++) {
			if (m_times[i] < previous && i > 0) {
				m_second_part = i;
				break;
			}
			previous = m_times[i];
		}
	}
	if (width != m_columns_width || span != m_columns_span)
		rebuild_columns(width, span);
	else
		add_to_columns(before, span);
}

// The index of the value nearest `when` (the arrays are two runs, each forward in time).
size_t CameraPanel::nearest_value(double when) const {
	const size_t count = m_times.size();
	const size_t split = m_second_part > 0 ? m_second_part : count;
	size_t best = 0;
	double best_distance = -1.0;
	for (int run = 0; run < 2; run++) {
		const size_t first = run == 0 ? 0 : split;
		const size_t end = run == 0 ? split : count;
		if (first >= end)
			continue;
		size_t lower = first;
		size_t upper = end;
		while (lower < upper) {
			const size_t middle = lower + (upper - lower) / 2;
			if (m_times[middle] < when)
				lower = middle + 1;
			else
				upper = middle;
		}
		// `lower` is the first at or after `when`; the one before it may be nearer.
		for (int step = 0; step < 2; step++) {
			if (step == 0 && lower >= end)
				continue;
			if (step == 1 && lower == first)
				continue;
			const size_t candidate = step == 0 ? lower : lower - 1;
			const double distance = fabs(m_times[candidate] - when);
			if (best_distance < 0.0 || distance < best_distance) {
				best_distance = distance;
				best = candidate;
			}
		}
	}
	return best;
}

void CameraPanel::draw_plot(double seconds, int media) {
	ImGui::Separator();
	if (m_plot_path.empty()) {
		ImGui::TextDisabled("Click a number above to plot it over the whole file.");
		return;
	}
	ImGui::TextUnformatted(m_plot_label.c_str());
	ImGui::SameLine();
	if (ImGui::SmallButton("Clear")) {
		m_series.stop();
		m_plot_path.clear();
		return;
	}
	if (m_series.running()) {
		ImGui::SameLine();
		ImGui::TextDisabled("reading %d%%", (int)(m_series.progress() * 100.0f));
	}
	if (!m_series.running() && !m_series.error().empty()) {
		ImGui::TextDisabled("%s", m_series.error().c_str());
		return;
	}
	if (!m_series.running() && !m_series.note().empty()) {
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextDisabled("%s", m_series.note().c_str());
		ImGui::PopTextWrapPos();
	}

	const ImVec2 size(ImGui::GetContentRegionAvail().x, PLOT_HEIGHT);
	const int width = (int)size.x;
	if (width < 20)
		return;
	// The plot is as long as the file, from the first moment, whether or not it has been read yet.
	double span = m_series.duration();
	if (!m_times.empty()) {
		double latest = 0.0;
		for (size_t i = m_second_part > 0 ? m_second_part - 1 : m_times.size() - 1; i < m_times.size(); i++) {
			if (m_times[i] > latest)
				latest = m_times[i];
		}
		if (latest > span)
			span = latest;
	}
	take_new_values(width, span);

	const ImVec2 origin = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton("##camera_plot", size);
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(20, 20, 24, 255));

	if (!m_values.empty() && span > 0.0) {
		float low = m_low;
		float high = m_high;
		if (high - low < 1e-12f) {
			low -= 1.0f;
			high += 1.0f;
		}
		const float margin = 0.06f * (high - low);
		low -= margin;
		high += margin;
		const float top = origin.y + 4.0f;
		const float bottom = origin.y + size.y - 4.0f;
		const float scale = (bottom - top) / (high - low);
		const ImU32 line_color = IM_COL32(120, 200, 255, 255);

		if (m_values.size() <= (size_t)width * 2) {
			// Few values: the line through them (not across the place where the read went round to the start).
			for (size_t i = 0; i + 1 < m_values.size(); i++) {
				if (m_times[i + 1] <= m_times[i])
					continue;
				const float x0 = origin.x + (float)(m_times[i] / span) * (size.x - 1.0f);
				const float x1 = origin.x + (float)(m_times[i + 1] / span) * (size.x - 1.0f);
				draw->AddLine(ImVec2(x0, bottom - (m_values[i] - low) * scale), ImVec2(x1, bottom - (m_values[i + 1] - low) * scale), line_color);
			}
		} else {
			// Many: for each column the stretch from its lowest value to its highest.
			for (int column = 0; column < width; column++) {
				if (m_column_low[(size_t)column] > m_column_high[(size_t)column])
					continue;
				const float x = origin.x + (float)column;
				draw->AddLine(ImVec2(x, bottom - (m_column_low[(size_t)column] - low) * scale), ImVec2(x, bottom - (m_column_high[(size_t)column] - low) * scale - 1.0f), line_color);
			}
		}

		char text[48];
		snprintf(text, sizeof(text), "%.6g", (double)m_high);
		draw->AddText(ImVec2(origin.x + 4.0f, origin.y + 2.0f), IM_COL32(200, 200, 200, 255), text);
		snprintf(text, sizeof(text), "%.6g", (double)m_low);
		draw->AddText(ImVec2(origin.x + 4.0f, origin.y + size.y - ImGui::GetTextLineHeight() - 2.0f), IM_COL32(200, 200, 200, 255), text);
	}

	// Where the playhead is, if this is the file it is in.
	if (media == m_plot_media && span > 0.0) {
		const float x = origin.x + (float)(seconds / span) * (size.x - 1.0f);
		if (x >= origin.x && x <= origin.x + size.x)
			draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + size.y), IM_COL32(255, 60, 60, 255));
	}

	// The value under the mouse.
	if (ImGui::IsItemHovered() && span > 0.0 && !m_values.empty()) {
		const double when = (double)(ImGui::GetIO().MousePos.x - origin.x) / (double)(size.x - 1.0f) * span;
		const size_t at = nearest_value(when);
		ImGui::SetTooltip("%.3f s:  %.7g\nrange so far %.7g to %.7g", m_times[at], (double)m_values[at], (double)m_low, (double)m_high);
	}
}
