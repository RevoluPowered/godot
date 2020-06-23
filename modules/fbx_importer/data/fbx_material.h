/*************************************************************************/
/*  fbx_material.h					                                     */
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

#ifndef GODOT_FBX_MATERIAL_H
#define GODOT_FBX_MATERIAL_H

#include "core/reference.h"
#include "core/ustring.h"
#include "modules/fbx_importer/tools/import_utils.h"
#include "thirdparty/assimp/code/FBX/FBXDocument.h"

struct FBXMaterial : Reference {
protected:
	String material_name = String();
	mutable const Assimp::FBX::Material *material = nullptr;

public:
	void set_imported_material(const Assimp::FBX::Material *p_material) {
		material = p_material;
	}

	void import_material() {
		ERR_FAIL_COND(material == nullptr);
		// read the material file
		// is material two sided
		// read material name
		print_verbose("[material] material name: " + ImportUtils::FBXNodeToName(material->Name()));
		print_verbose("[material] shading model: " + String(material->GetShadingModel().c_str()));
	}
};

#endif // GODOT_FBX_MATERIAL_H
