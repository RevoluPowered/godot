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

Vector3 ImportUtils::deg2rad(const Vector3 &p_rotation) {
	return p_rotation / 180.0 * Math_PI;
}

Vector3 ImportUtils::rad2deg(const Vector3 &p_rotation) {
	return p_rotation / Math_PI * 180.0;
}

Basis ImportUtils::EulerToBasis(Assimp::FBX::Model::RotOrder mode, const Vector3 &p_rotation) {
	Basis ret;

	// FBX is using intrinsic euler, we can convert intrinsic to extrinsic (the one used in godot
	// by simply invert its order: https://www.cs.utexas.edu/~theshark/courses/cs354/lectures/cs354-14.pdf
	switch (mode) {
		case Assimp::FBX::Model::RotOrder_EulerXYZ:
			ret.set_euler_zyx(p_rotation);
			break;

		case Assimp::FBX::Model::RotOrder_EulerXZY:
			ret.set_euler_yzx(p_rotation);
			break;

		case Assimp::FBX::Model::RotOrder_EulerYZX:
			ret.set_euler_xzy(p_rotation);
			break;

		case Assimp::FBX::Model::RotOrder_EulerYXZ:
			ret.set_euler_zxy(p_rotation);
			break;

		case Assimp::FBX::Model::RotOrder_EulerZXY:
			ret.set_euler_yxz(p_rotation);
			break;

		case Assimp::FBX::Model::RotOrder_EulerZYX:
			ret.set_euler_xyz(p_rotation);
			break;

		case Assimp::FBX::Model::RotOrder_SphericXYZ:
			// TODO do this.
			break;

		default:
			// If you land here, Please integrate all enums.
			CRASH_NOW_MSG("This is not unreachable.");
	}

	return ret;
}

Quat ImportUtils::EulerToQuaternion(Assimp::FBX::Model::RotOrder mode, const Vector3 &p_rotation) {
	return ImportUtils::EulerToBasis(mode, p_rotation);
}

Vector3 ImportUtils::BasisToEuler(Assimp::FBX::Model::RotOrder mode, const Basis &p_rotation) {

	// FBX is using intrinsic euler, we can convert intrinsic to extrinsic (the one used in godot
	// by simply invert its order: https://www.cs.utexas.edu/~theshark/courses/cs354/lectures/cs354-14.pdf
	switch (mode) {
		case Assimp::FBX::Model::RotOrder_EulerXYZ:
			return p_rotation.get_euler_zyx();

		case Assimp::FBX::Model::RotOrder_EulerXZY:
			return p_rotation.get_euler_yzx();

		case Assimp::FBX::Model::RotOrder_EulerYZX:
			return p_rotation.get_euler_xzy();

		case Assimp::FBX::Model::RotOrder_EulerYXZ:
			return p_rotation.get_euler_zxy();

		case Assimp::FBX::Model::RotOrder_EulerZXY:
			return p_rotation.get_euler_yxz();

		case Assimp::FBX::Model::RotOrder_EulerZYX:
			return p_rotation.get_euler_xyz();

		case Assimp::FBX::Model::RotOrder_SphericXYZ:
			// TODO
			return Vector3();

		default:
			// If you land here, Please integrate all enums.
			CRASH_NOW_MSG("This is not unreachable.");
			return Vector3();
	}
}

Vector3 ImportUtils::QuaternionToEuler(Assimp::FBX::Model::RotOrder mode, const Quat &p_rotation) {
	return BasisToEuler(mode, p_rotation);
}
