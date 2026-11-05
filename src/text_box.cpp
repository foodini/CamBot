#include "text_box.h"

TextStyle::TextStyle()
	: font(0), size(48.0f / 1080.0f), bold(false), italic(false), strike(false), align(TEXT_ALIGN_CENTER),
	  shadow_dx(2.0f / 1080.0f), shadow_dy(2.0f / 1080.0f), shadow_opacity(1.0f) {
	for (int i = 0; i < 3; i++) {
		color[i] = 1.0f;
		shadow_color[i] = 0.0f;
	}
}

bool TextStyle::operator==(const TextStyle& other) const {
	for (int i = 0; i < 3; i++) {
		if (color[i] != other.color[i] || shadow_color[i] != other.shadow_color[i])
			return false;
	}
	return font == other.font && size == other.size && bold == other.bold && italic == other.italic && strike == other.strike &&
		align == other.align && shadow_dx == other.shadow_dx && shadow_dy == other.shadow_dy && shadow_opacity == other.shadow_opacity;
}

TextBox::TextBox()
	: id(0), anchor(), fade_in_ticks(0), dwell_ticks(0), fade_out_ticks(0), text(), x(0.1f), y(0.75f), w(0.8f), h(0.15f), style() {}

bool TextBox::operator==(const TextBox& other) const {
	return id == other.id && anchor == other.anchor && fade_in_ticks == other.fade_in_ticks && dwell_ticks == other.dwell_ticks &&
		fade_out_ticks == other.fade_out_ticks && text == other.text && x == other.x && y == other.y && w == other.w && h == other.h &&
		style == other.style;
}

namespace text_box {

float alpha_at(const TextBox& box, int64_t ticks_from_start) {
	const int64_t total = box.total_ticks();
	if (ticks_from_start < 0 || ticks_from_start >= total)
		return 0.0f;
	if (ticks_from_start < box.fade_in_ticks)
		return (float)((double)ticks_from_start / (double)box.fade_in_ticks);
	const int64_t into_fade_out = ticks_from_start - (box.fade_in_ticks + box.dwell_ticks);
	if (into_fade_out >= 0 && box.fade_out_ticks > 0)
		return (float)(1.0 - (double)into_fade_out / (double)box.fade_out_ticks);
	return 1.0f;
}

TextBox default_box() {
	TextBox box;
	box.text = "Lorem Ipsum";
	return box;
}

}  // namespace text_box
