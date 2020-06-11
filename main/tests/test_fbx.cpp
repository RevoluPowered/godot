/*************************************************************************/
/*  test_fbx.cpp                                                         */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2020 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2020 Godot Engine contributors (cf. AUTHORS.md).   */
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

#include "core/os/os.h"
#include "core/ustring.h"
#include <stdio.h>
#include <wchar.h>

#include "test_fbx.h"

#include <modules/fbx_importer/editor_scene_importer_fbx.h>

namespace TestFBX {

bool test_rotation(Vector3 deg_vector, Assimp::FBX::Model::RotOrder rot_order) {

	// Test phase
	const Vector3 rad_vector = ImportUtils::deg2rad(deg_vector);
	const Quat quat_rotation = ImportUtils::EulerToQuaternion(rot_order, rad_vector);
	// Convert back into rotation order.
	const Vector3 ro_rotation = ImportUtils::QuaternionToEuler(rot_order, quat_rotation);

	Vector3 deviation;
	for (int i = 0; i < 3; i += 1) {
		deviation[i] = fmod(rad_vector[i], Math_PI) - fmod(ro_rotation[i], Math_PI);
	}

	// Print phase
	OS *os = OS::get_singleton();
	switch (rot_order) {
		case Assimp::FBX::Model::RotOrder_EulerXYZ:
			os->print("Rotation order XYZ.");
			break;
		case Assimp::FBX::Model::RotOrder_EulerXZY:
			os->print("Rotation order XZY.");
			break;
		case Assimp::FBX::Model::RotOrder_EulerYZX:
			os->print("Rotation order YZX.");
			break;
		case Assimp::FBX::Model::RotOrder_EulerYXZ:
			os->print("Rotation order YXZ.");
			break;
		case Assimp::FBX::Model::RotOrder_EulerZXY:
			os->print("Rotation order ZXY.");
			break;
		case Assimp::FBX::Model::RotOrder_EulerZYX:
			os->print("Rotation order ZYX.");
			break;
		case Assimp::FBX::Model::RotOrder_SphericXYZ:
			os->print("Rotation order SphericXYZ.");
			break;
		default:
			os->print("Rotation order not supported!");
			return false;
	}
	os->print("\n");
	os->print("Original Rotation: %ls\n", String(deg_vector).c_str());
	os->print("Quaternion to rotation order: %ls\n", String(ImportUtils::rad2deg(ro_rotation)).c_str());
	os->printerr("Error deviation: %ls\n", (String(ImportUtils::rad2deg(deviation))).c_str());

	return deviation.length() < CMP_EPSILON;
}

bool test_1() {
	return test_rotation(
			Vector3(0, 0, 0),
			Assimp::FBX::Model::RotOrder_EulerXYZ);
}

bool test_2() {
	return test_rotation(
			Vector3(0, -30, 0),
			Assimp::FBX::Model::RotOrder_EulerXYZ);
}

bool test_3() {
	return test_rotation(
			Vector3(0.00770179601386189f, 4.93110418319702f, 6.8879861831665f),
			Assimp::FBX::Model::RotOrder_EulerXYZ);
}

bool test_4() {
	return test_rotation(
			Vector3(90, 60, 0),
			Assimp::FBX::Model::RotOrder_EulerXYZ);
}

bool test_5() {
	return test_rotation(
			Vector3(90, 60, 90),
			Assimp::FBX::Model::RotOrder_EulerXYZ);
}

bool test_6() {
	return test_rotation(
			Vector3(20, 0, 360),
			Assimp::FBX::Model::RotOrder_EulerXYZ);
}

bool test_7() {
	return test_rotation(
			Vector3(360, 360, 360),
			Assimp::FBX::Model::RotOrder_EulerXYZ);
}

bool test_8() {
	return test_rotation(
			Vector3(0.1, -50, -60),
			Assimp::FBX::Model::RotOrder_EulerXYZ);
}

bool test_9() {
	return test_rotation(
			Vector3(0.5, 50, 20),
			Assimp::FBX::Model::RotOrder_EulerXZY);
}

bool test_10() {
	return test_rotation(
			Vector3(0.5, 0, 90),
			Assimp::FBX::Model::RotOrder_EulerXZY);
}

bool test_11() {
	return test_rotation(
			Vector3(0.5, 0, -90),
			Assimp::FBX::Model::RotOrder_EulerXZY);
}

bool test_12() {
	return test_rotation(
			Vector3(0, 0, -30),
			Assimp::FBX::Model::RotOrder_EulerXZY);
}

typedef bool (*TestFunc)(void);

TestFunc test_funcs[] = {
	// XYZ
	test_1,
	test_2,
	test_3,
	test_4,
	test_5,
	test_6,
	test_7,
	test_8,

	// XZY
	test_9,
	test_10,
	test_11,
	test_12,

	// End
	0
};

MainLoop *test() {

	/** A character length != wchar_t may be forced, so the tests won't work */

	ERR_FAIL_COND_V(sizeof(CharType) != sizeof(wchar_t), NULL);

	int count = 0;
	int passed = 0;

	while (true) {
		if (!test_funcs[count])
			break;

		OS::get_singleton()->print("\n---------------------------------------------\n");
		OS::get_singleton()->print("[fbx] running test: %d\n", count + 1);
		bool pass = test_funcs[count]();
		if (pass)
			passed++;
		OS::get_singleton()->print("\t%s\n", pass ? "PASS" : "FAILED");
		count++;
	}

	OS::get_singleton()->print("\n\n\n");
	OS::get_singleton()->print("*************\n");
	OS::get_singleton()->print("***TOTALS!***\n");
	OS::get_singleton()->print("*************\n");

	OS::get_singleton()->print("Passed %i of %i tests\n", passed, count);

	return NULL;
}
} // namespace TestFBX
