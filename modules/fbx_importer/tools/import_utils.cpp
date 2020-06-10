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

Quat AssimpUtils::EulerToQuaternion(Assimp::FBX::Model::RotOrder mode, const Vector3 &p_rotation) {
	Vector3 rotation = p_rotation;
	// // we want to convert from rot order to ZXY
	Basis x = Basis(Vector3(Math::deg2rad(rotation.x), 0, 0));
	//x.set_euler_xyz(Vector3(Math::deg2rad(rotation.x), 0, 0));
	Basis y = Basis(Vector3(0, Math::deg2rad(rotation.y), 0));
	//y.set_euler_xyz(Vector3(0, Math::deg2rad(rotation.y), 0));
	Basis z = Basis(Vector3(0, 0, Math::deg2rad(rotation.z)));
	//z.set_euler_xyz(Vector3(0, 0, Math::deg2rad(rotation.z)));

	Quat result;
	// So we can theoretically convert calls
	switch (mode) {
		case Assimp::FBX::Model::RotOrder_EulerXYZ:
			result = z * y * x;
			break;
		case Assimp::FBX::Model::RotOrder_EulerXZY:
			result = y * z * x;
			break;
		case Assimp::FBX::Model::RotOrder_EulerYZX:
			result = x * z * y;
			break;
		case Assimp::FBX::Model::RotOrder_EulerYXZ:
			result = z * x * y;
			break;
		case Assimp::FBX::Model::RotOrder_EulerZXY:
			result = y * x * z;
			break;
		case Assimp::FBX::Model::RotOrder_EulerZYX:
			result = y * x * z;
			break;
		case Assimp::FBX::Model::RotOrder_SphericXYZ:
			result = z * y * x;
			break;
		default:
			result = z * y * x;
			break;
	}
	//print_verbose("euler input data:" + rotation);
	//print_verbose("euler to quaternion: " + (result.get_euler_xyz() * (180 / Math_PI)));

	return result;
}