// Tests the schema-less protobuf reader (proto_wire.h) on hand-built messages.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "proto_wire.h"

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); g_failures++; } } while (0)

static void put_varint(std::string* out, uint64_t value) {
	while (value >= 0x80) {
		out->push_back((char)(0x80 | (value & 0x7f)));
		value >>= 7;
	}
	out->push_back((char)value);
}
static void put_key(std::string* out, int number, int wire_type) { put_varint(out, ((uint64_t)number << 3) | (uint64_t)wire_type); }
static void put_int(std::string* out, int number, uint64_t value) { put_key(out, number, 0); put_varint(out, value); }
static void put_float(std::string* out, int number, float value) { put_key(out, number, 5); out->append((const char*)&value, 4); }
static void put_double(std::string* out, int number, double value) { put_key(out, number, 1); out->append((const char*)&value, 8); }
static void put_bytes(std::string* out, int number, const std::string& bytes) { put_key(out, number, 2); put_varint(out, bytes.size()); out->append(bytes); }

static bool parse(const std::string& bytes, std::vector<WireField>* fields) {
	return proto_wire::parse((const uint8_t*)bytes.data(), bytes.size(), fields);
}

static void test_scalars_and_text() {
	std::string m;
	put_int(&m, 1, 300);
	put_int(&m, 2, (uint64_t)-326);          // A negative number is ten bytes of varint.
	put_float(&m, 3, 1.5f);
	put_double(&m, 4, 2.25);
	put_bytes(&m, 5, "DJI OsmoAction6");
	std::vector<WireField> f;
	CHECK(parse(m, &f));
	CHECK(f.size() == 5);
	CHECK(f[0].number == 1 && f[0].kind == WIRE_VARINT && f[0].bits == 300);
	double v = 0.0;
	CHECK(proto_wire::number_of(f[1], &v) && v == -326.0);
	CHECK(f[2].kind == WIRE_FIXED32 && proto_wire::float_of(f[2]) == 1.5f);
	CHECK(proto_wire::number_of(f[2], &v) && v == 1.5);
	CHECK(f[3].kind == WIRE_FIXED64 && proto_wire::double_of(f[3]) == 2.25);
	CHECK(proto_wire::number_of(f[3], &v) && v == 2.25);
	CHECK(f[4].kind == WIRE_TEXT && f[4].bytes == "DJI OsmoAction6");
	CHECK(!proto_wire::number_of(f[4], &v));
}

static void test_nested_and_repeated() {
	std::string inner1, inner2, m;
	put_float(&inner1, 1, 0.5f); put_float(&inner1, 2, 0.25f);
	put_float(&inner2, 1, 0.75f); put_float(&inner2, 2, 1.0f);
	std::string outer;
	put_bytes(&outer, 3, inner1);
	put_bytes(&outer, 3, inner2);
	put_int(&outer, 4, 7);
	put_bytes(&m, 2, outer);
	std::vector<WireField> f;
	CHECK(parse(m, &f));
	CHECK(f.size() == 1 && f[0].kind == WIRE_MESSAGE && f[0].children.size() == 3);
	CHECK(f[0].children[0].kind == WIRE_MESSAGE && f[0].children[0].children.size() == 2);

	std::vector<double> values;
	std::vector<int> path;
	path.push_back(2); path.push_back(3); path.push_back(1);
	proto_wire::collect(f, path, &values);
	CHECK(values.size() == 2 && values[0] == 0.5 && values[1] == 0.75);
	values.clear();
	path.back() = 2;
	proto_wire::collect(f, path, &values);
	CHECK(values.size() == 2 && values[0] == 0.25 && values[1] == 1.0);
	values.clear();
	path.clear(); path.push_back(2); path.push_back(4);
	proto_wire::collect(f, path, &values);
	CHECK(values.size() == 1 && values[0] == 7.0);
	values.clear();
	path.clear(); path.push_back(9);
	proto_wire::collect(f, path, &values);
	CHECK(values.empty());
	proto_wire::collect(f, std::vector<int>(), &values);
	CHECK(values.empty());
}

