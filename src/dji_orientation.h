#pragma once

#include "tracking_path.h"
#include <stddef.h>
#include <stdint.h>

// The camera's orientation as the DJI cameras record it (one quaternion in each frame's djmd message, see camera_metadata.h),
// turned into what the paths need (see tracking_path.h): the rotation from camera directions (x right, y down, z out along the
// lens axis) to world directions. The camera records the rotation of its body, whose axes are X forward, Y right and Z down, to
// a world whose z points down; the lens looks along the body's X. So the camera's directions are first turned into the body's
// (x to Y, y to Z, z to X: a third of a turn about the body's diagonal), then by the recorded rotation.

namespace dji_orientation {

// The rotation from camera directions to world directions, given the recorded rotation of the body.
tracking::Quat camera_to_world(const tracking::Quat& body_to_world);

// The orientation in one djmd message (the fields 3.2.9.1 to 3.2.9.4, w x y z), already turned by camera_to_world(). False when
// the message has no orientation, or one that is not a rotation (its four numbers are not of length one, within a few percent).
bool from_message(const uint8_t* bytes, size_t size, tracking::Quat* camera_to_world_out);

}  // namespace dji_orientation
