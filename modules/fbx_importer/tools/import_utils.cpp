/*************************************************************************/
/*  import_utils.cpp					                                 */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2020 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2020 Godot Engine contributors (cf. AUTHORS.md)    */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/
#include "import_utils.h"

Vector3 AssimpUtils::deg2rad(const Vector3 &p_rotation) {
	return p_rotation / 180.0 * Math_PI;
}

Vector3 AssimpUtils::rad2deg(const Vector3 &p_rotation) {
	return p_rotation / Math_PI * 180.0;
}

Basis AssimpUtils::EulerToBasis(Assimp::FBX::Model::RotOrder mode, const Vector3 &p_rotation) {
	real_t c, s;

	// Rotation around X axis
	c = Math::cos(p_rotation.x);
	s = Math::sin(p_rotation.x);
	const Basis x(1.0, 0.0, 0.0, 0.0, c, -s, 0.0, s, c);

	// Rotation around Y axis
	c = Math::cos(p_rotation.y);
	s = Math::sin(p_rotation.y);
	const Basis y(c, 0.0, s, 0.0, 1.0, 0.0, -s, 0.0, c);

	// Rotation around Z axis
	c = Math::cos(p_rotation.z);
	s = Math::sin(p_rotation.z);
	const Basis z(c, -s, 0.0, s, c, 0.0, 0.0, 0.0, 1.0);

	// Multiply the axis following the rotation order.
	switch (mode) {
		case Assimp::FBX::Model::RotOrder_EulerXYZ:
			return x * y * z;
		case Assimp::FBX::Model::RotOrder_EulerXZY:
			return x * z * y;
		case Assimp::FBX::Model::RotOrder_EulerYZX:
			return y * z * x;
		case Assimp::FBX::Model::RotOrder_EulerYXZ:
			return y * x * z;
		case Assimp::FBX::Model::RotOrder_EulerZXY:
			return z * x * y;
		case Assimp::FBX::Model::RotOrder_EulerZYX:
			return z * y * x;
		case Assimp::FBX::Model::RotOrder_SphericXYZ:
			// TODO do this.
			return Vector3();
		default:
			// If you land here, Please integrate all enums.
			CRASH_NOW_MSG("This is not unreachable.");
			return Vector3();
	}
}

Quat AssimpUtils::EulerToQuaternion(Assimp::FBX::Model::RotOrder mode, const Vector3 &p_rotation) {
	return AssimpUtils::EulerToBasis(mode, p_rotation);
}

Vector3 AssimpUtils::QuaternionToEuler(Assimp::FBX::Model::RotOrder mode, const Quat &p_rotation) {

	switch (mode) {
		case Assimp::FBX::Model::RotOrder_EulerXYZ:
			return p_rotation.get_euler_xyz();

		case Assimp::FBX::Model::RotOrder_EulerXZY: {

			// Euler angles in XZY convention.
			// See https://en.wikipedia.org/wiki/Euler_angles#Rotation_matrix
			//
			// rot =  cz*cy             -sz             cz*sy
			//        sx*sy+cx*cy*sz    cx*cz           cx*sz*sy-cy*sx
			//        cy*sx*sz          cz*sx           cx*cy+sx*sz*sy

			Basis rotation = p_rotation;

#ifdef MATH_CHECKS
			ERR_FAIL_COND_V(!rotation.is_rotation(), Vector3());
#endif
			Vector3 euler;
			real_t sz = rotation[0][1];
			if (sz < 1.0) {
				if (sz > -1.0) {
					euler.x = Math::atan2(rotation[2][1], rotation[1][1]);
					euler.y = Math::atan2(rotation[0][2], rotation[0][0]);
					euler.z = Math::asin(-sz);
				} else {
					// It's -1
					euler.x = -Math::atan2(rotation[1][2], rotation[2][2]);
					euler.y = 0.0;
					euler.z = Math_PI / 2.0;
				}
			} else {
				// It's 1
				euler.x = -Math::atan2(rotation[1][2], rotation[2][2]);
				euler.y = 0.0;
				euler.z = -Math_PI / 2.0;
			}
			return euler;
		}

		case Assimp::FBX::Model::RotOrder_EulerYZX:
			// TODO
			return Vector3();

		case Assimp::FBX::Model::RotOrder_EulerYXZ:
			return p_rotation.get_euler_yxz();

		case Assimp::FBX::Model::RotOrder_EulerZXY:
			// TODO
			return Vector3();

		case Assimp::FBX::Model::RotOrder_EulerZYX:
			// TODO
			return Vector3();

		case Assimp::FBX::Model::RotOrder_SphericXYZ:
			// TODO
			return Vector3();

		default:
			// If you land here, Please integrate all enums.
			CRASH_NOW_MSG("This is not unreachable.");
			return Vector3();
	}
}