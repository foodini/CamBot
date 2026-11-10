// Tests of dji_orientation: the camera's recorded orientation turned into the lens-to-world rotation the paths use.

#include "dji_orientation.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); failures++; } } while (0)

static bool near3(const tracking::Vec3& a, double x, double y, double z) {
	return fabs(a.x - x) < 1e-9 && fabs(a.y - y) < 1e-9 && fabs(a.z - z) < 1e-9;
}

// Protobuf bytes, by hand: a length-delimited field, and a float field.
static std::string message_field(int number, const std::string& inner) {
	std::string out;
	out += (char)((number << 3) | 2);
	out += (char)inner.size();     // These test messages are all under 128 bytes.
	return out + inner;
}

static std::string float_field(int number, float value) {
	std::string out;
	out += (char)((number << 3) | 5);
	char bytes[4];
	memcpy(bytes, &value, 4);      // Little-endian, like the camera and the machines these run on.
	return out + std::string(bytes, 4);
}

static std::string orientation_message(float w, float x, float y, float z) {
	std::string quaternion = float_field(1, w) + float_field(2, x) + float_field(3, y) + float_field(4, z);
	return message_field(3, message_field(2, message_field(9, quaternion)));
}

static bool read(const std::string& message, tracking::Quat* q) {
	return dji_orientation::from_message((const uint8_t*)message.data(), message.size(), q);
}

static void test_axes() {
	// A body that is not turned: the camera's directions map to the body's, as a third of a turn about the diagonal says.
	const tracking::Quat identity(1, 0, 0, 0);
	const tracking::Quat q = dji_orientation::camera_to_world(identity);
	CHECK(near3(tracking::rotate(q, tracking::Vec3(1, 0, 0)), 0, 1, 0));   // Right is the body's Y.
	CHECK(near3(tracking::rotate(q, tracking::Vec3(0, 1, 0)), 0, 0, 1));   // Down is the body's Z.
	CHECK(near3(tracking::rotate(q, tracking::Vec3(0, 0, 1)), 1, 0, 0));   // Forward is the body's X.
}

static void test_body_turned() {
	// The body turned a quarter about the world's down (z) axis: forward (X) then points along Y.
	const double half = 3.14159265358979323846 / 4.0;
	const tracking::Quat body_to_world(cos(half), 0, 0, sin(half));
	const tracking::Quat q = dji_orientation::camera_to_world(body_to_world);
	CHECK(near3(tracking::rotate(q, tracking::Vec3(0, 0, 1)), 0, 1, 0));   // The lens axis.
	CHECK(near3(tracking::rotate(q, tracking::Vec3(0, 1, 0)), 0, 0, 1));   // Down stays down.
	CHECK(near3(tracking::rotate(q, tracking::Vec3(1, 0, 0)), -1, 0, 0));  // Right was Y, which has gone round to -X.
}

static void test_message() {
	tracking::Quat q;
	CHECK(read(orientation_message(1.0f, 0.0f, 0.0f, 0.0f), &q));
	CHECK(near3(tracking::rotate(q, tracking::Vec3(0, 0, 1)), 1, 0, 0));
	// Not quite of length one is fine (the floats are rounded); it is brought to length one.
	CHECK(read(orientation_message(1.02f, 0.0f, 0.0f, 0.0f), &q));
	CHECK(fabs(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z - 1.0) < 1e-9);
	// Not a rotation: refused.
	CHECK(!read(orientation_message(0.0f, 0.0f, 0.0f, 0.0f), &q));
	CHECK(!read(orientation_message(3.0f, 0.0f, 0.0f, 0.0f), &q));
	// Missing a number, or no orientation at all.
	const std::string partial = message_field(3, message_field(2, message_field(9, float_field(1, 1.0f) + float_field(2, 0.0f))));
	CHECK(!read(partial, &q));
	CHECK(!read(message_field(3, message_field(2, float_field(1, 1.0f))), &q));
	CHECK(!read(std::string(), &q));
}

int main() {
	test_axes();
	test_body_turned();
	test_message();
	if (failures == 0)
		printf("all ok\n");
	return failures == 0 ? 0 : 1;
}
