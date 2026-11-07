#include "proto_wire.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace proto_wire {

static bool read_varint(const uint8_t* data, size_t size, size_t* at, uint64_t* value) {
	uint64_t result = 0;
	int shift = 0;
	while (*at < size) {
		const uint8_t byte = data[(*at)++];
		result |= (uint64_t)(byte & 0x7f) << shift;
		if ((byte & 0x80) == 0) {
			*value = result;
			return true;
		}
		shift += 7;
		if (shift > 63)
			return false;
	}
	return false;
}

static bool is_text(const std::string& bytes) {
	if (bytes.empty())
		return false;
	for (size_t i = 0; i < bytes.size(); i++) {
		const unsigned char c = (unsigned char)bytes[i];
		if (c < 32 || c > 126)
			return false;
	}
	return true;
}

static bool parse_at_depth(const uint8_t* data, size_t size, std::vector<WireField>* fields, int depth) {
	fields->clear();
	size_t at = 0;
	while (at < size) {
		uint64_t key = 0;
		if (!read_varint(data, size, &at, &key))
			return false;
		const int number = (int)(key >> 3);
		const int wire_type = (int)(key & 7);
		if (number == 0 || (key >> 3) > 0x1fffffff)
			return false;
		WireField field;
		field.number = number;
		if (wire_type == 0) {
			field.kind = WIRE_VARINT;
			if (!read_varint(data, size, &at, &field.bits))
				return false;
		} else if (wire_type == 5) {
			if (at + 4 > size)
				return false;
			uint32_t value = 0;
			memcpy(&value, data + at, 4);
			at += 4;
			field.kind = WIRE_FIXED32;
			field.bits = value;
		} else if (wire_type == 1) {
			if (at + 8 > size)
				return false;
			memcpy(&field.bits, data + at, 8);
			at += 8;
			field.kind = WIRE_FIXED64;
		} else if (wire_type == 2) {
			uint64_t length = 0;
			if (!read_varint(data, size, &at, &length) || length > size - at)
				return false;
			field.bytes.assign((const char*)data + at, (size_t)length);
			at += (size_t)length;
			field.kind = WIRE_BYTES;
			if (is_text(field.bytes)) {
				field.kind = WIRE_TEXT;
			} else if (!field.bytes.empty() && depth < 12) {
				std::vector<WireField> inner;
				if (parse_at_depth((const uint8_t*)field.bytes.data(), field.bytes.size(), &inner, depth + 1)) {
					bool sane = true;
					for (size_t i = 0; i < inner.size(); i++) {
						if (inner[i].number > MAX_GUESSED_FIELD_NUMBER)
							sane = false;
					}
					if (sane) {
						field.kind = WIRE_MESSAGE;
						field.children.swap(inner);
					}
				}
			}
		} else {
			return false;   // Groups (3, 4) and the unused types: not something this camera writes.
		}
		fields->push_back(field);
	}
	return true;
}

bool parse(const uint8_t* data, size_t size, std::vector<WireField>* fields) {
	return parse_at_depth(data, size, fields, 0);
}

float float_of(const WireField& field) {
	const uint32_t bits = (uint32_t)field.bits;
	float value = 0.0f;
	memcpy(&value, &bits, 4);
	return value;
}

double double_of(const WireField& field) {
	double value = 0.0;
	memcpy(&value, &field.bits, 8);
	return value;
}

bool plausible_float(float value) {
	if (value != value || isinf(value))
		return false;
	const float magnitude = fabsf(value);
	return magnitude == 0.0f || (magnitude > 1e-9f && magnitude < 1e12f);
}

static bool number_from_bits(WireKind kind, uint64_t bits, double* value) {
	if (kind == WIRE_VARINT) {
		*value = (double)(int64_t)bits;
		return true;
	}
	if (kind == WIRE_FIXED32) {
		const uint32_t low = (uint32_t)bits;
		float as_float = 0.0f;
		memcpy(&as_float, &low, 4);
		*value = (double)as_float;
		return *value == *value && !isinf(*value);
	}
	if (kind == WIRE_FIXED64) {
		double as_double = 0.0;
		memcpy(&as_double, &bits, 8);
		if (as_double == as_double && !isinf(as_double) && (as_double == 0.0 || (fabs(as_double) > 1e-30 && fabs(as_double) < 1e30))) {
			*value = as_double;
		} else {
			*value = (double)(int64_t)bits;
		}
		return true;
	}
	return false;
}

