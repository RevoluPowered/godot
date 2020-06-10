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

bool test_rotation(Vector3 vector, Assimp::FBX::Model::RotOrder rot_order) {
	Quat rotation = AssimpUtils::EulerToQuaternion(rot_order, vector);

	OS *os = OS::get_singleton();
	os->print("Expecting: %ls\n", (String(vector).c_str()));
	Vector3 result = (rotation.get_euler_xyz() * (180 / Math_PI));
	os->print("returned: %ls\n", (String(result).c_str()));

	if (result != vector) {
		os->printerr("Error deviation: %ls\n", (String((result - vector)).c_str()));
	}

	return result == vector;
}

bool test_1() {
	return test_rotation(
			Vector3(0, 0, 0),
			Assimp::FBX::Model::RotOrder_EulerXYZ);
}

bool test_2() {
	return test_rotation(
			Vector3(90, 0, 0),
			Assimp::FBX::Model::RotOrder_EulerXYZ);
}

bool test_3() {
	// possibly bad value to make test fail
	// 0.00770179601386189,4.93110418319702,6.8879861831665
	return test_rotation(
			Vector3(0.00770179601386189f, 4.93110418319702f, 6.8879861831665f),
			Assimp::FBX::Model::RotOrder_EulerXYZ);
}

typedef bool (*TestFunc)(void);

TestFunc test_funcs[] = {
	test_1,
	test_2,
	test_3,
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
