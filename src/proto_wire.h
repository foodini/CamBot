#pragma once

// A reader for protobuf's wire format that needs no schema, for looking inside the camera's metadata streams (see
// camera_metadata.h), whose .proto files are not published. A message is a run of fields, each a number and a value, and the
// value is one of: a varint, 4 bytes, 8 bytes, or a length-delimited run of bytes. What the bytes of the last kind mean (text, a
// nested message, or packed numbers) is not in the data, so parse() guesses: printable ASCII is text, bytes that read as a whole
// message are one, and anything else is left as bytes. The guess can be wrong (a packed array can look like a message), which is
// why the raw bytes are always kept.

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

enum WireKind { WIRE_VARINT, WIRE_FIXED32, WIRE_FIXED64, WIRE_TEXT, WIRE_MESSAGE, WIRE_BYTES };

struct WireField {
	int                    number;     // The field's number in its message.
	WireKind               kind;
	uint64_t               bits;       // A varint's value; the 4 or 8 bytes of a fixed field, as an integer.
	std::string            bytes;      // The text, or the raw bytes of a message or of bytes (empty for numbers).
	std::vector<WireField> children;   // The fields of a WIRE_MESSAGE.
	WireField() : number(0), kind(WIRE_VARINT), bits(0) {}
};

namespace proto_wire {

// The highest field number a nested message may have for the guess to call it a message (numbers beyond it are almost always
// bytes that only happen to read as fields).
const int MAX_GUESSED_FIELD_NUMBER = 4096;

// Reads `size` bytes as one message into *fields. False (with *fields holding what was read) if they are not a whole, valid message.
bool parse(const uint8_t* data, size_t size, std::vector<WireField>* fields);

// The value of a number field as a double, as a plot wants it: a varint (as a signed number), a float, or a double. False for
// the other kinds, and for a 64-bit value that is not a usable double (it is then taken as an integer).
bool number_of(const WireField& field, double* value);
float  float_of(const WireField& field);       // The 4 bytes of a WIRE_FIXED32 as a float.
double double_of(const WireField& field);      // The 8 bytes of a WIRE_FIXED64 as a double.

// Whether a float read from 32 bits is likely to be what the bits mean (not NaN, not a tiny or enormous stray number).
bool plausible_float(float value);

// Every value of the number field at `path` (the field numbers from the top message down, e.g. 3, 2, 9, 1), in the order they come:
// a field that repeats, or that sits in a message that repeats, gives all of its values.
void collect(const std::vector<WireField>& fields, const std::vector<int>& path, std::vector<double>* values);

// The same as collect(), straight from the bytes of a message, without taking the whole of it apart first: many times faster, as nothing
// is copied or allocated but the values. The fields on the path are entered as messages whatever they look like. Where the bytes are not
// valid it stops, with what it has got so far.
void collect_from_bytes(const uint8_t* data, size_t size, const std::vector<int>& path, std::vector<double>* values);

// Bytes as hex, at most `max_bytes` of them (with "..." after if cut short).
std::string hex(const std::string& bytes, size_t max_bytes);

// The bytes read as packed varints ("1, 200"), or "" if they are not that.
std::string packed_varints(const std::string& bytes);

}  // namespace proto_wire