bool number_of(const WireField& field, double* value) {
	return number_from_bits(field.kind, field.bits, value);
}

static void collect_at(const std::vector<WireField>& fields, const std::vector<int>& path, size_t depth, std::vector<double>* values) {
	for (size_t i = 0; i < fields.size(); i++) {
		const WireField& field = fields[i];
		if (field.number != path[depth])
			continue;
		if (depth + 1 == path.size()) {
			double value = 0.0;
			if (number_of(field, &value))
				values->push_back(value);
		} else if (field.kind == WIRE_MESSAGE) {
			collect_at(field.children, path, depth + 1, values);
		}
	}
}

void collect(const std::vector<WireField>& fields, const std::vector<int>& path, std::vector<double>* values) {
	if (path.empty())
		return;
	collect_at(fields, path, 0, values);
}

static void collect_bytes_at(const uint8_t* data, size_t size, const std::vector<int>& path, size_t depth, std::vector<double>* values) {
	const bool last = depth + 1 == path.size();
	size_t at = 0;
	while (at < size) {
		uint64_t key = 0;
		if (!read_varint(data, size, &at, &key))
			return;
		const bool wanted = (key >> 3) == (uint64_t)path[depth];
		const int wire_type = (int)(key & 7);
		if (wire_type == 0) {
			uint64_t bits = 0;
			if (!read_varint(data, size, &at, &bits))
				return;
			double value = 0.0;
			if (wanted && last && number_from_bits(WIRE_VARINT, bits, &value))
				values->push_back(value);
		} else if (wire_type == 5) {
			if (at + 4 > size)
				return;
			uint32_t bits = 0;
			memcpy(&bits, data + at, 4);
			at += 4;
			double value = 0.0;
			if (wanted && last && number_from_bits(WIRE_FIXED32, bits, &value))
				values->push_back(value);
		} else if (wire_type == 1) {
			if (at + 8 > size)
				return;
			uint64_t bits = 0;
			memcpy(&bits, data + at, 8);
			at += 8;
			double value = 0.0;
			if (wanted && last && number_from_bits(WIRE_FIXED64, bits, &value))
				values->push_back(value);
		} else if (wire_type == 2) {
			uint64_t length = 0;
			if (!read_varint(data, size, &at, &length) || length > size - at)
				return;
			if (wanted && !last)
				collect_bytes_at(data + at, (size_t)length, path, depth + 1, values);
			at += (size_t)length;
		} else {
			return;   // Groups and the unused types: not something this camera writes.
		}
	}
}

void collect_from_bytes(const uint8_t* data, size_t size, const std::vector<int>& path, std::vector<double>* values) {
	if (!path.empty())
		collect_bytes_at(data, size, path, 0, values);
}

std::string hex(const std::string& bytes, size_t max_bytes) {
	std::string text;
	const size_t count = bytes.size() < max_bytes ? bytes.size() : max_bytes;
	for (size_t i = 0; i < count; i++) {
		char two[4];
		snprintf(two, sizeof(two), "%02x", (unsigned)(unsigned char)bytes[i]);
		text += two;
	}
	if (count < bytes.size())
		text += "...";
	return text;
}

std::string packed_varints(const std::string& bytes) {
	if (bytes.empty())
		return std::string();
	std::string text;
	size_t at = 0;
	const uint8_t* data = (const uint8_t*)bytes.data();
	int count = 0;
	while (at < bytes.size()) {
		uint64_t value = 0;
		if (!read_varint(data, bytes.size(), &at, &value))
			return std::string();
		char one[32];
		snprintf(one, sizeof(one), "%s%llu", count == 0 ? "" : ", ", (unsigned long long)value);
		text += one;
		if (++count >= 16 && at < bytes.size())
			return text + ", ...";
	}
	return text;
}

}  // namespace proto_wire