static void test_packed_and_bytes() {
	// Shutter speed 1/200 as packed varints (1, 200): not a message (its first byte is field 0), not text.
	std::string packed;
	put_varint(&packed, 1);
	put_varint(&packed, 200);
	std::string m;
	put_bytes(&m, 1, packed);
	put_bytes(&m, 2, std::string());                 // An empty field.
	std::vector<WireField> f;
	CHECK(parse(m, &f));
	CHECK(f.size() == 2);
	CHECK(f[0].kind == WIRE_BYTES && f[0].bytes == packed);
	CHECK(proto_wire::packed_varints(f[0].bytes) == "1, 200");
	CHECK(proto_wire::hex(f[0].bytes, 8) == "01c801");
	CHECK(proto_wire::hex(f[0].bytes, 2) == "01c8...");
	CHECK(f[1].kind == WIRE_BYTES && f[1].bytes.empty());
	CHECK(proto_wire::packed_varints(std::string()) == "");
}

static void test_garbage_is_refused() {
	std::vector<WireField> f;
	CHECK(!parse(std::string("\x08", 1), &f));                 // A key and no value.
	CHECK(!parse(std::string("\x12\x05" "ab", 4), &f));        // A length that runs off the end.
	CHECK(!parse(std::string("\x00\x01", 2), &f));             // Field number 0.
	CHECK(!parse(std::string("\x0b\x01", 2), &f));             // A group.
	CHECK(!parse(std::string("\x0d\x00\x00", 3), &f));         // Half a float.
	CHECK(parse(std::string(), &f) && f.empty());
	// A nested guess is not taken when the inner "field numbers" are absurd.
	std::string absurd;
	put_int(&absurd, 99999, 1);
	std::string m;
	put_bytes(&m, 1, absurd);
	CHECK(parse(m, &f) && f.size() == 1 && f[0].kind == WIRE_BYTES);
}

static void test_implausible_floats() {
	CHECK(proto_wire::plausible_float(0.0f) && proto_wire::plausible_float(-0.93f) && proto_wire::plausible_float(12800.0f));
	CHECK(!proto_wire::plausible_float(NAN));
	CHECK(!proto_wire::plausible_float(1e-30f) && !proto_wire::plausible_float(3e20f));
}

static void test_collect_from_bytes() {
	// What is got straight from the bytes is what is got by taking the message apart first.
	std::string inner1, inner2, outer, m;
	put_float(&inner1, 1, 0.5f); put_float(&inner1, 2, 0.25f);
	put_float(&inner2, 1, 0.75f); put_float(&inner2, 2, 1.0f);
	put_bytes(&outer, 3, inner1);
	put_bytes(&outer, 3, inner2);
	put_int(&outer, 4, (uint64_t)-5);
	put_double(&outer, 5, 2.5);
	put_bytes(&outer, 6, "text");
	put_int(&m, 1, 9);
	put_bytes(&m, 2, outer);
	put_bytes(&m, 2, outer);
	std::vector<WireField> f;
	CHECK(parse(m, &f));
	const int paths[6][3] = { { 2, 3, 1 }, { 2, 3, 2 }, { 2, 4, 0 }, { 2, 5, 0 }, { 1, 0, 0 }, { 2, 6, 0 } };
	const size_t lengths[6] = { 3, 3, 2, 2, 1, 2 };
	for (int i = 0; i < 6; i++) {
		const std::vector<int> path(paths[i], paths[i] + lengths[i]);
		std::vector<double> slow, fast;
		proto_wire::collect(f, path, &slow);
		proto_wire::collect_from_bytes((const uint8_t*)m.data(), m.size(), path, &fast);
		CHECK(slow == fast);
		if (i < 5)
			CHECK(!fast.empty());
	}
	std::vector<double> got;
	proto_wire::collect_from_bytes((const uint8_t*)m.data(), m.size(), std::vector<int>({ 2, 3, 1 }), &got);
	CHECK(got.size() == 4 && got[0] == 0.5 && got[1] == 0.75 && got[2] == 0.5 && got[3] == 0.75);
	// Bytes that run out give what came before.
	got.clear();
	proto_wire::collect_from_bytes((const uint8_t*)m.data(), m.size() - 3, std::vector<int>({ 1 }), &got);
	CHECK(got.size() == 1 && got[0] == 9.0);
	got.clear();
	proto_wire::collect_from_bytes((const uint8_t*)m.data(), 0, std::vector<int>({ 1 }), &got);
	CHECK(got.empty());
}

int main() {
	test_collect_from_bytes();
	test_scalars_and_text();
	test_nested_and_repeated();
	test_packed_and_bytes();
	test_garbage_is_refused();
	test_implausible_floats();
	printf(g_failures ? "FAILED\n" : "all ok\n");
	return g_failures;
}
