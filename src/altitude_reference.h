#pragma once

// Barometric altitude drifts slowly (a meter or two over a couple of hours), and the barometer
// alone only knows altitude relative to whatever the pressure was at the time -- so, for the
// video, the user can say what the true altitude was at the start and at the end of it, and
// the displayed altitude is the barometer plus an offset that moves linearly from the first
// reference's offset to the second's. This is deliberately NOT causal (the offset partway
// through the flight depends on the end reference); it's for presentation. The climb-rate
// filters keep working on the uncorrected barometer, which is what a real instrument has.

#include <stdint.h>

#define ALTITUDE_REF_UNSET (-99999.0f)

// What the user typed in (and what the project file stores), in feet -- the unit the overlay
// shows altitude in.
struct AltitudeReferences {
	float start_ft;
	float end_ft;
	bool  end_same_as_start;  // Land where you launched: use the start altitude for the end too.

	AltitudeReferences() : start_ft(ALTITUDE_REF_UNSET), end_ft(ALTITUDE_REF_UNSET), end_same_as_start(false) {}

	static bool is_set(float feet) { return feet > ALTITUDE_REF_UNSET + 1.0f; }
	static float feet_to_meters(float feet) { return feet / 3.28084f; }

	bool  has_start() const { return is_set(start_ft); }
	bool  has_end() const   { return end_same_as_start ? has_start() : is_set(end_ft); }
	float start_m() const   { return feet_to_meters(start_ft); }
	float end_m() const     { return feet_to_meters(end_same_as_start ? start_ft : end_ft); }

	bool operator==(const AltitudeReferences& other) const {
		return start_ft == other.start_ft && end_ft == other.end_ft && end_same_as_start == other.end_same_as_start;
	}
	bool operator!=(const AltitudeReferences& other) const { return !(*this == other); }
};

// What applying the references worked out, for the settings window to show.
struct AltitudeCorrectionInfo {
	bool  has_start;
	bool  has_end;
	float start_baro_m;    // Average barometer reading around the start reference.
	float end_baro_m;
	float start_offset_m;  // True altitude minus that average.
	float end_offset_m;

	AltitudeCorrectionInfo() : has_start(false), has_end(false), start_baro_m(0.0f), end_baro_m(0.0f), start_offset_m(0.0f), end_offset_m(0.0f) {}
	// How far the barometer wandered between the two references (meters); 0 unless both exist.
	float drift_m() const { return (has_start && has_end) ? (end_offset_m - start_offset_m) : 0.0f; }
};

// The offset to add to the barometer at a given sample index. With both references, it moves
// linearly between them and is held constant outside them; with one, it's constant.
class AltitudeOffsetModel {
public:
	AltitudeOffsetModel() { clear(); }

	void clear() {
		m_has_start = m_has_end = false;
		m_start_index = m_end_index = 0;
		m_start_offset = m_end_offset = 0.0f;
	}
	void set_start(int32_t index, float offset_m) { m_has_start = true; m_start_index = index; m_start_offset = offset_m; }
	void set_end(int32_t index, float offset_m)   { m_has_end = true;   m_end_index = index;   m_end_offset = offset_m; }
	bool active() const { return m_has_start || m_has_end; }

	float offset_at(int32_t index) const {
		if (m_has_start && m_has_end) {
			if (m_end_index <= m_start_index)
				return m_start_offset;
			float t = (float)(index - m_start_index) / (float)(m_end_index - m_start_index);
			t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
			return m_start_offset + t * (m_end_offset - m_start_offset);
		}
		if (m_has_start)
			return m_start_offset;
		if (m_has_end)
			return m_end_offset;
		return 0.0f;
	}

private:
	bool    m_has_start, m_has_end;
	int32_t m_start_index, m_end_index;
	float   m_start_offset, m_end_offset;
};
