#include "scan_gesture.h"

#include <math.h>

namespace scan_gesture {

float rate_for_offset(float drag, float native_rate) {
	float magnitude = fabsf(drag);
	if (magnitude < DEADZONE)
		return 0.0f;

	float max_rate = MAX_SPEED_MULTIPLIER * native_rate;
	float t = (magnitude - DEADZONE) / (MAX_DRAG - DEADZONE);
	if (t > 1.0f)
		t = 1.0f;
	// Exponential ramp: fine control just past the deadzone, but still able to reach a very fast
	// shuttle (potentially 1000+ units/sec) within a comfortable drag distance.
	float rate = MIN_RATE_PER_SECOND * powf(max_rate / MIN_RATE_PER_SECOND, t);
	return (drag < 0.0f) ? -rate : rate;
}

float drag_for_pixels(float pixels) {
	return 2.0f * pixels / REFERENCE_WIDTH_PIXELS;
}

}  // namespace scan_gesture
