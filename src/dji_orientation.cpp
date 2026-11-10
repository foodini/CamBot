#include "dji_orientation.h"
#include "proto_wire.h"
#include <math.h>
#include <vector>

namespace dji_orientation {

tracking::Quat camera_to_world(const tracking::Quat& body_to_world) {
	const double TURN = 2.0 * 3.14159265358979323846 / 3.0;
	const tracking::Quat camera_to_body = tracking::from_axis_angle(tracking::Vec3(1.0, 1.0, 1.0), TURN);
	return tracking::multiply(body_to_world, camera_to_body);
}

bool from_message(const uint8_t* bytes, size_t size, tracking::Quat* camera_to_world_out) {
	double q[4];
	for (int i = 0; i < 4; i++) {
		std::vector<int> path;
		path.push_back(3);
		path.push_back(2);
		path.push_back(9);
		path.push_back(i + 1);
		std::vector<double> values;
		proto_wire::collect_from_bytes(bytes, size, path, &values);
		if (values.empty() || !(fabs(values[0]) < 2.0))
			return false;
		q[i] = values[0];
	}
	const double length = sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
	if (!(length > 0.95 && length < 1.05))
		return false;
	*camera_to_world_out = camera_to_world(tracking::Quat(q[0] / length, q[1] / length, q[2] / length, q[3] / length));
	return true;
}

}  // namespace dji_orientation
