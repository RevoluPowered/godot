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

	const std::vector<std::string> valid_properties_to_read = {
		// Legacy Format
		"DiffuseColor", "Maya|DiffuseTexture",
		"AmbientColor",
		"EmissiveColor", "EmissiveFactor",
		"SpecularColor", "Maya|SpecularTexture",
		"TransparentColor",
		"ReflectionColor", "Maya|ReflectionMapTexture",
		"DisplacementColor",
		"NormalMap", "Maya|NormalTexture",
		"Bump", "3dsMax|Parameters|bump_map",
		"ShininessExponent",
		"TransparencyFactor",
		"Maya|FalloffTexture", // opacity

		// PBR Model
		"Maya|baseColor|file", "3dsMax|Parameters|base_color_map",
		"Maya|normalCamera|file",
		"Maya|emissionColor|file", "3dsMax|Parameters|emission_map",
		"Maya|metalness|file", "3dsMax|Parameters|metalness_map",
		"Maya|diffuseRoughness|file", "3dsMax|Parameters|roughness_map",

		// Legacy PBR / Stingray
		"Maya|TEX_color_map|file",
		"Maya|TEX_normal_map|file",
		"Maya|TEX_emissive_map|file",
		"Maya|TEX_metallic_map|file",
		"Maya|TEX_roughness_map|file",
		"Maya|TEX_ao_map|file",
	};



	void import_material() {
		ERR_FAIL_COND(material == nullptr);

		// read the material file
		// is material two sided
		// read material name
		print_verbose("[material] material name: " + ImportUtils::FBXNodeToName(material->Name()));
		// ignore this it's irrelevant right now
		// print_verbose("[material] shading model: " + String(material->GetShadingModel().c_str()));

		// string is the field 'DiffuseColor'
		for( std::pair<std::string, const Assimp::FBX::Texture*> texture : material->Textures())
		{
			// string texture name
			String texture_name = String(texture.second->Name().c_str());
			print_verbose("[" + String(texture.first.c_str()) + "] Texture name: " + texture_name);
		}

		//const std::string &uvSet = PropertyGet<std::string>(props, "UVSet", ok);

		// does anyone use this?
		for( auto layer_textures : material->LayeredTextures()) {

		}
	}
};

#endif // GODOT_FBX_MATERIAL_H
